"""Losslessly package actual moving Vulkan-primary readbacks and their measured errors."""
from Images import Root, Output, Read, Png
import hashlib
import json
import struct
import zlib
Scratch = Root / '_ClaudeScratch/light-compute'
Frames = [Read(Scratch / f'PrimaryMotion{Index}.ppm') for Index in range(6)]
def Chunk(Name, Content):
    return struct.pack('>I', len(Content)) + Name + Content + struct.pack('>I', zlib.crc32(Name + Content))
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
assert Frame == Controls == 6
(Output / 'Images/PrimaryMotion.png').write_bytes(Animation)
for Name in ('Exact1', 'Fresh1', 'Reject1'):
    (Output / f'Images/PrimaryMotion{Name}.png').write_bytes(Png(640, 400, Read(Scratch / f'PrimaryMotion{Name}.ppm')))
Fresh = Read(Scratch / 'PrimaryMotionFresh1.ppm')
Comparison = b''.join(Fresh[Y*1920:(Y+1)*1920] + Frames[1][Y*1920:(Y+1)*1920] for Y in range(400))
(Output / 'Images/PrimaryMotionComparison.png').write_bytes(Png(1280, 400, Comparison))
Rows = {}
for Line in (Output / 'Results/PrimaryMotion.txt').read_text().splitlines():
    Category = Line.split()[0]
    if Category.startswith('PRIMARY'):
        Parts = {K: float(V) for K, V in (Pair.split('=', 1) for Pair in Line.split()[1:] if '=' in Pair)}
        Rows.setdefault(Category, []).append(Parts)
assert len(Rows['PRIMARY_MOTION']) == len(Rows['PRIMARY_REPROJECTION']) == len(Rows['PRIMARY']) == 6
Total = Rows['PRIMARY_MOTION_SUM'][0]
Report = {'checks': Rows, 'moving_rmse_reduction_percent': 100 * (1 - Total['filtered_rmse_sum'] / Total['fresh_rmse_sum']),
          'physical_gpu_executed': False, 'device': 'SwiftShader CPU type 4; see PrimaryMotion.txt',
          'cpu_primary_raster_executed': False, 'indirect_transport_executed': False,
          'environment': 'black; analytic direct illumination plus visible material emission',
          'warmup': '16 static updates, then five moving updates; light revision at pose 3',
          'comparison': 'fresh eight-candidate RIS left; temporal/spatial reuse and reconstruction right',
          'animation': 'six lossless readbacks; illustrative playback timing, not measured frame time',
          'motion_rgb_sha256': [hashlib.sha256(P).hexdigest() for P in Frames]}
(Output / 'Results/PrimaryMotion.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS six lossless GPU-primary motion frames; moving RMSE reduction %.2f%%' % Report['moving_rmse_reduction_percent'])
