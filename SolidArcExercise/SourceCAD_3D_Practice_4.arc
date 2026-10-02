# SolidArc native document v1
# Construction journal. Edit only with the documented .arc grammar; save keeps a .bak recovery copy.

reset
box (0,0,0) 5 4 6 --name=Blank
fillet Blank 2 --edges=5 --name=Part
polyline (-0.5,4.5,-0.5) (3.5417,4.5,-0.5) (-0.5,4.5,4.35) --closed --name=W0
polyline (-0.5,1.3,-0.5) (-0.4583,1.3,-0.5) (-0.5,1.3,-0.45) --closed --name=W1
loft W0 W1 --name=Wedge
delete W0 W1
boolean subtract Part -- Wedge --name=Part
view fit
view fit
