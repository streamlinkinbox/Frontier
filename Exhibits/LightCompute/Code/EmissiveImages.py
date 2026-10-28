"""Lossless actual Vulkan emissive-area readbacks and finite-reference error evidence."""
from Images import Root, Output, Read, Png
import hashlib
import json
import struct
import zlib
Scratch = Root / '_ClaudeScratch/light-compute'
for Name in ('EmissiveArea', 'EmissiveHemisphere'):
    (Output / f'Images/{Name}.png').write_bytes(Png(640, 400, Read(Scratch / f'{Name}.ppm')))
Before, After = Read(Scratch / 'EmissiveHemisphere.ppm'), Read(Scratch / 'EmissiveArea.ppm')
Comparison = b''.join(Before[Y*1920:(Y+1)*1920] + After[Y*1920:(Y+1)*1920] for Y in range(400))
(Output / 'Images/EmissiveComparison.png').write_bytes(Png(1280, 400, Comparison))
def Chunk(Name, Content):
    return struct.pack('>I', len(Content)) + Name + Content + struct.pack('>I', zlib.crc32(Name + Content))
Frames = [Read(Scratch / f'EmissiveMotion{I}.ppm') for I in range(6)]
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
(Output / 'Images/EmissiveMotion.png').write_bytes(Animation)
Rows = {}
for Name in ('EmissiveArea', 'EmissiveMotion'):
    for Line in (Output / f'Results/{Name}.txt').read_text().splitlines():
        Category = Line.split()[0]
        if Category.startswith('EMISSIVE'):
            Rows.setdefault(Category, []).append({K: float(V) for K, V in (P.split('=', 1) for P in Line.split()[1:] if '=' in P)})
assert len(Rows['EMISSIVE_MOTION']) == 6
assert all(R['current_indirect_equal'] == 1 for R in Rows['EMISSIVE_MOTION'])
Metrics = Rows['EMISSIVE_AREA'][0]
Report = {'checks': Rows, 'probe_rmse_reduction_percent': 100*(1-Metrics['area_rmse']/Metrics['hemisphere_rmse']),
          'reference': '225 distributed surface probes; independent 8192-sample native emissive-area integration, seed 8171',
          'comparison': 'seed 71, 16 fixed hemisphere samples versus 16 explicit area samples plus the retained hemisphere routes',
          'equal_cost_comparison': False, 'universal_unbiasedness_claim': False,
          'physical_gpu_executed': False, 'indirect_filtering_executed': False,
          'cdf': 'area times luminance, plus one of 2^24 probability ticks reserved per positive-area emitter',
          'animation': 'six actual lossless readbacks, illustrative timing, not frame-rate proof',
          'motion_rgb_sha256': [hashlib.sha256(F).hexdigest() for F in Frames]}
(Output / 'Results/Emissive.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS emissive-area images and six lossless motion frames; probe RMSE %.2f%% lower' % Report['probe_rmse_reduction_percent'])
