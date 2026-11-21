/**
 * Lollypop C++ - EventBus Tests
 *
 * Tests FIFO ordering, conflated latest-value events, MPSC publishing,
 * handler management, and thread-safe main-context dispatch.
 */

#include <gtest/gtest.h>
#include "event_bus.hpp"

#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

using namespace lollypop;

class EventBusTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Helper to make a string payload event using ErrorEvent (has a message string)
static Event make_string_event(const std::string& type, const std::string& value,
                                EventClass ec = EventClass::Fifo) {
    Event event;
    event.type = type;
    event.event_class = ec;
    event.payload = ErrorEvent{value};
    return event;
}

static std::string get_string_payload(const Event& event) {
    return std::get<ErrorEvent>(event.payload).message;
}

TEST(EventBusTest, FifoOrdering) {
    std::vector<std::string> received;
    std::mutex mutex;

    (void)EventBus::instance().subscribe("fifo_test", [&](const Event& event) {
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(get_string_payload(event));
    });

    EventBus::instance().publish_fifo(make_string_event("fifo_test", "first"));
    EventBus::instance().publish_fifo(make_string_event("fifo_test", "second"));
    EventBus::instance().publish_fifo(make_string_event("fifo_test", "third"));

    EventBus::instance().drain();

    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], "first");
    EXPECT_EQ(received[1], "second");
    EXPECT_EQ(received[2], "third");
}

TEST(EventBusTest, ConflatedLatestWins) {
    std::string latest;
    std::mutex mutex;

    (void)EventBus::instance().subscribe("position", [&](const Event& event) {
        std::lock_guard<std::mutex> lock(mutex);
        latest = get_string_payload(event);
    });

    // Publish multiple conflated events - only the last should be delivered
    EventBus::instance().publish_conflated("position", make_string_event("position", "v1", EventClass::Conflated));
    EventBus::instance().publish_conflated("position", make_string_event("position", "v2", EventClass::Conflated));
    EventBus::instance().publish_conflated("position", make_string_event("position", "v3", EventClass::Conflated));

    EventBus::instance().drain();

    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ(latest, "v3");
}

TEST(EventBusTest, FifoAndConflatedIndependent) {
    std::vector<std::string> fifo_events;
    std::string conflated_event;
    std::mutex mutex;

    EventBus::instance().subscribe("mixed", [&](const Event& event) {
        std::lock_guard<std::mutex> lock(mutex);
        if (event.event_class == EventClass::Fifo) {
            fifo_events.push_back(get_string_payload(event));
        } else {
            conflated_event = get_string_payload(event);
        }
    });

    EventBus::instance().publish_fifo(make_string_event("mixed", "fifo1"));
    EventBus::instance().publish_conflated("mixed", make_string_event("mixed", "c1", EventClass::Conflated));
    EventBus::instance().publish_fifo(make_string_event("mixed", "fifo2"));
    EventBus::instance().publish_conflated("mixed", make_string_event("mixed", "c2", EventClass::Conflated));

    EventBus::instance().drain();

    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(fifo_events.size(), 2u);
    EXPECT_EQ(fifo_events[0], "fifo1");
    EXPECT_EQ(fifo_events[1], "fifo2");
    EXPECT_EQ(conflated_event, "c2");
}

TEST(EventBusTest, UnsubscribeStopsDelivery) {
    int count = 0;
    std::mutex mutex;

    auto id = EventBus::instance().subscribe("unsub_test", [&](const Event&) {
        std::lock_guard<std::mutex> lock(mutex);
        ++count;
    });

    EventBus::instance().publish_fifo(make_string_event("unsub_test", "x"));
    EventBus::instance().drain();

    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(count, 1);
    }

    EventBus::instance().unsubscribe(id);

    EventBus::instance().publish_fifo(make_string_event("unsub_test", "y"));
    EventBus::instance().drain();

    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(count, 1);  // Should not have increased
    }
}

TEST(EventBusTest, PositionUpdateConflation) {
    PositionUpdate latest;
    std::mutex mutex;

    (void)EventBus::instance().subscribe("position", [&](const Event& event) {
        std::lock_guard<std::mutex> lock(mutex);
        latest = std::get<PositionUpdate>(event.payload);
    });

    auto now = std::chrono::steady_clock::now();
    EventBus::instance().publish_position(1000000, now, 1.0);   // 1 second
    EventBus::instance().publish_position(2000000, now, 1.0);   // 2 seconds
    EventBus::instance().publish_position(3000000, now, 1.0);   // 3 seconds - latest wins

    EventBus::instance().drain();

    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ(latest.position_us, 3000000LL);
    EXPECT_DOUBLE_EQ(latest.playback_rate, 1.0);
}

TEST(EventBusTest, MpscPublishing) {
    std::atomic<int> received_count{0};
    std::mutex mutex;

    (void)EventBus::instance().subscribe("mpsc_test", [&](const Event&) {
        std::lock_guard<std::mutex> lock(mutex);
        ++received_count;
    });

    // Publish from multiple threads (simulating MPSC)
    std::vector<std::thread> threads;
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([]() {
            for (int i = 0; i < 100; ++i) {
                EventBus::instance().publish_fifo(
                    make_string_event("mpsc_test", "x"));
            }
        });
    }
    for (auto& th : threads) th.join();

    EventBus::instance().drain();

    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ(received_count, 500);
}

TEST(EventBusTest, UnknownEventTypeNoCrash) {
    bool called = false;

    EventBus::instance().subscribe("known_type", [&](const Event&) { called = true; });

    // Publish to a type with no subscribers - should not crash
    EventBus::instance().publish_fifo(make_string_event("unknown_type", "x"));
    EventBus::instance().drain();

    EXPECT_FALSE(called);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
