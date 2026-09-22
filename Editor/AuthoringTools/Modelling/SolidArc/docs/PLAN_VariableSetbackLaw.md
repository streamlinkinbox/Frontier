# Stage 2 plan — bounded variable support-setback corner fillet

Stage 1 proved a finite straight planar corner whose quarter-circle rolling section varies with
one positive linear radius law. This slice adds a distinct, independently measured support
clearance law without widening the accepted geometry class.

## Accepted construction

For edge station `t ∈ [0, 1]`:

- `r(t) = r0 + (r1 - r0)t` is the positive linear rolling radius;
- `s(t) = s0 + (s1 - s0)t` is the positive linear support setback;
- each perpendicular support extent is `d(t) = r(t) + s(t)`;
- the meridian is the exact quarter-circle from `(0, r(t))` to `(r(t), 0)`;
- four straight support boundaries and the quarter-circle boundary are lofted between the two
  end sections, then capped by the two planar end sections.

The setback is not a second radius approximation: it is measured as the distance from the
rolling tangent point to the far boundary of each support. The accepted result is a one-hull,
genus-zero `V10/E15/C30/L7/F7` body.

## Acceptance checks

The verifier must establish:

1. both laws are finite, positive, and independently non-constant in the proof fixture;
2. at multiple stations, `d(t) - r(t) = s(t)` to numerical tolerance;
3. each tessellated face has an outward normal;
4. volume agrees with the integrated identity
   `L(d0² + d0d1 + d1²)/3 - (1 - π/4)L(r0² + r0r1 + r1²)/3`;
5. zero/negative radius, zero/negative setback, degenerate frame, and zero-length requests
   refuse before a result is published;
6. a constant-setback comparison renders beside the variable-setback result as a distinct
   visible proof.

## Explicit boundary

This is still one complete finite straight corner with perpendicular support directions. The
law descriptors are linear by construction. Nonlinear radius or setback laws, unequal support
setbacks, partial-edge selections, apexes, G2 continuity, curved/freeform supports, and general
intersection/trim/sew healing remain refused or unimplemented. No console selection dispatch is
added by this slice.

## Deliverables

- `VariableSetbackCornerSpecification` and transactional reconstruction API;
- `VariableSetbackCornerFilletVerification` with analytic/topological/refusal checks;
- CMake and focused-gate registration;
- `Phase37b_VariableSetbackCornerFillet.png` and this plan;
- roadmap, README, and proof-audit updates that do not overclaim the route.
