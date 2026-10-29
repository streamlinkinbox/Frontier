"""Lossless PNG wrappers and an exact native display-readback comparison; no synthesized lighting."""
from pathlib import Path
import hashlib
import json
import struct
import zlib
Root = Path(__file__).resolve().parents[3]
Output = Root / 'Exhibits/ClusteredLighting'
Output.joinpath('Capture').mkdir(exist_ok=True)
def Png(Width, Height, Pixels):
    def Chunk(Name, Content):
        return (struct.pack('>I', len(Content)) + Name + Content
                + struct.pack('>I', zlib.crc32(Name + Content)))
    Stride = Width * 3
    Scanlines = b''.join(b'\0' + Pixels[Y * Stride:(Y + 1) * Stride] for Y in range(Height))
    return (b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', Width, Height, 8, 2, 0, 0, 0))
            + Chunk(b'IDAT', zlib.compress(Scanlines)) + Chunk(b'IEND', b''))
Images, Hashes = [], []
for Stem in ['AnalyticAll', 'AnalyticCluster']:
    Source = Root / f'_ClaudeScratch/vehicle-reference/{Stem}0.ppm'
    Header = b'P6\n640 400\n255\n'
    Payload = Source.read_bytes()
    assert Payload.startswith(Header)
    Pixels = Payload[len(Header):]
    assert len(Pixels) == 640 * 400 * 3
    Hashes.append(hashlib.sha256(Pixels).hexdigest())
    Images.append(Pixels)
    (Output / f'Capture/{Stem}.png').write_bytes(Png(640, 400, Pixels))
assert Images[0] == Images[1], 'Clustering changed the rendered RGB readback'
Composite = b''.join(Images[0][Y*1920:(Y+1)*1920] + Images[1][Y*1920:(Y+1)*1920] for Y in range(400))
(Output / 'Capture/Comparison.png').write_bytes(Png(1280, 400, Composite))
Records = []
for Line in (Output / 'CaptureResults.txt').read_text().splitlines():
    Records.append(dict(Part.split('=', 1) for Part in Line.split() if '=' in Part))
assert len(Records) == 2 and Records[0]['hash'] == Records[1]['hash']
AllVisits, LocalVisits = [int(Record['light_list_visits']) for Record in Records]
Report = {'extent': [640, 400], 'vehicle_facets': int(Records[0]['original_vehicle_facets']),
          'analytic_lights': AllVisits // int(Records[0]['surface_pixels']),
          'display_rgb_identical': True, 'changed_display_pixels': 0, 'rgb_sha256': Hashes,
          'rendering_path': 'native C++ CPU reference, exhaustive direct sum in both images',
          'ies_fixture': 'synthetic LM-63 data, not manufacturer photometry',
          'all_light_list_visits': AllVisits, 'clustered_light_list_visits': LocalVisits,
          'list_visit_reduction_percent': 100 * (1 - LocalVisits / AllVisits),
          'clustered_fallback_queries': int(Records[1]['light_fallbacks']),
          'timing_or_gpu_speedup_claimed': False}
(Output / 'ImageResults.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS native ControlVehicle RGB readbacks identical; conservative fallback retained')
