# Stage 4e — bounded oblique planar corner fillet

Add one exact non-orthogonal planar-support route without broadening the edge-selection or healing
surface. The input is an explicit finite straight edge frame: two unit support directions in the
cross-section plane, a strict interior wedge angle, two finite support extents, and one constant
rolling radius.

## Acceptance boundary

- one finite straight edge and two planar supports perpendicular to that edge;
- support directions define one strict interior angle `0 < θ < π`;
- exact circular cross-section tangent points use `t = r cot(θ/2)`;
- one extruded rational circular fillet profile, four retained support/outer faces, and two end
  caps sew to one closed genus-zero body with deterministic `V8/E12/F6/L6` topology;
- analytic volume is `Length * (sharp wedge area − removed oblique-corner area)`;
- outward normals, bounds, G1 tangent directions, and refusal transactions are verified;
- parallel/opposite supports, radius-consuming extents, non-planar frames, partial intervals,
  variable laws, arbitrary edge selection, apexes, freeform supports, and healing remain refused.

This is a distinct constant-radius oblique planar construction, not a rolling-ball G2 route.
