#!/usr/bin/env bash
# Verify that stylesheet tokens, runtime tokens, focus styling, and
# accessibility naming stay in sync. Safe to run headlessly from CTest.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 - "$repo_root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
style_path = root / "resources/style.css"
apple_path = root / "resources/apple_ui.css"
tokens_path = root / "include/ui/theme/design_tokens.hpp"

style = style_path.read_text()
apple = apple_path.read_text()
tokens = tokens_path.read_text()
failures = []


def fail(message):
    failures.append(message)


def cpp_string(name):
    pattern = (r'inline constexpr const char\*\s*' + re.escape(name) +
               r'\s*=\s*"((?:\\.|[^"\\])*)"')
    match = re.search(pattern, tokens)
    if not match:
        fail(f"design_tokens.hpp: missing {name}")
        return None
    return match.group(1).replace('\\"', '"')


def root_declarations(text):
    declarations = {}
    for block in re.findall(r':root\s*\{[^{}]*\}', text, flags=re.S):
        for line in block.splitlines():
            match = re.match(r'\s*(--[\w-]+)\s*:\s*(.*?)\s*;', line)
            if match:
                declarations[match.group(1)] = match.group(2)
    return declarations


def non_root_lines(text):
    in_root = False
    depth = 0
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if not in_root and re.match(r'^:root\s*\{', stripped):
            in_root = True
            depth = line.count('{') - line.count('}')
            continue
        if in_root:
            depth += line.count('{') - line.count('}')
            if depth <= 0:
                in_root = False
            continue
        yield number, line


style_decls = root_declarations(style)

font_tokens = re.findall(
    r'\{\s*"[^"]+",\s*"(--font-[\w-]+)",\s*([0-9.]+)\}', tokens)
motion_tokens = re.findall(
    r'\{\s*"[^"]+",\s*"(--motion-[\w-]+)",\s*(\d+)\}', tokens)

if len(font_tokens) != 15:
    fail(f"expected 15 canonical font tokens, found {len(font_tokens)}")
if len(motion_tokens) != 3:
    fail(f"expected 3 canonical motion tokens, found {len(motion_tokens)}")

for name, px in font_tokens:
    expected = f'calc({float(px):g}px * var(--font-scale))'
    if style_decls.get(name) != expected:
        fail(f"style.css:root {name} expected {expected}, got "
             f"{style_decls.get(name)!r}")
    occurrences = len(re.findall(r'^\s*' + re.escape(name) + r'\s*:',
                                 style, flags=re.M))
    if occurrences != 1:
        fail(f"style.css: {name} declared {occurrences} times")

for name, ms in motion_tokens:
    if style_decls.get(name) != f'{int(ms)}ms':
        fail(f"style.css:root {name} expected {int(ms)}ms, got "
             f"{style_decls.get(name)!r}")

static_checks = {
    cpp_string('kFontScaleName'): '1',
    cpp_string('kFontFamilyName'): cpp_string('kFontFamily'),
    cpp_string('kEaseOutName'): cpp_string('kEaseOut'),
    cpp_string('kBorderFocusName'): 'var(--accent-color)',
    cpp_string('kTextOnAccentName'): cpp_string('kTextOnAccent'),
}
for name, expected in static_checks.items():
    if not name:
        continue
    if style_decls.get(name) != expected:
        fail(f"style.css:root {name} expected {expected!r}, got "
             f"{style_decls.get(name)!r}")

for label, text in (('style.css', style), ('apple_ui.css', apple)):
    for number, line in non_root_lines(text):
        if re.search(r'font-size\s*:\s*(?:\d|calc\()', line):
            fail(f"{label}:{number}: literal font-size; use a --font-* token")
        for match in re.finditer(r'(?<![\w-])(\d+)ms\b', line):
            if int(match.group(1)) not in (0, 1):
                fail(f"{label}:{number}: hardcoded motion duration; use a "
                     "--motion-* token")
        for match in re.finditer(r'#[0-9A-Fa-f]{3,8}\b', line):
            fail(f"{label}:{number}: raw hex {match.group()}; use a color token")

for label, text in (('style.css', style), ('apple_ui.css', apple)):
    blocks = re.findall(r'([^{}]+)\{([^{}]*)\}', text, flags=re.S)
    focus_blocks = [body for selector, body in blocks
                    if ':focus-visible' in selector]
    if focus_blocks and not any('--border-focus' in body
                                for body in focus_blocks):
        fail(f"{label}: focus-visible styling does not use --border-focus")

required_sources = {
    'frontend/include/util/accessibility.hpp': 'set_label',
    'frontend/src/ui/components/AlbumCard.cpp': 'accessibility::set_label',
    'frontend/src/ui/components/AppleAlbumTile.cpp': 'accessibility::set_label',
    'frontend/src/ui/components/TrackRow.cpp': 'accessibility::set_label',
    'frontend/src/panels/pip_window.cpp': 'accessibility::set_label',
    'frontend/src/panels/right_now_playing.cpp': 'accessibility::set_label',
}
for relative_path, needle in required_sources.items():
    source = root / relative_path
    if not source.exists() or needle not in source.read_text():
        fail(f"{relative_path}: missing {needle}")

if failures:
    print("Design consistency check failed:")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print(f"Design consistency OK: {len(font_tokens)} font tokens, "
      f"{len(motion_tokens)} motion tokens, no raw hex or literal typography "
      "outside token blocks")
PY
