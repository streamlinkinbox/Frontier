# SolidArc · Phase 23 · adversarial 2D profile proof (2x2 contact sheet)
# The companion ProfileAdversarialVerification executable checks the numerical contracts.  This image is the visual
# counterpart: shared/coincident boundaries, zero-area tangencies, invalid self-crossing source curves, and results from
# safe free-form offsets / Booleans are all present in one reproducible proof.

echo Phase 23 — adversarial planar NURBS contacts, self-intersections, free-form offsets and Booleans
gizmo off
reset

# ─── Tile 1: a shared boundary and an external tangent ───────────────────────────────────────────────────────
echo -- tile 1: shared boundary union + tangent circles
view top
rect (-6,-2) (-3,2) --name=SharedLeft
rect (-3,-2) (0,2) --name=SharedRight
boolean union SharedLeft SharedRight --name=SharedBoundaryUnion
circle (2,0) 1 --name=TangentLeft
circle (4,0) 1 --name=TangentRight
intersections TangentLeft TangentRight
select none
view fit
view dolly 0.82
render sheet 0

# ─── Tile 2: two visibly self-crossing cubic source curves ───────────────────────────────────────────────────
echo -- tile 2: closed interpolated bow-tie + one-span Bézier loop (both deliberately rejected by offset)
reset
view top
spline (1,-2) (5,2) (1,2) (5,-2) --closed --name=ClosedBowTie
cpcurve (-7,-2) (-1.930615,6.004659) (-8.019467,1.008169) (-4,-2) --degree=3 --name=OneSpanLoop
intersections ClosedBowTie ClosedBowTie
intersections OneSpanLoop OneSpanLoop
select none
view fit
view dolly 0.78
render sheet 1

# ─── Tile 3: safe, distance-controlled free-form and ellipse offsets ─────────────────────────────────────────
echo -- tile 3: cubic spline and ellipse offsets (safe side / distance)
reset
view top
spline (-6,0) (-4,2) (-2,-1) (0,2) (2,0) --degree=3 --name=GentleSpline
offset GentleSpline 0.15 --copy
ellipse (6,0) 2.5 1.6 --name=EllipseSource
offset EllipseSource 0.2 --copy
select none
view fit
view dolly 0.82
render sheet 2

# ─── Tile 4: overlapping closed cubic splines, union and common ──────────────────────────────────────────────
echo -- tile 4: free-form cubic Boolean union and common
reset
view top
spline (-5,0) (-3,2) (-1,0) (-3,-2) --closed --name=UnionA
spline (-3.5,0) (-1.5,2) (0.5,0) (-1.5,-2) --closed --name=UnionB
boolean union UnionA UnionB --name=FreeformUnion
spline (2,0) (4,2) (6,0) (4,-2) --closed --name=CommonA
spline (3.5,0) (5.5,2) (7.5,0) (5.5,-2) --closed --name=CommonB
boolean intersect CommonA CommonB --name=FreeformCommon
select none
view fit
view dolly 0.82
render sheet 3

# ─── Compose ─────────────────────────────────────────────────────────────────────────────────────────────────
echo -- compositing Phase23_AdversarialProfiles.png
render sheet finalize Phase23_AdversarialProfiles
select none
