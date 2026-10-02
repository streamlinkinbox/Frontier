# SolidArc native document v1
# SourceCAD 3D practice 4 — modelled Z-up (right-handed: X width 5, Y depth 6, Z height 4).
# Front view (X,Z) = 5 x 4 with the corner chamfer line (0,2)-(2.5,4); top view (X,Y) = 5 x 6 with R2 on the back-right vertical edge.

reset
box (0,0,0) 5 6 4 --name=Blank
fillet Blank 2 --edges=10 --name=Part
# The corner cut is ONE plane through (2.5,0,4) (0,0,2) (0,3,4). A triangle lying in that plane, enlarged x3 about its
# centroid, extruded towards the removed corner (-normal) gives an exact planar tool — no tapered loft.
polyline (5.8333,-2,5.3333) (-1.6667,-2,-0.6667) (-1.6667,7,5.3333) --closed --name=CutProfile
extrude CutProfile 4 --direction=(-6,-5,7.5) --name=CutTool
delete CutProfile
boolean subtract Part -- CutTool --name=Part
view fit
