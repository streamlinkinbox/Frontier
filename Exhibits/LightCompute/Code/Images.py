"""Wrap real CPU/Vulkan readbacks as PNG; compare display bytes without synthesizing lighting."""
from pathlib import Path
import hashlib
import json
import re
import struct
import zlib
Root = Path(__file__).resolve().parents[3]
Output = Root / 'Exhibits/LightCompute'
def Read(Pathname):
    Payload = Pathname.read_bytes()
    Header = b'P6\n640 400\n255\n'
    assert Payload.startswith(Header)
    Payload = Payload[len(Header):]
    assert len(Payload) == 640 * 400 * 3
    return Payload
def Png(Width, Height, Pixels):
    def Chunk(Name, Content):
        return struct.pack('>I', len(Content)) + Name + Content + struct.pack('>I', zlib.crc32(Name + Content))
    Stride = Width * 3
    Scanlines = b''.join(b'\0' + Pixels[Y*Stride:(Y+1)*Stride] for Y in range(Height))
    return (b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', Width, Height, 8, 2, 0, 0, 0))
            + Chunk(b'IDAT', zlib.compress(Scanlines)) + Chunk(b'IEND', b''))
if __name__ == '__main__':
    Native = Read(Root / '_ClaudeScratch/vehicle-reference/VulkanInput0.ppm')
    All = Read(Root / '_ClaudeScratch/light-compute/All.ppm')
    Clustered = Read(Root / '_ClaudeScratch/light-compute/Clustered.ppm')
    Ris = Read(Root / '_ClaudeScratch/light-compute/FreshRis.ppm')
    assert All == Clustered
    Deltas = [abs(A-B) for A, B in zip(Native, Clustered)]
    assert max(Deltas) <= 1, 'Native/Vulkan display comparison exceeded one code step'
    for Name, Pixels in [('Native', Native), ('Clustered', Clustered), ('FreshRis', Ris)]:
        (Output / f'Images/{Name}.png').write_bytes(Png(640, 400, Pixels))
    Comparison = b''.join(Native[Y*1920:(Y+1)*1920] + Clustered[Y*1920:(Y+1)*1920] for Y in range(400))
    (Output / 'Images/Comparison.png').write_bytes(Png(1280, 400, Comparison))
    DeviceType = int(re.search(r' type=(\d+)', (Output / 'Results/Vulkan.txt').read_text()).group(1))
    Report = {'extent': [640, 400], 'left': 'native double-precision exact lighting',
              'right': 'actual Vulkan clustered exact lighting; CPU primary visibility and sky retained',
              'vulkan_all_vs_clustered_identical': True, 'native_vs_vulkan_max_display_delta': max(Deltas),
              'native_vs_vulkan_changed_pixels': sum(Native[I:I+3] != Clustered[I:I+3] for I in range(0, len(Native), 3)),
              'native_rgb_sha256': hashlib.sha256(Native).hexdigest(),
              'vulkan_rgb_sha256': hashlib.sha256(Clustered).hexdigest(),
              'fresh_ris_rgb_sha256': hashlib.sha256(Ris).hexdigest(),
              'physical_gpu_executed': DeviceType in (1, 2), 'software_vulkan_executed': DeviceType == 4}
    (Output / 'Results/Images.json').write_text(json.dumps(Report, indent=2) + '\n')
    print('PASS actual native/Vulkan display comparison:', Report['native_vs_vulkan_changed_pixels'],
          'changed pixels, max delta', max(Deltas))
