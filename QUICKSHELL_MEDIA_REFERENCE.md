# Quickshell Media Control Reference

Reference files from `/home/mahesh/.config/quickshell/end4-pC/` for implementing MPRIS, CAVA visualization, and media controls in Lollypop C++.

---

## File Structure

```
end4-pC/
├── services/
│   ├── MprisController.qml        # MPRIS player management singleton
│   ├── LyricsService.qml          # Lyrics fetching & sync
│   ├── DesktopWidgets.qml
│   ├── SongRec.qml
│   ├── Ai.qml
│   ├── PresetsOnline.qml
│   └── SettingsQuickControls.qml
├── modules/common/
│   ├── widgets/
│   │   ├── WaveVisualizer.qml     # Canvas-based wavy visualizer (THE KEY FILE)
│   │   ├── Player.qml             # Full player widget with art, visualizer, content
│   │   ├── PlayerContent.qml      # Track info, progress bar, controls, lyrics
│   │   ├── Visualizer.qml         # Simple bar visualizer
│   │   ├── LayoutSection.qml
│   │   ├── DirectoryIcon.qml
│   │   ├── AttachedFileIndicator.qml
│   │   ├── RippleButton.qml
│   │   ├── StyledSlider.qml
│   │   ├── StyledProgressBar.qml
│   │   ├── MaterialSymbol.qml
│   │   ├── MaterialDial.qml
│   │   ├── DashboardCard.qml
│   │   └── StyledComboBoxSearch.qml
│   ├── models/
│   ├── functions/
│   │   ├── StringUtils.qml        # cleanMusicTitle, friendlyTimeForSeconds
│   │   └── NotificationUtils.qml
│   ├── Directories.qml
│   ├── DockStyle.qml
│   └── ColorUtils.qml
├── modules/ii/
│   ├── bar/
│   │   ├── DiMedia.qml
│   │   └── Visualizer.qml
│   ├── sidebarLeft/
│   │   └── SidebarPlayerControl.qml
│   ├── sidebarRight/
│   │   └── SidebarRightContent.qml
│   ├── mediaControls/
│   │   └── MediaControls.qml      # Main media controls panel
│   ├── equalizer/
│   │   └── EqualizerPopup.qml
│   ├── background/widgets/visualizer/
│   │   ├── VisualizerWidget.qml
│   │   └── VisualizerEngine.qml
│   └── settings/
│       └── DashboardMediaPage.qml # CAVA integration example
├── scripts/cava/
│   └── raw_output_config.txt      # CAVA config for raw ASCII output
├── GlobalStates.qml               # Shared state (visualizerPoints, etc.)
└── Directories.qml                # Path constants
```

---

## Key Implementation Details

### 1. CAVA Integration (from DashboardMediaPage.qml)

```qml
Process {
    id: cavaProc
    running: root.visible && root.media.playing
    command: ["cava", "-p", `${FileUtils.trimFileProtocol(Directories.scriptPath)}/cava/raw_output_config.txt`]
    onRunningChanged: if (!running) root.visualizerPoints = []
    stdout: SplitParser {
        onRead: data => root.visualizerPoints = data.split(";").map(p => parseFloat(p.trim())).filter(p => !isNaN(p))
    }
}
```

**CAVA Config** (`raw_output_config.txt`):
```ini
[general]
mode = waves
framerate = 60
autosens = 1
bars = 50

[output]
method = raw
raw_target = /dev/stdout
data_format = ascii
channels = mono
mono_option = average

[smoothing]
noise_reduction = 20
```

**Output Format**: ASCII semicolon-separated values (e.g., `123;456;789;...`)

### 2. WaveVisualizer.qml (Core Visualizer)

```qml
Canvas {
    id: root
    property list<var> points
    property real maxVisualizerValue: 1000
    property int smoothing: 2
    property bool live: true
    property color color: Appearance.m3colors.m3primary

    // Paint throttling (~30fps cap)
    Timer { id: paintThrottle; interval: 33; ... }

    onPaint: {
        // 1. Clear canvas
        // 2. Apply smoothing (moving average)
        // 3. Draw filled path from bottom
        // 4. Fill with color at 15% opacity
        
        // MultiEffect blur/saturation layer for "wavy" look
        layer.enabled: true
        layer.effect: MultiEffect {
            saturation: 0.2
            blurEnabled: true
            blurMax: 7
            blur: 1
        }
    }
}
```

**Key Techniques**:
- Moving average smoothing (configurable window)
- Paint throttling to prevent memory growth
- MultiEffect for blur + desaturation
- Filled path from bottom (mirrored wave)

### 3. MprisController.qml (MPRIS Management)

```qml
Singleton {
    // Player filtering (removes duplicates: firefox, chromium, playerctld, mpd)
    property list<MprisPlayer> players: Mpris.players.values.filter(player => isRealPlayer(player))
    
    // Active player selection priority:
    // 1. Manual override
    // 2. Preferred player (config)
    // 3. Tracked player (last playing)
    // 4. First available
    property MprisPlayer activePlayer: manualPlayer ?? preferredPlayer ?? trackedPlayer ?? Mpris.players.values[0] ?? null
    
    // Track metadata
    property var activeTrack: { uniqueId, artUrl, title, artist, album }
    
    // Playback controls
    function togglePlaying() { activePlayer.togglePlaying() }
    function previous() { activePlayer.previous() }
    function next() { activePlayer.next() }
    
    // Loop/Shuffle
    property var loopState: activePlayer?.loopState ?? MprisLoopState.None
    function setLoopState(state) { activePlayer.loopState = state }
    function setShuffle(bool) { activePlayer.shuffle = bool }
}
```

### 4. PlayerContent.qml (UI Components)

**Layout Structure**:
- Artwork (left) with hover volume dial
- Track title/artist (animated transitions)
- Progress bar (Wavy slider or simple bar)
- Play/Pause (large circular button)
- Prev/Next/Lyrics/Equalizer buttons
- Lyrics panel (slides in/out)

**Progress Bar**:
```qml
StyledSlider {
    configuration: StyledSlider.Configuration.Wavy  // Uses WaveVisualizer internally
    value: player.position / player.length
    onMoved: player.position = value * player.length
}
```

**Wavy Progress** switches to simple bar when not seekable:
```qml
Loader {
    active: player?.canSeek ?? false
    sourceComponent: StyledSlider { configuration: StyledSlider.Configuration.Wavy }
}
Loader {
    active: !(player?.canSeek ?? false)
    sourceComponent: StyledProgressBar { wavy: player?.isPlaying }
}
```

### 5. LyricsService.qml

- Fetches via Python script (`lyrics.py`)
- LRC format parsing with timestamps
- Real-time sync with position tracking
- Slot-based rendering (3 before, 3 after current line)
- Auto-resync every 4 seconds

---

## GTK4/C++ Implementation Mapping

| Quickshell Component | GTK4/C++ Equivalent |
|---------------------|---------------------|
| `MprisController` | `libmpris` / `Gio.DBusProxy` for MPRIS2 |
| `cava Process` | `GSubprocess` or `pipewire` + custom FFT |
| `WaveVisualizer (Canvas)` | `GtkDrawingArea` + Cairo |
| `MultiEffect blur` | Cairo blur or `GskBlurNode` |
| `StyledSlider.Wavy` | Custom `GtkWidget` with Cairo draw |
| `PlayerContent` | `GtkBox`/`GtkGrid` layout |
| `LyricsService` | C++ class + `Glib::TimeoutSource` |
| `MprisLoopState` | `Gio.DBusProxy` property mapping |

---

## CAVA Integration for GTK4

### Option A: Subprocess (Simple)
```cpp
class CavaVisualizer {
    Glib::RefPtr<Gio::Subprocess> cava_proc_;
    std::vector<double> points_;
    
    void start() {
        cava_proc_ = Gio::Subprocess::create_sync(
            {"cava", "-p", "/path/to/config.txt"},
            Gio::SUBPROCESS_FLAGS_STDOUT_PIPE
        );
        // Read stdout line by line in a thread
    }
    
    void on_data(const Glib::ustring& line) {
        // Parse "123;456;789;" format
        points_ = parse_ascii_points(line);
        // Emit signal for UI update
    }
};
```

### Option B: PipeWire (Native, Better)
```cpp
// Use PipeWire for audio capture + FFT
// More complex but lower latency, no external process
```

---

## PiP (Picture-in-Picture) Implementation

### Quickshell Approach
- Separate window with compact player
- Shared `MprisController` singleton
- Animated transitions

### GTK4 Approach
```cpp
class PipWindow : public Gtk::Window {
public:
    PipWindow(Application* app) {
        set_type_hint(Gdk::WINDOW_TYPE_HINT_DIALOG);
        set_decorated(false);
        set_keep_above(true);
        set_resizable(false);
        set_default_size(400, 120);
        
        // Compact layout: Art | Title/Artist | Controls | Progress
    }
};

// In Application:
void on_pip_clicked() {
    if (!pip_window_) pip_window_ = std::make_unique<PipWindow>(this);
    pip_window_->present();
}
```

**Shared State**: Both main window and PiP observe same `BackendController` callbacks.

---

## Color/Theme System

Quickshell uses Material 3 dynamic colors:
```qml
property color artDominantColor: ColorUtils.mix(...)
property QtObject blendedColors: AdaptedMaterialScheme { color: artDominantColor }
```

**GTK4 Equivalent**: Extract dominant color from artwork → generate CSS variables → apply via `GtkCssProvider`.

---

## Key Files to Reference

| File | Purpose |
|------|---------|
| `WaveVisualizer.qml` | Core wavy visualizer algorithm |
| `PlayerContent.qml` | Complete player UI layout |
| `MprisController.qml` | MPRIS player management |
| `DashboardMediaPage.qml` | CAVA subprocess integration |
| `LyricsService.qml` | Lyrics sync implementation |
| `Visualizer.qml` | Simple bar visualizer alternative |
| `raw_output_config.txt` | CAVA config for ASCII output |

---

## Next Steps for Lollypop C++

1. **MPRIS**: Implement `BackendController::mpris_proxy_` using `Gio::DBusProxy`
2. **CAVA**: Start with subprocess approach, parse ASCII output
3. **WaveVisualizer**: Port Canvas logic to `GtkDrawingArea` + Cairo
4. **Blur Effect**: Use `GskBlurNode` or Cairo box blur
5. **PiP Window**: Separate `GtkWindow` with `TYPE_HINT_DIALOG`
6. **Lyrics**: Port LRC parser + sync timer
7. **Theme**: Extract artwork colors → GTK CSS variables