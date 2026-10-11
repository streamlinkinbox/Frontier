# SolidArc Phase 36s — bounded partial cone–cylinder root fillet

## Scope

Add the next narrow constant-radius root blend for one open coaxial sector joining a conical frustum to a cylindrical
boss:

- positive lower-cone base/root radii and a positive upper-cylinder height;
- one non-reflex open sector, including its half-turn boundary case;
- a lower cone whose radius widens toward the shared constant-radius cylinder;
- one exact circular meridian roll tangent to the cone generator and cylinder;
- retained lower cone, toroidal roll, upper cylinder, planar end caps, and radial endpoint caps.

The route is intentionally separate from the complete cylinder–cone fillet and the already-supported partial
cone–cylinder chamfer. It accepts only the measured two-member coaxial root chain.

## Acceptance checks

- Classify exactly two open rational root members shared by a native cone and cylinder, measure the common axis, positive
  base/root radii, cone/cylinder heights, radial frame, and sector sweep.
- Require a cone that widens toward the cylinder (`base radius < root radius`), positive radius, non-reflex sector, and
  a non-consuming tangent circle; refuse zero/negative or consuming rolls, narrowing/equal-slope/zero-radius/apex cases,
  complete sectors, arbitrary selections, non-coaxial supports, and unsupported topology transactionally.
- Construct the exact tangent circular meridian, revolve the retained cone, toroidal roll, and cylinder over the measured
  sector, preserve planar caps, heal only the two radial endpoint caps for a general sector, and sew/orient the result.
- Enforce genus-zero topology `V10/E15/C30/L7/F7` for a general sector and `V10/E14/C28/L6/F6` for a half-turn, with one
  solid and no open, non-manifold, or misoriented edges.
- Compare volume to the sector fraction of the analytic cone–cylinder source volume minus the meridian first-moment roll
  removal; verify source immutability, console dispatch, and an exterior-readable proof render.

## Explicit non-goals

Do not support complete sectors through this route, narrowing or equal-slope cones, apex/zero-radius supports,
non-coaxial/oblique roots, reflex/arbitrary/branched/incomplete chains, variable-radius/nonlinear/G2 blends, partial-edge
selection, curved corner patches, or general intersection/trim/sew healing.

## Verification and documentation

Add `PartialConeCylinderFilletVerification.cpp`, CMake/CTest and official-gate registration, README/roadmap/limits entries,
and `Proofs/Phase36s_PartialConeCylinderFillet.png`. Run the dedicated verifier and complete `CheckSolidArc.sh` gate, inspect
the exterior proof image, then commit and push the bounded slice.
