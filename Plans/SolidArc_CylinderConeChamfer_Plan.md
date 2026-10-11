# SolidArc Set 3 plan — complete cylinder–cone root chamfer

## Bounded next slice

Extend the complete circular curved-edge chamfer family to one mixed curved-support pair:

- a native coaxial cylindrical boss meeting a native coaxial conical frustum;
- the root is a complete rational circular edge shared by the cylinder and cone;
- the surrounding source remains the canonical outer cylinder, planar annular shoulder, cylindrical boss, conical frustum, and two caps;
- the setback is measured along the cylindrical support and the cone slant, then bridged by an exact revolved straight meridian, i.e. a native conical chamfer band;
- retain exact cylinder and cone supports on either side of the band;
- require one positive-volume genus-zero hull with no open, non-manifold, or misoriented edges;
- preserve source immutability and refuse zero, consuming, malformed, partial, non-coaxial, and freeform cases;
- add a dedicated verification and source/result proof image before documenting the capability.

## Explicit non-goals

This slice does not implement arbitrary cone–cylinder intersections, non-coaxial or oblique support pairs, partial circular loops, multiple curved roots, curved corner patches, concave/non-convex networks, variable setback laws, or general intersection/trim/sew healing. Similar-looking geometry must continue to refuse unless it satisfies the canonical complete topology and measured support identities.

## Acceptance artifacts

- bounded kernel classifier and reconstruction route;
- `Verification/CylinderConeChamferVerification.cpp`;
- CMake and `Tools/Build/CheckSolidArc.sh` entries;
- `Proofs/Phase36g_CylinderConeChamfer.png`;
- roadmap and blend-limit documentation updated only after the gate and proof pass.
