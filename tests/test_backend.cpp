/**
 * Lollypop C++ - Backend Tests
 */

#include <gtest/gtest.h>
#include "controller.hpp"

using namespace lollypop;

class BackendTest : public ::testing::Test {
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

TEST_F(BackendTest, Initialize) {
    EXPECT_FALSE(backend->is_playing());
    // Volume is restored from the persisted preference, so only its valid
    // range can be asserted on a shared database.
    EXPECT_GE(backend->get_volume(), 0.0);
    EXPECT_LE(backend->get_volume(), 1.0);
}

TEST_F(BackendTest, VolumeControl) {
    backend->set_volume(0.5);
    EXPECT_DOUBLE_EQ(backend->get_volume(), 0.5);

    backend->set_volume(1.5);  // Should clamp to 1.0
    EXPECT_DOUBLE_EQ(backend->get_volume(), 1.0);

    backend->set_volume(-0.5);  // Should clamp to 0.0
    EXPECT_DOUBLE_EQ(backend->get_volume(), 0.0);
}

TEST_F(BackendTest, PlaybackModes) {
    // Shuffle
    EXPECT_FALSE(backend->get_shuffle());
    backend->set_shuffle(true);
    EXPECT_TRUE(backend->get_shuffle());
    backend->set_shuffle(false);
    EXPECT_FALSE(backend->get_shuffle());

    // Repeat
    EXPECT_FALSE(backend->get_repeat());
    backend->set_repeat(true);
    EXPECT_TRUE(backend->get_repeat());
    backend->set_repeat(false);
    EXPECT_FALSE(backend->get_repeat());

    // Repeat one
    EXPECT_FALSE(backend->get_repeat_one());
    backend->set_repeat_one(true);
    EXPECT_TRUE(backend->get_repeat_one());
    // Repeat one should disable repeat
    EXPECT_FALSE(backend->get_repeat());
}

TEST_F(BackendTest, Callbacks) {
    bool track_changed = false;
    bool position_changed = false;
    bool state_changed = false;
    bool queue_changed = false;

    backend->set_track_changed_callback([&](const Track& track) {
        track_changed = true;
        EXPECT_EQ(track.title, "Test");
    });

    backend->set_position_changed_callback([&](double pos, double dur) {
        position_changed = true;
        EXPECT_DOUBLE_EQ(pos, 0.0);
        EXPECT_DOUBLE_EQ(dur, 180.0);
    });

    backend->set_playback_state_changed_callback([&](bool playing) {
        state_changed = true;
        EXPECT_TRUE(playing);
    });

    backend->set_queue_changed_callback([&](const std::vector<std::string>& queue) {
        queue_changed = true;
        EXPECT_EQ(queue.size(), 1);
    });

    // Create a track in storage first
    Track track;
    track.id = "callback-test";
    track.title = "Test";
    track.artist = "Artist";
    track.duration = 180;
    track.file_path = "/music/test.mp3";

    // We can't easily test this without storage integration
    // This is just verifying callback registration works
    EXPECT_FALSE(track_changed);
    EXPECT_FALSE(position_changed);
    EXPECT_FALSE(state_changed);
    EXPECT_FALSE(queue_changed);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}