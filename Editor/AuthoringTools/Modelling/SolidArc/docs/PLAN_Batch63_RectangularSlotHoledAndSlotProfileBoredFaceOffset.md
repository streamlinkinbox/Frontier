# Batch 63 plan — exact rectangular slot-holed and slot-profile circular-bored prism face offsets

## Scope

Promote one consolidated batch covering the first **slot-support** domains, i.e. bodies whose
profile or through-hole is an exact analytic slot (a closed line/arc/line/arc chain built by
`NurbsCurve::Slot`, stored as a degree-two rational closed curve with thirteen poles):

1. A canonical axis-aligned rectangular prism with exactly one canonical **slot through-hole**
   (genus 1). This is the first holed route whose hole wall is a composite linear-plus-circular
   analytic chain rather than a single conic or a polygonal chain.
2. A canonical axis-aligned **slot-profile prism** with exactly one canonical concentric circular
   bore (genus 1). This is the first route whose outer wall is a slot chain.

Both support only a finite positive +Z extension of their upper planar cap.

## Canonical geometry

- Rectangular slot-holed route: rectangle `(-8,-5)..(8,5)`, height `6`, one slot hole with arc
  centres `(-3,0)` and `(3,0)` and radius `1.5` centred at the origin. Closed manifold topology
  `V10/E15/C30/L9/F7`, genus 1, two-loop caps, five extrusion walls, thirteen seam lines and two
  slot curves.
- Slot-profile bored route: slot profile with arc centres `(-3,0)` and `(3,0)` and radius `1.5`,
  height `6`, one concentric circular bore of radius `0.8` at the origin. Closed manifold topology
  `V4/E6/C12/L6/F4`, genus 1, two-loop caps, two extrusion walls, two seam lines, two slot curves
  and two circles.

Because the extrusion pipeline flattens a slot's classification, recognition is structural plus
sampled: closed, rational, degree two, thirteen poles, Z axis, `SpanY = 2·Radius`,
`SpanX = 2·(HalfSpan + Radius)`, planar, and 49 samples within `1e-6` of a freshly built
`NurbsCurve::Slot` with the derived parameters.

## Recognition contract

The strict readers require exact canonical bounds, vertex level counts, cap loop structure, the
canonical rectangle corner set or the canonical slot outer profile, exact slot half-span/radius/
centre, exact bore radius/centre/plane, bore containment inside the slot profile, full edge
censuses (13 lines + 2 slots; 2 lines + 2 circles + 2 slots) and face censuses, plus reciprocal
analytic edge incidence.

Non-canonical rectangle, slot or bore dimensions, bore/hole counts, mismatched hole supports
(circular-holed rectangle, circular-bored ellipse, elliptical bore), twin slots or twin bores,
hole-free slot sources, translated profiles, malformed incidence, lower/side selections, and
invalid distances refuse transactionally without healing and without mutating the source. The
pre-existing circular-holed rectangular and circular-bored elliptical routes remain separate and
must still dispatch exactly.

## Verification and proof

Use one verifier,
`RectangularSlotHoledAndSlotProfileBoredFaceOffsetVerification.cpp`, with distances `0.5`, `1.5`,
and `3.0`; exact topology, cap, face-census, and support checks; exact volume identities
(`160 − (2·L·2·r + πr²)` and `(2·L·2·r + πr²) − πb²` with `L=3`, `r=1.5`, `b=0.8`, times the
extended height); source immutability; dispatcher coverage with strict-route agreement;
regression checks that the earlier circular-holed rectangle and bored-ellipse domains still route;
explicit refusal boundaries; and one durable four-body proof:

`Proofs/Batch63_RectangularSlotHoledAndSlotProfileBoredFaceOffset.png`

Register the verifier and proof in the standalone CMake target, dependency-free full gate,
capability roadmap, and proof audit.

## Explicit non-goals

No arbitrary rectangle, slot or bore dimensions or orientations, arbitrary hole/bore counts,
radii, placements or supports, Y-aligned or tilted slots, hole-free slot prisms, inward or
lower/side offsets, healing, or general multi-loop face editing is promoted by this batch.
