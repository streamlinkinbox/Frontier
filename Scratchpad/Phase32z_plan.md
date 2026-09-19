# Phase 32z — asymmetric (unequal-radius) support plan

## Scope

This batch opens the first unequal-radius support pair without opening general blend intersections. The upstream
scaffold (specification-level validators, linear radius law, ruled surface, tapered-frustum reconstruction) never
reached a B-rep operation; the delivered increment is the bounded plane–cone boss-root fillet built on it.

## Sequence

- [x] Add failure-oriented source fixtures first: undercut flare, steep 45°, α = 0, oblique/reversed axes, apex cone,
      conical top rim, outer shoulder rim, Boolean-built source, both feasibility boundaries.
- [x] Extract endpoint support descriptors from topology rather than assuming equal-radius or radial pairing
      (`PlaneConeBossRoot`: measured shoulder plane, measured cone end rows and generators, tag agreement, seam-tolerant
      outer rim, cap rim radius).
- [x] Add an explicit finite-support feasibility classifier (`r > 0`, `z_t < H`, `ρ_c < R_outer`; `ρ_t > 0` defensive).
- [x] Build the smallest accepted asymmetric case with exact rational support curves and analytic volume
      (`FilletPlaneConeBossRoot`: `π/2 − α` rational torus band, Pappus wedge, self-refusal on volume disagreement).
- [x] Register the focused verification binary and deterministic proof render
      (`PlaneConeFilletVerification`, 61 checks; `Proofs/Phase32z_PlaneConeFillet.png`).
- [x] Update `CAPABILITY_ROADMAP.md`, `BLEND_LIMITS.md`, and the ParametricSketcher README after the verifier passed.
- [ ] Partial endpoint chains, non-radial endpoint pairs, unequal/non-orthogonal corner networks — deferred; the
      specification modes stay explicitly refused (`PartialEndpointChain`, `VariableRadiusRoll` reconstruction).

## Acceptance gates (all met)

- Every refusal leaves the source body unchanged (verified).
- Support classification is invariant under axis direction, non-unit axes, and rigid transforms (verified).
- Accepted results have closed genus-zero `V5/E9/C18/L6/F6` topology and exact support extents (verified).
- Analytic and measured volume pass the centralised tolerance policy; the differential wedge check is declared at
  `5e-3` of the wedge against a measured `≤ 1.5e-3` (recorded in `BLEND_LIMITS.md`).
- No claim of cone–cone, cone–cylinder, apex-cone, or general non-box blend/blend intersection support.

## Findings fixed on the way

- Upstream HEAD did not compile (`Deliver<T>` passed to `Expect(bool)`, `Within` with four arguments);
  `AsymmetricEndpointVerification` was folded into the new suite.
- `VariableRadiusSurface::TangentAlong` dropped the `B·sin θ` component of the generator, so it and the derived normal
  were only right at θ ∈ {0, π}; the verifier now checks the tangent against a sampled derivative at six angles.
- `FilletEdge`/`ChamferEdge` returned `Why.c_str()` of a dying local `std::string` inside a `Refusal`
  (documented "static text only"); the corner-frame refusal text is now a static literal end to end.
- `ScalarCriteria.h` banner described three tolerance bands for thirteen constants; `VolumeTolerance` was labelled
  `[m³]` while applied as a relative gate.

## Explicit non-goals

Oblique cavity sets, mixed-axis cavity sets, thin-wall solids, arbitrary unequal corner networks, and unrestricted
G2/variable-radius blends remain outside this batch.
