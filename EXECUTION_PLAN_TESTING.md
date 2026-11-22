# Testing & Validation Execution Plan

> **Duration**: Ongoing (parallel with all phases)  
> **Goal**: Automated quality gates, visual regression, performance budgets  
> **Branch**: `testing-infra`

---

## 🎯 Testing Pyramid

```
        ┌─────────────┐
        │   E2E/UI    │  ← 5 tests (critical paths)
        ├─────────────┤
        │ Integration │  ← 20 tests (component + backend)
        ├─────────────┤
        │   Unit      │  ← 100+ tests (logic, shaders, utils)
        └─────────────┘
```

---

## 📋 Test Categories

### 1. Unit Tests (Target: 100+)

| Module | Test File | Coverage Target |
|--------|-----------|-----------------|
| `AudioEngine` | `test_audio_engine.cpp` | 90% |
| `QueueManager` | `test_queue_manager.cpp` | 95% |
| `StorageManager` | `test_storage.cpp` | 90% |
| `LyricsManager` | `test_lyrics.cpp` | 85% |
| `CacheManager` | `test_cache.cpp` | 90% |
| `LibraryScanner` | `test_scanner.cpp` | 80% |
| `ShaderManager` | `test_shaders.cpp` | 95% |
| `StackNavigator` | `test_stack_nav.cpp` | 95% |
| `ThemeManager` | `test_theme.cpp` | 85% |

**Framework**: GoogleTest + GoogleMock

```cpp
// Example: QueueManager test
TEST(QueueManagerTest, PlayNextRespectsOrder) {
    QueueManager qm;
    qm.set_queue({"a", "b", "c"}, 0);
    
    qm.enqueue("x", true);  // Play next
    EXPECT_EQ(qm.queue(), std::vector{"a", "x", "b", "c"});
    
    qm.next_index();  // Should play "x"
    EXPECT_EQ(qm.current_index(), 1);
}
```

---

### 2. Integration Tests (20 tests)

| Test | Description |
|------|-------------|
| `scan_and_play` | Scan library → select track → verify playback starts |
| `search_and_play` | Search → select result → verify track loads |
| `queue_operations` | Add/remove/move/reorder → verify queue state |
| `shuffle_repeat` | Toggle modes → verify playback order |
| `volume_persistence` | Set volume → restart → verify restored |
| `mpris_control` | External MPRIS client → play/pause/next/prev |
| `pip_window` | Open PiP → verify sync with main window |
| `lyrics_sync` | Play synced track → verify lyrics highlight |
| `artwork_loading` | Scroll grid → verify async load + cancel |
| `theme_switch` | Change accent → verify all panels update |

---

### 3. E2E/UI Tests (5 critical paths)

| Test | Steps |
|------|-------|
| `cold_start_play` | Launch → scan → select album → play track |
| `search_play` | Search → click result → verify playback |
| `queue_management` | Add 5 tracks → reorder → remove → play |
| `settings_pip` | Open PiP → move → close → verify main window |
| `media_keys` | Press media keys → verify play/pause/next/prev |

**Tool**: `gtk4-test` + custom test harness

---

## 🎨 Visual Regression Testing

### Tool: `pixelmatch` + `playwright`

**Baseline Images**: `tests/visual/baselines/`

| View | Baseline | Tolerance |
|------|----------|-----------|
| Main window (expanded) | `main_expanded.png` | 0.1% |
| Main window (compact) | `main_compact.png` | 0.1% |
| Album grid | `albums_grid.png` | 0.2% |
| Artist detail | `artist_detail.png` | 0.2% |
| Now playing | `now_playing.png` | 0.1% |
| Bottom bar | `bottom_bar.png` | 0.1% |
| PiP window | `pip_window.png` | 0.2% |
| Search results | `search_results.png` | 0.2% |
| Dark mode | `dark_mode.png` | 0.1% |
| High contrast | `high_contrast.png` | 0.2% |

**Test Script**: `tests/visual/run_visual_tests.sh`
```bash
#!/bin/bash
# 1. Launch app with test profile
# 2. Navigate to each view
# 3. Screenshot
# 3. Compare with pixelmatch (threshold 0.1%)
# 4. Generate diff images on failure
```

---

## ⚡ Performance Budgets

| Metric | Budget | Measurement |
|--------|--------|-------------|
| Cold start | < 2s | `gtk-launch-time` |
| Frame time | < 16ms (60fps) | `GtkFrameClock` |
| Memory (idle) | < 200MB RSS | `massif` |
| Memory (playing) | < 300MB RSS | `massif` |
| Library scan | < 5s/1000 tracks | Custom timer |
| Search latency | < 100ms | `perf` |
| Shader compile | < 500ms | Build time |
| Shader switch | < 16ms | Frame time |

**CI Gate**: Fail if any budget exceeded by >20%

---

## 🤖 CI/CD Pipeline

### GitHub Actions Workflow: `.github/workflows/ci.yml`

```yaml
name: CI

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    container: ubuntu:24.04
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: |
          apt-get update && apt-get install -y \
            libgtk-4-dev libadwaita-1-dev libgraphene-1.0-dev \
            libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
            gstreamer1.0-plugins-{good,bad,ugly} \
            libsqlite3-dev libtag1-dev libglib2.0-dev \
            gobject-introspection libgirepository1.0-dev \
            google-mock google-test cmake ninja-build \
            gsk-glsl-compiler  # GTK 4.14+
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address"
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Unit tests
        run: cd build && ctest --output-on-failure -j$(nproc)
      - name: Shader compilation
        run: cmake --build build --target shaders
      - name: Unit tests (ASan)
        run: cd build && ./test_* --gtest_filter="*"
      
  visual-regression:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Run visual tests
        run: |
          xvfb-run -a ./scripts/visual_test.sh
      - uses: actions/upload-artifact@v4
        if: failure()
        with:
          name: visual-diffs
          path: tests/visual/diffs/
      
  performance:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - name: Run perf tests
        run: |
          ./build/perf_test_suite
      - name: Check budgets
        run: |
          python3 scripts/check_perf_budgets.py
      
  deploy-docs:
    needs: [build, visual-regression, performance]
    if: github.ref == 'refs/heads/main'
    runs-on: ubuntu-latest
    steps:
      - name: Deploy docs
        run: ./scripts/deploy_docs.sh
```

---

## 🔧 Local Test Commands

```bash
# Unit tests
cd build && ctest --output-on-failure -j$(nproc)

# Specific test
cd build && ./test_queue_manager --gtest_filter="QueueManagerTest.PlayNextRespectsOrder"

# ASan build
mkdir build-asan && cd build-asan
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" ..
make -j$(nproc)
./test_suite

# Visual regression
xvfb-run -a ./scripts/visual_test.sh

# Performance
./build/perf_test_suite
python3 scripts/check_perf_budgets.py

# Stress test
./scripts/stress_test.sh  # 10k rapid nav events
```

---

## 📊 Coverage Reports

```bash
# Generate coverage
cmake -DCMAKE_CXX_FLAGS="--coverage" ..
make -j$(nproc)
ctest
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

**Targets**:
- Line coverage: >85%
- Branch coverage: >80%
- Function coverage: >90%

---

## 📝 Test Data Management

| Data | Location | Size |
|------|----------|------|
| Test audio files | `tests/data/audio/` | ~50MB |
| Test artwork | `tests/data/artwork/` | ~10MB |
| Test lyrics | `tests/data/lyrics/` | ~1MB |
| Test database | `tests/data/test.db` | ~5MB |

**Fixtures**: `tests/fixtures/` - pre-built test databases

---

## 📝 Notes

- **GTK 4.14+** required for `GskGLShader` testing
- **Headless**: Use `xvfb-run -a` for CI
- **Flaky tests**: Tag with `@flaky` and quarantine
- **Test data**: Commit small samples; generate large via script
- **Shader tests**: Verify `.gskshader` loads and renders

---

**Next**: Phase 1 stability tests run first; other tests added incrementally