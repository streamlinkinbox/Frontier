echo Phase 6 — boundary representation: solids from primitives, extrude, revolve and sewing
box (0,0,0) 2 1.5 1
cylinder (4,0,0) 0.8 2
cone (7,0,0) 1 0.4 2
sphere (10,0,0) 1
torus (0,4,0) 1.2 0.4
matcap Box gold ; matcap Cylinder steel ; matcap Cone copper ; matcap Sphere clay ; matcap Torus steel
list
topology Cylinder
view iso ; view fit
render Proof_06a_Primitives
polygon (4,4) 1 6
extrude Polygon 1.5
polyline (7,3.5,0) (8.5,3.5,0) (8.5,3.5,1) (7,3.5,1) --closed
revolve Polyline 270 --origin=(7.5,4.5,0) --axis=(0,0,1)
topology Extrusion
topology Revolution
view fit
render Proof_06b_ExtrudeRevolve
echo A sheet cylinder has two open rims — sew caps them with trimmed planes and orients the result
cylinder (10,4,0) 0.8 1.5 --sheet
describe Cylinder.2
sew Cylinder.2
describe Sewn
echo Sewing arbitrary sheets — five planes of an open box, the missing planar side is capped automatically
plane (12,3,0) 1 1
plane (12,3,0) 1 1 --v=(0,0,1)
plane (12,4,0) 1 1 --v=(0,0,1)
plane (12,3,0) 1 1 --u=(0,1,0) --v=(0,0,1)
plane (13,3,0) 1 1 --u=(0,1,0) --v=(0,0,1)
sew Plane Plane.2 Plane.3 Plane.4 Plane.5
describe Sewn.2
key 3
select faces Box 1
select faces Cylinder 0 --add
key 2
select edges Box 4 5 6 7 --add
render Proof_06c_FaceEdgeSelection
key 4
select Torus ; move Torus (0,0,1) ; undo
timeline
