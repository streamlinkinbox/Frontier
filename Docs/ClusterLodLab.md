# Cluster-local LOD lab and Frontier integration plan

## Run the HTML demo

```sh
npm --prefix Experimental/FrontierEditor ci
FRONTIER_LOD_DEMO=1 npm --prefix Experimental/FrontierEditor run dev -- --port 5174
```

Open `/` on that dedicated server, or `/cluster-lod.html` on the normal editor server. The page and its two modules are also copied into the Vite production build. No CDN assets are required.

Controls: orbit with drag, dolly with scroll or the distance slider, change screen-error tolerance, switch between adaptive/fixed meshes, inspect from a second observer viewpoint, enable normal-variation weighting, enable shading-only normal detail, visualize UV boundaries, and apply procedural height displacement. Glass-safe mode deliberately locks the complete shell at maximum detail; disable it to examine camera-only simplification.

### What is real in this demo

- 72 parametric sphere patches choose local subdivision rates 1/2/4/8.
- Rendered mesh topology really changes; selected and drawn triangle counts are computed from the generated triangles.
- Adjacent patches match edge samples and stitch mixed subdivision rates, without skirts.
- UV island boundaries stay fixed. A periodic displacement function keeps the longitude seam and poles closed.
- Height displacement changes vertex positions; shading-only normal detail does not change the triangle count.
- Opaque backfaces are culled for drawing only. They remain in the selected mesh. Glass draws both sides.
- An explicit full-shell glass policy prevents camera-facing detail selection from throwing away unseen exit surfaces.

### What this demo does NOT implement

This is an original Canvas educational renderer, not a browser port of Nanite or Vulcanite. There is no general QEM mesh simplifier, cluster-group DAG, streaming, occlusion hierarchy, BVH, certified error bound, temporal geomorphing or physical refraction. Selection uses a parametric curvature/displacement heuristic with optional normal-variation weighting. UV visualization/locked boundaries are not a general UV-aware simplification solver. Translucent triangles illustrate shell retention; they do not prove optical accuracy.

Displayed rebuild milliseconds are CPU JavaScript work, not GPU time or expected Frontier performance.

## How Nanite-style local LOD works

Traditional object LOD picks one mesh for the whole object. A virtual-geometry system builds small triangle clusters and a hierarchy of simplified alternatives. Runtime selection chooses an appropriate covering set of clusters using projected geometric error, bounds and visibility. Different parts of the same object can use different detail levels. Nanite's documented mesh pipeline supports UVs and vertex attributes; its ray-tracing path and displacement support have their own constraints. [1](https://dev.epicgames.com/documentation/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)

It is NOT generally “front-facing = high detail; back-facing = low detail.” A nearby or silhouette-sensitive region may need more detail. Opaque backfaces/occluded clusters can often be culled from the camera's raster pass entirely. They may still be needed by reflection, shadow and transmission rays. A sphere's front and back can also have different projected errors simply because their distances differ.

The useful error intuition is `screen error ≈ world geometric error × focal length in pixels / distance`. Production implementations account for bounds, transforms, monotonic parent/child error, coverage, and transitions. Naively selecting unrelated simplified patches creates cracks; hierarchy construction and boundary agreement are essential.

## Vulcanite reference supplied by the user

Repository inspected at `3a48aa479a288b656f04ac9fb1ea5a1272050bfa`:

- README: OpenMesh/METIS mesh building, cluster grouping, simplification/reclustering and runtime error selection.
- `mesh/Cluster.h`: parent/child cluster indices, LOD errors, parent error, and bounding spheres.
- `shaders/glsl/pbrtexture/bvhtraversal.comp`: bounding-sphere/error-based traversal decisions.
- Root `LICENSE.md`: MIT. Third-party dependencies have their own notices/licenses.

This is a useful Vulkan reference, not evidence that all of Frontier's rendering/material requirements are solved. No Vulcanite source, models, dependencies or screenshots were copied into the lab. [2](https://github.com/bdwhst/Vulcanite)

## What Frontier already has and what is missing

Existing code has spatial cluster records (`GeometryStructure.h`), GPU cluster culling/HiZ/indirect drawing (`VisibilityExchange.cpp`, `ClusterCull.slang`), and a software CWBVH/two-level traversal path for rays. These are useful foundations, but do not amount to a Nanite-style simplification hierarchy.

Suggested implementation stages:

1. Offline cluster-group build with parent alternatives, geometric error, normal/UV/material constraints and locked group boundaries. Validate coverage and cracks on meshes with seams and thin features.
2. Opaque primary-view raster LOD selection with screen-space error, hysteresis and frustum/HiZ culling. Keep stable surface/object identity and deliberately invalidate history where topology changes.
3. Measure raster cost, traversal-node/triangle tests, shading time and acceleration-structure update cost. Smaller visible meshes do not automatically imply a proportional frame-time saving.
4. Introduce a separate ray-tracing representation/selection policy. Never populate the ray scene using only camera-visible clusters. Initially retain full-resolution ray geometry for correctness.
5. Evaluate ray-footprint-aware or conservative ray-scene LOD only with reflection, shadow and transmission regression images and update-cost profiling.

Frontier rasterizes primary visibility and traces other lighting paths in compute. If its bottleneck is cloud integration, expensive materials or sampling, reducing triangle count may give little improvement. If traversal/geometry traffic dominates, cluster LOD may help considerably. No speedup is claimed without measurements.

## Glass: stricter than opaque geometry

For an initial safe engine implementation, keep the complete closed glass mesh in the ray scene at a stable high-detail level. Preserve both entry and exit surfaces, winding, material/medium boundaries and thickness. Do not apply primary-camera backface/occlusion culling to transmission geometry.

Later optical LOD needs bounds on surface displacement, normal deviation, thin-wall thickness and refracted/reflected image error. Do not independently switch entry and exit representations within a path without a consistency design. Keep compatible displacement and normal conventions in raster and ray paths. A tiny silhouette error alone does not bound refraction error.

Unreal documents a fallback-mesh ray-tracing path by default and experimental native Nanite ray tracing. Fallback error can affect ray-traced appearance; it is not proof that all glass failures have one cause or that they are unavoidable. [1](https://dev.epicgames.com/documentation/unreal-engine/nanite-virtualized-geometry-in-unreal-engine) [3](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-technical-details)

## Normals, UV seams and height maps

- **Normal maps:** change shading, not geometry or silhouette. They do not inherently add triangles.
- **Hard normals/material boundaries:** constrain simplification, since merging them can change appearance even with low positional error.
- **UV seams:** preserve separate attribute vertices while keeping matching geometric boundaries. UV error matters independently of position error.
- **Height/displacement maps:** actually move geometry; require sufficient tessellation, displacement-aware bounds and matching edge evaluation. Arbitrary displacement can create cracks at discontinuous UVs or normals.

Epic distinguishes static displacement building from dynamic Nanite tessellation. Its displacement documentation explicitly lists limitations, including cracks at discontinuous attributes; do not assume automatic seam-safe displacement in a new engine. [4](https://dev.epicgames.com/documentation/unreal-engine/working-with-naniteenabled-content)

## Validation

`node Experimental/FrontierEditor/scripts/test-cluster-lod.mjs`: **34 checks PASS**, including mixed-LOD watertightness, finite geometry, distance/error responses, glass full-shell preservation, and real DOM event handlers rendered through software Canvas. `npm run build` passes; dedicated preview root returns the lab HTML.

A Chromium download failed in this environment. The validation above is **not** a Chromium/WebGL/GPU test. Manual browser interaction and mobile layout review remain appropriate. Generated canvas proof is under ignored `build/cluster-lod`, not a substitute for engine output.
