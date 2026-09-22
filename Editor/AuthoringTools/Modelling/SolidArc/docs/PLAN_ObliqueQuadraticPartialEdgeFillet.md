# Stage 4g — bounded oblique quadratic partial-edge fillet

Extend the completed Stage 4f oblique partial-edge construction by one deliberately bounded
nonlinear radius law. One explicit finite straight edge keeps the same two planar support
directions and strict wedge angle, while the circular fillet radius varies quadratically only over
one strict interior interval.

## Acceptance boundary

- one explicit finite straight edge with two planar support directions perpendicular to it;
- strict wedge angle `0 < theta < pi`;
- one genuinely nonlinear positive quadratic radius law sampled over `Start < t < End`;
- every tangent distance is the exact station value `r(t) cot(theta/2)` and remains below both
  finite support widths;
- sharp corner retained before and after the interval, with rational quadratic lofts through the
  start, middle, and end circular stations;
- exact retained support/outer surfaces, sharp continuation walls, rational variable fillet band,
  transition caps, and finite end caps;
- analytic volume from the sharp wedge volume minus the integral of the oblique removed-corner area,
  which is proportional to the integrated quadratic radius square;
- closed genus-zero topology, finite outward/tangent-fit checks, and transactional refusals;
- arbitrary variable-radius laws, arbitrary edge selection, non-planar/freeform supports, apexes,
  rolling-ball G2, and healing remain unsupported.
