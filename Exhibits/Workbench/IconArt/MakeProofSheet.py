#!/usr/bin/env python3
"""Optional PNG evidence from the C++ proof's output (requires Pillow). Not runtime assets."""
from pathlib import Path
import re
import sys
import textwrap
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/icon-release/evidence'
OUTPUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / 'Docs/IconEvidence/ThorVGAllIcons.png'
items = re.findall(r'FRONTIER_ICON\(([^,]+),\s*"([^"]+)"', (ROOT / 'Engine/DisplayPresentation/IconSymbols.inc').read_text())
im = Image.new('RGB', (1344, 60 + ((len(items) + 11) // 12) * 118), '#191b20')
draw = ImageDraw.Draw(im)
font = ImageFont.truetype(str(ROOT / 'EngineContent/FontArchives/Archivo/Archivo-Regular.ttf'), 11)
title = ImageFont.truetype(str(ROOT / 'EngineContent/FontArchives/Archivo/Archivo-SemiBold.ttf'), 21)
draw.text((18, 10), f'{len(items)} native ThorVG icons — zero substitutes', font=title, fill='#f4f4f6')
draw.text((18, 37), 'C++ software renderer output, reduced from 128 px for this proof sheet. This PNG is evidence only, not a runtime icon asset.', font=font, fill='#afb5c3')
# Refuse to label a failing run as zero substitutes.
rows = (SOURCE / 'IconResults.tsv').read_text().splitlines()[1:]
assert len(rows) == len(items) and all(row.split('\t')[2:4] == ['ready', '0'] for row in rows)
for i, (symbol, filename) in enumerate(items):
    x, y = (i % 12) * 112, 60 + (i // 12) * 118
    icon = Image.open(SOURCE / 'Raster' / (symbol + '.png')).convert('RGBA').resize((76, 76), Image.Resampling.LANCZOS)
    im.paste(icon, (x + 18, y + 2), icon)
    for line, n in zip(textwrap.wrap(filename.removesuffix('.svg'), 19), range(2)):
        draw.text((x + 56, y + 83 + n * 13), line, font=font, fill='#bbc0cb', anchor='mt')
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
im.save(OUTPUT)
