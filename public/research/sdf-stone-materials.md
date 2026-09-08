# SDF stone material editor · Frontier 0.12

## Open the separate experiment

**`material-editor.html`** is a second, independently bootable page, not a replacement for the terrain studio. It is linked from the main Materials inspector. For a local preview that opens directly into it, run `npm run dev:material`. The normal `npm run dev` still opens the terrain editor.

The experiment renders one small organic stone with **geometric signed-field detail**. Its only sampled material texture is a **256 × 1 SatMap color lookup**. There are no image-based rock height maps, normal maps, displacement maps, photo-detail tiles, scanned meshes, or hidden texture-based relief.

SDFs are not the only way to make detailed rock surfaces; they are the deliberate choice for this prototype. The aim is to inspect actual surface changes rather than mistake albedo variation or a bump-only normal for geometry.

## What changes the surface

The base is a bounded ellipsoidal distance estimator intersected with softened facet planes and perturbed by low-frequency 3D noise. It has a flat support cut to sit on the presentation ground.

The detail stack modifies the field's zero surface:

1. **Chipped relief:** signed, multi-scale 3D displacement gives broken faces and an irregular contour.
2. **Bedding / slate:** ordered, jittered layer boundaries give unequal sheet thickness. Each sheet has its own displacement and spalling pattern, with narrow, variable-width linear transitions and partly missing edges. This replaces the old sinusoidal ribs with flatter, broken cleavage ledges. **Sheet breakup** controls the per-sheet damage; the material color is not used to draw layers.
3. **Cracks:** a deterministic network of up to **56 finite 3D segments** is projected onto the organic base. Parent paths have correlated direction changes; forks share existing vertices and taper toward their tips. SDF slit cutters have a depth profile plus irregular flared/chipped mouths. They are shell-limited relative to the **already-detailed substrate**, not the original smooth form, so ledges do not erase their lips. **Crack branching** and **Fracture edge chipping** are independent controls. Spacing changes network density and reach within the fixed segment budget; it is an art control, not an exact uniform interval.
4. **Pores:** the original seeded centers/population remain, but **Pore irregularity** changes spherical cuts into randomly rotated, anisotropic superellipsoids joined to secondary lobes, with gently uneven walls. This makes oval, asymmetric and merged vesicles rather than only circles. Zero irregularity returns to round cavities. These are geometric cuts, not dark painted dots.
5. **Grain:** fine 3D displacement bands also change the field. Bands smaller than the current pixel footprint fade out rather than being enlarged into visible blobs.

Normals are finite differences of the **same effective field used for ray intersection**. This is not the main terrain editor's legacy shader-normal relief. Clay and Silhouette views provide direct visual checks independent of the SatMap coloring.

The six parameter recipes—sandstone, basalt, granite, slate, limestone and river-worn stone—are **authored visual approximations**, not measured samples or simulations of the geological processes that formed a particular real rock.

## SatMaps and lighting

All 100 built-in CLUTs are available, with search and their satellite/authored/fantasy labels. The lab does not use their shared satellite-luminance detail maps. Geometry-derived height and signed relief select colors in the CLUT; palette, saturation, bias and contrast do not modify geometry.

Lighting uses a dielectric GGX response, an analytic environment/fill, SDF-traced soft shadows and local field-probe ambient occlusion. These help the cavities and ridges read in depth. Roughness and colors remain authored, rather than a calibrated measured rock BRDF. There is no HDR-photo lighting dependency or claim of photometric accuracy.

## Try these checks

- **Orbit** by dragging; wheel or the +/− buttons zoom. **F** frames the stone. Hold **C** while the canvas is focused to see the smooth form temporarily.
- Disable **SDF surface detail**: the actual geometry returns to the base form.
- Enable **Compare smooth / detailed** and move the split. Both halves use the same camera, light and palette; their signed geometry differs.
- **Clay** removes the palette. **Albedo** removes lighting. **Normals** shows geometric normals. **SDF relief** encodes inward/outward displacement relative to the base. **Silhouette** shows actual hit/miss coverage.
- **March steps** reveals raymarch work. Red marks an exhausted tracing budget; it is not a valid hit disguised with a flat material.
- Zoom in, then vary grain relief/size or pore coverage. At a distance, the finest bands intentionally disappear through geometric LOD filtering.

A displayed footprint of 2 mm cannot faithfully resolve a 0.5 mm feature. Draft, Balanced and Close-up change pixel/step budgets, not the geological meaning of the millimeter controls. Preset selection retains the chosen preview budget so selecting a different rock does not unexpectedly raise GPU load.

## Important numerical limits

After displacement and CSG, the function is a **distance estimator**, not a globally exact Euclidean signed-distance function. Tracing uses an outer envelope, separate component gradient bounds for the displaced substrate, fractures and pores, and a narrow detail band. Verified over-relaxation accepts a longer step only after checking that consecutive empty bounds overlap; otherwise it backtracks before accepting a hit. Sign crossings are refined by bisection. There is still a bounded number of steps. Very narrow fissures, grazing views or extreme combinations of controls can require more work or exhaust that budget.

The effective field is band-limited by a frame-wide world-space pixel footprint. Consequently, unresolved geometry is intentionally simplified when zooming out or lowering preview quality. It is not a view-independent mesh at unlimited microscopic resolution.

Pore layout, fractures, grain and weathering are procedural constructions. The prototype does not simulate crystal growth, weathering over geological time, fracture mechanics, multiple scattering, inter-reflection inside every pore, or every mineral's optical response. A plausible visual match is not a physical measurement.

## Performance and integration boundary

This is a standalone **WebGL2** raymarcher with GPU backpressure and on-demand redraw. Turntable mode redraws continuously; hidden tabs stop rendering. There is no RTX-only feature dependency, and no per-frame CPU volume generation. No specific GTX/RTX frame rate is promised: expensive close-up or highly fissured settings need measurement on the target hardware.

The main terrain simulation, its SDF grid, sculpt/erosion code, and its existing materials are unchanged. Its voxel spacing is far larger than these millimeter features. Integrating this technique into that renderer would require careful narrow-band detail evaluation, consistent normals/shadows/reflections, LOD and performance profiling; it cannot simply be baked into the current coarse volume at full microscopic fidelity.

There is no stone mesh export in this experiment. A future mesh bake would need a resolution that actually resolves the selected features. PNG export is the rendered view; it is not an editable geometry asset.

## Save and deployment

- **Save** writes a recipe to the separate browser key `frontier.sdf-material-lab.v1`. It never overwrites a terrain project.
- **Export recipe** downloads compact, versioned parameter JSON (v2). Version-1 recipes still load: their original numeric controls are preserved and the four new structure controls receive defaults. The refined geometry algorithms intentionally change the older appearance. **Import recipe** validates the format, all numerical bounds, booleans, quality/view and registered palette ID. Files over 64 KB are rejected. Recipes contain no executable shader text or external asset URLs.
- **Export PNG** reads owned render pixels, not a recycled display surface, and supports the inspection views and comparison split.
- Vite builds both `index.html` and `material-editor.html`. Both use relative assets in the Pages build. The second source HTML also recovers to `docs/material-editor.html` if a repository root is mistakenly published.

## Verification

- CPU tests cover seeded noise, bounded form, disabled-detail identity, actual intersection changes, palette/geometry independence, finite geometric normals, detail filtering and strict recipe round trips.
- Browser tests compare GPU field samples with the CPU reference across all six specimens, including targeted branch junctions, deep cuts and organic pore walls.
- Structure tests check ordered/nonuniform bed boundaries, flat sheet interiors, joined/tapered branches, finite support, flared mouths and asymmetric pore contours. Palette and lighting changes reuse the same cached fracture descriptors.
- Image tests demonstrate changed hit silhouettes and changed clay surfaces when detail is enabled; changing only the SatMap preserves the silhouette.
- Browser checks cover comparison, controls, recipe import/export, main-project storage isolation, mobile navigation and a missing-WebGL2 explanation.

The development-only `window.__materialLab` hook supports these probes and is not published by the production build.

**Implementation:** `src/material-lab/model.ts`, `base.ts`, `field.ts`, `fractures.ts`, `structure.ts`, `stone.glsl`, `structure.glsl`, `shader.ts`, `renderer.ts`, `MaterialLab.tsx`; entry point `material-editor.html`.

The fracture graph is generated on the CPU only when its seed/base/spacing/branching changes and uploaded as small uniform arrays, not as a texture or mesh. Per-frame shading and ray intersection still evaluate the 3D field on the GPU. This is procedural fracture geometry, not a stress-propagation or geological fracture-mechanics solver.
