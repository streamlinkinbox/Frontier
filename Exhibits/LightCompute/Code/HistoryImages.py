"""Lossless native Vulkan readbacks, animated PNG sequence and quantitative reconstruction report."""
from Images import Read, Png, Root, Output
import hashlib
import json
import struct
import zlib
Scratch = Root / '_ClaudeScratch/light-compute'
for Name in ['History', 'HistoryFresh', 'MotionReject1', 'MotionExact1', 'MotionFresh1']:
    Pixels = Read(Scratch / (Name + '.ppm'))
    (Output / 'Images' / (Name + '.png')).write_bytes(Png(640, 400, Pixels))
Fresh, Reconstructed = Read(Scratch / 'MotionFresh1.ppm'), Read(Scratch / 'Motion1.ppm')
Comparison = b''.join(Fresh[Y*1920:(Y+1)*1920] + Reconstructed[Y*1920:(Y+1)*1920] for Y in range(400))
(Output / 'Images/HistoryComparison.png').write_bytes(Png(1280, 400, Comparison))
def Chunk(Name, Content):
    return struct.pack('>I', len(Content)) + Name + Content + struct.pack('>I', zlib.crc32(Name + Content))
Images = [Read(Scratch / f'Motion{Index}.ppm') for Index in range(6)]
Animation = b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', 640, 400, 8, 2, 0, 0, 0))
Animation += Chunk(b'acTL', struct.pack('>II', len(Images), 0))
Sequence = 0
for Index, Pixels in enumerate(Images):
    Animation += Chunk(b'fcTL', struct.pack('>IIIIIHHBB', Sequence, 640, 400, 0, 0, 12 if Index == 0 else 4, 10, 0, 0))
    Sequence += 1
    Scanlines = b''.join(b'\0' + Pixels[Y*1920:(Y+1)*1920] for Y in range(400))
    Compressed = zlib.compress(Scanlines)
    if Index == 0:
        Animation += Chunk(b'IDAT', Compressed)
    else:
        Animation += Chunk(b'fdAT', struct.pack('>I', Sequence) + Compressed)
        Sequence += 1
Animation += Chunk(b'IEND', b'')
(Output / 'Images/Motion.png').write_bytes(Animation)
# Check animation structure, sequence numbering, checksums and lossless frame payloads.
Offset, Controls, Next, Frame = 8, 0, 0, 0
while Offset < len(Animation):
    Length = struct.unpack_from('>I', Animation, Offset)[0]
    Name = Animation[Offset+4:Offset+8]
    Content = Animation[Offset+8:Offset+8+Length]
    assert zlib.crc32(Name + Content) == struct.unpack_from('>I', Animation, Offset+8+Length)[0]
    if Name in [b'fcTL', b'fdAT']:
        assert struct.unpack_from('>I', Content)[0] == Next
        Next += 1
    if Name == b'fcTL':
        Controls += 1
    if Name in [b'IDAT', b'fdAT']:
        Decoded = zlib.decompress(Content if Name == b'IDAT' else Content[4:])
        assert b''.join(Decoded[Y*1921+1:(Y+1)*1921] for Y in range(400)) == Images[Frame]
        Frame += 1
    Offset += 12 + Length
assert Controls == Frame == 6
Rows = {}
Moving = []
for Line in (Output / 'Results/Vulkan.txt').read_text().splitlines():
    Category = Line.split()[0]
    Parts = {Pair.split('=', 1)[0]: float(Pair.split('=', 1)[1])
             for Pair in Line.split()[1:] if '=' in Pair and Category not in ['DEVICE']}
    if Category == 'MOTION':
        Moving.append(Parts)
    elif Category in ['HISTORY', 'MOTION_SUM', 'RESET', 'REFRESH', 'REUSE_ENERGY', 'REJECTION']:
        Rows[Category] = Parts
assert len(Moving) == 5
Static = Rows['HISTORY']
Total = Rows['MOTION_SUM']
Report = {'static': Static, 'moving': Moving, 'reuse_energy': Rows['REUSE_ENERGY'], 'rejection': Rows['REJECTION'],
          'reset': Rows['RESET'], 'residual_refresh': Rows['REFRESH'],
          'static_rmse_reduction_percent': 100 * (1 - Static['reconstructed_rmse'] / Static['fresh_rmse']),
          'moving_mean_fresh_rmse': Total['fresh_rmse_sum'] / 5,
          'moving_mean_filtered_rmse': Total['filtered_rmse_sum'] / 5,
          'moving_rmse_reduction_percent': 100 * (1 - Total['filtered_rmse_sum'] / Total['fresh_rmse_sum']),
          'motion_display_rgb_sha256': [hashlib.sha256(Pixels).hexdigest() for Pixels in Images],
          'animation': 'lossless six-image APNG; display timing is illustrative, not measured rendering time',
          'warmup': '16 static history updates before moving poses; light change on moving index 3 resets history',
          'image_comparison': 'left fresh eight-candidate RIS; right temporal/spatial reservoirs and reconstruction, same moving pose'}
(Output / 'Results/History.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS six lossless Vulkan animation images; static RMSE reduction %.2f%%, moving %.2f%%' %
      (Report['static_rmse_reduction_percent'], Report['moving_rmse_reduction_percent']))
