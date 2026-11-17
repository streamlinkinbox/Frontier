# Frontier Import Inspector

A separate, self-contained, dark-theme import-dialogue reference. The name of the imported file appears in the inspector; the window is titled **Import inspector**. Frontier World Mirror's existing demo is unchanged.

## Open

- Open `index.html` directly for the self-contained preview, or serve this directory for reliable browser storage.
- From the repository root: `python3 -m http.server 8104 --bind 0.0.0.0 --directory Experimental/ImportDialogue`.
- Open the server's root URL. It accepts the Arena preview host without an origin allowlist.
- All dependencies, DM Sans fonts, native icons and the built-in specimens are embedded in the generated HTML. No CDN is required.

## Interaction

| Page | Functional controls |
| --- | --- |
| Source | Filename-derived import name, destination, file selection/drop, source record, type-adaptive options, built-in geometry/image/audio/text examples. |
| Geometry | XYZ translation/rotation/scale, linked scale, source units/up axis, centre/bottom/source/custom pivot, source/smooth/flat normals, winding, GPU bounds/normal/pivot inspection, orbit, zoom, framing, turntable. |
| UVs | Actual UV0 layout, checker, zoom, UV scale/offset/vertical flip, checker on geometry, UV triangle/area diagnostics. |
| Detail levels | Local vertex-cluster reduction, Morton-ordered triangle groups, configurable levels/ratio/group capacity, cancellation, per-level preview, measured vertex displacement, baked-geometry JSON export. |
| Materials | Decoded material slots with per-triangle assignments, selection/isolation, live base colour/metallic/roughness/opacity/emission/double-sided edits, base/normal/roughness/metallic textures, source reset. |
| SDF bake | Actual closest-triangle CPU distance field, unsigned or signed ray parity, resolution/padding, progress/cancel, XY/XZ/YZ voxel slices, float32 sample export. |
| Metadata | Author, license, tags, notes, custom properties, shipped icon selection, image upload, current-view thumbnail capture. |

The seven inspector pages slide horizontally, with connected V2-style trapezoidal tabs, previous/next controls and arrow-key navigation on the tabs. The inspector remains scrollable; the import confirmation is fixed in the footer. Mobile layout stacks the preview over the inspector.

**Import to library** saves original files, the import recipe, thumbnail, original decoded geometry and optional baked levels plus distance volume into this browser's IndexedDB. Library entries reopen with their settings. **Export settings** downloads a portable JSON recipe, not a native Frontier asset. No source is uploaded. This demo does not write files into an engine project.

## Formats and limits

- Browser geometry decoders: OBJ, STL, PLY, GLB/glTF, FBX. glTF companion resources must be selected together; remote dependencies are blocked. External compression decoders (Draco, Meshopt), rigs, animation, point-cloud conversion and native materials are not processed here. Geometry previews use decoded PBR surface properties under studio lighting. Native shader graphs, OBJ MTL libraries, vertex colours, extra UV sets, texture animation, and full material extensions are not interpreted. Unsupported materials retain their original files; property edits are a browser import recipe, not compiled native shaders. If decoding fails, **Keep source only** imports the original without geometry processing. Original files are retained unchanged.
- Images, audio, video and fonts use browser codecs. Unsupported codecs/formats remain preservable source files; preview failure is disclosed.
- Text-like files are inert UTF-8 previews, limited to 200,000 displayed characters. Arbitrary other types show a byte preview and retain their source.
- One primary source plus companions per import, at most 128 MB total. Geometry preview is capped at 250,000 triangles. Custom icons accept PNG/JPEG/WebP/SVG up to 2 MB. Material texture replacements accept the same image formats up to 8 MB per input.
- Imported decoded texture previews are embedded at up to 1024 px. Base colour is sRGB; normal/roughness/metallic inputs are linear. Roughness reads G and metallic reads B (glTF convention). Source texture orientation, wrapping and UV transforms are retained where available, using UV0 only. Texture replacements use UV0 and repeat wrapping; full source texture data remains in the original files.
- SDF controls offer 16³–48³ dense, voxel-centred float32 samples in transformed world metres, stored X-fastest, then Y, then Z. The bake uses full-resolution transformed geometry, independent of the selected detail level. Bounds are cubic and padded. The CPU bake yields between Z slices; large triangle sets/high resolutions can be slow, and tree construction is synchronous. Cancellation stops subsequent slices rather than interrupting the current slice.
- The built-in mechanical specimen has three assigned PBR materials, generated packed UV charts and real triangle geometry. It is not an external OBJ file. Saving it preserves its geometry directly.

## Deliberate analytical limits

The detail-level bake is **Nanite-inspired inspection, not Nanite**: spatial vertex grouping and Morton-ordered triangle clusters, independently reduced per level. It is not a certified QEM simplifier, a crack-free parent hierarchy/DAG, virtualized streaming, surface-error metric, or runtime LOD selector. The target ratio is a goal, not a guaranteed triangle count. Vertex displacement is measured, not a certified surface-error bound. UV-aware grouping is a heuristic, not seam locking. Topological/appearance fidelity can degrade at coarse levels.

**SDF sign is not a topology validator.** Unsigned distance is the default. Signed mode uses deduplicated +X ray parity and is suitable only for closed, non-self-intersecting geometry. Open/non-manifold surfaces and overlapping solids can have ambiguous inside regions; overlaps follow parity, not boolean union. The supplied housing is a multi-part specimen, so use unsigned distance unless you intend parity semantics. This is a local CPU reference bake, not native GPU baking, sparse storage, collision cooking, engine SDF shadows or GI. Stale volumes are excluded from library saves/settings exports until rebuilt. Material changes do not invalidate geometric distance fields.

UV area is a triangle sum, not measured occupancy; overlap detection and native unwrapping/baking are not connected. Image colour-space selection records import intent rather than transcoding pixels. Audio waveform readouts report the browser-decoded PCM rate, which may differ from the original file rate.

## Rebuild and verify

```sh
npm ci --prefix Experimental/ImportDialogue
node Experimental/ImportDialogue/Build.mjs
npm test --prefix Experimental/ImportDialogue
# Requires Chromium CDP plus the HTTP server:
node Experimental/ImportDialogue/Verify.mjs 9264 http://127.0.0.1:8104
```

The build reuses the shipped icon/font distribution in `FrontierWorldMirror/index.html` plus the shared `ActionIcon.jsx` source. Dependencies are pinned in the local package lock. `LICENSES.txt` and the standalone HTML contain the distribution notices.

All 18 model tests cover geometry/UV validity, bounds, partition integrity, reduction, material assignment preservation, progress/cancellation, actual OBJ reading, inert arbitrary-file handling, closest-triangle edge/face/degenerate cases, BVH versus brute force, closed-box distance signs and voxel sample coordinates. The CDP suite checks all seven pages, transforms, UV updates, actual cluster baking/staleness, material selection/PBR edits/texture replacement/clear, SDF bake/cancel/slice/plane/staleness/sample export, metadata/icon capture, IndexedDB save/reopen (including edited materials and float32 volumes), adaptive image/audio previews, source errors, inert text and 390px layout with active-tab visibility.
