echo Phase 7b — sketch areas: planar arrangement, bucket fill, extrude with through-holes, orthographic gizmo
view top
rect (0,0) (4,4)
circle (2,2) 1
circle (2,2) 0.4
line (0,2) (4,2)
areas
fill off a2
fill at (2,1.5) off
areas
view fit
render Phase7b_Areas_Top
extrude a0 1 --name=Lower
extrude a1 1 --name=Upper
extrude a4 2 --name=PinLower
extrude a5 2 --name=PinUpper
delete Line
areas
extrude Circle 1.5 --name=Ring
topology Ring
list
select Ring
gizmo status
gizmo grips
view iso
gizmo status
view fit
render Phase7b_Areas_Iso
