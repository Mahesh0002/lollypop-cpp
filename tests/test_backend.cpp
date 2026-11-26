/**
 * Lollypop C++ - Backend Tests
 */

#include <gtest/gtest.h>
#include "controller.hpp"
#include "external_providers.hpp"
#include "manager.hpp"

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

TEST(ArtistInfoProviderTest, ParsesWikipediaSummary) {
    const auto result = ArtistInfoProvider::parse_biography(
        R"({"type":"standard","title":"Example Artist","description":"musician","extract":"Example Artist is a musician known for testing."})");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->title, "Example Artist");
    EXPECT_EQ(result->description, "musician");
    EXPECT_EQ(result->extract,
              "Example Artist is a musician known for testing.");
}

TEST(ArtistInfoProviderTest, RejectsDisambiguationAndEmptyExtract) {
    EXPECT_FALSE(ArtistInfoProvider::parse_biography(
                     R"({"type":"disambiguation","extract":"May refer to:"})")
                     .has_value());
    EXPECT_FALSE(
        ArtistInfoProvider::parse_biography(R"({"type":"standard"})")
            .has_value());
}

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
    // Playback modes are restored from persisted preferences on a shared
    // database, so normalize them before asserting default-off behaviour.
    backend->set_shuffle(false);
    backend->set_repeat(false);
    backend->set_repeat_one(false);

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

TEST(StorageBatchTest, LargeLookupAndUnboundedPagination) {
    StorageManager storage;
    ASSERT_TRUE(storage.initialize(":memory:"));

    constexpr int kCount = 1050;  // Beyond both the 999 bind limit and 500 chunking.
    for (int i = 0; i < kCount; ++i) {
        Track track;
        track.id = "batch-track-" + std::to_string(i);
        track.title = "Title " + std::to_string(i);
        track.artist = "Artist";
        track.file_path = "/music/" + track.id + ".mp3";
        ASSERT_TRUE(storage.add_track(track));
    }

    std::vector<std::string> ids;
    for (int i = kCount - 1; i >= 0; --i) {
        ids.push_back("batch-track-" + std::to_string(i));
    }
    ids.push_back("missing-track");
    ids.push_back(ids.front());  // Duplicate must be preserved.

    const auto tracks = storage.get_tracks_by_ids(ids);
    ASSERT_EQ(tracks.size(), static_cast<size_t>(kCount) + 1);
    EXPECT_EQ(tracks.front().id, "batch-track-1049");
    EXPECT_EQ(tracks[kCount - 1].id, "batch-track-0");
    EXPECT_EQ(tracks.back().id, "batch-track-1049");

    // Negative limit means unbounded; this library is larger than 10k's
    // conceptual cap and must still be returned in full.
    EXPECT_EQ(storage.get_tracks_paginated(-1, "title").size(),
              static_cast<size_t>(kCount));

    storage.shutdown();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}