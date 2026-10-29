"""Retain matched native field-on/off pixels and geometry-only distance-bake evidence."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import zlib

Root=Path(__file__).resolve().parents[3]
Exhibit=Root/'Exhibits/VehicleReference'
Parser=argparse.ArgumentParser(description=__doc__)
Parser.add_argument('--require-baseline',action='store_true',dest='RequireBaseline')
Arguments=Parser.parse_args()
Width,Height=640,400

def Encode(Pixels,Width,Height):
    def Chunk(Category,Content):
        return struct.pack('>I',len(Content))+Category+Content+struct.pack('>I',zlib.crc32(Category+Content))
    Rows=b''.join(b'\x00'+Pixels[Row*Width*3:(Row+1)*Width*3] for Row in range(Height))
    return (b'\x89PNG\r\n\x1a\n'+Chunk(b'IHDR',struct.pack('>IIBBBBB',Width,Height,8,2,0,0,0))+
            Chunk(b'IDAT',zlib.compress(Rows))+Chunk(b'IEND',b''))

def Read(Stem):
    Payload=(Root/f'_ClaudeScratch/vehicle-reference/{Stem}.ppm').read_bytes()
    Header=f'P6\n{Width} {Height}\n255\n'.encode()
    assert Payload.startswith(Header) and len(Payload)==len(Header)+Width*Height*3
    return Payload[len(Header):]

Rows=[]
for Pose in range(3):
    Off=Read(f'Vehicle{Pose}')
    On=Read(f'Distance{Pose}')
    Encoded=Encode(Off,Width,Height)
    Prior=subprocess.run(['git','show',f'4a0e4ced:Exhibits/VehicleReference/Capture/Vehicle{Pose}.png'],cwd=Root,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    MatchesBaseline=(Prior.stdout==Encoded) if Prior.returncode==0 else None
    if Arguments.RequireBaseline:
        assert MatchesBaseline, 'Disabled branch changed prior committed native control'
    (Exhibit/f'Capture/Distance{Pose}.png').write_bytes(Encode(On,Width,Height))
    Deltas=[abs(A-B) for A,B in zip(Off,On)]
    Changed=sum(any(Off[Pixel+Channel]!=On[Pixel+Channel] for Channel in range(3)) for Pixel in range(0,len(Off),3))
    assert Changed>100 and max(Deltas)>2, 'Triangle-field lighting route did not visibly execute'
    Values=(Root/f'_ClaudeScratch/vehicle-reference/Distance{Pose}.bin').read_bytes()
    assert len(Values)==64*32*24*8
    Distances=struct.unpack('<'+'d'*(len(Values)//8),Values)
    assert all(0<=Number<float('inf') for Number in Distances)
    Rows.append({'pose':Pose,'changed_display_pixels':Changed,'display_code_mean_absolute_error':sum(Deltas)/len(Deltas),
                 'display_code_max_absolute_error':max(Deltas),'off_matches_previous_commit':MatchesBaseline,
                 'distance_grid_sha256':hashlib.sha256(Values).hexdigest()})
    if Pose==0:
        Pair=b''.join(Off[Row*Width*3:(Row+1)*Width*3]+b'\x10\x17\x22'*8+
                      On[Row*Width*3:(Row+1)*Width*3] for Row in range(Height))
        (Exhibit/'Capture/DistanceComparison.png').write_bytes(Encode(Pair,Width*2+8,Height))
Report={'path':'native C++ CPU reference; not Vulkan','comparison':'left field off; right field on: occlusion + one-bounce source radiance',
        'distance':'unsigned open-triangle distance, conservative interpolation strides, exact triangle hit refinement',
        'resolution':[64,32,24],'samples_per_pose':49152,'vehicle_facets':25068,'poses':Rows}
(Exhibit/'DistanceImageResults.json').write_text(json.dumps(Report,indent=2)+'\n')
print(json.dumps(Report,indent=2))
