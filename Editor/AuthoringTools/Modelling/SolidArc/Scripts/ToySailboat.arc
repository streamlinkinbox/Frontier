echo ════ SolidArc · oak-and-white sailboat · loft, boolean deck cut, tapered sweep, spline sails, Coons and bridge NURBS sheets (1 unit = 1 cm)
gizmo off
show iso off
show shading plastic

echo ── 1. hull sections: seven ellipses on YZ workplanes, rotated so the loft seam runs along the keel, not the deck
workplane yz --origin=(-9,0,0)
ellipse (0,0) 0.16 0.22 --rotation=90 --name=S1
workplane yz --origin=(-7,0,0)
ellipse (0,0) 0.75 1.05 --rotation=90 --name=S2
workplane yz --origin=(-4,0,0)
ellipse (0,0) 1.3 1.8 --rotation=90 --name=S3
workplane yz --origin=(0,0,0)
ellipse (0,0) 1.55 2.2 --rotation=90 --name=S4
workplane yz --origin=(4,0,0)
ellipse (0,0) 1.35 1.9 --rotation=90 --name=S5
workplane yz --origin=(7,0,0)
ellipse (0,0) 0.8 1.15 --rotation=90 --name=S6
workplane yz --origin=(9,0,0)
ellipse (0,0) 0.2 0.28 --rotation=90 --name=S7
view iso
view orbit -20 -10
view fit
render Boat_1_Sections
render sheet 0

echo ── 2. loft the sections at degree 2 (fair, 4 % overshoot instead of the cubic's 27 %)
loft S1 S2 S3 S4 S5 S6 S7 --degree=2 --name=Spindle
topology Spindle
hide S1 S2 S3 S4 S5 S6 S7
tint Spindle 0.95 0.95 0.93
view fit
render Boat_2_Loft
render sheet 1

echo ── 3. boolean intersect with a half-space box: a flat deck 0.4 below the widest section
box (-11,-4,-3.5) 22 8 3.1 --name=Keep
boolean intersect Spindle -- Keep --name=Hull
topology Hull
tint Hull 0.95 0.95 0.93
view iso
view orbit -20 -18
view fit
render Boat_3_Hull
render sheet 2

echo ── 4. mast: a circle swept up a line with --scale=0.5 (tapering), unioned into the hull
workplane xy --origin=(0,0,-1.2)
circle (0.4,0) 0.16 --name=MastFoot
line (0.4,0,-1.2) (0.4,0,10.8) --name=MastLine
sweep MastFoot MastLine --scale=0.5 --name=Mast
hide MastFoot MastLine
boolean union Hull -- Mast --name=Boat
topology Boat
tint Boat 0.95 0.95 0.93
view iso
view orbit -20 -8
view fit
render Boat_4_MastSweep
render sheet 3
render sheet finalize Boat_A_Construction

echo ── 5. sail sketches on XZ: luff, headboard, interpolating-spline leech, foot — joined into one closed outline each
workplane xz --origin=(0,0.075,0)
line (0.15,-0.2) (0.15,10.4) --name=MainLuff
line (0.15,10.4) (-0.3,10.4) --name=MainHead
spline (-0.3,10.4) (-2.9,6.9) (-5.6,3.5) (-7.6,-0.2) --name=MainLeech
line (-7.6,-0.2) (0.15,-0.2) --name=MainFoot
join MainLuff MainHead MainLeech MainFoot --name=MainOutline
line (0.65,-0.2) (0.65,9.6) --name=JibLuff
line (0.65,9.6) (1.05,9.6) --name=JibHead
spline (1.05,9.6) (3.4,6.5) (5.7,3.3) (7.7,-0.2) --name=JibLeech
line (7.7,-0.2) (0.65,-0.2) --name=JibFoot
join JibLuff JibHead JibLeech JibFoot --name=JibOutline
describe MainOutline
view front
view fit
render Boat_5_SailSketch
render sheet 0

echo ── 6. veneer sails: the closed outlines extruded 1.5 mm — the leech faces are NURBS extrusions of the splines
extrude MainOutline 0.15 --name=MainSail
extrude JibOutline 0.15 --name=JibSail
topology MainSail
hide MainOutline JibOutline
tint MainSail 0.86 0.73 0.53
tint JibSail 0.86 0.73 0.53
view iso
view orbit -20 -8
view fit
view dolly 1
render Boat_6_Sails
render sheet 1

echo ── 7. NURBS sheet sails: a Coons patch (fillpatch) and a two-curve bridge with a bellied leech, shown as surfaces
line (0.15,0.15,-0.2) (0.15,0.15,10.4) --name=BLuff
line (0.15,0.15,10.4) (-0.3,0.15,10.4) --name=BHead
spline (-0.3,0.15,10.4) (-2.9,0.75,6.9) (-5.6,0.6,3.5) (-7.6,0.15,-0.2) --name=BLeech
line (-7.6,0.15,-0.2) (0.15,0.15,-0.2) --name=BFoot
fillpatch BLuff BHead BLeech BFoot --name=MainSheet
describe MainSheet
line (0.65,0.15,-0.2) (0.65,0.15,9.6) --name=JLuff
spline (1.05,0.15,9.6) (3.4,-0.5,6.5) (5.7,-0.4,3.3) (7.7,0.15,-0.2) --name=JLeech
bridge JLuff JLeech --name=JibSheet
describe JibSheet
hide BLuff BHead BLeech BFoot JLuff JLeech MainSail JibSail
tint MainSheet 0.86 0.73 0.53
tint JibSheet 0.86 0.73 0.53
view iso
view orbit -20 -8
view fit
view dolly 1
render Boat_7_NurbsSheets
render sheet 2
hide MainSheet JibSheet
unhide MainSail JibSail

echo ── 8. assembly
list
view iso
view orbit -20 -22
view fit
view dolly 1.2
render Boat_8_Final
render sheet 3
render sheet finalize Boat_B_SailsAndAssembly

view iso
view orbit -20 -22
view fit
view dolly 1.2
render sheet 0
view iso
view orbit 160 -20
view fit
view dolly 1.2
render sheet 1
view front
view fit
view dolly 1
render sheet 2
view top
view fit
view dolly 1
render sheet 3
render sheet finalize Boat_C_FinalViews
view iso
view orbit -25 -24
view fit
view dolly 1.6
render Boat_Hero --size=1920x1200
