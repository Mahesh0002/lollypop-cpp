/**
 * Lollypop C++ - Storage Tests
 */

#include <gtest/gtest.h>
#include "manager.hpp"
#include "controller.hpp"

#include <cstdio>

using namespace lollypop;

class StorageTest : public ::testing::Test {
protected:
    std::unique_ptr<StorageManager> storage;

    void SetUp() override {
        storage = std::make_unique<StorageManager>();
        ASSERT_TRUE(storage->initialize(":memory:"));
    }

    void TearDown() override {
        storage.reset();
    }
};

TEST_F(StorageTest, CreateSchema) {
    // Schema should be created during initialize - verify by calling count methods
    EXPECT_EQ(storage->get_track_count(), 0);
    EXPECT_EQ(storage->get_album_count(), 0);
    EXPECT_EQ(storage->get_artist_count(), 0);
}

TEST_F(StorageTest, AddAndGetTrack) {
    Track track;
    track.id = "test-track-1";
    track.title = "Test Song";
    track.artist = "Test Artist";
    track.album = "Test Album";
    track.duration = 240;
    track.file_path = "/music/test.mp3";

    EXPECT_TRUE(storage->add_track(track));

    Track retrieved = storage->get_track("test-track-1");
    EXPECT_EQ(retrieved.id, "test-track-1");
    EXPECT_EQ(retrieved.title, "Test Song");
    EXPECT_EQ(retrieved.artist, "Test Artist");
    EXPECT_EQ(retrieved.album, "Test Album");
    EXPECT_EQ(retrieved.duration, 240);
}

TEST_F(StorageTest, UpdateTrack) {
    Track track;
    track.id = "test-track-2";
    track.title = "Original Title";
    track.artist = "Test Artist";
    track.file_path = "/music/test2.mp3";

    EXPECT_TRUE(storage->add_track(track));

    track.title = "Updated Title";
    EXPECT_TRUE(storage->update_track(track));

    Track retrieved = storage->get_track("test-track-2");
    EXPECT_EQ(retrieved.title, "Updated Title");
}

TEST_F(StorageTest, DeleteTrack) {
    Track track;
    track.id = "test-track-3";
    track.title = "To Delete";
    track.artist = "Test Artist";
    track.file_path = "/music/test3.mp3";

    EXPECT_TRUE(storage->add_track(track));
    EXPECT_TRUE(storage->delete_track("test-track-3"));

    Track retrieved = storage->get_track("test-track-3");
    EXPECT_TRUE(retrieved.id.empty());
}

TEST_F(StorageTest, LikeTrack) {
    Track track;
    track.id = "test-track-4";
    track.title = "Like Test";
    track.artist = "Test Artist";
    track.file_path = "/music/test4.mp3";
    track.liked = false;

    EXPECT_TRUE(storage->add_track(track));
    EXPECT_TRUE(storage->toggle_like("test-track-4"));

    Track retrieved = storage->get_track("test-track-4");
    EXPECT_TRUE(retrieved.liked);

    // Toggle again
    EXPECT_TRUE(storage->toggle_like("test-track-4"));
    retrieved = storage->get_track("test-track-4");
    EXPECT_FALSE(retrieved.liked);
}

TEST_F(StorageTest, GetLikedTracks) {
    Track track1;
    track1.id = "liked-1";
    track1.title = "Liked Song 1";
    track1.artist = "Artist 1";
    track1.file_path = "/music/liked1.mp3";

    Track track2;
    track2.id = "liked-2";
    track2.title = "Liked Song 2";
    track2.artist = "Artist 2";
    track2.file_path = "/music/liked2.mp3";

    Track track3;
    track3.id = "unliked-1";
    track3.title = "Unliked Song";
    track3.artist = "Artist 3";
    track3.file_path = "/music/unliked1.mp3";

    EXPECT_TRUE(storage->add_track(track1));
    EXPECT_TRUE(storage->add_track(track2));
    EXPECT_TRUE(storage->add_track(track3));

    EXPECT_TRUE(storage->toggle_like("liked-1"));
    EXPECT_TRUE(storage->toggle_like("liked-2"));

    auto liked = storage->get_liked_tracks();
    EXPECT_EQ(liked.size(), 2);
}

TEST_F(StorageTest, PlaylistOperations) {
    EXPECT_TRUE(storage->create_playlist("Test Playlist"));

    auto playlists = storage->get_all_playlists();
    EXPECT_EQ(playlists.size(), 1);
    EXPECT_EQ(playlists[0].name, "Test Playlist");

    Track track;
    track.id = "playlist-track-1";
    track.title = "Playlist Track";
    track.artist = "Artist";
    track.file_path = "/music/playlist.mp3";
    EXPECT_TRUE(storage->add_track(track));

    EXPECT_TRUE(storage->add_track_to_playlist(playlists[0].id, track.id));

    Playlist retrieved = storage->get_playlist(playlists[0].id);
    EXPECT_EQ(retrieved.track_ids.size(), 1);
    EXPECT_EQ(retrieved.track_ids[0], track.id);

    EXPECT_TRUE(storage->remove_track_from_playlist(playlists[0].id, track.id));
    retrieved = storage->get_playlist(playlists[0].id);
    EXPECT_EQ(retrieved.track_ids.size(), 0);

    EXPECT_TRUE(storage->delete_playlist(playlists[0].id));
    playlists = storage->get_all_playlists();
    EXPECT_EQ(playlists.size(), 0);
}

TEST_F(StorageTest, Preferences) {
    EXPECT_TRUE(storage->set_preference("test_key", "test_value"));

    auto value = storage->get_preference("test_key");
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "test_value");

    auto missing = storage->get_preference("nonexistent");
    EXPECT_FALSE(missing.has_value());
}

TEST_F(StorageTest, SearchTracks) {
    Track track1;
    track1.id = "search-1";
    track1.title = "Hello World";
    track1.artist = "Test Artist";
    track1.file_path = "/music/search1.mp3";

    Track track2;
    track2.id = "search-2";
    track2.title = "Goodbye World";
    track2.artist = "Another Artist";
    track2.file_path = "/music/search2.mp3";

    EXPECT_TRUE(storage->add_track(track1));
    EXPECT_TRUE(storage->add_track(track2));

    auto results = storage->search_tracks("Hello");
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].title, "Hello World");

    results = storage->search_tracks("World");
    EXPECT_EQ(results.size(), 2);

    results = storage->search_tracks("Nonexistent");
    EXPECT_EQ(results.size(), 0);
}

TEST_F(StorageTest, SearchHandlesPunctuation) {
    Track track;
    track.id = "punct-1";
    track.title = "AC/DC Live";
    track.artist = "AC/DC";
    track.file_path = "/music/punct.mp3";
    ASSERT_TRUE(storage->add_track(track));

    // '/', quotes and '-' used to produce FTS syntax errors.
    EXPECT_EQ(storage->search_tracks("AC/DC").size(), 1);
    EXPECT_EQ(storage->search_tracks("\"unterminated").size(), 0);
}

TEST_F(StorageTest, AddTrackPopulatesAlbumAndArtist) {
    Track track;
    track.id = "album-track-1";
    track.title = "Song";
    track.artist = "Track Artist";
    track.album_artist = "Album Artist";
    track.album = "The Album";
    track.genre = "Rock";
    track.file_path = "/music/album-track.mp3";
    ASSERT_TRUE(storage->add_track(track));

    const auto albums = storage->get_all_albums();
    ASSERT_EQ(albums.size(), 1u);
    EXPECT_EQ(albums[0].title, "The Album");

    const auto album = storage->get_album(albums[0].id);
    EXPECT_EQ(album.track_ids, std::vector<std::string>{"album-track-1"});

    const auto artists = storage->get_all_artists();
    EXPECT_EQ(artists.size(), 2u);  // track artist + album artist
    EXPECT_EQ(storage->get_tracks_by_artist("Album Artist").size(), 1u);
    EXPECT_EQ(storage->get_tracks_by_artist("Nobody").size(), 0u);

    ASSERT_TRUE(storage->toggle_favorite_album(albums[0].id));
    const auto refreshed = storage->get_album(albums[0].id);
    EXPECT_TRUE(refreshed.favorite);
}

TEST_F(StorageTest, AddTrackUpsertsByFilePath) {
    Track track;
    track.id = "original-id";
    track.title = "Old Title";
    track.artist = "Artist";
    track.album = "Album";
    track.file_path = "/music/upsert.mp3";
    ASSERT_TRUE(storage->add_track(track));
    ASSERT_TRUE(storage->toggle_like("original-id"));
    storage->update_play_count("original-id");

    Track updated = track;
    updated.id = "different-id";
    updated.title = "New Title";
    ASSERT_TRUE(storage->add_track(updated));

    const Track stored = storage->get_track_by_path("/music/upsert.mp3");
    EXPECT_EQ(stored.id, "original-id");
    EXPECT_EQ(stored.title, "New Title");
    EXPECT_TRUE(stored.liked);
    EXPECT_EQ(stored.play_count, 1);
}

TEST_F(StorageTest, Fts5DiacriticsAndPrefix) {
    Track track;
    track.id = "fts-1";
    track.title = "Café au Lait";
    track.artist = "José González";
    track.file_path = "/music/fts1.mp3";
    ASSERT_TRUE(storage->add_track(track));

    auto results = storage->search_tracks("cafe");
    EXPECT_EQ(results.size(), 1);

    results = storage->search_tracks("jos");
    EXPECT_EQ(results.size(), 1);

    results = storage->search_tracks("gonz");
    EXPECT_EQ(results.size(), 1);
}

TEST_F(StorageTest, KeysetPagination) {
    for (int i = 0; i < 10; ++i) {
        Track track;
        track.id = "page-" + std::to_string(i);
        track.title = "Track " + std::to_string(i);
        track.artist = "Artist";
        track.file_path = "/music/page" + std::to_string(i) + ".mp3";
        track.date_added = i + 1;  // Use 1-10 (0 is overridden by add_track)
        ASSERT_TRUE(storage->add_track(track));
    }

    auto page1 = storage->get_tracks_paginated(3, "date_added", "", "");
    ASSERT_EQ(page1.size(), 3u);
    EXPECT_EQ(page1[0].id, "page-0");
    EXPECT_EQ(page1[1].id, "page-1");
    EXPECT_EQ(page1[2].id, "page-2");

    auto page2 = storage->get_tracks_paginated(3, "date_added",
                                                std::to_string(page1.back().date_added),
                                                page1.back().id);
    ASSERT_EQ(page2.size(), 3u);
    EXPECT_EQ(page2[0].id, "page-3");
    EXPECT_EQ(page2[1].id, "page-4");
    EXPECT_EQ(page2[2].id, "page-5");

    auto page3 = storage->get_tracks_paginated(3, "date_added",
                                                std::to_string(page2.back().date_added),
                                                page2.back().id);
    ASSERT_EQ(page3.size(), 3u);
    EXPECT_EQ(page3[0].id, "page-6");
    EXPECT_EQ(page3[1].id, "page-7");
    EXPECT_EQ(page3[2].id, "page-8");

    auto page4 = storage->get_tracks_paginated(3, "date_added",
                                                std::to_string(page3.back().date_added),
                                                page3.back().id);
    ASSERT_EQ(page4.size(), 1u);
    EXPECT_EQ(page4[0].id, "page-9");

    auto page5 = storage->get_tracks_paginated(3, "date_added",
                                                std::to_string(page4.back().date_added),
                                                page4.back().id);
    EXPECT_EQ(page5.size(), 0u);
}

TEST_F(StorageTest, GenreDag) {
    EXPECT_TRUE(storage->add_genre_edge("Rock", "Alternative Rock"));
    EXPECT_TRUE(storage->add_genre_edge("Alternative Rock", "Grunge"));
    EXPECT_TRUE(storage->add_genre_edge("Rock", "Classic Rock"));

    EXPECT_FALSE(storage->add_genre_edge("Grunge", "Rock"));
    EXPECT_FALSE(storage->add_genre_edge("Rock", "Rock"));

    auto descendants = storage->get_genre_descendants("Rock");
    EXPECT_EQ(descendants.size(), 3u);

    Track track1;
    track1.id = "genre-1";
    track1.title = "Song 1";
    track1.artist = "Artist";
    track1.genre = "Grunge";
    track1.file_path = "/music/genre1.mp3";
    ASSERT_TRUE(storage->add_track(track1));

    Track track2;
    track2.id = "genre-2";
    track2.title = "Song 2";
    track2.artist = "Artist";
    track2.genre = "Classic Rock";
    track2.file_path = "/music/genre2.mp3";
    ASSERT_TRUE(storage->add_track(track2));

    Track track3;
    track3.id = "genre-3";
    track3.title = "Song 3";
    track3.artist = "Artist";
    track3.genre = "Jazz";
    track3.file_path = "/music/genre3.mp3";
    ASSERT_TRUE(storage->add_track(track3));

    auto rock_tracks = storage->get_tracks_by_genre_hierarchy("Rock");
    ASSERT_EQ(rock_tracks.size(), 2u);

    auto grunge_tracks = storage->get_tracks_by_genre("Grunge");
    ASSERT_EQ(grunge_tracks.size(), 1u);
    EXPECT_EQ(grunge_tracks[0].id, "genre-1");

    auto jazz_tracks = storage->get_tracks_by_genre_hierarchy("Jazz");
    ASSERT_EQ(jazz_tracks.size(), 1u);
    EXPECT_EQ(jazz_tracks[0].id, "genre-3");
}

TEST_F(StorageTest, LibraryRoots) {
    EXPECT_TRUE(storage->add_library_root("/music"));
    EXPECT_TRUE(storage->add_library_root("/media/music"));
    EXPECT_FALSE(storage->add_library_root(""));

    auto roots = storage->get_library_roots();
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0], "/media/music");
    EXPECT_EQ(roots[1], "/music");

    EXPECT_TRUE(storage->is_library_root("/music"));
    EXPECT_TRUE(storage->is_library_root("/media/music"));
    EXPECT_FALSE(storage->is_library_root("/other"));

    EXPECT_TRUE(storage->remove_library_root("/music"));
    EXPECT_FALSE(storage->is_library_root("/music"));

    roots = storage->get_library_roots();
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0], "/media/music");
}

TEST_F(StorageTest, DatabaseRecovery) {
    EXPECT_TRUE(storage->integrity_check());
    EXPECT_TRUE(storage->quick_check());
    EXPECT_TRUE(storage->vacuum());

    std::string backup_path = "/tmp/lollypop-test-backup.db";
    EXPECT_TRUE(storage->backup_database(backup_path));

    Track track;
    track.id = "backup-test";
    track.title = "Backup Song";
    track.artist = "Artist";
    track.file_path = "/music/backup.mp3";
    ASSERT_TRUE(storage->add_track(track));

    EXPECT_TRUE(storage->backup_database(backup_path));
    std::remove(backup_path.c_str());
}

TEST_F(StorageTest, StringInterning) {
    for (int i = 0; i < 5; ++i) {
        Track track;
        track.id = "intern-" + std::to_string(i);
        track.title = "Song " + std::to_string(i);
        track.artist = "Same Artist";
        track.album = "Same Album";
        track.file_path = "/music/intern" + std::to_string(i) + ".mp3";
        ASSERT_TRUE(storage->add_track(track));
    }

    auto artists = storage->get_all_artists();
    ASSERT_EQ(artists.size(), 1u);
    EXPECT_EQ(artists[0].name, "Same Artist");

    auto albums = storage->get_all_albums();
    ASSERT_EQ(albums.size(), 1u);
    EXPECT_EQ(albums[0].title, "Same Album");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}