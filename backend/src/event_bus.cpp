/**
 * Lollypop C++ - EventBus Implementation
 *
 * Thread-safe backend event delivery to the GTK main context.
 */

#include "event_bus.hpp"

#include <glibmm/main.h>

#include <iostream>

namespace lollypop {

EventBus::EventBus() {
    dispatcher_.connect([this] { on_drain(); });
}

EventBus::~EventBus() = default;

EventBus::HandlerId EventBus::subscribe(const std::string& type,
                                         Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    HandlerId id = next_id_++;
    handlers_[type].emplace_back(id, std::move(handler));
    return id;
}

void EventBus::unsubscribe(HandlerId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [type, handlers] : handlers_) {
        handlers.erase(
            std::remove_if(handlers.begin(), handlers.end(),
                           [id](const auto& entry) { return entry.first == id; }),
            handlers.end());
    }
}

void EventBus::publish_fifo(const Event& event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fifo_queue_.push_back(event);
    }
    dispatcher_.emit();
}

void EventBus::publish_conflated(const std::string& type, const Event& event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conflated_latest_[type] = event;
    }
    dispatcher_.emit();
}

void EventBus::publish_position(int64_t position_us,
                                std::chrono::steady_clock::time_point timestamp,
                                double playback_rate) {
    Event event;
    event.type = "position";
    event.event_class = EventClass::Conflated;
    event.payload = PositionUpdate{position_us, timestamp, playback_rate};
    publish_conflated(event.type, event);
}

void EventBus::drain() {
    std::deque<Event> fifo;
    std::unordered_map<std::string, Event> conflated;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        fifo.swap(fifo_queue_);
        conflated.swap(conflated_latest_);
    }

    // Deliver conflated (latest-value) events first, then FIFO events in
    // order. This gives the UI the freshest continuous values alongside the
    // ordered discrete events.
    for (auto& [type, event] : conflated) {
        deliver(event);
    }
    for (auto& event : fifo) {
        deliver(event);
    }
}

void EventBus::on_drain() {
    drain();
}

void EventBus::deliver(const Event& event) {
    std::vector<std::pair<HandlerId, Handler>> handlers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(event.type);
        if (it == handlers_.end()) return;
        // Copy the handler list so a handler that unsubscribes during delivery
        // does not invalidate our iteration.
        handlers = it->second;
    }
    for (const auto& [id, handler] : handlers) {
        try {
            handler(event);
        } catch (const std::exception& e) {
            std::cerr << "EventBus: handler threw: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "EventBus: handler threw unknown exception"
                      << std::endl;
        }
    }
}

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

}  // namespace lollypop
