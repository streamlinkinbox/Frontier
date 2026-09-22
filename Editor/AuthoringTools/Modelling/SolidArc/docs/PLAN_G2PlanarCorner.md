# Stage 3b plan — bounded G2 planar corner transition

Stage 3a adds a nonlinear-radius rolling corner, but its circular rolling/support junction is
not G2: a plane has zero curvature while a circle has non-zero curvature. This slice therefore
uses a separate, explicitly non-rolling profile family to prove the G2 construction boundary.

## Accepted construction

A quintic Bezier profile runs from `(0, r)` to `(r, 0)` with:

- support-aligned endpoint tangents;
- zero second derivative, hence zero profile curvature, at both endpoints;
- a handle fraction `h/r` strictly between zero and one half;
- a constant cross-section along one finite straight edge.

The profile is extruded along the edge axis and sewn to four straight support surfaces and two
planar end caps. At the support joins, both the tangent and normal curvature match the planar
support. At the end caps, the same zero profile curvature avoids an artificial curvature break.

This is a G2 corner transition, **not** a quarter-circle rolling-ball fillet. The proof compares
it visually with the existing circular rolling route so that the distinction is explicit.

## Acceptance checks

The verifier establishes:

1. a valid quintic profile with support-aligned tangents;
2. zero endpoint curvature and positive interior curvature;
3. one-hull `V10/E15/C30/L7/F7` topology with outward normals;
4. volume equal to the exact quintic Green's-theorem profile-area identity;
5. refusal of zero/half-radius handles, consuming radius, degenerate edge, and invalid frame;
6. a distinct G2-versus-rolling visible proof.

## Explicit boundary

This stage covers only a complete finite straight corner between perpendicular planar supports.
It does not claim a rolling-ball solution, variable radius, nonlinear setback, partial edges,
apexes, freeform supports, or general intersection/trim/sew healing. The existing rolling routes
remain the correct bounded APIs for rolling-ball geometry.
