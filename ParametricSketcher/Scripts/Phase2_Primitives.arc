# SolidArc · Phase 2 · sketch curves + primitive surfaces + derived surfaces, rendered from several views
echo -- sketch on XY
circle (0,0) 3 --name=Ring
rect (-6,-2) (-2,2) --radius=0.5 --name=Plate
slot (2,-4) (6,-4) 0.8
polygon (5,3) 1.5 6 --name=Hex
spline (-6,4) (-4,6) (-2,4.5) (0,6.5) (2,5)
ellipse (0,-7) 2.5 1.2 --rotation=20
arc (7,0) 2 30 240
line (-8,-8) (8,-8) --construction

echo -- primitives
sphere (0,0,1.5) 1.5
torus (-6,6,0.6) 1.6 0.6
cylinder (7,-7,0) 1 2.5
cone (-7,-6,0) 1.2 0.4 2

echo -- derived
extrude Hex 2
workplane xz
cpcurve (0,0) (0.8,0.5) (0.6,1.5) (1.2,2.2) (0.4,3) --name=Profile
workplane xy
revolve Profile 360 --origin=(0,0,0) --axis=(0,0,1)
move Revolution (10,4,0)

list
view iso ; view frame ; render Proof_02e_Script_Iso
view top ; view frame ; render Proof_02f_Script_Top
view front ; view frame ; render Proof_02g_Script_Front
select Ring Hex Sphere
view iso ; view frame selected ; render Proof_02h_Script_Selected
pick 640 400
