# SolidArc — parametric NURBS modelling tool (C++20, console + rasterised proofs)

Folder: `Editor/EditorTools/ParametricSketcher/`. Application name: **SolidArc**.

A standalone modelling tool that lives beside `Engine/` and `Projects/` and depends on neither. Every piece of
geometry is a NURBS curve or surface; every solid is a B-rep of trimmed NURBS faces; every visual is drawn by a
GPU-shaped renderer (software rasteriser here, Vulkan on a machine with a GPU). No UI toolkit: all payload goes to the
console, all visuals go to PNG proofs in `Proofs/`.

## Build & verify

```bash
cd ParametricSketcher
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure      # 69 suites (56 C++ verification binaries + 13 script smoke tests)
```

No external packages. `-Wall -Wextra -Wpedantic -Werror`.

## Native `.arc` documents (Phase 22)

`.arc` is now the native, versioned **construction-document** format: `save bracket.arc`, `save`, and
`open bracket.arc` preserve the parametric command journal rather than flattening the NURBS/B-rep into display
triangles. Opening is transactional (the current scene is not touched if replay refuses) and replacement saves retain a
`.arc.bak` recovery copy. Proof scripts in `Scripts/` remain runnable scripts; they are not native documents and are
intentionally refused by `open` unless they carry the v1 document header. See
[`docs/NATIVE_DOCUMENTS.md`](docs/NATIVE_DOCUMENTS.md) for the on-disk contract and its verification coverage. The
validated, incremental implementation plan is in [`docs/CAPABILITY_ROADMAP.md`](docs/CAPABILITY_ROADMAP.md).

## Adversarial planar NURBS safety (Phase 23)

`ProfileAdversarialVerification` adds 41 checks around profile contact semantics and free-form quality. Coincident curve
portions intentionally produce no fabricated *point* crossing; external tangencies report one tangent point but have a
zero-area intersection profile and remain two simple components in a union. The checks cover shared boundaries,
near-tangent crossings, closed interpolated and single-span cubic loops, and overlapping closed cubic Boolean results.

`offset` now refuses a self-crossing source, a sampled curvature cusp, or a self-crossing candidate result. Circular arcs
retain their exact radial offset. Rational quadratic spans are tested for circularity first, so ellipses no longer become
osculating circular arcs: they follow the measured free-form offset route instead. Run
`Scripts/Phase23_AdversarialProfiles.arc` to reproduce
[`Proofs/Phase23_AdversarialProfiles.png`](Proofs/Phase23_AdversarialProfiles.png), a four-tile visual proof for shared
boundaries/tangencies, rejected loop sources, safe offsets, and free-form Boolean union/common results.

## Boolean contact healing (Phase 24)

`BooleanContactVerification` adds a C++-only 31-check regression for coincident and zero-volume contact between
structurally verified axis-aligned box solids. Face-touching and rectangularly overlapping boxes rebuild as a single
clean box; point-/edge-touching or gapped boxes preserve separate, manifold B-rep hulls instead of welding a
non-manifold edge. Empty common and complete subtraction are explicit refusals. The C++ verification itself renders
[`Proofs/Phase24_BooleanContacts.png`](Proofs/Phase24_BooleanContacts.png); no HTML proof or browser component is used.

## Duplicate B-rep Boolean identity (Phase 24b)

`BooleanIdentityVerification` adds 26 C++ checks for valid B-reps that are exact representation-level copies:
union/common return one source solid and difference returns an explicit empty result without a fictitious SSI curve.
The comparison includes topology, NURBS surfaces/curves, and trim traces—no spatial fuzzy equality is used.
[`Proofs/Phase24b_BooleanIdentity.png`](Proofs/Phase24b_BooleanIdentity.png) is generated directly by that C++ test;
there is no HTML proof or browser implementation.

## Affine-NURBS Boolean equivalence (Phase 24c)

`BooleanEquivalenceVerification` adds 17 C++ checks for full coincident geometry built through separate paths: a native
cylinder and an extrusion of an equal circular profile. Matching is exact in homogeneous control points and topology;
only affine knot domains are normalized, so a 0.0001-radius change remains outside the identity gate.
[`Proofs/Phase24c_BooleanEquivalence.png`](Proofs/Phase24c_BooleanEquivalence.png) is C++-generated, with no HTML
proof or browser component.

## Seam-invariant cylinder Boolean identity (Phase 24d)

`BooleanCylinderSeamVerification` adds 18 C++ checks for full right cylinders whose periodic circular seams or
construction direction differ. It derives physical cylinder dimensions from the validated B-rep instead of treating a
seam relocation as a Boolean intersection. The classifier remains narrow: radius/height near misses, partial cylinders,
cones, and free-form periodic faces do not enter the identity path. The C++ suite renders
[`Proofs/Phase24d_BooleanCylinderSeams.png`](Proofs/Phase24d_BooleanCylinderSeams.png); no HTML proof is used.

## Exact circular-cap chamfers (Phase 25)

`BlendSolver::ChamferEdge` now recognizes a **complete, native right-cylinder cap edge** in addition to its existing
straight planar-edge route. Rather than trying to subtract a faceted wedge from a curved rim, it retains the cylinder
run and sews on an exact rational conical frustum: `Cylinder(R, H − s) + Cone(R, R − s, s)`. The input is structurally
verified as the native `V2/E3/C6/L3/F3` cylinder topology with one classified cylinder side, two planar caps, and a
rational quadratic circular selected cap edge. The result is a closed `V3/E5/F4` solid; `s` is exactly both its radial
and axial set-back.

`CylinderChamferVerification` has 19 C++ checks for both caps, a non-unit oblique construction axis, structural-scope
refusal of a circular extrusion, feasibility refusals at the cylinder axis/full height, exact sampled conic generatrices,
and console integration. It creates
[`Proofs/Phase25_CylinderChamfers.png`](Proofs/Phase25_CylinderChamfers.png) entirely from C++ console commands. This
is intentionally not a claim of arbitrary curved-edge chamfering: periodic circular extrusions, partial cylinders,
cones, and general NURBS edges retain the normal refusal path until their own constructions are verified.

## Exact circular-cap rolling-ball fillets (Phase 26)

The same verified native-cylinder cap route now supports `BlendSolver::FilletEdge`. It revolves an exact rational
quarter-circle meridian around the cylinder axis, preserves that patch as a partial torus with major radius `R − r` and
minor radius `r`, and sews it to the retained cylindrical wall and two planar caps. The resulting `V3/E5/F4` B-rep has
an exact constant-radius roll and G1 normals at both the cylindrical and planar joins—without passing a coincident
circular edge through the generic planar fillet cutter.

`CylinderFilletVerification` has 22 C++ checks for top/bottom, oblique-axis, and reversed-direction cylinders; exact
sampled torus implicit residuals; G1 endpoint normals; topology/validity; analytic-volume observation; feasibility
refusals; an explicit circular-extrusion refusal; and console integration. It writes
[`Proofs/Phase26_CylinderFillets.png`](Proofs/Phase26_CylinderFillets.png) directly from C++ console commands. This is
also deliberately bounded: it does not claim support for partial cylindrical rims, circular extrusions, plane–cylinder
junctions, cylinder–cylinder intersections, or arbitrary NURBS-support fillets.

## Exact circular-cap face push (Phase 27)

`BlendSolver::PushFace` now recognizes either planar cap face of the same structural native right cylinder. The old
planar push construction correctly avoids coincident Boolean walls for general faces but cannot extend a cap bounded by
a periodic cylindrical wall. The new direct route reconstructs `Cylinder(R, H + d)` exactly; pushing the lower cap also
moves the canonical base by `−axis·d`, while an upper-cap push leaves it fixed. Consequently positive distance always
moves the selected cap along its outward normal, and negative distance sinks it, without creating an SSI/contact case.

`CylinderPushVerification` has 19 C++ checks for outward/inward pushes on both caps, exact sampled cylinder geometry,
oblique and reversed construction direction, invalid side/over-collapse refusal, and console integration. Its C++
commands produce [`Proofs/Phase27_CylinderPushes.png`](Proofs/Phase27_CylinderPushes.png), comparing original and
pushed cylinder pairs. The route remains intentionally limited to the fully verified native-cylinder topology; it does
not claim radial side-face offsets, circular-extrusion caps, partial cylinders, or generic curved-face modification.

## Exact cylindrical-side face push (Phase 28)

`PushFace` also now recognizes the classified **cylindrical side** of that native topology. A positive push moves the
entire curved support outward and an inward push moves it toward the axis, rebuilding `Cylinder(R + d, H)` with unchanged
axis, end caps, and axial extent. This adds a true curved-face direct-modelling offset without approximating the surface,
constructing a Boolean shell, or changing the cap topology.

`CylinderSidePushVerification` has 15 C++ checks covering exact outward/inward radii, oblique and reversed construction,
radius-collapse/zero refusal, explicit circular-extrusion exclusion, and console integration. It produces
[`Proofs/Phase28_CylinderSidePushes.png`](Proofs/Phase28_CylinderSidePushes.png) directly in C++, including paired
source/result comparisons and a plan-view radius proof. This remains a native-cylinder-only direct route; arbitrary
curved faces, partial cylinders, and extrusion representations are not overclaimed.

## Exact conical-side face push (Phase 29)

`PushFace` recognizes a full native conical-frustum side and reconstructs its exact normal offset. For slope `s`, both
cap radii change by `d·sqrt(1+s²)` while the two cap planes, axis, and height remain fixed. The structural classifier
checks the rational circular rims, straight seam, planar caps, and sampled linear conical support before taking the
direct route. A radius collapse refuses instead of crossing the apex.

`ConeSidePushVerification` has 10 C++ checks for inward/outward offsets, sampled exact radii, an oblique axis, collapse
refusal, console integration, and a C++-generated
[`Proofs/Phase29_ConeSidePushes.png`](Proofs/Phase29_ConeSidePushes.png).

## Exact conical-cap face push (Phase 30)

Either planar cap of the same full native frustum can move along its true outward normal while remaining on the original
infinite conical support. The operation edits height, base, and the selected radius analytically. Native recognition is
construction-direction invariant: a negative-height cone is canonicalized from its geometric low ring to its high ring,
so cap and side pushes retain the same outward semantics. A continuation that consumes the height or crosses the apex
refuses; apex cones and arbitrary trimmed/free-form conical faces remain outside this bounded route.

`ConeCapPushVerification` has 19 C++ checks covering both caps inward/outward, exact base/axis/radii/height/caps/surface
samples/topology/volume, oblique and negative-height construction, expanding and tapering slopes, height/radius-collapse
refusals, apex refusal, exact console commit, and the C++-generated
[`Proofs/Phase30_ConeCapPushes.png`](Proofs/Phase30_ConeCapPushes.png).

## Exact plane–cylinder boss-root fillet (Phase 31)

The first bounded general smooth-support blend recognizes the circular root where a native cylindrical boss leaves a
planar annular shoulder in a closed five-face stepped solid. Offsetting the shoulder and boss supports by the requested
radius produces an exact circular spine. The result directly reconstructs the retained outer cylinder, trimmed shoulder,
rational quarter-torus, shortened boss cylinder, and both caps. The torus meets the plane and cylinder at exact G1
contact circles; it is not a sampled sweep or faceted cutter.

This structural route is deliberately narrow: the selected circle must be the inner shoulder rim and the radius must be
smaller than both boss height and shoulder width. The opposite rim, partial cylinders, arbitrary trimmed supports, and
radii that consume a retained face refuse instead of falling through to the planar-edge approximation.
`PlaneCylinderFilletVerification` has 24 C++ checks for analytic torus identity and residual, G1 contacts, exact support
extents, V5/E9/C18/L6/F6 topology, volume direction/value, oblique and reversed axes, feasibility refusals, the legacy
native-cylinder route, exact console commit, and the C++-generated
[`Proofs/Phase31_PlaneCylinderFillet.png`](Proofs/Phase31_PlaneCylinderFillet.png).

## Closed tangent-chain boss-root fillets (Phase 32a)

A circular boss root may be represented by several rational arc edges when its shoulder, boss, and outer wall are split
into angular NURBS patches. `BlendSolver::TangentChain` now follows unambiguous G1 edge continuations from one selected
manifold seed. The plane–cylinder classifier validates that the discovered arcs close through a complete `2π`, that all
support patches share the derived axis/radii/extents, and that the complete `3N+2` face stepped topology is present.
Selecting one arc then rolls the whole root and heals the artificial representation seams into the same canonical exact
quarter-torus result as the unsplit body.

`TangentChainFilletVerification` has 28 C++ checks covering two- and four-member closed chains, seed independence,
singleton closed edges, exact topology/torus residual/G1/supports/volume, seam healing, oblique axes, unsupported outer
chains, radius bounds, non-analytic member refusal, console commit, and the C++-generated
[`Proofs/Phase32a_TangentChainFillet.png`](Proofs/Phase32a_TangentChainFillet.png).

## Finite semicircular tangent-chain fillets (Phase 32b)

The first endpoint-aware route accepts a semicircular stepped boss whose root is one half-turn, optionally split into two
or four rational arc members. The two physical chain endpoints lie on one planar diameter face. A seed anywhere on the
chain propagates only through its G1 members, trims the exact plane/cylinder supports, and creates a rational half-torus
with two exact quarter-circle end meridians. Internal angular seams heal while the planar end face remains; the result is
a one-hull, genus-zero `V12/E17/C34/L7/F7` solid. Positive or negative half-turns and oblique axes are supported.

`OpenChainFilletVerification` has 30 C++ checks covering endpoint count, seed independence, two/four-member chains,
exact torus residual and G1 contacts, end meridians, support/cap retention, analytic half-volume, sweep direction,
transformed axes, refusal bounds, console commit, and the C++-generated
[`Proofs/Phase32b_OpenChainFillet.png`](Proofs/Phase32b_OpenChainFillet.png).

## Transactional multi-edge fillet sets (Phase 32c)

`BlendSolver::FilletEdges` accepts an intentional seed set under all-or-nothing semantics. Repeated indices and multiple
members of the same curved tangent chain deduplicate before construction. Independent vertex-disjoint chains are sorted
by sampled geometric identity, re-resolved after each topology change, and committed only if every roll succeeds. This
removes the console's former midpoint-only, partial-success loop. A shared vertex is classified as a corner request and
refuses before any geometry changes rather than pretending two independent rolls solve the corner.

`MultiEdgeFilletVerification` has 28 C++ checks covering two opposite box-edge rolls, summed analytic volume, exact
V12/E18/C36/L8/F8 topology, order independence, source immutability, two/four-member boss-chain deduplication, empty,
out-of-range, oversize, mixed unsupported and shared-corner refusals, console scene rollback, and the C++-generated
[`Proofs/Phase32c_MultiEdgeFillet.png`](Proofs/Phase32c_MultiEdgeFillet.png).

## General-angle radial chain endpoints (Phase 32d)

Finite plane–cylinder root chains are no longer restricted to a half-turn. The classifier measures the ordered signed
arc sweep from the actual chain, verifies a matching outer-wall span, and recognizes a structurally exact sector whose
two radial planar caps share one rotation-axis edge. Reconstruction heals angular representation seams but retains both
physical torus meridians and both endpoint caps. Positive/negative quarter turns, 120° sectors, 270° reflex sectors, and
shifted oblique axes use the same exact-support route.

`SectorEndpointFilletVerification` has 33 C++ checks covering two/four-member source topology, exact
V12/E18/C36/L8/F8 output, rational partial-torus identity and span, implicit residual, both G1 contacts, radial caps and
axis edge, exact meridians, angular-fraction volume, direction/split invariance, transformed axes, transactional chain
deduplication, malformed-cap/radius refusal, console commit, and the C++-generated
[`Proofs/Phase32d_SectorEndpointFillet.png`](Proofs/Phase32d_SectorEndpointFillet.png). Phase 32 remains open for
asymmetric/non-radial endpoints, unequal/non-orthogonal partial corners, holes, non-box thin walls, and general
blend/blend intersections.

## Exact orthogonal three-face corners (Phase 32e)

`FilletEdges` now recognizes exactly three mutually perpendicular straight edges meeting at one vertex of a structurally verified rectangular solid. It rebuilds six trimmed planes, three exact equal-radius rolling cylinders, and one rational spherical octant transition as a one-hull `V13/E21/C42/L10/F10` solid. Input order and repeated seeds are invariant, and rigidly transformed boxes use the same geometric classifier.

`CornerFilletVerification` has 23 C++ checks for topology, exact support counts, spherical identity/residual, three G1 sphere-cylinder seams, six exact radius arcs, analytic volume direction/value, source immutability, ordering, transforms, transactional refusal, console commit, and `Proofs/Phase32e_CornerFillet.png`. Two-edge and other partial corner networks, unequal radii, non-orthogonal corners, holes in this corner route, and general non-box blend intersections remain unsupported.

## Complete rounded rectangular solids (Phase 32f)

Selecting all twelve edges of a structurally verified rectangular solid now takes one exact network route. Six inset planes, twelve equal-radius cylinders, and eight rational spherical octants sew to canonical `V24/E48/C96/L26/F26` topology. Repeated/reordered seeds and rigid transforms are invariant, and incomplete interacting networks refuse transactionally. `RoundedBoxFilletVerification` contributes 19 checks plus `Proofs/Phase32f_RoundedBox.png`. Partial networks, unequal radii, holes, and non-orthogonal blend intersections remain separate.

## Rounded-prism parallel edge families (Phase 32g)

Selecting all four mutually parallel edges of a verified rectangular solid now rebuilds the complete cross-section as four planes and four rational cylinders, with two planar rounded end caps. The route produces `V16/E24/C48/L10/F10`, is invariant across all three box axes, seed order, duplicates, and rigid transforms, and refuses before construction when `2r` consumes either cross-wall. `RoundedPrismFilletVerification` contributes 20 checks and `Proofs/Phase32g_RoundedPrism.png`. Mixed or incomplete interacting families remain unsupported.

## Perforated rounded prisms (Phase 32h)

The same complete outer four-edge family now preserves one exact circular through-hole when the source is the verified `V10/E15/C30/L9/F7` rectangular extrusion, the bore is centred on and coaxial with the prism, and its radius leaves positive radial wall clearance. The rebuilt body adds the reversed exact cylindrical bore to the rounded outer shell so sewing creates two annular end caps and canonical genus-one `V18/E27/C54/L13/F11` topology. This phase established only the single centred boundary; additional placements remained separate. `PerforatedPrismFilletVerification` contributes 20 checks and `Proofs/Phase32h_PerforatedPrism.png`.

## Offset axis-parallel bores (Phase 32i)

One axis-parallel circular through-hole may now move away from the prism centreline while retaining the same exact genus-one reconstruction. Feasibility is tested against the inward offset of the actual rounded cross-section: for a bore smaller than the outer roll, its centre must lie inside the reduced-radius rounded rectangle; at or above the roll radius, strict side-wall distances apply. Safe corner-adjacent holes commit while corner/side intersections refuse before fallback. `OffsetBorePrismFilletVerification` contributes 21 checks and `Proofs/Phase32i_OffsetBorePrism.png`.

## Twin axis-parallel bores (Phase 32j)

Exactly two geometrically paired circular through-holes now survive the same complete outer-edge rebuild. Each bore independently passes the rounded-wall offset test and the pair must retain a strictly positive inter-hole ligament. The result has two inward rational cylinders, two end caps with three loops each, and canonical genus-two `V20/E30/C60/L16/F12` topology. Profile-loop order, rail order, and rigid transforms are invariant. `TwinBorePrismFilletVerification` contributes 21 checks and `Proofs/Phase32j_TwinBorePrism.png`.

## Bounded multi-bore rounded prisms (Phase 32k)

The geometric rim-pairing and pairwise-clearance route now accepts three through eight axis-parallel circular bores. For `N` holes, source topology must be canonical `V(8+2N)/E(12+3N)/C(24+6N)/L(6+3N)/F(6+N)` and the rounded result is exactly `V(16+2N)/E(24+3N)/C(48+6N)/L(10+3N)/F(10+N)`, with genus `N`. Every disk passes the rounded-wall erosion gate and every pair retains a merge-tolerance ligament. Three- and eight-hole cases are verified; nine holes, intersections, and arbitrary perforated solids refuse transactionally. `MultiBorePrismFilletVerification` contributes 21 checks and `Proofs/Phase32k_MultiBorePrism.png`.

## One axis-parallel blind bore (Phase 32l)

A canonical rectangular prism with one cylindrical cavity entering either selected-family end now retains its offset, radius, entry side, and finite depth while the outer four edges round. The source is a one-hull genus-zero `V10/E15/C30/L9/F8` Boolean result. The dedicated route rebuilds the exact rounded exterior, subtracts the bounded cavity, and restores the fitted entrance intersection as an exact rational circle. Output is canonical genus-zero `V18/E27/C54/L13/F12`, with seven planes, four outer rolls, one inward cylindrical wall, one annular entrance cap, and one planar cavity floor. The orthogonal side-entering case delegates to Phase 32r; oblique, stepped, or counterbored side cavities remain separate. `BlindBorePrismFilletVerification` contributes 21 checks and `Proofs/Phase32l_BlindBorePrism.png`.

## Exactly two separated axis-parallel blind bores (Phase 32m)

The same bounded classifier accepts canonical genus-zero `V12/E18/C36/L12/F10` sources containing exactly two finite cylindrical cavities. The cavities may enter one common end or opposite ends, and may even be coaxial across a positive axial ligament. Pairwise finite-cylinder clearance combines bounded axial-interval and transverse-disk separation rather than treating the cavities as infinite cylinders. Reconstruction subtracts both cavities deterministically and restores both fitted entrances, yielding exact `V20/E30/C60/L16/F14` topology with eight planes, six cylinders, and four rational cavity rims. `DualBlindBorePrismFilletVerification` contributes 26 checks and `Proofs/Phase32m_DualBlindBorePrism.png`.

## Bounded multi-blind-bore set (Phase 32n)

Phase 32n scales the same exact construction to `3 <= N <= 8` separated finite cavities. Canonical source topology is `V=8+2N`, `E=12+3N`, `C=24+6N`, `L=6+3N`, `F=6+2N`; the rounded result is `V=16+2N`, `E=24+3N`, `C=48+6N`, `L=10+3N`, `F=10+2N`, always with genus zero. Every cavity passes the rounded-wall gate, all finite-cylinder pairs retain positive radial, axial, or combined clearance, and all `2N` fitted/floor rims finish as rational circles. Three- and eight-cavity cases are verified across both selected-axis ends. Nine or more selected-axis cavities, intersections, and oblique entries remain unsupported; one or two parallel side cavities use Phases 32r/32s. `MultiBlindBorePrismFilletVerification` contributes 31 checks and `Proofs/Phase32n_MultiBlindBorePrism.png`.

## One coaxial two-diameter stepped blind bore (Phase 32o)

A canonical counterbore with one larger entrance cylinder, one annular shoulder, and one smaller deeper cylinder survives the rounded-prism rebuild. The classifier distinguishes its connected axial spans from two separated cavities, requires a shared axis and strictly decreasing radius, and verifies both depths and analytic removed volume. Reconstruction subtracts the shallow outer stage before the deep inner stage, then restores the entrance, both shoulder rims, and floor rim as exact rational circles. Output is genus-zero `V20/E30/C60/L16/F14`, with eight planes, six rational cylinders, and two independently annular planar regions. `SteppedBlindBorePrismFilletVerification` contributes 25 checks and `Proofs/Phase32o_SteppedBlindBorePrism.png`.

## Bounded multistage coaxial blind bore (Phase 32p)

The connected-span classifier and deterministic cutter sequence scale to `3 <= N <= 8` strictly decreasing coaxial stages. Source and rounded topology follow the same genus-zero linear formula as other finite cylindrical cavity sets: source `V=8+2N/E=12+3N/C=24+6N/L=6+3N/F=6+2N`, output `V=16+2N/E=24+3N/C=48+6N/L=10+3N/F=10+2N`. Each added diameter contributes one cylindrical wall, one annular shoulder, and two exact rational rims; the last stage terminates at a planar floor. Three- and eight-stage cases, both entry ends, offsets, ordering, transforms, wall/eccentric refusal, and the explicit nine-stage cap are verified. `MultiStageBlindBorePrismFilletVerification` contributes 33 checks and `Proofs/Phase32p_MultiStageBlindBorePrism.png`.

## Exactly two separated two-stage blind bores (Phase 32q)

Two canonical counterbores may now enter the same or opposite prism ends. Each retains a larger shallow cylinder, annular shoulder, and smaller deep cylinder. Classification partitions four cylindrical spans into two unique coaxial chains; reconstruction applies both in deterministic geometric order. Pairwise clearance compares all four axial-band combinations using radial and bounded axial separation, permitting coaxial opposite-end counterbores when a positive ligament remains. Output is genus-zero `V24/E36/C72/L22/F18`, with ten planes, eight rational cylinders, four annular levels, and eight exact rational rims. A third stepped cavity, intersecting stage bands, eccentric chains, or multistage combinations remain separate increments. `DualSteppedBlindBorePrismFilletVerification` contributes 27 checks and `Proofs/Phase32q_DualSteppedBlindBorePrism.png`.

## One orthogonal side-entering blind bore (Phase 32r)

One canonical blind cylindrical cavity may enter either planar side parallel to either prism cross-section direction while the complete selected-axis edge family rounds. Classification preserves the chosen side, entry direction, axial and transverse centre, radius, finite depth, inward rational cylinder, and planar floor. Reconstruction requires the complete entrance circle to lie within the retained planar strip between rounded corners and clear of the selected-axis end caps, then restores the fitted entrance and existing floor as two exact rational circles. The result is genus-zero `V18/E27/C54/L13/F12`, with seven planes, five rational cylinders, one annular side wall, and analytic volume. A second parallel side cavity delegates to Phase 32s and stepped side counterbores to Phases 32u/32v/32w; oblique, through, mixed-axis, corner-crossing, end-crossing, and opposite-wall side cavities refuse transactionally. `SideBlindBorePrismFilletVerification` contributes 27 checks and `Proofs/Phase32r_SideBlindBorePrism.png`.

## Exactly two separated parallel side-entering blind bores (Phase 32s)

Exactly two canonical side cavities may share either cross-section direction and enter one common retained side or opposite parallel sides. Each entrance disk independently clears the selected-axis end caps and rounded-corner strips. Pairwise finite-cylinder clearance combines separation of the axial intervals along the common side direction with disk separation in the selected-axis/transverse plane, permitting coaxial opposite-side cavities across a positive ligament. Deterministic reconstruction restores all four entrance/floor rims as exact rational circles and produces genus-zero `V20/E30/C60/L16/F14`, with eight planes, six rational cylinders, and two planar inner loops. A third parallel cavity delegates to Phase 32t and a pair of two-stage cavities to Phase 32w; intersecting, mixed-axis, oblique, corner-crossing, end-crossing, and through side combinations refuse transactionally. `DualSideBlindBorePrismFilletVerification` contributes 32 checks and `Proofs/Phase32s_DualSideBlindBorePrism.png`.

## Bounded parallel side-entering blind-bore set (Phase 32t)

The parallel side route now scales to `3 <= N <= 8` separated finite cavities. Source topology is `V=8+2N/E=12+3N/C=24+6N/L=6+3N/F=6+2N`; rounded topology is genus-zero `V=16+2N/E=24+3N/C=48+6N/L=10+3N/F=10+2N`. Every entrance disk independently clears the selected-axis end caps and rounded-corner strip, every finite-cylinder pair retains positive radial, axial, or combined clearance, and all `2N` entrance/floor rims finish as exact rational circles. Cavities may enter one retained side or span opposite parallel sides while sharing Y or Z direction. Mixed-axis, intersecting, ninth, oblique, stepped sets, through, corner-crossing, and end-crossing side cavities refuse transactionally. `MultiSideBlindBorePrismFilletVerification` contributes 33 checks and `Proofs/Phase32t_MultiSideBlindBorePrism.png`.

## One two-diameter side-entering stepped blind bore (Phase 32u)

One canonical side counterbore may enter either retained Y/Z side with a larger shallow cylinder, exact annular shoulder, and smaller coaxial deep cylinder ending at a planar floor. Classification derives side, centre, radii, shoulder depth, and total depth from two connected spans in canonical genus-zero `V12/E18/C36/L12/F10` topology and verifies analytic removed volume. Reconstruction independently checks the outer entrance disk against the selected-axis end caps and retained planar strip, subtracts both bounded stages, and restores four exact rational entrance/shoulder/floor rims. Output is genus-zero `V20/E30/C60/L16/F14`, with eight planes, six rational cylinders, and two annular planar levels. A third stage delegates to Phase 32v; eccentric, undercut, corner-crossing, end-crossing, and opposite-wall side counterbores refuse transactionally. `SideSteppedBlindBorePrismFilletVerification` contributes 31 checks and `Proofs/Phase32u_SideSteppedBlindBorePrism.png`.

## Bounded multistage side-entering stepped blind bore (Phase 32v)

The same side-chain classifier now accepts one canonical cavity with `3 <= N <= 8` coaxial stages, while the two-stage route remains compatible. It recovers a unique low/high Y or Z entry and contiguous shoulder chain, then requires a common selected-axis/transverse centre, strictly decreasing positive radii, and strictly increasing finite cumulative depths. Source topology is genus-zero `V=8+2N/E=12+3N/C=24+6N/L=6+3N/F=6+2N`; rounded topology is `V=16+2N/E=24+3N/C=48+6N/L=10+3N/F=10+2N`. Deterministic bounded subtraction preserves all `N` annular planar levels, the final floor, analytic band volume, and `2N` exact rational rims. Multi-cavity sets delegate to Phases 32w/32x/32y; ninth stages, eccentric or non-decreasing chains, consumed shoulders, breakthrough, rounded-corner or selected-axis end-cap contact, and oblique/malformed topology refuse. `MultiStageSideBlindBorePrismFilletVerification` contributes 42 checks and `Proofs/Phase32v_MultiStageSideBlindBorePrism.png`.

## Exactly two separated side-entering two-stage blind bores (Phase 32w)

Two canonical side counterbores may share one Y/Z direction and enter one common retained side or opposite parallel sides. Classification partitions four cylindrical spans into two unique coaxial chains, requiring decreasing radii, increasing finite depths, exact stepped volume, and canonical genus-zero `V16/E24/C48/L18/F14` topology. Every pair of finite stage bands must retain positive radial, axial, or combined clearance, so coaxial opposite-side chains are supported across a ligament. Reconstruction checks both outer disks against rounded-corner strips and selected-axis end caps, subtracts all four stages deterministically, and restores eight exact rational entrance/shoulder/floor rims. Output is `V24/E36/C72/L22/F18`, with ten planes, eight rational cylinders, and four annular levels. A third two-stage chain delegates to Phase 32x and mixed stage counts delegate to Phase 32y; intersection, mixed axes, eccentricity, breakthrough, and retained-wall contact refuse transactionally. `DualSideSteppedBlindBorePrismFilletVerification` contributes 40 checks and `Proofs/Phase32w_DualSideSteppedBlindBorePrism.png`.

## Bounded side-entering two-stage blind-bore set (Phase 32x)

Phase 32x scales the separated two-stage side route to `3 <= N <= 8` counterbores. Source topology is genus-zero `V=8+4N/E=12+6N/C=24+12N/L=6+6N/F=6+4N`; rounded topology is `V=16+4N/E=24+6N/C=48+12N/L=10+6N/F=10+4N`. Every cavity has one decreasing-radius shoulder chain and may enter either parallel retained side while all cavities share Y or Z direction. All `4N` entrance/shoulder/floor rims become exact rational circles, all `2N` planar levels remain, and every one of the `N(N-1)/2` cavity pairs passes all four finite stage-band clearance comparisons. Mixed-stage members delegate to Phase 32y; nine cavities, intersections, mixed axes, eccentricity, breakthrough, and retained-wall contact refuse transactionally. `MultiSideSteppedBlindBorePrismFilletVerification` contributes 41 checks and `Proofs/Phase32x_MultiSideSteppedBlindBorePrism.png`.

## Bounded mixed-stage side-entering blind-bore set (Phase 32y)

Phase 32y generalizes the bounded side-stepped route to `2 <= N <= 8` cavities with independently mixed `2 <= Sᵢ <= 8` stage counts and a total budget `M = ΣSᵢ <= 16`. All cavities share retained-frame Y or Z direction, may independently enter either parallel retained side, and retain concentric strictly decreasing radii, strictly increasing finite depths, every shoulder and floor, and positive combined axial/radial clearance for every pair of finite stage bands. Source topology is genus-zero `V=8+2M/E=12+3M/C=24+6M/L=6+3M/F=6+2M`; rounded topology is `V=16+2M/E=24+3M/C=48+6M/L=10+3M/F=10+2M`. Output contains `6+M` planes, `4+M` rational cylinders, `M` planar inner loops, and `2M` exact rational circles. Seventeen total stages, a ninth stage or cavity, one-stage members, intersecting bands, mixed axes, eccentric or non-decreasing stages, wall/end contact, and breakthrough refuse transactionally. `MixedStageSideBlindBorePrismFilletVerification` contributes 42 checks and `Proofs/Phase32y_MixedStageSideBlindBorePrism.png`.

## Exact plane–cone boss-root fillet (Phase 32z)

The first unequal-radius support pair rounds the circular root where a native conical frustum boss (foot radius `R_f` on
the shoulder, top radius `R_t ≠ R_f`, half-angle `tan α = (R_f − R_t) / H`) leaves a planar annular shoulder. Both
supports are surfaces of revolution about one axis, so their offsets meet on an exact circular spine at
`ρ_c = R_f + r (1 − sin α) / cos α` and the roll is an exact rational torus band spanning `π/2 − α`, meeting the shoulder
at `ρ_c` and the cone at `(ρ_t, z_t) = (R_f − z_t tan α, r (1 − sin α))` with G1 contact circles. Narrowing bosses
(`α > 0`), undercut flares (`α < 0`) and the `α = 0` limit — which reproduces Phase 31's spine and closed-form volume to
`1e-12` — all pass through one route. The added wedge is given exactly by Pappus, and the route refuses its own result
if the tessellated volume disagrees with it. Feasibility is explicit (`z_t < H`, `ρ_c < R_outer`); apex cones, the
conical top rim, the outer shoulder rim, and Boolean-built sources without a canonical root rim refuse rather than
approximate. The suite also verifies the specification-level unequal-radius validators (endpoint pairs and chains, the
linear radius law and its measured ruled surface, G1 endpoint matching, the tapered-frustum reconstruction cross-check)
that Phase 33's variable-radius work will rest on; the partial-chain and variable-radius-roll modes remain refused.
`PlaneConeFilletVerification` contributes 61 checks and `Proofs/Phase32z_PlaneConeFillet.png`; the declared measurement
tolerances are recorded in [`docs/BLEND_LIMITS.md`](docs/BLEND_LIMITS.md#exact-plane–cone-boss-roots).

## Worked model: a wooden toy car (`Scripts/ToyCar.arc`)

The verifiers prove routes one at a time; this script proves the tool as a modelling tool by building a 20 cm wooden
toy car the way a woodworker would, with a proof render at every stage (`./build/SolidArc Scripts/ToyCar.arc`):

1. **Sketch** the side silhouette on the XZ workplane — one polyline, six per-corner sketch fillets (nose, cowl,
   windscreen, roof, deck, chin) — plus two arch circles, and subtract them with the **2D profile Boolean**;
2. **extrude** the silhouette 8 cm (the rational arcs become exact extrusion faces, `V10/E15/F7`);
3. sketch the **plan view** on XY (tapered nose, rounded corners) and extrude it through the block height;
4. **Boolean intersect** the two extrusions — the two-view carve gives the body, still genus 0;
5. **Boolean union** two axle bearing blocks that close the arches between the wheels;
6. **Boolean subtract** the two axle bores (genus 2: two tunnels) and six grille slots (pockets, genus unchanged);
7. build the parts — wheels, dowel hubs and axles — and union each set into one genus-0 solid.

Every intermediate is reported by `topology` as a closed, manifold, oriented solid with one hull, and the script ends with
`0 refusal(s)`. Proofs: `Proofs/ToyCar_A_Construction.png` (sketch → block → plan → intersect),
`Proofs/ToyCar_B_Features.png` (underside, grille close-up, wheelsets, assembly), `Proofs/ToyCar_C_FinalViews.png`
(front and rear three-quarter, nose-on, plan) and `Proofs/ToyCar_Hero.png`; the eight stage renders `ToyCar_1…8` are
written alongside. The kernel limits the exercise ran into — and the construction that honestly avoids each — are
recorded in [`docs/CAPABILITY_ROADMAP.md`](docs/CAPABILITY_ROADMAP.md#kernel-limits-found-by-the-worked-model).

The same exercise fixed a presentation defect that had made every proof render "x-ray": the camera's near plane
collapsed to 1 mm after any `view fit`, so the constant line depth bias spanned hundreds of scene units and B-rep edges
drew through walls. Depth planes now follow the eye every frame (`CameraProjection::NearDistance/FarDistance`) and the
line bias is a per-view clip-space constant equal to 0.2 % of the view depth (`ViewRecord::DepthPolicy`), so edges lying
on a face still win while edges behind a wall are occluded.

## Worked models: toy biplane and sailboat (`Scripts/ToyBiplane.arc`, `Scripts/ToySailboat.arc`)

Two more reference toys exercise the features the car did not: lofting, NURBS surfaces, sweeps, radial and linear
arrays, mirrors and exact edge fillets. Both scripts end with `0 refusal(s)` and every body reports as a closed manifold
solid.

**Biplane.** Fuselage = degree-1 `loft` of four rounded-rectangle sections on YZ workplanes (a tapering wooden bar);
tail fin = open interpolating `spline` joined to a straight base and extruded, unioned with the stabilizer slab; cowl =
native cylinder with the exact Phase 26 cap fillet plus six dowel plugs placed by `radial --count=6` and unioned one by
one; propeller = one slat with four exact tip fillets unioned through the ball nose; wings = rounded-rectangle slabs,
the upper wing's trailing-edge notch cut as a 2D Boolean before the extrude; struts = `pipe` × linear `array` ×
`mirror --across=xz`; landing gear = wheels, axle pipe and V-legs mirrored. Proofs: `Proofs/Biplane_A_Construction.png`,
`Proofs/Biplane_B_Assembly.png`, `Proofs/Biplane_C_FinalViews.png`, `Proofs/Biplane_Hero.png`.

**Sailboat.** Hull = degree-2 `loft` of seven ellipses rotated so the loft seam runs along the keel, cut flat with a
Boolean intersect 0.4 cm below the widest section; mast = a circle `sweep --scale=0.5` (tapering) unioned into the hull;
sails = luff, headboard, interpolating-spline leech and foot `join`ed into closed outlines and extruded 1.5 mm (veneer, as
in the reference); the bellied `fillpatch` (Coons) and `bridge` NURBS sheets are rendered as a separate proof tile.
Proofs: `Proofs/Boat_A_Construction.png`, `Proofs/Boat_B_SailsAndAssembly.png`, `Proofs/Boat_C_FinalViews.png`,
`Proofs/Boat_Hero.png`.

**Defects these two models found and fixed.** `radial` and `mirror` rotated or reflected a figure's *Blueprint* cells and
rebuilt the figure from them: direction cells (`Axis`, `Normal`) were moved as points, so any axis or plane not through
the origin skewed them; a rotated `box` was rebuilt as the axis-aligned box between its rotated corners (a 1.47 cm³ blade
came back at 21.8 cm³); derived figures were not transformed at all; and the documented `((o),(d))` axis form of
`mirror --across` never parsed. Both verbs now apply one exact affine map to the geometry, move Blueprint positions with
the map and directions with its linear part, bake forms the Blueprint cannot represent, and read `(dx,dy,dz)` as a
direction like `array --axis=`. The kernel limits they exposed are recorded in
[`docs/CAPABILITY_ROADMAP.md`](docs/CAPABILITY_ROADMAP.md#kernel-limits-found-by-the-worked-models).

Colour note: `tint` is only visible under `show shading plastic|flat`; the default matcap studios ignore it.

## Layout

| Folder | Role | Status |
|---|---|---|
| `Kernel/` | Pure geometry, zero dependencies (port target for anything) | **Phase 1 ✓** |
| `Interaction/` | `CameraProjection` · `SnapResolution` (lattice/endpoint/midpoint/centre/quadrant/on-curve/perpendicular/tangent/intersection/axis, pixel radius + priority) · `InputEvent` · `HotkeyChart` (Plasticity + Blender defaults, rebindable) · `ToolSession` (modal prompts, numeric entry, axis/plane locks, rubber-band preview, G/R/S) · `TransformGizmo` (GizmoPRO per `References/Gizmo.html`: cone/puck/plane/sector per axis, billboarded ring, analytic picking, Ctrl snapping) | **Phase 3 ✓** |
| `Presentation/` | `RasterExchange` seam · `SoftwareRaster` (CPU, pick + depth, PNG with own deflate) · Slang shaders compiled twice: by Slang for Vulkan later, by the C++ compiler through `SlangMirror.h` today · `ScenePresentation` (kernel → streams) · `MatcapStudio.slang` (ten procedural studios pre-render once to a layer sequence; one matcap **per whole**, switchable flat / plastic / matcap) | **Phase 2 ✓** |
| `Console/` | `CommandCodec` (`.arc` grammar) · `ConsoleHost` (sketch, primitives, extrude/revolve/loft, scene, view, `render`, `pick`) · `SolidArc` executable (script / `-c` / REPL) | **Phase 2 ✓** |
| `Document/` | `SceneDocument` — named figure with stable identities (pick id = figure ⊕ pole index), per-figure pole selection · `UndoSequence` — snapshot undo / redo with a change fingerprint | **Phase 4 ✓** |
| `Verification/` | One console-proof executable per phase, registered with ctest | ongoing |
| `Scripts/` | Reproducible `.arc` scripts (the visual test suite) | Phase 3+ |
| `Proofs/` | PNG outputs shown after each phase | Phase 2+ |

### Kernel (Phase 1)

| Unit | Contents |
|---|---|
| `ScalarCriteria.h` | The one tolerance policy (`KernelTolerance 1e-9`, `MergeTolerance 1e-6`, `AngularTolerance 1e-7`), `Refusal` / `Deliver<T>` fail-fast values |
| `VectorSpecification.h` | `Vec2/3/4`, `Quat`, column-major `Mat4` (Vulkan clip conventions, Z-up right-handed), `Plane`, `Workplane`, `Ray`, `Box3` |
| `CurveSpecification` | `NurbsCurve`: exact rational Line / Arc / Circle / 3-pt arc / Ellipse / Rectangle (+rounded) / Polygon / Slot, Bézier, control-point B-spline (open + periodic), global interpolation; de Boor, derivatives, curvature, length, closest point (Newton), knot insertion, Bézier decomposition, degree elevation, split / trim / reverse / join, adaptive tessellation |
| `SurfaceSpecification` | `NurbsSurface`: exact Plane / Sphere / Cylinder / Cone / Torus, B-spline patch, Extrusion, Revolution, Ruled, Loft (homogeneous skinning so circles stay exact); derivatives, outward normals (degenerate poles handled), iso-curves, closest point, knot insertion / split in U and V, curvature-adaptive tessellation with CCW triangles |

Winding rule: for every closed primitive `∂S/∂u × ∂S/∂v` points **outward**; tessellations are CCW seen from
outside. Verified numerically in `KernelVerification` — this is what booleans and back-face tinting rely on later.

## Phase schedule

| # | Phase | Proof |
|---|---|---|
| 1 | Kernel: vectors, tolerance, NURBS curves & surfaces | `KernelVerification` — 76 checks (radius error < 1e-12, de Boor ≡ Bernstein, refinement invariance, outward normals) |
| 2 | `RasterExchange` + software rasteriser + Slang shaders (lattice, line, point, surface) + camera | `Proof_02a_Lattice.png`, `Proof_02_Sphere.png` |
| 3 | Workplane, snap, modal input, sketch tools (Line … control-point curve) | dimensioned profile with snap markers |
| 4 | Pick step, selection modes, records, undo/redo, hotkey chart | highlighted selection, box select |
| 5 | Gizmo (GizmoPRO design) + G/R/S modal + numeric input | combined and separate T/R/S gizmos |
| 6 | B-rep topology (`BrepBody`), sew / cap / orient, solid primitives, face & edge selection | `TopologyVerification` — 79 checks; `Proof_06a/b/c` |
| 7 | 2D booleans, fillet / chamfer / trim / offset / join | area tables, winding normalised |
| 8 | Extrude / Revolve / Loft / Sweep → solids | extruded profile with hole, revolved vase |
| 9 | Surface–surface intersection + 3D NURBS booleans | `IntersectionVerification` — 47 checks; `Proofs/Phase9_Booleans_{Iso,Top}.png` |
| 9b | FairPatch — energy-fair fills with G0 / G1 / G2 rims from the adjacent faces, tension, guides, N-sided | `FairPatchVerification` — 47 checks; `Proofs/Phase9b_FairPatch_{Iso,Window,Pillow}.png` |
| 10 | Script suite, contact sheet, Vulkan hand-off notes | `SuiteVerification` — 51 checks; `docs/HANDOFF_VULKAN.md`; `docs/CONTACT_SHEET.md`; `Proofs/Phase10_ContactSheet.png` (2×2 of 1280×800 tiles); `Proofs/Phase10_Suite.png` (one iso render of every phase) |
| 11a | `solidify` (single-surface shell, refuses closed / closed-in-U or V / flat-sheet), `loft --guides=a,b` (plasticity-style, sheets bend through named curves via iterative projection with boundary clamping), `chamfer <body> --edges=i` (planar setback along the named body edge, with the two adjacent faces trimmed to the set-back lines and a new planar face added) | `BodyOpsVerification` — 25 checks (refusal cases for body / sphere, success on a saddle shell, bent-loft Z rises above the plain range, boundary rows preserved, single + triple sequential body chamfers reduce the volume) |
| 12 | `bridge <curve1> <curve2> [--degree] [--no-align]` (Plasticity-style 2-section loft: connects two open curves in any orientation as a ruled/skin surface), `array <figure>... --count=N --step=(dx,dy,dz)` (linear array, N total including seed, each copy translated by `Step·T` for `T = K/(N-1)`), `array <figure>... --count=N --axis=(ox,oy,oz),(dx,dy,dz) [--angle=deg] [--scale=s]` (radial array around an arbitrary axis with optional taper), `plane --name=N` + `workplane <name>` (named construction planes, also recoverable from a face normal with `plane --from=<figure> --name=N`) | `ArrayAndBridgeVerification` — 67 checks (bridge bounds match curve extents, linear-array X-step, radial-array Z-preservation + unit volume under rotation, taper shrinks volume, named-plane round-trip, all refusal cases); `Proofs/Phase12_{Bridge,ArrayLinear,ArrayRadial,NamedPlanes,ContactSheet}.png` |
| 13 | `dim list` / `dim <figure> --along=X\|Y\|Z` / `dim <figure> <p1> <p2>` / `dim edit <id> <value>` / `dim hide\|show\|delete <id\|all>` / `angle <polyline> [--at=K]` — Plasticity-style dimensions: **white** lines (was yellow), **world-space offset of 4 cm** (was 22 px screen-space), live editing that **rebuilds the figure from its parametric source** (Box / Sphere / Cylinder / Cone / Torus / Line / Circle / Arc / Ellipse / Polyline / Spline / Rect + Extrude / ChamferEdge). Each primitive records a `Blueprint` (input parameters) and each auto-emitted dim carries a slot index pointing into that Blueprint; `dim edit <id> <v>` mutates the slot and rebuilds the body / curve in place. Dims are drawn as a world-space overlay with extension lines, ticks, and a tiny 5×7 bitmap-font label. A `--no-dim` switch on any primitive suppresses auto-emit. | `DimensionVerification` — 78 checks (3 bbox dims on a box, arc-length 5 on a 3-4-5 line, radius 2 + circumference 4π on a circle, user linear/bbox/free dims, angle 90° on a right triangle, dim edit/hide/show/delete mutates the tree, refusal cases, `--no-dim` suppresses auto-emit, **Phase 13 redo**: live-edit rebuilds the body for box / cylinder / cone / chamfer with the new value, every primitive records a `ParametricBlueprint`, dim renderer uses white + world-space offset, not yellow + 22 px); `Proofs/Phase13b_{BoxBefore,BoxAfter,ConeBefore,ConeAfter,ChamferBefore,ChamferAfter,Round,ContactSheet}.png` |
| 14 | **Dim lines are hidden by default** in the SolidArc binary (Phase 14 polish pending). The dim tree itself is still auto-emitted and editable — `dim list`, `dim edit <id> <value>`, `dim hide\|show\|delete <id\|all>` all work; only the on-screen overlay is suppressed. Turn dims back on with `dim on` (or `dim off` / `dim on` to toggle). The intent: the next phase iterates on Plasticity-style placement (smart lift direction, face-aware insertion, tick + label glyph quality) without forcing the user to look at the current draft in the meantime. The Phase 13 proof scripts have been updated to start with `dim on` so their PNGs still show the dim lines. | (no new verification — toggle is covered by `DimensionVerification` which sets `ShowDimensions=true` per host; all 78 checks still pass) |
| 15 | **Per-vertex polyline live edit, construction-geometry dim suppression, undo for dim edits.** Polylines now get a per-vertex X/Y/Z dim set (slot 18+K·3, K=0…N-1), so `dim edit <name> <axis><K> <value>` pulls a single vertex and rebuilds the polyline. `dim edit` accepts a 2-token dim name (`B Y2`, `Penta X0`, etc.) as well as a numeric id. Construction lines (`--construction`) suppress the auto dim set — they still draw as construction geometry but contribute no live dims. `undo` / `redo` now re-emit the auto dim set so the dim tree tracks the rolled-back scene (the dim ids may renumber, but the named dims are stable). | `DimensionVerification` — 90 checks (+12: construction line emits zero auto dims, non-construction line still emits 3; 4-vertex polyline emits 12 per-vertex dims, vertex 2's Y dim live-edits to 5.0 and the polyline rebuilds with vertex 2 at y=5; box's Y dim live-edit to 7.0 then `undo` rolls back to 3.0 in both the body and the dim tree); `SuiteVerification` — 53 checks (+2: `Phase15_PolylineUndo.scr` runs without refusal and produces the 6 PNGs); `Scripts/Phase15_PolylineUndo.scr`; `Proofs/Phase15_{PolylineBefore,PolylineAfter,ConstructionSuppression,UndoBefore,UndoAfterEdit,UndoAfterUndo}.png` |
| 16 | **Live-edit for the remaining derived figures: revolve, pipe, sweep, loft, boolean (and explicit Blueprint for extrude).** Each derived op now records a `ParametricBlueprint` (loft degree, sweep scale + twist, pipe radius, revolve angle + axis + origin) so `dim edit` mutates the slot and `ApplyLiveEdit` re-produces the body from the recipe. Revolve angle is stored in radians; sweep twist is in radians; both the user input (in degrees) and the dim display (back to degrees) convert via `ScalarCriteria`. Boolean has no live slot — it consumes its inputs and emits a read-only header dim (slot = -1); the dim tree just labels the result. | `DimensionVerification` — 101 checks (+11: revolve emits a live angle dim (180°→90° rebuilds); pipe emits a live radius dim (0.25→0.5 rebuilds); sweep emits a live scale dim (1.0→2.0 rebuilds); boolean emits a header dim with slot=-1, no live rebuild path); `SuiteVerification` — 55 checks (+2: `Phase16_DerivedLiveEdit.scr` runs without refusal and produces 7 PNGs); `Scripts/Phase16_DerivedLiveEdit.scr`; `Proofs/Phase16_{RevolveBefore,RevolveAfter,PipeBefore,PipeAfter,SweepBefore,SweepAfter,Boolean}.png` |
| 17 | **Sub-entity dimensions + leader lines.** `dim <figure> face <F>` emits a face-anchored dim (face area via tessellation, perimeter = sum of edge lengths around the face's loops) — both fields are read-only (slot = -1). `dim <figure> edge <E>` emits an edge-anchored dim (length, plus a radius dim for Circle / Arc-classified edges). `dim sub <figure>` auto-emits per-face + per-edge dims for the entire body in one command (every face gets an area + perimeter dim, every edge gets a length dim, circular edges get a radius dim too). `dim leader <figure> (x,y,z) [text...]` emits a free-floating leader: a line from the feature point to the label position, with the text drawn at the label. `--leader=(x,y,z)` on `dim face` or `dim edge` switches an existing dim to leader mode. Each sub-entity dim carries an `AnchorFace` or `AnchorEdge` int, so the renderer knows where the dim is anchored (not just the figure's bounding box). The renderer draws leaders as line + dot at the feature + offset label; standard dims are unchanged. | `SubEntityDimensionVerification` — 34 checks (face dim emits area + perim with AnchorFace set; edge dim emits length with AnchorEdge set; `--leader=` flips the dim to leader mode with the right B endpoint; `dim leader` produces a labelled leader; `dim sub` adds 24 dims to a box (12 face + 12 edge) and 11 to a cylinder (6 face + 3 edge + 2 radius); out-of-range face index refuses; non-body refuses `dim sub` / `dim edge`; deleting the anchor figure doesn't crash the renderer); `SuiteVerification` — 57 checks (+2: `Phase17_SubEntityDims.scr` runs without refusal and produces 4 PNGs); `Scripts/Phase17_SubEntityDims.scr`; `Proofs/Phase17_{FaceEdgeLeader,DimSub,CylinderDimSub,FaceWithLeader}.png` |
| 18 | **2D constraint graph (Newton + analytic Jacobian).** A persistent constraint graph lives in the host: `constraint distance <fA.p> <fB.p> = <v>`, `constraint angle <lineA> <lineB> = <deg>`, `constraint coincident <fA.p> <fB.p>`, `constraint horizontal / vertical / parallel / perpendicular / equal / equal-radius`, `constraint pin <f.p>`, `constraint list / clear / dof / solve / delete`. The unknowns are 2D points on the workplane; each constraint type has a hand-derived residual and Jacobian row (no auto-diff). The solver is Newton with normal equations (A^T A dx = -A^T r) and backtracking line search; rank-deficient systems are reported with a dof count instead of refusing. `dim edit` on a figure that is referenced by the graph re-projects the Blueprint, re-solves, and rebuilds — the closed-loop dim-driven re-solve hook. The graph is also wiped by `reset` (the dim tree is not, to preserve Phase 13 script id expectations). Point refs are `Figure.start / end / centre / point / vertexK / cornerK`; line refs are figure names; circle refs are figure names. | `ConstraintVerification` — 51 checks (each constraint type's analytic Jacobian is checked row-by-row; dof analysis on 2 free points + 1 distance + 1 coincident + 1 vertical/horizontal returns the right rank-deficiency; Newton converges a 2×3 rectangle (4 lines, 4 distances, 4 coincident, 1 fixed), a 3-4-5 triangle (3 lines, 3 distances, 1 vertical), and a two-circles distance-between-centres; under-determined Newton still moves toward the constraint via the minimum-norm step; host integration: `constraint distance L1.start L1.end = 2` adds the constraint, `constraint solve` reshapes a 5×5 square into a 2×3 rectangle, `constraint dof` reports, `constraint list` / `clear` / `delete` round-trip, `dim edit C1 radius 1.5` triggers the re-solve hook, the solved rectangle renders to a non-empty PNG); `SuiteVerification` — 59 checks (+2: `Phase18_Constraints.scr` runs without refusal and produces 4 PNGs); `Scripts/Phase18_Constraints.scr`; `Proofs/Phase18_{Rectangle,Triangle,TwoCircles,DimEditResolves}.png` |
| 19 | **3D mirror, radial mirror, and Empty transform handles.** `mirror <fig...|selected> [--across=<spec>] [--also=<spec>]... [--copy\|--in-place] [--name=<stem>]` reflects a figure across a plane (the spec is `xy`, `xz`, `yz`, a named workplane saved with `plane --name=…`, a plane through the origin with normal `(nx,ny,nz)`, or a line `(ox,oy,oz),(dx,dy,dz)`). `--also=<spec>` adds a second (or third) perpendicular axis, and the verb produces 2ⁿ−1 copies in one command (1 plane = 1 copy, 2 planes = 3 copies, 3 planes = 7 copies). `radial <fig...|selected> --count=N --axis=(ox,oy,oz),(dx,dy,dz) [--angle=deg=360] [--name=<stem>]` produces N−1 evenly-spaced rotated copies around the 3D line. `empty --name=E --at=(x,y,z)` adds a no-geometry transform handle (Blender Empty) that can itself be mirrored, used as a radial-axis anchor, or simply be the named reference of a workplane. `list empty` enumerates them; `delete empty <name>\|all` removes one or all. The math lives in `Kernel/MirrorSolver.{h,cpp}` (hand-rolled Rodrigues, no Eigen / GLM). The verb wraps the math by reflecting the source figure's `Blueprint.A` / `Blueprint.B` and rebuilding from the reflected source (so curved, planar, and Brep bodies all mirror consistently). | `MirrorVerification` — 40 checks (each reflection formula is checked point-by-point: XY / XZ / YZ axis-aligned planes, axis reflection with on-axis invariance, Rodrigues 90° / 180° / 360°, multi-axis composition is a 180° rotation around the line of intersection, radial 4 copies at 90° around Z are distinct, custom diagonal plane; host integration: empty create / list / delete round-trip, mirror of a line across XY flips the Z and preserves length, in-place reflection mutates the source instead of copying, mirror across a custom named workplane produces the right reflected point, mirror across an axis line flips only the perpendicular component, multi-axis mirror produces 3 copies in 2 perpendicular planes, mirror of a body reflects A and B and rebuilds, radial of a sphere produces 4 sphere figures); `SuiteVerification` — 61 checks (+2: `Phase19_Mirror.scr` runs without refusal and produces 6 PNGs); `Scripts/Phase19_Mirror.scr`; `Proofs/Phase19_{MirrorLine,MirrorBox,MultiAxis,RadialSphere,InPlace,Empties}.png` |
| 20 | **Dim placement polish + black background.** (1) `Backdrop` changed from slate `(0.117, 0.129, 0.153)` to pure black `0, 0, 0` everywhere (the main render pass, the backstop fill in the contact-sheet composite, and the RasterVerification harness). (2) `ConsoleHost::CameraFacingSide(Vec3 N)` — new helper that returns `+N` or `−N` flipped to point at the camera, with a screen-right fallback when the camera looks along `N` (so axis-aligned views front / back / side / top still pick a sensible side). (3) Every dim in `AutoEmitDimensions` is now placed on the camera-facing side: Box's X / Y / Z dims pick the +Y / +X / +X side that faces the camera, Cylinder / Cone / Extrude / Pipe / Sphere / Torus / ChamferEdge all use the camera-facing perpendicular, Loft / Sweep / Boolean pick the camera-facing top face. (4) Once a curve is consumed by an extrude / revolve / pipe / sweep / loft / boolean / bridge, the source's auto dim set is hidden so only the result's dim shows (no more "C1 radius 1.000 AND E1 length 2.000" — just the latter). (5) `reset` now also hides any auto dim whose anchor figure no longer exists (the old "dim id stability" behaviour is preserved — only the visibility changes). (6) The pre-render offset (`FaceOffset = 0.04` in the dim emit + `OffM = 0.04` in the renderer) is now only applied in the renderer, so the dim is 4 cm off the body instead of 8 cm. | `SuiteVerification` — 63 checks (+2: `Phase20_DimPolish.scr` runs without refusal and produces 12 PNGs: circle with radius dim, extrude with length dim iso / front / back / top, box with three bbox dims iso / front, sphere with radius dim iso / right — all on the camera-facing side, no source-dim leakage); `Scripts/Phase20_DimPolish.scr`; `Proofs/Phase20_{CircleRadius,ExtrudeLengthIso,ExtrudeLengthFront,ExtrudeLengthBack,ExtrudeLengthTop,BoxIso,BoxFront,SphereIso,SphereRight}.png`; also re-rendered the existing `Proofs/Phase19_*.png` and all per-phase proofs in the new black background. The `RasterVerification` check "Cell interior stays near backdrop" was updated from `R≈95, B≈110` (slate) to `R<25, G<25, B<25` (black) — same pixel test, new expectation. |

| 23 | **Adversarial planar NURBS contacts, self-crossings, free-form offsets and Booleans.** `SelfIntersections` now also identifies non-rational cubic loops contained within one Bézier span. `offset` rejects pre-existing self crossings, sampled curvature cusps and folded candidate results; its rational-quadratic fast path verifies a span is circular before using an exact arc construction, preventing ellipse-to-osculating-circle corruption. | `ProfileAdversarialVerification` — 41 checks; `Scripts/Phase23_AdversarialProfiles.arc`; `Proofs/Phase23_AdversarialProfiles.png` (2560 × 1600 contact sheet) |
| 24 | **B-rep Boolean contact healing for structurally verified axis-aligned boxes.** Shared faces and rectangular overlaps resolve constructively; edge/point contacts are retained as independent manifold hulls; empty-volume outcomes refuse explicitly. The general curved/non-transversal path remains bounded. | `BooleanContactVerification` — 31 C++ checks; `Proofs/Phase24_BooleanContacts.png` (2560 × 1600 C++-generated contact sheet) |
| 24b | **Exact duplicate B-rep Boolean identity.** Full structural copies bypass non-transversal SSI; union/common retain one valid operand and difference refuses as empty. Near copies are not fuzzy-merged. | `BooleanIdentityVerification` — 26 C++ checks; `Proofs/Phase24b_BooleanIdentity.png` (2560 × 1600 C++-generated contact sheet) |
| 24c | **Affine-NURBS-equivalent Boolean identity.** A cylinder and an equivalent circular extrusion can differ only by parameter domain and analytic hint; full geometry returns one solid without an SSI curve, while spatial near misses do not pass. | `BooleanEquivalenceVerification` — 17 C++ checks; `Proofs/Phase24c_BooleanEquivalence.png` (2560 × 1600 C++-generated contact sheet) |
| 24d | **Seam-invariant full right-cylinder Boolean identity.** Equivalent cylinders with relocated circular seams or opposite construction direction return one solid without a fictitious SSI section; physical radius/height differences remain excluded. | `BooleanCylinderSeamVerification` — 18 C++ checks; `Proofs/Phase24d_BooleanCylinderSeams.png` (2560 × 1600 C++-generated contact sheet) |
| 25 | **Exact circular native-cylinder cap chamfer.** A selected rational circular cap edge rebuilds as the retained cylinder plus an exact conical frustum, including top/bottom and oblique-axis instances; extrusion lookalikes and axis/full-height set-backs refuse. | `CylinderChamferVerification` — 19 C++ checks; `Proofs/Phase25_CylinderChamfers.png` (2560 × 1600 C++-generated contact sheet) |
| 26 | **Exact circular native-cylinder cap fillet.** A selected circular cap edge rebuilds as the retained cylinder plus a rational quarter-torus rolling-ball patch, with G1 joins at side and cap, including reversed construction; extrusion lookalikes and axis/full-height radii refuse. | `CylinderFilletVerification` — 22 C++ checks; `Proofs/Phase26_CylinderFillets.png` (2560 × 1600 C++-generated contact sheet) |
| 27 | **Exact native-cylinder cap face push.** A selected cap moves along its actual outward normal by directly rebuilding the cylinder height/base, including upper/lower, inward/outward, oblique, and reversed construction cases. | `CylinderPushVerification` — 19 C++ checks; `Proofs/Phase27_CylinderPushes.png` (2560 × 1600 C++-generated contact sheet) |
| 28 | **Exact native-cylinder radial side-face push.** The selected cylindrical face offsets directly to `R+d`, retaining exact caps/axis/height across outward, inward, oblique, and reversed construction cases. | `CylinderSidePushVerification` — 15 C++ checks; `Proofs/Phase28_CylinderSidePushes.png` (2560 × 1600 C++-generated contact sheet) |
| 29 | **Exact native-frustum side-face push.** The selected conical side offsets normally by changing both radii by `d·sqrt(1+slope²)` while retaining cap planes, axis, and height. | `ConeSidePushVerification` — 10 C++ checks; `Proofs/Phase29_ConeSidePushes.png` (2560 × 1600 C++-generated contact sheet) |
| 30 | **Exact native-frustum cap-face push.** Either cap follows the original infinite conical support; positive/negative height construction, tapering/expanding slopes, exact geometry and apex-crossing refusals are verified. | `ConeCapPushVerification` — 19 C++ checks; `Proofs/Phase30_ConeCapPushes.png` (2560 × 1600 C++-generated contact sheet) |
| 31 | **Exact plane–cylinder boss-root fillet.** A structurally recognized circular boss/annular-shoulder contact rebuilds as trimmed exact supports and a rational quarter-torus along their offset-intersection spine. | `PlaneCylinderFilletVerification` — 24 C++ checks; `Proofs/Phase31_PlaneCylinderFillet.png` (2560 × 1600 C++-generated contact sheet) |
| 32a | **Closed G1 tangent-chain propagation.** One arc seed follows a complete representation-split boss-root ring; all support patches are verified and rebuilt as one exact seam-healed roll. | `TangentChainFilletVerification` — 28 C++ checks; `Proofs/Phase32a_TangentChainFillet.png` (2560 × 1600 C++-generated contact sheet) |
| 32b | **Finite semicircular chain endpoints.** A two/four-member half-turn root retains one planar diameter cap and two exact torus end meridians while healing internal seams. | `OpenChainFilletVerification` — 30 C++ checks; `Proofs/Phase32b_OpenChainFillet.png` (2560 × 1600 C++-generated contact sheet) |
| 32c | **Intentional multi-edge fillet sets.** Repeated/tangent-chain member seeds deduplicate; independent vertex-disjoint chains apply in deterministic geometric order and commit transactionally; interacting corner sets refuse before construction. | `MultiEdgeFilletVerification` — 28 C++ checks; `Proofs/Phase32c_MultiEdgeFillet.png` (2560 × 1600 C++-generated contact sheet) |
| 32d | **General-angle radial chain endpoints.** Ordered signed chain spans from 90° through reflex 270° rebuild as one exact partial torus between two verified radial caps sharing the axis edge. | `SectorEndpointFilletVerification` — 33 C++ checks; `Proofs/Phase32d_SectorEndpointFillet.png` (2560 × 1600 C++-generated contact sheet) |
| 32e | **Exact orthogonal three-face corner.** Three incident edges of a rectangular solid rebuild as three equal-radius cylinders joined G1 to one rational spherical octant. | `CornerFilletVerification` — 23 C++ checks; `Proofs/Phase32e_CornerFillet.png` (2560 × 1600 C++-generated contact sheet) |
| 32f | **Complete rounded rectangular solid.** Selecting all twelve edges composes six inset planes, twelve exact cylinders, and eight rational spherical octants. | `RoundedBoxFilletVerification` — 19 C++ checks; `Proofs/Phase32f_RoundedBox.png` (2560 × 1600 C++-generated contact sheet) |
| 32g | **Complete parallel-edge family.** Four parallel box edges rebuild as an exact rounded prism with a global `2r` cross-wall feasibility gate. | `RoundedPrismFilletVerification` — 20 C++ checks; `Proofs/Phase32g_RoundedPrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32h | **One coaxial hole through a rounded prism.** The complete outer family retains one centred exact circular bore with positive radial wall clearance and genus-one annular caps. | `PerforatedPrismFilletVerification` — 20 C++ checks; `Proofs/Phase32h_PerforatedPrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32i | **One offset axis-parallel through-hole.** Exact rounded-wall inward-offset clearance admits safe side/corner placements and refuses wall intersections. | `OffsetBorePrismFilletVerification` — 21 C++ checks; `Proofs/Phase32i_OffsetBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32j | **Exactly two axis-parallel through-holes.** Geometric rim pairing plus wall and inter-hole ligament checks preserve canonical genus-two topology. | `TwinBorePrismFilletVerification` — 21 C++ checks; `Proofs/Phase32j_TwinBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32k | **Bounded multi-bore rounded prism.** Three through eight separated axis-parallel bores scale by exact topology formula with all-pairs ligament checks. | `MultiBorePrismFilletVerification` — 21 C++ checks; `Proofs/Phase32k_MultiBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32l | **One axis-parallel blind bore.** Either prism end may retain one finite-depth exact cylindrical cavity with a planar floor and restored rational entrance rim. | `BlindBorePrismFilletVerification` — 21 C++ checks; `Proofs/Phase32l_BlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32m | **Exactly two separated axis-parallel blind bores.** Same-end, opposite-end, and coaxial opposite-end cavities retain finite-cylinder clearance and four rational rims. | `DualBlindBorePrismFilletVerification` — 26 C++ checks; `Proofs/Phase32m_DualBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32n | **Bounded multi-blind-bore rounded prism.** Three through eight separated finite cylinders scale by exact genus-zero topology formula and all-pairs finite clearance. | `MultiBlindBorePrismFilletVerification` — 31 C++ checks; `Proofs/Phase32n_MultiBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32o | **One coaxial two-diameter stepped blind bore.** A larger entrance stage, exact annular shoulder, and smaller deep stage retain four rational rims. | `SteppedBlindBorePrismFilletVerification` — 25 C++ checks; `Proofs/Phase32o_SteppedBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32p | **Bounded multistage coaxial blind bore.** Three through eight decreasing diameters preserve exact annular levels and `2N` rational rims. | `MultiStageBlindBorePrismFilletVerification` — 33 C++ checks; `Proofs/Phase32p_MultiStageBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32q | **Exactly two separated two-stage blind bores.** Same/opposite-end counterbores retain finite-band clearance, four annular levels, and eight rational rims. | `DualSteppedBlindBorePrismFilletVerification` — 27 C++ checks; `Proofs/Phase32q_DualSteppedBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32r | **One orthogonal side-entering blind bore.** Either cross-section direction and either retained planar side preserve finite depth and two rational rims. | `SideBlindBorePrismFilletVerification` — 27 C++ checks; `Proofs/Phase32r_SideBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32s | **Exactly two separated parallel side-entering blind bores.** Same/opposite retained sides preserve finite-cylinder clearance and four rational rims. | `DualSideBlindBorePrismFilletVerification` — 32 C++ checks; `Proofs/Phase32s_DualSideBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32t | **Bounded parallel side-entering blind-bore set.** Three through eight cavities preserve finite-cylinder clearance and `2N` rational rims. | `MultiSideBlindBorePrismFilletVerification` — 33 C++ checks; `Proofs/Phase32t_MultiSideBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32u | **One two-diameter side-entering stepped blind bore.** A retained side entrance, annular shoulder, and planar floor preserve four rational rims. | `SideSteppedBlindBorePrismFilletVerification` — 31 C++ checks; `Proofs/Phase32u_SideSteppedBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32v | **Bounded multistage side-entering stepped blind bore.** Three through eight decreasing diameters preserve every annular level and `2N` rational rims. | `MultiStageSideBlindBorePrismFilletVerification` — 42 C++ checks; `Proofs/Phase32v_MultiStageSideBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32w | **Exactly two separated side-entering two-stage blind bores.** Same/opposite retained sides preserve finite-band clearance, four annular levels, and eight rational rims. | `DualSideSteppedBlindBorePrismFilletVerification` — 40 C++ checks; `Proofs/Phase32w_DualSideSteppedBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32x | **Bounded side-entering two-stage blind-bore set.** Three through eight counterbores preserve all-pairs finite-band clearance, `2N` planar levels, and `4N` rational rims. | `MultiSideSteppedBlindBorePrismFilletVerification` — 41 C++ checks; `Proofs/Phase32x_MultiSideSteppedBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32y | **Bounded mixed-stage side-entering blind-bore set.** Two through eight cavities, two through eight stages each and sixteen stages total preserve all finite bands, `M` planar levels, and `2M` rational rims. | `MixedStageSideBlindBorePrismFilletVerification` — 42 C++ checks; `Proofs/Phase32y_MixedStageSideBlindBorePrism.png` (2560 × 1600 C++-generated contact sheet) |
| 32z | **Exact plane–cone boss-root fillet.** The first unequal-radius support pair: a native conical frustum boss on a planar shoulder rebuilds as trimmed exact supports and a rational `π/2 − α` torus band along the analytic spine, with the Pappus closed-form wedge and explicit feasibility limits. | `PlaneConeFilletVerification` — 61 C++ checks; `Proofs/Phase32z_PlaneConeFillet.png` (2560 × 1600 C++-generated contact sheet) |

## Console quick start

```bash
./build/SolidArc Scripts/Phase2_Primitives.arc        # run a script; PNGs land in Proofs/
./build/SolidArc -c "circle (0,0) 3; sphere (0,0,1) 1; view iso; view fit; render Quick"
./build/SolidArc                                        # REPL — type help
```

Points are `(x,y)` on the active workplane or `(x,y,z)` in world. Items are addressed by name or `#id`.

## Modal input (Phase 3)

The console is the input device — the same events a window will send later:

| Command | Meaning |
|---|---|
| `tool line` / `rect` / `circle` / `arc` / `spline` / … / `move` / `rotate` / `scale` | start a modal tool (prompts print as you go) |
| `pointer x y` · `click [--right] [--shift]` · `wheel n` | synthetic pointer; tools snap and preview |
| `key g` · `key shift+x` · `key numpad7` · `key enter` · `key esc` | hotkeys (global chart) or modal keys (tool) |
| `type 4` · `type 2,5` · `type @1,1` · `type r2` · `type a45` · `type a30,2` · `type n6` · `type d2` · `type 3*2` | numeric entry: distance · absolute · relative · radius · angle · polar · sides · degree · arithmetic |
| `inspect x y` · `snap …` · `hud` · `bind`/`unbind`/`hotkeys` | inspect snapping, toggle it, dump modal state, edit the hotkey chart |

| `gizmo on|off` · `gizmo combined|translate|rotate|scale` · `gizmo size px` · `gizmo grips` · `release` | GizmoPRO on the selection; `grips` prints every grip's pixel so scripts can grab it; `click` on a grip starts a drag, `pointer` moves it (`--ctrl` snaps 0.25 m / 0.1× / 5°), `release` commits |
| `show shading flat|plastic|matcap` · `matcap <figure> <studio>` · `matcap list` · `tint <figure> r g b` | shading mode for the view; per-figure studio (steel chrome gold copper plastic-white plastic-red plastic-blue clay pearl carbon) |


## Selection and timeline (Phase 4)

| Command | Meaning |
|---|---|
| `click x y [--shift]` · `select box x0 y0 x1 y1 [--add|--subtract]` · `select all|none|invert` · `select <figure>` | pick-plane selection: click, toggle, marquee (hidden figures never select) |
| `selectmode control|edge|face|whole|cycle` · keys `1 2 3 4`, `Tab` | Plasticity modes; control mode picks poles, `select poles <figure> <i…>|all|none` |
| `hide` `H` · `hide unselected` / `isolate` `Shift+H` · `unhide all` `Alt+H` · `delete` `X` | visibility and removal |
| `duplicate [(dx,dy,dz)]` `Shift+D` · `mirror x|y|z [--copy]` `Alt+X` | copies keep the source's matcap / tint; mirrored surfaces are re-oriented so normals stay outward |
| `undo [n]` `Ctrl+Z` · `redo [n]` `Ctrl+Shift+Z` / `Ctrl+Y` · `timeline` | every mutating command is one entry; a gizmo drag from grab to release is one entry; selection undoes with geometry |

Gizmo in control mode moves only the selected poles (pivot = their centroid), so a cage edit is a drag.

Inside a tool: `X`/`Y`/`Z` lock an axis (again to clear), `Shift+X/Y/Z` lock a plane, `Backspace` removes the last point,
`Enter`/right-click confirm, `Esc` cancels, `↑`/`↓` change polygon sides or spline degree, `Ctrl` while moving suppresses snapping.


## Boundary representation (Phase 6)

`Kernel/TopologySpecification.{h,cpp}` adds `BrepBody`: vertices, edges (NURBS curves), coedges (edge + sense), loops, faces (NURBS
surfaces + `Reversed` switch + trimming loops). Bodies are assembled with one generic operation — `BrepBody::Sew(surfaces)`:

1. every surface becomes a face whose natural boundary is split at tangent kinks, so a hexagon prism gets six side faces and one
   edge per corner while a slot extrusion stays a single side face;
2. coincident boundary curves are merged into shared edges (`FindCoincidentEdge`, either sense);
3. `Orient()` walks face adjacency so every interior edge has two coedges of opposite sense, then flips the whole body if the
   divergence-theorem volume is negative;
4. `Capped()` fits a plane (Newell) to every remaining open loop and adds a trimmed planar face, triangulated by ear clipping with
   hole bridging;
5. `Validate()` reports V/E/F/L, hulls, open / non-manifold / mis-oriented edges, χ, genus, volume and area.

`Box`, `Cylinder`, `Cone`, `Sphere`, `Torus`, `Extrude(profile, dir, length)` and `Revolve(profile, origin, axis, angle)` are thin
wrappers over `Sew`. Tessellated volumes land within 0.1 % of the analytic values; χ = 2 for genus-0 solids, 0 for the torus and ring.

Console: `box`, and `cylinder`/`cone`/`sphere`/`torus`/`extrude`/`revolve` now produce solids (`--sheet` keeps a plain surface; an
open profile still extrudes to a sheet). `topology <body>` prints vertices, edges with their coedge senses, loops and face normals;
`sew <figure…>` stitches surfaces. Select modes 3 (face) and 2 (edge) pick faces and edges from the pick plane — each face and edge
carries its own pick id — and `select faces|edges <body> <i…>|all|none` does it by index. Sub-selections drive the gizmo pivot and
are hashed into the undo timeline.

## FairPatch — fills whose rims follow the neighbouring faces (Phase 9b)

`Kernel/FairPatchSolver.{h,cpp}`. `fillpatch` (Phase 8) is a Coons blend: exact on its boundary, blind to what lies
next to it, so it always meets the surrounding faces with a crease. `fairpatch` keeps the exact boundary and *solves*
the interior instead:

- **Rims** are curves or body edges, each with a continuity and a tension. A rim's *support* is the face on the other
  side of the edge (chosen automatically as the face that continues flush with the fill; `@fN` picks one explicitly)
  or, for a sketch curve, the surface / body named by `--on=`. G1 makes the cross-boundary derivative lie in the
  support's tangent plane; G2 also matches the support's normal curvature across the rim (second fundamental form,
  measured by closest point so the support's parameterisation is irrelevant). Tension scales the cross-derivative
  magnitude relative to the Coons fill — 0.5 hugs the rim, 2 fills out.
- **Fair interior**: the boundary pole rows are fixed; the interior poles minimise a bending energy on the control
  net (second divided differences over the Greville abscissae in u, v and the mixed term, so a flat rim gives an
  exactly flat sheet). One linear least-squares solve per round (Cholesky on the normal equations, x/y/z as three
  right-hand sides); G2 and guides are re-projected for three rounds.
- **Guides** (`--guides=a,b`) are interior interpolation conditions: samples of each guide are pulled onto the sheet at
  their closest (u,v).
- **Four rims → one untrimmed quad**; three, five or more rims (or `--star`) → N quads about a centre. The spokes
  carry a shared normal field, perpendicular to the spoke tangent, so both neighbouring quads honour it and the seams
  are tangent-continuous (0.06° across a hexagonal window; 2.5° on a strongly warped pentagon). For a window in a
  smooth skin the centre is placed on the skin itself; for walls meeting the fill at an angle it is lifted along
  their continuation.
- **Report** after every fill: quads, unknowns, worst rim normal angle (G1), worst curvature mismatch (G2), seam
  break, guide deviation and the sampled bending energy against the plain Coons fill.
- **Associative**: `RecipeOperation::FairPatch` stores rims with their continuity / tension / face, guides and options;
  the fingerprint includes the supports' poles, so cutting the window elsewhere or moving a guide rebuilds the fill.

```
fairpatch Windowed:e2 Windowed:e3 Windowed:e4 Windowed:e7 Windowed:e8 Windowed:e9 --g2 --name=Window
fairpatch Block:e4@f2 Block:e5@f5 Block:e6@f3 Block:e7@f4 --g1 --guides=Crest --name=Pillow
fairpatch L1 L2@g2@t0.5 L3 L4 --g1 --on=Tube        # sketch curves, G1 from a named surface, one rim G2 with tension 0.5
fairpatch Bowl:e1@f0 --g2                              # one closed edge, quartered; closes a cut sphere G2
```

`Scripts/Phase9b_FairPatch.arc` → `Proofs/Phase9b_FairPatch_{Iso,Window,Pillow}.png`: a box-shaped window cut through a
drum filled G0 / G1 / G2 (radial deviation from the drum 5.4 % → 0.7 % → 0.3 %; G2 rim curvature mismatch 0.13 of 0.5),
a pillow over a box that leaves every wall vertically and passes through a guide crest (deviation 1 mm), five free
sketch splines filled N-sided, a cut sphere closed G2 (rim normal error 0.005°), and the guide moved with the pillow
following. `FairPatchVerification` (47 checks) measures all of it: planar rims give an exactly planar sheet, G1/G2 rim
angles < 0.5° against a cylinder, tension monotone, guide within 2 mm, star / N-sided seams, refusal of open rings,
and the console verb's per-rim tags, `--on`, `--guides`, undo and regeneration.

## Suite, contact sheet and Vulkan hand-off (Phase 10)

The closing phase is the meta-test: prove the toolchain end to end and write the spec for the GPU port.

**Suite script.** `Scripts/Phase10_Suite.arc` builds one document with a contribution from every phase (sketch,
primitives, profile algebra + areas, loft / sweep / pipe, four booleans, a FairPatch drum window) and renders one
`1280 × 800` iso image, `Proofs/Phase10_Suite.png`. If anything in the kernel is broken the suite image shows it.

**Contact sheet.** `Scripts/Phase10_ContactSheet.arc` composes a single `2560 × 1600` PNG from four separately rendered
`1280 × 800` tiles (top / front / right / iso), each its own fresh scene. The mechanism is a new sub-verb of `render`:

```
render sheet 0                          # capture the current raster as tile 0 (top-left)
render sheet 1                          # ... 1 = top-right
render sheet 2                          # ... 2 = bottom-left
render sheet 3                          # ... 3 = bottom-right
render sheet finalize Phase10_Contact   # 2x2 composite, writes Proofs/<name>.png
```

The compositor lives in `Console/ConsoleHost.cpp` (alongside `render`); it uses the same in-tree PNG writer
(`Presentation/SoftwareRaster::WritePng`) so the sheet has no external dependency. Four `RasterImage` tiles are
captured in `ConsoleHost::SheetTiles[N]`, the script clears the live scene with the new `reset` verb, builds the next
tile's scene, and `finalize` walks the four buffers into a single 2×2 RGBA8 image with a 1 px divider at the inner
edge. `docs/CONTACT_SHEET.md` is the design doc.

**Vulkan hand-off.** `docs/HANDOFF_VULKAN.md` is the contract for the next `VulkanRaster` that will sit next to
`SoftwareRaster` and implement the same `RasterExchange`. The verbs the seam already speaks (one `Begin/End` per
target, one `BindView` per frame, draw-record per draw, three sub-passes for lattice / opaque / overlay, a separate
`R32_UINT` pick attachment, ten matcap studios) are mapped to a single render pass with three sub-passes, one
staging buffer per `Begin/End`, an instance-rate draw record (push constant) and the four `.spv` outputs from the
existing `.slang` sources. The acceptance criterion is a `RendersEqual` check: same scene, same view, SoftwareRaster
vs VulkanRaster, PNG hashes within 1 LSB / channel.

**Regression net.** `SuiteVerification` (65 checks) re-runs every per-phase `.arc` script, asserts each terminates
without refusal, decodes the resulting PNG to confirm the `IHDR` is `1280 × 800` `RGBA8` and the file is non-empty,
runs the Phase 10 suite + contact sheet, and finally drives a `ConsoleHost` directly to confirm the new `render
sheet` / `reset` / `recipe` verbs exist and refuse garbage. It is the single executable that proves the console,
the scene, the kernel and the raster still all agree after every commit.

ctest now registers **69 suites** — 56 per-feature verification binaries (2,018 checks total) and 13 script smoke
tests. The Phase 32z direct C++ verifier sweep is green, including `DimensionVerification`; the per-suite check counts
are:

| Suite | Checks |
|---|---|
| `KernelVerification`              | 90  |
| `InteractionVerification`         | 54  |
| `SelectionVerification`           | 35  |
| `RasterVerification`              | 24  |
| `TopologyVerification`            | 79  |
| `ProfileVerification`             | 103 |
| `ProfileAdversarialVerification`  | 41  |
| `SkinVerification`                | 48  |
| `IntersectionVerification`        | 47  |
| `BooleanContactVerification`      | 31  |
| `BooleanIdentityVerification`     | 26  |
| `BooleanEquivalenceVerification`  | 17  |
| `BooleanCylinderSeamVerification` | 18  |
| `CylinderChamferVerification`     | 19  |
| `CylinderFilletVerification`      | 22  |
| `CylinderPushVerification`        | 19  |
| `CylinderSidePushVerification`    | 15  |
| `ConeSidePushVerification`        | 10  |
| `ConeCapPushVerification`         | 19  |
| `PlaneCylinderFilletVerification` | 24  |
| `TangentChainFilletVerification`  | 28  |
| `OpenChainFilletVerification`     | 30  |
| `MultiEdgeFilletVerification`     | 28  |
| `SectorEndpointFilletVerification`| 33  |
| `CornerFilletVerification`        | 23  |
| `RoundedBoxFilletVerification`     | 19  |
| `RoundedPrismFilletVerification`   | 20  |
| `PerforatedPrismFilletVerification`| 20  |
| `OffsetBorePrismFilletVerification`| 21  |
| `TwinBorePrismFilletVerification`  | 21  |
| `MultiBorePrismFilletVerification` | 21  |
| `BlindBorePrismFilletVerification` | 21  |
| `DualBlindBorePrismFilletVerification` | 26  |
| `MultiBlindBorePrismFilletVerification` | 31  |
| `SteppedBlindBorePrismFilletVerification` | 25  |
| `MultiStageBlindBorePrismFilletVerification` | 33  |
| `DualSteppedBlindBorePrismFilletVerification` | 27  |
| `SideBlindBorePrismFilletVerification` | 27  |
| `DualSideBlindBorePrismFilletVerification` | 32  |
| `MultiSideBlindBorePrismFilletVerification` | 33  |
| `SideSteppedBlindBorePrismFilletVerification` | 31  |
| `MultiStageSideBlindBorePrismFilletVerification` | 42  |
| `DualSideSteppedBlindBorePrismFilletVerification` | 40  |
| `MultiSideSteppedBlindBorePrismFilletVerification` | 41  |
| `MixedStageSideBlindBorePrismFilletVerification` | 42  |
| `PlaneConeFilletVerification`     | 61  |
| `FairPatchVerification`           | 47  |
| `BodyOpsVerification`             | 25  |
| `BlendVerification`               | 33  |
| `ArrayAndBridgeVerification`      | 67  |
| `DimensionVerification`           | 101 |
| `DocumentVerification`            | 38  |
| `SubEntityDimensionVerification`  | 34  |
| `ConstraintVerification`          | 51  |
| `MirrorVerification`              | 40  |
| `SuiteVerification`               | 65  |
| **Total** | **2018** |

Phase 10 also adds two new console verbs that the other phases do not need: `reset` (clears the scene + undo +
workplane + the contact-sheet tile buffer) and `render sheet <0|1|2|3> / render sheet finalize <name>` (the contact
sheet compositor described above).

## True NURBS booleans — surface–surface intersection on the B-rep (Phase 9)

`Kernel/IntersectionSolver.{h,cpp}` intersects and combines closed solids on their exact NURBS faces — nothing drops to
polygons except the seeding step:

1. **Seed** — coarse tessellations of the trimmed faces are intersected triangle against triangle to find where face pairs
   meet at all.
2. **March** — from each seed the true curve is traced: a predictor step along `Na × Nb`, then Newton on the two tangent
   planes plus the step plane, with `(u,v)` on **both** surfaces carried along. Step size adapts to the turning angle.
3. **Exit on edges** — when the trace leaves a face in its `(u,v)` domain, the exact point is solved as
   *edge curve ∩ other surface* (3×3 Newton), recorded once as an **exit**, and the march continues on the neighbouring face
   through the shared edge. Pieces therefore begin and end on real edges, and both bodies agree on those points to
   `KernelTolerance`. Seams of closed surfaces (cylinder, sphere, torus) are ordinary edges here.
4. **Fit** — each piece becomes a cubic interpolant; chords that stray more than 2 µm from either surface are refined.
5. **Split** — each face is a planar arrangement in its own `(u,v)` domain: trimming loops (split at exits) plus cut
   curves in both directions. The left-face walk (`PlanarCells`) yields the face pieces, holes attached to their cell.
6. **Classify** — a piece touching a cut is inside the other body iff its inward direction across the cut opposes the
   other face's normal (local, exact). Untouched pieces follow by flooding across shared edges, whole untouched hulls
   by a three-ray parity vote.
7. **Assemble** — union / subtract / intersect keep the right pieces (subtraction flips the tool's), share edges and
   vertices (`AddEdge` merges), and store the `(u,v)` trace on every coedge. The result is again a closed, manifold,
   consistently wound body; `Validate()` checks it, `Orient()` only runs if a mismatch is reported.

Topology gained what trimmed curved faces need: `BrepCoedge::Trace` (the `(u,v)` polyline), `CoedgeTrace()` (stored,
iso-side, or projected), and a `TessellateFace` for **trimmed curved faces** — the surface's own lattice is clipped by
the trimming rings (`PlanarCells` again), so a trimmed cylinder tessellates in ~1 ms with a few hundred triangles and the
volume quadrature matches the natural face.

Refusals are explicit rather than guessed: coincident or tangent faces, a curve through a sphere pole / cone apex, a
curve exactly through a vertex, sheets, empty results.

```
boolean subtract Brick Bore            true 3D boolean (bodies); still the 2D profile boolean for sketch curves
boolean union selected                 Q / Shift+Q / Ctrl+Q on two selected bodies
intersections Brick Bore --curves      SSI curve pieces (deviation, length), optionally added as sketch curves
boolean … --keep --verbose             keep the operands · trace the marching
```

`Scripts/Phase9_Booleans.arc` → `Proofs/Phase9_Booleans_{Iso,Top}.png`: box − cylinder (genus 1, 8 − π/2), box ∪ side
rod, sphere ∩ sphere lens (closed-form volume within 2e-3), box − torus through all four walls (10 curve pieces),
sphere scooping a box corner (curve crossing three faces), pipe tee. `IntersectionVerification` checks exact volumes
(box∪box = 15, box∩box = unit cube), inclusion–exclusion identities, genus, face winding of the bore wall and console
behaviour (undo, `--keep`, hotkeys).

## Loft, sweep, pipe, patch — derived figures that follow their sketch (Phase 8)

`Kernel/SkinSolver.{h,cpp}` skins exact NURBS over curves:

- **Loft** — sections are harmonised first: same sense (loop normals), seams rotated to the least twist (exact split +
  join at the best of the knot breaks / closest point / 24 trial seams), common degree and merged knots; then interpolated
  across in V (homogeneous, shared chord-length parameters, so circles stay circles). Closed sections → capped solid;
  `Outer+Hole` groups or filled areas with holes → solid with through-holes (hole sheets face inward); `--loop` closes the
  loft back onto its first section (four circles round a ring → genus-1 torus-like body); `--sheet` keeps the skin open.
- **Sweep** — rotation-minimising frames (double reflection), or `--bases=frenet|fixed`, along any curve or body edge;
  `--scale`, `--twist`, `--stations`. A square along a line is an exact prism (1.08 = 0.36·3); a circle along a quarter
  arc reproduces the quarter torus to 0.2 %. **Pipe** is a sweep of an exact circle.
- **Patch** — Coons blend over 3 or 4 boundaries in any order / sense (rational boundaries are refitted within
  tolerance, integral ones are exact); N ≥ 5 boundaries become N Coons quads meeting at a common centre with mirrored
  spoke tangents (Plasticity's xNURBS-style N-sided fill), delivered as one sewn sheet body; one closed curve is quartered.
- Fixed `NurbsCurve::Split` at an existing full-multiplicity knot (it used to return a one-pole piece).

**Recipes** (`Document/FigureRecipe.{h,cpp}`). Every extrude / revolve / loft / sweep / pipe / fillpatch result carries a
recipe: the operation, its options and the identities of its sources (curves, sketch areas by bounding curves + centroid
signature, body edges). The sources stay in the scene as ordinary curves. After every command the document regenerates
each recipe whose inputs' geometry fingerprint changed — move a section, pull a pole in edit mode, move the box whose
edge a pipe follows, and the result rebuilds. A recipe that can no longer be satisfied (a deleted source, a patch ring
that no longer closes) keeps its last geometry and reports a complaint; undo brings it back. `recipe` lists them,
`dependents <figure>` shows what follows a curve, `recipe bake` detaches a figure into plain geometry.

Console: `loft <sections…>|selected [--degree] [--loop] [--sheet] [--no-align]` (L), `sweep <profile> <path>` (Shift+P),
`pipe <path> r` (P), `fillpatch <boundaries…>` (Shift+L); sections are curves, `aN` areas, `Body:eN` edges or
`Outer+Hole` groups. Edit-mode G/R/S now moves only the selected poles. Script `Scripts/Phase8_Skins.arc`, proofs
`Proofs/Phase8_Skins_{Iso,Top}.png`, `SkinVerification` 48 checks.

## Sketch areas, bucket fill and through-holes (Phase 7b)

Sketch curves on the workplane are arranged into **closed areas** automatically (`Kernel/ProfileSolver::Cells`): every
crossing and T-junction splits the curves, dangling ends are pruned, and each bounded face of the arrangement is traced
once by the leftmost-turn walk — two overlapping squares give three areas (3, 1, 3), a circle cut by a line two half discs,
nested loops become areas with holes at depths 0/1/2. `SceneDocument::RebuildAreas` derives them after every command; the
only user-owned part is each area's **fill**, which survives rebuilds by centroid + area signature and takes part in undo.

- `areas` lists `aN`, fill, depth, area, holes, centroid and bounding curves; filled areas render as translucent sheets
  and are hoverable / clickable (`click`, `select a0 a3`) like any figure.
- `fill on|off|toggle <aN…>`, `fill all|none`, `fill at (x,y) [on|off]` — bucket fill: an area that is filled is material.
- `extrude` / `revolve` accept `aN` or a curve; a filled area (or a closed curve whose filled area it bounds) extrudes as a
  **solid with through-holes** (multi-loop caps, `BrepBody::Extrude/Revolve(loops, …)`, loop senses normalised, Euler
  `V − E + F − (L − F)` so a plate with one hole reports χ 0 / genus 1); an unfilled area or open curve yields sheets.
- Orthographic views hide the gizmo grips that cannot work along the view axis (`gizmo status` → `view along Z (only
  XY-plane move + Z rotate)`; `gizmo grips` prints `hidden (orthographic view along Z)`), and `AimAt(view)` restores the
  full rig as soon as the view is free.

Script `Scripts/Phase7b_Areas.arc`, proofs `Proofs/Phase7b_Areas_Top.png` / `Proofs/Phase7b_Areas_Iso.png`,
checks in `ProfileVerification` (103 in total).

## Planar profile algebra (Phase 7)

`Kernel/ProfileSolver.{h,cpp}` works on **closed planar NURBS loops** without dropping to polygons:

- `Intersect(A, B)` — Bézier-piece subdivision on pole hulls, then a Newton polish on both parameters; crossings land within
  1e-12 on both curves and tangent contacts are labelled. `SelfIntersections` uses the same machinery span against span.
- `SignedArea`, `Winding(P)` — Green's theorem / crossing count on a 1e-5-sagitta tessellation; counter-clockwise about the
  profile normal is material, clockwise is a hole, and a point is inside when its total winding is non-zero.
- `Assemble` validates closure, planarity, coplanarity and simplicity, then nests loops (depth 0 outer, 1 hole, 2 island …);
  `Normalised` fixes each loop's sense to its depth parity, so input winding never matters.
- `Combine(A, B, union | subtract | intersect)` splits every loop at true crossings, classifies each piece by the other
  profile's winding just left and just right of its midpoint (so coincident boundaries are kept exactly once), reverses the
  B pieces of a subtraction, and chains survivors by sharpest-left-turn. Circle pieces stay rational quadratics — the notch
  in a subtracted circle lies on the true circle to 1e-12.
- `Filleted` / `Chamfered` replace corners (all, or `--corners=i,j`) by exact tangent arcs / lines with arc-length setbacks;
  corners whose setbacks collide are left sharp. `Offset` shifts lines and arcs exactly (arcs change radius, circles remain
  exact circles), bridges convex corners with arcs and trims concave ones. `Trimmed` removes the piece nearest a point between
  crossings with the cutters; `Joined` chains pieces in any order or sense.

Console: `boolean union|subtract|intersect <A…> -- <B…> [--keep]` (Q / Shift+Q / Ctrl+Q on the selection), `profile`,
`intersections`, `fillet <curves> r`, `chamfer <curves> d`, `offset <curves> d [--copy]`, `trim <curve> (near) [--by=…]`,
`join`, `explode`. Results are ordinary curves, so they extrude / revolve into solids with holes.
