#!/usr/bin/env bash
# Headless RTL smoke check. Catches unmirrored physical edges and confirms the
# surfaces in the manual RTL smoke pass have explicit :dir(rtl) coverage.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 - "$repo_root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
stylesheets = (
    ('style.css', root / 'resources/style.css'),
    ('apple_ui.css', root / 'resources/apple_ui.css'),
)
texts = {label: path.read_text() for label, path in stylesheets}
combined = '\n'.join(texts.values())
failures = []

required_rtl_selectors = (
    '.sidebar:dir(rtl)',
    '.glass-sidebar:dir(rtl)',
    '.right-now-playing:dir(rtl)',
    '.apple-sidebar:dir(rtl)',
    '.apple-album-meta:dir(rtl)',
    '.track-row:dir(rtl)',
    '.pip-info-box:dir(rtl)',
    '.now-playing-progress-row:dir(rtl)',
    '.pip-progress-row:dir(rtl)',
    '.home-subtitle:dir(rtl)',
    '.bottom-playback-modes:dir(rtl)',
)
for selector in required_rtl_selectors:
    if selector not in combined:
        failures.append(f'missing RTL selector: {selector}')

required_surfaces = (
    '.apple-album-tile',
    '.album-card-content',
    '.track-row',
    '.pip-title',
    '.now-playing-title',
    '.now-playing-progress-row',
    '.pip-progress-row',
)
for selector in required_surfaces:
    if selector not in combined:
        failures.append(f'missing smoke surface selector: {selector}')

physical_property = re.compile(
    r'^\s*(border-(?:left|right)(?:-color)?|'
    r'margin-(?:left|right)|padding-(?:left|right))\s*:',
    flags=re.M,
)
directional_align = re.compile(r'^\s*text-align\s*:\s*(left|right)\s*;',
                               flags=re.M)

for label, text in texts.items():
    for match in re.finditer(r'([^{}]+)\{([^{}]*)\}', text, flags=re.S):
        selector, body = match.group(1).strip(), match.group(2)
        if ':dir(ltr)' in selector or ':dir(rtl)' in selector:
            continue
        for property_match in physical_property.finditer(body):
            failures.append(
                f'{label}: {property_match.group(1).strip()} in "{selector}" '
                'is not scoped to :dir(ltr)/:dir(rtl)')
        align_match = directional_align.search(body)
        if align_match:
            failures.append(
                f'{label}: text-align: {align_match.group(1)} in "{selector}" '
                'is not scoped to :dir(ltr)/:dir(rtl)')

if failures:
    print('RTL smoke check failed:')
    for failure in failures:
        print(f'  - {failure}')
    raise SystemExit(1)

print(f'RTL smoke OK: {len(required_rtl_selectors)} mirrored selectors, '
      'no unscoped physical edges')
PY
