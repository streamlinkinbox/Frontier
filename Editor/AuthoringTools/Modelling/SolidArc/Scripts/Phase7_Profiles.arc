echo Phase 7 — planar profile algebra: exact crossings, winding, booleans, fillet, chamfer, offset, trim, join
view top
rect (0,0) (2,2)
rect (1,1) (3,3)
intersections Rectangle Rectangle.2
boolean union Rectangle Rectangle.2
circle (2,1) 0.75
boolean subtract Union -- Circle
rect (0.5,0.5) (0.9,0.9)
boolean subtract Difference -- Rectangle
profile Difference Difference.2
fillet Difference 0.15
tint Difference 0.95 0.55 0.2
echo Wheel — a rim minus four bores, then a keyway slot subtracted through the hub
circle (6,1) 2
circle (7.3,1) 0.4
circle (4.7,1) 0.4
circle (6,2.3) 0.4
circle (6,-0.3) 0.4
boolean subtract Circle -- Circle.2 Circle.3 Circle.4 Circle.5
slot (5,1) (7,1) 0.25
boolean subtract Difference.3 Difference.4 Difference.5 Difference.6 Difference.7 -- Slot
profile Difference.3 Difference.4 Difference.5 Difference.6
echo Offset and chamfer — hexagon rings and a bevelled square
polygon (11,1) 1.2 6
offset Polygon 0.3 --copy
offset Polygon -0.3 --copy
rect (13,0) (15,2)
chamfer Rectangle 0.4
echo Trim — a line cut by a circle, keeping the outer pieces, then joined back with an arc
line (-3,4,0) (3,4,0)
circle (0,4) 1
trim Line (0,4,0)
arc (0,4) 1 180 -180
join Line Arc Line.2
list
view fit
render Proof_07a_Profiles
select Difference.3 Difference.4 Difference.5 Difference.6
timeline
