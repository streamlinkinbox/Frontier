echo Phase 9 — true NURBS surface–surface-intersection booleans on solids
view iso
box (0,0,0) (2,2,2) --name=Brick
cylinder (1,1,-0.5) 0.5 3 --name=Bore
intersections Brick Bore
boolean subtract Brick Bore --name=Drilled
matcap Drilled steel
box (3,0,0) (5,2,2) --name=Slab
cylinder (2.5,1,1) 0.6 3 --axis=(1,0,0) --name=Rod
boolean union Slab Rod --name=Welded
matcap Welded copper
sphere (0,4,1) 1 --name=Ball
sphere (1.2,4.3,1.2) 1 --name=Ball.2
boolean intersect Ball Ball.2 --name=Lens
matcap Lens gold
box (3,3.2,0) (5,5.2,2) --name=Housing
torus (4,4.2,1) 1.2 0.3 --name=Ring
boolean subtract Housing Ring --name=Grooved
matcap Grooved pearl
box (-3,0,0) (-1,2,2) --name=Cube
sphere (-1.1,2.1,2.05) 0.8 --name=Corner
boolean subtract Cube Corner --name=Scooped
matcap Scooped plastic-blue
cylinder (-3,4,-1) 1 4 --name=Pipe
cylinder (-5,4.3,1) 0.5 4 --axis=(1,0,0) --name=Branch
boolean union Pipe Branch --name=Tee
matcap Tee plastic-red
list
topology Drilled
select none
view iso
view fit
render Phase9_Booleans_Iso
view top
view fit
render Phase9_Booleans_Top
