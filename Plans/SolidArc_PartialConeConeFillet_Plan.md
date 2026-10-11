# SolidArc Phase 36r — bounded partial cone–cone root fillet

## Scope

Add one narrow constant-radius fillet route for an open coaxial sector bounded by two conical frusta:

- one positive-radius lower cone, one positive-radius upper cone, and a shared circular root;
- a non-reflex open sector, including the supported half-turn boundary case;
- an increasing narrowing slope from the lower support into the upper support;
- one exact constant-radius circular meridian roll tangent to both cone generators;
- retained lower cone, toroidal roll, retained upper cone, planar axial caps, and radial endpoint caps;
- exact sector topology and the analytic cone–cone meridian first-moment volume target.

The implementation is deliberately bounded to one coaxial two-cone chain. It does not generalize the existing complete
cone–cone route to arbitrary selections or arbitrary sector geometry.

## Acceptance checks

- Classify exactly two connected coaxial conical frusta, measure positive base/root/top radii, common axis, sector sweep,
  root frame, and the two support slopes.
- Accept only an increasing narrowing configuration with a positive roll radius and a valid non-reflex open sector;
  refuse zero or negative radii, consuming rolls, flaring/equal-slope supports, apex roots, unsupported sectors, and
  arbitrary selections transactionally.
- Construct the tangent circle in the cone meridian, revolve it over the requested sector, preserve the two cone patches,
  planar caps, and radial endpoint caps, then sew and orient the result without silently healing arbitrary intersections.
- Enforce genus-zero `V10/E15/C30/L7/F7` for a general sector and `V10/E14/C28/L6/F6` for the half-turn, with one solid,
  no open or non-manifold edges, and positive volume.
- Compare volume against the angular-fraction analytic cone–cone meridian first-moment target; verify source immutability,
  console dispatch, and an exterior-readable proof render without unintended backfaces.

## Explicit non-goals

Do not support complete/apex-only fixtures through this partial route, zero-radius or negative-radius cones, flaring or
same-slope pairs, consuming fillets, reflex or arbitrary sectors, non-coaxial/oblique cones, branched or incomplete roots,
variable-radius/nonlinear/G2 blends, partial-edge selection, curved corner patches, or general intersection/trim/sew healing.

## Verification and documentation

Add `PartialConeConeFilletVerification.cpp`, CMake/CTest and build-gate registration, README/roadmap/limits entries, and
`Proofs/Phase36r_PartialConeConeFillet.png`. Run the dedicated verifier and the complete `CheckSolidArc.sh` gate, inspect
the exterior proof image, then commit and push the bounded slice.
