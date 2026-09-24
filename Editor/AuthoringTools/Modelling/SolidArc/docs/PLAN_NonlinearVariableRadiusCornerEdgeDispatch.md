# Stage 4x — bounded eligible-edge dispatch for a nonlinear variable-radius corner

Extend the Stage 4v rectangular-box edge-selection layer to the verified Stage 37c nonlinear
radius route. The source remains one explicit straight edge of a canonical rectangular box; the
quadratic law and common setback are supplied independently and returned without mutating the source.

## Accepted construction

`ClassifyNonlinearVariableRadiusCornerEdge(body, edge, setback, radiusLaw)` accepts only:

- a closed one-hull genus-zero rectangular box with exact `V8/E12/C24/L6/F6` topology;
- one selected straight manifold edge with two single-loop planar rectangular adjacent faces,
  strict orthogonality, and equal positive support widths;
- a finite positive, genuinely nonlinear quadratic radius law with equal endpoint radii and finite positive common setback;
- every sampled radius plus setback strictly inside the measured support width.

The returned `NonlinearVariableRadiusCornerSpecification` retains the selected origin, normalized
edge axis, length, setback, and quadratic law. Reconstruction remains a separate transaction through
`ReconstructNonlinearVariableRadiusCornerBlend`.

## Refusal boundary

Refuse non-box topology, out-of-range/non-manifold/curved/closed edges, non-planar/non-rectangular
supports, non-orthogonal or unequal-width corners, linear/asymmetric-endpoint/zero/negative/non-finite
laws, consuming radius-plus-setback requests, partial edges, loops, apexes, freeform supports, healing,
and source-body replacement.

## Acceptance checks

The distinct verifier must cover exact edge/frame/length/law extraction, deterministic dispatch,
proof that the law is genuinely nonlinear, separate `V10/E15/C30/L7/F7` reconstruction, outward
normals, source immutability, refusal boundaries, and a distinct sharp-versus-nonlinear-radius proof.
