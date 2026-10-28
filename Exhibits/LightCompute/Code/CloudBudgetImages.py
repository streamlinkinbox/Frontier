"""Losslessly retain lower-sample cloud readbacks, with optional same-scene reference comparisons."""
from Images import Root, Output, Png
import hashlib
import json
import re
import sys
import zlib

Compare = '--compare' in sys.argv
Stem = 'CloudBudget' if Compare else 'CloudPreview'
Log = (Output / f'Results/{Stem}.txt').read_text()
Rows = [{K: float(V) for K, V in (P.split('=', 1) for P in Line.split()[1:])}
        for Line in Log.splitlines() if Line.startswith('CLOUD_BUDGET ')]
assert len(Rows) == (4 if Compare else 2)
assert all(R['max_rgb_error'] < .003 and R['sky_max_rgb_error'] < .003 and R['sky_probes'] > 0 for R in Rows)
Width, Height = 160, 100
Frames = {}

def Save(Name, Pixels, W, H):
    Payload = Png(W, H, Pixels)
    # Png uses one unfiltered IDAT chunk. Verify its decoded scanlines exactly, not a visual approximation.
    Offset, Compressed = 8, b''
    while Offset < len(Payload):
        Length = int.from_bytes(Payload[Offset:Offset + 4], 'big')
        Tag, Content = Payload[Offset + 4:Offset + 8], Payload[Offset + 8:Offset + 8 + Length]
        assert zlib.crc32(Tag + Content) == int.from_bytes(Payload[Offset + 8 + Length:Offset + 12 + Length], 'big')
        if Tag == b'IDAT':
            Compressed += Content
        Offset += Length + 12
    Scanlines = zlib.decompress(Compressed)
    assert len(Scanlines) == (W * 3 + 1) * H
    assert all(Scanlines[Y * (W * 3 + 1)] == 0 for Y in range(H))
    assert b''.join(Scanlines[Y * (W * 3 + 1) + 1:(Y + 1) * (W * 3 + 1)] for Y in range(H)) == Pixels
    (Output / f'Images/{Name}.png').write_bytes(Payload)

for Wind in (0, 120):
    for Steps in ((32, 64) if Compare else (32,)):
        Name = f'CloudBudget{Wind}_{Steps}'
        Payload = (Root / '_ClaudeScratch/light-compute' / (Name + '.ppm')).read_bytes()
        Header = b'P6\n160 100\n255\n'
        assert Payload.startswith(Header)
        Pixels = Payload[len(Header):]
        assert len(Pixels) == Width * Height * 3
        Frames[Name] = Pixels
        Save(Name, Pixels, Width, Height)
Columns = (32, 64) if Compare else (32,)
Pixels = b''.join(b''.join(Frames[f'CloudBudget{Wind}_{Steps}'][Y * 480:(Y + 1) * 480]
                           for Steps in Columns) for Wind in (0, 120) for Y in range(Height))
Save(Stem + 'Comparison', Pixels, Width * len(Columns), Height * 2)
Device = Log.splitlines()[0]
Report = {
    'device': Device, 'software_vulkan_executed': 'type=4 ' in Device,
    'physical_gpu_executed': bool(re.search(r'type=[12] ', Device)),
    'extent': [Width, Height], 'columns_cloud_and_fog_shadow_steps': list(Columns),
    'rows_wind_seconds': [0, 120], 'checks': Rows,
    'errors_to_finite64': [{K: float(V) for K, V in (P.split('=', 1) for P in Line.split()[1:])}
                         for Line in Log.splitlines() if Line.startswith('CLOUD_BUDGET_ERROR ')],
    'working_preset': '32 cloud view/cube/surface-shadow steps, 6 cloud self-shadow steps, 32 fog view and 32 fog cloud-shadow steps',
    'reference': '64 cloud and fog-shadow steps; finite comparison, not ground truth' if Compare else None,
    'cost': 'halves cloud outer/shadow step counts versus 64; no measured frame-time or hardware speed claim',
    'appearance': 'lower-budget trade-off accepted for iteration; regular horizon/cloud aliasing remains',
    'retained': 'same authored ControlVehicle, geometry, density, atmosphere and analytic/emissive surface lighting',
    'readback_rgb_sha256': {Name: hashlib.sha256(P).hexdigest() for Name, P in Frames.items()},
}
(Output / f'Results/{Stem}.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS lossless cloud-budget images and native cloud/fog checks')
