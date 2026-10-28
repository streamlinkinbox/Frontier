"""Actual raw/filtered Vulkan transport readbacks and losslessly decoded motion evidence."""
from Images import Root, Output, Read, Png
import hashlib
import json
import struct
import zlib
Scratch = Root / '_ClaudeScratch/light-compute'
Before, After = Read(Scratch / 'TransportUnfiltered1.ppm'), Read(Scratch / 'TransportFiltered1.ppm')
Comparison = b''.join(Before[Y*1920:(Y+1)*1920] + After[Y*1920:(Y+1)*1920] for Y in range(400))
(Output / 'Images/TransportReconstructionComparison.png').write_bytes(Png(1280, 400, Comparison))
for Name in ('TransportFiltered1', 'TransportUnfiltered1'):
    (Output / f'Images/{Name}.png').write_bytes(Png(640, 400, Read(Scratch / f'{Name}.ppm')))
def Chunk(Name, Content):
    return struct.pack('>I', len(Content)) + Name + Content + struct.pack('>I', zlib.crc32(Name + Content))
Frames = [Read(Scratch / f'TransportFiltered{I}.ppm') for I in range(6)]
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
(Output / 'Images/TransportFiltered.png').write_bytes(Animation)
Rows = {}
for Line in (Output / 'Results/TransportReconstruction.txt').read_text().splitlines():
    Category = Line.split()[0]
    if Category.startswith('TRANSPORT_RECONSTRUCTION'):
        Rows.setdefault(Category, []).append({K: float(V) for K,V in (P.split('=',1) for P in Line.split()[1:] if '=' in P)})
assert len(Rows['TRANSPORT_RECONSTRUCTION_MOTION']) == 6
R = Rows['TRANSPORT_RECONSTRUCTION'][0]
Report = {'checks': Rows, 'isolated_emission_rmse_reduction_percent': 100*(1-R['filtered_rmse']/R['fresh_rmse']),
          'reference': '160x100, isolated emission, 16 independent seeds times 16 area draws; 8 reconstruction updates',
          'full_resolution_capture': '640x400, six moving poses, one update each, matched direct histories and seeds',
          'filter': 'reprojected capped mean, current-neighborhood RGB clamp, 5x5 surface-compatible spatial kernel',
          'bias': 'nonlinear clamping and spatial filtering are biased; moving-emitter/visibility changes are not exhaustively validated',
          'physical_gpu_executed': False, 'cpu_primary_raster_executed': False,
          'animation': 'six actual lossless readbacks; illustrative timing, not frame-rate proof',
          'motion_rgb_sha256': [hashlib.sha256(F).hexdigest() for F in Frames]}
(Output / 'Results/TransportReconstruction.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS lossless indirect reconstruction evidence; isolated emission RMSE %.2f%% lower' % Report['isolated_emission_rmse_reduction_percent'])
