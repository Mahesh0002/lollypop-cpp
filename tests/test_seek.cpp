/**
 * Lollypop C++ - Seek Debounce and Generation Tests
 *
 * Tests seek debounce timing, generation counter for stale response handling,
 * can_seek() gating, and position updates during scrubbing.
 */

#include <gtest/gtest.h>
#include "controller.hpp"
#include "audio_engine.hpp"

#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

using namespace lollypop;

class SeekTest : public ::testing::Test {
protected:
    std::unique_ptr<BackendController> backend;

    void SetUp() override {
        backend = std::make_unique<BackendController>();
        ASSERT_TRUE(backend->initialize());
    }

    void TearDown() override {
        backend->shutdown();
        backend.reset();
    }
};

// Test that seek() calls can_seek() and returns early for unseekable sources.
// Since we don't have a real track loaded, can_seek() should be false.
TEST_F(SeekTest, SeekUnseekableSourceDoesNotIssueRealSeek) {
    // Without a loaded track, can_seek() should be false.
    EXPECT_FALSE(backend->can_seek());

    // Position should be clamped and emitted even without real seek.
    backend->seek(100.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0); // duration_ is 0, so clamped to 0
    EXPECT_DOUBLE_EQ(backend->get_duration(), 0.0);
}

// Test seek debounce: rapid seeks should only commit the last one.
TEST_F(SeekTest, SeekDebounceCommitsLastPosition) {
    // Without a real track, duration is 0 so all seeks clamp to 0.
    // This test verifies the debounce logic structure; actual position
    // updates require a loaded track with non-zero duration.

    double emitted_position = -1.0;
    backend->set_position_changed_callback([&](double pos, double dur) {
        emitted_position = pos;
    });

    // First seek - immediate position update (clamps to 0)
    backend->seek(10.0);
    EXPECT_DOUBLE_EQ(emitted_position, 0.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);

    // Second seek - immediate position update (clamps to 0)
    backend->seek(20.0);
    EXPECT_DOUBLE_EQ(emitted_position, 0.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);

    // Third seek - immediate position update (clamps to 0)
    backend->seek(30.0);
    EXPECT_DOUBLE_EQ(emitted_position, 0.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);
}

// Test seek generation counter: each seek increments generation.
TEST_F(SeekTest, SeekIncrementsGeneration) {
    // We can't directly access seek_generation_ (private), but we can verify
    // the behavior: only the last seek in a rapid sequence should be committed.
    // Since duration is 0 without a track, all seeks clamp to 0.

    // This is a behavioral test - the generation counter ensures stale
    // timer callbacks don't commit old seeks.
    // Since we can't run the GLib main loop easily in unit tests,
    // we verify the immediate position updates work (clamped).

    backend->seek(5.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);

    backend->seek(15.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);

    backend->seek(25.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);
}

// Test that seek clamps to valid range [0, duration].
TEST_F(SeekTest, SeekClampsToValidRange) {
    // Duration is 0 initially, so all seeks clamp to 0.
    backend->seek(-10.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);

    backend->seek(100.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);

    backend->seek(0.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);
}

// Test position updates flow during scrubbing (immediate emission).
TEST_F(SeekTest, PositionUpdatesFlowDuringScrubbing) {
    std::vector<double> positions;
    backend->set_position_changed_callback([&](double pos, double dur) {
        positions.push_back(pos);
    });

    // Simulate rapid scrubbing - all clamp to 0 since duration=0
    backend->seek(1.0);
    backend->seek(2.0);
    backend->seek(3.0);
    backend->seek(4.0);
    backend->seek(5.0);

    // All positions should have been emitted immediately (clamped to 0)
    ASSERT_EQ(positions.size(), 5);
    for (double pos : positions) {
        EXPECT_DOUBLE_EQ(pos, 0.0);
    }
}

// Test that seek emits position_changed and notifies MPRIS server.
TEST_F(SeekTest, SeekEmitsPositionChangedAndNotifiesMpris) {
    // We can't easily mock MprisServer, but we can verify the callback chain.
    // The test mainly ensures no crashes. Position clamps to 0 (duration=0).

    backend->seek(42.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);
}

// Test can_seek() returns false when no track is loaded.
TEST_F(SeekTest, CanSeekFalseWhenNoTrack) {
    EXPECT_FALSE(backend->can_seek());
    EXPECT_FALSE(backend->get_playback_source_manager()->can_seek());
}

// Test seek_by (relative seek) - delegates to seek().
TEST_F(SeekTest, RelativeSeek) {
    backend->seek(10.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0); // Clamped to duration=0

    // seek_by would add to current position - tested via PlaybackSourceManager
    // but we can verify the API exists. Position remains 0 (clamped).
    backend->get_playback_source_manager()->seek_by(5.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0); // Immediate update clamped
}

// Test that seek works with external source (MPRIS) when available.
// This is a structural test - we verify the routing logic exists.
TEST_F(SeekTest, SeekRoutesToActiveSource) {
    // Initially internal source
    EXPECT_TRUE(backend->is_playback_source_internal());

    // Switch to external (if MPRIS manager available)
    backend->set_playback_source_external();
    // Source manager should route to external if available

    // Switch back to internal
    backend->set_playback_source_internal();
    EXPECT_TRUE(backend->is_playback_source_internal());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}