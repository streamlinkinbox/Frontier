# SolidArc · Phase 4 · selection through the pick plane, select modes, hide / isolate, undo / redo, duplicate / mirror
sphere (0,0,1) 1 ; cylinder (3,0,0) 0.8 2 ; line (-3,-2) (-3,2) ; torus (0,4,0.5) 1.2 0.4
matcap Sphere chrome ; matcap Cylinder copper ; matcap Torus plastic-blue
view top ; view fit ; gizmo off

echo -- hover and click through the pick plane, Shift toggles, box selects, Ctrl+I inverts
pointer 640 400 ; click
click 890 400 --shift
select box 560 250 950 550
key ctrl+i
render Proof_04a_BoxSelection

echo -- hide, isolate, unhide  H / Shift+H / Alt+H
select Torus ; key h ; list
select Sphere ; isolate ; list
isolate off ; unhide all

echo -- control-point mode: pick poles, gizmo moves only those poles
clear
cpcurve (0,0) (1,2) (3,-1) (5,1) ; view top ; view fit
key 1
select ControlCurve
select poles ControlCurve 1
gizmo on ; gizmo size 160 ; gizmo grips
render Proof_04b_ControlMode

echo -- undo / redo record: every mutating command is one entry, a gizmo drag is one entry
timeline
undo ; describe ControlCurve
redo ; describe ControlCurve
key 4

echo -- duplicate and mirror
clear
cylinder (2,0,0) 0.5 1 ; matcap Cylinder gold ; select Cylinder
duplicate (0,3,0)
select Cylinder ; mirror selected --across=yz --copy
key shift+d
view iso ; view fit
render Proof_04d_DuplicateMirror
timeline
