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

### Phase 33: variable radius, setbacks, partial edges, and G2

Variable-radius blends need a radius law along the spine, feasibility detection, and a non-linear solve. G2 continuity
requires its own surface construction and curvature acceptance measurements. These are not small extensions of the
current constant-radius planar implementation.

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
