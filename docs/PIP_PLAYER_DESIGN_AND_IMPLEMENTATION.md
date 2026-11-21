# PiP (Picture-in-Picture) Player Design and Implementation

> **Project:** Lollypop C++ Desktop Music Player  
> **Related:** `frontendPlan.md` Phase 12, Section 15 (PiP integration boundary)

---

## 1. Overview

The PiP (Picture-in-Picture) player is a compact, floating window that displays the
current track and basic playback controls. It reuses the same shared state as the
main application — it does **not** create a second playback engine.

**Authority model:** The shell's `MediaControls.qml` remains the sole playback
authority. The PiP window is a read-only display (with optional pass-through
controls in a future phase), observing `PlaybackState` through `IPlayerService`
callbacks.

---

## 2. Visual Direction

```
┌───────────────────────────────────────────────────────┐
│                                                       │
│  ┌─────────┐  Track title                              │
│  │         │  Artist name                              │
│  │ ARTWORK │                                           │
│  │         │      Previous   Play/Pause   Next         │
│  └─────────┘                                           │
│                                                       │
└───────────────────────────────────────────────────────┘
```

- Compact, rounded floating player (~400×140 px)
- Artwork on the left, track info in the middle, controls on the right
- Shares the same `PlaybackState`, artwork cache, accent color, and visualizer
  model as the main window

---

## 3. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Shell (Quickshell)                     │
│  MediaControls.qml ───────┐                                 │
│                           ▼                                 │
│                    MPRIS D-Bus                              │
│                           ▼                                 │
│              lollypop-cpp MprisServer                       │
│                           ▼                                 │
│                    BackendController                        │
│  • PlaybackState (single source of truth)                   │
│  • GStreamer / AudioEngine                                  │
│                           ▼                                 │
│         Callbacks (track/position/state/visualization)      │
│                           ▼                                 │
│   ┌─────────────────┐  ┌──────────────────────────────┐   │
│   │ Main Window     │  │ PiP Window (floating)        │   │
│   │ (read-only)     │  │ (read-only, shared state)    │   │
│   └─────────────────┘  └──────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. Reused Components

| Component | Usage in PiP |
|-----------|--------------|
| `PlaybackState` | Single source of truth — observed, never owned |
| `IPlayerService` | Interface for queries (no writes from PiP) |
| `ArtworkCache` | Artwork lookup by key, file-backed for display |
| `AccentColorManager` | Accent color for controls and waveform |
| `VisualizerModel` | Shared sample data for waveform |
| `WaveformView` | Compact waveform display |
| `ThemeManager` | Application-scoped CSS custom properties |

---

## 5. Implementation

### 5.1 PiP Window Class

**File:** `frontend/include/panels/pip_window.hpp`, `frontend/src/panels/pip_window.cpp`

The PiP window is a `GtkWindow` (undecorated, non-resizable) that:

- Displays current artwork, title, artist
- Shows Previous / Play-Pause / Next buttons (read-only, insensitive by default)
- Displays a compact `WaveformView`
- Tracks playback state via `IPlayerService` callbacks
- Positions itself as a floating window (no always-on-top guarantee — see §6)

### 5.2 Creation and Lifetime

The `Application` class creates the PiP window on demand (lazy initialization)
and keeps it alive until the application shuts down or the user closes it.

```cpp
void Application::show_pip() {
    if (!pip_window_) {
        pip_window_ = std::make_unique<PiPWindow>(this);
        pip_window_->set_backend(backend_.get());
        pip_window_->present();
    } else {
        pip_window_->present();
    }
}
```

### 5.3 State Synchronization

The PiP window registers the same callbacks as the main window's panels:

- `track_changed_cb_` → update artwork, title, artist
- `position_changed_cb_` → update progress and time labels
- `playback_state_changed_cb_` → update Play/Pause icon
- `visualization_cb_` → update `WaveformView` samples

All callbacks are marshalled to the GTK main context by `BackendController`.

---

## 6. Wayland and Hyprland

GTK 4 does **not** provide a portable always-on-top mechanism on Wayland.
Window stacking order is controlled by the compositor. Therefore:

- **No guaranteed always-on-top:** The PiP window is a normal `GtkWindow`.
  Do not claim GTK provides keep-above behavior.
- **Hyprland:** Users who want PiP to float above other windows should add a
  Hyprland rule, e.g.:

  ```ini
  # ~/.config/hypr/hyprland.conf
  windowrule = float, ^(lollypop-cpp-pip)$
  windowrule = nopfocus, ^(lollypop-cpp-pip)$
  ```

  These rules are **documented, not applied automatically**. The application
  must not silently edit the user's Hyprland configuration.
- **Floating mode:** The PiP window uses `gtk_window_set_decorated(FALSE)` and
  a fixed default size. It works as an ordinary separate window without
  compositor rules.

---

## 7. Files

| File | Purpose |
|------|---------|
| `frontend/include/panels/pip_window.hpp` | PiP window class declaration |
| `frontend/src/panels/pip_window.cpp` | PiP window implementation |
| `docs/PIP_PLAYER_DESIGN_AND_IMPLEMENTATION.md` | This document |

---

## 8. Acceptance Criteria

- [ ] PiP window displays the same current track as the main window
- [ ] PiP does not create a second playback engine (reuses `IPlayerService`)
- [ ] PiP artwork, title, artist update when the shell changes track
- [ ] PiP waveform updates with the same `VisualizerModel`
- [ ] PiP buttons are read-only (insensitive) by default
- [ ] PiP works as an ordinary window without Hyprland rules
- [ ] Hyprland float/nopfocus rules are documented (not auto-applied)
- [ ] No always-on-top guarantee is claimed