# SolidArc · Phase 10 · suite render: every phase contributed to a single document, one iso render
# Builds one scene that exercises: sketch, primitives, profile algebra, areas, loft, sweep, pipe,
# booleans (genus 1 lens, tee, drilled block, scooped corner), and a FairPatch window. The result is
# one image — if anything in the chain is broken, the iso view shows it.
#
# Layout on the XY ground:
#   -7..-3 row   : Phase 2 primitives + sketch
#    -3.. 1 row  : Phase 6/7 (capsule from revolve, plate with hole from profile boolean, areas + fill)
#     1.. 5 row  : Phase 8 loft + sweep + pipe
#     5.. 9 row  : Phase 9 booleans (drilled block, tee, lens)
#     9..12 row  : Phase 9b FairPatch window in a drum
#
# Y up in world is 0; objects are on a 14 m × 14 m floor with a 4 m height budget.
# Final view: iso, fit-all, dolly in 1.2× for a tight contact sheet tile.

echo Phase 10 — suite render (every phase in one document)
gizmo off ; select none

# ─── Phase 2 ────────────────────────────────────────────────────────────────────────────────────────────────
echo -- Phase 2: primitives + sketch
circle (-5.5,0) 0.6 --name=Sketch
rect (-4.4,-0.4) (-3.6,0.4) --radius=0.1 --name=Plate2d
sphere (-5.5,1.2,0.7) 0.6 --name=Phase2_Sphere
matcap Phase2_Sphere chrome
torus (-4.0,1.2,0.6) 0.5 0.18 --name=Phase2_Torus
matcap Phase2_Torus gold
cylinder (-2.5,1.2,0) 0.4 1.2 --name=Phase2_Cylinder
matcap Phase2_Cylinder copper

# ─── Phase 6/7 — capsule, plate with hole, area with fill ─────────────────────────────────────────────────
echo -- Phase 6/7: revolve, profile boolean, area fill
spline (-2.0,0) (-2.3,0.4) (-2.0,0.8) (-1.6,1.0) (-1.6,0) --name=VaseProfile
workplane xy
revolve VaseProfile 360 --origin=(-1.8,0,0) --axis=(0,1,0) --name=Vase
matcap Vase pearl

circle (-0.2,0) 0.8 --name=DiscOuter
circle (-0.2,0) 0.4 --name=DiscHole
boolean subtract DiscOuter DiscHole --name=Ring
extrude Ring 0.4 --name=Plate
matcap Plate plastic-white
fill on Ring
# the area's fill is the default; just confirm we have a hole

rect (1.0,-0.7) (2.2,0.7) --name=SketchBox
circle (1.6,0) 0.3 --name=SketchDot
workplane xy
boolean union SketchBox SketchDot --name=Fused
extrude Fused 0.4 --name=FusedBlock
matcap FusedBlock plastic-blue

# ─── Phase 8 — loft, sweep, pipe ──────────────────────────────────────────────────────────────────────────
echo -- Phase 8: loft, sweep, pipe
# Six sections in a line; the first three are rectangles (boxes), the last three are circles → box→circle loft
rect (3.0,-0.5) (3.4,0.5) --name=Sq1
rect (3.6,-0.4) (4.0,0.4) --name=Sq2
rect (4.2,-0.3) (4.6,0.3) --name=Sq3
circle (5.0,0) 0.4 --name=Cr1
circle (5.4,0) 0.4 --name=Cr2
circle (5.8,0) 0.4 --name=Cr3
loft Sq1 Sq2 Sq3 Cr1 Cr2 Cr3 --name=BoxToCircle
matcap BoxToCircle steel

# A small square swept along a closed-ish spline (Dolphin path)
spline (7.2,-0.4) (7.6,0) (7.2,0.4) (6.8,0) --name=SweepPath
circle (7.2,0) 0.15 --name=SweepCircle
sweep SweepCircle SweepPath --name=SweptCircle
matcap SweptCircle gold

# A pipe along a quadratic curve
spline (8.5,0) (8.5,1.5) (9.0,2.0) (9.5,2.0) --name=PipePath
pipe PipePath 0.08 --name=PipePath_Body
matcap PipePath_Body copper

# ─── Phase 9 — booleans ──────────────────────────────────────────────────────────────────────────────────
echo -- Phase 9: booleans (drilled block, tee, lens, scooped corner)
box (3.0,2.0,0) (4.6,3.6,1.6) --name=DrillBlock
cylinder (3.8,2.8,-0.2) 0.35 2.0 --name=DrillBit
boolean subtract DrillBlock DrillBit --name=Drilled
matcap Drilled steel

cylinder (5.6,2.8,0) 0.5 1.6 --name=PipeA
cylinder (4.4,3.4,0.5) 0.4 1.6 --axis=(1,0,0) --name=PipeB
boolean union PipeA PipeB --name=Tee
matcap Tee plastic-red

sphere (7.0,3.0,0.8) 0.6 --name=Ba
sphere (7.4,3.3,0.8) 0.6 --name=Bb
boolean intersect Ba Bb --name=Lens
matcap Lens gold

box (8.6,2.0,0) (9.4,2.8,1.0) --name=ScoopCube
sphere (9.2,2.6,0.9) 0.5 --name=ScoopBit
boolean subtract ScoopCube ScoopBit --name=Scooped
matcap Scooped pearl

# ─── Phase 9b — FairPatch window in a drum ───────────────────────────────────────────────────────────────
echo -- Phase 9b: FairPatch drum window
# Drum is centred at the origin of a sub-tile, 0.9 radius, 1.8 tall; a through-hole box cuts completely through the
# cylinder side (from outside to outside), leaving a planar ring of 4 wall edges.
cylinder (11.0,2.0,0) 0.9 1.8 --name=Drum
box (10.05,1.2,0.45) (11.95,2.8,1.35) --name=Cutter
boolean subtract Drum Cutter --name=Windowed
matcap Windowed clay
# The cylinder side is one face with a hole loop; the four edges of the hole are e10..e13, in order.
fairpatch Windowed:e10 Windowed:e11 Windowed:e12 Windowed:e13 --g2 --name=WindowG2
matcap WindowG2 gold

# ─── Camera ───────────────────────────────────────────────────────────────────────────────────────────────
select none
view iso
view fit
view dolly 1.05
render Phase10_Suite
echo -- Phase10_Suite.png written
