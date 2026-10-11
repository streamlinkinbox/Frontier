# Batch 64 plan — exact circular-bored rounded-rectangular and rounded-rectangular-holed prism face offsets

## Scope

Promote one consolidated batch covering the first **filleted-rectangle (rounded-rectangle)**
domains, i.e. bodies whose profile or through-hole is an exact analytic tangent line/arc composite
built by `NurbsCurve::Rectangle` with a corner radius (stored as a degree-two rational closed
curve with seventeen poles):

1. A canonical axis-aligned rounded-rectangular prism with exactly one canonical **eccentric
   circular bore** (genus 1).
2. A canonical axis-aligned rectangular prism with exactly one canonical **rounded-rectangular
   through-hole** (genus 1).

Both support only a finite positive +Z extension of their upper planar cap.

## Canonical geometry

- Circular-bored rounded-rectangular route: filleted rectangle with half-extents `6` (X) and `4`
  (Y), corner radius `1.5`, centred at the origin, height `6`, one circular bore of radius `1.2`
  centred at `(2, 0)` — deliberately eccentric so the route cannot be satisfied by a concentric
  annular reader. Closed manifold topology `V4/E6/C12/L6/F4`, genus 1, two-loop caps, two
  extrusion walls, two seam lines, two circles and two filleted-rectangle curves.
- Rounded-rectangular-holed route: rectangle `(-7,-5)..(7,5)`, height `6`, one filleted-rectangle
  hole with half-extents `4` (X) and `2.5` (Y) and corner radius `1.0` centred at the origin.
  Closed manifold topology `V10/E15/C30/L9/F7`, genus 1, two-loop caps, five extrusion walls,
  thirteen seam lines and two filleted-rectangle curves.

Because the extrusion pipeline flattens a filleted rectangle's classification, recognition is
structural plus sampled: closed, rational, degree two, seventeen poles, Z axis, planar,
`SpanX = 2·HalfX`, `SpanY = 2·HalfY`, `SpanX > SpanY`, corner radius below both half-extents, and
65 samples within `1e-6` of a freshly built `NurbsCurve::Rectangle` with the canonical corner
radius. The seventeen-pole signature separates the family from nine-pole conics and thirteen-pole
slots, and the sampled comparison separates differing corner radii that share identical spans.

## Recognition contract

The strict readers require exact canonical bounds, vertex level counts, cap loop structure, exact
filleted-rectangle outer profile or canonical rectangle corner set, exact bore radius/centre/plane
with containment inside the fillet-inscribed central rectangle, full edge censuses (2 seam lines +
2 circles + 2 filleted rectangles; 13 lines + 2 filleted rectangles) and face censuses, plus
reciprocal analytic edge incidence.

Non-canonical half-extents, corner radii, bore radii or centres, hole counts, mismatched hole
supports (circular-holed rectangle, slot-holed rectangle, bored ellipse, bored slot, elliptical
bore), twin bores or twin filleted holes, mixed circular+filleted holes, hole-free filleted
sources, translated profiles, malformed incidence, lower/side selections, and invalid distances
refuse transactionally without healing and without mutating the source. The pre-existing
circular-holed rectangular, slot-holed rectangular, circular-bored elliptical, and slot-profile
bored routes remain separate and must still dispatch exactly.

## Verification and proof

Use one verifier,
`CircularBoredRoundedRectangularAndRoundedRectangularHoledFaceOffsetVerification.cpp`, with
distances `0.5`, `1.5`, and `3.0`; exact topology, cap, face-census, and support checks; exact
volume identities (`4ab − (4−π)r² − πρ²` and `4AB − (4ab − (4−π)r²)` with `a=6, b=4, r=1.5, ρ=1.2`
and `A=7, B=5, a=4, b=2.5, r=1.0`, times the extended height); source immutability; dispatcher
coverage with strict-route agreement; regression checks that the earlier circular-holed,
slot-holed, bored-elliptical, and bored-slot domains still route; explicit refusal boundaries; and
one durable four-body proof:

`Proofs/Batch64_CircularBoredRoundedRectangularAndRoundedRectangularHoledFaceOffset.png`

Register the verifier and proof in the standalone CMake target, dependency-free full gate,
capability roadmap, and proof audit.

## Explicit non-goals

No arbitrary half-extents, corner radii, bore radii/centres or orientations, arbitrary hole/bore
counts or supports, square or Y-dominant filleted profiles, hole-free filleted prisms, inward or
lower/side offsets, healing, or general multi-loop face editing is promoted by this batch.
