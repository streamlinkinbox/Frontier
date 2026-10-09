# Stamp workspace and surface decals

Stamp is the fifth workspace in the existing Alloy React application. Material, Pattern, Texture, Stamp and Baking share the theme, header and left-library / right-inspector layout. The material/pattern Apply and export workflows, source-stroke painting, mesh-map baking and teapot deliverables are unchanged.

## Make and reuse a stamp

1. Open **Stamp** (`/?studio=stamp`). Its simpler composition canvas combines **Text**, **SVG** and **Image** components on a transparent 512 × 512 square.
2. Text is a real editable rich-text field. Select a character range and change its font, size, semibold weight or colour. Different runs share a baseline, including mixed fonts/sizes and line breaks. Without a range, formatting applies to the whole text. DM Sans and Space Grotesk, at weights 400/600, are bundled locally. Size changes update the artwork geometry, not just the editor appearance. Unsupported glyphs are reported.
3. Paste **SVG markup** into its source editor and apply it, or import an SVG file. SVG is validated, scoped and rendered as static artwork, **never executable JavaScript**. Scripts, events, external resources, foreign content, invalid references and excessive expansion are rejected. PNG/JPEG/WebP imports become bounded embedded PNGs.
4. Move/resize components on the canvas, or use the shared inspector controls. Shift snaps drag gestures. Visibility, opacity and locks are independent. Undo/redo, a locally recovered draft and project/SVG/PNG export are available.
5. Choose channel targets and retained values. Normal requires Height. Base colour comes from the artwork; height and normals derive from its luminance. Select alpha or luminance × alpha interpretation for stamp masks.
6. **Save preset** updates the current preset. **Save as new preset** creates an independent entry. **New stamp project** begins another composition. Presets can be opened or removed from the left library. **Use in Texture** saves the preset and opens the Texture stamp brush. Presets also appear in the shared overlay Content Browser.

Text exports are actual font outlines, not platform-dependent `<text>` placeholders. SVG/image/text composition exports a self-contained SVG, a transparent PNG, or editable `alloy.stamp.v1` JSON. The embedded surface PNG is at most 256 × 256; SVG export retains vector paths.

## Both decal hierarchies

- A regular non-folder layer can contain many **decal components**, alongside its existing channel sources and mask.
- Add **Decal layer** to create a dedicated container for many components.
- The left tree shows each component's actual stamp artwork and mask/decal role. The right inspector edits its name, transform, opacity, visibility, lock, mask role and channel targets. Duplicate and remove are real document operations.
- A stamp snapshot is embedded once and referenced by component IDs. Re-saving a library preset does **not** silently rewrite existing placements. **Replace artwork & targets with selected preset** explicitly replaces one component's content/targets while retaining its position, orientation, dimensions and mask role; this replacement is undoable.
- Parent visibility, opacity and locks are inherited. A component lock protects its properties and deletion; its eye and unlock remain available. Moving/deleting/duplicating a subtree containing locked content is blocked.

## Both placement modes

**Stamp brush:** select an unlocked layer, select a preset, choose Stamp, aim at the teapot and click. The translucent aim preview follows a genuine mesh ray hit; each click commits one placed component. Brush size, rotation, opacity and mask role are configurable. Zero brush opacity is a no-op. Dragging does not spray a continuous stroke or orbit the camera. Choose **Navigate** or Escape to leave placement mode.

**Transform placement:** choose Transform, click the surface to create the initial anchor, then use the real 3D **Move / Rotate / Scale** handles. Existing components can be selected directly. **Re-anchor to surface** snaps an existing component to a new ray hit without creating another component. Inspector numeric controls adjust position, width, height, depth and roll.

These use Three's clipped `DecalGeometry` on the actual specimen, filtered against opposite-facing triangles and stored in specimen-local coordinates. No unique mesh UV assumption is made. Off-surface or too-shallow projectors can produce no triangles; re-anchor or adjust depth to recover them. Coordinates are saved relative to the mesh bounding sphere (`object-sphere`).

Transform gestures use a draft and **one history commit on release**, not one per pointer move. Escape, pointer cancellation, lost capture, selection/lock/document changes and unmount cancel stale drafts and restore orbit interaction. The same safeguards apply to composition drags and asynchronous imports/preset rendering.

## Channels and mask scope

Real projected physical-material overlays support all 14 existing targets: Base color, Metalness, Roughness, Height, Normal, Opacity, Emission, Ambient occlusion, Anisotropy, Anisotropy angle, Clearcoat, Refraction index, Sheen and Subsurface. Disabled values are retained. RGB-disabled components retain the underlying selected base-colour preview rather than painting a white quad. With no targets enabled, a non-mask component is muted.

- Height is luminance-derived bump shading; Normal is a derived tangent-space normal map. This does not displace mesh vertices.
- Ambient occlusion modulates indirect illumination. Refraction index changes physical dielectric reflectance; enabling it does not make an opaque surface transmissive.
- Subsurface is a bounded wrap-light colour approximation, **not volumetric scattering**.

**Use as layer mask** converts a component into projected coverage instead of an extra visible decal mesh. Alpha/luminance interpretation, inversion and exact zero strength work on the surface. Multiple stamp masks on one owner combine as a union; ancestor coverage multiplies. Existing Fill/Gradient masks also clip physical decal overlays. Stamp coverage is evaluated through a bounded 1024² image atlas and a 7 × 64 float transform table, not teapot UVs.

Mask coverage affects the **selected fill/gradient preview and the layer's projected decal components**. Source strokes remain bounded 2D source images. This is **not a whole-stack UV painter/compositor or a baked decal texture atlas**; layer blend modes outside the live normal-overlay preview are still document settings. Material map exports and geometric baking are not newly relabelled as decal-composited maps.

## Layer drag/drop and the asset browser

The dedicated grip reorders a layer or its complete subtree. Leaf top/bottom halves place before/after; folder top/bottom quarters reorder around the folder and the middle half nests inside it. Valid zones are highlighted; inside drops expand the folder. A root drop target ungroups a moved subtree. Cycles, inherited/component locks and excessive nesting are rejected. Each completed move has one undo entry; identical moves are no-ops. Alt-Up/Down and the Folder inspector remain keyboard alternatives. Search/filtered trees disable reorder grips to avoid ambiguous order.

The Content Browser remains an **overlay**, never a scene-resizing panel. Its **list rows span the available centre-pane width**. The fixed 172px grid cards / 124px media and scrolling grid are preserved. Stamp assets can be inspected, assigned to an unlocked layer, or dragged onto a target; dropping a preset arms surface placement rather than pretending a default front-face position is a real hit. A folder asset drop creates a Decal child layer.

## Formats, storage and bounds

- Stamp project: `alloy.stamp.v1`; local draft `alloy-stamp-project-v1`; library `alloy-stamp-presets-v1`.
- 24 artwork components, 128 style runs, 1,024 text characters, sizes 8–160, 32,000 outline commands; 600KB project limit.
- 20 local presets / 4MB library. Exceeding capacity or browser storage quota is reported; saving does not silently evict another preset. Export is the portable backup.
- Texture retains `alloy.texture.v1`, its 64 layers / eight folder levels / 1MB project bound and 80-step history. Legacy projects without `stamps`/`decals` load without deleting their sources.
- 64 decal components / 16 distinct embedded stamp snapshots per Texture project. Shared snapshots are reused across repeated clicks; unreferenced snapshots are pruned after removal/replacement. Duplicating a layer creates independent component identities.
- Surface textures, aim geometry, transform helpers and mask tables are bounded and disposed on replacement/unmount, with material disposal deferred when a renderer compilation is in flight.

## Checks

```sh
npm run test:stamps
npm run test:painting
npm run test:runtime
npm run build
npm run build:githack
npm run test:standalone
```

The model tests cover rich-run edits/validation, channel dependencies, snapshot deduplication, transform projection, GPU table packing, locks/caps, tree drop positions and history. Browser checks exercise actual rich editing, SVG/raster import/export, aimed surface clicks, 3D gizmos, mask pixel changes, channel compilation, full-width list rows, native layer drags, cache reloads and phone layout. Use a supported Playwright Chromium installation (or `PLAYWRIGHT_EXECUTABLE_PATH`) with WebGL enabled.

### Verification checkpoint — 9 October 2026

- **32 Stamp/Decal checks passed** in the final uninterrupted run (21 model + 11 browser cases). This includes real gizmo drag/cancellation, rendered mask changes, explicit artwork replacement, rich-run editing/rejection, static SVG/raster import/export, both decal hierarchies, native drag/drop and phone actions.
- **66 existing painting/Texture cases passed across the regression run and focused rechecks.** The initial run exposed centre-drop compatibility and a resource-contention timeout in material-browser testing; the corrected cases passed together in an uninterrupted focused rerun. The old paint, masks, generators, gradient handles and history checks remained intact.
- **10 workspace-runtime/dock cases passed across the suite and focused cache-replay check.** The replay harness now intercepts hook module URLs with Vite HMR timestamps as well as bare paths, so the deliberate stale Texture React generation is actually exercised. Recovery retains the existing saved project.
- **12 offline standalone cases passed** against the rebuilt self-contained HTML, including rich Stamp text/surface decals, Pattern Apply/export, inline mesh baking and the real volume-SDF worker; no external runtime asset requests were accepted.
- Production and standalone builds passed; only the existing large-bundle warning remains. `git diff --check` is clean. Audited teapot deliverables are unchanged.
