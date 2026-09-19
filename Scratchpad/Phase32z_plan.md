# Phase 32z — asymmetric and partial endpoint/corner support plan

## Scope

This batch extends the bounded rectangular blend routes without opening general blend intersections. It covers:

1. asymmetric endpoint supports for one verified radial chain;
2. non-radial endpoint pairs with explicit support-plane classification;
3. partial interacting corner families with a finite support network;
4. unequal or non-orthogonal corner networks only when their intersection topology is fully classified.

The batch must refuse unsupported mixed networks transactionally rather than falling back to the existing symmetric builders.

## Sequence

- [x] Add failure-oriented source fixtures first: asymmetric cap radii, endpoint planes with mismatched normals, missing support faces, unequal corner radii, and a partial network whose unselected continuation would intersect.
      — six classified fixtures in `AsymmetricEndpointVerification`: folding span, oblique support plane, support leaving the axis, missing ligament, equal-radius neighbour, vanishing radius. Each asserts the classifier's own reason, that the builder reports that same literal, and that the refusal leaves the support untouched.
- [ ] Extract endpoint support descriptors from topology rather than assuming equal-radius or radial pairing.
- [x] Add an explicit finite-support feasibility classifier: support continuity, endpoint containment, positive clearances, and no unclassified blend/blend intersection.
- [x] Build the smallest accepted asymmetric case with exact rational support curves and analytic volume (bounded tapered/ruled route).
- [x] Add partial-network construction only for a topology with a closed, manifold result; refuse open or ambiguous networks.
      — `ReconstructAsymmetricChain` sews consecutive conical spans along one monotone axis (`V3/E5/C10/L4/F4` for three supports, `V4/E7/C14/L5/F5` for four, genus zero, one hull, analytic swept volume). Monotone support positions plus per-span convexity is the closed-manifold proof, so the classification is the guarantee.
- [ ] Add non-orthogonal support handling after the orthogonal asymmetric case has independent proofs.
      — the orthogonal case now has independent proofs (50 checks, rigid-transform and span-order invariance). Non-orthogonal supports are *classified and refused* ("oblique to the chain axis"), never approximated. Acceptance needs an oblique-rim route (an oblique rim is not a circle, so it cannot be a conical span) and stays open.
- [x] Register focused verification binaries and deterministic proof renders.
      — `AsymmetricEndpointVerification` (50 checks, registered in CMakeLists) renders `Proofs/Phase32z_AsymmetricChain.png` (960 x 600, 0.89 MB) through the console contact-sheet writer.
- [x] Update `CAD_ROADMAP.md`, `BLEND_LIMITS.md`, and the ParametricSketcher README only after the verifier is passing.
      — all three updated after the 50-check verifier went green, including the measured volume-integrator band and the boundary-slack policy.

## Acceptance gates

- Every refusal leaves the source body unchanged.
- Endpoint support pairing is invariant under edge/profile ordering and rigid transforms.
- Accepted results have closed/manifold topology and exact support counts.
- Analytic volume and measured volume pass the centralized tolerance policy — extended with a tightened measured band (`MeasuredVolumeBand`, 2e-4 relative) measured at `VerificationChord`/`VerificationCap`; the shipped gate keeps a measured 2.6x margin at the default tessellation.
- No claim of general non-box blend/blend intersection support is made. Oblique supports, unequal corner networks, and blend/blend interaction are named as open.

## Explicit non-goals

Oblique cavity sets, mixed-axis cavity sets, thin-wall solids, arbitrary unequal corner networks, and unrestricted G2/variable-radius blends remain outside this batch.
