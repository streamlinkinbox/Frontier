# SolidArc · Phase 12 · contact sheet (2x2 tile of four features added in phase 12)
#   Tile 1: bridge — two open curves in different planes connected by a ruled surface
#   Tile 2: linear array — five boxes spaced along X with one matcap per copy
#   Tile 3: radial array — five spokes around a vertical axis (top view)
#   Tile 4: named construction plane — two plane surfaces recalled by name (iso view)

echo Phase 12 — contact sheet (4 tiles, 1 PNG)
gizmo off
reset

# ─── Tile 1: iso — bridge ───────────────────────────────────────────────────────────────
echo -- tile 1: iso, bridge
view iso
rect (0,0) (4,2) --name=Bot
workplane xz
rect (0,0) (4,3) --name=Side
workplane xy
bridge Bot Side --name=Shell
matcap Shell steel
view fit
view dolly 0.85
render sheet 0
select none

# ─── Tile 2: iso — linear array ─────────────────────────────────────────────────────────
echo -- tile 2: iso, linear array
reset
view iso
box (-0.5,0,0) (0.5,1,1) --name=Seed
array Seed --count=5 --step=(1.6,0,0) --name=Linear
matcap Seed steel
matcap Linear.1 pearl
matcap Linear.2 gold
matcap Linear.3 copper
matcap Linear.4 plastic-blue
view fit
view dolly 0.7
render sheet 1
select none

# ─── Tile 3: top — radial array ─────────────────────────────────────────────────────────
echo -- tile 3: top, radial array
reset
view top
box (1.2,0,0) (1.7,0.3,0.3) --name=Spoke
array Spoke --count=6 --axis=(0,0,0),(0,0,1) --angle=360 --name=Radial
matcap Spoke steel
matcap Radial.1 pearl
matcap Radial.2 gold
matcap Radial.3 copper
matcap Radial.4 plastic-blue
matcap Radial.5 carbon
view fit
view dolly 0.8
render sheet 2
select none

# ─── Tile 4: iso — named construction plane ────────────────────────────────────────────
echo -- tile 4: iso, named construction plane
reset
view iso
plane (0,0,0) 4 2 --name=DeckSurface
plane --name=Deck
workplane yz
plane (0,0,0) 4 2 --name=SideSurface
workplane Deck
plane (0,0,0) 4 2 --name=DeckFrame
matcap SideSurface copper
matcap DeckFrame gold
view fit
view dolly 0.85
render sheet 3

# ─── Compose ────────────────────────────────────────────────────────────────────────────
echo -- compositing
render sheet finalize Phase12_ContactSheet
select none
echo -- Phase12_ContactSheet.png written
