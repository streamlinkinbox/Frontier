# Stage 3a plan — bounded quadratic nonlinear radius corner fillet

Stage 37a accepted a linear law and Stage 37b added a separate linear support-setback law. This
slice adds the smallest genuinely nonlinear law without pretending that G2 support continuity
has been solved.

## Accepted construction

The radius is a quadratic interpolant through three measured values:

- `r(0) = r0`;
- `r(0.5) = rm`;
- `r(1) = r1`.

A positive constant support setback `s` gives the outer station extent `d(t) = r(t) + s`.
The five boundary surfaces are quadratic lofts through the three station sections `t = 0,
0.5, 1`; the corner section at each station is an exact quarter-circle. This is a quadratic
surface construction, not the linear ruled/frustum route reused under a new name.

## Acceptance checks

The dedicated verifier establishes:

1. the middle value is reached at the middle station and differs from the endpoint interpolation;
2. first and second radius derivatives are explicit and finite;
3. circumferential and meridional curvature bounds accept the sampled surface and reject bounds
   below the measured values;
4. the sewn body is a positive-volume one-hull `V10/E15/C30/L7/F7` solid with outward normals;
5. the analytic volume matches the integrated quadratic outer extent minus the quarter-circle
   removal;
6. a linear descriptor, non-positive laws, degenerate frames, and zero setback refuse;
7. a comparison render distinguishes the nonlinear bulged radius from the linear fixture.

## Explicit boundary

This stage accepts one finite straight edge, two perpendicular support directions, one quadratic
radius law, and one common positive setback. It does not claim G2 continuity at the circular
rolling/support join, because that curvature transition still needs a dedicated construction and
measurement. Partial edges, unequal support setbacks, apexes, freeform supports, nonlinear
setback laws, general edge selection, and arbitrary intersection/trim/sew healing remain outside
this slice.

## Deliverables

- `QuadraticRadiusLaw` and `QuadraticVariableRadiusSurface` descriptors;
- derivative/curvature acceptance helpers;
- `ReconstructNonlinearVariableRadiusCornerBlend` and its 24-check verifier;
- focused-gate and CMake registration;
- `Phase37c_NonlinearVariableRadiusCorner.png` proof;
- roadmap, README, and audit updates with the G2 boundary stated explicitly.
