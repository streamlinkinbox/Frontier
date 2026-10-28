#============================================================================================================================================
#                                                     PANETRANSMITTANCEIMAGES.PY
#============================================================================================================================================
# 🧩 Retain genuine Vulkan clerestory shaft readbacks and before/after thin-pane emitter comparisons with source hashes.

from pathlib import Path
import hashlib
import json
import sys

Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / '_ClaudeScratch/tmp/MaterialTools'))
from PIL import Image, ImageDraw

Capture = Root / '_ClaudeScratch/pane-transmittance'
Images = Root / 'Exhibits/LightCompute/Images'
Results = Root / 'Exhibits/LightCompute/Results'

AcceptedLog = (Results / 'PaneTransmittance.txt').read_text()
OriginalLog = (Results / 'PaneTransmittanceOriginal.txt').read_text()
assert 'PANE_TRANSMITTANCE_PASS ' in AcceptedLog, 'Refusing to publish images without full PANE_TRANSMITTANCE_PASS'
assert 'PANE_FOG_EMISSIVE_ORIGINAL ' in OriginalLog, 'Refusing to publish images without pinned negative control'

Panels = [
    ('PaneTransmittanceEmissiveOriginal', 'Pinned f5d99d4: emissive shaft leaks unattenuated'),
    ('PaneTransmittanceEmissiveAccepted', 'Accepted: emissive shaft + floor attenuated & tinted'),
    ('PaneTransmittanceAnalytic', 'Accepted: analytic fog shaft (open vs mullion vs pane)'),
    ('PaneTransmittanceScene', 'Accepted: analytic + emissive + fog + presets + solid glass'),
]

Hashes = {}
Loaded = {}
for Stem, _ in Panels:
    Source = Capture / (Stem + '.ppm')
    Picture = Image.open(Source).convert('RGB')
    assert Picture.size == (320, 200), f'Unexpected capture size for {Source}: {Picture.size}'
    Hashes[Source.name] = hashlib.sha256(Source.read_bytes()).hexdigest()
    Loaded[Stem] = Picture
    if Stem != 'PaneTransmittanceScene':
        Picture.save(Images / (Stem + '.png'))

# Standalone 2x nearest-neighbour view of the full combined clerestory studio hall.
Hero = Image.new('RGB', (640, 444), '#131922')
HeroDraw = ImageDraw.Draw(Hero)
HeroDraw.text((10, 8), 'Open bay (left) vs 5-pane laminated skylight (right): analytic + emissive fog shafts, presets & solid glass', fill='#eef2f8')
Hero.paste(Loaded['PaneTransmittanceScene'].resize((640, 400), Image.Resampling.NEAREST), (0, 26))
HeroDraw.text((10, 429), 'Actual Vulkan 320x200 readback (2x nearest pixels); SwiftShader CPU (type 4); not physical RTX/GTX.', fill='#b9c8db')
Hero.save(Images / 'PaneTransmittanceScene.png')

# 2x2 comparison contact sheet of exact 1:1 320x200 Vulkan readbacks.
Sheet = Image.new('RGB', (656, 476), '#131922')
Draw = ImageDraw.Draw(Sheet)
for Index, (Stem, Title) in enumerate(Panels):
    Col, Row = Index % 2, Index // 2
    X, Y = 8 + Col * 324, 24 + Row * 222
    Draw.text((X + 4, Y - 16), Title, fill='#eef2f8')
    Sheet.paste(Loaded[Stem], (X, Y))
Draw.text((12, 456), 'Actual 1:1 Vulkan 320x200 readbacks; SwiftShader CPU (type 4); left bay open, centre mullion, right bay 5-pane skylight.', fill='#b9c8db')
Sheet.save(Images / 'PaneTransmittanceComparison.png')

Report = {
    'device': 'SwiftShader Device (Subzero)',
    'device_type': 4,
    'software_vulkan': True,
    'physical_gpu': False,
    'capture_dimensions': [320, 200],
    'hero_display_dimensions': [640, 444],
    'comparison_dimensions': [656, 476],
    'pinned_negative_control_commit': 'f5d99d4',
    'capture_sha256': Hashes,
    'logs': {
        'accepted': AcceptedLog,
        'pinned_original': OriginalLog,
        'sanitized_native': (Results / 'PaneTransmittanceSanitized.txt').read_text(),
        'regression': (Results / 'PaneTransmittanceRegression.txt').read_text(),
    },
    'scope': (
        'Spectral thin-pane transmittance across compact area-emitter quadrature, eight-pane BSDF continuation, '
        'analytic and emissive volumetric fog shafts, and four reused multi-stratum material mixtures on software Vulkan. '
        'No physical RTX/GTX, Windows/editor or unified distance-field moving-scene claim.'
    ),
}
(Results / 'PaneTransmittance.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PANE_TRANSMITTANCE_IMAGES captures=4 source_dimensions=320x200 software_vulkan=1')
