# SolidArc curve rebuilding

## Open a project without a BAT file

Use the **integrated Frontier project browser**. Browse to your project directory, select the `.solidarc` file, inspect its CAD preview, then choose **Open in SolidArc**. Frontier launches its CAD companion with that exact document. Keep the companion executable and packaged fonts/icons beside the Frontier installation.

This requires the Frontier build containing the SolidArc integration. An older standalone SolidArc executable does not contain the Frontier browser. The source handoff in `Integration/Frontier-e72d319/` is not an installed Windows update or a completed merge into Frontier.

## Split a curve at the mouse

1. Press **Split** in the compact viewport toolbar (formerly **Cut**).
2. Hover the curve. The curve highlights and a ring marks the nearest point on the actual projected curve.
3. Click once. Both pieces remain selected. The operation divides the existing NURBS definition by knot insertion; it does not fit a replacement curve or insert display samples as controls.
4. Press **Esc** or **Sel** to exit. **Ctrl+Z** undoes the entire split.

This works with authored **3D splines / surface traces** and the existing planar curve tools. A curve end is already a boundary, so splitting there refuses. Locked or hidden curves are not picked. Detach live targets and a guide's dependent features before splitting them; a refusal must not silently change downstream geometry. Rational-weight 3D curves refuse until their editable split format supports those weights. Ctrl crossing cuts remain the existing **planar sketch** operation, not a promise to cut all 3D curves at a projected crossing.

## Live equal distance along a surface

| Step | Action |
|---|---|
| 1 | Lock the reference body in the Outliner. Use the sheet actually containing the guide. |
| 2 | Select the guide first, then **Shift-select** the target 3D curve. The target's placement chooses the side and extent. |
| 3 | Open **Geometry → Live surface distance**, now at the top of Inspector. |
| 4 | Choose the reference explicitly when there is more than one locked body. Set the distance and tolerance in **metres**. For example, `0.025` m is 25 mm. |
| 5 | Press **Link / update distance**. Applied changes have green inline feedback; refusals explain why the previous geometry was kept. |
| 6 | Move/edit the guide. The linked target regenerates from it. |
| 7 | Select the linked target **alone** to change distance, tolerance or reference. With **Apply linked edits on release** enabled, Enter/leaving a numeric field applies the change. Otherwise use Link / update. |
| 8 | Use **Detach (keep curve)** before editing or splitting the target's generated controls independently. |

**Surface/geodesic distance is not an XYZ offset or a straight chord.** The intrinsic solver refines to its stated tolerance; this is not an analytical exactness claim. Changing a field does not create the initial association: the first link still needs the button. Edits to an existing link can apply on field release.

### If it appears to do nothing

- **Read the inline result**, not just the transient status bar. The card distinguishes an active link from unapplied settings and a refused update.
- Choose the correct locked reference. A multi-sheet vehicle must not silently use whichever body happened to be first in history.
- Keep the guide on that reference within **four times the selected tolerance**. Clicking only a few surface-attached controls does not necessarily keep the spline between those controls on a curved sheet.
- A gap, unavailable guide/reference, insufficient surface extent or refinement/resource limit may refuse. The tool keeps the previous geometry rather than accepting a wrong distance.
- Look at **Active link** for the currently applied distance. A refused typed value is not the active value.
- Selecting just an unlinked curve is not enough to identify its guide. Select two curves in order first.

## Dock the three primary panels

Drag the **Outliner**, **Viewport** or **Inspector** tab. Drop on a docking indicator to split the area, or onto another tab strip to group panels. Drag free to float a panel inside the application. Resize the separators normally. **Reset panels** in the status bar restores the default three columns.

The four Inspector sections remain **Geometry / Transform / Topology / History**. They are not three additional dock panels. Separate operating-system windows and persistence across application restarts are not claimed by this change.

Docking uses Frontier's existing `PanelDockspace`; tab chrome and palette use Texture Workspace's `PanelChrome`. Switches and image opacity/crop sliders delegate to its existing `VectorAuthoringControlSurface` rather than a second control implementation.

## In-application guide

Press **Curve guide** in the status bar for Split, Distance, Workspace and Open projects instructions. The Split section can start the tool directly.

The supplied Liger cage is not modified by these operations until you explicitly edit it. No vehicle lofts, replacement panels or T-spline reconstruction are authored by this update.
