# Stage 4y — bounded eligible-edge dispatch for a nonlinear support-setback corner

Extend the rectangular-box edge-selection layer to the verified Stage 37e nonlinear support-setback
route. This deliberately keeps the rolling radius constant while the support clearance varies by a
symmetric genuinely nonlinear quadratic law.

## Accepted construction

`ClassifyNonlinearVariableSetbackCornerEdge(body, edge, radiusLaw, setbackLaw)` accepts only:

- a closed one-hull genus-zero rectangular box with exact `V8/E12/C24/L6/F6` topology;
- one selected straight manifold edge with two single-loop planar rectangular adjacent faces,
  strict orthogonality, and equal positive support widths;
- a finite positive constant radius law;
- a finite positive genuinely nonlinear quadratic setback law with equal endpoint setbacks;
- every sampled radius-plus-setback extent strictly inside the measured source width.

The returned `NonlinearVariableSetbackCornerSpecification` retains the selected origin, normalized
edge axis, length, constant radius law, and nonlinear setback law. Reconstruction remains a separate
transaction through `ReconstructNonlinearVariableSetbackCornerBlend`.

## Refusal boundary

Refuse non-box topology, out-of-range/non-manifold/curved/closed edges, non-planar/non-rectangular
supports, non-orthogonal or unequal-width corners, variable/nonlinear radius laws, linear/zero/
negative/non-finite/asymmetric-endpoint setback laws, consuming extents, partial edges, loops, apexes,
freeform supports, healing, and source-body replacement.

## Acceptance checks

The distinct verifier must cover exact edge/frame/length/law extraction, constant-radius and nonlinear-
setback identities, deterministic dispatch, separate `V10/E15/C30/L7/F7` reconstruction, integrated
quadratic volume, outward normals, source immutability, refusal boundaries, and a distinct
sharp-versus-nonlinear-setback proof.
