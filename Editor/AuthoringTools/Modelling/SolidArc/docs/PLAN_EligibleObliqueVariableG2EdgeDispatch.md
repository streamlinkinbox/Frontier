# Stage 4p — bounded eligible-edge dispatch for oblique variable-radius G2 reconstruction

Complete the bounded variable-radius application dispatch by combining the Stage 4n oblique
rectangular-edge frame with the verified Stage 4l oblique variable-radius G2 route. This accepts
one explicit edge, one genuinely nonlinear positive quadratic radius law, and no arbitrary B-rep
fillet semantics.

## Accepted construction

`ClassifyObliqueVariableG2RollingBallEdge(body, edge, radiusLaw, transitionAngle)` accepts:

- one in-range straight manifold edge shared by exactly two planar rectangular faces;
- a strict non-orthogonal, non-reflex interior corner;
- independently extracted finite positive support widths, including unequal widths;
- a finite positive, genuinely nonlinear `QuadraticRadiusLaw` whose stations remain feasible;
- a finite transition angle accepted by the Stage 4l oblique variable G2 route;
- deterministic support orientation.

The returned `ObliqueVariableG2RollingBallPlanarCornerSpecification` is feasibility-checked through
the existing transactional reconstruction route, while the source B-rep remains untouched.

## Refusal boundary

Refuse orthogonal selections (covered by Stage 4o), constant/invalid/non-positive/consuming laws,
curved or non-rectangular supports, non-manifold edges, invalid transitions, degenerate/non-reflex
corners, and invalid source bodies. Do not add edge loops, healing, source-body replacement,
freeform supports, or arbitrary B-rep variable filleting.

## Acceptance checks

The distinct verifier must cover an explicit oblique edge, exact angle/length/width/radius-law
extraction, deterministic frame orientation, separate `V12/E18/F8/L8` reconstruction, outward
normals, source immutability, and transactional refusals for orthogonal, curved, non-manifold,
constant, non-positive, consuming-law, and invalid-transition selections.
