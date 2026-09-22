# Stage 4f — bounded oblique partial-edge fillet

Extend the explicit oblique planar corner route to one strict interior interval. This is a
separate bounded construction: one constant-radius circular fillet occupies `Start < t < End`
along one finite straight edge, while the same oblique planar corner remains sharp before and
after the interval.

## Acceptance boundary

- one explicit straight edge, two planar support directions, and one strict wedge angle
  `0 < theta < pi`;
- one positive constant radius with tangent distance `r cot(theta/2)` below both support widths;
- strict interval `0 < Start < End < Length`;
- exact retained oblique support/outer surfaces, two sharp-corner continuation walls, one rational
  oblique fillet band, two planar transition caps, and finite end caps;
- analytic volume `Length * sharp wedge area − (End − Start) * removed oblique-corner area`;
- closed genus-zero topology, normals, tangent-fit checks, and transactional refusals;
- variable radius, arbitrary edge selection, non-planar/freeform supports, apexes, and healing
  remain refused.
