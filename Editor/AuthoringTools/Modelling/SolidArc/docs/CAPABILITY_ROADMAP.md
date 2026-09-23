# SolidArc capability roadmap — one validated capability at a time

This plan turns the current NURBS/B-rep prototype into a capable modelling tool without pretending that a closed body
or a rendered PNG proves an operation is production-ready. A phase is only complete when it has exact/analytic checks
where applicable, adversarial regressions, refusal behaviour for unsupported cases, and a reproducible visual proof.

## Completed first increment — Phase 22: native `.arc` documents

Implemented in this change:

- versioned, human-reviewable native construction documents (`save`, `save <path>`, `open`);
- extension enforcement and automatic `.arc` suffixing;
- same-directory temporary write, replacement backup (`.arc.bak`), and rename-on-success;
- transactional open: replay into a clean host, adopt only on a zero-refusal result;
- persistence of construction operations rather than display triangles, retaining the editable NURBS/B-rep and the
  existing live recipe/dimension/constraint systems;
- round-trip and deliberately-invalid-document regression coverage.

The architecture follows the important distinction between native procedural data and neutral B-rep exchange: the NIST
work on ISO 10303 explains why a parametric model needs construction history, parameters, constraints, and a secondary
B-rep validation representation rather than only its final boundary shape. [NISTIR 7433](https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=822720)

## Completed increment — Phase 23: adversarial 2D profile geometry

Implemented and verified in this change:

- `ProfileAdversarialVerification`, a 41-check kernel-facing regression suite, covers coincident portions, shared
  boundaries, exact tangency plus a ±0.0001 near-tangent separation, both closed interpolated and single-span cubic
  self-crossings, free-form spline/ellipse offsets, curvature-cusp refusal, and overlapping free-form Boolean results;
- `Scripts/Phase23_AdversarialProfiles.arc` and its committed 2560 × 1600 contact sheet reproduce the visual cases;
- `SelfIntersections` now detects a non-rational cubic Bézier loop that lies wholly inside one span, in addition to
  cross-span intersections;
- `Offset` rejects a self-intersecting source, detects sampled normal-offset curvature cusps before interpolation, and
  rejects a self-intersecting candidate result instead of returning a folded curve;
- the rational-quadratic exact-offset path now verifies that a span is circular. Ellipses therefore follow the
  distance-controlled free-form path rather than being replaced by osculating circular arcs. The covered ellipse case
  remains within 0.001 mm of its requested 0.2 mm normal distance.

The contact semantics are intentional. A coincident curve portion is a continuum, so the point-only `CurveCrossing` API
emits no fabricated isolated crossing for it; Boolean classification still handles coincident profiles and shared edges.
An external circle tangency emits one tangent contact, but its intersection profile is empty (zero area) and its union
keeps two simple touching components rather than constructing one self-touching loop.

Research informs the acceptance boundary rather than replacing validation: offset self-intersections are both local
(curvature) and global (distant portions collide), so sampling alone is not an adequate acceptance criterion. The
distance-map/trimming work by Seong, Elber, and Kim describes detecting and trimming both classes through
parameter-space zero sets and numeric marching. [Computer-Aided Design article](https://www.sciencedirect.com/science/article/abs/pii/S0010448505001491)

**Exit gate met:** every covered construction either meets its declared tolerance or refuses with a specific reason; the
suite also verifies that no accepted result loop self-intersects. This is a targeted planar-curve safety boundary, not a
claim of general offset-loop trimming or an interval-overlap contact API.

## Subsequent increments — 2D fixes, then general 3D blends

### Completed Phase 24: B-rep Boolean contact healing for axis-aligned boxes

The Phase 23 contact policy exposed the analogous 3D failure mode: a generic surface-intersection marcher has no unique
section curve for face-on-face coincidence or a zero-volume point/edge contact. It must not invent one or sew touching
solids into a non-manifold edge. This increment adds a narrow, structural constructive resolver before the general SSI
path when **both** operands prove to be natural six-plane, eight-corner axis-aligned boxes:

- face-touching and rectangularly overlapping boxes are rebuilt as one exact box;
- identical and contained boxes select the mathematically surviving box for union/common;
- a one-sided box slice is rebuilt exactly for subtraction;
- point-touching, edge-touching, and separated boxes return independent B-rep hulls with no topology weld;
- zero-volume common and complete subtraction are explicit `DegenerateInput` empty-result refusals;
- cavities and L-shaped differences retain the existing trimmed-face SSI path rather than being approximated as boxes.

`BooleanContactVerification` has 31 checks for these cases, including a real 0.0001 overlap that must not be mistaken
for contact, and creates `Proofs/Phase24_BooleanContacts.png` directly from C++ console commands. No HTML or browser
implementation is part of this capability.

The scope mirrors robust-kernel practice: Open CASCADE exposes a separate fuzzy tolerance for close/coincident Boolean
classification and explicit topology handling, rather than assuming every contact has a transversal section curve.
[OCCT Boolean options](https://dev.opencascade.org/doc/refman/html/class_b_o_p_algo___options.html)

**Exit gate met:** the covered contact cases are closed, manifold, consistently wound B-reps or explicit empty results;
edge and point contacts preserve two valid hulls instead of a four-coedge non-manifold edge. General curved or
non-transversal contact remains deliberately refused pending a separately validated contact classifier.

### Completed Phase 24b: exact duplicate B-rep Boolean identity

An exact duplicate valid B-rep is another non-transversal case that does not need a surface-intersection curve. The
Boolean kernel now compares the complete B-rep representation exactly: vertices, NURBS edges and face surfaces,
knots/poles, coedge traces, loops, orientation, and topology indices. When both input
solids are exact copies, union and common retain one operand, while subtraction returns an explicit empty-result
refusal. The comparison intentionally has **no fuzzy tolerance**: near-coincident shapes are not silently merged and
remain in the bounded general contact path.

`BooleanIdentityVerification` has 26 C++ checks covering copied and independently rebuilt spheres, cylinders, tori,
and extrudes; an exact copy of a trimmed sphere-union result; a near-coincident refusal; and a distinct crossing pair
that still takes SSI. It generates `Proofs/Phase24b_BooleanIdentity.png` directly from C++ console commands, with no
HTML or browser code.

This is aligned with the robust-kernel distinction between full coincidence (which should not be split) and partial
or near coincidence (which needs interference classification). [OCCT Boolean Operations](https://occt3d.com/dev/doc/overview/html/specification__boolean_operations.html)

**Exit gate met:** exactly identical valid B-reps no longer enter the non-transversal marcher; topology-rich trimmed
copies are covered; nearby but distinct bodies are demonstrably not mistaken for identity. At this stage, geometric
equivalence across reparameterized or reordered B-reps remained outside the identity gate.

### Completed Phase 24c: affine-NURBS-equivalent Boolean identity

A native cylinder and the extrusion of the same full circle are geometrically identical, yet their side-face and seam
NURBS use different affine parameter intervals and a different analytic classification hint. The identity comparator now
normalizes only affine knot domains while requiring every homogeneous control point and every topology field to match
exactly. This lets construction-independent but parameter-affine-equivalent B-reps take the same zero-section Boolean
path. It does **not** use a spatial merge tolerance, reorder faces/edges, reverse parameter directions, or claim general
shape equivalence.

`BooleanEquivalenceVerification` has 17 C++ checks for cylinder versus circular extrusion in both operand orders, an
explicitly reparameterized circle, a 0.0001-radius near miss that must not pass, and an ordinary sphere/sphere SSI case.
It generates `Proofs/Phase24c_BooleanEquivalence.png` directly from C++ console commands; no HTML or browser work is
included.

**Exit gate met:** full coincident geometry generated through two construction paths is returned as one valid solid or
an explicit empty subtraction; a geometric near miss remains outside the identity gate. Reversed/reordered topology and
partial coincidence remain future correspondence/contact-classification work.

### Completed Phase 24d: seam-invariant full right-cylinder Boolean identity

A periodic circular seam is a parameterization/topology choice, not a material intersection. Two equal cylinders can
therefore have different seam vertices, different full-circle start angles, or opposite construction directions even
after affine knot normalization; the generic SSI marcher then sees coincident faces and correctly declines to fabricate
a section. This increment recognizes only validated closed right cylinders with two classified full-circle caps, one
classified straight seam, two planar caps, and one cylinder/circular-extrusion side. It derives and canonically signs
the physical axis, base, radius, and height, then applies the identity Boolean result only when those values agree at
kernel-scale numerical noise.

`BooleanCylinderSeamVerification` has 18 C++ checks covering a native cylinder versus a 60° seam-shifted circular
extrusion, top-down construction, two independent seam positions, radius/height near misses, and a perpendicular
crossing-cylinder SSI control. It generates `Proofs/Phase24d_BooleanCylinderSeams.png` from C++ console commands; no
HTML or browser work is included.

Periodic parameterization is an explicit Boolean limitation in mature-kernel documentation, and their algorithms reuse
existing topology rather than manufacture duplicate section curves at coincident entities.
[OCCT Boolean parameterization limits](https://dev.opencascade.org/doc/overview/html/specification__boolean_operations.html)

**Exit gate met:** fully coincident, seam-relocated right cylinders produce exactly one valid operand for union/common
or an explicit empty subtraction; any physical radius/height change remains outside the gate. Cones, partial cylinders,
and non-circular periodic NURBS remain on the regular contact/SSI path.

### Completed Phase 25: exact circular right-cylinder cap chamfer

A complete circular cap rim of the native right-cylinder B-rep is a curved edge, so the planar prism-cutter path is
incorrect for it. `BlendSolver::ChamferEdge` now has a deliberately structural route for that one topology: two planar
caps, one classified cylinder side, two rational quadratic circular rims, one straight seam, and `V2/E3/C6/L3/F3` before
the operation. It keeps the cylinder over `H−s`, attaches an exact rational conical frustum over `s`, and sews the two
surfaces and their planar caps. This gives an exact radial-and-axial set-back and a valid `V3/E5/F4` result without a
Boolean cut or faceting. The route works on either cap and arbitrary non-unit construction axes; set-backs that reach
the axis or consume the height refuse.

`CylinderChamferVerification` supplies 19 C++ checks: top/bottom, an oblique axis, validity/topology, an exact sampled
cone generatrix, explicit circular-extrusion scope refusal, feasibility refusals, and the console command. It generates
`Proofs/Phase25_CylinderChamfers.png` directly from C++ commands with no HTML/browser component.

**Exit gate met:** the supported circular cap has exact conic geometry rather than an approximate cutter result. This is
not general curved-edge blending: circular-extrusion topology, partial cylinders, cones, and arbitrary NURBS edges still
refuse until independently implemented and verified.

### Completed Phase 26: exact circular right-cylinder cap rolling-ball fillet

A circular cap rim of the same tightly verified native-cylinder topology now has a constant-radius rolling-ball solution.
`BlendSolver::FilletEdge` creates the meridian as an exact rational quarter circle and revolves it around the cylinder
axis. The resulting surface is recorded as the appropriate **partial torus** (`Rmajor = R−r`, `Rminor = r`), then sewn
to the retained cylinder and planar caps; it is a closed `V3/E5/F4` B-rep. Its two endpoints are respectively tangent
to the cylinder and cap, so the feature has measured G1 joins instead of merely a rounded-looking tessellation. It works
at either cap, with a non-unit or reversed construction direction; a radius reaching the axis or full height is refused.

`CylinderFilletVerification` has 22 C++ checks for exact torus samples, G1 normals at both joins, topology/solidity,
top/bottom, oblique/reversed construction, explicit circular-extrusion rejection, feasibility bounds, the analytic
volume observation, and console integration. It generates `Proofs/Phase26_CylinderFillets.png` directly from C++ commands
(no HTML or browser implementation).

The bounded feature follows established solid-modelling fillet semantics: production kernels attach a constant radius to a selected
edge/contour and track its continuity to support faces.
[OCCT constant-radius fillet API](https://dev.opencascade.org/doc/refman/html/class_b_rep_fillet_a_p_i___make_fillet.html)

**Exit gate met:** a supported circular rim has a true rational quarter-torus and measured G1 joins. Partial rims,
circular-extrusion topology, cones, plane–cylinder/cylinder–cylinder intersections, and arbitrary NURBS supports still
refuse rather than being treated as this primitive case.

### Completed Phase 27: exact native-cylinder cap face push

A planar face push must move the chosen cap along its **outward normal**, not construct a tangential Boolean tool at the
periodic cylinder wall. `BlendSolver::PushFace` now recognizes a planar cap from the same structural native right-cylinder
shape and reconstructs it as `Cylinder(R, H+d)`. An upper-cap push retains the canonical base; a lower-cap push shifts
that base by `−axis·d`, so positive values extend material outward at either end and negative values reduce height.
The result retains the exact native `V2/E3/C6/L3/F3` topology with no coincident SSI/contact dependency.

`CylinderPushVerification` has 19 C++ checks for upper/lower outward/inward moves, sampled exact cylinder geometry,
oblique and reversed construction, refusal of the radial side or an over-collapse, and the console command. Its direct
C++ proof commands create `Proofs/Phase27_CylinderPushes.png`, with each pushed result shown beside its unchanged
reference cylinder.

**Exit gate met:** selected native-cylinder caps have exact direct face offsets and correct outward direction. Radial
side-face offsets, circular-extrusion topology, partial cylinders, and arbitrary curved-face modifications remain out
of scope and refuse/retain the established generic route.

### Completed Phase 28: exact native-cylinder radial side-face push

The cylindrical side is a curved direct-modelling face: its normal offset is exactly a radius edit, not a planar push.
`BlendSolver::PushFace` now recognizes only the checked native right-cylinder side and rebuilds
`Cylinder(R+d, H)`. Its axial extent, two cap planes, and axis remain exact; positive `d` adds material outward and
negative `d` removes material inward. A non-positive resulting radius explicitly refuses.

`CylinderSidePushVerification` has 15 C++ checks for outward/inward exact sampled radii, oblique and reversed
construction, collapse/zero refusal, exclusion of the topologically distinct circular extrusion, and console use. It
creates `Proofs/Phase28_CylinderSidePushes.png` directly from C++ commands, including side-by-side reference/offset
pairs plus a top-view radius comparison.

**Exit gate met:** the supported cylindrical surface offsets exactly, preserving valid native cylinder topology. This
is not a generic surface offset: partial cylinders, extrusion representations, cones, and freeform faces do not enter
the route.

### Completed Phase 29: exact native-cone radial side-face push

`PushFace` recognizes a checked full native conical-frustum side. A normal offset by `d` reconstructs the exact cone with
both cap radii shifted by `d·sqrt(1+slope²)`, preserving axial height, cap planes, and axis; either cap collapsing refuses.
The structural gate checks the rational circular rims, straight seam, planar caps, and sampled linear conical support.
`ConeSidePushVerification` has 10 C++ checks and creates `Proofs/Phase29_ConeSidePushes.png` directly from C++.

**Exit gate met:** normal side offsets are exact for full native frusta. Apex cones, partial cones, and arbitrary curved
faces remain outside this route.

### Completed Phase 30: exact native-cone cap-face push

Either planar cap of the same full native frustum now moves along its geometric outward normal while retaining the
original infinite conical support. The direct route analytically updates the base, height, and selected radius. Native
recognition derives the two physical end rings from the NURBS rather than assuming positive construction height, so a
negative-height cone is canonicalized low-to-high and takes the same exact cap and side routes. Expanding and tapering
frusta are both covered; consuming the height or continuing through an apex refuses.

`ConeCapPushVerification` has 19 C++ checks for upper/lower inward/outward motion; exact base, axis, radii, height, cap
planes, sampled support, topology, and volume; non-unit oblique and negative-height construction; expanding/tapering
slope signs; height/radius-collapse and apex refusals; exact console commit; and a direct C++ proof render at
`Proofs/Phase30_ConeCapPushes.png`.

**Exit gate met:** both cap planes of a full native frustum follow the same exact conical support in either construction
direction. Apex cones, partial/trimmed cones, and arbitrary free-form faces remain deliberately unsupported.

### Phase 31: first general smooth-support fillet ✅

The bounded five-face stepped solid is now recognized structurally when the selected circular inner shoulder rim joins a
planar annulus to a native cylindrical boss. Its two radius-offset supports intersect in an exact circular spine at
`boss radius + r`, one radius above the shoulder plane. Reconstruction retains the outer cylinder, trims the annular
shoulder, inserts a rational quarter-torus, shortens the boss cylinder, and sews both caps into a closed solid.

Delivered:

1. support/edge classification derived from NURBS geometry and B-rep adjacency rather than face order;
2. exact plane and cylinder offsets with an analytic circular spine;
3. an exact rational rolling-ball torus retaining torus analytic identity;
4. sampled implicit-radius residual plus exact G1 normal checks at both contact circles;
5. explicit feasibility interval `0 < r < min(boss height, outer radius − boss radius)`;
6. clean refusal of the opposite shoulder rim, unsupported topologies, and consumed supports;
7. unchanged dispatch to the established native-cylinder cap and straight-planar routes.

**Exit gate met:** `PlaneCylinderFilletVerification` proves a `V5/E9/C18/L6/F6` closed manifold, exact retained support
extents, torus residual below `1e-9`, G1 breaks below `1e-10`, positive/analytic volume change, oblique and reversed
axes, bounded refusals, legacy cylinder-cap dispatch, and exact console commit.

**Proof:** `Verification/PlaneCylinderFilletVerification.cpp` (24 C++ checks) and
`Proofs/Phase31_PlaneCylinderFillet.png` (2560 × 1600 C++-generated contact sheet).

### Phase 32: blend chains and corners — in progress

#### Phase 32a: closed representation-split tangent chains ✅

`BlendSolver::TangentChain` now follows unambiguous G1 continuations from one manifold seed and refuses a branching
choice instead of depending on edge-table order. The first consuming operation is deliberately bounded: a Phase 31
boss-root ring may be represented by two or four angular NURBS support patches rather than one closed edge. The
classifier proves that the propagated rational arcs close through `2π`, that the boss/shoulder/outer-wall patches share
one axis and exact dimensions, and that the source has the complete `V=4N, E=7N, C=14N, L=3N+2, F=3N+2` topology.
It then heals the representation seams by rebuilding the same canonical exact quarter-torus result.

**32a exit gate met:** `TangentChainFilletVerification` proves seed independence, two- and four-member propagation,
singleton closed-edge behavior, exact `V5/E9/C18/L6/F6` output, torus residual below `1e-9`, both G1 breaks below
`1e-10`, support/contact healing, analytic volume, oblique axes, bounded refusals, and exact console commit.

**Proof:** `Verification/TangentChainFilletVerification.cpp` (28 C++ checks) and
`Proofs/Phase32a_TangentChainFillet.png` (2560 × 1600 C++-generated contact sheet).

#### Phase 32b: finite semicircular chain endpoints ✅

The first endpoint-aware chain is deliberately exact and bounded: a semicircular stepped boss is cut by one planar
diameter face, and its root may contain one, two, or four rational arc members. The classifier requires two chain
endpoints, total angular span `π`, matching partial plane/cylinder supports, bottom/top sector patches, and the source
formula `V=4N+6, E=9N+5, C=18N+10, L=5N+1, F=5N+1`. Positive and negative half-turns select the correct side of the
diameter plane. Reconstruction heals only internal representation seams while preserving two exact torus meridians and
the diameter cap as a one-hull, genus-zero `V12/E17/C34/L7/F7` solid.

**32b exit gate met:** `OpenChainFilletVerification` proves two/four-member propagation and endpoint degree, seed
independence, exact torus residual below `1e-9`, both G1 breaks below `1e-10`, support/contact/end-cap retention,
analytic half-volume, oblique axes, reversed sweep, bounded refusals, native-cylinder regression, and console commit.

**Proof:** `Verification/OpenChainFilletVerification.cpp` (30 C++ checks) and
`Proofs/Phase32b_OpenChainFillet.png` (2560 × 1600 C++-generated contact sheet).

#### Phase 32c: intentional multi-edge selection ✅

`BlendSolver::FilletEdges` adds an all-or-nothing layer over the bounded exact fillet routes. It validates all seed
indices and the source solid before construction, expands each seed through `TangentChain`, and deduplicates repeated
indices or multiple members of the same chain. The remaining vertex-disjoint chains are ordered by their sampled
three-point geometric signatures; each later target is matched against the changed B-rep by the same orientation-free
signature instead of an old edge index or midpoint alone. A missing or ambiguous target refuses the complete operation.

Interacting chains that share a source vertex are classified up front as a corner request and refuse before either roll.
That boundary is intentional: sequentially applying two edge rolls does not construct the required three-face corner
patch. The console now dispatches one body-edge selection through this transactional API and reports requested seeds and
committed chains rather than retaining a partially modified intermediate body.

**32c exit gate met:** `MultiEdgeFilletVerification` proves two independent opposite box rolls with exact
`V12/E18/C36/L8/F8` topology, summed analytic volume, input-order invariance, source immutability, duplicate and
two/four-member chain deduplication, empty/invalid/oversize/mixed/corner refusal, console scene rollback, and direct C++
proof generation.

**Proof:** `Verification/MultiEdgeFilletVerification.cpp` (28 C++ checks) and
`Proofs/Phase32c_MultiEdgeFillet.png` (2560 × 1600 C++-generated contact sheet).

#### Phase 32d: general-angle radial endpoint pairs ✅

The finite plane–cylinder route now measures an open chain's ordered signed sweep rather than assuming `±π`. Its source
gate recognizes the bounded rotational sector topology: matching shoulder, boss, and outer-wall angular spans; planar
bottom/top patches; and two radial caps sharing one physical rotation-axis edge. The cap support planes must actually
contain their corresponding chain endpoint rays. A merely topological face with displaced support refuses.

Reconstruction revolves the exact outer wall, trimmed shoulder, rational quarter-torus meridian, shortened boss, bottom,
and top through the measured sweep. Non-half-turn results explicitly close the two radial endpoint paths against one
axis edge. Internal two/four-member representation seams heal while the two physical torus meridians remain. Both
positive and negative quarter turns, 120° sectors, reflex 270° sectors, and shifted oblique axes retain the exact
`V12/E18/C36/L8/F8` one-hull, genus-zero result.

**32d exit gate met:** `SectorEndpointFilletVerification` proves source topology and endpoint degree, seed/split/sweep
invariance, exact partial-torus identity and angular span, implicit residual below `1e-9`, both G1 breaks below `1e-10`,
two radial caps and one axis edge, exact end meridians, angular-fraction volume, transformed axes, transactional chain
deduplication, malformed-cap and consumed-support refusal, console commit, and direct C++ proof generation.

**Proof:** `Verification/SectorEndpointFilletVerification.cpp` (33 C++ checks) and
`Proofs/Phase32d_SectorEndpointFillet.png` (2560 × 1600 C++-generated contact sheet).

#### Phase 32e: exact orthogonal three-face corner ✅

Three equal-radius, mutually perpendicular box edges meeting at one vertex now take a dedicated transactional route. The classifier proves a six-plane rectangular solid from geometry, not edge numbering, then rebuilds six trimmed planes, three exact cylindrical rolls, and one rational spherical octant. The three sphere-cylinder seams are G1 and the far ends remain exact circular arcs.

**32e exit gate met:** `CornerFilletVerification` proves `V13/E21/C42/L10/F10` manifold topology, analytic support identity, sphere residual below `1e-9`, three G1 transition seams below `1e-10`, volume, source immutability, seed-order and rigid-transform invariance, bounded refusal, console commit, and direct C++ proof generation.

**Proof:** `Verification/CornerFilletVerification.cpp` (23 C++ checks) and `Proofs/Phase32e_CornerFillet.png`.

#### Phase 32f: complete rounded rectangular edge network ✅

Selecting all twelve edges of a verified rectangular solid composes the bounded corner solution globally: six inset planar faces, twelve exact equal-radius cylinders, and eight rational spherical octants sew directly to one `V24/E48/C96/L26/F26` solid. The route is deterministic under seed duplication/order and rigid transforms, verifies the exact rounded-box volume formula, and refuses incomplete interacting networks or radii that consume an inset face.

**Proof:** `RoundedBoxFilletVerification` (19 C++ checks) and `Proofs/Phase32f_RoundedBox.png`.

#### Phase 32g: exact parallel-edge families and cross-wall feasibility ✅

All four mutually parallel edges of a verified rectangular solid now rebuild together as one rounded prism. Four retained planes and four exact cylinders close against two planar rounded end caps at `V16/E24/C48/L10/F10`. A global `2r < min(cross-section dimensions)` gate refuses thin-wall collapse before construction. All three box directions, seed deduplication/order, and rigid transforms are verified.

**Proof:** `RoundedPrismFilletVerification` (20 C++ checks) and `Proofs/Phase32g_RoundedPrism.png`.

#### Phase 32h: one coaxial hole through a rounded prism ✅

The complete four-edge outer route now recognizes the bounded `V10/E15/C30/L9/F7` rectangular extrusion with exactly one centred, coaxial circular through-hole. It retains the bore as a reversed exact rational cylinder while rebuilding the rounded exterior; sewing therefore makes two annular end caps and one genus-one `V18/E27/C54/L13/F11` hull. The classifier checks source topology, circular equality/coaxiality, analytic perforated volume, centred placement, and positive radial wall clearance before committing. Ordering, duplication, rigid transforms, refusal, and console use remain transactional.

**Proof:** `PerforatedPrismFilletVerification` (20 C++ checks) and `Proofs/Phase32h_PerforatedPrism.png`.

#### Phase 32i: one offset axis-parallel through-hole ✅

The single-bore classifier now retains the exact circular cylinder away from the rectangular centreline when its axis remains parallel to the selected outer-edge family. Clearance is evaluated against the inward offset of the final rounded cross-section rather than only the source box: smaller bores use a reduced-radius rounded-rectangle centre domain, while bores at least as large as the outer roll use strict retained-wall distances. Safe corner-adjacent placement keeps canonical `V18/E27/C54/L13/F11` genus-one topology; side/corner overlap refuses transactionally before generic fallback.

**Proof:** `OffsetBorePrismFilletVerification` (21 C++ checks) and `Proofs/Phase32i_OffsetBorePrism.png`.

#### Phase 32j: exactly two separated axis-parallel through-holes ✅

Canonical perforated-extrusion classification now pairs two lower and two upper circular rims geometrically, independent of profile-loop order. Each bore passes the Phase 32i rounded-wall offset test and their centre distance must exceed the sum of their radii by merge tolerance. Two reversed rational cylinders sew into end caps with three loops each and canonical one-hull, genus-two `V20/E30/C60/L16/F12` topology.

**Proof:** `TwinBorePrismFilletVerification` (21 C++ checks) and `Proofs/Phase32j_TwinBorePrism.png`.

#### Phase 32k: bounded multi-bore rounded prisms ✅

The same structural classifier and exact builder now scale to three through eight separated axis-parallel bores. For `N` holes, the source and result obey explicit linear topology formulae, both end caps gain `N+1` loops, and the result genus is exactly `N`. Every bore passes the rounded-wall erosion gate and all `N(N-1)/2` pairs retain positive merge-tolerance ligaments. Three- and eight-bore constructions, profile ordering, rigid transforms, collision/wall refusal, and the explicit nine-hole cap are verified without claiming unrestricted perforated profiles.

**Proof:** `MultiBorePrismFilletVerification` (21 C++ checks) and `Proofs/Phase32k_MultiBorePrism.png`.

#### Phase 32l: one axis-parallel blind cylindrical cavity ✅

A canonical genus-zero rectangular prism with one finite-depth cylindrical cavity entering either end now takes a dedicated route. The classifier derives the outer frame from the selected rails, verifies seven planes plus one reversed cylinder, extracts entry/depth from both closed rims, and checks analytic cavity volume. The builder rounds the outer prism, subtracts the bounded exact cylinder, then replaces the fitted Boolean entrance intersection with its exact rational circle. The result is one `V18/E27/C54/L13/F12` hull with seven planes, five cylinders, one annular entrance cap, and a planar cavity floor. Offset placement, both entry directions, transforms, wall refusal, and delegation of an orthogonal side-axis cavity to Phase 32r are verified.

**Proof:** `BlindBorePrismFilletVerification` (21 C++ checks) and `Proofs/Phase32l_BlindBorePrism.png`.

#### Phase 32m: exactly two separated axis-parallel blind cavities ✅

The bounded blind-cavity classifier accepts canonical `V12/E18/C36/L12/F10` sources containing two inward finite cylinders. Each entrance/floor pair is recovered independently; the pair may enter one shared end or opposite ends. Clearance is measured between finite cylinders by combining transverse disk separation and bounded axial-interval separation, so same-end radial separation, opposite-end combined separation, and coaxial cavities across a positive axial ligament are all handled honestly. The rounded result has exact `V20/E30/C60/L16/F14` genus-zero topology, eight planes, six rational cylinders, and four exact rational cavity rims. Intersections and wall crossings refuse without partial application.

**Proof:** `DualBlindBorePrismFilletVerification` (26 C++ checks) and `Proofs/Phase32m_DualBlindBorePrism.png`.

#### Phase 32n: bounded multi-blind-cavity rounded prisms ✅

The exact finite-cylinder route now scales to three through eight separated blind cavities entering either prism end. For `N` cavities, source topology is `V=8+2N`, `E=12+3N`, `C=24+6N`, `L=6+3N`, `F=6+2N`; rounded topology is `V=16+2N`, `E=24+3N`, `C=48+6N`, `L=10+3N`, `F=10+2N`, with genus zero. Every cavity independently passes the rounded-wall gate, every pair retains positive finite-cylinder distance, and all `2N` rims are exact rational circles after deterministic sequential reconstruction. Three- and eight-cavity cases, both entry ends, ordering, transforms, wall refusal, the explicit nine-cavity cap, and the prior one/two routes are verified.

**Proof:** `MultiBlindBorePrismFilletVerification` (31 C++ checks) and `Proofs/Phase32n_MultiBlindBorePrism.png`.

#### Phase 32o: one coaxial two-diameter stepped blind cavity ✅

One canonical counterbore may enter either selected prism end. Its larger cylinder terminates at an annular shoulder and its smaller coaxial cylinder continues to a deeper planar floor. The structural classifier distinguishes these connected spans from two independent cavities, requires strict radius decrease and depth increase, and verifies the two-stage analytic volume. Deterministic reconstruction subtracts the shallow outer stage before the deep inner stage and restores all four entrance/shoulder/floor rims as exact rational circles. The result is genus-zero `V20/E30/C60/L16/F14`, with eight planes and six rational cylinders. Safe offsets, transforms, opposite entry, wall refusal, and eccentric-stage refusal are covered.

**Proof:** `SteppedBlindBorePrismFilletVerification` (25 C++ checks) and `Proofs/Phase32o_SteppedBlindBorePrism.png`.

#### Phase 32p: bounded multistage coaxial blind cavities ✅

The connected-span route now supports one chain of three through eight strictly decreasing coaxial diameters. It discovers the unique entry span, orders every subsequent cylinder through matching shoulder planes, rejects branches or eccentricity, and verifies the sum of every finite axial-band removal. For `N` stages, source topology is `V=8+2N/E=12+3N/C=24+6N/L=6+3N/F=6+2N`; rounded topology is `V=16+2N/E=24+3N/C=48+6N/L=10+3N/F=10+2N`, with genus zero and `2N` exact rational rims. Three/eight stages, both ends, offsets, ordering, transforms, wall/eccentric refusal, the nine-stage cap, and two-stage regression are verified.

**Proof:** `MultiStageBlindBorePrismFilletVerification` (33 C++ checks) and `Proofs/Phase32p_MultiStageBlindBorePrism.png`.

#### Phase 32q: exactly two separated two-stage blind cavities ✅

Two canonical counterbores may enter a common end or opposite ends. Four cylindrical spans are partitioned into two unique coaxial decreasing-radius chains, each with its own annular shoulder and planar floor. Pairwise feasibility compares all stage-band pairs using bounded axial intervals and transverse disk separation; coaxial opposite-end counterbores remain valid across a positive axial ligament. Deterministic reconstruction produces genus-zero `V24/E36/C72/L22/F18`, with ten planes, eight rational cylinders, four annular levels, and eight exact rational rims. Same/opposite entry, transforms, ordering, wall refusal, an explicit third-cavity cap, and prior-route regression are covered.

**Proof:** `DualSteppedBlindBorePrismFilletVerification` (27 C++ checks) and `Proofs/Phase32q_DualSteppedBlindBorePrism.png`.

#### Phase 32r: one orthogonal side-entering blind cavity ✅

One canonical finite cylindrical cavity may now enter either retained planar side parallel to either prism cross-section direction while all four selected-axis rails round. The classifier derives side and entry direction, centre, radius, and depth from the inward cylinder and both rims; it requires canonical genus-zero `V10/E15/C30/L9/F8` source topology and analytic volume. The complete entrance disk must clear the rounded corners on that side and both selected-axis end caps. Bounded reconstruction explicitly restores the Boolean-fitted entrance and planar-floor rims as exact rational circles, producing genus-zero `V18/E27/C54/L13/F12` with seven planes, five rational cylinders, and one annular side wall. Low/high Y and Z entry, offsets, rail ordering, transforms, corner/through refusal, delegation of a parallel pair to Phase 32s and a two-diameter counterbore to Phase 32u, prior-route regression, console commit, and deterministic rendering are verified.

**Proof:** `SideBlindBorePrismFilletVerification` (27 C++ checks) and `Proofs/Phase32r_SideBlindBorePrism.png`.

#### Phase 32s: exactly two separated parallel side-entering blind cavities ✅

Two canonical finite cylinders may now enter one common retained side or opposite parallel sides while sharing either prism cross-section direction. Both cavities independently retain side, centre, radius, depth, planar floor, rounded-corner-strip clearance, and selected-axis end-cap clearance. Pairwise feasibility combines bounded intervals along the shared side axis with transverse disk separation, admitting coaxial opposite-side cavities across a positive ligament. Deterministic reconstruction restores four exact rational entrance/floor rims and produces genus-zero `V20/E30/C60/L16/F14`, with eight planes, six rational cylinders, and two planar inner loops. Same/opposite entry, coaxial ligament, the alternate Z direction, construction/rail ordering, transforms, corner/end/intersection refusal, mixed-axis refusal, delegation of a third cavity to Phase 32t, prior-route regression, console commit, and deterministic rendering are verified.

**Proof:** `DualSideBlindBorePrismFilletVerification` (32 C++ checks) and `Proofs/Phase32s_DualSideBlindBorePrism.png`.

#### Phase 32t: bounded parallel side-entering blind-cavity set ✅

The parallel side route now scales to three through eight separated finite cylinders sharing either prism cross-section direction. For `N` cavities, source topology is `V=8+2N/E=12+3N/C=24+6N/L=6+3N/F=6+2N`; rounded topology is genus-zero `V=16+2N/E=24+3N/C=48+6N/L=10+3N/F=10+2N`. Every entrance independently clears the selected-axis end caps and retained planar strip, all finite-cylinder pairs retain positive distance, and deterministic reconstruction restores `2N` exact rational entrance/floor rims. Three- and eight-cavity cases, a one-side grid, opposite parallel sides, Y/Z direction, construction and rail ordering, transforms, corner/end/intersection/mixed-axis refusal, the explicit ninth-cavity cap, prior-route regression, console commit, and deterministic rendering are verified.

**Proof:** `MultiSideBlindBorePrismFilletVerification` (33 C++ checks) and `Proofs/Phase32t_MultiSideBlindBorePrism.png`.

#### Phase 32u: one two-diameter side-entering stepped blind cavity ✅

One canonical counterbore may now enter either retained Y/Z side. A larger entrance cylinder terminates at an annular shoulder and a smaller coaxial cylinder continues to a planar floor. Classification requires canonical genus-zero `V12/E18/C36/L12/F10` topology, one common side axis and centre, strict radius decrease, increasing finite depths, and analytic stepped volume. Reconstruction independently checks the outer disk against selected-axis end caps and the retained planar strip, subtracts both bounded stages, and restores four exact rational entrance/shoulder/floor rims. Output is genus-zero `V20/E30/C60/L16/F14`, with eight planes, six rational cylinders, and two annular levels. Low/high Y and Z entry, offsets, ordering, transforms, corner/end/undercut/eccentric refusal, delegation of a third stage to Phase 32v, prior side and selected-axis routes, console commit, and deterministic rendering are verified.

**Proof:** `SideSteppedBlindBorePrismFilletVerification` (31 C++ checks) and `Proofs/Phase32u_SideSteppedBlindBorePrism.png`.

#### Phase 32v: bounded multistage side-entering stepped blind cavity ✅

The single connected side chain now scales to three through eight coaxial stages while retaining the established two-stage route. For `N` stages, source topology is `V=8+2N/E=12+3N/C=24+6N/L=6+3N/F=6+2N`; rounded topology is genus-zero `V=16+2N/E=24+3N/C=48+6N/L=10+3N/F=10+2N`. Classification finds one unique low/high Y or Z entry, orders a contiguous shoulder chain, and requires one selected-axis/transverse centre, strictly decreasing positive radii, and strictly increasing finite cumulative depths. Deterministic bounded subtraction retains `6+N` planes, `4+N` rational cylinders, all `N` annular levels, the final planar floor, analytic band volume, and `2N` exact rational circles. Three/eight stages, directions, offsets, construction and rail ordering, transforms, exact supports and dimensions, wall/end/breakthrough/eccentric/non-decreasing/consumed-shoulder/oblique/malformed refusal, delegation of multi-cavity sets to Phases 32w/32x/32y, the ninth-stage cap, prior-route compatibility, console commit, and deterministic rendering are verified.

**Proof:** `MultiStageSideBlindBorePrismFilletVerification` (42 C++ checks) and `Proofs/Phase32v_MultiStageSideBlindBorePrism.png`.

#### Phase 32w: exactly two separated side-entering two-stage blind cavities ✅

Two canonical side counterbores may now share Y or Z direction and enter one common retained side or opposite parallel sides. Four cylindrical spans are partitioned into two unique coaxial decreasing-radius chains in canonical genus-zero `V16/E24/C48/L18/F14` source topology. Pairwise feasibility compares all four finite stage-band combinations using bounded side-axis intervals and selected-axis/transverse disk separation; coaxial opposite-side counterbores remain valid across a positive ligament. Deterministic reconstruction checks each outer disk against rounded-corner strips and selected-axis end caps, subtracts all four stages, restores eight exact rational rims, and produces `V24/E36/C72/L22/F18` with ten planes, eight rational cylinders, and four annular levels. Same/opposite entry, Y/Z direction, offsets, coaxial clearance, construction and rail ordering, transforms, exact supports and dimensions, wall/end/intersection/mixed-axis/eccentric/through refusal, delegation of a third two-stage chain to Phase 32x and mixed stage counts to Phase 32y, prior-route compatibility, console commit, and deterministic rendering are verified.

**Proof:** `DualSideSteppedBlindBorePrismFilletVerification` (40 C++ checks) and `Proofs/Phase32w_DualSideSteppedBlindBorePrism.png`.

#### Phase 32x: bounded side-entering two-stage blind-cavity set ✅

The separated side-counterbore route now scales to three through eight two-stage chains sharing Y or Z direction. For `N` cavities, canonical source topology is `V=8+4N/E=12+6N/C=24+12N/L=6+6N/F=6+4N`; rounded topology is genus-zero `V=16+4N/E=24+6N/C=48+12N/L=10+6N/F=10+4N`, with `2N` planar levels and `4N` exact rational rims. Every outer disk independently clears selected-axis end caps and the rounded-corner strip, and every one of the `N(N-1)/2` cavity pairs passes all four finite stage-band clearance comparisons. Three/eight cavities, common/opposite entry, Y/Z direction, construction and rail ordering, rigid translations, exact supports and dimensions, wall/end/intersection/mixed-axis/eccentric/through refusal, delegation of mixed-stage members to Phase 32y, the ninth-cavity cap, prior-route compatibility, console commit, and deterministic rendering are verified.

**Proof:** `MultiSideSteppedBlindBorePrismFilletVerification` (41 C++ checks) and `Proofs/Phase32x_MultiSideSteppedBlindBorePrism.png`.

#### Phase 32y: bounded mixed-stage side-entering blind-cavity set ✅

The bounded side-stepped route now supports two through eight cavities with independently mixed two-through-eight-stage concentric chains and at most sixteen stages total. Writing `M` for the total stage count, canonical source topology is `V=8+2M/E=12+3M/C=24+6M/L=6+3M/F=6+2M`; rounded topology is genus-zero `V=16+2M/E=24+3M/C=48+6M/L=10+3M/F=10+2M`. All members share retained-frame Y or Z direction but may independently enter either parallel retained side. The classifier partitions each contiguous chain, requires strictly decreasing positive radii and strictly increasing finite depths, validates analytic band volume, and reconstructs `6+M` planes, `4+M` rational cylinders, `M` planar inner loops, and `2M` exact rational circles. Every cross-cavity finite stage-band pair must retain positive combined axial/radial clearance. Mixed `3+2+4` and maximum `8+8` fixtures, common/opposite entry, Z direction, construction and rail ordering, rigid translations, exact supports and dimensions, wall/end/intersection/mixed-axis/eccentric/through/non-decreasing refusal, one/nine-stage limits, nine-cavity and seventeen-total-stage limits, prior side-stepped and selected-axis compatibility, console commit, and deterministic rendering are verified.

**Proof:** `MixedStageSideBlindBorePrismFilletVerification` (42 C++ checks) and `Proofs/Phase32y_MixedStageSideBlindBorePrism.png`.

#### Phase 32z: first unequal-radius support pair — exact plane–cone boss root ✅

The first asymmetric (unequal-radius) support pair is the native conical frustum boss standing on a planar annular shoulder: its foot circle `R_f` on the shoulder and its top circle `R_t ≠ R_f` are coaxial but unequal, so the boss wall is a cone with half-angle `tan α = (R_f − R_t) / H` (`α > 0` narrows upward, `α < 0` is an undercut flare). Plane and coaxial cone are both surfaces of revolution about one axis, so their `r`-offsets meet in an exact circular spine and the rolling ball sweeps an exact rational torus band rather than an approximation: contact height `z_t = r (1 − sin α)`, contact radius `ρ_t = R_f − z_t tan α`, spine radius `ρ_c = R_f + r (1 − sin α) / cos α`, meridian span `π/2 − α`. The wedge the roll adds is given in closed form by Pappus as `2π` times the first moment of the meridian region (quadrilateral minus circular sector); at `α = 0` the spine, the wedge formula, and the result coincide with Phase 31's plane–cylinder route to `1e-12`. Classification is structural — closed genus-zero `V4/E7/C14/L5/F5` source, root rim shared by a measured planar shoulder and a measured native cone whose tag agrees with its sampled end rows and straight generators, concentric outer rim into a native cylinder wall, and two planar caps with a top-rim radius check — never a face-order or tag-only shortcut. Reconstruction retains the outer cylinder, trims the shoulder to `ρ_c`, revolves the `π/2 − α` arc into a torus band tagged with its analytic identity, shortens the cone to start at `(ρ_t, z_t)`, and sews both caps into a genus-zero `V5/E9/C18/L6/F6` solid; the route refuses its own result if the tessellated volume disagrees with the closed form. Feasibility is explicit: `r > 0`, `z_t < H`, `ρ_c < R_outer`. Apex cones (`R_t = 0`), the conical top rim, the outer shoulder rim, and Boolean-built sources without a canonical root rim refuse rather than approximate. The specification-level validators the route rests on (unequal parallel endpoint pairs, collinear chains, the linear radius law and its ruled surface with measured tangent/normal/curvature, G1 endpoint matching, and the tapered-frustum reconstruction cross-check) are verified in the same suite; the partial-endpoint-chain and variable-radius-roll modes remain explicitly refused as Phase 33 work.

**32z exit gate met:** `PlaneConeFilletVerification` proves narrowing (`α = +0.211`), undercut flaring (`α = −0.245`), steep 45°, `α = 0`, oblique-axis, and reversed-axis fixtures each give the exact topology, torus residual below `1e-9` (measured `≤ 3e-14`), both G1 breaks below `1e-10` (measured `0`), exact retained-support extents, the two analytic contact circles, total volume within the kernel's `1e-3` gate, and added material within `5e-3` of the Pappus wedge (measured `≤ 1.5e-3`, against a `3.8e-4` tessellation floor that even the sharp source carries); both feasibility limits refuse at the boundary and roll exactly at `0.98×` on fixtures where that limit binds first; transactional multi-edge dispatch, console commit, and deterministic rendering are verified.

**Proof:** `Verification/PlaneConeFilletVerification.cpp` (61 C++ checks) and `Proofs/Phase32z_PlaneConeFillet.png` (2560 × 1600 C++-generated contact sheet).

#### Still required before Phase 32 is complete

Cone–cone and cone–cylinder support pairs, apex cones, non-radial endpoint supports, unequal/non-orthogonal and partial corner networks, oblique or mixed-axis cavity sets, more-than-eight side cavities, more-than-sixteen total side-cavity stages, undercut/non-decreasing/eccentric stages beyond named routes, more-than-two stepped or multiple multistage selected-axis cavities, more-than-eight simple cavities/through-holes, non-box thin walls, and general non-box blend/blend intersections each need separate topology and visual regressions. This increment does not claim them.

#### Kernel limits found by the worked models

`Scripts/ToyCar.arc`, `Scripts/ToyBiplane.arc` and `Scripts/ToySailboat.arc` build three reference toys through sketches,
extrusions, lofts, sweeps, arrays, mirrors and Booleans, and every step they use is exact. What they could not do is
recorded here as Phase 33 candidates rather than worked around silently:

1. **Cylinder tool parallel to an extrusion's rulings.** Subtracting a Y-axis wheel-arch cylinder from the Y-extruded
   side silhouette refuses with "marching did not close" (both surfaces are ruled along Y, so their intersection is a
   family of parallel lines the marcher cannot close), while the identical pocket into the Z-extruded plan block works.
   The exact route today is to cut the arch in the 2D silhouette before extruding.
2. **Union across a kink edge.** A box whose face plane crosses the edge where an arch cylinder meets the planar bottom
   returns an open two-hull sheet (or "marching did not close"), while a box that meets only the cylinder above that
   edge unions cleanly — the reason the bearing blocks start at z = 0.8 above the z = 0.7 underside.
3. **Union order segfault.** `(wheel ∪ hub) ∪ axle`, then `∪ (wheel ∪ hub)` crashes instead of refusing; the other
   four orders tried succeed. Reproducer: `Scratchpad/ToyCar/Repro_UnionSegfault.arc`. A crash is a defect by policy —
   the kernel must refuse, never fault.

4. **Tool faces crossing a rounded-rectangle rim.** A box or cylinder subtracted through the rim edge of a
   rounded-rectangle extrusion (the wing notch) refuses with "edge–surface refinement did not converge at a face
   boundary"; the same tool through the face interior works (the car's grille). The 2D profile Boolean before the extrude
   is the exact route.
5. **Second union into a trimmed sphere.** One filleted blade unions into the ball nose; the mirror-image second blade
   then returns an open two-hull sheet in either order, while a single slat through the ball works. A
   rounded-rectangle *extrusion* unioned with the sphere refuses as "tangent or coincident" although it is transversal.
6. **Waterline through tiny loft tip caps.** Intersecting the spindle loft with a half-space whose plane passes through
   the two small end caps returned only the deck disc — the skin pieces were dropped without a refusal. Cutting below
   the caps (deck at z = −0.4) is correct and closed.
7. **Union into a degree-1 loft with kinks.** A slab unioned into the four-section fuselage loft refuses at the loft's
   cap boundary; the tail is therefore one body seated on the fuselage.
8. **`solidify` is a Phase 11a MVP.** Its side wall is a tessellated polyline that never sews to a spline-bounded Coons
   or bridge patch (three open hulls, no refusal) and it is not exact; veneer sails are extruded from their outlines
   instead, and the bellied sheets are shown as NURBS surfaces.

Fixed on the way (verb layer, not kernel): `radial` and `mirror` now transform geometry with one exact affine map (see
the README's worked-models section); previously a rotated box changed volume and offset axes skewed direction cells.

### Phase 33: variable radius, setbacks, partial edges, and G2

Variable-radius blends need a radius law along the spine, feasibility detection, and a non-linear solve. G2 continuity
requires its own surface construction and curvature acceptance measurements. These are not small extensions of the
current constant-radius planar implementation.

### Phase 34: direct modelling — in progress

#### Phase 34a: face loft between two solids ✅

One solid from two closed solids through a chosen face of each. The two faces are dropped, their boundary loops become
the sections of a ruled (two-section) loft with the second rim sense-aligned and re-seamed for least twist, the seam
foot is put onto a real rim vertex by the new Euler operator `BrepBody::SplitEdge` when it lands mid-edge, and the skin
is sewn to the surviving faces along the very edges the dropped faces used (`SkinSolver::LoftFaces`). No Boolean is
involved, so the two shared rims are exact and the coincident-face refusal that a union would raise never arises. Each
chosen face must be bounded by one loop without a seam (a cap or a planar face), the faces must face each other across
a gap, and both bodies must be closed; a face with a hole, a cylinder side face, two faces of one body, an open sheet,
or an out-of-range face refuses with the sources untouched. Console: `loft A:fN B:fM [--keep] [--name=]`.

**34a exit gate met:** `FaceLoftVerification` proves box→box is the analytic prism (volume 224 and area 256 to
`1e-16`, `V16/E25/F11`, all eight rim edges shared, every edge two-coedged), cap→cap is the exact frustum (volume
`12π + 7π + 3π` within the `1e-3` tessellation gate, seam ruling exactly `√10`, skin area `π(r₁+r₂)·slant`), box→cylinder
is a genus-zero `V11/E17/F8` solid whose cap circle is split into two arcs at the least-twist seam (ruling length
`√(3² + (2√2 − 2.4)²)` to `2e-15`) with the skin volume between the two end-area prisms, six refusals, console commit
with and without `--keep`, and the deterministic proof.

**Proof:** `Verification/FaceLoftVerification.cpp` (29 C++ checks) and `Proofs/Phase34a_FaceLoft.png` (2560 × 1600
C++-generated contact sheet).

#### Phase 34b: tweaks — translate a face, an edge or a vertex on fixed topology ✅

`TweakSolver` moves a set of body vertices by one vector and re-fits only the geometry that touches them; no vertex,
edge, loop or face is created or destroyed, so closure is structural. Edges touching moved vertices must be straight and
are rebuilt as lines; a face whose vertices all move is translated rigidly; a natural four-sided face (degree 1×1 — box
faces, extrusion sides, loft rectangles) has its poles set to its corners and stays a `Plane` when they remain coplanar,
otherwise it becomes an exact bilinear patch that is accepted only with `--warp`; a trimmed planar face (extrusion and
prism caps, n-gons) keeps its plane and has its coedge traces refitted — and its underlying plane patch grown when the
moved loop leaves it — provided every moved vertex stays in that plane, since an n-gon cannot warp. The answers this
gives a modeller are exact: translating a box face by *any* vector keeps all six faces planar (the neighbours become
parallelograms and the volume follows Cavalieri), translating a box edge keeps the two faces along it planar and warps
the end faces only for a component along the edge, and lifting a box corner warps only the faces whose planes do not
contain the direction — the top, not the two vertical sides. The default refuses any warp and names the faces; the
tweak never detects a face passing through another and refuses only an inverted or degenerate result. Console:
`tweak <body> (dx,dy,dz) --face=i | --edge=i | --vertex=i [--warp] [--name=]`. `push` remains the offset operation;
on a general body it adds a slab through a Boolean (`V12/E20/F10` on a box), while the tweak is the true face move
(`V8/E12/F6`).

Found on the way: `NurbsSurface::SpanSubdivision` returned the minimum subdivision for every degree-1 direction, so a
twisted bilinear cell (a tweaked face, a twisted ruled loft) was tessellated as two triangles — the lifted box corner
measured 27.25 against an exact 27. Degree-1 spans now subdivide on the height of each cell's fourth pole above the
plane of the other three (zero for planar and developable cells), and the corner measures 27.001 at the 32-span cap.

**34b exit gate met:** `TweakVerification` proves face moves along the normal (volume 42 exactly, `V8/E12/F6`
unchanged, six planes), in-plane shear (volume 24 preserved), oblique (42) and composition; edge lift (30) and
out-of-plane perpendicular move (section area × length to `1e-16`); the along-edge move names exactly the two end faces
and refuses without `--warp`, then matches the trilinear hexahedron integral (2×2×2 Gauss, exact for the trilinear
Jacobian) with two bilinear faces; the lifted corner names only the top, refuses by default, then measures
`24 + ∫∫(x/4)(y/3) = 27` to `3.6e-5` with the bilinear top through all four corners; the oblique corner pull matches
the integral with three bilinear faces; a hexagonal prism's cap moves obliquely (base × 2.8 exactly), one vertical edge
pushes outward with both caps refitted and grown (`(6√3 + 0.6√3)·2` to `1.6e-16`), a cap vertex refuses to leave its
plane and moves within it; cylinder caps, zero vectors, bad indices, open sheets and inverting moves refuse; console
commit, refusal texts and the proof `Proofs/Phase34b_Tweak.png` are verified (41 checks).

#### Completed Phase 34c/34d: arbitrary-dihedral planar chamfers and transactional edge loops ✅

`BlendSolver::ChamferEdges` now accepts one or more selected straight edges when every selected edge is manifold and its
adjacent faces are planar. A convex planar source is reconstructed from the original supporting half-spaces plus one exact
setback plane per selected edge. The intersection of those planes creates shared endpoint vertices and true mitres in one
transaction, so adjacent selections do not depend on edge-table order or on a Boolean cutter cap touching a vertex. The
single-edge API uses the same route, including non-right dihedrals; complete native cylinder-cap rims retain their exact
conical-frustum route.

The feasibility gate rejects non-planar/curved members, duplicate selections, invalid topology, and a setback that reaches
a non-selected boundary vertex. The console now accepts `--edges=i,j,…`, commits connected planar sets atomically, and
leaves the source and destination name untouched on refusal. A disconnected set may use the deterministic independent
fallback; if any member fails, no partial result is published. Convexity is an explicit boundary rather than a claim of
unrestricted concave planar Boolean healing.

`DirectModelingVerification` now checks the public console route, a two-edge miter, a self-intersection refusal, and the
visible `Proofs/Phase34c_SingleEdgeChamfer.png`. `ChamferLoopVerification` adds 18 checks for a two-edge miter, a
four-edge box loop, a 45° triangular-prism dihedral, analytic wedge volume, transactional source preservation, and the
visible `Proofs/Phase34d_ChamferLoop.png`.

#### Phase 34e: bounded planar face rotation and uniform scale tweaks ✅

`TweakSolver::RotateFace` and `ScaleFace` apply a centroid-pivoted arbitrary-axis rotation or positive uniform scale to
one planar face while preserving fixed topology. The target face is transformed rigidly; adjacent natural degree-1×1
quads are re-fitted from their four corners. If that refit leaves a quad non-coplanar, the default route refuses with an
explicit adjacent-warp diagnostic; `--warp` opts into a bilinear free-form side. Trimmed planar faces remain planar, while
curved edges/faces and unsupported surfaces refuse rather than being approximated. The result is delivered transactionally
only after `Validate()` confirms a closed, manifold, oriented solid with positive numerical volume.

The console commands are `rotate Body angleDeg --face=i [--axis=(x,y,z)] [--warp] [--name=]` and
`scale Body factor --face=i [--warp] [--name=]`. Both preserve the source on refusal. The bounded scope is deliberate:
this is not unrestricted curved-face rotation, arbitrary CAD transform support, or topology-changing face editing.

**34e exit gate met:** `TransformTweakVerification` checks the analytic tapered-box scale (`V8/E12/F6`, volume 14),
source immutability, default refusal of an in-plane rotation that would warp adjacent quads, explicit warp acceptance,
fixed topology and positive numerical volume, curved-rim refusal, and both console commands. It also writes the visible
`Proofs/Phase34e_TransformTweaks.png` proof (1600 × 800).

#### Phase 34f: bounded same-body face loft handles ✅

`SkinSolver::LoftFaces` and the `loft A:fN B:fM` console route now accept two distinct faces owned by one B-rep
when that B-rep contains two disconnected, valid hulls. The selected rims are removed exactly once and one ruled skin
bridges them into a single positive-volume solid; coincident faces, connected same-body selections that collapse to a
zero-volume duplicate shell, faces with holes, seam-bearing faces, and non-facing selections still refuse transactionally.
This is an explicit multi-hull handle bridge, not unrestricted same-solid face surgery.

`FaceLoftVerification` now covers the two-hull source, V16/E25/F11 genus-zero bridge, volume 224, console `--keep`
integration, source preservation, and the visible `Proofs/Phase34f_SameBodyFaceLoft.png` proof.

#### Phase 35a: native curved-cylinder cap and circular-edge tweaks ✅

The curved-tweak route is intentionally analytic and narrow. A native right-cylinder cap is recognized structurally as
one rational circular closed edge, two planar caps, one cylindrical side, and the canonical `V2/E3/F3` topology. Moving
the cap face or its complete circular rim along the cylinder axis rebuilds the exact cylinder with the new height and
preserves topology. Lateral motion, the cylindrical side face, spheres, arcs, and non-native curved edges refuse rather
than becoming sampled approximations. The source remains immutable on every refusal.

**35a exit gate met:** `CurvedTweakVerification` checks exact source/result volumes (`16π` and `20π`), fixed topology,
face/edge route equivalence, lateral and non-native curved refusals, console commit, and the visible
`Proofs/Phase35a_CurvedCapTweaks.png` proof. The existing `TweakVerification` cap regression now asserts the same
analytic route instead of expecting the old blanket curved-edge refusal.

#### Phase 35b: bounded concave planar chamfer networks ✅

`BlendSolver::ChamferEdges` now recognizes a simple prismatic network: every selected edge is a straight parallel edge
running through the same planar cap profile, and the complete cap perimeter is a single simple polygon. All selected
reflex corners are inset in one 2D outline operation and the profile is extruded again, producing one transactional
solid with exact planar bevels. The route is deterministic for multiple U/L-handle corners and avoids sequential cutter
order and coincident Boolean caps. Analytic profile-area × extrusion-length volume checks and the V/E/F counts guard the
rebuild; over-large setbacks, curved/non-prismatic members, self-intersections and arbitrary non-convex solids still
refuse.

**35b exit gate met:** `ConcaveChamferVerification` checks a two-reflex U profile, V20/E30/F12 topology, exact
chamfered-profile volume, source immutability, over-large transactional refusal, console integration, and the visible
`Proofs/Phase35b_ConcaveChamferNetwork.png` proof.

#### Phase 35c: native right-cone circular-cap chamfers ✅

The native curved-edge chamfer route now recognizes a structurally verified right cone with one rational circular cap
edge. The meridian set-back is solved analytically from the cone slant: the retained cone ends at the tangent point,
a conical bevel frustum runs from that point to the reduced planar cap, and both exact NURBS support faces are sewn into
one `V3/E5/F4` solid. Set-backs that reach the top radius/apex, general torus edges, arbitrary NURBS curves and all
unproven curved-edge networks refuse transactionally.

**35c exit gate met:** `ConeChamferVerification` checks the two-frustum closed-form volume, topology, source preservation,
feasibility boundaries, generic curved-edge refusal, console integration, and the visible
`Proofs/Phase35c_ConeCapChamfer.png` proof.

#### Phase 35d: connected same-body opposite-cap identity loft ✅

A connected same-body request is no longer blanket-refused. One exact and useful boundary is now recognized structurally:
two opposite planar end caps of a canonical axis-aligned rectangular prism (`V8/E12/F6`, one hull, all six planar
faces). The selected caps are the two sections through material already enclosed by that prism, so the mathematically
correct loft is an identity prism. The route rebuilds the box from its measured bounds only after validating the source,
selected normals, opposite bound planes, topology, and positive volume; it does not accept the generic periodic skin,
which would be a genus-one/zero-volume shell. Both console `--keep` and consuming commits are transactional.

Adjacent connected faces, connected native-cylinder caps, curved side faces, holed or open bodies, rotated/non-canonical
prisms and arbitrary same-body face surgery remain explicit refusals. This is a bounded same-body seed, not a claim of
general connected multi-face replacement or multi-section lofting.

**35d exit gate met:** `ConnectedFaceLoftVerification` has 16 checks for exact topology, volume and area, source
immutability, refusal boundaries, both console commit modes, and the visible `Proofs/Phase35d_ConnectedFaceLoft.png`.

#### Phase 36b: bounded general connected same-body face lofts ✅

`SkinSolver::LoftFaces` now accepts a conservative set of connected same-body replacements beyond the canonical box identity:
validated native cylinder/cone cap identities, hole-free prismatic extrusion cap replacements, and separated planar side
faces on real prismatic solids. The generic same-body bridge now rejects adjacent faces before it can duplicate an existing
band, while accepted results are checked for one positive-volume hull. Dissimilar disconnected hulls in one B-rep remain
covered as non-identity ruled replacements. Healing preserves a validated solid when a natural-face re-sew would split a
periodic seam or open the shell; no source is mutated. The console, transactional refusal paths, unequal prism fixture,
source immutability, and visible proof are covered by `GeneralConnectedFaceLoftVerification` and
`Proofs/Phase36b_GeneralConnectedFaceLoft.png`.

#### Phase 36c: first general curved-edge chamfer ✅

The first arbitrary curved-edge chamfer beyond native cylinder/cone caps is bounded to a complete circular root where a
planar annular shoulder meets a native cylindrical boss. Structural classification reuses the measured tangent chain,
circular support frames, and mixed plane/cylinder adjacency; reconstruction trims the shoulder radius by the setback,
raises the boss start by the same axial setback, inserts an exact conical band, and sews a `V5/E9/F6` manifold. The
concave wedge volume is checked analytically. Partial circular loops, torus/freeform edges, non-closed mixed supports,
and consuming setbacks refuse transactionally; console dispatch and a visible source/result proof are covered by
`GeneralCurvedChamferVerification` and `Proofs/Phase36c_GeneralCurvedChamfer.png`.

#### Phase 36d: bounded convex non-planar edge-loop chamfers ✅

`BlendSolver::ChamferEdges` now recognizes a closed non-planar edge loop whose members are still straight and bounded by
planar supports. The loop must have one connected degree-two boundary with an unambiguous cyclic pairing; open or branched
non-planar selections refuse before reconstruction. Accepted convex sources are solved through one shared supporting-plane
system, producing real mitre vertices and a single transactional `V14/E24/F12` manifold result for the covered six-edge
loop. Curved supports, non-convex sources, and general freeform/non-planar faces remain outside this route. The console,
source immutability, refusal boundaries, and visible source/result proof are covered by
`ArbitraryNonPlanarEdgeLoopVerification` and `Proofs/Phase36d_ArbitraryNonPlanarEdgeLoop.png`.

#### Phase 36e: bounded Phase 33 linear variable-radius foundation ✅

The first Phase 33 increment accepts an explicit complete-circular linear radius law and reconstructs its ruled surface as
an exact native frustum. It validates endpoint radii, axial support correspondence, positive radius, sampled
circumferential curvature, analytic swept volume, and one-hull topology. Partial circular supports, zero-radius/apex
laws, non-axial endpoints, partial endpoint chains, nonlinear laws, variable setback fillets, and G2 continuity remain
explicit refusals. The law/surface/reconstruction and adversarial checks are covered by
`Phase33VariableRadiusVerification`.

#### Phase 36f: bounded complete plane–cone boss-root chamfer ✅

Set 3 now has one verified mixed-support chamfer slice: a complete circular root shared by a planar annular shoulder and
a native coaxial conical frustum. `PlaneConeBossRoot` measures the cone end rows and straight generators, derives the
setback point from the cone slant, and transactionally rebuilds the retained outer cylinder, shoulder of revolution,
conical band, retained cone and caps. Accepted results are one genus-zero `V5/E9/F6` solid with no open,
non-manifold, or misoriented edges; the differential volume follows the exact square-radius meridian wedge within the
kernel's tessellation floor, and the source is left unchanged. Narrowing and flaring fixtures, console dispatch,
zero/negative/consuming/apex refusals, arbitrary torus refusal, and a visible source/result proof are covered by
`PlaneConeChamferVerification` and `Proofs/Phase36f_PlaneConeChamfer.png`.

This does not claim general curved-edge support: partial loops, cone–cylinder pairs, non-coaxial or freeform supports,
curved networks/corner patches, concave/non-convex networks, and general intersection/trim/sew healing remain explicit
refusals for later slices.

#### Phase 36g: bounded complete cylinder–cone boss-root chamfer ✅

The next mixed-support slice accepts the canonical complete circular root shared by a native coaxial cylindrical boss and
native coaxial conical frustum. The classifier measures both curved supports, the root radius, the shoulder and outer wall,
and the two planar caps; reconstruction retains the outer cylinder, annular shoulder, shortened boss cylinder, exact
straight-meridian conical chamfer band, shortened cone and caps. Accepted results are one genus-zero `V6/E11/F7` solid
with no open, non-manifold, or misoriented edges. Narrowing and flaring fixtures follow the exact square-radius meridian
integral, source bodies remain unchanged, and zero/negative/consuming and arbitrary-torus cases refuse transactionally.
`CylinderConeChamferVerification` covers the route, console transaction, refusal boundaries, and visible
`Proofs/Phase36g_CylinderConeChamfer.png`.

This remains a canonical complete coaxial route only. Partial circular loops, non-coaxial or oblique cone/cylinder
intersections, multiple curved roots, curved networks/corner patches, concave/non-convex networks, and general
intersection/trim/sew healing remain explicit refusals.

#### Phase 36h: bounded half-turn partial curved-root chamfer ✅

The curved-root chamfer family now accepts one physical semicircular root chain shared by a planar shoulder and
cylindrical boss. A selected open rational arc propagates through the complete two-member chain; reconstruction keeps the
endpoint meridians and one planar diameter cap while inserting the exact conical setback band. The result is one
manifold genus-zero `V12/E17/F7` half-turn, with the differential wedge volume exactly one-half of the complete ring
route within tessellation tolerance. Selecting either chain member, source immutability, zero/consuming refusals,
console dispatch, and a visible source/result proof are covered by `PartialCurvedChamferVerification` and
`Proofs/Phase36h_PartialCurvedChamfer.png`.

This is deliberately a half-turn topology slice. General-angle sectors, arbitrary partial arcs, mixed cone/cylinder
partial roots, branched or incomplete chains, curved networks/corner patches, and general intersection/trim/sew healing
remain explicit refusals.

#### Phase 36i: bounded general-angle partial curved-root chamfer ✅

The partial curved-root route now accepts one measured non-reflex sector with two radial endpoint caps. The same complete
open chain is propagated from either member; exact outer/shoulder/chamfer/boss endpoint surfaces are reconstructed, then
the two radial cap faces are healed transactionally. Accepted results are one genus-zero `V12/E18/F8` manifold with no
open, non-manifold, or misoriented edges. The added wedge follows the exact angular fraction of the full circular
analytic value. `SectorCurvedChamferVerification` covers source `V14/E24/C48/L12/F12` classification, chain selection,
volume, source immutability, refusal boundaries, console dispatch, and `Proofs/Phase36i_GeneralSectorChamfer.png`.

Reflex sectors, arbitrary partial arcs, mixed cone/cylinder partial roots, branched or incomplete chains, curved
networks/corner patches, and general intersection/trim/sew healing remain explicit refusals.

#### Phase 36j: bounded partial plane–cone root chamfer ✅

The partial mixed-support route now accepts one measured coaxial conical-frustum sector shared by a planar annular
shoulder. It supports the canonical half-turn and a non-reflex general sector, derives the setback contact from the
measured frustum slant, reconstructs the exact shoulder/chamfer-band/retained-cone meridian, and heals the radial endpoint
caps transactionally. The accepted general-sector result is genus-zero `V12/E18/C36/L8/F8`; the half-turn identity uses
`V12/E17/L7/F7`. `PartialPlaneConeChamferVerification` covers the `V14/E24/C48/L12/F12` general-sector source,
chain propagation, angular square-radius volume, source immutability, console dispatch, refusal boundaries, and
`Proofs/Phase36j_PartialPlaneConeChamfer.png`.

The route remains intentionally narrow: reflex or arbitrary trimmed sectors, mixed cone/cylinder partial roots,
non-coaxial or oblique supports, branched/incomplete chains, curved networks/corner patches, and general
intersection/trim/sew healing refuse explicitly.

#### Phase 36k: bounded partial cone–cylinder root chamfer ✅

The next mixed-support route accepts one canonical coaxial cone/cylinder sector with a complete open two-member root
chain. It supports the half-turn and one non-reflex general sector, measures the cone/cylinder support endpoints and
common radius, reconstructs the retained cone, exact conical chamfer band, retained cylinder, and axial caps, then heals
the radial endpoint caps transactionally. The accepted general-sector result is genus-zero `V10/E15/C30/L7/F7`; the
half-turn identity is `V10/E14/C28/L6/F6`. `PartialConeCylinderChamferVerification` covers source
`V11/E19/C38/L10/F10`, half-turn classification, chain propagation, the meridian volume identity, console dispatch,
refusal boundaries, and `Proofs/Phase36k_PartialConeCylinderChamfer.png`.

Cone–cone pairs, non-coaxial or oblique supports, apex/zero-radius cones, arbitrary trimmed or reflex arcs,
branched/incomplete chains, curved networks/corner patches, freeform supports, and general intersection/trim/sew
healing remain explicit refusals.

#### Phase 36l: bounded partial cone–cone root chamfer ✅

The next mixed-support route accepts two native coaxial conical frusta meeting at one measured open circular root chain.
It supports the canonical half-turn and one non-reflex general sector, measures both cone endpoint pairs and common
root frame, derives equal slant setbacks, reconstructs the retained lower cone, exact conical chamfer band, retained upper
cone, and axial caps, then heals radial endpoint caps transactionally. The accepted general-sector result is genus-zero
`V10/E15/C30/L7/F7`; the half-turn identity is `V10/E14/C28/L6/F6`. `PartialConeConeChamferVerification` covers
source `V11/E19/C38/L10/F10`, half-turn classification, chain propagation, the cone–cone meridian volume identity,
console dispatch, refusal boundaries, and `Proofs/Phase36l_PartialConeConeChamfer.png`.

Non-coaxial or oblique cone/cylinder or cone/cone supports, apex/zero-radius cones, arbitrary trimmed or reflex arcs,
branched/incomplete chains, curved networks/corner patches, freeform supports, and general intersection/trim/sew healing
remain explicit refusals.

#### Phase 36m: bounded complete apex plane–cone root chamfer ✅

The native plane–cone route now accepts one complete coaxial conical boss whose top endpoint is a true apex. The classifier
measures the circular root, outer cylindrical shoulder wall, finite cone height, and zero-radius endpoint rather than
assuming a top cap. Reconstruction retains the outer cylinder and shoulder, inserts the exact conical setback band, and
retains the apex cone as one vertex. The accepted result is genus-zero `V6/E9/C18/L5/F5`; the analytic square-radius
meridian wedge, source immutability, console transaction, refusal boundaries, and `Proofs/Phase36m_ApexPlaneConeChamfer.png`
are covered by `ApexPlaneConeChamferVerification`.

Partial apex sectors, non-coaxial or oblique supports, cone–cone/cylinder–cone apex networks, arbitrary trimmed or reflex
roots, branched/incomplete chains, curved corner patches, freeform supports, and general intersection/trim/sew healing
remain explicit refusals.

#### Phase 36n: bounded partial apex plane–cone root chamfer ✅

The apex slice now extends to one canonical coaxial partial sector. The classifier accepts the half-turn or one non-reflex
general open root chain, measures the zero-radius cone endpoint and endpoint caps, and reconstructs exact partial outer,
shoulder, chamfer, and retained apex-cone surfaces. The accepted general-sector result is genus-zero
`V10/E15/C30/L7/F7`; the half-turn identity is `V10/E14/C28/L6/F6`. `PartialApexPlaneConeChamferVerification` covers
source `V11/E19/C38/L10/F10`, chain propagation, apex meridian volume, source immutability, console dispatch, refusal
boundaries, and `Proofs/Phase36n_PartialApexPlaneConeChamfer.png`.

Non-coaxial or oblique supports, apex networks, arbitrary trimmed/reflex/branched/incomplete roots, curved corner patches,
freeform supports, and general intersection/trim/sew healing remain explicit refusals.

#### Phase 36o: bounded partial plane–cone root fillet ✅

The constant-radius fillet family now accepts the same canonical coaxial open plane–cone root sector as the bounded
partial chamfer route, including its physical half-turn. The classifier reuses the measured two-member tangent chain and
native conical-frustum supports; reconstruction computes the exact plane–cone rolling-circle contacts and revolves the
circular meridian into a toroidal roll. General sectors heal two radial endpoint caps transactionally, while the exact
result is genus-zero `V12/E18/C36/L8/F8`; the half-turn identity is `V12/E17/C34/L7/F7`. The analytic first-moment
wedge, source immutability, console transaction, refusal boundaries, and exterior-readable top-view proof are covered by
`PartialPlaneConeFilletVerification` and `Proofs/Phase36o_PartialPlaneConeFillet.png`.

This remains a named constant-radius sector slice, not a generic partial-edge or variable-radius fillet. Apex/zero-radius
cones, non-coaxial or oblique supports, reflex/arbitrary/branched/incomplete roots, cone–cone/cylinder–cone apex networks,
curved corner patches, nonlinear/G2 construction, and general intersection/trim/sew healing remain explicit refusals.

#### Phase 36p: bounded complete cylinder–cone root fillet ✅

The complete mixed-support fillet family now accepts one narrowing coaxial cylindrical boss meeting a native conical
frustum. The classifier measures the closed cylinder–cone root, positive conical top radius, and exact support dimensions;
reconstruction solves the circular meridian tangencies analytically, retains the outer cylinder, shoulder, shortened
boss cylinder and cone, and inserts a toroidal roll. The result is a genus-zero `V6/E11/C22/L7/F7` solid, with its
meridian removal checked by an exact first-moment integral. Flaring/zero-angle, apex, partial, non-coaxial and arbitrary
roots remain refused; `CylinderConeFilletVerification` covers 15 checks, console dispatch, source immutability, refusal
boundaries, and the exterior-readable `Proofs/Phase36p_CylinderConeFillet.png`.

The slice is constant-radius only; variable-radius/nonlinear/G2 construction, curved corner patches, and general
intersection/trim/sew healing remain explicit refusals.

#### Phase 36q: bounded complete cone–cone root fillet ✅

The complete cone–cone fillet family now accepts one canonical coaxial pair of conical frusta with positive far radii
and an increasing narrowing slope across the shared root. The classifier measures both cone endpoint rows, the common
root circle, exact axis and cap geometry; reconstruction solves the shared circular meridian tangencies analytically and
retains both cones around one toroidal roll. The result is genus-zero `V4/E7/C14/L5/F5`, with volume checked by the exact
first moment of the removed cone–cone meridian wedge. `ConeConeFilletVerification` covers 16 checks, source
immutability, console dispatch, refusal boundaries, and the exterior-readable `Proofs/Phase36q_ConeConeFillet.png`.

Partial sectors, flaring/equal-slope pairs, apex/zero-radius cones, non-coaxial or oblique supports, arbitrary or branched
roots, variable-radius/nonlinear/G2 construction, curved corner patches, and general intersection/trim/sew healing
remain explicit refusals.

#### Phase 36r: bounded partial cone–cone root fillet ✅

The complete cone–cone roll now extends to one canonical open coaxial non-reflex sector, including the physical half-turn.
The classifier reuses the measured two-member cone–cone tangent chain and support slopes; reconstruction revolves the exact
circular meridian, retains both cone patches and axial caps, and heals two radial endpoint caps for general sectors. The
accepted result is genus-zero `V10/E15/C30/L7/F7`, with half-turn identity `V10/E14/C28/L6/F6`; its angular-fraction
first-moment volume, chain propagation, source immutability, console dispatch, refusal boundaries, and exterior-readable
proof are covered by `PartialConeConeFilletVerification` and `Proofs/Phase36r_PartialConeConeFillet.png`.

Flaring, equal-slope, reflex/arbitrary/branched/incomplete roots, apex/zero-radius cones, non-coaxial or oblique supports,
variable-radius/nonlinear/G2 construction, curved corner patches, and general intersection/trim/sew healing remain
explicit refusals.

#### Phase 36s: bounded partial cone–cylinder root fillet ✅

The mixed-support fillet family now includes one canonical open coaxial cone–cylinder sector, including the physical
half-turn. Unlike the earlier partial cone–cylinder chamfer, this fillet deliberately accepts only the orientation whose
cone base radius is smaller than the shared cylinder/root radius: the cone widens toward the cylinder, yielding a valid
interior constant-radius roll. The exact circular meridian is revolved with the retained cone and upper cylinder; two
radial endpoint caps are healed only for a general sector. The result is genus-zero `V10/E15/C30/L7/F7`, with half-turn
identity `V10/E14/C28/L6/F6`, and its angular-fraction cone-plus-cylinder first-moment volume, chain propagation,
source immutability, console dispatch, refusal boundaries, and exterior-readable proof are covered by
`PartialConeCylinderFilletVerification` and `Proofs/Phase36s_PartialConeCylinderFillet.png`.

The opposite narrowing orientation, equal-slope/zero-radius/apex supports, complete sectors, reflex/arbitrary/branched or
incomplete roots, non-coaxial or oblique supports, variable-radius/nonlinear/G2 construction, curved corner patches, and
general intersection/trim/sew healing remain explicit refusals.

#### Phase 37a: bounded straight-edge rolling-ball variable-radius fillet ✅

The first actual variable-radius fillet application is bounded to one finite straight edge shared by two perpendicular
planar supports. A positive linear law changes the quarter-circle rolling section from station to station; five ruled/lofted
side surfaces and two planar end caps form an exact `V10/E15/C30/L7/F7` solid. The integrated rounded-corner area gives
the analytic volume, every result tessellation is checked for outward normals, and the exterior proof compares the sharp
box with the variable-radius result. Zero/negative/consuming laws, degenerate edge frames, nonlinear laws, partial-edge
selection, G2 continuity, apexes, and arbitrary support/healing remain refused. Coverage is in
`VariableRadiusCornerFilletVerification` and `Proofs/Phase37_VariableRadiusCornerFillet.png`.

#### Phase 37b: bounded variable support-setback corner fillet ✅

The same finite straight, perpendicular planar corner now accepts an independent positive linear
support-setback law. At each station the far support extent is `d(t) = r(t) + s(t)`, so the
quarter-circle rolling section remains exact while the available planar clearance changes along
the edge. Four straight support boundaries, one ruled quarter-circle surface, and two planar end
caps produce the same one-hull `V10/E15/C30/L7/F7` topology. The volume is checked against the
integrated variable-extent prism minus the rounded-corner area, and the verifier samples
`d(t) - r(t) = s(t)` at multiple stations. Zero/negative laws, degenerate frames, and zero
length refuse transactionally. Nonlinear radius/setback laws, unequal support setbacks,
partial-edge selection, G2 continuity, apexes, and arbitrary support/healing remain refused.
Coverage is in `VariableSetbackCornerFilletVerification`, its plan is
`docs/PLAN_VariableSetbackLaw.md`, and its distinct proof is
`Proofs/Phase37b_VariableSetbackCornerFillet.png`.

#### Phase 37c: bounded quadratic nonlinear-radius corner fillet ✅

The variable-radius corner now accepts a genuinely nonlinear quadratic law specified by its
radii at the two endpoints and at the middle station. Five quadratic lofts pass through three
exact quarter-circle sections, so the result is not a renamed linear ruled surface. Explicit
first and second radius derivatives provide circumferential and meridional curvature bounds
before sewing; the integrated quadratic volume, outward normals, capped `V10/E15/C30/L7/F7`
topology, refusals, and an exterior comparison proof are covered by
`NonlinearVariableRadiusCornerVerification` and
`Proofs/Phase37c_NonlinearVariableRadiusCorner.png`.

This slice remains one complete finite straight corner with a common positive setback. It does
not claim G2 continuity at the rolling/support junction, nonlinear setback laws, partial edges,
unequal support setbacks, apexes, freeform supports, or general healing.

#### Phase 37d: bounded G2 planar corner transition ✅

A separate quintic non-rolling corner profile now has support-aligned tangents and zero endpoint
curvature. Its extrusion joins the two planar support faces with measured G2 position/tangent/
curvature continuity, while retaining one-hull `V10/E15/C30/L7/F7` topology and an exact
Green's-theorem profile-area volume. The verifier compares this G2 profile against the circular
rolling route and refuses degenerate handles, consuming radii, and invalid frames.

This is explicitly **not** a rolling-ball or nonlinear-radius G2 fillet. Rolling-ball G2 joins,
nonlinear setbacks, partial edges, unequal support setbacks, apexes, freeform supports, and
general healing remain open. Coverage is in `G2PlanarCornerVerification`, its plan is
`docs/PLAN_G2PlanarCorner.md`, and its proof is `Proofs/Phase37d_G2PlanarCorner.png`.

#### Phase 37e: bounded nonlinear support-setback corner blend ✅

The support-setback family now accepts a genuinely nonlinear quadratic clearance law while the
rolling radius remains independently constant in the proof fixture. Five quadratic lofts pass
through exact quarter-circle sections at three stations; the extent identity `d(t) = r(t) +
s(t)`, analytic quadratic volume, positivity/refusals, capped `V10/E15/C30/L7/F7` topology,
outward normals, and a nonlinear-versus-linear comparison proof are covered by
`NonlinearVariableSetbackCornerVerification` and
`Proofs/Phase37e_NonlinearVariableSetbackCorner.png`.

This does not claim unequal setback laws, rolling-ball G2 continuity, partial edges, apexes,
freeform supports, or general healing. Its plan is `docs/PLAN_NonlinearSetbackLaw.md`.

#### Phase 37f: bounded unequal support-setback corner blend ✅

The two perpendicular planar supports now accept independent positive linear clearance laws.
Equal laws refuse so this is not a renamed common-setback fixture. The accepted non-square
station sections retain exact quarter-circle rolling sections; the product-of-extents volume,
`V10/E15/C30/L7/F7` topology, outward normals, transactional refusals, and asymmetric
comparison proof are covered by `UnequalSetbackCornerVerification` and
`Proofs/Phase37f_UnequalSetbackCorner.png`.

This remains a complete finite straight corner. Nonlinear laws on both supports, rolling-ball G2
continuity, partial edges, apexes, freeform supports, and general healing remain open. Its plan
is `docs/PLAN_UnequalSetbackLaw.md`.

#### Phase 37g: bounded nonlinear unequal support-setback corner blend ✅

The two perpendicular planar supports now accept independent genuinely nonlinear quadratic
clearance laws. Three exact station sections drive five quadratic lofts with a shared station
parameterization, preserving the asymmetric seams as one watertight `V10/E15/C30/L7/F7`
solid. The exact volume integrates the product of the two quadratic outer extents and subtracts
the integrated quarter-circle removal. `NonlinearUnequalSetbackCornerVerification` covers 34
checks, positivity, extent identities, outward normals, linear/equal-law refusals, and the
comparison proof `Proofs/Phase37g_NonlinearUnequalSetbackCorner.png`.

This remains one explicit finite straight corner; rolling-ball G2, arbitrary edge selection,
freeform supports, and general healing remain unsupported. Its plan is
`docs/PLAN_NonlinearUnequalSetbackLaw.md`.

#### Phase 38e: bounded oblique planar corner fillet ✅

An explicit finite straight edge frame now accepts two planar support directions with a strict
non-orthogonal interior angle and one constant rolling radius. The exact tangent offset
`r cot(theta/2)`, rational circular extrusion, retained outer support, and finite end caps sew as
`V8/E12/F6/L6`; wedge-minus-circular-segment volume, normals, and refusal boundaries are covered
by `ObliquePlanarCornerFilletVerification` and
`Proofs/Phase38e_ObliquePlanarCornerFillet.png`. Variable laws, partial edges, arbitrary
selection, apexes, freeform supports, and healing remain unsupported; this is not rolling-ball G2.

#### Phase 38f: bounded oblique partial-edge fillet ✅

A strict interior interval on one explicit finite straight edge now accepts the same constant-radius
oblique planar corner construction while retaining the sharp corner before and after the interval.
The route splits retained support and outer walls at both stations, adds rational circular-band and
transition-cap surfaces, and sews deterministic `V20/E38/F20/L20` topology. The analytic volume is
`Length * sharp wedge area - (End - Start) * removed oblique-corner area`; tangent distance, boundary
normals, interval/radius refusals, and a distinct sharp/partial proof render are covered by
`ObliquePartialEdgeFilletVerification` and `Proofs/Phase38f_ObliquePartialEdgeFillet.png`.
Variable radius, arbitrary edge selection, freeform supports, apexes, rolling-ball G2, and healing
remain unsupported. Its plan is `docs/PLAN_ObliquePartialEdgeFillet.md`.

#### Phase 38g: bounded oblique quadratic partial-edge fillet ✅

The explicit Stage 4f oblique interval now accepts one genuinely nonlinear positive quadratic
radius law without broadening into arbitrary variable-radius support handling. Fixed station
parameterization produces exact start, middle, and end circular sections with shared tangent
boundary curves, rational quadratic lofts, sharp continuation walls, transition sectors, and
finite caps as `V20/E38/F20/L20`. The integrated radius-square oblique removal volume, exact
station tangent distances, boundary normals, and transactional law/frame/interval refusals are
covered by `ObliqueQuadraticPartialEdgeFilletVerification` and
`Proofs/Phase38g_ObliqueQuadraticPartialEdgeFillet.png`. Arbitrary variable laws, edge selection,
freeform supports, apexes, rolling-ball G2, and healing remain unsupported. Its plan is
`docs/PLAN_ObliqueQuadraticPartialEdgeFillet.md`.

#### Phase 38h: bounded oblique quadratic full-edge fillet ✅

The complete-edge counterpart now accepts one genuinely nonlinear positive quadratic radius law
across the full explicit oblique frame. A fixed-weight tensor-product station construction keeps
shared tangent boundaries exact while preserving rational circular sections, fixed retained outer
supports, and explicit finite caps as `V8/E12/F6/L6`. The analytic volume integrates the oblique
removed-corner coefficient against the quadratic radius square; station tangent distances,
outward normals, rational-band presence, transactional refusals, and a distinct sharp/rounded
proof are covered by `ObliqueQuadraticEdgeFilletVerification` and
`Proofs/Phase38h_ObliqueQuadraticEdgeFillet.png`. Partial intervals, arbitrary variable laws,
freeform supports, apexes, rolling-ball G2, and healing remain unsupported. Its plan is
`docs/PLAN_ObliqueQuadraticEdgeFillet.md`.

#### Phase 38i: bounded rolling-ball-core G2 planar transition ✅

The explicit perpendicular planar corner now has a distinct curvature-matched route: quintic
support transitions leave each plane with zero curvature, meet an exact rational circular
rolling-ball core at `1/r`, and retain the core through the middle of the profile. Three split
profile surfaces plus four retained support surfaces sew as `V14/E21/F9/L9`; the exact profile
line-integral volume, curvature joins, rational core, outward normals, refusal boundaries, and
the distinct G2/core-versus-pure-rolling proof are covered by `G2RollingBallVerification` and
`Proofs/Phase38i_G2RollingBall.png`.

This is not a pure quarter-circle G2-to-plane join and does not claim rolling-ball G2 on arbitrary
B-reps. Variable-radius cores, oblique/freeform supports, arbitrary edge selection, apexes, and
healing remain unsupported. Its plan is `docs/PLAN_G2RollingBall.md`.

#### Phase 38j: bounded oblique rolling-ball-core G2 transition ✅

The Stage 4i core-and-transition construction now accepts one explicit strict non-orthogonal
support wedge with unequal finite widths. Exact tangent distance `r cot(theta/2)` defines the
support contacts; quintic zero-curvature transitions meet an exact rational circular core over
the oblique `pi - theta` arc, and three profile surfaces plus three retained support surfaces
sew as `V12/E18/F8/L8`. Curvature joins, the profile line-integral volume, outward normals,
transactional frame/angle/width refusals, and the distinct sharp-versus-oblique proof are covered
by `ObliqueG2RollingBallVerification` and `Proofs/Phase38j_ObliqueG2RollingBall.png`.

Variable-radius cores, freeform or mixed supports, arbitrary edge selection, apexes, pure
quarter-circle G2-to-plane contacts, and healing remain unsupported. Its plan is
`docs/PLAN_ObliqueG2RollingBall.md`.

#### Phase 38k: bounded quadratic variable-radius rolling-ball-core G2 ✅

The perpendicular Stage 4i route now accepts one genuinely nonlinear positive quadratic radius
law over one complete explicit edge. Three exact station profiles retain zero-curvature planar
joins, `1/r(t)` circular-core curvature, and rational core sections; fixed homogeneous station
surfaces preserve the nonlinear law, retained supports, and finite caps as `V14/E21/F9/L9`.
Integrated radius-square volume, station curvature, outward normals, transactional law/frame
refusals, and a variable-versus-constant proof are covered by `VariableG2RollingBallVerification`
and `Proofs/Phase38k_VariableG2RollingBall.png`.

Oblique variable G2, freeform/mixed supports, arbitrary edge selection, variable-radius apexes,
pure quarter-circle G2-to-plane contacts, and healing remain unsupported. Its plan is
`docs/PLAN_VariableG2RollingBall.md`.

#### Phase 38l: bounded oblique quadratic variable-radius rolling-ball-core G2 ✅

The oblique Stage 4j route now accepts one genuinely nonlinear positive quadratic radius law
across one complete explicit edge with unequal finite support widths. Exact `r(t) cot(theta/2)`
tangent stations retain rational `pi - theta` cores and zero-to-`1/r(t)` curvature joins;
fixed homogeneous station surfaces and three retained wedge strips sew as `V12/E18/F8/L8`.
Integrated radius-square volume, station curvature, outward normals, transactional frame/law
refusals, and the distinct variable-versus-constant proof are covered by
`ObliqueVariableG2RollingBallVerification` and `Proofs/Phase38l_ObliqueVariableG2.png`.

Freeform or mixed supports, arbitrary edge selection, variable-radius apexes, pure quarter-circle
G2-to-plane contacts, and healing remain unsupported. Its plan is
`docs/PLAN_ObliqueVariableG2RollingBall.md`.

#### Phase 38m: bounded eligible-edge dispatch for G2 reconstruction ✅

The selected-edge layer now accepts one explicit edge from an existing B-rep only when it is
straight and manifold, its two adjacent faces are planar rectangles, and the interior corner is
strictly orthogonal. Adjacent face vertices provide finite positive support widths; unequal widths,
curved or non-rectangular supports, and oblique parallelogram supports refuse because the existing
G2 specification has one common width parameter.

`ClassifyG2RollingBallEdge` validates the radius and transition angle, deterministically orients the
support vectors, and returns the already-verified `G2RollingBallPlanarCornerSpecification`. It does
not mutate the source; reconstruction remains a separate transactional call. The distinct
`EligibleG2EdgeDispatchVerification` performs 21 checks for discovery, exact length/width/radius
extraction, `V14/E21/F9/L9` reconstruction, outward normals, source immutability, consuming-radius
and invalid-transition refusals, curved/unequal-width/non-manifold refusals, and an oblique/parallelogram refusal.
The proof is `Proofs/Phase38m_EligibleG2EdgeDispatch.png`.

This remains bounded dispatch, not arbitrary B-rep rolling-ball G2: edge loops, source-body
replacement, healing, curved/freeform supports, unequal widths, oblique dispatch, and arbitrary
selected-edge routes remain unsupported. Its plan is `docs/PLAN_EligibleG2EdgeDispatch.md`.

#### Phase 38n: bounded eligible-edge dispatch for oblique G2 reconstruction ✅

The selection layer now covers one strict non-orthogonal rectangular prism corner without broadening
into arbitrary B-rep filleting. A straight manifold edge shared by two planar rectangular faces is
classified from its topology and vertices; finite support widths may be unequal, and deterministic
orientation returns the existing `ObliqueG2RollingBallPlanarCornerSpecification` with its exact
interior angle, edge length, radius, and transition angle.

`EligibleObliqueG2EdgeDispatchVerification` performs 23 checks over a six-vertex triangular-prism
source: exact `60°` frame and `8/6/5` dimensions, deterministic orientation, separate
`V12/E18/F8/L8` reconstruction, outward normals, source immutability, and orthogonal/curved/
non-manifold/out-of-range/consuming-radius/invalid-transition refusals. The distinct proof is
`Proofs/Phase38n_EligibleObliqueG2EdgeDispatch.png`.

This remains bounded dispatch, not arbitrary oblique B-rep G2: edge loops, source-body replacement,
healing, curved/freeform supports, invalid corners, and arbitrary selected-edge routes remain
unsupported. Its plan is `docs/PLAN_EligibleObliqueG2EdgeDispatch.md`.

#### Phase 38o: bounded eligible-edge dispatch for variable-radius G2 reconstruction ✅

The orthogonal eligible-edge layer now accepts one genuinely nonlinear positive quadratic radius law
without broadening the source-selection boundary. The classifier inherits Stage 4m's straight,
manifold, planar-rectangular, strict-orthogonal, equal-width conditions, extracts the explicit frame,
validates the full radius law and finite support feasibility, and returns the verified
`VariableG2RollingBallPlanarCornerSpecification` without mutating the source.

`EligibleVariableG2EdgeDispatchVerification` performs 26 checks covering exact `4/4/.25/.35/.50`
frame and station extraction, deterministic orientation, separate `V14/E21/F9/L9` reconstruction,
outward normals, source immutability, and constant/non-positive/consuming-law, invalid-transition,
oblique/unequal-width/curved/non-manifold refusals. The distinct proof is
`Proofs/Phase38o_EligibleVariableG2EdgeDispatch.png`.

This remains orthogonal variable-radius dispatch only: oblique variable dispatch, arbitrary B-rep
variable filleting, edge loops, healing, curved/freeform supports, and source-body replacement remain
unsupported. Its plan is `docs/PLAN_EligibleVariableG2EdgeDispatch.md`.

#### Phase 38p: bounded eligible-edge dispatch for oblique variable-radius G2 reconstruction ✅

The bounded application layers now combine the Stage 4n oblique rectangular-edge frame with the
verified Stage 4l oblique variable-radius G2 route. A strict non-orthogonal rectangular prism edge
may carry unequal finite support widths and one genuinely nonlinear positive quadratic radius law;
exact angle, length, widths, station radii, and deterministic orientation are retained without
mutating the source.

`EligibleObliqueVariableG2EdgeDispatchVerification` performs 28 checks over a six-vertex triangular
prism: exact `60°` and `8/6/5` frame extraction, `.25/.35/.50` station extraction, deterministic
orientation, separate `V12/E18/F8/L8` reconstruction, outward normals, source immutability, and
constant/non-positive/consuming-law, invalid-transition, orthogonal/out-of-range/curved/non-manifold
refusals. The distinct proof is `Proofs/Phase38p_EligibleObliqueVariableG2EdgeDispatch.png`.

This remains bounded oblique variable dispatch only: arbitrary B-rep variable filleting, edge loops,
healing, curved/freeform supports, invalid corners, and source-body replacement remain unsupported.
Its plan is `docs/PLAN_EligibleObliqueVariableG2EdgeDispatch.md`.

#### Phase 38q: bounded native-cone apex vertex dispatch ✅

The verified Stage 4b spherical-cap route now has a conservative vertex-selection layer for one
canonical native right-circular cone. `ClassifyConeApexFilletVertex` structurally recognizes the
closed `V2/E2/F2` cone topology, its native cone/planar-face pair, circular base rim, straight apex
seam, and unique axis vertex; it derives the base, axis, base radius, and height instead of trusting
face order or a caller-supplied specification. Reconstruction remains a separate transaction.

`ConeApexVertexDispatchVerification` performs 24 checks using a distinct 5-by-7 cone and 0.6 fillet:
exact apex extraction, deterministic dispatch, separate `V4/E5/F3/L3` spherical-cap reconstruction,
outward normals, source immutability, base/out-of-range/invalid/consuming/cylinder/malformed refusals,
and the distinct proof `Proofs/Phase38q_ConeApexVertexDispatch.png`.

This is not general vertex-selected apex filleting: mixed supports, partial sectors, arbitrary cones,
freeform/curved roots, edge loops, healing, and source-body replacement remain unsupported. Its plan
is `docs/PLAN_ConeApexVertexDispatch.md`.

#### Phase 38r: bounded half-turn partial-cone apex vertex dispatch ✅

The apex selection layer now covers one canonical closed half-turn partial cone. The classifier
recognizes the exact `V4/E6/C12/L4/F4` native revolution/base topology, derives its unique base-center
and axis-apex pair plus equal base-rim radius, and returns a half-turn
`PartialConeApexFilletSpecification` only for the selected apex. The existing partial spherical-cap
route performs the separate reconstruction transaction.

`PartialConeApexVertexDispatchVerification` performs 27 checks over a distinct 5-by-7 half-turn
source: exact base/axis/radius/height/sweep extraction, deterministic dispatch, separate
`V6/E9/F5/L5` reconstruction, outward normals, source immutability, non-apex/full-turn/invalid/
consuming/cylinder/malformed refusals, and `Proofs/Phase38r_PartialConeApexVertexDispatch.png`.

This remains a half-turn partial-apex slice, not general partial-sector or vertex-selected apex
filleting. Non-half-turn sectors, mixed supports, arbitrary cones, freeform/curved roots, edge loops,
healing, and source-body replacement remain unsupported. Its plan is
`docs/PLAN_PartialConeApexVertexDispatch.md`.

#### Phase 38s: bounded non-reflex partial-cone apex vertex dispatch ✅

The partial-apex vertex layer now accepts one canonical non-reflex native revolve surface set with
`V4/E6/C8/L3/F3` topology and a strict sweep below `pi`. It derives the unique base-center/axis-apex
pair, equal base-rim radius, height, and sweep angle, then returns the existing partial spherical-cap
specification; reconstruction remains a separate transaction.

`GeneralPartialConeApexVertexDispatchVerification` performs 28 checks over a distinct 60-degree
5-by-7 source: exact base/axis/radius/height/sweep extraction, deterministic dispatch, separate
`V6/E9/F5/L5` reconstruction, outward normals, source immutability, non-apex/half-turn/full-turn/
invalid/consuming/cylinder/malformed refusals, and
`Proofs/Phase38s_GeneralPartialConeApexVertexDispatch.png`.

This remains a non-reflex native-revolve slice, not arbitrary partial-sector or vertex-selected apex
filleting. Reflex/zero/full sweeps, mixed supports, arbitrary cones, freeform/curved roots, edge loops,
healing, and source-body replacement remain unsupported. Its plan is
`docs/PLAN_GeneralPartialConeApexVertexDispatch.md`.

#### Phase 38t: bounded reflex partial-cone apex vertex dispatch ✅

The partial-apex vertex layer now accepts one canonical reflex native revolve surface set with
`V4/E6/C8/L3/F3` topology and a strict sweep between `pi` and `2pi`. It unwraps the native base-rim
circular path rather than reducing the endpoints to the minor angle, derives the unique base-center/
axis-apex pair and equal positive base-rim radius, then returns the existing partial spherical-cap
specification; reconstruction remains a separate transaction.

`ReflexPartialConeApexVertexDispatchVerification` performs 28 checks over a distinct 270-degree
5-by-7 source: exact base/normalized-axis/radius/height/reflex-sweep extraction, deterministic dispatch,
separate `V6/E9/F5/L5` reconstruction, outward normals, source immutability, non-apex/non-reflex/
zero/half-turn/full-turn/invalid/consuming/cylinder/malformed refusals, and
`Proofs/Phase38t_ReflexPartialConeApexVertexDispatch.png`.

This remains bounded to native reflex partial-revolve sectors, not arbitrary partial-sector or
vertex-selected apex filleting. Mixed supports, arbitrary cones, freeform/curved roots, edge loops,
healing, and source-body replacement remain unsupported. Its plan is
`docs/PLAN_ReflexPartialConeApexVertexDispatch.md`.

#### Still required in Phase 34–36

General curved-face and curved-edge tweaks beyond the native analytic cylinder/cone/root routes, curved edge loops beyond
the complete plane/cylinder, plane/cone, cylinder/cone, half-turn partial plane/cylinder, partial plane/cone, partial cone/cylinder, partial cone/cone, complete apex plane/cone, partial apex plane/cone root chamfers, and complete narrowing cylinder/cone root fillets, further mixed-support chamfers, arbitrary non-prismatic
concave/non-convex planar chamfer networks, non-planar loops on non-planar/freeform supports, corner patches,
and general intersection/trim/sew resolution remain future topology work. Phase 33 still requires broader variable-radius
application, partial-edge blends, nonlinear/G2 surface construction, unequal support-setback variants, and broader
cone/cone, cone/cylinder, apex, unequal-radius, and non-orthogonal support pairs. Generic multi-section curve/area lofts, guide
curves, and multi-loop lofts with through-holes already exist in the earlier SkinSolver routes and remain separately
bounded by their existing verification coverage.

## Later direct modelling and platform work

After the 2D and blend foundations are reliable, implement and validate these as isolated features:

1. robust coincident/tangent Boolean classification, healing, sliver removal, and face merge;
2. general shell/thicken, face offset, draft, move/delete/replace face, extend/trim;
3. holes, threads, ribs, bosses, patterns, and stable ordered feature history;
4. STEP/DXF/STL/3MF/OBJ exchange and controlled tessellation;
5. assemblies, mates, BOM, mass properties, drawing extraction, and manufacturing workflows;
6. a production interactive UI and GPU viewport.

Neutral formats are required for exchange but do not replace the native `.arc` source of truth: final-shape STEP B-rep
transfer commonly loses feature-tree and sketch-constraint behaviour. [NIST procedural-model exchange discussion](https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=904157)

## Working policy

- Implement one bounded capability per change.
- Start each capability by adding failure-oriented test geometry.
- Use analytic quantities when available; otherwise declare numerical tolerance and measure it.
- Treat a closed/manifold body as necessary but not sufficient: inspect close visual proofs at the actual requested edge.
- Keep unsupported domains explicit and refused rather than returning plausible but broken geometry.
