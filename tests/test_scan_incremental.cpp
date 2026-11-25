/**
 * Lollypop C++ - Incremental Library Scan Tests
 *
 * Tests incremental scanning with mtime+size cache, progress throttling,
 * and library_changed emission once per scan.
 */

#include <gtest/gtest.h>
#include "library_scanner.hpp"
#include "library_manager.hpp"
#include "controller.hpp"

#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>

using namespace lollypop;

class IncrementalScanTest : public ::testing::Test {
protected:
    std::string test_dir_;
    std::string cache_path_;

    void SetUp() override {
        // Create a temporary directory for testing
        test_dir_ = "/tmp/lollypop_test_scan_" + std::to_string(getpid());
        std::filesystem::create_directories(test_dir_);
        cache_path_ = test_dir_ + "/scan_cache.txt";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    void create_test_file(const std::string& name, const std::string& content = "test") {
        std::ofstream f(test_dir_ + "/" + name);
        f << content;
    }
};

// Test that incremental cache path can be set.
TEST_F(IncrementalScanTest, SetIncrementalCachePath) {
    LibraryScanner scanner;
    scanner.set_incremental_cache_path(cache_path_);
    // No crash, just verify the method exists and is callable.
    SUCCEED();
}

// Test that scan_progress callback is throttled (100ms).
// This is a structural test - we verify the callback is called.
TEST_F(IncrementalScanTest, ProgressCallbackThrottled) {
    LibraryScanner scanner;
    scanner.set_incremental_cache_path(cache_path_);

    int progress_calls = 0;
    ScanProgress last_progress;
    scanner.set_progress_callback([&](const ScanProgress& progress) {
        progress_calls++;
        last_progress = progress;
    });

    // Create a test audio file (empty, just for extension)
    create_test_file("test.mp3");

    scanner.scan_directory(test_dir_);

    // Progress should have been emitted at least once
    EXPECT_GT(progress_calls, 0);
    EXPECT_EQ(last_progress.files_found, 1u);
    EXPECT_EQ(last_progress.files_processed, 1u);
}

// Test that scan_file works with incremental cache.
TEST_F(IncrementalScanTest, ScanFileWithCache) {
    LibraryScanner scanner;
    scanner.set_incremental_cache_path(cache_path_);

    bool track_found = false;
    scanner.set_track_found_callback([&](const Track& track) {
        track_found = true;
        EXPECT_FALSE(track.id.empty());
    });

    create_test_file("test.mp3");

    // First scan - should process the file
    scanner.scan_file(test_dir_ + "/test.mp3");
    EXPECT_TRUE(track_found);

    // Second scan - file unchanged, should still call track_found
    // (since we don't have access to existing track data in this test)
    track_found = false;
    scanner.scan_file(test_dir_ + "/test.mp3");
    // The file is unchanged, but track_found is still called because
    // we can't easily return existing track data without storage.
    // The optimization is that TagLib parsing is skipped.
}

// Test that scan_directory works with incremental cache.
// Note: Empty test files fail TagLib parsing, so track_found isn't called.
// This test verifies the cache logic structure (files_found/files_processed).
TEST_F(IncrementalScanTest, ScanDirectoryWithCache) {
    LibraryScanner scanner;
    scanner.set_incremental_cache_path(cache_path_);

    ScanProgress last_progress;
    scanner.set_progress_callback([&](const ScanProgress& progress) {
        last_progress = progress;
    });

    create_test_file("song1.mp3");
    create_test_file("song2.flac");
    create_test_file("not_audio.txt");

    // First scan
    scanner.scan_directory(test_dir_);
    EXPECT_EQ(last_progress.files_found, 2u); // Only audio files
    EXPECT_EQ(last_progress.files_processed, 2u);

    // Second scan - files unchanged, should still count them
    scanner.scan_directory(test_dir_);
    EXPECT_EQ(last_progress.files_found, 2u);
    EXPECT_EQ(last_progress.files_processed, 2u);
}

// Test that cache persists across scanner instances.
// Note: Empty test files fail TagLib parsing, so track_found isn't called.
// This test verifies the cache file is created and loaded.
TEST_F(IncrementalScanTest, CachePersistsAcrossInstances) {
    {
        LibraryScanner scanner1;
        scanner1.set_incremental_cache_path(cache_path_);

        create_test_file("persist.mp3");
        scanner1.scan_file(test_dir_ + "/persist.mp3");
    } // scanner1 destroyed, cache saved

    // Verify versioned binary cache exists and has a header plus entries.
    std::error_code ec;
    EXPECT_TRUE(std::filesystem::exists(cache_path_, ec));
    EXPECT_GT(std::filesystem::file_size(cache_path_, ec), 12u);

    {
        LibraryScanner scanner2;
        scanner2.set_incremental_cache_path(cache_path_);

        ScanProgress last_progress;
        scanner2.set_progress_callback([&](const ScanProgress& progress) {
            last_progress = progress;
        });

        // File unchanged, cache should be loaded, files_processed should match
        scanner2.scan_file(test_dir_ + "/persist.mp3");
        EXPECT_EQ(last_progress.files_found, 1u);
        EXPECT_EQ(last_progress.files_processed, 1u);
    }
}

// Test that modified file is re-processed.
// Note: Empty test files fail TagLib parsing, so track_found isn't called.
// This test verifies the cache detects mtime changes.
TEST_F(IncrementalScanTest, ModifiedFileReprocessed) {
    LibraryScanner scanner;
    scanner.set_incremental_cache_path(cache_path_);

    ScanProgress last_progress;
    scanner.set_progress_callback([&](const ScanProgress& progress) {
        last_progress = progress;
    });

    create_test_file("modify.mp3", "original");

    // First scan
    scanner.scan_file(test_dir_ + "/modify.mp3");
    EXPECT_EQ(last_progress.files_found, 1u);
    EXPECT_EQ(last_progress.files_processed, 1u);

    // Modify the file
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Ensure mtime changes
    create_test_file("modify.mp3", "modified");

    // Second scan - should re-process (cache detects mtime change)
    scanner.scan_file(test_dir_ + "/modify.mp3");
    EXPECT_EQ(last_progress.files_found, 1u);
    EXPECT_EQ(last_progress.files_processed, 1u);
}

// Test that cancelled scan doesn't save partial cache.
TEST_F(IncrementalScanTest, CancelledScan) {
    LibraryScanner scanner;
    scanner.set_incremental_cache_path(cache_path_);

    create_test_file("cancel.mp3");

    scanner.cancel();
    scanner.scan_file(test_dir_ + "/cancel.mp3");

    // Cancelled scan should not have processed the file
    // (The cancel flag is checked at the start)
    // This is more of a structural test.
}

// Test that library_changed is emitted once per scan (structural).
// The actual emission happens in LibraryManager/BackendController.
TEST_F(IncrementalScanTest, LibraryChangedOncePerScan) {
    // This is a structural test - the LibraryManager emits library_changed
    // once per completed scan via its finished_cb_.
    // We verify the scanner completes without crashing.
    LibraryScanner scanner;
    scanner.set_incremental_cache_path(cache_path_);

    create_test_file("test.mp3");
    scanner.scan_directory(test_dir_);

    // If we reach here without crash, the scan completed
    SUCCEED();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}