echo Phase 8 — loft, sweep, pipe, patch: derived figures that follow their sketch curves
view iso
rect (-1,-1) (1,1)
circle (0,0,2.5) 1
circle (0,0,5) 0.4
loft Rectangle Circle Circle.2 --name=Vase
recipe Vase
move Circle (0.6,0,0)
recipe Vase
rect (4,-1) (6,1) --name=Base
circle (5,0) 0.4 --name=BaseHole
circle (5,0,3) 0.7 --name=Cap
circle (5,0,3) 0.25 --name=CapHole
loft a1 Cap+CapHole --name=Chimney
spline (-4,0,0) (-4,2,1) (-4,4,0) (-4,6,1) --name=Rail
pipe Rail 0.3 --name=Tube
line (-6,0,0) (-6,0,3) --name=Post
circle (-6,0,0) 0.5 --name=Disc
sweep Disc Post --twist=90 --scale=0.5 --name=Twist
line (2,4,0) (4,4,0) --name=P1
line (4,4,0) (4,6,0.8) --name=P2
line (4,6,0.8) (2,6,0) --name=P3
line (2,6,0) (2,4,0) --name=P4
fillpatch P1 P2 P3 P4 --name=Coons
polygon (7,5) 1.4 5 --name=Pent
explode Pent
fillpatch Pent Pent.2 Pent.3 Pent.4 Pent.5 --name=Star
recipe
dependents Circle
select Circle.2
key g
type 0,0,1.5
recipe Vase
list
select none
view iso
view fit
render Phase8_Skins_Iso
view top
view fit
render Phase8_Skins_Top
