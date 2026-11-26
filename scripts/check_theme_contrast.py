#!/usr/bin/env python3
"""Verify theme contrast, token coverage and colour hygiene.

Implements docs/THEME_AND_COLOR_PLAN.md §11 acceptance metrics:

  raw_hex_outside_token_definitions: 0
  raw_rgba_outside_token_definitions: 0
  contrast_failures_body_text: 0
  hardcoded_accent_in_canvas_widgets: 0
  accent_tokens_emitted: 9
  schemes_verified: 2
  undefined_css_variables: 0

Run from CTest or directly: python3 scripts/check_theme_contrast.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

STYLE = ROOT / "resources/style.css"
APPLE = ROOT / "resources/apple_ui.css"
TOKENS = ROOT / "include/ui/theme/design_tokens.hpp"
ACCENTS = ROOT / "frontend/src/ui/theme/AccentColorManager.cpp"
CANVAS_FILES = [
    ROOT / "frontend/src/panels/waveform_widget.cpp",
    ROOT / "frontend/src/panels/waveform_view.cpp",
    ROOT / "frontend/src/panels/progress_wave.cpp",
]

ACCENT_TOKENS = [
    "--accent-color",
    "--accent-hover",
    "--accent-active",
    "--accent-soft",
    "--accent-softer",
    "--accent-border",
    "--accent-shadow",
    "--accent-shadow-strong",
    "--accent-on",
]

failures: list[str] = []


def fail(message: str) -> None:
    failures.append(message)


# ---------------------------------------------------------------------------
# Colour parsing / contrast
# ---------------------------------------------------------------------------


def parse_hex(value: str) -> tuple[float, float, float]:
    value = value.strip().lstrip("#")
    if len(value) == 3:
        value = "".join(ch * 2 for ch in value)
    return tuple(int(value[i : i + 2], 16) / 255.0 for i in (0, 2, 4))


def parse_rgba(value: str) -> tuple[float, float, float, float]:
    match = re.match(r"rgba?\(([^)]*)\)", value.strip())
    if not match:
        return 0.0, 0.0, 0.0, 1.0
    parts = [p.strip() for p in match.group(1).split(",")]
    rgb = [float(p) / 255.0 for p in parts[:3]]
    alpha = float(parts[3]) if len(parts) > 3 else 1.0
    return rgb[0], rgb[1], rgb[2], alpha


def luminance(rgb: tuple[float, float, float]) -> float:
    def channel(c: float) -> float:
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4

    return (
        0.2126 * channel(rgb[0])
        + 0.7152 * channel(rgb[1])
        + 0.0722 * channel(rgb[2])
    )


def contrast(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def composite(
    fg: tuple[float, float, float, float], bg: tuple[float, float, float]
) -> tuple[float, float, float]:
    r, g, b, a = fg
    return (r * a + bg[0] * (1 - a), g * a + bg[1] * (1 - a), b * a + bg[2] * (1 - a))


# ---------------------------------------------------------------------------
# Parse the C++ token tables (the runtime source of truth)
# ---------------------------------------------------------------------------


def cpp_scheme_colors(text: str, array_name: str) -> dict[str, str]:
    block = re.search(
        rf"const NamedColor {array_name}\[\] = \{{(.*?)\}};", text, re.S
    )
    if not block:
        fail(f"design_tokens.hpp: missing {array_name}")
        return {}
    return dict(
        re.findall(
            r'\{"(--[\w-]+)",\s*rgb_from_hex\("(#[0-9A-Fa-f]{6})"\)\}',
            block.group(1),
        )
    )


tokens_text = TOKENS.read_text()
dark_colors = cpp_scheme_colors(tokens_text, "dark_colors")
light_colors = cpp_scheme_colors(tokens_text, "light_colors")

for scheme, table in (("dark", dark_colors), ("light", light_colors)):
    if len(table) < 15:
        fail(f"{scheme}: expected at least 15 semantic colours, found {len(table)}")

for name in ("--text-primary", "--text-secondary", "--text-tertiary"):
    if name not in dark_colors or name not in light_colors:
        fail(f"missing {name} in one scheme")

# Body text contrast budgets from §4.2.
for scheme, table in (("dark", dark_colors), ("light", light_colors)):
    surfaces = [
        name
        for name in (
            "--surface-app",
            "--surface-sidebar",
            "--surface-panel",
            "--surface-elevated",
            "--surface-raised",
        )
        if name in table
    ]
    for text_name, minimum in (
        ("--text-primary", 12.0),
        ("--text-secondary", 4.5),
        ("--text-tertiary", 4.5),
    ):
        if text_name not in table:
            continue
        fg = parse_hex(table[text_name])
        for surface_name in surfaces:
            ratio = contrast(fg, parse_hex(table[surface_name]))
            if ratio + 1e-6 < minimum:
                fail(
                    f"{scheme}: {text_name} on {surface_name} is "
                    f"{ratio:.2f}:1, below {minimum:.1f}:1"
                )

# ---------------------------------------------------------------------------
# Accent presets: on-accent ink must clear AA in both schemes
# ---------------------------------------------------------------------------

accent_text = ACCENTS.read_text()
presets = re.findall(
    r'\{"([^"]+)",\s*"(#[0-9A-Fa-f]{6})",\s*"(#[0-9A-Fa-f]{6})"\}',
    accent_text,
)
if len(presets) != 9:
    fail(f"expected 9 accent presets, found {len(presets)}")
for name, dark_hex, light_hex in presets:
    for scheme, seed_hex in (("dark", dark_hex), ("light", light_hex)):
        seed = parse_hex(seed_hex)
        best = max(contrast(seed, parse_hex("#0B0B10")),
                   contrast(seed, parse_hex("#FFFFFF")))
        if best + 1e-6 < 4.5:
            fail(
                f"accent {name}/{scheme} ({seed_hex}) cannot reach 4.5:1 "
                f"with black or white ink (best {best:.2f}:1)"
            )
    # Accent text/icons sit on the scheme floor, not only inside a fill.
    dark_floor = contrast(parse_hex(dark_hex), parse_hex("#0B0B10"))
    light_floor = contrast(parse_hex(light_hex), parse_hex("#FFFFFF"))
    if dark_floor + 1e-6 < 4.5:
        fail(f"accent {name}/dark on surface-app is {dark_floor:.2f}:1")
    if light_floor + 1e-6 < 4.5:
        fail(f"accent {name}/light on white is {light_floor:.2f}:1")

# Every accent token must be emitted by build_css().
for token in ACCENT_TOKENS:
    if f'"{token}"' not in tokens_text and token not in tokens_text:
        fail(f"design_tokens.hpp does not emit {token}")

# ---------------------------------------------------------------------------
# Stylesheet hygiene
# ---------------------------------------------------------------------------


def root_span(text: str) -> tuple[int, int]:
    match = re.search(r":root\s*\{", text)
    if not match:
        return 0, 0
    depth = 0
    index = match.end()
    while index < len(text):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return match.start(), index + 1
        index += 1
    return match.start(), len(text)


def non_token_lines(text: str):
    start, end = root_span(text)
    body = text[:start] + text[end:]
    for number, line in enumerate(body.splitlines(), start=1):
        if line.lstrip().startswith("--"):
            continue
        yield number, line


for label, path in (("style.css", STYLE), ("apple_ui.css", APPLE)):
    text = path.read_text()
    for number, line in non_token_lines(text):
        for match in re.finditer(r"#[0-9A-Fa-f]{3,8}\b", line):
            fail(f"{label}:{number}: raw hex {match.group()}")
        if re.search(r"rgba?\(", line):
            fail(f"{label}:{number}: raw rgb/rgba outside token definitions")
    # Undefined custom properties.
    defined = set(re.findall(r"(--[\w-]+)\s*:", STYLE.read_text()))
    defined.update(set(re.findall(r"(--[\w-]+)\s*:", APPLE.read_text())))
    defined.update(
        re.findall(r'"(--[\w-]+)"', tokens_text)
    )
    defined.update(
        f"--overlay-{n}" for n in (2, 4, 6, 8, 10, 12, 16, 72, 82, 92)
    )
    defined.update(
        f"--shadow-{n}" for n in (6, 8, 10, 16, 20, 24, 28, 38, 40, 50)
    )
    defined.update(f"--{status}-soft" for status in ("success", "warning", "error", "info"))
    for name in ACCENT_TOKENS:
        defined.add(name)
    for number, line in enumerate(text.splitlines(), start=1):
        for name in re.findall(r"var\((--[\w-]+)", line):
            if name not in defined:
                fail(f"{label}:{number}: undefined variable {name}")

# Canvas widgets must not hardcode the old cyan/lavender accent.
for path in CANVAS_FILES:
    source = path.read_text()
    for pattern in ("0.2, 0.7, 1.0", "0.58, 0.54, 0.70"):
        if pattern in source:
            fail(f"{path.name}: hardcoded accent tuple {pattern}")

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

if failures:
    print("Theme contrast check failed:")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print(
    f"Theme contrast OK: {len(presets)} accents, 2 schemes, "
    f"{len(dark_colors)} dark / {len(light_colors)} light semantic colours, "
    "0 raw colours outside token definitions"
)
