# Stage 4h — bounded oblique quadratic full-edge fillet

Extend the completed oblique quadratic partial-edge route to one complete finite straight edge.
The corner is rounded over the entire edge by one genuinely nonlinear positive quadratic radius
law; no sharp continuation or interval transition is introduced.

## Acceptance boundary

- one explicit finite straight edge with two planar support directions perpendicular to it;
- strict wedge angle `0 < theta < pi`;
- one genuinely nonlinear positive quadratic radius law over the complete edge;
- every tangent distance is exactly `r(t) cot(theta/2)` and remains below both finite support widths;
- rational quadratic lofts through start, middle, and end circular stations;
- exact retained support/outer surfaces, variable rational fillet band, and explicit finite end caps;
- analytic volume from integrating the oblique removed-corner area, proportional to the integrated
  quadratic radius square;
- closed genus-zero topology, outward-normal and station-fit checks, and transactional refusals;
- partial intervals, arbitrary variable-radius laws, arbitrary edge selection, non-planar/freeform
  supports, apexes, rolling-ball G2, and healing remain unsupported.

The plan is committed separately from production implementation. The route must not be integrated
until the exact surface construction passes the analytic volume gate without healing or acceptance
bypasses.
