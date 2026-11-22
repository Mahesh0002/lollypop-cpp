# CI/CD Pipeline Execution Plan

> **Duration**: 1 week setup + ongoing maintenance  
> **Goal**: Automated build, test, release, and deployment  
> **Branch**: `ci-cd-setup`

---

## 🎯 Pipeline Overview

```
┌─────────────┐   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐
│   Source    │──▶│   Build     │──▶│   Test      │──▶│  Artifact   │
│   Control   │   │  (ASan)     │   │  (Unit/E2E) │   │  (AppImage) │
└─────────────┘   └─────────────┘   └─────────────┘   └─────────────┘
       │                │                 │                 │
       ▼                ▼                 ▼                 ▼
  GitHub           Ubuntu 24.04      ASan + Unit       AppImage +
  Push/PR          Container         + Visual          Flatpak +
                                                              Snap
```

---

## 📋 CI Pipeline Stages

### 1. Pre-commit Hooks (Local)

**File**: `.pre-commit-config.yaml`

```yaml
repos:
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v4.6.0
    hooks:
      - id: trailing-whitespace
      - id: end-of-file-fixer
      - id: check-yaml
      - id: check-json
      - id: check-merge-conflict
      - id: debug-logger
  
  - repo: https://github.com/cpp-linter/cpplint
    rev: v1.6.1
    hooks:
      - id: cpplint
        args: ["--filter=-whitespace/braces,-whitespace/indent"]
  
  - repo: https://github.com/cpp-format/clang-format
    rev: v17.0.6
    hooks:
      - id: clang-format
  
  - repo: local
    hooks:
      - id: cmake-format
        name: cmake-format
        entry: cmake-format -i
        language: system
        types: [cmake]
      - id: shader-compile
        name: shader-compile
        entry: bash -c 'cmake --build build --target shaders'
        language: system
        pass_filenames: false
```

**Install**: `pip install pre-commit && pre-commit install`

---

### 2. GitHub Actions CI

**File**: `.github/workflows/ci.yml`

```yaml
name: CI

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]
  release:
    types: [published]

env:
  CMAKE_BUILD_TYPE: Debug
  ASAN_OPTIONS: detect_leaks=1:halt_on_error=0

jobs:
  lint:
    name: Lint & Format
    runs-on: ubuntu-latest
    container: ubuntu:24.04
    steps:
      - uses: actions/checkout@v4
      - name: Install linters
        run: apt-get update && apt-get install -y clang-format-17 cmake-format
      - name: Check format
        run: |
          clang-format-17 --dry-run --Werror $(find . -name "*.cpp" -o -name "*.hpp" -o -name "*.h")
          cmake-format --check CMakeLists.txt cmake/
  
  build:
    name: Build (Debug + ASan)
    needs: lint
    runs-on: ubuntu-latest
    container: ubuntu:24.04
    steps:
      - uses: actions/checkout@v4
      
      - name: Install dependencies
        run: |
          apt-get update && apt-get install -y \
            libgtk-4-dev libadwaita-1-dev libgraphene-1.0-dev \
            libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
            gstreamer1.0-plugins-{good,bad,ugly,base} \
            libsqlite3-dev libtag1-dev libglib2.0-dev \
            gobject-introspection libgirepository1.0-dev \
            google-mock google-test cmake ninja-build \
            gsk-glsl-compiler \
            libglib2.0-dev-bin  # for glib-compile-resources
      
      - name: Configure (ASan)
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g"
      
      - name: Build
        run: cmake --build build -j$(nproc)
      
      - name: Compile shaders
        run: cmake --build build --target shaders
      
      - name: Verify artifacts
        run: |
          ls -la build/shaders/*.gskshader
          ls -la build/lollypop-cpp
      
      - name: Upload build artifacts
        uses: actions/upload-artifact@v4
        with:
          name: build-artifacts
          path: |
            build/lollypop-cpp
            build/shaders/*.gskshader
          retention-days: 7

  test:
    name: Unit Tests (ASan)
    needs: build
    runs-on: ubuntu-latest
    container: ubuntu:24.04
    steps:
      - uses: actions/checkout@v4
      - name: Download artifacts
        uses: actions/download-artifact@v4
        with:
          name: build-artifacts
          path: build
      - name: Make executable
        run: chmod +x build/lollypop-cpp
      - name: Run unit tests
        run: |
          cd build
          ctest --output-on-failure -j$(nproc)
      - name: Run stress test
        run: |
          timeout 300 ./scripts/stress_test.sh || true
      
  visual-regression:
    name: Visual Regression
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Download artifacts
        uses: actions/download-artifact@v4
        with:
          name: build-artifacts
          path: build
      - name: Run visual tests
        run: |
          xvfb-run -a ./scripts/visual_test.sh
      - uses: actions/upload-artifact@v4
        if: failure()
        with:
          name: visual-diffs
          path: tests/visual/diffs/
          retention-days: 7

  performance:
    name: Performance Budgets
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Download artifacts
        uses: actions/download-artifact@v4
        with:
          name: build-artifacts
          path: build
      - name: Run perf tests
        run: |
          ./build/perf_test_suite
      - name: Check budgets
        run: |
          python3 scripts/check_perf_budgets.py

  deploy:
    name: Deploy Release
    needs: [test, visual-regression, performance]
    if: github.event_name == 'release' && github.event.action == 'published'
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Download artifacts
        uses: actions/download-artifact@v4
        with:
          name: build-artifacts
          path: build
      - name: Build AppImage
        run: |
          ./scripts/build_appimage.sh
      - name: Build Flatpak
        run: |
          flatpak-builder --force-clean build-dir org.lollypop.Lollypop.yml
      - name: Upload to release
        uses: softprops/action-gh-release@v1
        with:
          files: |
            build/lollypop-cpp.AppImage
            build/org.lollypop.Lollypop.flatpak
```

---

### 3. Release Automation

**File**: `.github/workflows/release.yml`

```yaml
name: Release

on:
  release:
    types: [published]

jobs:
  build-release:
    runs-on: ubuntu-latest
    container: ubuntu:24.04
    steps:
      - uses: actions/checkout@v4
      
      - name: Install deps
        run: |
          apt-get update && apt-get install -y \
            libgtk-4-dev libadwaita-1-dev libgraphene-1.0-dev \
            libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
            gstreamer1.0-plugins-{good,bad,ugly,base} \
            libsqlite3-dev libtag1-dev libglib2.0-dev \
            gobject-introspection libgirepository1.0-dev \
            google-mock google-test cmake ninja-build \
            gsk-glsl-compiler \
            appimagetool flatpak-builder
      
      - name: Configure (Release)
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release
      
      - name: Build
        run: cmake --build build -j$(nproc)
      
      - name: Build AppImage
        run: |
          ./scripts/build_appimage.sh
      
      - name: Build Flatpak
        run: |
          flatpak-builder --force-clean build-dir org.lollypop.Lollypop.yml
      
      - name: Generate checksums
        run: |
          sha256sum build/lollypop-cpp.AppImage > SHA256SUMS
          sha256sum build/org.lollypop.Lollypop.flatpak >> SHA256SUMS
      
      - name: Upload to release
        uses: softprops/action-gh-release@v1
        with:
          files: |
            build/lollypop-cpp.AppImage
            build/org.lollypop.Lollypop.flatpak
            SHA256SUMS
```

---

## 📦 AppImage Build Script

**File**: `scripts/build_appimage.sh`

```bash
#!/bin/bash
set -e

APP_NAME="lollypop-cpp"
VERSION=$(grep -m1 'version' CMakeLists.txt | sed 's/.*version[^0-9]*\([0-9.]*\).*/\1/')
APPDIR="build/${APP_NAME}.AppDir"

# Clean
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
mkdir -p "${APPDIR}/usr/share/lollypop-cpp/shaders"
mkdir -p "${APPDIR}/usr/share/lollypop-cpp/resources"

# Copy binary
cp build/lollypop-cpp "${APPDIR}/usr/bin/"

# Copy shaders
cp build/shaders/*.gskshader "${APPDIR}/usr/share/lollypop-cpp/shaders/"

# Copy resources
cp -r resources/* "${APPDIR}/usr/share/lollypop-cpp/resources/"

# Desktop entry
cat > "${APPDIR}/usr/share/applications/org.lollypop.Lollypop.desktop" <<EOF
[Desktop Entry]
Name=Lollypop
Comment=Music player
Exec=lollypop-cpp
Icon=org.lollypop.Lollypop
Terminal=false
Type=Application
Categories=Audio;Music;Player;
StartupNotify=true
MimeType=audio/mpeg;audio/flac;audio/ogg;audio/x-ms-wma;
EOF

# Icon
cp resources/icons/lollypop.png "${APPDIR}/usr/share/icons/hicolor/256x256/apps/org.lollypop.Lollypop.png"

# AppRun
cat > "${APPDIR}/AppRun" <<'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS}"
export GTK_PATH="${HERE}/usr/lib/gtk-4.0"
exec "${HERE}/usr/bin/lollypop-cpp" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

# Download appimagetool
wget -q "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" -O appimagetool
chmod +x appimagetool

# Build
ARCH=x86_64 ./appimagetool "${APPDIR}" "build/lollypop-cpp-${VERSION}-x86_64.AppImage"

echo "AppImage built: build/lollypop-cpp-${VERSION}-x86_64.AppImage"
```

---

## 📦 Flatpak Manifest

**File**: `org.lollypop.Lollypop.yml`

```yaml
app-id: org.lollypop.Lollypop
runtime: org.gnome.Platform
runtime-version: '46'
sdk: org.gnome.Sdk
command: lollypop-cpp
finish-args:
  - --share=ipc
  - --socket=wayland
  - --socket=pulseaudio
  - --filesystem=home
  - --filesystem=xdg-run/pipewire-0:create
  - --device=all
  - --talk-name=org.freedesktop.portal.Desktop
  - --talk-name=org.freedesktop.secrets
  - --env=GST_PLUGIN_SYSTEM_PATH=/app/lib/gstreamer-1.0
cleanup:
  - /include
  - /lib/pkgconfig
  - /share/gtk-doc
modules:
  - name: lollypop-cpp
    buildsystem: cmake-ninja
    config-opts:
      - -DCMAKE_BUILD_TYPE=Release
    sources:
      - type: git
        url: https://github.com/Mahesh0002/lollypop-cpp.git
        tag: v${VERSION}
    build-options:
      env:
        LD_LIBRARY_PATH: /app/lib:/app/lib/x86_64-linux-gnu
```

---

## 🔄 Dependency Management

### CMake FetchContent (Optional)

```cmake
# For external deps not in distro
include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)
```

---

## 🔐 Secrets Management

| Secret | Purpose | Storage |
|--------|---------|---------|
| `FLATPAK_GPG_KEY` | Flatpak signing | GitHub Secrets |
| `APP_IMAGE_SIGN_KEY` | AppImage signing | GitHub Secrets |
| `DOCKER_HUB_TOKEN` | Container images | GitHub Secrets |

---

## 📊 Monitoring & Alerts

| Metric | Alert Threshold |
|--------|-----------------|
| Build failure rate | > 10% in 24h |
| Test failure rate | > 5% in 24h |
| Visual regression failures | Any |
| Performance regression | > 20% budget |

**Notifications**: GitHub Actions → Slack/Discord webhook

---

## 📝 Maintenance Schedule

| Frequency | Task |
|-----------|------|
| Daily | CI runs on push/PR |
| Weekly | Dependency updates (dependabot) |
| Monthly | Base image update (Ubuntu) |
| Per release | AppImage/Flatpak build + sign |
| Quarterly | Security audit (cargo audit, osv-scanner) |

---

## 📝 Notes

- **Container**: Ubuntu 24.04 for GTK 4.14+ support
- **ASan**: Enabled in CI for memory safety
- **Visual tests**: Run in xvfb
- **Release artifacts**: AppImage + Flatpak (primary), Snap (optional)
- **Versioning**: Semantic versioning from CMakeLists.txt

---

**Next**: Set up GitHub repo with these workflows, enable dependabot, configure secrets