# Stage 4v — bounded eligible-edge dispatch for a variable-radius corner

Add the smallest application-selection layer over the verified Stage 37a variable-radius corner
reconstruction. The source remains one canonical closed rectangular box and one explicit straight
edge; the classifier returns the existing specification without mutating the source.

## Accepted construction

`ClassifyVariableRadiusCornerEdge(body, edge, radiusLaw)` accepts only:

- a closed one-hull genus-zero rectangular box with exact `V8/E12/C24/L6/F6` topology;
- six single-loop planar rectangular faces, straight non-closed manifold edges, and a selected edge
  whose adjacent faces meet at a strict right angle;
- equal positive support widths measured from the selected edge, so the existing common-width Stage
  37a route is selected without silently entering an unequal-support route;
- one finite positive linear radius law whose endpoint radii leave both planar supports available.

The returned `VariableRadiusCornerSpecification` retains the selected edge origin, normalized axis,
length, common support width, and exact law. Reconstruction remains a separate transaction through
`ReconstructVariableRadiusCornerBlend`.

## Refusal boundary

Refuse non-box topology, out-of-range/non-manifold/curved/closed edges, non-planar or non-rectangular
supports, non-orthogonal or unequal-width corners, zero/negative/non-finite/consuming laws, partial
edges, loops, apexes, freeform supports, healing, and source-body replacement.

## Acceptance checks

The distinct verifier must cover exact edge/frame/width/law extraction, deterministic dispatch,
separate `V10/E15/C30/L7/F7` reconstruction, outward normals, source immutability, refusal boundaries,
and one distinct sharp-versus-variable-radius proof using a canonical rectangular box.
