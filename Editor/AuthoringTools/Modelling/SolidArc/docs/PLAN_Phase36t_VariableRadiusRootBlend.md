# Phase 36t plan — bounded linear variable-radius root blend

## Goal

Extend the existing Phase 36e/Phase 33 linear radius-law foundation into one explicitly bounded
root-blend route without pretending to solve generic variable-radius fillets.

## Accepted scope

- Two complete, coaxial circular supports on parallel planes.
- One straight axial spine and a positive linear radius law `r(t) = r0 + (r1-r0)t`.
- Endpoint radii must match the measured support radii exactly.
- The reconstructed result must remain a single positive-volume genus-zero solid with native
  frustum topology and analytic swept volume.
- The visible proof must show the accepted result fully filled from an exterior-facing camera.

## Explicit refusals

- Zero or negative radius, apex/point endpoints, consuming clearance, non-axial centres, or
  non-parallel support normals.
- Partial circular supports, partial edges, open endpoint chains, nonlinear laws, variable
  setback laws, G2 requirements, and arbitrary/non-coaxial supports.
- This slice will not silently approximate a rolling-ball fillet. The accepted surface is the
  exact linear-law ruled/frustum route already represented by `VariableRadiusSurface` and
  `ReconstructVariableRadiusRuledSolid`.

## Implementation steps

1. Add a named production entry point for the bounded variable-radius root route, reusing the
   existing structural endpoint validation and exact frustum reconstruction.
2. Preserve transactional refusal semantics and source immutability.
3. Add Phase 36t verification for endpoint matching, positive law, curvature bound, topology,
   analytic volume, unsupported modes, console dispatch, and the exterior proof render.
4. Update the SolidArc capability roadmap, README, CMake target, and full gate.

## Exit criteria

- Dedicated Phase 36t verifier passes all checks and writes
  `Proofs/Phase36t_VariableRadiusRootBlend.png`.
- Full `Tools/Build/CheckSolidArc.sh` passes with zero failures.
- The proof contains no unintended back-face tint or partially filled visible faces.
