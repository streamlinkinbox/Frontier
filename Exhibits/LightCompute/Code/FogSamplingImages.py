"""Retain lossless Vulkan quadrature readbacks and finite-reference error measurements."""
from Images import Root, Output, Png
import hashlib
import json
import re
import struct
import zlib

Log = (Output / 'Results/FogSampling.txt').read_text()
Rows = []
for Line in Log.splitlines():
    if Line.startswith('FOG_SAMPLING '):
        Rows.append({K: float(V) for K, V in (P.split('=', 1) for P in Line.split()[1:])})
assert len(Rows) == 8
assert 'FOG_SAMPLING_PRESET shadow_steps=64 both_winds_improved=1 reference_finite=1' in Log
Width, Height = 160, 100
Frames = {}

def Verify(Payload, Pixels, Extent):
    assert Payload[:8] == b'\x89PNG\r\n\x1a\n'
    Offset, Compressed = 8, b''
    while Offset < len(Payload):
        Length = struct.unpack_from('>I', Payload, Offset)[0]
        Tag, Content = Payload[Offset + 4:Offset + 8], Payload[Offset + 8:Offset + 8 + Length]
        assert zlib.crc32(Tag + Content) == struct.unpack_from('>I', Payload, Offset + 8 + Length)[0]
        if Tag == b'IDAT':
            Compressed += Content
        Offset += Length + 12
    Scanlines = zlib.decompress(Compressed)
    Stride = Extent[0] * 3
    assert len(Scanlines) == (Stride + 1) * Extent[1]
    assert all(Scanlines[Y * (Stride + 1)] == 0 for Y in range(Extent[1]))
    assert b''.join(Scanlines[Y * (Stride + 1) + 1:(Y + 1) * (Stride + 1)]
                    for Y in range(Extent[1])) == Pixels

for Wind in (0, 120):
    for Steps in (16, 32, 64, 128):
        Name = f'FogSampling{Wind}_{Steps}'
        Payload = (Root / '_ClaudeScratch/light-compute' / (Name + '.ppm')).read_bytes()
        Header = f'P6\n{Width} {Height}\n255\n'.encode()
        assert Payload.startswith(Header)
        Pixels = Payload[len(Header):]
        assert len(Pixels) == Width * Height * 3
        Frames[Name] = Pixels
        Payload = Png(Width, Height, Pixels)
        Verify(Payload, Pixels, (Width, Height))
        (Output / f'Images/{Name}.png').write_bytes(Payload)
    AtWind = {int(R['shadow_steps']): R for R in Rows if R['wind'] == Wind}
    assert set(AtWind) == {16, 32, 64, 128}
    assert AtWind[64]['rmse_to128'] < AtWind[16]['rmse_to128']
    assert AtWind[128]['rmse_to128'] == 0
    assert all(R['width'] == Width and R['height'] == Height for R in AtWind.values())
    assert all(R['native_rgb_error'] < .003 and R['native_probes'] > 100 for R in AtWind.values())

# Exact row concatenation only: no invented shafts, resampling, contrast boost or difference colouring.
Pixels = b''.join(b''.join(Frames[f'FogSampling{Wind}_{Steps}'][Y * Width * 3:(Y + 1) * Width * 3]
                           for Steps in (16, 64, 128)) for Wind in (0, 120) for Y in range(Height))
Payload = Png(Width * 3, Height * 2, Pixels)
Verify(Payload, Pixels, (Width * 3, Height * 2))
(Output / 'Images/FogSamplingComparison.png').write_bytes(Payload)
Device = Log.splitlines()[0]
DeviceType = int(re.search(r' type=(\d+)', Device).group(1))
Report = {
    'device': Device, 'physical_gpu_executed': DeviceType in (1, 2),
    'software_vulkan_executed': DeviceType == 4,
    'extent': [Width, Height], 'view_steps': 32, 'shadow_budgets': [16, 32, 64, 128],
    'reference': 'finite 128-step cloud-shadow quadrature, NOT converged ground truth',
    'rows': Rows, 'selected_shadow_steps': 64,
    'comparison_columns': [16, 64, 128], 'comparison_rows_wind_seconds': [0, 120],
    'retained': 'original ControlVehicle, Sun, atmosphere, analytic and four-sample emissive/diffuse lighting',
    'cloud_field': 'inherited procedural density; identical across budgets, only wind time changes between rows',
    'historical_metadata_correction': 'Fog.json day medium corrected from 0.0007/m and 650m to actual source 0.0015/m and 450m; old images unchanged',
    'configuration': 'GCC Release, pinned Slang spirv_1_3 O0, Linux software Vulkan triangle BVH',
    'limits': ['two wind fixtures, not broad convergence', 'view steps remain 32 and can alias',
               '4x shadow-sample budget versus 16, no speed or real-time claim',
               'no physical GPU, Windows or multiple scattering validation'],
    'readback_rgb_sha256': {Name: hashlib.sha256(RGB).hexdigest() for Name, RGB in Frames.items()},
    'exact_source_and_product_hashes': 'Results/Manifest.json',
}
(Output / 'Results/FogSampling.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS eight lossless Vulkan fog-budget readbacks, two wind fixtures, finite-reference comparison')
