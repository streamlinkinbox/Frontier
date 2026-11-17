# Import Inspector delivery

## Scope

Separate self-contained HTML reference at `Experimental/ImportDialogue/index.html`. No native engine integration or changes to the World Mirror demo are included in this delivery.

| Requirement | Implementation | Verification |
| --- | --- | --- |
| File-named import review | Source, geometry, UV, detail, materials, SDF and metadata pages; type-adaptive nongeometry imports | Seven-page desktop checks plus source/media/error handling |
| Material inspection | Real decoded slots and per-triangle IDs, isolated selection, PBR property editing, texture replacement/clear, source reset | Material edits and texture upload/clear in Chromium; mapping/reduction model test |
| Distance-field baking | Closest-triangle BVH, dense voxel-centred distance samples, unsigned/default or ray-parity sign, resolution/padding, progress/cancel | Analytic box signs/distances, brute-force comparison, degenerate triangles, coordinates, progress/cancel |
| Field inspection | XY/XZ/YZ plane selection, section slider, signed-zero contour, distance colour map, bounds/spacing/range | Slice and plane redraws; JSON export sample count/layout |
| V2 tab shape | Continuous dark category rail with raised trapezoidal active tab, native icons and mobile horizontal scrolling | Desktop screenshots; 390 px active-tab visibility |
| Save/reopen/export | IndexedDB originals, recipe, materials, optional LODs and fresh float32 field; JSON settings/geometry/distance exports | Edited material values and 24³ float32 volume survive library reopen |

## Delivery sequence

1. Preserve the existing five-page standalone workflow.
2. Extend decoded geometry with material slots and retain them through transforms/reduction.
3. Add editable material surfaces and closest-triangle distance baking.
4. Add the connected category rail without removing existing pages or controls.
5. Rebuild the offline HTML; run model/browser suites and inspect screenshots.
6. Commit only this demo, its Experimental index row and this report to the current Arena branch; push that branch to GitHub.

## Results

- Standalone build: passed, approximately 1.24 MiB with dependencies/icons/fonts embedded.
- Model suite: **18 passed, 0 failed**.
- Chromium CDP suite: **passed**, no runtime/console errors. Desktop 1440 × 1000; mobile 390 × 844.
- Every inspector section remains borderless; no page overflow or NaN readouts found.
- Existing geometry transforms, UV display, real LOD/cluster baking, metadata, custom icons, media, malformed-source fallback and source-only handling remain functional.
- Browser screenshots were inspected for materials, distance volume and mobile layout. Temporary captures are not included in Git.

## Explicit limits

- Browser-local reference, not native project import or engine material compilation.
- PBR preview supports a subset of decoded properties, four texture inputs and UV0. OBJ MTL libraries, additional UV sets and arbitrary shader graphs are not processed.
- Distance volume uses a CPU spatial tree and dense float32 samples. Work yields between slices; large inputs can be slow. It is not a native GPU bake or sparse runtime structure.
- Ray-parity signs require suitable closed geometry; open/non-manifold/self-intersecting or overlapping solids may be ambiguous. Unsigned is the default and the multi-part sample is not claimed to be a watertight boolean union.
- Spatial LOD reduction and Morton groups are not Nanite, a certified surface-error metric, crack-free hierarchy or streaming runtime.
- Unrelated environment/editor/other-demo changes in the working tree are deliberately excluded from this commit.

## Reproduce

```sh
npm ci --prefix Experimental/ImportDialogue
npm run build --prefix Experimental/ImportDialogue
npm test --prefix Experimental/ImportDialogue
python3 -m http.server 8104 --bind 0.0.0.0 --directory Experimental/ImportDialogue
# With Chromium exposing CDP on 9264:
node Experimental/ImportDialogue/Verify.mjs 9264 http://127.0.0.1:8104
```
