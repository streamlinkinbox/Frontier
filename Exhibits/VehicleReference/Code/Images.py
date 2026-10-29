"""Losslessly wrap native display-encoded PPM readbacks as PNG; no geometry or lighting synthesis."""
from pathlib import Path
import hashlib
import json
import struct
import zlib

Root = Path(__file__).resolve().parents[3]
Output = Root / 'Exhibits/VehicleReference/Capture'
Hashes = []
for Index in range(3):
    Source = Root / f'_ClaudeScratch/vehicle-reference/Vehicle{Index}.ppm'
    Payload = Source.read_bytes()
    Width, Height = 640, 400
    Stride = Width * 3
    Header = f'P6\n{Width} {Height}\n255\n'.encode()
    assert Payload.startswith(Header)
    Pixels = Payload[len(Header):]
    assert len(Pixels) == Width * Height * 3
    def Chunk(Category, Content):
        return struct.pack('>I', len(Content)) + Category + Content + struct.pack('>I', zlib.crc32(Category + Content))
    Scanlines = b''.join(b'\x00' + Pixels[Row * Stride:(Row + 1) * Stride] for Row in range(Height))
    Encoded = (b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', Width, Height, 8, 2, 0, 0, 0))
               + Chunk(b'IDAT', zlib.compress(Scanlines)) + Chunk(b'IEND', b''))
    (Output / f'Vehicle{Index}.png').write_bytes(Encoded)
    Hashes.append(hashlib.sha256(Pixels).hexdigest())
assert len(set(Hashes)) == 3, 'Three rigid poses must not repeat the same image'
print('PASS: three distinct native 640x400 CPU display readbacks; PNG preserves RGB bytes exactly')
(Root / 'Exhibits/VehicleReference/ImageResults.txt').write_text(
    json.dumps({'native_extent': [640, 400], 'distinct_pose_images': 3, 'rgb_sha256': Hashes,
                'rendering_path': 'native C++ CPU reference, not Vulkan'}, indent=2) + '\n')
