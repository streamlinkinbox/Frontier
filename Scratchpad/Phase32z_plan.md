# Phase 32z — asymmetric and partial endpoint/corner support plan

## Scope

This batch extends the bounded rectangular blend routes without opening general blend intersections. It covers:

1. asymmetric endpoint supports for one verified radial chain;
2. non-radial endpoint pairs with explicit support-plane classification;
3. partial interacting corner families with a finite support network;
4. unequal or non-orthogonal corner networks only when their intersection topology is fully classified.

The batch must refuse unsupported mixed networks transactionally rather than falling back to the existing symmetric builders.

## Sequence

- [ ] Add failure-oriented source fixtures first: asymmetric cap radii, endpoint planes with mismatched normals, missing support faces, unequal corner radii, and a partial network whose unselected continuation would intersect.
- [ ] Extract endpoint support descriptors from topology rather than assuming equal-radius or radial pairing.
- [x] Add an explicit finite-support feasibility classifier: support continuity, endpoint containment, positive clearances, and no unclassified blend/blend intersection.
- [x] Build the smallest accepted asymmetric case with exact rational support curves and analytic volume (bounded tapered/ruled route).
- [ ] Add partial-network construction only for a topology with a closed, manifold result; refuse open or ambiguous networks.
- [ ] Add non-orthogonal support handling after the orthogonal asymmetric case has independent proofs.
- [ ] Register focused verification binaries and deterministic proof renders.
- [ ] Update `CAD_ROADMAP.md`, `BLEND_LIMITS.md`, and the ParametricSketcher README only after the verifier is passing.

## Acceptance gates

- Every refusal leaves the source body unchanged.
- Endpoint support pairing is invariant under edge/profile ordering and rigid transforms.
- Accepted results have closed/manifold topology and exact support counts.
- Analytic volume and measured volume pass the centralized tolerance policy.
- No claim of general non-box blend/blend intersection support is made.

## Explicit non-goals

Oblique cavity sets, mixed-axis cavity sets, thin-wall solids, arbitrary unequal corner networks, and unrestricted G2/variable-radius blends remain outside this batch.
