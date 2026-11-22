# Phase 1 Execution Plan: Stability & Foundation

> **Duration**: 1-2 weeks  
> **Goal**: Zero segfaults under ASan, safe navigation patterns, cancelable async  
> **Branch**: `phase1-stability`

---

## 🎯 Objectives

| Objective | Success Criteria |
|-----------|------------------|
| Zero segfaults | ASan clean under 10k rapid nav events |
| Safe navigation | `StackNavigator` manages all `GtkStack` transitions |
| Cancelable async | All artwork/lyrics loads use `GTask` + `GCancellable` |
| Memory safety | ASan + LeakSanitizer clean |

---

## 📋 Task Breakdown

### 1.1 Audit & Fix Double-Parenting (Days 1-2)

| File | Issue | Fix |
|------|-------|-----|
| `frontend/src/panels/middle_library.cpp` | `gtk_stack_add_named()` without unparenting | Use `safe_stack_add_named()` |
| `frontend/src/ui/pages/AlbumsPage.cpp` | `gtk_box_append()` on widgets that may have parent | Use `safe_box_append()` |
| `frontend/src/ui/pages/ArtistsPage.cpp` | Same | Same |
| `frontend/src/ui/pages/GenresPage.cpp` | Same | Same |
| `frontend/src/ui/pages/PlaylistsPage.cpp` | Same | Same |
| `frontend/src/ui/pages/AlbumDetailPage.cpp` | `gtk_stack_add_named()` on detail pages | Use `safe_stack_add_named()` |
| `frontend/src/ui/pages/ArtistDetailPage.cpp` | Same | Same |

**Action**: Replace all direct GTK container calls with `safe_gtk.hpp` equivalents.

```cpp
// Before
gtk_box_append(GTK_BOX(box), widget);

// After
safe_box_append(GTK_BOX(box), widget);
```

---

### 1.2 ArtworkView Lifecycle Fix (Days 2-3)

**File**: `frontend/src/ui/components/ArtworkView.cpp`

| Problem | Fix |
|---------|-----|
| Async callback fires after widget destroyed | Store `GCancellable*` in widget; cancel in `dispose()` |
| `GTask` not cancelled on destroy | Store task pointer; `g_task_return_error_if_cancelled()` in `dispose()` |
| No weak reference to self | Use `std::weak_ptr<ArtworkView>` in callbacks |

```cpp
// ArtworkView.hpp additions
class ArtworkView {
    GCancellable* cancellable_ = nullptr;
    GTask* current_task_ = nullptr;
    
    void dispose() {
        if (cancellable_) g_cancellable_cancel(cancellable_);
        if (current_task_) g_task_return_error_if_cancelled(current_task_);
        // ... parent dispose
    }
};
```

---

### 1.3 StackNavigator Implementation (Days 3-4)

**New Files**: `frontend/include/ui/components/StackNavigator.hpp`, `frontend/src/ui/components/StackNavigator.cpp`

```cpp
// StackNavigator.hpp
class StackNavigator {
public:
    explicit StackNavigator(GtkStack* stack);
    
    // Push new page with spring transition
    void push(const std::string& page_name, GtkWidget* widget);
    
    // Pop with reverse transition
    void pop();
    
    // Replace current page (no animation)
    void replace(const std::string& page_name, GtkWidget* widget);
    
    // Get current page name
    std::string current_page() const;
    
private:
    GtkStack* stack_;
    std::vector<std::string> history_;
    std::mutex mutex_;
};
```

**Key behaviors**:
- `push()`: `gtk_stack_set_transition_type(GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT)` + `gtk_stack_set_visible_child()`
- `pop()`: Reverse transition + history pop
- All operations use `safe_stack_add_named()` / `safe_stack_remove()`

---

### 1.4 Async Cancellation Infrastructure (Days 4-5)

**File**: `frontend/src/ui/components/ArtworkView.cpp`, `LyricsView.cpp`

```cpp
// Pattern for all async artwork loads
void ArtworkView::load_artwork(const std::string& path) {
    // Cancel previous
    if (cancellable_) g_cancellable_cancel(cancellable_);
    cancellable_ = g_cancellable_new();
    
    auto task = g_task_new(this, cancellable_, 
        [](GObject*, GAsyncResult* res, gpointer) {
            g_task_propagate_pointer(G_TASK(res), nullptr);
        }, nullptr);
    current_task_ = G_TASK(task);
    
    g_task_run_in_thread(task, [](GTask* task) {
        // Load image in thread
        if (g_task_return_error_if_cancelled(task)) return;
        // ... load pixbuf
        g_task_return_pointer(task, pixbuf, g_object_unref);
    });
}
```

---

### 1.5 AddressSanitizer Integration (Day 5)

**CMakeLists.txt**:
```cmake
# Add sanitizer builds
set(CMAKE_CXX_FLAGS_ASAN "-fsanitize=address -fno-omit-frame-pointer -g")
set(CMAKE_CXX_FLAGS_LSAN "-fsanitize=leak")

# Add build types
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS_ASAN}")
# Or create separate target: lollypop-cpp-asan
```

**Test script**: `scripts/stress_test.sh`
```bash
#!/bin/bash
# Rapid navigation stress test
for i in {1..10000}; do
    # Simulate rapid nav: home -> albums -> artists -> playlists -> genres -> songs -> home
    # Use gtk-test or custom script
done
```

---

## ✅ Definition of Done

| Check | Verification |
|-------|--------------|
| Zero ASan errors | `./build/lollypop-cpp-asan` runs 10k nav events clean |
| Zero LeakSan errors | `lsan` reports 0 leaks after stress test |
| No double-parenting | `gtk-inspector` shows clean widget tree |
| Safe navigation | `StackNavigator` used everywhere |
| Async cancelled | No callbacks after widget destroy |

---

## 🚀 Quick Start Commands

```bash
# 1. Create branch
git checkout -b phase1-stability

# 2. Build with ASan
mkdir build-asan && cd build-asan
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" ..
make -j$(nproc)

# 3. Run stress test
./scripts/stress_test.sh

# 4. Verify
./lollypop-cpp  # Manual smoke test
```

---

## 📝 Notes

- **Priority**: Fix `middle_library.cpp` first (main navigation hub)
- **Test incrementally**: Commit after each file fixed
- **ASan build time**: ~2-3x slower; run stress test overnight
- **Commit message format**: `fix: middle_library - safe stack navigation`

---

**Next**: Phase 2 (Glass Materials) starts after Phase 1 DoD met.