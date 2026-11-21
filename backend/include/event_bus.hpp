/**
 * Lollypop C++ - EventBus
 *
 * Thread-safe backend event delivery to the GTK main context.
 *
 * Two event classes (plan.md Section 9):
 *   - FIFO: ordered delivery (state changes, track changes, errors,
 *     queue mutations, player appearance/disappearance, scan completion).
 *   - Conflated: latest value wins per type (position, spectrum frame,
 *     buffering percent). A slow UI cannot build a backlog.
 *
 * All handlers are invoked on the GTK main context via a Glib::Dispatcher.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <glibmm/dispatcher.h>

namespace lollypop {

struct PositionUpdate {
    int64_t position_us = 0;
    std::chrono::steady_clock::time_point timestamp;
    double playback_rate = 1.0;
};

struct SpectrumFrame {
    std::vector<float> magnitudes;
};

struct StateChange {
    bool playing = false;
};

struct TrackChange {
    std::string track_id;
};

struct ErrorEvent {
    std::string message;
};

struct QueueMutation {};
struct ScanFinished {
    bool success = false;
    bool cancelled = false;
};

struct PlayerAppeared {
    std::string name;
};
struct PlayerDisappeared {
    std::string name;
};

using EventPayload = std::variant<
    PositionUpdate,
    SpectrumFrame,
    StateChange,
    TrackChange,
    ErrorEvent,
    QueueMutation,
    ScanFinished,
    PlayerAppeared,
    PlayerDisappeared,
    std::monostate>;

enum class EventClass {
    Fifo,       // ordered, every event delivered
    Conflated   // latest value per type wins
};

struct Event {
    std::string type;
    EventClass event_class = EventClass::Fifo;
    EventPayload payload;
};

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;
    using HandlerId = uint64_t;

    EventBus();
    ~EventBus();

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Subscribe to an event type. Returns a handler id for unsubscription.
    HandlerId subscribe(const std::string& type, Handler handler);
    void unsubscribe(HandlerId id);

    // Publish a FIFO event (ordered delivery, every event).
    void publish_fifo(const Event& event);

    // Publish a conflated event (latest value per type wins).
    void publish_conflated(const std::string& type, const Event& event);

    // Convenience: publish a conflated position update.
    void publish_position(int64_t position_us,
                          std::chrono::steady_clock::time_point timestamp,
                          double playback_rate);

    // Drain all queued events. Safe to call from any thread; handlers run
    // on the GTK main context.
    void drain();

    static EventBus& instance();

private:
    std::mutex mutex_;
    std::deque<Event> fifo_queue_;
    std::unordered_map<std::string, Event> conflated_latest_;
    std::unordered_map<std::string,
        std::vector<std::pair<HandlerId, Handler>>> handlers_;
    HandlerId next_id_ = 1;

    Glib::Dispatcher dispatcher_;

    void on_drain();
    void deliver(const Event& event);
};

}  // namespace lollypop
