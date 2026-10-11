echo ════ SolidArc · wooden toy biplane · loft, exact cap fillet, radial array, linear array, mirror, pipe, spline, booleans (1 unit = 1 cm)
gizmo off
show iso off
show shading plastic

echo ── 1. fuselage: four rounded-rectangle sections on YZ workplanes, lofted at degree 1 (a tapering wooden bar)
workplane yz --origin=(0,0,0)
rect (-1.8,0.2) (1.8,3.8) --radius=0.35 --name=F1
workplane yz --origin=(9,0,0)
rect (-1.8,0.2) (1.8,3.8) --radius=0.35 --name=F2
workplane yz --origin=(17,0,0)
rect (-1.3,0.6) (1.3,3.2) --radius=0.3 --name=F3
workplane yz --origin=(23,0,0)
rect (-0.7,1.0) (0.7,2.6) --radius=0.25 --name=F4
view iso
view orbit -25 -8
view fit
render Biplane_1_Sections
render sheet 0
loft F1 F2 F3 F4 --degree=1 --name=Fuselage
topology Fuselage
hide F1 F2 F3 F4
tint Fuselage 0.92 0.80 0.62
view fit
render Biplane_2_FuselageLoft
render sheet 1

echo ── 2. tail: fin outline = open interpolating spline joined to a straight base, extruded and unioned with the stabilizer slab
workplane xz --origin=(0,0.2,0)
spline (18.6,2.05) (19.2,4.6) (21.0,5.8) (22.9,5.4) (23.5,3.9) (23.3,2.05) --name=FinTop
line (23.3,2.05) (18.6,2.05) --name=FinBase
join FinTop FinBase --name=FinOutline
extrude FinOutline 0.4 --name=Fin
workplane xy --origin=(0,0,1.9)
rect (18.2,-5.0) (23.8,5.0) --radius=1.2 --name=StabOutline
extrude StabOutline 0.4 --name=Stabilizer
boolean union Stabilizer -- Fin --name=Tail
topology Tail
hide FinOutline StabOutline
tint Tail 0.98 0.80 0.12

echo ── 3. cowl: native cylinder with an exact rolling-ball cap fillet (Phase 26 route), six dowel plugs by radial array, ball nose
cylinder (0,0,2) 2.3 1.3 --axis=(-1,0,0) --name=CowlBlank
fillet CowlBlank 0.45 --edges=2 --name=CowlDisc
cylinder (-1.1,0,3.45) 0.22 0.5 --axis=(-1,0,0) --name=Plug
radial Plug --count=6 --axis=(0,0,2),(1,0,0)
boolean union CowlDisc -- Plug --name=Cowl1
boolean union Cowl1 -- Radial.Plug.1 --name=Cowl2
boolean union Cowl2 -- Radial.Plug.2 --name=Cowl3
boolean union Cowl3 -- Radial.Plug.3 --name=Cowl4
boolean union Cowl4 -- Radial.Plug.4 --name=Cowl5
boolean union Cowl5 -- Radial.Plug.5 --name=Cowl
topology Cowl
tint Cowl 0.15 0.62 0.85
sphere (-1.9,0,2) 0.85 --name=Nose
tint Nose 0.93 0.82 0.64
view iso
view orbit -25 -8
view fit
render Biplane_3_TailAndCowl
render sheet 2

echo ── 4. propeller: one slat with exact tip fillets on both ends, unioned through the nose ball
box (-2.35,-5.6,1.55) 0.32 11.2 0.9 --name=SlatBlank
fillet SlatBlank 0.42 --edges=0,2,4,6 --name=Slat
boolean union Nose -- Slat --name=Propeller
topology Propeller
tint Propeller 0.98 0.80 0.12
view fit
render Biplane_4_Propeller
render sheet 3
render sheet finalize Biplane_A_Construction

echo ── 5. wings: rounded-rectangle slabs — the upper wing's trailing-edge notch is a 2D boolean before the extrude
workplane xy --origin=(0,0,-0.3)
rect (3.2,-13) (8.8,13) --radius=1.3 --name=LowerOutline
extrude LowerOutline 0.5 --name=LowerWing
workplane xy --origin=(0,0,6.2)
rect (3.0,-13) (8.6,13) --radius=1.3 --name=UpperBlank
circle (8.6,0) 1.6 --name=Notch
boolean subtract UpperBlank -- Notch --name=UpperOutline
extrude UpperOutline 0.5 --name=UpperWing
topology UpperWing
hide LowerOutline UpperOutline
tint LowerWing 0.96 0.42 0.08
tint UpperWing 0.96 0.42 0.08
view iso
view orbit -25 -8
view fit
render Biplane_5_Wings
render sheet 0

echo ── 6. struts: one dowel as a pipe, a linear array along the chord, mirrored across the fuselage plane
line (4.6,9.5,0.2) (4.6,9.5,6.2) --name=StrutLine
pipe StrutLine 0.22 --name=Strut
hide StrutLine
array Strut --count=2 --step=(2.6,0,0) --name=Strut
mirror Strut Strut.1 --across=xz --name=StrutL
line (4.6,1.4,3.8) (4.6,1.4,6.2) --name=CabaneLine
pipe CabaneLine 0.22 --name=Cabane
hide CabaneLine
array Cabane --count=2 --step=(2.6,0,0) --name=Cabane
mirror Cabane Cabane.1 --across=xz --name=CabaneL
tint Strut Strut.1 Cabane Cabane.1 0.90 0.78 0.58
view fit
render Biplane_6_Struts
render sheet 1

echo ── 7. landing gear: red wheels, axle pipe, V-legs mirrored
cylinder (3.6,2.1,-1.4) 1.15 0.9 --axis=(0,1,0) --name=WheelR
cylinder (3.6,-3.0,-1.4) 1.15 0.9 --axis=(0,1,0) --name=WheelL
tint WheelR 0.86 0.14 0.14
tint WheelL 0.86 0.14 0.14
line (3.6,-3.2,-1.4) (3.6,3.2,-1.4) --name=AxleLine
pipe AxleLine 0.18 --name=Axle
hide AxleLine
line (2.6,1.2,0.3) (3.6,2.4,-1.4) --name=LegLineA
line (5.0,1.2,0.3) (3.6,2.4,-1.4) --name=LegLineB
pipe LegLineA 0.2 --name=LegA
pipe LegLineB 0.2 --name=LegB
hide LegLineA LegLineB
mirror LegA LegB --across=xz --name=LegL
tint Axle LegA LegB 0.90 0.78 0.58
view iso
view orbit -25 -8
view fit
render Biplane_7_Gear
render sheet 2

echo ── 8. assembly
list
view iso
view orbit -25 -14
view fit
view dolly 1
render Biplane_8_Final
render sheet 3
render sheet finalize Biplane_B_Assembly

view iso
view orbit -25 -14
view fit
view dolly 1
render sheet 0
view iso
view orbit 155 -14
view fit
view dolly 1
render sheet 1
view left
view fit
view dolly 1
render sheet 2
view top
view fit
view dolly 1
render sheet 3
render sheet finalize Biplane_C_FinalViews
view iso
view orbit -30 -16
view fit
view dolly 1
render Biplane_Hero --size=1920x1200
