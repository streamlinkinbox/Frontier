echo Phase 9b — FairPatch: energy-fair fills whose rims follow the neighbouring faces (G0 / G1 / G2), guides, N-sided
view iso
echo -- 1. a window cut through a drum, filled three ways: G0 (crease), G1 (tangent), G2 (curvature) --
cylinder (0,0,0) 2 3 --name=Drum
box (1,-0.8,0.8) (3,0.8,2.2) --name=Cutter
boolean subtract Drum Cutter --name=Windowed
matcap Windowed steel
fairpatch Windowed:e2 Windowed:e3 Windowed:e4 Windowed:e7 Windowed:e8 Windowed:e9 --g0 --name=WindowG0
fairpatch Windowed:e2 Windowed:e3 Windowed:e4 Windowed:e7 Windowed:e8 Windowed:e9 --g1 --name=WindowG1
fairpatch Windowed:e2 Windowed:e3 Windowed:e4 Windowed:e7 Windowed:e8 Windowed:e9 --g2 --name=WindowG2
hide WindowG0 WindowG1
matcap WindowG2 gold
echo -- 2. a pillow over a box: the fill leaves every side wall flush (G1 to the walls), pulled up by a guide --
box (4,-1,0) (6,1,1) --name=Block
matcap Block plastic-blue
spline (4,0,1) (4.7,0,1.55) (5.3,0,1.55) (6,0,1) --name=Crest
fairpatch Block:e4@f2 Block:e5@f5 Block:e6@f3 Block:e7@f4 --g1 --guides=Crest --name=Pillow
matcap Pillow copper
echo -- 3. the same rims with the flat top as support give the flat top back (G1 to a plane is the plane) --
fairpatch Block:e4 Block:e5 Block:e6 Block:e7 --g1 --name=FlatAgain
hide FlatAgain
echo -- 4. five free-floating sketch curves: an N-sided fair fill with tangent-continuous seams --
spline (-2.6,0,0) (-3.2,0.8,0.3) (-3.57,1.33,0.5) --degree=2 --name=Side1
spline (-3.57,1.33,0.5) (-4.4,1.1,0.4) (-5.13,0.82,0.1) --degree=2 --name=Side2
line (-5.13,0.82,0.1) (-5.13,-0.82,-0.3) --name=Side3
spline (-5.13,-0.82,-0.3) (-4.4,-1.1,-0.2) (-3.57,-1.33,0.1) --degree=2 --name=Side4
spline (-3.57,-1.33,0.1) (-3.2,-0.8,-0.1) (-2.6,0,0) --degree=2 --name=Side5
fairpatch Side1 Side2 Side3 Side4 Side5 --name=Petal
matcap Petal pearl
echo -- 5. a sphere with its cap cut off, closed again G2 — the seam disappears --
sphere (0,4.5,1) 1.2 --name=Ball
box (-2,3,1.7) (2,6,3) --name=Lid
boolean subtract Ball Lid --name=Bowl
matcap Bowl plastic-red
fairpatch Bowl:e1@f0 --g2 --name=Dome
matcap Dome plastic-red
echo -- 6. the fills are recipes: move the crest and the pillow follows --
move Crest (0,0,0.4)
recipe Pillow
recipe
list
select none
view iso
view orbit 90 0
view fit
render Phase9b_FairPatch_Iso
select Windowed WindowG2
view fit selected
view dolly 2
select none
render Phase9b_FairPatch_Window
select Block Pillow Crest
view fit selected
view dolly 2
select none
render Phase9b_FairPatch_Pillow
select none
