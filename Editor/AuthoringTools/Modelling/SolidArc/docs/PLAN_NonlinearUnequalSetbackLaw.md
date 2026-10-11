# Stage 3e — bounded nonlinear unequal support-setback corner blend

The remaining support-setback gap is narrower than general variable-radius application: two
perpendicular planar supports receive independent genuinely nonlinear quadratic clearances. This
slice does not attempt rolling-ball G2; a finite circular rolling section has nonzero transverse
curvature and cannot be G2 to a plane. It also does not broaden input selection, curved supports,
or healing.

## Acceptance boundary

- one finite straight edge and two perpendicular planar supports;
- one positive quadratic radius law and two positive, genuinely nonlinear quadratic setback laws;
- the two setback laws must be unequal, so this route cannot duplicate the common-setback route;
- three exact station sections at `T = 0, 0.5, 1` drive five quadratic lofts;
- exact quarter-circle station arcs, product-of-extents volume, capped `V10/E15/C30/L7/F7`
  topology, outward normals, and transactional refusals;
- zero/negative laws, linear setback laws, equal setbacks, degenerate frames, and zero-length
  edges refuse without returning approximate geometry.

The construction is a bounded explicit descriptor, not arbitrary B-rep edge selection, a rolling-ball
G2 solution, a freeform support route, or general intersection/trim/sew healing.
