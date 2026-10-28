#============================================================================================================================================
#                                                        PREPAREMATERIALSCENE.PY
#============================================================================================================================================
# 🧩 Compile existing TOML material presets and baked maps into an ignored, bounded scene-validation fixture.

from pathlib import Path
import hashlib
import json
import struct
import sys
import tomllib

Root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Root / '_ClaudeScratch/tmp/MaterialTools'))
from PIL import Image
Library = Root / 'EngineContent/MaterialArchives/TextureStudio'
Destination = Root / '_ClaudeScratch/build/light-compute'
Destination.mkdir(parents=True, exist_ok=True)
Names = ['racing-green', 'performance-rubber', 'crystal-glass', 'pp-polypropylene', 'indigo-denim']
Packed = []
Offsets = []
Sources = {}

def Spectrum(Sequence, Count=4):
    return list(Sequence) + [0.] * (Count - len(Sequence))

for Name in Names:
    Pathname = Library / (Name + '.material')
    Sources[str(Pathname.relative_to(Root))] = hashlib.sha256(Pathname.read_bytes()).hexdigest()
    Material = tomllib.loads(Pathname.read_text())
    Strata = Material['Strata']
    assert 1 <= len(Strata) <= 8
    Offset = len(Packed)
    Offsets.append(Offset)
    # Count, transport classification, albedo pyramid address, normal pyramid address; UV scale follows.
    Category = 2 if Name == 'crystal-glass' else 3 if Name == 'pp-polypropylene' else 0
    Uv = Strata[0].get('UvScaleOffset', [1, 1, 0, 0])
    Packed.extend([len(Strata), Category, 0, 0] + Uv)
    for Index in range(8):
        S = Strata[Index] if Index < len(Strata) else {}
        Packed.extend(Spectrum(S.get('Albedo', [.5, .5, .5]))[:3] + [S.get('Roughness', .4)])
        Packed.extend([S.get('Metallic', 0), S.get('Ior', 1.5), S.get('Coverage', 1), 0])
        Packed.extend(Spectrum(S.get('Absorption', [0, 0, 0]))[:3] + [S.get('Thickness', 0)])
        Packed.extend([int(S.get('Category', 'Opaque') == 'Coating'), 0, 0, 0])
        Packed.extend(Spectrum(S.get('SheenColor', [0, 0, 0]))[:3] + [S.get('SheenIntensity', 0)])
        Packed.extend([S.get('SheenRoughness', 0), 0, 0, 0])
    Paint = Material.get('AutomotivePaint', {})
    Palette = Paint.get('Palette', [[1, 1, 1, 0]])
    Packed.extend([int(Paint.get('Enabled', False)), Paint.get('Layers', 1), {'Palette': 0, 'Ramp': 1, 'Single': 2}.get(Paint.get('ColourMode'), 0), len(Palette)])
    Packed.extend(Paint.get('Distribution', [.64, 450, 1, .65]))
    Packed.extend(Paint.get('Finish', [.12, .3, .85, 1]))
    Packed.extend(Paint.get('Relief', [.2, .55, .18, 24]))
    for Index in range(12):
        Packed.extend(Palette[Index] if Index < len(Palette) else [0, 0, 0, 0])
    # Same 48-position palette expectation as CompileSurfaceMaterial; these presets do not use ramp mode.
    Packed.extend([sum(Palette[min(len(Palette) - 1, int((I + .5) / 48 * len(Palette)))][C] for I in range(48)) / 48 for C in range(3)] + [0])
    assert len(Packed) - Offset == 268
    for Channel, Header in [('Albedo', 2), ('Normal', 3)]:
        Texture = Strata[0].get('Textures', {}).get(Channel)
        if not Texture:
            continue
        Pathname = Library / Texture
        Sources[str(Pathname.relative_to(Root))] = hashlib.sha256(Pathname.read_bytes()).hexdigest()
        Packed[Offset + Header] = len(Packed)
        # Preserve committed source pixels; build linear-colour / renormalized-normal mip pyramids.
        Picture = Image.open(Pathname).convert('RGB')
        Width, Height = Picture.size
        assert Width == Height and Width <= 1024 and Width & (Width - 1) == 0
        Texels = []
        for Pixel in Picture.getdata():
            if Channel == 'Albedo':
                Texels.extend([C / 255 / 12.92 if C / 255 <= .04045 else ((C / 255 + .055) / 1.055) ** 2.4 for C in Pixel])
            else:
                Texels.extend([C / 127.5 - 1 for C in Pixel])
        Packed.extend([min(Width, 256), 0, 0, 0])
        while True:
            if Width <= 256:
                Packed.extend(Texels)
            if Width == 1:
                break
            Reduced = []
            for Y in range(Width // 2):
                for X in range(Width // 2):
                    Colour = [sum(Texels[((2 * Y + Dy) * Width + 2 * X + Dx) * 3 + C] for Dy in range(2) for Dx in range(2)) / 4 for C in range(3)]
                    if Channel == 'Normal':
                        Length = max(1e-9, sum(C * C for C in Colour) ** .5)
                        Colour = [C / Length for C in Colour]
                    Reduced.extend(Colour)
            Width //= 2
            Texels = Reduced
assert len(Packed) < 2097152
(Destination / 'MaterialSceneMaps.bin').write_bytes(struct.pack('<' + 'f' * len(Packed), *Packed))
(Destination / 'MaterialSceneOffsets.h').write_text('constexpr uint32_t MaterialSceneOffsets[] = {' + ','.join(str(I) for I in Offsets) + '};\n')
Report = {'preset_names': Names, 'source_sha256': Sources, 'float_count': len(Packed), 'offsets': Offsets,
          'glass': 'explicit thin-sheet transport override; original Crystal Glass preset is opaque',
          'maps': 'committed baked PNGs with generated linear-colour/normal mip pyramids; no runtime recipe rebake claim'}
(Destination / 'MaterialSceneInputs.json').write_text(json.dumps(Report, indent=2) + '\n')
print('Prepared material scene:', len(Packed) * 4, 'bytes of shared material/map floats')
