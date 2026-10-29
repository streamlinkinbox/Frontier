"""Package actual 320x200 Vulkan fog readbacks without inventing beam imagery."""
from Images import Root, Output, Png
import hashlib
import json
import re
import struct
import zlib
Scratch = Root / '_ClaudeScratch/light-compute'
Names = ('FogDry', 'FogShafts0', 'FogShafts1', 'FogShafts2', 'FogLocalDry', 'FogLocal', 'FogSky0', 'FogSky1')
def ReadFog(Name):
    Payload = (Scratch / (Name + '.ppm')).read_bytes()
    Header = b'P6\n320 200\n255\n'
    assert Payload.startswith(Header)
    Pixels = Payload[len(Header):]
    assert len(Pixels) == 320 * 200 * 3
    return Pixels
def VerifyPng(Payload, Pixels, Width):
    Offset, Compressed = 8, b''
    while Offset < len(Payload):
        Length = struct.unpack_from('>I', Payload, Offset)[0]
        Tag, Content = Payload[Offset+4:Offset+8], Payload[Offset+8:Offset+8+Length]
        assert zlib.crc32(Tag + Content) == struct.unpack_from('>I', Payload, Offset+8+Length)[0]
        if Tag == b'IDAT': Compressed += Content
        Offset += Length + 12
    Scanlines = zlib.decompress(Compressed)
    Stride = Width * 3
    assert b''.join(Scanlines[Y*(Stride+1)+1:(Y+1)*(Stride+1)] for Y in range(200)) == Pixels
Frames = {Name: ReadFog(Name) for Name in Names}
assert Frames['FogShafts0'] != Frames['FogShafts1'] != Frames['FogShafts2']
assert Frames['FogLocalDry'] != Frames['FogLocal']
for Name, Pixels in Frames.items():
    Payload = Png(320, 200, Pixels)
    VerifyPng(Payload, Pixels, 320)
    (Output / f'Images/{Name}.png').write_bytes(Payload)
for Name, Panels in (('FogComparison', ('FogDry', 'FogShafts0', 'FogShafts1')),
                     ('FogLocalComparison', ('FogLocalDry', 'FogLocal')),
                     ('FogSkyComparison', ('FogSky0', 'FogSky1'))):
    Pixels = b''.join(b''.join(Frames[P][Y*960:(Y+1)*960] for P in Panels) for Y in range(200))
    Payload = Png(320 * len(Panels), 200, Pixels)
    VerifyPng(Payload, Pixels, 320 * len(Panels))
    (Output / f'Images/{Name}.png').write_bytes(Payload)
Rows = {}
for Filename in ('FogChecks.txt', 'FogRays.txt', 'FogSky.txt'):
    for Line in (Output / 'Results' / Filename).read_text().splitlines():
        if Line.startswith('FOG_'):
            Category = Line.split()[0]
            Rows.setdefault(Category, []).append({K: float(V) for K,V in (P.split('=',1) for P in Line.split()[1:] if '=' in P)})
Log = (Output / 'Results/FogRays.txt').read_text()
assert len(Rows['FOG_RAY']) == 8 and len(Rows['FOG_LOCAL']) == 2 and len(Rows['FOG_SKY']) == 2
DeviceType = int(re.search(r' type=(\d+)', Log).group(1))
Report = {'device': Log.splitlines()[0], 'physical_gpu_executed': DeviceType in (1, 2),
          'compute_revision': 'cdc516d; exact current sources and products in Results/Manifest.json',
          'compiler_configuration': 'GCC 12.2 Release captures; RelWithDebInfo ASan/UBSan numeric checks, external-driver leak detection disabled',
          'extent': [320, 200], 'checks': Rows, 'software_vulkan_executed': DeviceType == 4,
          'day_panels': ['fog disabled', 'fog with cloud shadow disabled only for in-scattering',
                         'fog with cloud- and geometry-shadowed in-scattering'],
          'sky_panels': 'same raised camera, in-scattering cloud shadow off/on; broken cloud layer 800-1100m, density scale 2.5, extinction 0.02/m',
          'wind_capture': 'FogShafts2, same field at 120 seconds; fixed Sun and stationary vehicle/camera',
          'fog_model': 'new shadowed single-scattering integration of inherited exponential height density; not a colour blend',
          'integration': 'analytic camera/light-segment optical depth; 32 view midpoints, 32 cloud-shadow samples; local-light view uses 64 midpoints',
          'day_medium': 'base extinction 0.0015/m, scale height 450m, scattering albedo 0.9, HG g 0.65',
          'local_medium': 'base extinction 0.07/m, scale height 10m, HG g 0.35; original point/IES/spot sources',
          'vehicle': 'same authored ControlVehicle; analytic lights, visible emission and four-sample emissive/diffuse transport retained',
          'emissive_fog': 'sampled emitter-to-surface extinction included; emissive-volume in-scattering disabled in these captures, opt-in implementation documented in FogFacets.json',
          'history': 'fog resolved after surface reconstruction; resolved scattering never feeds surface history',
          'limitations': ['single scattering, not multiple scattering', 'regular view/shadow sampling can alias',
                          'no temporal fog reconstruction', 'diffuse inter-bounce fog scattering/extinction not fully coupled',
                          'bounded local medium, observer-centred cloud environment for indirect rays',
                          'not physical-GPU, Windows or real-time acceptance; no new celestial integration'],
          'readback_rgb_sha256': {Name: hashlib.sha256(Pixels).hexdigest() for Name,Pixels in Frames.items()},
          'exact_source_and_product_hashes': 'Results/Manifest.json'}
(Output / 'Results/Fog.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS lossless actual Vulkan sun-shaft and point/IES/spot-fog readbacks')
