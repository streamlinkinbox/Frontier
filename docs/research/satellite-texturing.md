# Satellite terrain texturing · Frontier 0.8

> **v0.10 library expansion:** the four image-derived maps below are preserved alongside 96 clearly labeled authored CLUTs. See the [100-map library](satmap-library.md).

## What “SatMaps” means here

Gaea describes SatMaps as color lookup tables (CLUTs) extracted from satellite data. A grayscale input selects a color in the table; it is not simply a photograph projected over a terrain. Gaea also describes deriving that input from elevation, slope, curvature, flow and soil/deposition data. Frontier implements this **style of workflow**, not Gaea's implementation or proprietary map library. [1](https://docs.quadspinner.com/Reference/Color/SatMaps.html) [2](https://docs.quadspinner.com/Guide/Using-Gaea/Color-Production.html)

Frontier's default surface now takes this path:

```text
edited 3D SDF ─┬─ elevation / geometric normal → slope ─┐
              ├─ signed local curvature               │
              ├─ geometric ambient occlusion          ├─ texture mask
              └─ upper envelope → D8 catchment         │       ↓
live transport ── runoff / net deposition ─────────────┤  clip / bias / contrast
satellite crop ─── triplanar luminance detail ──────────┘       ↓
satellite crop ─── 256-sample sRGB CLUT ────────────────→  linear albedo → lighting
```

**The legacy rock color/noise/strata shader is bypassed in SatMap mode.** Color comes from a sampled image-derived texture. Existing dielectric lighting, shoreline wetness, water, sculpting and erosion remain. The old rock material is an explicit **Legacy rock** option; pre-0.8 projects select it automatically to preserve their original appearance.

## Try it

1. Open **Materials → SatMaps**. Select a satellite palette.
2. Use **Inspect maps** to see albedo or the individual data inputs. These are actual renderer outputs, not illustrative thumbnails. Diagnostic views hide water and bypass lighting, fog, exposure and filmic processing. **Surface** restores the lit scene. The existing Clay view still inspects the geometry.
3. In **Terrain mask mixer**, tune elevation, slope, curvature, cavities, flow, deposition and source-image detail. All-zero inputs give a neutral midpoint instead of dividing by zero.
4. Adjust **Color mapping**: input contrast, bias, low/high clipping, saturation, or reversal. Reversal changes the order *inside* the clipped interval.
5. Run erosion to see runoff and net deposits affect the color. A new unweathered terrain has no deposited sediment: its deposition mask correctly starts black.
6. **Import your own map** accepts a PNG/JPEG/WebP reference image or an ordered horizontal/vertical color strip. The former is sorted by luminance; the latter preserves its color order. Fully transparent images and malformed files are rejected. Use imagery you have rights to.
7. Save locally or export a `.frontier` project. Imported palette and detail pixels are embedded, so reopening does not depend on the original image file, a network URL, or a browser object URL. The download icon beside the source exports the original 256 × 1 palette PNG, before clipping/color adjustments.

A direct entry point is `?renderer=webgl&panel=materials`. WebGPU also supports all the controls, including safe/native display.

## Data inputs and limits

| Input | How it is obtained | Important limit |
| --- | --- | --- |
| Elevation | Hit-point height, normalized to the current upper-envelope minimum/maximum | Not a georeferenced elevation model; these are the workbench's meters |
| Slope / normals | Central differences of the actual 3D SDF | Normals are world-space XYZ, not an imported tangent-space texture |
| Curvature | Scale-normalized SDF Laplacian divided by gradient magnitude, sampled at 1.4 voxels | A signed curvature **proxy**, not an exact differential-geometric estimator on an imperfect distance field |
| Ambient occlusion | Five outward SDF probes along the geometric normal | Local occlusion approximation, not a global illumination bake. It selects colors independently of lighting AO |
| Catchment | D8 steepest-descent contributing area of the edited terrain's upper surface | Top-surface drainage only; flats and closed basins remain sinks. No depression filling, subterranean watershed solver or CFD is claimed |
| Runoff | Current simulation water channel | Distinct from the decorative river's animation/direction controls |
| Deposition | Negative accumulated signed simulation displacement, weighted toward upward surfaces | **Net** deposition after subsequent erosion, not suspended sediment concentration or mineral classification |
| Photo detail | 64² luminance data extracted from the source crop; mirrored, mip-filtered triplanar sampling | Artistic detail, **not** measured relief or a scanned material. Color and optional bump have separate controls |

Catchment projection is suppressed on undersides and on surfaces below the upper envelope, so a roof's drainage is not painted onto a cave floor. Local volumetric runoff can still be visible there. AO, curvature and normals use the full volume, including overhangs.

Geometry/transport masks update as the SDF changes. Catchment recomputation is coalesced to at most once per 250 ms on WebGL and per 500 ms on WebGPU, plus computation/readback time. WebGPU extracts and reads only the small 2D upper envelope, **not the full volume every frame**. Captures wait for current maps. Original/compare terrain has a separate map; reset restores it and invalidates older pending results.

The palette and its detail map are fixed-size GPU resources. Changing a palette updates texture pixels, not geometry, simulation parameters or GPU pipelines. The sRGB CLUT decodes to linear color before interpolation/lighting; terrain masks and luminance maps are data textures, not sRGB textures.

## Sources and provenance

Four public-domain USGS satellite-image crops ship locally. They are satellite composites / enhanced colors, **not calibrated surface reflectance** and not Gaea assets:

- **Namib dunes:** USGS/NASA Landsat 7, *Earth as Art 1*. [3](https://www.usgs.gov/media/images/namib-desert)
- **Canyonlands:** USGS/NASA Landsat 8, cropped to the canyon terrain. [4](https://www.usgs.gov/media/images/canyonlands-national-park-4)
- **Volcanic coast:** USGS/NASA Landsat 9, Snæfellsnes, Iceland; bands 6/5/4 enhance vegetation. [5](https://www.usgs.gov/media/images/landsat-9-image-snaefellsjokull-and-snaefellsnes-peninsula-west-iceland)
- **White Sands:** USGS/NASA Landsat composite, bands 6/5/4. The USGS page labels it Landsat 9 / May 13, 2024, while its source-image filename identifies LC08 / May 3. The app deliberately does not infer an acquisition date from this conflicting metadata. [6](https://www.usgs.gov/media/images/landsat-9-image-white-sands-national-park)

Credits, source URLs, crop rectangles, source/crop SHA-256 hashes, extracted CLUTs and detail pixels are in `src/engine/satmaps/library.json`. `public/satmaps/` contains the compact lossless crops used for both the gallery and extraction. There are no external runtime asset requests, maps API keys, analytics, geolocation requests or image uploads.

Extraction uses 2–98% luminance quantiles and a small averaging window, making a 256-color CLUT from source pixels rather than authored color stops. Source detail is area-averaged and percentile normalized; a flat image stays flat. To rebuild from cached/downloaded originals:

```sh
npm run build:satmaps  # Node 22+; source downloads cached under .cache/satmaps/
```

The unit suite independently reconstructs each shipped palette and detail map from its local crop and checks exact equality.

## Save / export

- `.frontier` files embed custom palette (256 RGB samples) and detail (64² grayscale samples), plus all mapping controls. The existing v1 binary format is retained; the validated JSON header cap is now 64 KiB, with the existing 64 MiB whole-file cap. Custom data has exact size and hexadecimal validation; no arbitrary asset URLs are read from a project.
- GLB export evaluates the **same satellite CLUT and terrain-mask equations** into linear `COLOR_0` vertex colors. Lighting is not baked. Because this is sampled at mesh vertices, it is a **vertex-resolution approximation**, not a full-resolution UV albedo texture atlas. Micro-bump, water and atmospheric rendering are not exported.
- Viewport PNG captures reflect the selected surface/diagnostic mode.

## Verification

Tests cover image extraction and alpha handling; palette color space; flat/detail mipmaps; real-image provenance; convex/concave curvature; open/cavity AO; D8 routing, contributing-area conservation and sinks; cave projection suppression; every mask control; old/new/custom project validation and round trips; unchanged mesh geometry with changed satellite colors; and GLB metadata. Browser tests run the actual WebGL and WebGPU backends, switch all palettes and all 11 views, verify that legacy pigment changes no longer affect satellite albedo, test live displacement and drainage, compare/reset, local import errors, project reopening, palette PNG export and mobile layout.
