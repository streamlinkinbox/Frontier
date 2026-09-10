# SolidArc · Phase 13 · contact sheet (2x2 tile of four features added in phase 13)
#   Tile 1: bbox dims on a box (auto-emit)
#   Tile 2: radius + circumference dims on a circle + ellipse (auto-emit)
#   Tile 3: explicit dim + free dim + dim edit
#   Tile 4: angle dim on a polyline

echo Phase 13 -- contact sheet (4 tiles, 1 PNG)
dim on
gizmo off
reset

# ─── Tile 1: iso — bbox dims on a box ──────────────────────────────────────────────────
echo -- tile 1: iso, box with auto bbox dims
view iso
box (0,0,0) (2,1.5,1) --name=Box
matcap Box pearl
view fit
view dolly 0.85
render sheet 0
select none

# ─── Tile 2: top — radius + circumference on circle and ellipse ───────────────────────
echo -- tile 2: top, circle + ellipse
reset
view top
circle (0,0,0) 1 --name=Circ
matcap Circ chrome
ellipse (2,0,0) 1.5 0.7 --name=Ov
matcap Ov copper
view fit
view dolly 0.85
render sheet 1
select none

# ─── Tile 3: iso — explicit dim + free dim + dim edit ─────────────────────────────────
echo -- tile 3: iso, dim edit
reset
view iso
box (0,0,0) (3,2,1) --name=Box2
matcap Box2 plastic-blue
dim Box2 --along=Y
dim Box2 (0,0,0) (0,0,4)
dim edit 12 9.999
view fit
view dolly 0.85
render sheet 2
select none

# ─── Tile 4: top — angle dim on a polyline ─────────────────────────────────────────────
echo -- tile 4: top, angle
reset
view top
polyline (0,0) (4,0) (4,3) --name=Tri
matcap Tri plastic-red
angle Tri --at=1
view fit
view dolly 0.85
render sheet 3

# ─── Compose ────────────────────────────────────────────────────────────────────────────
echo -- compositing
render sheet finalize Phase13_ContactSheet
select none
echo -- Phase13_ContactSheet.png written
