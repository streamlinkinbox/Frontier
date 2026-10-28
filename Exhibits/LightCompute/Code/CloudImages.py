"""Package actual Vulkan cloud/sky readbacks and validate lossless weather-sequence payloads."""
from Images import Root, Output, Read, Png
import hashlib
import json
import struct
import zlib
Scratch = Root / '_ClaudeScratch/light-compute'
Cases = (0, 2, 3, 4)
for Prefix in ('CloudVehicle', 'CloudSky'):
    for Case in Cases:
        Pixels = Read(Scratch / f'{Prefix}{Case}.ppm')
        (Output / f'Images/{Prefix}{Case}.png').write_bytes(Png(640, 400, Pixels))
def Chunk(Name, Content):
    return struct.pack('>I', len(Content)) + Name + Content + struct.pack('>I', zlib.crc32(Name + Content))
for Prefix, Name in (('CloudVehicle', 'CloudWeather'), ('CloudSky', 'CloudSkyWeather')):
    Frames = [Read(Scratch / f'{Prefix}{I}.ppm') for I in Cases]
    Animation = b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', 640, 400, 8, 2, 0, 0, 0))
    Animation += Chunk(b'acTL', struct.pack('>II', len(Frames), 0))
    Sequence = 0
    for Index, Pixels in enumerate(Frames):
        Animation += Chunk(b'fcTL', struct.pack('>IIIIIHHBB', Sequence, 640, 400, 0, 0, 12, 10, 0, 0))
        Sequence += 1
        Payload = zlib.compress(b''.join(b'\0' + Pixels[Y*1920:(Y+1)*1920] for Y in range(400)))
        Animation += Chunk(b'IDAT' if Index == 0 else b'fdAT', Payload if Index == 0 else struct.pack('>I', Sequence) + Payload)
        if Index:
            Sequence += 1
    Animation += Chunk(b'IEND', b'')
    Offset, Next, Frame, Controls = 8, 0, 0, 0
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
            assert b''.join(Decoded[Y*1921+1:(Y+1)*1921] for Y in range(400)) == Frames[Frame]
            Frame += 1
        Offset += Length + 12
    assert Controls == Frame == len(Frames)
    (Output / f'Images/{Name}.png').write_bytes(Animation)
Before, After = Read(Scratch / 'CloudVehicle0.ppm'), Read(Scratch / 'CloudVehicle2.ppm')
Comparison = b''.join(Before[Y*1920:(Y+1)*1920] + After[Y*1920:(Y+1)*1920] for Y in range(400))
(Output / 'Images/CloudComparison.png').write_bytes(Png(1280, 400, Comparison))
Rows = {}
for Line in (Output / 'Results/Clouds.txt').read_text().splitlines():
    Category = Line.split()[0]
    if Category.startswith('CLOUD'):
        Rows.setdefault(Category, []).append({K: float(V) for K,V in (P.split('=',1) for P in Line.split()[1:] if '=' in P)})
assert len(Rows['CLOUD_CUBE']) == 5 and len(Rows['CLOUD_CAMERA']) == 4
Report = {'checks': Rows, 'case_order': ['clear 35-degree Sun', 'procedural clouds', 'same field advected 120 seconds', '3-degree low Sun'],
          'uniform_medium_case': 'numeric fixture only, not part of the animation',
          'camera_clouds': '64-step per-pixel GPU raymarch, six-step sun self-shadowing',
          'indirect_environment': 'six 48x48 GPU cloud-composited faces; native CPU single-scattering atmosphere input',
          'ambient': 'constant isotropic upper-hemisphere sky approximation, not multiple scattering',
          'direct_sun_shadow': '64-step transmittance at each shaded receiver and secondary hit; observer record is diagnostic only',
          'sun_positions': 'explicit fixture angles, not observer/time ephemeris',
          'ordinary_sun_flare_stars': 'existing rendering code preserved; not newly connected to these captures',
          'physical_gpu_executed': False, 'moon_executed': False, 'gpu_atmosphere_executed': False,
          'animation': 'actual lossless Vulkan readbacks, illustrative timing, not real-time performance',
          'vehicle_rgb_sha256': [hashlib.sha256(Read(Scratch / f'CloudVehicle{I}.ppm')).hexdigest() for I in Cases]}
(Output / 'Results/Clouds.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS clear/cloud/wind/low-Sun lossless Vulkan evidence and native cloud checks')
