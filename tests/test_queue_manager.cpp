/**
 * Lollypop C++ - QueueManager Tests
 *
 * Tests queue manipulation, next/previous navigation, shuffle, repeat,
 * play-next FIFO, cursor adjustment, and edge cases.
 */

#include <gtest/gtest.h>
#include "queue_manager.hpp"

#include <set>

using namespace lollypop;

class QueueManagerTest : public ::testing::Test {
protected:
    QueueManager qm;
};

TEST_F(QueueManagerTest, SetQueueAndIndex) {
    qm.set_queue({"a", "b", "c"}, 1);
    EXPECT_EQ(qm.current_index(), 1u);
    EXPECT_EQ(qm.queue().size(), 3u);
    EXPECT_FALSE(qm.is_empty());
}

TEST_F(QueueManagerTest, StartIndexClamped) {
    qm.set_queue({"a", "b"}, 10);  // start_index > size
    EXPECT_EQ(qm.current_index(), 1u);  // Clamped to last index
}

TEST_F(QueueManagerTest, EmptyQueue) {
    EXPECT_TRUE(qm.is_empty());
    EXPECT_FALSE(qm.next_index().has_value());
    EXPECT_FALSE(qm.previous_index().has_value());
}

TEST_F(QueueManagerTest, NextPreviousNoRepeat) {
    qm.set_queue({"a", "b", "c"}, 0);

    // next_index() advances cursor from 0 to 1.
    auto next = qm.next_index();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(*next, 1u);

    // previous_index() now advances cursor back from 1 to 0.
    auto prev = qm.previous_index();
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(*prev, 0u);

    // At index 0, previous_index() returns nullopt (no previous).
    prev = qm.previous_index();
    EXPECT_FALSE(prev.has_value());

    // Advance cursor to index 1 via set_current_index, then previous returns 0.
    qm.set_current_index(1);
    prev = qm.previous_index();
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(*prev, 0u);
}

TEST_F(QueueManagerTest, EndOfQueueNoRepeat) {
    qm.set_queue({"a", "b", "c"}, 2);  // Last index

    // next_index() at end returns nullopt, cursor stays at 2.
    auto next = qm.next_index();
    EXPECT_FALSE(next.has_value());

    // previous_index() advances cursor from 2 to 1.
    auto prev = qm.previous_index();
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(*prev, 1u);
}

TEST_F(QueueManagerTest, RepeatPlaylist) {
    qm.set_queue({"a", "b", "c"}, 0);
    qm.set_repeat(RepeatMode::Playlist);

    // Next from last wraps to first
    qm.set_queue({"a", "b", "c"}, 2);
    auto next = qm.next_index();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(*next, 0u);

    // Previous from first wraps to last
    qm.set_queue({"a", "b", "c"}, 0);
    auto prev = qm.previous_index();
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(*prev, 2u);
}

TEST_F(QueueManagerTest, RepeatTrack) {
    qm.set_queue({"a", "b", "c"}, 1);
    qm.set_repeat(RepeatMode::Track);

    // Next stays on current track
    auto next = qm.next_index();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(*next, 1u);

    // Previous stays on current track
    auto prev = qm.previous_index();
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(*prev, 1u);
}

TEST_F(QueueManagerTest, ShuffleNoImmediateRepeat) {
    qm.set_queue({"a", "b", "c", "d", "e"}, 0);
    qm.set_shuffle(true);

    // next_index() advances the shuffle cursor to the NEXT track.
    // Starting from index 0, we can visit the other 4 tracks.
    std::set<size_t> seen;
    for (int i = 0; i < 4; ++i) {
        auto next = qm.next_index();
        ASSERT_TRUE(next.has_value()) << "run " << i;
        EXPECT_LT(*next, 5u);
        seen.insert(*next);
    }
    // All 4 other tracks should have been visited exactly once.
    EXPECT_EQ(seen.size(), 4u);

    // After exhausting the shuffle order without repeat, next_index() returns
    // nullopt.
    EXPECT_FALSE(qm.next_index().has_value());
}

TEST_F(QueueManagerTest, EnqueuePlayNext) {
    qm.set_queue({"a", "b", "c"}, 0);
    qm.enqueue("next1", true);  // Play next
    qm.enqueue("next2", true);  // Play next (goes after next1)

    // "next2" should be played before "next1" (LIFO within play-next)
    auto next = qm.next_index();
    ASSERT_TRUE(next.has_value());
    // After current (index 0), play-next items come first
    EXPECT_EQ(*next, 1u);  // next2 inserted at index 1
}

TEST_F(QueueManagerTest, EnqueueRegular) {
    qm.set_queue({"a", "b"}, 0);
    qm.enqueue("c", false);  // Append to end

    EXPECT_EQ(qm.queue().size(), 3u);
}

TEST_F(QueueManagerTest, RemoveCurrent) {
    qm.set_queue({"a", "b", "c"}, 1);  // Current is "b"
    qm.remove(1);  // Remove current

    // After removing current at index 1, cursor should move to index 1
    // (which is now "c")
    EXPECT_EQ(qm.current_index(), 1u);
    EXPECT_EQ(qm.queue().size(), 2u);
}

TEST_F(QueueManagerTest, RemoveBeforeCursor) {
    qm.set_queue({"a", "b", "c"}, 2);  // Current is "c" at index 2
    qm.remove(0);  // Remove "a" before cursor

    EXPECT_EQ(qm.current_index(), 1u);  // Cursor decremented
    EXPECT_EQ(qm.queue().size(), 2u);
}

TEST_F(QueueManagerTest, MoveItem) {
    qm.set_queue({"a", "b", "c", "d"}, 0);
    qm.move(3, 0);  // Move "d" to front

    EXPECT_EQ(qm.queue()[0], "d");
    EXPECT_EQ(qm.current_index(), 1u);  // Cursor follows moved item
}

TEST_F(QueueManagerTest, ValidateIndex) {
    qm.set_queue({"a", "b"}, 0);
    EXPECT_TRUE(qm.validate_index(0));
    EXPECT_TRUE(qm.validate_index(1));
    EXPECT_FALSE(qm.validate_index(2));
    EXPECT_FALSE(qm.validate_index(100));
}

TEST_F(QueueManagerTest, Clear) {
    qm.set_queue({"a", "b", "c"}, 1);
    qm.clear();

    EXPECT_TRUE(qm.is_empty());
    EXPECT_EQ(qm.current_index(), 0u);
}

TEST_F(QueueManagerTest, DuplicateEntries) {
    // Same track can appear multiple times
    qm.set_queue({"a", "a", "b"}, 0);
    EXPECT_EQ(qm.queue().size(), 3u);
    EXPECT_TRUE(qm.validate_index(1));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
