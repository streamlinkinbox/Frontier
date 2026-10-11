# Stage 4o — bounded eligible-edge dispatch for variable-radius G2 reconstruction

Add one narrow application layer over the verified Stage 4k variable-radius G2 route. Accept one
explicit edge from an existing B-rep only when the Stage 4m orthogonal rectangular eligibility
conditions hold, then attach one genuinely nonlinear positive quadratic radius law. The classifier
returns the explicit variable specification and leaves the source B-rep untouched.

## Accepted construction

`ClassifyVariableG2RollingBallEdge(body, edge, radiusLaw, transitionAngle)` accepts:

- one in-range straight manifold edge with exactly two adjacent planar rectangular faces;
- a strict orthogonal interior corner and equal positive support widths;
- a finite positive, genuinely nonlinear `QuadraticRadiusLaw` whose stations remain feasible;
- a finite transition angle accepted by the Stage 4k variable G2 route;
- deterministic support orientation inherited from the bounded orthogonal dispatch.

The resulting `VariableG2RollingBallPlanarCornerSpecification` is passed to the existing validated
variable-radius reconstruction route for feasibility checking, but the source body is not modified.
The caller performs reconstruction as a separate transactional operation.

## Refusal boundary

Refuse constant/invalid/non-positive laws, curved or non-rectangular supports, oblique and unequal-
width selections, non-manifold edges, invalid transitions, and laws whose stations consume the
finite supports. Do not add variable oblique dispatch, edge loops, healing, source-body replacement,
freeform supports, or arbitrary B-rep variable filleting.

## Acceptance checks

The distinct verifier must cover valid law/frame extraction, exact station radii, separate variable
G2 reconstruction, `V14/E21/F9/L9` topology, outward normals, source immutability, deterministic
orientation, and refusals for constant/invalid/consuming laws, oblique/curved/non-manifold/unequal-
width selections, and invalid transitions.
