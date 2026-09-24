# Stage 4w — bounded eligible-edge dispatch for a variable support-setback corner

Extend the Stage 4v rectangular-box edge-selection layer to the verified Stage 37b reconstruction.
The source remains one explicit straight edge of a canonical rectangular box; the radius and support
setback laws are supplied independently and returned without mutating the source.

## Accepted construction

`ClassifyVariableSetbackCornerEdge(body, edge, radiusLaw, setbackLaw)` accepts only:

- a closed one-hull genus-zero rectangular box with exact `V8/E12/C24/L6/F6` topology;
- one selected straight manifold edge with two single-loop planar rectangular adjacent faces,
  strict orthogonality, and equal positive support widths;
- finite positive linear radius and setback laws whose endpoint extents
  `radius(t) + setback(t)` remain strictly inside the measured source support width.

The returned `VariableSetbackCornerSpecification` retains the selected origin, normalized edge axis,
length, independent laws, and the exact source-frame eligibility. Reconstruction remains a separate
transaction through `ReconstructVariableSetbackCornerBlend`.

## Refusal boundary

Refuse non-box topology, out-of-range/non-manifold/curved/closed edges, non-planar/non-rectangular
supports, non-orthogonal or unequal-width corners, invalid/zero/negative/non-finite laws, consuming
support extents, partial edges, loops, apexes, unequal/mixed supports, nonlinear laws, freeform
supports, healing, and source-body replacement.

## Acceptance checks

The distinct verifier must cover exact edge/frame/length/law extraction, deterministic dispatch,
independent radius/setback station identities, separate `V10/E15/C30/L7/F7` reconstruction, outward
normals, source immutability, refusal boundaries, and a distinct sharp-versus-variable-setback proof.
