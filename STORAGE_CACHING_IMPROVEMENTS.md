# Storage Caching Improvements — Fast Startup & Metadata Persistence

## Current Architecture Analysis

### Existing Caching Layers
| Layer | Location | What's Cached | Format |
|-------|----------|---------------|--------|
| **Incremental Scan Cache** | `LibraryScanner::Impl::scan_cache_` | `file_path → {mtime, size}` | Text file: `path\|mtime\|size` |
| **SQLite Database** | `StorageManager` | All tracks, albums, artists, playlists, lyrics | SQLite WAL |
| **In-Memory Lookup Maps** | `StorageManager::Impl` | `artist_name_to_id`, `album_key_to_id` | `unordered_map` |
| **Read-Only Connections** | `StorageManager` | Concurrent read access for scanner | SQLite RO connections |
| **Artwork Cache** | `ArtworkCache` (separate) | Thumbnails at 64/160/512/1024px | File-backed + GdkTexture LRU |

---

## Problems Identified

1. **Slow Startup on Large Libraries** — On launch, `get_all_tracks()`, `get_all_albums()`, `get_all_artists()` query SQLite. For 10k+ tracks, this takes 200-500ms+.

2. **No Pre-Computed Metadata Cache** — Every scan re-extracts TagLib metadata even for unchanged files. The scan cache only stores mtime+size, not the extracted tags.

3. **Genre Hierarchy Queries Are Slow** — Recursive CTEs on genre DAG run on every page load.

4. **FTS5 Index Rebuilds on Schema Change** — Full-text search index rebuilt only on schema version change, not incrementally.

5. **No Smart Playlist Materialization** — Smart playlists re-evaluated on every access.

5. **Album/Artist Aggregates Not Cached** — `get_tracks_by_album`, `get_tracks_by_artist` run queries every time.

6. **No Background Preloading** — UI requests data on-demand; no predictive loading.

---

## Proposed Improvements

### Phase 1: Metadata Cache File (Instant Startup)

**File:** `~/.local/share/lollypop-cpp/metadata_cache.bin` (or `.json` for debug)

**Content:** Pre-computed library views serialized to binary/JSON:
```json
{
  "version": 1,
  "generated_at": 1699999999,
  "tracks": [
    {"id": "...", "title": "...", "artist_id": "...", "album_id": "...", "genre": "...", "duration": 240.5, "track_number": 1, "disc_number": 1, "year": 2020, "liked": false, "play_count": 5, "last_played": 1699999999, "date_added": 1699999999}
  ],
  "albums": [
    {"id": "...", "title": "...", "artist_id": "...", "year": 2020, "track_count": 12, "artwork_path": "..."}
  ],
  "artists": [
    {"id": "...", "name": "...", "album_count": 3, "track_count": 36, "artwork_path": "..."}
  ],
  "genres": ["Rock", "Electronic", "Hip Hop"],
  "genre_hierarchy": {"Rock": ["Progressive Rock", "Hard Rock"]},
  "playlists": [{"id": "...", "name": "...", "track_ids": [...], "smart_criteria": "..."}],
  "library_roots": ["/home/user/Music"],
  "stats": {"track_count": 10000, "album_count": 800, "artist_count": 500}
}
```

**Benefits:**
- **Startup: O(1) instead of O(n)** — Load cache file, populate in-memory structures, skip SQLite queries
- **Background Refresh** — Rebuild cache async after UI is responsive
- **Incremental Updates** — On track add/update/delete, patch cache instead of full rebuild

**Implementation:**
1. Add `MetadataCache` class in `storage/` with load/save methods
2. Serialize `Track`, `Album`, `Artist`, `Playlist`, `Genre` to compact binary (FlatBuffers/Protocol Buffers) or JSON
3. Integrate with `StorageManager::initialize()` — load cache first, fall back to SQLite if stale/corrupt
4. Add `StorageManager::rebuild_metadata_cache()` called after scan completes
5. Add `StorageManager::patch_metadata_cache(TrackChange)` for incremental updates

---

### Phase 2: Extended Incremental Scan Cache (Skip TagLib Entirely)

**Current:** `scan_cache_` only stores `mtime + size`

**Extended:** Store extracted metadata hash + key tags:
```cpp
struct ExtendedFileMeta {
    std::time_t mtime = 0;
    std::uintmax_t size = 0;
    uint64_t metadata_hash = 0;  // Hash of title+artist+album+genre+track_number+year
    std::string title;
    std::string artist;
    std::string album;
    std::string album_artist;
    std::string genre;
    int track_number = 0;
    int disc_number = 0;
    int year = 0;
    double duration = 0.0;
    std::string artwork_hash;  // Hash of embedded artwork for change detection
};
```

**Benefits:**
- **Skip TagLib entirely for unchanged files** — Compare `metadata_hash` instead of re-parsing
- **Instant track_found callback** — Return cached metadata without TagLib
- **Detect artwork changes** — `artwork_hash` catches embedded art updates

**Format:** Binary (msgpack/cbor) or extended text: `path|mtime|size|metadata_hash|title|artist|...`

---

### Phase 3: Query Result Caching (In-Memory + Persistent)

**In-Memory LRU Cache** for frequent queries:
```cpp
struct QueryCache {
    // Track queries
    std::unordered_map<std::string, std::vector<Track>> tracks_by_album;
    std::unordered_map<std::string, std::vector<Track>> tracks_by_artist;
    std::unordered_map<std::string, std::vector<Track>> tracks_by_genre;
    
    // Album queries
    std::unordered_map<std::string, std::vector<Album>> albums_by_artist;
    
    // Genre hierarchy
    std::unordered_map<std::string, std::vector<std::string>> genre_descendants;
    
    // Playlist tracks
    std::unordered_map<std::string, std::vector<Track>> playlist_tracks;
    
    // LRU eviction: max 100 entries per category, TTL 5 min
};
```

**Persistent Query Cache** (disk):
- Cache results of expensive queries (genre hierarchy, smart playlists)
- Store in `query_cache/` directory as `.bin` files keyed by query hash
- Invalidate on relevant data changes

---

### Phase 4: Smart Playlist Materialization

**Current:** Smart playlists evaluated on every `get_smart_playlist_tracks()` call.

**Improved:**
```cpp
struct SmartPlaylistCache {
    std::string playlist_id;
    std::vector<std::string> track_ids;  // Materialized result
    int64_t computed_at = 0;
    std::string criteria_hash;  // Hash of criteria for invalidation
};
```

- Materialize on scan finish or criteria change
- Store in `smart_playlists` table + cache file
- Background refresh on library changes

---

### Phase 5: Background Preloading & Predictive Loading

**LibraryManager worker thread** already exists — extend it:
```cpp
void LibraryManager::preload_common_queries() {
    // Run on worker thread after scan
    storage_->get_all_tracks();           // Populate track cache
    storage_->get_all_albums();           // Populate album cache
    storage_->get_all_artists();          // Populate artist cache
    storage_->get_all_genres();           // Populate genre cache
    storage_->get_genre_descendants("");  // Populate genre hierarchy
    // Pre-warm FTS5 with common prefixes
}
```

**UI Integration:**
- `MainWindow` requests `preload_common_queries()` after scan
- Show "Library ready" when preload completes
- Prioritize visible page data first

---

### Phase 6: Artwork Integration with Scan Cache

**Current:** Artwork extracted separately via `ArtworkManager`, stored in `ArtworkCache`.

**Improved:** Embed artwork hash in scan cache → detect artwork changes without re-extracting:
```cpp
// In LibraryScanner::process_file():
if (file.has_embedded_artwork()) {
    track.artwork_hash = hash_artwork(file.embedded_artwork());
    // Save artwork to cache dir: cache/artwork/{track_id}.jpg
}
```

- Skip artwork re-extraction if `artwork_hash` unchanged
- Pre-generate thumbnails during scan (background)

---

## Implementation Priority

| Phase | Effort | Impact | Dependencies |
|-------|--------|--------|--------------|
| 1. Metadata Cache File | Medium | **High** (instant startup) | None |
| 2. Extended Scan Cache | Medium | **High** (fast re-scans) | Phase 1 |
| 3. Query Result Cache | Low | Medium | Phase 1 |
| 4. Smart Playlist Materialization | Medium | Medium | Phase 1, 3 |
| 5. Background Preloading | Low | Medium | Phase 1 |
| 6. Artwork Integration | Medium | Medium | Phase 2 |

---

## Files to Create/Modify

### New Files
```
storage/include/metadata_cache.hpp
storage/src/metadata_cache.cpp
storage/include/query_cache.hpp
storage/src/query_cache.cpp
backend/include/smart_playlist_cache.hpp
backend/src/smart_playlist_cache.cpp
```

### Modified Files
```
storage/include/manager.hpp          // Add cache methods
storage/src/manager.cpp              // Integrate metadata cache
backend/include/library_scanner.hpp  // Extended FileMeta
backend/src/library_scanner.cpp      // Extended cache load/save, hash comparison
backend/include/library_manager.hpp  // Preload methods
backend/src/library_manager.cpp      // Worker preload logic
backend/include/controller.hpp       // Cache rebuild triggers
backend/src/controller.cpp           // Call cache rebuild after scan
```

---

## Technical Details

### Metadata Cache Serialization (Binary - Recommended)
Use **FlatBuffers** (zero-copy, fast) or **Protocol Buffers**:
```bash
# Add to CMakeLists.txt
find_package(flatbuffers REQUIRED)
# or
find_package(Protobuf REQUIRED)
```

**Schema example (FlatBuffers):**
```fbs
namespace lollypop;

table Track {
    id: string;
    title: string;
    artist_id: string;
    album_id: string;
    genre: string;
    track_number: int;
    disc_number: int;
    year: int;
    duration: float;
    liked: bool;
    play_count: int;
    last_played: long;
    date_added: long;
}

table Album {
    id: string;
    title: string;
    artist_id: string;
    year: int;
    track_count: int;
    artwork_path: string;
}

table Artist {
    id: string;
    name: string;
    album_count: int;
    track_count: int;
    artwork_path: string;
}

table MetadataCache {
    version: int;
    generated_at: long;
    tracks: [Track];
    albums: [Album];
    artists: [Artist];
    genres: [string];
    genre_hierarchy: [string];  // Flattened parent|child pairs
    playlists: [Playlist];
    library_roots: [string];
}
```

### Cache Invalidation Strategy
| Event | Invalidate |
|-------|------------|
| Track added/updated/deleted | Patch metadata cache (O(1) for single track) |
| Album added/updated | Patch album + related tracks |
| Artist added/updated | Patch artist + related albums/tracks |
| Playlist changed | Patch playlist cache |
| Genre hierarchy changed | Full genre cache rebuild |
| Full scan completed | Full metadata cache rebuild |
| Artwork changed | Patch track/album/artist artwork_path |

### Binary vs JSON Trade-off
| Format | Parse Speed | File Size | Debuggability |
|--------|-------------|-----------|---------------|
| JSON | ~50 MB/s | Large | Excellent |
| FlatBuffers | ~500 MB/s | Small | Requires tool |
| MessagePack | ~200 MB/s | Medium | Good |
| CBOR | ~200 MB/s | Medium | Good |

**Recommendation:** **FlatBuffers** for production (zero-copy, schema evolution), JSON for debug builds.

---

## Testing Plan

1. **Startup Time Benchmark**
   - Measure `StorageManager::initialize()` with 1k, 10k, 50k, 100k tracks
   - Target: < 50ms for 10k tracks (vs current ~300ms)

2. **Re-scan Speed Benchmark**
   - Measure incremental scan with 0%, 10%, 50%, 100% changes
   - Target: 10x speedup for < 10% changes

3. **Cache Correctness**
   - Verify cache matches SQLite after every operation
   - Stress test: random operations + cache rebuild comparison

4. **Memory Usage**
   - Track metadata cache memory footprint
   - Target: < 50MB for 100k tracks

---

## Rollout Strategy

1. **Feature flag:** `bool enable_metadata_cache = true` (configurable)
2. **Graceful fallback:** If cache corrupt/missing → full SQLite query + rebuild
3. **Versioning:** Cache format version → auto-migrate or rebuild
4. **Observability:** Log cache hit/miss rates, rebuild times