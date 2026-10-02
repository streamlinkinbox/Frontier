# Tyre treads — five designs, CAD in SolidArc + all-quad meshes

Generator: `Vehicles/tools/tread_quads.py <outdir> [design…]` — one pattern definition per design drives both the
SolidArc journal and the quad mesh. No height-maps, textures or displacement anywhere: every block is a traced outline.

**Method.** One pitch of one half of the tread is designed on a *warped grid* (rows across the tread, columns around
it; each row places its nodes freely, so lug edges are slanted / curved / wavy / stepped). Cells carry a height
(groove floor, block top, sipe floor, stone-ejector…). The second half is produced by **mirror + half-pitch stagger**
(directional V), **plain mirror** (symmetric), or an **independent half design** (asymmetric). Then `radial` ×N.

| design | type | second half | pitches | what is in a pitch |
|---|---|---|---|---|
| `Vee` | directional | mirror + stagger | 68 | notched centre rib, curved diagonal lugs widening to the shoulder, sipe + notch, outer groove, shoulder blocks with sipe |
| `Touring` | symmetric | mirror | 72 | centre rib with two sipes, three zig-zag longitudinal grooves, blocks with three zig-zag sipes, shoulder rib with notches |
| `Sport` | asymmetric UHP | own outboard half | 66 | inboard: narrow curved lugs, fine sipes · outboard: wide rib, one wide groove, big stiff shoulder blocks |
| `AllTerrain` | off-road | mirror + stagger | 46 | 14 mm deep: staggered block rows, wide voids with stone-ejector ribs, stepped notches, shallow sipes |
| `Winter` | directional | mirror + stagger | 64 | wide centre groove, blocks with four zig-zag sipes each, lateral cut, serrated shoulders |

Each `<design>/` holds: `Tread.arc` (SolidArc journal: crown + rib revolves, each block outline drawn as 3D lines,
`join`ed, `extrude`d radially, `radial` array — all five replay with **0 refusals**), `Tread_quads.obj`
(welded all-quad mesh), `plan.png` (flat plan of three pitches with the quad grid), `render.arc`, and `Proofs/`:
`*_CAD_TopClose / _CAD_Quarter / _CAD_Iso.png` rendered by the SolidArc kernel and `*_Quads_TopClose / _Quads_Quarter.png`
rendered from the OBJ in Blender/Cycles (`tools/render_obj_bpy.py`). Overviews: `cad_top5.png`, `quads_top5.png`.

Quad audit (100 % quads, 0 non-manifold edges, open edges only on the two shoulder rims):

| design | vertices | quads | open (rim) edges |
|---|---|---|---|
| Vee | 34 000 | 32 776 | 2 448 |
| Touring | 57 600 | 56 592 | 2 016 |
| Sport | 25 872 | 24 882 | 1 980 |
| AllTerrain | 25 300 | 24 656 | 1 288 |
| Winter | 68 352 | 66 816 | 3 072 |

Walls are split at every global height level so sipe floors never create T-junctions; pitches are welded around
the wheel and the two halves on the centreline (stagger maps centreline nodes onto nodes because the column count is even).
Not included: sidewall/carcass (tread band only); CAD blocks are not booleaned into the crown band.
