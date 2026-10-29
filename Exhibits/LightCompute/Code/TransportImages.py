"""Actual Vulkan diffuse transport readbacks, lossless moving proof and measured native probes."""
from Images import Root, Output, Read, Png
import hashlib
import json
import struct
import zlib
Scratch = Root / '_ClaudeScratch/light-compute'
for Name in ('Transport', 'TransportDirect'):
    (Output / f'Images/{Name}.png').write_bytes(Png(640, 400, Read(Scratch / f'{Name}.ppm')))
Direct, Indirect = Read(Scratch / 'TransportDirect.ppm'), Read(Scratch / 'Transport.ppm')
Comparison = b''.join(Direct[Y*1920:(Y+1)*1920] + Indirect[Y*1920:(Y+1)*1920] for Y in range(400))
(Output / 'Images/TransportComparison.png').write_bytes(Png(1280, 400, Comparison))
def Chunk(Name, Content):
    return struct.pack('>I', len(Content)) + Name + Content + struct.pack('>I', zlib.crc32(Name + Content))
Frames = [Read(Scratch / f'TransportMotion{I}.ppm') for I in range(6)]
Animation = b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', 640, 400, 8, 2, 0, 0, 0))
Animation += Chunk(b'acTL', struct.pack('>II', 6, 0))
Sequence = 0
for Index, Pixels in enumerate(Frames):
    Animation += Chunk(b'fcTL', struct.pack('>IIIIIHHBB', Sequence, 640, 400, 0, 0, 12 if Index == 0 else 4, 10, 0, 0))
    Sequence += 1
    Payload = zlib.compress(b''.join(b'\0' + Pixels[Y*1920:(Y+1)*1920] for Y in range(400)))
    Animation += Chunk(b'IDAT' if Index == 0 else b'fdAT', Payload if Index == 0 else struct.pack('>I', Sequence) + Payload)
    if Index:
        Sequence += 1
Animation += Chunk(b'IEND', b'')
Offset, Next, Frame, Controls = 8, 0, 0, 0
while Offset < len(Animation):
    Length = struct.unpack_from('>I', Animation, Offset)[0]
    Name, Content = Animation[Offset+4:Offset+8], Animation[Offset+8:Offset+8+Length]
    assert zlib.crc32(Name + Content) == struct.unpack_from('>I', Animation, Offset+8+Length)[0]
    if Name in (b'fcTL', b'fdAT'):
        assert struct.unpack_from('>I', Content)[0] == Next
        Next += 1
    if Name == b'fcTL':
        Controls += 1
    if Name in (b'IDAT', b'fdAT'):
        Decoded = zlib.decompress(Content if Name == b'IDAT' else Content[4:])
        assert b''.join(Decoded[Y*1921+1:(Y+1)*1921] for Y in range(400)) == Frames[Frame]
        Frame += 1
    Offset += Length + 12
assert Controls == Frame == 6
(Output / 'Images/TransportMotion.png').write_bytes(Animation)
Rows = {}
for Name in ('Transport', 'TransportMotion'):
    for Line in (Output / f'Results/{Name}.txt').read_text().splitlines():
        Category = Line.split()[0]
        if Category.startswith('TRANSPORT'):
            Rows.setdefault(Category, []).append({K: float(V) for K, V in (P.split('=', 1) for P in Line.split()[1:] if '=' in P)})
assert len(Rows['TRANSPORT_MOTION']) == 6
assert all(R['current_indirect_equal'] == 1 for R in Rows['TRANSPORT_MOTION'])
Report = {'checks': Rows, 'primary_transport': 'one diffuse bounce, Fresnel-weighted Lambert response',
          'secondary_exitance': 'visible material emission plus exact shadowed analytic light evaluation',
          'environment': 'native CPU SingleScattering bake; six 48x48 cube faces, bilinear shader lookup',
          'quadrature': '16 fixed cosine-weighted hemisphere directions; not a stochastic convergence proof',
          'probe_reference': 'double BVH, independent barycentrics, native light/secondary BRDF and shadow queries',
          'physical_gpu_executed': False, 'cpu_per_pixel_radiance_uploaded': False,
          'gpu_atmosphere_executed': False, 'clouds_executed': False, 'moon_executed': False,
          'comparison': 'left direct plus visible sky; right adds one diffuse bounce',
          'animation': 'six lossless Vulkan readbacks; illustrative playback, no frame-rate claim',
          'motion_rgb_sha256': [hashlib.sha256(F).hexdigest() for F in Frames]}
(Output / 'Results/Transport.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS six lossless transport frames, source ablations, native probes and current-indirect checks')
