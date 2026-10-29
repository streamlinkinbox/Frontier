#============================================================================================================================================
#                                                          VEHICLEGLASSEDGES.PY
#============================================================================================================================================
# 🧩 Count exact-position boundary edges before assigning closed-volume transport to retained vehicle glazing.

from pathlib import Path
import collections
import hashlib
import json
import struct
import sys

Source = Path(sys.argv[1])
Destination = Path(sys.argv[2])
Archive = Source.read_bytes()
assert struct.unpack_from('<I', Archive)[0] == 0x33434c46
Offset = 12
for Stride in (96, 144, 48, 48, 4):
    Count = struct.unpack_from('<I', Archive, Offset)[0]
    Offset += 4 + Count * Stride
Count = struct.unpack_from('<I', Archive, Offset)[0]
Offset += 4
Edges = collections.Counter()
Directed = collections.Counter()
Facets = 0
for Index in range(Count):
    Triangle = struct.unpack_from('<36f', Archive, Offset + Index * 144)
    if abs(Triangle[24] - .026) >= 1e-5 or abs(Triangle[27] - .02) >= 1e-5:
        continue
    Facets += 1
    Corners = [tuple(Triangle[I:I+3]) for I in (0, 4, 8)]
    for I in range(3):
        A, B = Corners[I], Corners[(I+1) % 3]
        Edges[tuple(sorted((A, B)))] += 1
        Directed[(A, B)] += 1
Boundary = sum(Count == 1 for Count in Edges.values())
Nonmanifold = sum(Count > 2 for Count in Edges.values())
Winding = sum(Count == 2 and Directed[(A, B)] != 1 for (A, B), Count in Edges.items())
assert Facets == 74 and Boundary == 68
Report = {'source_sha256': hashlib.sha256(Archive).hexdigest(), 'glass_triangles': Facets,
          'boundary_edges': Boundary, 'paired_edges': sum(Count == 2 for Count in Edges.values()),
          'nonmanifold_edges': Nonmanifold, 'inconsistent_paired_winding': Winding,
          'closed_volume_assignment': False,
          'scope': 'Exact float-position incidence only; no tolerant welding, intersection or thickness acceptance.'}
Destination.write_text(json.dumps(Report, indent=2) + '\n')
print('VEHICLE_GLASS_EDGES triangles=%d boundary=%d paired=%d closed_volume_assignment=0' %
      (Facets, Boundary, Report['paired_edges']))
