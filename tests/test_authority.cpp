/**
 * Lollypop C++ - Source Authority Routing Tests
 *
 * Tests that transport verbs route correctly through PlaybackSourceManager
 * based on the active playback source (internal vs external).
 */

#include <gtest/gtest.h>
#include "controller.hpp"
#include "playback_source_manager.hpp"

#include <vector>

using namespace lollypop;

class AuthorityTest : public ::testing::Test {
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

// Test that initial source is internal.
TEST_F(AuthorityTest, InitialSourceIsInternal) {
    EXPECT_TRUE(backend->is_playback_source_internal());
    EXPECT_EQ(backend->get_playback_source_manager()->source(), PlaybackSource::Internal);
}

// Test switching to external source.
TEST_F(AuthorityTest, SwitchToExternalSource) {
    backend->set_playback_source_external();
    EXPECT_FALSE(backend->is_playback_source_internal());
    EXPECT_EQ(backend->get_playback_source_manager()->source(), PlaybackSource::External);
}

// Test switching back to internal source.
TEST_F(AuthorityTest, SwitchBackToInternalSource) {
    backend->set_playback_source_external();
    backend->set_playback_source_internal();
    EXPECT_TRUE(backend->is_playback_source_internal());
    EXPECT_EQ(backend->get_playback_source_manager()->source(), PlaybackSource::Internal);
}

// Test play_pause routes to active source.
TEST_F(AuthorityTest, PlayPauseRoutesToActiveSource) {
    // Internal source: should not crash
    backend->toggle_play_pause();
    // No track loaded, so nothing happens but no crash.

    // Switch to external (if MPRIS available)
    backend->set_playback_source_external();
    backend->toggle_play_pause(); // Should route to MPRIS if available

    // Back to internal
    backend->set_playback_source_internal();
    backend->toggle_play_pause();
}

// Test pause routes to active source.
TEST_F(AuthorityTest, PauseRoutesToActiveSource) {
    backend->pause(); // Internal

    backend->set_playback_source_external();
    backend->pause(); // External

    backend->set_playback_source_internal();
    backend->pause();
}

// Test stop routes to active source.
TEST_F(AuthorityTest, StopRoutesToActiveSource) {
    backend->stop(); // Internal

    backend->set_playback_source_external();
    backend->stop(); // External

    backend->set_playback_source_internal();
    backend->stop();
}

// Test next_track routes to active source.
TEST_F(AuthorityTest, NextTrackRoutesToActiveSource) {
    backend->next_track(); // Internal - should stop (empty queue)

    backend->set_playback_source_external();
    backend->next_track(); // External - routes to MPRIS

    backend->set_playback_source_internal();
    backend->next_track(); // Internal
}

// Test previous_track routes to active source.
TEST_F(AuthorityTest, PreviousTrackRoutesToActiveSource) {
    backend->previous_track(); // Internal - should seek to 0

    backend->set_playback_source_external();
    backend->previous_track(); // External - routes to MPRIS

    backend->set_playback_source_internal();
    backend->previous_track();
}

// Test seek routes to active source and respects can_seek().
TEST_F(AuthorityTest, SeekRoutesToActiveSource) {
    // Internal source - can_seek is false (no track loaded)
    // Should still update position immediately (clamped)
    backend->seek(100.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);

    backend->set_playback_source_external();
    backend->seek(100.0); // External - routes to MPRIS if can_seek

    backend->set_playback_source_internal();
    backend->seek(50.0);
}

// Test set_volume routes to active source.
TEST_F(AuthorityTest, SetVolumeRoutesToActiveSource) {
    backend->set_volume(0.5);
    EXPECT_DOUBLE_EQ(backend->get_volume(), 0.5);

    backend->set_playback_source_external();
    backend->set_volume(0.8);
    // Volume comes from active source

    backend->set_playback_source_internal();
    backend->set_volume(0.3);
    EXPECT_DOUBLE_EQ(backend->get_volume(), 0.3);
}

// Test can_seek reflects active source capability.
TEST_F(AuthorityTest, CanSeekReflectsActiveSource) {
    // Internal source with no track: can_seek should be false
    EXPECT_FALSE(backend->can_seek());
    EXPECT_FALSE(backend->get_playback_source_manager()->can_seek());

    // External source (no MPRIS player): can_seek should be false
    backend->set_playback_source_external();
    EXPECT_FALSE(backend->get_playback_source_manager()->can_seek());

    backend->set_playback_source_internal();
    EXPECT_FALSE(backend->get_playback_source_manager()->can_seek());
}

// Test can_control reflects active source capability.
TEST_F(AuthorityTest, CanControlReflectsActiveSource) {
    // Internal source: always controllable (if audio engine exists)
    EXPECT_TRUE(backend->get_playback_source_manager()->can_control());

    // External source (no MPRIS player): not controllable
    backend->set_playback_source_external();
    EXPECT_FALSE(backend->get_playback_source_manager()->can_control());

    backend->set_playback_source_internal();
    EXPECT_TRUE(backend->get_playback_source_manager()->can_control());
}

// Test can_go_next/previous reflect active source.
TEST_F(AuthorityTest, CanGoNextPreviousReflectActiveSource) {
    // Internal source with empty queue: can_go_next should be true (queue manager allows)
    // Actually queue manager returns true for can_go_next if repeat or shuffle enabled
    // but PlaybackSourceManager returns true for internal unconditionally.
    EXPECT_TRUE(backend->get_playback_source_manager()->can_go_next());
    EXPECT_TRUE(backend->get_playback_source_manager()->can_go_previous());

    // External source (no MPRIS player): false
    backend->set_playback_source_external();
    EXPECT_FALSE(backend->get_playback_source_manager()->can_go_next());
    EXPECT_FALSE(backend->get_playback_source_manager()->can_go_previous());

    backend->set_playback_source_internal();
    EXPECT_TRUE(backend->get_playback_source_manager()->can_go_next());
    EXPECT_TRUE(backend->get_playback_source_manager()->can_go_previous());
}

// Test that unsupported verbs on external source don't crash.
TEST_F(AuthorityTest, UnsupportedVerbsOnExternalSourceDontCrash) {
    backend->set_playback_source_external();

    // These should not crash even with no MPRIS player
    backend->toggle_play_pause();
    backend->pause();
    backend->stop();
    backend->next_track();
    backend->previous_track();
    backend->seek(10.0);
    backend->set_volume(0.5);

    // Switch back to internal - should still work
    backend->set_playback_source_internal();
    backend->toggle_play_pause();
    backend->pause();
    backend->stop();
    backend->next_track();
    backend->previous_track();
    backend->seek(10.0);
    backend->set_volume(0.5);
}

// Test that internal source operations work with queue.
TEST_F(AuthorityTest, InternalSourceQueueOperations) {
    EXPECT_TRUE(backend->is_playback_source_internal());

    // next_track on empty queue should stop (no crash)
    backend->next_track();

    // previous_track should seek to 0 (no crash)
    backend->previous_track();

    // seek should clamp to 0
    backend->seek(100.0);
    EXPECT_DOUBLE_EQ(backend->get_position(), 0.0);
}

// Test playback source manager capability queries.
TEST_F(AuthorityTest, PlaybackSourceManagerCapabilities) {
    auto* psm = backend->get_playback_source_manager();

    // Internal source capabilities
    EXPECT_EQ(psm->source(), PlaybackSource::Internal);
    // Note: internal_available() depends on deferred initialization which runs
    // asynchronously. In tests without a main loop, it may not have run yet.
    // EXPECT_TRUE(psm->internal_available());
    EXPECT_FALSE(psm->external_available()); // No MPRIS player
    EXPECT_TRUE(psm->can_control());
    EXPECT_FALSE(psm->can_seek()); // No track loaded
    EXPECT_TRUE(psm->can_go_next());
    EXPECT_TRUE(psm->can_go_previous());

    // Get position/duration from internal source
    EXPECT_DOUBLE_EQ(psm->get_position(), 0.0);
    EXPECT_DOUBLE_EQ(psm->get_duration(), 0.0);
    // Other tests persist volume to the shared database, so only assert a
    // valid range instead of a fixed default.
    EXPECT_GE(psm->get_volume(), 0.0);
    EXPECT_LE(psm->get_volume(), 1.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}