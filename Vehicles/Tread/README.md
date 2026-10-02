# Tyre tread — CAD model in SolidArc + all-quad mesh

Generator: `Vehicles/tools/tread_quads.py` (one pattern definition → both outputs). No height-maps, no textures,
no displacement: every block is a traced outline.

**Pattern** (directional, modelled after the reference family at mavis.com/learning-center/tire-tread-types-explained):
one pitch of the +Y half is designed on a *warped grid* — rows across the tread, columns around it, each row placing
its nodes freely, so lugs are slanted, curved and widen toward the shoulder. Cells carry a height
(groove floor 0 / block 8.5 mm / sipe floor 4.5 mm). Contents: centre rib with notches, inner longitudinal groove,
curved diagonal lugs with a short sipe and a notch into the outer groove, outer longitudinal groove, shoulder blocks
split by a sipe with a shoulder notch. The other half is the **mirror across the mid-plane shifted by half a pitch**
(the flip you see on real V-patterns), then 68 pitches around the wheel (R 330 mm floor, 6 mm crown, 2×95 mm wide).

**CAD (`Tread.arc`, replay in SolidArc, 0 refusals, 694 figures)** — crown band and centre rib as 360° revolves;
each block outline (12–18 corners) drawn as 3D lines on the tangent plane 1 mm under the groove floor, `join`ed and
`extrude`d along the radial normal; the mirrored/staggered half emitted explicitly; `radial --count=68` around Y.
Renders by the kernel: `Proofs/Tread_05_TopClose.png`, `Tread_06_Quarter.png`, `Tread_01_Iso.png`, `Tread_03_Side.png`
(`render_tread.arc`).

**Quads (`Tread_quads.obj`)** — the same grid traced as quads: block tops, groove floors, sipe floors, vertical walls
on every height change, walls split at all height levels (no T-junctions), pitches welded around the wheel and the
two halves welded on the centreline. Check: 32 776 faces, **100 % quads, 0 non-manifold edges**, open edges only on
the two shoulder rims (2 448). Blender/Cycles renders: `Proofs/Quads_01_TopClose.png` (orange wire = quads),
`Quads_02_Quarter.png`, `Quads_03_Side.png`, `Quads_04_Face.png` (`tools/render_obj_bpy.py`).
