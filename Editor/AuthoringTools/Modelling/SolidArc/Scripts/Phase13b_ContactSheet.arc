# SolidArc · Phase 13 redo · contact sheet (2x2 tile of four features added in phase 13 redo)
#   Tile 1: box with auto bbox dims (Plasticity-style: white, on-surface, world-space offset)
#   Tile 2: cone with auto height dim
#   Tile 3: chamfered plank with chamfer dim on the chamfered edge
#   Tile 4: sphere + cylinder + torus, each with live radius/height dim

echo Phase 13 redo -- contact sheet (4 tiles, 1 PNG)
dim on
gizmo off
reset

# ─── Tile 1: iso -- bbox dims on a box ──────────────────────────────────────────────────
echo -- tile 1: iso, box with auto bbox dims (live-edited from Y=3 to Y=7)
view iso
box (0,0,0) (2,3,4) --name=Box
matcap Box pearl
dim edit 2 7.0
view fit
view dolly 0.85
render sheet 0
select none

# ─── Tile 2: top -- cone with live height dim ──────────────────────────────────────────
echo -- tile 2: top, cone
reset
view top
cone (0,0,0) 2.0 0.5 4.0 --name=Cone
matcap Cone clay
dim edit 7 6.0
view fit
view dolly 0.85
render sheet 1
select none

# ─── Tile 3: iso -- chamfered plank with chamfer dim on the edge ──────────────────────
echo -- tile 3: iso, chamfered plank (live edit on setback 0.1 to 0.3)
reset
view iso
box (0,0,0) (3,1,1) --name=Plank
chamfer Plank 0.1 --edges=0 --name=PlankCham
matcap PlankCham gold
dim edit 12 0.3
view fit
view dolly 0.85
render sheet 2
select none

# ─── Tile 4: top -- sphere plus cylinder plus torus (live radius/height dims) ──────────
echo -- tile 4: top, sphere + cylinder + torus
reset
view top
sphere (0,0,0) 1.0 --name=Sph
cylinder (3,0,0) 0.6 2.0 --name=Cyl
torus (6,0,0) 0.7 0.2 --name=Trs
matcap Sph chrome
matcap Cyl copper
matcap Trs steel
view fit
view dolly 0.85
render sheet 3

# ─── Compose ────────────────────────────────────────────────────────────────────────────
echo -- compositing
render sheet finalize Phase13b_ContactSheet
select none
echo -- Phase13b_ContactSheet.png written
