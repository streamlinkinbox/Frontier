"""Retain real Vulkan vehicle/ground readbacks and verify their lossless shadow sequences."""
from Images import Root, Output, Read, Png
import hashlib
import json
import platform
import shutil
import subprocess
import struct
import zlib
Scratch = Root / '_ClaudeScratch/light-compute'
def Chunk(Name, Content):
    return struct.pack('>I', len(Content)) + Name + Content + struct.pack('>I', zlib.crc32(Name + Content))
def SequencePng(Frames):
    Width, Height = 640, 400
    Animation = b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', Width, Height, 8, 2, 0, 0, 0))
    Animation += Chunk(b'acTL', struct.pack('>II', len(Frames), 0))
    Sequence = 0
    for Index, Pixels in enumerate(Frames):
        Animation += Chunk(b'fcTL', struct.pack('>IIIIIHHBB', Sequence, Width, Height, 0, 0, 12, 10, 0, 0))
        Sequence += 1
        Payload = zlib.compress(b''.join(b'\0' + Pixels[Y*1920:(Y+1)*1920] for Y in range(Height)))
        Animation += Chunk(b'IDAT' if Index == 0 else b'fdAT', Payload if Index == 0 else struct.pack('>I', Sequence) + Payload)
        if Index:
            Sequence += 1
    Animation += Chunk(b'IEND', b'')
    Offset, Next, Index, Controls = 8, 0, 0, 0
    while Offset < len(Animation):
        Length = struct.unpack_from('>I', Animation, Offset)[0]
        Tag, Content = Animation[Offset+4:Offset+8], Animation[Offset+8:Offset+8+Length]
        assert zlib.crc32(Tag + Content) == struct.unpack_from('>I', Animation, Offset+8+Length)[0]
        if Tag in (b'fcTL', b'fdAT'):
            assert struct.unpack_from('>I', Content)[0] == Next
            Next += 1
        if Tag == b'fcTL':
            Controls += 1
        if Tag in (b'IDAT', b'fdAT'):
            Decoded = zlib.decompress(Content if Tag == b'IDAT' else Content[4:])
            assert b''.join(Decoded[Y*1921+1:(Y+1)*1921] for Y in range(Height)) == Frames[Index]
            Index += 1
        Offset += Length + 12
    assert Index == Controls == len(Frames)
    return Animation
Rows = {}
Log = (Output / 'Results/CloudShadows.txt').read_text()
for Line in Log.splitlines():
    if Line.startswith('CLOUD_SHADOW'):
        Category = Line.split()[0]
        Rows.setdefault(Category, []).append({K: float(V) for K,V in (P.split('=',1) for P in Line.split()[1:] if '=' in P)})
assert len(Rows['CLOUD_SHADOW']) == 5 and len(Rows['CLOUD_SHADOW_CAPTURE']) == 4
Digests = {}
for Subject in ('Vehicle', 'Ground'):
    Frames = [Read(Scratch / f'CloudShadow{Subject}{I}.ppm') for I in range(4)]
    assert len(set(Frames)) == 4, 'Weather did not change rendered surface lighting'
    for I, Pixels in enumerate(Frames):
        (Output / f'Images/CloudShadow{Subject}{I}.png').write_bytes(Png(640, 400, Pixels))
    (Output / f'Images/CloudShadow{Subject}.png').write_bytes(SequencePng(Frames))
    Digests[Subject] = [hashlib.sha256(Pixels).hexdigest() for Pixels in Frames]
Vehicle = Read(Scratch / 'CloudShadowVehicle0.ppm')
Ground = Read(Scratch / 'CloudShadowGround0.ppm')
Comparison = b''.join(Vehicle[Y*1920:(Y+1)*1920] + Ground[Y*1920:(Y+1)*1920] for Y in range(400))
(Output / 'Images/CloudShadowComparison.png').write_bytes(Png(1280, 400, Comparison))
Report = {'checks': Rows, 'device': Log.splitlines()[0], 'physical_gpu_executed': False,
          'platform': platform.platform(), 'compiler': subprocess.check_output(['c++', '--version'], text=True).splitlines()[0] if shutil.which('c++') else 'not detected by packager; see CMake compiler selection',
          'configuration': 'Release full captures; RelWithDebInfo ASan/UBSan numeric and cloud-volume checks',
          'shader_sha256': hashlib.sha256((Root / 'Frontier/Engine/Rendering/Shaders/LightInfluence.slang').read_bytes()).hexdigest(),
          'checkout_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=Root, text=True).strip(),
          'working_tree_changes': subprocess.check_output(['git', 'diff', '--name-only'], cwd=Root, text=True).splitlines(),
          'exact_sources': 'SHA256 source coverage in Results/Manifest.json',
          'windows_executed': False, 'extent': [640, 400], 'wind_seconds': [0, 60, 120, 180],
          'sun': 'fixed 35-degree elevation; native atmospheric attenuation at ground and cloud altitude',
          'sun_shadows': '64 midpoint samples of the cloud field at each receiver and secondary-hit position',
          'proposals': 'unattenuated light proposal mass; current-position attenuated target and contribution',
          'vehicle': 'same original ControlVehicle; analytic, IES/spot and emissive sources retained; 16 diffuse samples',
          'ground': 'same scene, 700m overhead camera below cloud base; exact direct plus visible emission, no indirect samples',
          'cloud_ambient': 'explicit constant RGB [0.1, 0.12, 0.15]; approximation, not multiple scattering',
          'environment': 'native CPU atmosphere, GPU cloud cube for indirect rays, per-camera-ray cloud sky',
          'limits': ['regular-step flat slab', 'observer-centred indirect cube', 'point Sun, no cloud-shadow penumbra',
                     'no lunar or new celestial integration', 'no physical GPU, Windows or real-time acceptance'],
          'animation': 'actual lossless Vulkan readbacks; illustrative timings, not real-time performance',
          'readback_rgb_sha256': Digests}
(Output / 'Results/CloudShadows.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS lossless moving surface-shadow and ControlVehicle evidence')
