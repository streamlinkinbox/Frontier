# SolidArc · Phase 3b · GizmoPRO grips driven by the synthetic pointer, per-whole matcaps
echo -- matcap contact scene: one studio per whole
sphere (0,0,1.5) 1.5 ; torus (4.5,0,0.6) 1.6 0.6 ; cylinder (-4,1,0) 1 2.5
sphere (-4,-3,1) 1 ; sphere (0,-4,1) 1 ; sphere (4,-4,1) 1
matcap list
matcap Sphere chrome ; matcap Torus gold ; matcap Cylinder plastic-blue
matcap Sphere.2 copper ; matcap Sphere.3 plastic-red ; matcap Sphere.4 pearl
view iso ; view fit
render Proof_02j_MatcapScene
show shading plastic ; render Proof_02k_PlasticScene
show shading matcap

echo -- GizmoPRO: pivot at the selection centre, grip anchors reported in pixels
clear
cylinder (0,0,0) 1 1.5 ; sphere (4,0,1) 1 ; matcap Cylinder gold ; matcap Sphere chrome
select Cylinder ; view iso ; view fit ; gizmo size 160
gizmo grips
render Proof_05a_GizmoPRO

echo -- drag the red cone with Ctrl held: X move snaps to 0.25 m
pointer 603 441 ; click ; pointer 700 490 --ctrl ; release
echo -- drag the red puck: X scale snaps to 0.1x about the pivot
pointer 623 429 ; click ; pointer 660 450 --ctrl ; release
echo -- drag the yellow sector: Z rotate snaps to 5 degrees
pointer 553 419 ; click ; pointer 600 395 --ctrl
render Proof_05b_GizmoDrag
release
describe Cylinder
gizmo translate ; render Proof_05c_GizmoTranslateOnly
gizmo rotate ; render Proof_05d_GizmoRotateOnly
gizmo combined ; gizmo status
