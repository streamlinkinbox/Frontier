"""Package actual emissive-fog Vulkan readbacks with independently checked image identity."""
from Images import Root, Output, Png
import hashlib
import json
import re
import struct
import zlib

Width, Height = 160, 100
Log = (Output / 'Results/FogFacets.txt').read_text()
Numeric = (Output / 'Results/FogFacetSanitizers.txt').read_text()

def Rows(Text, Prefix):
    return [{K: float(V) for K, V in (P.split('=', 1) for P in Line.split()[1:])}
            for Line in Text.splitlines() if Line.startswith(Prefix + ' ')]

CaptureRows = Rows(Log, 'FOG_FACET')
NumericRows = Rows(Numeric, 'FOG_FACET')
assert len(CaptureRows) == len(NumericRows) == 4
assert [R['case'] for R in CaptureRows] == [0, 1, 2, 3]
assert all(R['width'] == Width and R['height'] == Height for R in CaptureRows)
assert all(R['max_rgb_error'] < .003 for R in CaptureRows + NumericRows)
assert all('FOG_FACET_CHECKS cold_equal=1' in Text for Text in (Log, Numeric))
Analytic = Rows(Log, 'FOG_FACET_ANALYTIC')
assert len(Analytic) == 1 and Analytic[0]['relative_error'] < .002
Frames = {}

def Save(Name, Pixels, W, H):
    Payload = Png(W, H, Pixels)
    Offset, Compressed = 8, b''
    while Offset < len(Payload):
        Length = struct.unpack_from('>I', Payload, Offset)[0]
        Tag, Content = Payload[Offset + 4:Offset + 8], Payload[Offset + 8:Offset + 8 + Length]
        assert zlib.crc32(Tag + Content) == struct.unpack_from('>I', Payload, Offset + 8 + Length)[0]
        if Tag == b'IDAT':
            Compressed += Content
        Offset += Length + 12
    Scanlines = zlib.decompress(Compressed)
    Stride = W * 3
    assert len(Scanlines) == (Stride + 1) * H
    assert all(Scanlines[Y * (Stride + 1)] == 0 for Y in range(H))
    assert b''.join(Scanlines[Y * (Stride + 1) + 1:(Y + 1) * (Stride + 1)] for Y in range(H)) == Pixels
    (Output / f'Images/{Name}.png').write_bytes(Payload)

for Case in range(4):
    Name = f'FogFacet{Case}'
    Payload = (Root / '_ClaudeScratch/light-compute' / (Name + '.ppm')).read_bytes()
    Header = f'P6\n{Width} {Height}\n255\n'.encode()
    assert Payload.startswith(Header)
    Pixels = Payload[len(Header):]
    assert len(Pixels) == Width * Height * 3
    Frames[Name] = Pixels
    Save(Name, Pixels, Width, Height)
assert Frames['FogFacet0'] != Frames['FogFacet1'] != Frames['FogFacet2']
for Name, Cases in [('FogFacetComparison', (0, 1)), ('FogFacetCombined', (1, 3))]:
    Pixels = b''.join(b''.join(Frames[f'FogFacet{C}'][Y * Width * 3:(Y + 1) * Width * 3]
                               for C in Cases) for Y in range(Height))
    Save(Name, Pixels, Width * len(Cases), Height)
Device = Log.splitlines()[0]
DeviceType = int(re.search(r' type=(\d+)', Device).group(1))
Report = {
    'device': Device, 'software_vulkan_executed': DeviceType == 4,
    'physical_gpu_executed': DeviceType in (1, 2), 'windows_executed': False,
    'extent': [Width, Height], 'capture_checks': CaptureRows, 'sanitizer_checks': NumericRows,
    'rectangle_solid_angle': Analytic[0],
    'fixture': 'original ControlVehicle and ground (28276 input facets) plus explicit two-triangle calibration panel',
    'panel': {'corners_m': [[-1, -1, 6], [1, -1, 6], [1, 1, 6], [-1, 1, 6]],
              'two_sided_radiance_rgb': [35, 9, 2]},
    'fog': {'extinction_per_m': .07, 'scale_height_m': 12, 'albedo': .9, 'hg_g': .25,
            'maximum_distance_m': 70, 'view_steps': 128, 'emitter_samples_per_view_step': 16},
    'cases': ['area in-scattering off; extinction and emissive surface lighting retained',
              'area in-scattering on; analytic volume lighting and ambient off',
              'same as 1 but volume geometry occlusion off',
              'both original analytic lighting and emissive volume/surface lighting on'],
    'integration': 'area/luminance discrete CDF, uniform triangle samples, area-to-solid-angle Jacobian, HG phase, triangle visibility, analytic emitter-to-volume extinction',
    'compatibility': 'Fog[15] optional budget 0..64, default 0; existing analytic/surface paths retained',
    'checks': ['cold repeat bitwise equality', 'resolved fog never contaminates surface radiance',
               'zero-density equality', 'empty-CDF equality', 'invalid sample budgets and CDF rejected'],
    'compiler': 'GCC 12.2 Release captures; RelWithDebInfo ASan/UBSan checks; pinned Slang spirv_1_3 O0',
    'sanitizer_scope': 'external-driver leak detection disabled; not a global leak-free claim',
    'limits': ['finite Monte Carlo samples can be noisy', 'regular view marching remains',
               'two-sided emission matches existing surface convention', 'emitter distances <=4mm omitted',
               'single scattering; no inter-bounce/multiple-scattering coupling',
               'no physical GPU, Windows, temporal fog or real-time acceptance'],
    'readback_rgb_sha256': {Name: hashlib.sha256(Pixels).hexdigest() for Name, Pixels in Frames.items()},
    'exact_source_and_product_hashes': 'Results/Manifest.json',
}
(Output / 'Results/FogFacets.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS four actual emissive-fog frames and two lossless comparisons; native and analytic checks retained')
