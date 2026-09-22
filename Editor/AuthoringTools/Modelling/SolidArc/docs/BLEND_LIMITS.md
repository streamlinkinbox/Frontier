# Edge blends — exact face pushes and verified edge cuts

`Kernel/BlendSolver.cpp` implements a planar chamfer as a set operation and a fillet as a tangent cylinder seated on the corresponding flat. `Verification/BlendVerification.cpp` checks topology and volume across the complete pushed-spanner perimeter; this document records the current, reproducible bounds.

## The push-seam defect is removed

The former implementation made `push` by unioning an extrusion whose footprint was pulled inward by a few microns. That made the Boolean transversal, but left a narrow annular remnant of the old face. Its four inner boundary edges were implementation artefacts, not designed edges. They were only about 24 μm away from the real perimeter, so no 3 mm blend could legally fit there; those were the five reported refusals.

`PushFace` now first uses a direct B-rep construction for planar faces:

1. translate the selected face and its boundary edges to its new cap position;
2. retain each original boundary edge at the root;
3. make one ruled wall per boundary edge, sharing the root, cap, and vertical joins; and
4. orient and validate the assembled body before accepting it.

The Boolean push remains as a fallback if a future input cannot be sewn by the direct path. For a full-face push this produces the intended topology exactly: **16 vertices, 26 boundary edges, 12 faces, χ=2, genus 0**, and the expected volume is reached to round-off. The top/bottom root joins are coplanar tangent seams, not corners; `Frame` correctly excludes those two from the blend set.

## Current perimeter sweep

The checked sample is a hexagonal prism with one complete side face pushed 26 mm. The `BlendVerification` sweep starts from the direct-push result and tries every physical planar corner.

| check | current result |
|---|---:|
| physical, non-tangent blend corners | 24 |
| chamfers accepted | **24 / 24** |
| fillets accepted | **24 / 24** |
| returned non-solid bodies | **0** |
| chamfers within 3 μm³ of the closed form | **24 / 24** |
| worst chamfer error | **< 0.000003 mm³** |

This replaces the previous **25 / 30** sweep, its five artificial seam refusals, and its 1% fallback bound. The test now requires all 24 physical corners to chamfer and fillet, no broken body, and every accepted chamfer to reach the 3 μm³ numerical-integration bound. These are executable regression conditions, not image comparisons.

A planar chamfer has an exact local construction. If no cutter placement lands within the round-off bound, `ChamferEdge` now returns an explicit refusal rather than a "closest" but incorrect solid. The pushed-spanner sweep has an exact placement for all 24 physical corners, including both 120-degree shoulders.

## Tolerance hierarchy

Kernel tolerance policy is centralized in `ScalarCriteria` and deliberately separates decision bands:

- `KernelTolerance` (`1e-9`): coincidence and exact parameter boundaries.
- `GeometricTolerance` (`1e-8`): strict frame, axis, and geometric classification.
- `CurveTolerance` / `ScaledPositionTolerance` (`1e-7`): curve subdivision, intersections, and bounded rim positions.
- `AngularTolerance` (`1e-7`): direction classification; `SweepTolerance` (`1e-6`) handles finite arc-span endpoints.
- `CircularTolerance` / `DirectionTolerance` / `DistanceTolerance` (`1e-6`): circular rims, legacy face-edge direction checks, and local surface distances.
- `MergeTolerance` (`1e-6`): topological sewing and positive-clearance decisions.
- `VolumeTolerance` (`1e-3`): scaled analytic-versus-tessellated volume acceptance.
- `ChordTolerance` (`1e-4`): tessellation sagitta, not a topology acceptance threshold.

A smaller tolerance is not automatically safer: each category must match the numerical operation being classified. New kernel code should use a named policy constant or helper and add boundary acceptance/refusal coverage.

## Cutter precision

A bounded cutter needs end caps. A cap placed exactly at an edge end is a non-transversal intersection; placing it well past the end can trim a neighbouring face. `ChamferEdge` therefore evaluates a deterministic ladder of **100** cutter placements: five lateral half-widths and twenty positive/negative along-edge margins, retains the closest closed candidate, and returns it only when its measured volume is within **3 μm³** of the exact closed-form wedge. Otherwise the operation explicitly refuses the inaccurate cut. That tolerance is the tessellated volume integrator's observed numerical floor, not a percentage allowance.

The ladder now includes micro-margins from `1e-8` through `1e-3` in both directions. This lets the Boolean clear a seam without paying the old 0.018 mm³ minimum over-cut from its coarser first step. The direct face push removes the one topology in which no local cutter could be valid at all.

Analytic-volume acceptance gates are centralized as `ScalarCriteria::VolumeTolerance` (`1e-3` in model units) instead of repeating a solver-local literal. The current acceptance behavior is unchanged; future tolerance tightening is now a single reviewed policy change.

## Fillets

`FilletEdge` first makes the tangent-set-back flat and then replaces that face with the rolling cylindrical surface. It evaluates both the original cap edges and square-end circular cap sections, retaining the valid closed body closer to the analytic removal. A candidate that leaves less material than the flat it replaces is rejected: a convex roll must add material back relative to its tangent chamfer.

### Exact plane–cylinder boss roots

Phase 31 adds the first bounded smooth-support route that does not depend on a native primitive cap. The classifier finds
a rational circular edge shared by a planar annular shoulder and cylindrical boss, confirms the complete five-face
stepped topology, and derives the common axis, shoulder plane, inner/outer radii, and support extents from geometry and
adjacency rather than face order.

For fillet radius `r`, the offset plane and offset boss cylinder intersect on a circular spine of radius `Rb + r`, one
radius above the shoulder. Revolving an exact rational quadratic quarter-circle about that spine produces the retained
partial torus. Its lower contact circle is at radius `Rb + r` in the shoulder plane; its upper contact circle is at
radius `Rb` and height `shoulder + r`. Both joins are G1. The operation directly sews the retained outer cylinder,
trimmed annulus, quarter-torus, shortened boss cylinder, and two caps into a `V5/E9/C18/L6/F6` solid. The concave
roll adds the analytic volume
`ΔV = 2πr²[Rb(1 − π/4) + r(5/6 − π/4)]`; verification requires the tessellated solid volume to follow that value.

The supported radius interval is strictly `0 < r < min(boss height, outer radius − boss radius)`. A radius at either
upper bound consumes a support and refuses. Phase 32a also accepts a complete boss-root ring split into two or four
rational arc edges by angular representation seams. `TangentChain` follows only unambiguous G1 edge continuations; the
classifier then requires a closed `2π` ring and matching shoulder, boss, and outer-wall patch sets before it heals all
seams into the canonical exact result. Selecting either member of a two-edge chain or any member of a four-edge chain
therefore produces the same solid.

Phase 32b admits a half-turn of the stepped solid bounded by one planar diameter face. The root chain spans `π` with two
endpoints and matching bottom/top sector patches. Its internal two/four-member representation seams heal to one partial
torus, while two exact quarter-circle meridians terminate the roll on the retained diameter cap. The result is one hull
of genus zero with `V12/E17/C34/L7/F7`; its volume is one half of the corresponding full-ring result.

Phase 32d extends only this rotational-sector topology. A general open chain's signed arc span is derived by walking its
members in endpoint order, and the outer circular chain must have the same magnitude. Two distinct radial cap planes
must contain the corresponding start/end rays and meet on one axis edge. Reconstruction closes those radial paths
explicitly and returns a seam-healed `V12/E18/C36/L8/F8` result. Positive/negative quarter turns, 120° and reflex 270°
spans, and oblique axes are verified. The result volume is the absolute angular fraction of the full-ring analytic value.
This does not cover unequal, mitred, free-form, or otherwise non-radial endpoint supports.

### Exact plane–cone boss roots

Phase 32z is the first unequal-radius support pair. The boss is a native conical frustum whose foot circle `R_f` sits on
the planar annular shoulder and whose top circle `R_t ≠ R_f` closes the top cap, so the wall is a cone with half-angle
`tan α = (R_f − R_t) / H`; `α > 0` narrows upward and `α < 0` is an undercut flare. The classifier requires the closed
genus-zero `V4/E7/C14/L5/F5` stepped solid, a root rim shared by a measured planar shoulder and a native cone whose tag
agrees with its sampled end rows and straight generators, a concentric outer rim into a native cylinder wall, and two
planar caps with a top-rim radius check. The shoulder's revolution seam is skipped geometrically, as Phase 31 does.

Plane and coaxial cone are both surfaces of revolution about one axis, so their `r`-offsets meet in an exact circular
spine and the rolling ball sweeps an exact rational torus band:

```
z_t = r (1 − sin α)                      cone contact height above the shoulder
ρ_t = R_f − z_t tan α                    cone contact radius
ρ_c = R_f + r (1 − sin α) / cos α        spine radius = shoulder contact radius
meridian arc from π + α to 3π/2          span π/2 − α, centred at (ρ_c, r)
```

At `α = 0` every quantity reduces to Phase 31 (`ρ_c = R_f + r`, quarter turn). The added wedge is the meridian region
bounded by the shoulder, the generator and the arc, revolved about the axis; Pappus gives it exactly as `2π` times the
region's first moment (quadrilateral `(R_f, 0)–(ρ_c, 0)–(ρ_c, r)–(ρ_t, z_t)` minus the circular sector), and that
closed form equals Phase 31's `ΔV` at `α = 0` to `1e-12`. The route refuses its own result if the tessellated volume
disagrees with `V_source + ΔV` beyond the kernel's `VolumeTolerance`.

The supported radius interval is strictly `r > 0`, `z_t < H` (the cone contact stays below the top rim), and
`ρ_c < R_outer` (the shoulder contact stays inside the outer wall); the contact radius `ρ_t` is always positive for a
frustum because it reaches `R_t > 0` only at `z_t = H`, so that gate is defensive. Verification exercises each limit on
a fixture where it binds first: the boundary radius refuses and `0.98×` rolls exactly. Apex cones (`R_t = 0`), the
conical top rim, the outer shoulder rim, and Boolean-built sources without a canonical root rim all refuse. Cone–cone
and cone–cylinder pairs, non-radial endpoint supports, and variable-radius laws are not claimed.

**Declared measurement tolerances.** The kernel measures volume by tessellation at a `1e-4` sagitta, which leaves even
the sharp source body `≈ 3.8e-4` below its exact volume; the total-volume check therefore uses the kernel's own `1e-3`
gate. The sharper test is differential: `(V_rounded − V_source)` cancels that shared floor and follows the Pappus
wedge to `≤ 1.5e-3` of the wedge across narrowing, flaring, steep, `α = 0`, oblique, reversed, and both boundary
fixtures; it is gated at `5e-3`. Torus residual is gated at `1e-9` (measured `≤ 3e-14`) and both G1 breaks at `1e-10`
(measured `0`).

### Complete plane–cone boss-root chamfers

Phase 36f adds a separate, deliberately bounded chamfer route for the same complete circular plane–cone root. The native
cone is sampled at both end rows and through its straight generators; a setback `s` travels along the measured cone slant,
so the axial contact is `z = sH / sqrt(H² + (R_t − R_f)²)` and the contact radius is the linear cone radius at `z`.
The exact reconstruction keeps the outer cylinder, annular shoulder, and top support, and inserts the straight conical
band between `(R_f + s, 0)` and `(R_contact, z)`. The result is checked as one genus-zero `V5/E9/F6` solid, and the
added material is measured against the exact square-radius meridian integral. Narrowing and flaring frusta are covered.

The route refuses non-positive or consuming setbacks, zero-radius/apex cones, partial root loops, arbitrary torus/freeform
edges, cone–cylinder or non-coaxial support pairs, curved networks/corner patches, and general intersection/trim/sew
healing. These are not inferred from a visually similar edge: only the canonical complete plane–cone topology is accepted.

### Complete cylinder–cone boss-root chamfers

Phase 36g covers the next complete coaxial mixed-support root: a cylindrical boss ending at a conical frustum. A setback
`s` moves down the cylindrical support by `s` and along the cone by `sH / sqrt(H² + (R_t − R_b)²)`. The exact chamfer
band is the revolved straight meridian between those two measured contacts; the retained cylinder and cone are rebuilt
with native analytic supports. The result is required to be genus-zero `V6/E11/F7` with two cylinders, two cones, one
revolved shoulder, and two caps. Differential volume is checked against the exact square-radius integral of the band
minus the original cylinder/cone boundary, including both narrowing and flaring cones.

Only the canonical complete `V5/E9/C18/L6/F6` source is accepted. Zero/negative or consuming setbacks, partial rings,
non-coaxial or oblique cone/cylinder intersections, multiple curved roots, arbitrary freeform edges, curved
networks/corner patches, and general intersection/trim/sew healing remain explicit refusals.

### Half-turn partial curved-root chamfers

Phase 36h extends the plane–cylinder root route to one physical semicircle represented by two open rational arc members.
Selecting either member propagates the complete chain and rebuilds the outer wall, annular shoulder, conical band,
retained boss, endpoint meridians, and one planar diameter cap. The exact accepted result is genus-zero `V12/E17/F7`; its
added annular-cone wedge is one-half of the complete-ring value. The source topology is the bounded split-chain
`V14/E23/C46/L11/F11` form.

Only the half-turn is accepted in this slice. General-angle sectors, arbitrary partial arcs, incomplete or branched
chains, partial cone/cylinder pairs, multiple curved roots, freeform edges, curved networks/corner patches, and general
intersection/trim/sew healing remain explicit refusals.

### General-angle partial curved-root chamfers

Phase 36i accepts one bounded non-reflex sector of the same planar-shoulder/cylindrical-boss chain. The two endpoint
meridians and radial cap faces are healed explicitly; the accepted result is genus-zero `V12/E18/C36/L8/F8`, and its
added annular-cone wedge is `|θ|/(2π)` of the complete-ring value. The source remains the split-chain
`V14/E24/C48/L12/F12` topology.

Reflex sectors, arbitrary partial arcs, incomplete or branched chains, partial cone/cylinder pairs, multiple curved roots,
freeform edges, curved networks/corner patches, and general intersection/trim/sew healing remain explicit refusals.

### Partial plane–cone root chamfers

Phase 36j accepts one canonical open coaxial conical-frustum sector shared by a planar annular shoulder. The classifier
measures the complete open root chain, shoulder outer cylinder, cone endpoints/radii, and radial endpoint caps; it accepts
the physical half-turn or one non-reflex general sector only. Setback `s` follows the measured cone slant
`q = sqrt(H² + (R_t − R_f)²)`, so the axial contact is `z = sH/q` and the retained cone contact radius is
`R_f + (R_t − R_f)z/H`. The exact reconstruction retains the outer wall and top, inserts the straight meridian
between `(R_f+s, 0)` and `(R_contact, z)`, and heals each radial endpoint cap without a general Boolean trim/sew.
The general-sector result is `V12/E18/C36/L8/F8`; the half-turn result is `V12/E17/L7/F7`.

This route is not a generic partial cone solver. Reflex/arbitrary trimmed sectors, mixed cone/cylinder partial roots,
non-coaxial or oblique supports, branched/incomplete chains, multiple curved roots, freeform edges, curved
networks/corner patches, and general intersection/trim/sew healing refuse transactionally.

### Partial cone–cylinder root chamfers

Phase 36k accepts one canonical open coaxial cone/cylinder root sector. The cone ends at radius `R_b` and the cylinder
continues above that same circle; a setback `s` travels `s` along both measured supports. If the cone slant is
`q = sqrt(H² + (R_base − R_b)²)`, the retained cone contact is `z = H − sH/q` and
`R_contact = R_b + (R_base − R_b)s/q`; the retained cylinder begins at `z = H + s`. The new straight meridian
between those points is revolved exactly, with the half-turn or general sector radial caps healed explicitly. The general
accepted topology is `V10/E15/C30/L7/F7`; the half-turn is `V10/E14/C28/L6/F6`. The measured wedge volume follows
Pappus for the triangle between the original cone/cylinder corner and the setback chord.

This is not a general cone/cylinder solver. Cone–cone pairs, non-coaxial or oblique supports, apex/zero-radius cones,
reflex or arbitrary trimmed arcs, branched/incomplete chains, multiple curved roots, freeform supports, curved
networks/corner patches, and general intersection/trim/sew healing remain explicit refusals.

### Partial cone–cone root chamfers

Phase 36l accepts one canonical open coaxial root shared by two native conical frusta. With lower contact setback
`z_l = sH_l / sqrt(H_l² + (R_base − R_b)²)` and upper contact setback
`z_u = sH_u / sqrt(H_u² + (R_top − R_b)²)`, the exact meridian replaces the old two-segment corner by the straight
chord between the two measured cone contacts. The retained lower and upper cones, axial caps, and radial endpoint caps
remain analytic. The accepted general topology is `V10/E15/C30/L7/F7`; the half-turn is `V10/E14/C28/L6/F6`, and the
wedge volume is the Pappus volume of the triangle between the old corner and the setback chord.

This is not a general cone–cone solver. Non-coaxial or oblique supports, partial apex sectors, cone–cone/cylinder–cone
apex networks, reflex or arbitrary trimmed arcs, branched/incomplete chains, multiple curved roots, freeform supports,
curved networks/corner patches, and general intersection/trim/sew healing remain explicit refusals.

### Complete apex plane–cone root chamfers

Phase 36m accepts one complete coaxial native conical boss whose top endpoint has radius zero. The root remains the measured
circular plane–cone contact; the apex is validated as a single finite endpoint on the cone axis, not as a fabricated top cap.
The exact route retains the apex cone, reconstructs the outer cylinder and shoulder, and inserts the setback band. The output
is genus-zero `V6/E9/C18/L5/F5`, with the wedge volume checked against the square-radius meridian integral.

This is a complete-root apex slice only. Partial apex sectors, non-coaxial or oblique supports, apex networks, arbitrary
trimmed/reflex/branched/incomplete roots, freeform supports, curved corner patches, and general intersection/trim/sew
healing remain explicit refusals.

### Partial apex plane–cone root chamfers

Phase 36n accepts one canonical open coaxial apex sector. The zero-radius endpoint is measured as the cone-axis apex and
is retained as one vertex; the half-turn or non-reflex general sector keeps exact outer/shoulder/chamfer/apex-cone supports
and heals the radial endpoint caps. General topology is `V10/E15/C30/L7/F7`; half-turn topology is `V10/E14/C28/L6/F6`,
with the wedge volume checked against the apex square-radius meridian integral.

Partial apex networks, non-coaxial or oblique supports, arbitrary trimmed/reflex/branched/incomplete roots, freeform
supports, curved corner patches, and general intersection/trim/sew healing remain explicit refusals.

### Partial plane–cone root fillets

Phase 36o accepts one canonical open coaxial plane–cone sector with a positive-radius conical top. The constant-radius
rolling circle is solved analytically from the cone half-angle, the exact circular meridian is revolved over the measured
half-turn or non-reflex sweep, and only the general sector's two radial endpoint caps are healed. The accepted result is
`V12/E18/C36/L8/F8` for a general sector and `V12/E17/C34/L7/F7` for a half-turn; the wedge volume follows the Pappus
first-moment identity within the tessellation floor.

This is not a generic partial-edge or variable-radius fillet solver. Apex/zero-radius cones, non-coaxial or oblique
supports, reflex or arbitrary trims, branched/incomplete chains, cone–cone/cylinder–cone apex networks, curved corner
patches, nonlinear/G2 construction, and general intersection/trim/sew healing remain explicit refusals.

### Complete cylinder–cone root fillets

Phase 36p accepts one complete coaxial narrowing cylinder–cone root with a positive conical top radius. The exact
meridian roll is tangent to the cylindrical wall below the measured root and the conical wall above it; the retained
outer cylinder, planar shoulder, shortened boss cylinder, toroidal roll, retained cone, and caps sew to one genus-zero
`V6/E11/C22/L7/F7` solid. The removed volume is checked by the analytic first moment of the cylinder/cone/arc meridian
wedge.

Flaring or zero-angle pairs, apex/zero-radius cones, partial roots, non-coaxial or oblique supports, arbitrary or
branched roots, variable-radius/nonlinear/G2 construction, curved corner patches, and general intersection/trim/sew
healing remain explicit refusals.

### Complete cone–cone root fillets

Phase 36q accepts one complete coaxial pair of conical frusta whose narrowing slope increases across the shared root.
The two cone generators determine one exact circular meridian roll; the retained lower cone, toroidal roll, retained upper
cone, and planar caps sew to genus-zero `V4/E7/C14/L5/F5`. The accepted volume is checked against the analytic first
moment of the removed cone–cone meridian wedge.

Partial sectors, flaring or equal-slope pairs, apex/zero-radius cones, non-coaxial or oblique supports, arbitrary or
branched roots, variable-radius/nonlinear/G2 construction, curved corner patches, and general intersection/trim/sew
healing remain explicit refusals.

### Intentional multi-edge sets

Phase 32c introduces `FilletEdges` as a transactional composition layer. Every source seed is validated and expanded to
its tangent chain before construction. Repeated indices and multiple selected members of one chain collapse to one
target. Vertex-disjoint targets are sorted by orientation-free start/middle/end geometry, then geometrically re-resolved
after each earlier roll changes edge numbering. All results live in a working copy; one failed or ambiguous target
rejects the complete call. The original body is never mutated.

Chains sharing any source vertex refuse up front as an unsupported corner set. This is stricter than applying edges one
at a time on purpose: two adjacent rolling cylinders do not supply the three-face corner patch needed to close their
intersection. The console body form of `fillet --edges=i,j,…` now uses the same all-or-nothing route and reports the
number of distinct chains committed. The verified independent case is two opposite straight box edges at one common
radius, producing `V12/E18/C36/L8/F8`; this does not imply support for general blend/blend intersections.

The outer shoulder rim, incomplete/non-circular chains, asymmetric or non-radial endpoints, cylinder–cylinder contacts,
arbitrary trimmed/free-form surfaces, branching chains, thin-wall interactions, arbitrary holes, oblique bores, unsupported stepped-cavity sets beyond the named routes, more-than-eight through-holes, unequal/non-orthogonal or two-edge corner patches, and
general blend/blend intersections remain unsupported; they refuse rather than entering the straight-planar approximation. The
seventeen direct verifiers measure exact torus identity/residual/span, both G1 contacts, support extents,
closed/diameter/radial-sector/multi-edge topology, endpoint meridians and caps, analytic volume direction/value,
transformed axes, chain propagation/healing/deduplication, transactional refusal bounds, and console commit/rollback.

### Orthogonal three-face corner patch

Phase 32e supports exactly three equal-radius edges incident to one vertex of a structurally verified rectangular solid. The direct reconstruction retains six planar supports, adds three radius-`r` cylinders along the selected edges, and joins them with a rational spherical octant centred one radius along each local box axis. The output is a `V13/E21/C42/L10/F10` one-hull solid. Two-edge requests and unequal or non-orthogonal corners still refuse; this route is not a general blend/blend intersection solver.

### Complete parallel-edge family

Phase 32g recognizes all four box edges parallel to one local axis. It rebuilds the orthogonal cross-section as four exact radius-`r` arcs and four retained lines, extrudes that loop through the full axis length, and caps both ends. The strict feasibility condition is `2r < min(cross-wall dimensions)`; equality or overlap refuses transactionally. Mixed four-edge selections do not enter this route.

### One coaxial through-hole

Phase 32h extends only that complete outer-family route to a rectangular extrusion with exactly one centred circular bore along the selected edge direction. The classifier requires canonical genus-one `V10/E15/C30/L9/F7` source topology, two equal coaxial circular rims, matching extrusion length, centred cross-section coordinates, and positive radial wall clearance `holeRadius < min(cross-wall dimensions)/2`. The builder adds the exact bore cylinder with inward orientation before sewing, yielding two annular cap loops and canonical `V18/E27/C54/L13/F11` topology.

### One offset axis-parallel through-hole

Phase 32i permits that one bore to move away from the centreline while remaining parallel to the complete selected outer family. Its centre must lie in the erosion of the rounded cross-section by the bore disk. For `holeRadius < filletRadius`, this domain is the inner rectangle expanded by `filletRadius - holeRadius`; the implementation checks the Euclidean distance to that inner rectangle. For larger bores, strict distances to all four retained walls apply. Equality and merge-tolerance contact refuse. The exact cylinder, annular caps, and `V18/E27/C54/L13/F11` topology are unchanged.

### Exactly two axis-parallel through-holes

Phase 32j accepts the canonical two-hole extension `V12/E18/C36/L12/F8` of the same source extrusion. Four circular rims are partitioned by end plane and paired by radius and transverse centre, so profile-loop and edge-table order do not matter. Both bore disks independently satisfy the rounded-wall erosion test, and `distance(centre0,centre1) > radius0+radius1+MergeTolerance` enforces a positive ligament. Sewing two inward rational cylinders produces genus-two `V20/E30/C60/L16/F12`, with three loops on each planar end cap.

### Bounded multi-bore set

Phase 32k generalizes the same exact construction to `3 <= N <= 8`. Canonical source topology is `V=8+2N`, `E=12+3N`, `C=24+6N`, `L=6+3N`, `F=6+N`; the rounded result is `V=16+2N`, `E=24+3N`, `C=48+6N`, `L=10+3N`, `F=10+N`, with genus `N`. End-plane/radius/centre pairing is geometric, each bore passes the wall-offset gate, and every pair passes the ligament inequality. Nine or more holes, intersecting/tangent disks, and non-canonical perforated solids refuse before fallback.

### Bounded axis-parallel blind cavities

Phase 32l recognizes canonical `V10/E15/C30/L9/F8`, genus-zero rectangular prisms with seven planar faces and one inward cylindrical wall. Two closed cylinder rims locate the entrance and planar floor; exactly one rim must lie on a prism end and the depth must remain strictly below the prism length. The outer prism is rebuilt exactly, the finite cylinder is subtracted, and the Boolean entrance curve is replaced by an exact rational circle at the original seam. Canonical output is `V18/E27/C54/L13/F12`, with seven planes, five cylinders, two exact cavity circles, and one annular end cap.

Phase 32m extends this route to exactly two separated cavities. Canonical source topology is `V12/E18/C36/L12/F10`, with eight planes and two inward cylinders; canonical rounded topology is `V20/E30/C60/L16/F14`, with eight planes, six cylinders, and four exact rational cavity circles. Cavities may enter the same or opposite ends. Pairwise feasibility computes radial separation of both transverse disks and axial separation of both bounded intervals, then requires positive finite-cylinder distance; therefore coaxial opposite-end cavities are valid when a positive axial ligament remains.

Phase 32n scales the same construction to `3 <= N <= 8`. Canonical source topology is `V=8+2N`, `E=12+3N`, `C=24+6N`, `L=6+3N`, `F=6+2N`; the rounded result is `V=16+2N`, `E=24+3N`, `C=48+6N`, `L=10+3N`, `F=10+2N`, with genus zero throughout. Both ends, safe offsets, deterministic order, rigid transforms, and all `N(N-1)/2` finite-cylinder clearance checks are supported. Oblique, intersecting, more-than-eight, and non-cylindrical cavities refuse transactionally. Parallel side entry is outside this selected-axis route and is handled separately for one through eight cavities by Phases 32r–32t; mixed-axis side sets refuse.

Phase 32o separately accepts one canonical coaxial two-diameter counterbore. The source is `V12/E18/C36/L12/F10`: one large cylinder runs from the selected end to an annular shoulder, and one smaller coaxial cylinder continues to a planar floor. Strictly decreasing radius and increasing depth distinguish it from separated cavities. Rebuilding the rounded exterior and subtracting the outer stage before the inner stage yields `V20/E30/C60/L16/F14`, with eight planes, six rational cylinders, two annular planar regions, and four exact rational rims.

Phase 32p scales that one connected chain to `3 <= N <= 8` stages. Source and rounded topology use the same linear formula as the separated finite-cavity route, but classification additionally requires one entry span, a unique contiguous shoulder chain, one common transverse centre, strictly decreasing radii, and strictly increasing cumulative depths. Deterministic largest-to-smallest subtraction restores `2N` exact rational rims.

Phase 32q separately supports exactly two two-stage chains. Its source is `V16/E24/C48/L18/F14`; the rounded result is genus-zero `V24/E36/C72/L22/F18`, with ten planes, eight rational cylinders, four annular levels, and eight exact rational rims. Every pair of axial bands must retain positive finite-cylinder distance, so same-end radial separation and coaxial opposite-end axial separation are both valid. Three or more stepped cavities, multi-stage combinations, eccentric stages, undercuts, non-decreasing radii, and more than eight stages remain unsupported.

Phase 32r separately recognizes one canonical blind cylinder whose axis follows either prism cross-section direction and whose entrance is wholly contained by one retained planar side strip. The source is again genus-zero `V10/E15/C30/L9/F8`; low/high entry, selected-axis and transverse offsets, radius, and finite depth are extracted from the inward cylinder and its two rims. The cavity disk must clear both selected-axis end caps and the rounded corners bounding its entrance wall. Reconstruction subtracts a bounded side cutter and explicitly restores both the fitted entrance rim and planar-floor rim, producing genus-zero `V18/E27/C54/L13/F12` with seven planes, five rational cylinders, one annular side wall, and two exact rational cavity rims. A second parallel side cavity is handled by Phase 32s and stepped counterbores by Phases 32u/32v/32w; oblique, through, mixed-axis, corner-crossing, end-crossing, and opposite-wall side cavities remain unsupported and refuse transactionally.

Phase 32s extends that route to exactly two separated cavities sharing one side-axis direction. Canonical source topology is `V12/E18/C36/L12/F10`; canonical rounded topology is genus-zero `V20/E30/C60/L16/F14`, with eight planes, six rational cylinders, two planar inner loops, and four exact rational rims. Entrances may occupy one retained side or opposite parallel sides. Pairwise feasibility combines bounded intervals along the common side axis with disk separation in the selected-axis/transverse plane, so coaxial opposite-side cavities remain valid across a positive axial ligament. A third parallel cavity delegates to Phase 32t and two two-stage cavities to Phase 32w; intersecting, mixed-axis, oblique, corner/end-crossing, through, and opposite-wall side combinations remain unsupported and refuse transactionally.

Phase 32t scales the same parallel-side construction to `3 <= N <= 8`. Source and rounded topology follow the genus-zero linear formulae `V=8+2N/E=12+3N/C=24+6N/L=6+3N/F=6+2N` and `V=16+2N/E=24+3N/C=48+6N/L=10+3N/F=10+2N`. Each cavity independently passes retained-strip and selected-axis end-cap clearance, every one of the `N(N-1)/2` finite-cylinder pairs retains positive separation, and deterministic cutter order restores `2N` exact rational entrance/floor rims. One-side grids and sets spanning opposite parallel sides are supported in either Y or Z direction. Mixed-axis, intersecting, ninth, oblique, stepped sets, through, corner-crossing, and end-crossing side cavities remain unsupported and refuse transactionally.

Phase 32u separately supports one canonical two-diameter side counterbore. Its source is genus-zero `V12/E18/C36/L12/F10`: a larger cylinder begins at one retained side, terminates at an annular shoulder, and a smaller coaxial cylinder continues to a planar floor. Classification requires one Y/Z direction, a common selected-axis/transverse centre, strict radius decrease, increasing finite depths, and analytic stepped volume. The outer disk passes retained-strip and end-cap clearance before deterministic bounded subtraction restores four exact rational rims. Output is genus-zero `V20/E30/C60/L16/F14`, with eight planes, six rational cylinders, and two annular levels. A third stage delegates to Phase 32v; eccentric, undercut, corner/end-crossing, through, and opposite-wall side counterbores remain unsupported and refuse transactionally.

Phase 32v generalizes that single side-entering chain to `3 <= N <= 8` stages while retaining the Phase 32u two-stage case. Source topology is `V=8+2N`, `E=12+3N`, `C=24+6N`, `L=6+3N`, `F=6+2N`; rounded topology is genus-zero `V=16+2N`, `E=24+3N`, `C=48+6N`, `L=10+3N`, `F=10+2N`. Classification finds one unique low/high Y or Z entry and a contiguous coaxial shoulder chain with strictly decreasing positive radii and strictly increasing finite cumulative depths. Bounded largest-to-smallest subtraction preserves the analytic band volume `πΣ rᵢ²(dᵢ-dᵢ₋₁)`, `N` annular planar levels, a final floor, and `2N` exact rational rims; output support counts are `6+N` planes and `4+N` rational cylinders. Multi-cavity sets delegate to Phases 32w/32x/32y. A ninth stage, eccentric or non-decreasing chain, consumed shoulder, breakthrough, rounded-corner or selected-axis end-cap contact, and oblique/malformed topology refuse without partial application.

Phase 32w supports exactly two separated side-entering two-stage chains. Source topology is genus-zero `V16/E24/C48/L18/F14`; rounded topology is `V24/E36/C72/L22/F18`, with ten planes, eight rational cylinders, four planar inner loops, and eight exact rational rims. Both cavities share Y or Z direction but may enter one common retained side or opposite parallel sides. Every pair among their four finite stage bands must retain positive combined axial/radial distance, permitting coaxial opposite-side counterbores across a ligament. Both outer disks independently pass rounded-corner-strip and selected-axis end-cap clearance. A third valid two-stage cavity delegates to Phase 32x and mixed stage counts delegate to Phase 32y; eccentric, undercut, mixed-axis, intersecting, through, and wall-contact combinations refuse before deterministic reconstruction.

Phase 32x scales the same two-stage construction to `3 <= N <= 8` cavities. Canonical source topology is `V=8+4N`, `E=12+6N`, `C=24+12N`, `L=6+6N`, `F=6+4N`; rounded topology is genus-zero `V=16+4N`, `E=24+6N`, `C=48+12N`, `L=10+6N`, `F=10+4N`. Output contains `6+2N` planes, `4+2N` rational cylinders, `2N` planar inner loops, and `4N` exact rational rims. Every pair of cavities passes all four finite stage-band clearance comparisons, and each outer disk independently clears selected-axis end caps and the retained rounded-corner strip. Mixed-stage members delegate to Phase 32y. The route accepts common/opposite entry sides in Y or Z direction and refuses nine cavities, intersections, mixed axes, eccentricity, breakthrough, and wall contact transactionally.

Phase 32y generalizes the bounded side-stepped construction to `2 <= N <= 8` cavities with independently mixed `2 <= Sᵢ <= 8` stage counts and total stage budget `M = ΣSᵢ <= 16`. Canonical source topology is `V=8+2M`, `E=12+3M`, `C=24+6M`, `L=6+3M`, `F=6+2M`; rounded topology is genus-zero `V=16+2M`, `E=24+3M`, `C=48+6M`, `L=10+3M`, `F=10+2M`. Output contains `6+M` planes, `4+M` rational cylinders, `M` planar inner loops, and `2M` exact rational circles. All members share Y or Z direction but may independently enter either parallel retained side. Classification partitions every contiguous concentric chain, requires strictly decreasing positive radii and strictly increasing finite depths, validates analytic band volume, and checks positive combined axial/radial clearance for every cross-cavity stage-band pair. Seventeen total stages, a ninth stage or cavity, one-stage members, intersecting bands, mixed axes, eccentric or non-decreasing stages, breakthrough, and retained-wall/end-cap contact refuse transactionally.

### Complete rounded-box network

Phase 32f recognizes the complete twelve-edge set of the same rectangular topology. For `2r < min(lengths)`, it directly sews six inset planes, twelve exact cylindrical strips, and eight rational spherical octants into `V24/E48/C96/L26/F26`. The result follows the closed-form Minkowski-sum volume. Any incomplete interacting network still refuses; this is not a general arbitrary subset or non-box blend-intersection solver.

### Re-entrant handle roots

The rendered cases are reproducible with `Scripts/Phase21_Blends.arc`, including the re-entrant **210° material-angle** handle root (`e1`) requested for the spanner. The normal-based dihedral is the smaller 150° void angle, so a reflex root must not use the convex Boolean cutter.

For a straight, end-capped prism, the solver traces the complete cap perimeter (including the pieces created by `PushFace`), identifies the reflex turn from the loop orientation, and rebuilds that perimeter through the selected edge. A chamfer replaces the root vertex with one line segment and correctly **adds** its triangular void wedge. A fillet uses an exact rational circular arc with tangent distance `R / tan(void-angle / 2)`, extrudes the arc as its own cylindrical face, and caps the two circular end arcs. Other geometry remains on the existing blend path.

`BlendVerification` checks more than solidity here: the R4 chamfer adds exactly the analytic 210° wedge and has the expected 11-face / 27-edge topology; the R4 fillet adds the analytic circular wedge and has the same clean topology plus exactly two radius-4 cap arcs. `Root_Concave_{Chamfer,Fillet}.png` show the full parts, while their `_Detail` counterparts show the single bevel face and smooth roll close-up. A full-face direct push changes the transient edge-table numbering: the geometric arm-tip edge previously reached as `e26` is now `e19`; it is the same left-hand vertical edge of the moved cap, with no hidden micro-rim in front of it.
