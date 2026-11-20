# Lollypop C++

A complete overhaul of Lollypop music player written in C++ with a custom design.

## Project Structure

```
lollypop-cpp/
├── src/           # Source files
├── include/       # Header files
├── resources/     # UI resources, icons, themes
├── build/         # Build output (gitignored)
└── tests/         # Unit tests
```

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Design Goals

- Modern C++ (C++20/23)
- Clean architecture with separation of concerns
- Custom UI design (not GTK/Qt based)
- High performance audio playback
- Cross-platform support (Linux first)