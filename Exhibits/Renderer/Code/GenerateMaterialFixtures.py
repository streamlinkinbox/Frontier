#============================================================================================================================================
#                                                     GENERATEMATERIALFIXTURES.PY
#============================================================================================================================================
# 🧩 Generate small deterministic texture specimens for actual native material compilation; no downloaded art or project presets.

from pathlib import Path
import math
import struct
import zlib

Root = Path(__file__).resolve().parents[1] / 'Materials'
(Root / 'Textures').mkdir(parents=True, exist_ok=True)


def Normal(Column, Row):
    Horizontal = .32 * math.sin(Column * math.pi / 8)
    Vertical = .32 * math.cos(Row * math.pi / 8)
    Forward = math.sqrt(1 - Horizontal ** 2 - Vertical ** 2)
    return [round((Coefficient * .5 + .5) * 255) for Coefficient in (Horizontal, Vertical, Forward)]


def Encode(Name, Evaluate):
    Rows = b''.join(b'\0' + b''.join(bytes(Evaluate(Column, Row)) for Column in range(128)) for Row in range(128))
    def Chunk(Category, Payload):
        return struct.pack('>I', len(Payload)) + Category + Payload + struct.pack('>I', zlib.crc32(Category + Payload))
    (Root / 'Textures' / (Name + '.png')).write_bytes(b'\x89PNG\r\n\x1a\n' +
        Chunk(b'IHDR', struct.pack('>IIBBBBB', 128, 128, 8, 2, 0, 0, 0)) +
        Chunk(b'IDAT', zlib.compress(Rows, 9)) + Chunk(b'IEND', b''))


Encode('Albedo', lambda Column, Row: (205, 155, 92) if (Column // 16 + Row // 16) % 2 else (55, 95, 125))
Encode('Orm', lambda Column, Row: (255, 90 if Column % 32 < 16 else 220, 255))
Encode('Normal', Normal)
Encode('CoatNormal', lambda Column, Row: (round(128+20*math.sin(Row*math.pi/32)), 128, 254))
Encode('CoatOrm', lambda Column, Row: (255, 160 if Row % 32 < 16 else 245, 0))
Encode('Radiance', lambda Column, Row: (255, 100, 30) if Row % 32 < 3 else (0, 0, 0))

Definitions = {
    'BrushedCopper': ('Brushed copper', '[0.95, 0.48, 0.22]', '0.35', '1.0', '0', ''),
    'CoatedCeramic': ('Coated ceramic', '[0.12, 0.42, 0.65]', '0.5', '0.0', '5', '''
[[Strata]]
Category = "Coating"
Roughness = 0.12
Ior = 1.5
Coverage = 1.0
Absorption = [2.0, 0.3, 0.1]
Thickness = 0.02
TextureMask = 6
UvScaleOffset = [0.5, 2.0, 0.13, 0.0]
NormalStrength = 0.5
Anisotropy = 0.55
Rotation = 0.4
Fresnel = "Dielectric"

[Strata.Textures]
Normal = "Textures/CoatNormal.png"
Orm = "Textures/CoatOrm.png"
'''),
    'TexturedAlloy': ('Textured alloy', '[1.0, 1.0, 1.0]', '0.8', '0.8', '7', ''),
    'LuminousPattern': ('Luminous pattern', '[0.08, 0.08, 0.08]', '0.7', '0.0', '8', '')
}
for Filename, (Name, Albedo, Roughness, Metallic, Mask, Coating) in Definitions.items():
    Radiance = '[3.0, 1.0, 0.3]' if Filename == 'LuminousPattern' else '[0.0, 0.0, 0.0]'
    Directional = 'Anisotropy = 0.8\nRotation = 0.35\n' if Filename == 'BrushedCopper' else ''
    (Root / (Filename + '.material')).write_text(f'''Format = 1
Name = "{Name}"
UvScaleOffset = [3.0, 2.0, 0.0, 0.0]
NormalStrength = 1.0
Radiance = {Radiance}

[Textures]
Albedo = "Textures/Albedo.png"
Orm = "Textures/Orm.png"
Normal = "Textures/Normal.png"
Radiance = "Textures/Radiance.png"

[[Strata]]
Category = "Opaque"
Albedo = {Albedo}
Roughness = {Roughness}
Metallic = {Metallic}
Ior = 1.5
Coverage = 1.0
TextureMask = {Mask}
{Directional}{Coating}''')
print('Generated four authored TOML materials and six 128px texture specimens')
