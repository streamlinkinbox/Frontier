# SolidArc · Phase 10 · contact sheet (2x2 tile of four views of four scenes)
# Each tile is rendered independently into the live raster, then the four buffers are composited
# into a single 2560x1600 PNG. The script is the entry point; SuiteVerification re-runs it and
# reads the result back as the regression net.

echo Phase 10 — contact sheet (4 tiles, 1 PNG)
gizmo off
reset

# ─── Tile 1: top — Phase 2 primitives + sketch ──────────────────────────────────────────────────────────
echo -- tile 1: top, primitives
view top
circle (-5.5,0) 0.6
rect (-4.4,-0.4) (-3.6,0.4) --radius=0.1
sphere (-5.5,1.2,0.7) 0.6
torus (-4.0,1.2,0.6) 0.5 0.18
cylinder (-2.5,1.2,0) 0.4 1.2
view fit
view dolly 0.85
render sheet 0
select none

# ─── Tile 2: front — Phase 3/3b sketch + gizmo + matcap ─────────────────────────────────────────────────
echo -- tile 2: front, sketch tools + matcap
reset
view front
sphere (0,0,1) 0.8
matcap Sphere chrome
torus (3,0,1) 0.8 0.25
matcap Torus gold
box (6,0,0) (7.5,1,1.5)
matcap Box copper
view fit
view dolly 0.7
render sheet 1
select none

# ─── Tile 3: right — Phase 6/7 solid primitives + profile algebra ──────────────────────────────────────
echo -- tile 3: right, solids + profile algebra
reset
view right
box (0,0,0) (1.2,1.2,1.2)
matcap Box plastic-white
sphere (3,0.6,0.6) 0.6
matcap Sphere pearl
torus (6,0.6,0.6) 0.7 0.22
matcap Torus plastic-red
cylinder (9,0.6,0) 0.45 1.2
matcap Cylinder plastic-blue
view fit
view dolly 0.7
render sheet 2
select none

# ─── Tile 4: iso — Phase 8/9/9b loft + booleans + fairpatch ────────────────────────────────────────────
echo -- tile 4: iso, loft + booleans + fairpatch
reset
view iso
rect (0,-0.5) (0.4,0.5) --name=SqA
rect (0.6,-0.4) (1.0,0.4) --name=SqB
circle (1.5,0) 0.4 --name=CirC
loft SqA SqB CirC --name=BoxToCircle
matcap BoxToCircle steel
box (4,-0.6,0) (5.6,0.6,1.2) --name=Block
cylinder (4.8,0,-0.2) 0.35 1.6 --name=Bit
boolean subtract Block Bit --name=Drilled
matcap Drilled pearl
sphere (8,0,0.6) 0.6 --name=Ball0
sphere (8.4,0.3,0.6) 0.6 --name=Ball1
boolean intersect Ball0 Ball1 --name=Lens
matcap Lens gold
view fit
view dolly 0.7
render sheet 3

# ─── Compose the four tiles into one PNG ───────────────────────────────────────────────────────────────
echo -- compositing
render sheet finalize Phase10_ContactSheet
select none
echo -- Phase10_ContactSheet.png written
