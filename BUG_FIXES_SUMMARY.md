# Lollypop C++ - Bug Fixes Summary

## Overview
This document tracks the critical UI architecture bugs identified in the MiddleLibrary panel and their current fix status.

---

## Bug Status Summary

| Bug | Description | Status | Location |
|-----|-------------|--------|----------|
| **Bug 1** | Nested Scrolled Windows (ALL pages) | ✅ **MOSTLY FIXED** | Various pages |
| **Bug 2** | MiddleLibrary `content_box` vexpand/valign wrong | ✅ **FIXED** | `middle_library.cpp:150-152` |
| **Bug 3** | Stack pages don't vexpand | ✅ **FIXED** | Various pages |
| **Bug 4** | Back/Forward buttons don't sync LeftSidebar | ⚠️ **PARTIAL** | `left_sidebar.cpp` missing impl |
| **Bug 5** | HomePage "View All" adds duplicate history | ✅ **FIXED** | `middle_library.cpp:363` |
| **Bug 6** | Search clear goes to Home instead of previous | ✅ **FIXED** | `middle_library.cpp:549-557` |
| **Bug 7** | SongsPage double scrolled window nesting | ✅ **FIXED** | `SongsPage.cpp` |
| **Bug 8** | FlowBox/ListBox natively scrollable (no wrapper needed) | ✅ **FIXED** | Various pages |

---

## Detailed Analysis

### Bug 1: Nested Scrolled Windows ✅ MOSTLY FIXED

**Root Cause**: Each page created its own `gtk_scrolled_window_new()` as root widget, but MiddleLibrary already has a single scrolled window (`scrolled_window_`) containing the stack. This caused triple nesting → broken scrolling, double scrollbars, focus/keyboard navigation failures.

**Pages Status:**

| Page | Before | After | Status |
|------|--------|-------|--------|
| `AlbumsPage` | `gtk_scrolled_window_new()` | `gtk_flow_box_new()` directly | ✅ FIXED |
| `ArtistsPage` | `gtk_scrolled_window_new()` | `gtk_flow_box_new()` directly | ✅ FIXED |
| `PlaylistsPage` | `gtk_scrolled_window_new()` | `gtk_flow_box_new()` directly | ✅ FIXED |
| `GenresPage` | `gtk_scrolled_window_new()` | `gtk_list_box_new()` directly | ✅ FIXED |
| `FavouriteAlbumsPage` | `gtk_scrolled_window_new()` | `gtk_flow_box_new()` directly | ✅ FIXED |
| `LikedMusicPage` | `gtk_scrolled_window_new()` | `gtk_list_box_new()` directly | ✅ FIXED |
| `SongsPage` | 2 scrolled windows | `gtk_box_new()` + `gtk_list_box_new()` | ✅ FIXED |
| `SearchResultsPage` | **2 scrolled windows** | Still has 2 scrolled windows | ❌ **NOT FIXED** |

**SearchResultsPage Issue** (`SearchResultsPage.cpp:42,75`):
```cpp
// Line 42: Root widget is a scrolled window
widget_.reset(gtk_scrolled_window_new());

// Line 75: ANOTHER scrolled window inside
scrolled_window_.reset(gtk_scrolled_window_new());
```
This creates triple nesting with MiddleLibrary's scrolled window.

---

### Bug 2: MiddleLibrary `content_box` vexpand/valign ✅ FIXED

**File**: `middle_library.cpp:150-152`

**Before (broken)**:
```cpp
gtk_widget_set_vexpand(content_box_, FALSE);   // Won't grow
gtk_widget_set_valign(content_box_, GTK_ALIGN_START);  // Pinned to top
```

**After (fixed)**:
```cpp
// Content box must expand to fill scroller so pages can scroll
gtk_widget_set_vexpand(content_box_, TRUE);
gtk_widget_set_valign(content_box_, GTK_ALIGN_FILL);
```

The content box now expands to fill the scroller, allowing pages to scroll properly.

---

### Bug 3: Stack pages don't vexpand ✅ FIXED

All pages now set `vexpand=true` on their root widgets:

```cpp
// AlbumsPage.cpp:33-34
gtk_widget_set_hexpand(grid_.get(), TRUE);
gtk_widget_set_vexpand(grid_.get(), TRUE);

// ArtistsPage.cpp:31-32
gtk_widget_set_hexpand(grid_.get(), TRUE);
gtk_widget_set_vexpand(grid_.get(), TRUE);

// Similar in PlaylistsPage, GenresPage, FavouriteAlbumsPage, LikedMusicPage
```

---

### Bug 4: Back/Forward buttons don't sync LeftSidebar ⚠️ PARTIAL

**Problem**: `on_back_clicked` / `on_forward_clicked` (lines 467-495 in `middle_library.cpp`) manipulate stack + history and call `page_change_cb_`, but the callback in `MainWindow` calls `left_sidebar_->set_active_item(page_id)` which **triggers navigation again** (calls `middle->set_active_page()`), creating a loop.

**Missing**: `LeftSidebar::set_active_item_visual()` - declared in header (`left_sidebar.hpp:45`) but **NOT IMPLEMENTED** in `left_sidebar.cpp`.

**Required Fix**:
1. Implement `LeftSidebar::set_active_item_visual(const std::string& item_id)` - updates visual active state ONLY, no navigation
2. Change `MainWindow` callback to use `set_active_item_visual` for external sync

```cpp
// In MainWindow::setup_layout() - CHANGE THIS:
middle_library_->set_page_change_callback([this](const std::string& page_id) {
    if (left_sidebar_) {
        left_sidebar_->set_active_item_visual(page_id);  // Not set_active_item!
    }
});
```

---

### Bug 5: HomePage "View All" callbacks add duplicate history ✅ FIXED

**File**: `middle_library.cpp:363`

**Fix Applied**:
```cpp
// Don't add to history if already on that page (prevents duplicates from HomePage "View All")
if (add_to_history && active_page_id_ != page_id) {
    // Truncate forward history
    history_.resize(history_index_ + 1);
    history_.push_back(page_id);
    history_index_ = history_.size() - 1;
}
```

---

### Bug 6: Search clear goes to Home instead of previous page ✅ FIXED

**File**: `middle_library.cpp:549-557`

**Fix Applied**:
```cpp
if (!query.empty()) {
    navigate_to_page("search", true);
} else {
    // Clear search -> go back to previous page in history (not home)
    if (history_index_ > 0) {
        // Navigate back without adding to history
        history_index_--;
        active_page_id_ = history_[history_index_];
        gtk_stack_set_visible_child_name(GTK_STACK(stack_), active_page_id_.c_str());
        update_toolbar_state();
        if (page_change_cb_) {
            page_change_cb_(active_page_id_);
        }
    } else {
        navigate_to_page("home", true);
    }
}
```

---

### Bug 7: SongsPage has DOUBLE scrolled window nesting ✅ FIXED

**Before**: SongsPage created a scrolled window as root + another scrolled window inside for the list.

**After** (`SongsPage.cpp:43-75`): Returns a `GtkBox` containing header + `GtkListBox` directly:
```cpp
void SongsPage::setup_ui() {
    // Root widget is a Box containing header + ListBox (both implement GtkScrollable)
    // MiddleLibrary provides the single shared scrolled window
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // ...
    list_box_.reset(gtk_list_box_new());
    // ...
    safe_box_append(GTK_BOX(main_box), header_box_.get());
    safe_box_append(GTK_BOX(main_box), list_box_.get());
    widget_.reset(main_box);
}
```

---

### Bug 8: FlowBox/ListBox are natively scrollable ✅ FIXED

**Principle**: `gtk_flow_box_new()` and `gtk_list_box_new()` implement `GtkScrollable` — they don't need a scrolled window wrapper. Wrapping them breaks kinetic scrolling, touch handling, and keyboard navigation.

**Applied to**: AlbumsPage, ArtistsPage, PlaylistsPage, GenresPage, FavouriteAlbumsPage, LikedMusicPage, SongsPage, HomePage sections.

---

## Remaining Work

### 1. Fix SearchResultsPage (Critical)
**File**: `frontend/src/ui/pages/SearchResultsPage.cpp`

**Changes needed**:
- Remove root `widget_` scrolled window (line 42)
- Remove inner `scrolled_window_` (line 75)
- Return `main_box` (Box containing header + ListBox) directly as widget
- Connect scroll handling to parent scrolled window (via MiddleLibrary signal like SongsPage)

### 2. Implement LeftSidebar::set_active_item_visual (Critical for Bug 4)
**File**: `frontend/src/panels/left_sidebar.cpp`

**Add implementation**:
```cpp
void LeftSidebar::set_active_item_visual(const std::string& item_id) {
    active_item_id_ = item_id;
    update_active_state();  // Only updates CSS classes, no navigation
}
```

### 3. Fix MainWindow callback (Bug 4)
**File**: `frontend/src/main_window.cpp:134-138`

**Change**:
```cpp
middle_library_->set_page_change_callback([this](const std::string& page_id) {
    if (left_sidebar_) {
        left_sidebar_->set_active_item_visual(page_id);  // Visual sync only
    }
});
```

---

## Architecture Summary (Post-Fix)

```
MiddleLibrary (single scrolled window)
├── Overlay (root)
│   ├── ScrolledWindow (THE scroller, vexpand, policy NEVER/AUTOMATIC)
│   │   └── ContentBox (vexpand=TRUE, valign=FILL, margins for toolbar)
│   │       └── Stack (pages)
│   │           ├── home        → HomePage (Box + ListBox/FlowBox)
│   │           ├── albums      → AlbumsPage (FlowBox)
│   │           ├── artists     → ArtistsPage (FlowBox)
│   │           ├── playlists   → PlaylistsPage (FlowBox)
│   │           ├── genres      → GenresPage (ListBox)
│   │           ├── songs       → SongsPage (Box + ListBox)
│   │           ├── favorites   → FavouriteAlbumsPage (FlowBox)
│   │           ├── liked       → LikedMusicPage (ListBox)
│   │           ├── search      → SearchResultsPage (Box + ListBox)  ← NEEDS FIX
│   │           ├── album_detail   → AlbumDetailPage
│   │           └── artist_detail  → ArtistDetailPage
│   └── GlassToolbar (overlay, floating)
```

---

## Verification Checklist

After implementing remaining fixes:

- [ ] SearchResultsPage returns content widget directly (no scrolled window)
- [ ] SearchResultsPage lazy-loading works via MiddleLibrary scroll signal
- [ ] LeftSidebar::set_active_item_visual implemented
- [ ] MainWindow uses set_active_item_visual for external sync
- [ ] Back/Forward buttons update sidebar highlight without navigation loop
- [ ] No double scrollbars anywhere
- [ ] Kinetic scrolling works on all pages
- [ ] Keyboard navigation (arrow keys, Tab) works on all pages
- [ ] Touch scrolling works on all pages
- [ ] Search clear returns to previous page correctly
- [ ] HomePage "View All" doesn't create duplicate history entries

---

## Files Modified (Fixed Bugs)

- `frontend/src/panels/middle_library.cpp` - Bugs 2, 5, 6
- `frontend/src/ui/pages/AlbumsPage.cpp` - Bugs 1, 3, 8
- `frontend/src/ui/pages/ArtistsPage.cpp` - Bugs 1, 3, 8
- `frontend/src/ui/pages/PlaylistsPage.cpp` - Bugs 1, 3, 8
- `frontend/src/ui/pages/GenresPage.cpp` - Bugs 1, 3, 8
- `frontend/src/ui/pages/FavouriteAlbumsPage.cpp` - Bugs 1, 3, 8
- `frontend/src/ui/pages/LikedMusicPage.cpp` - Bugs 1, 3, 8
- `frontend/src/ui/pages/SongsPage.cpp` - Bugs 1, 3, 7, 8
- `frontend/src/ui/components/HomePage.cpp` - Bug 8

---

## Files Needing Changes (Remaining Bugs)

- `frontend/src/ui/pages/SearchResultsPage.cpp` - Bug 1 (triple nesting)
- `frontend/src/panels/left_sidebar.cpp` - Bug 4 (missing set_active_item_visual)
- `frontend/src/main_window.cpp` - Bug 4 (callback uses wrong method)