# Render settings inspector prototype

The owner asked for an HTML version of the Render settings inspector before it is built in the C++ editor. All of the
renderer's settings (multi-bounce, GI, reflections, ray tracing, viewport scale, debug views and the rest of the
VehicleHMI demo's render settings) live in **one outliner component** with **one inspector**. The look follows the
dashboard reference (pill tabs with a raised active pill, big readouts with dimmed decimals, dashed charts with white
markers) and the Frontier Editor's Sun, Post Process and Fracture inspectors (cards, numeric pill + knob fields, toggle
tiles, live parameter map). Colours are the C++ `InspectorPalette` (dark branch) value for value; the accent stands for
`ThemePalette::QueryAccentColor()`. Faces are DM Sans and JetBrains Mono, as `EditorTypefaces` loads them.

![The prototype: outliner, viewport with Log Stream, and the Render settings inspector](Preview.jpg)

![Inspector sections: resolution, GI, reflections, denoising, exposure, geometry, debug views](Sections.jpg)

## Open it

```sh
python3 -m http.server 8080 --directory .   # from the repository root
# then http://localhost:8080/Experimental/RenderSettingsInspector/
```

`index.html` also opens straight from disk (classic scripts, relative paths). The fonts come from
`EngineContent/FontArchives` (`Automation/PrepareFonts.py`), with bundled DM Sans woff2 files as the fallback.

## Files

| File                          | Contents                                                                         |
| ----------------------------- | -------------------------------------------------------------------------------- |
| `index.html`                  | Outliner, viewport with Log Stream, inspector                                    |
| `RenderSettingsInspector.css` | Palette tokens and every control's look                                          |
| `InspectorControls.js`        | Reusable controls: rows, tiles, cards, charts, parameter map, inertial scrolling |
| `RenderSettingsCatalogue.js`  | Settings, levelspace keys, availability rules, extents, ray count, cost model    |
| `RenderSettingsInspector.js`  | The page: outliner, viewport, hero, the ten sections, levelspace record          |
| `Fonts/`                      | DM Sans 300 / 400 / 500 (OFL)                                                    |

The Force Field and Particle editor prototypes reuse this design and these controls (2026-11-03). Added for them,
backward compatible: refresher scopes (`OpenScope`, `CloseScope`, `ReleaseScope`) so a rebuilt page releases its
refreshers; logarithmic sliders with m / km readings; units and labels on `TimeSeries`; `SelectRow`, `TextRow`,
`VectorRow`, `ColourRow`, `ChipToggles`, `ActionRow`, `StatGrid`, `ChipRow`, `CurvePlot`, `PolarDial`; force and
particle glyphs; and, in the stylesheet, the editors' shared chrome (outliner nesting, library sheet, minimap).
This page opens no scopes, so it behaves as before (`Exhibits/ForceFieldEditor/Images/RenderSettingsRegression.jpg`).

## What is real and what is estimated

| Shown                    | Source                                                                                 |
| ------------------------ | -------------------------------------------------------------------------------------- |
| Viewport and debug views | The C++ viewport offscreen on SwiftShader (`Exhibits/SceneViewport/Images/Inspection`) |
| Render and trace extents | The demo's rule: viewport × scale under the grade's pixel ceiling, ⌈÷ trace scale⌉     |
| Rays per refresh         | Exact upper bound from the settings (every path to its last bounce)                    |
| Quality grade rows       | `IlluminationConstants.cpp`, field for field                                           |
| Scene counts             | The port's readout: 504 TLAS instances, 20,878 BLAS triangles, 8,554 cards             |
| Refinement figures       | `Documentation/Plans/Ongoing/RayTracedLighting.md`                                     |
| GPU time and its bars    | A prototype model; the C++ inspector shows measured submission times instead           |

## One component

The record `RenderPipeline` ("Render settings") absorbs `Diagnostics`, `PolygonClusters` ("Geometry clusters"),
`DistanceField` ("BVH & distance field") and `SurfaceCards` ("Surface cache"). Its sections:

| Section                  | Drives                                                                                |
| ------------------------ | ------------------------------------------------------------------------------------- |
| Hero and quick controls  | Renderer (RTX, GTX, Analytic), the demo's five switches, ray-traced shadows           |
| Resolution & sampling    | Viewport scale, trace resolution (Full / Half / Quarter), paths per pixel, grade      |
| Global illumination      | Indirect GI, path bounces, NEE, roulette, ReSTIR reuse, intensities, surface cache    |
| Reflections              | Reflections, RTX rays, bounces, intensity, roughness cutoff, mirror filter and trace  |
| Sun & shadows            | Sun shadows, ray-traced shadows, firefly filter, solar disc, stratified sun, penumbra |
| Denoising & antialiasing | Variance filter, diffuse and glossy wavelet levels, history, edge stops; temporal AA  |
| Exposure                 | Auto exposure, compensation                                                           |
| Geometry                 | Virtualized geometry, stream slots, GPU cluster culling                               |
| Distance field & BVH     | The grade's four clipmap cascades, freezing the field                                 |
| Debug views              | The fifteen inspection views, BVH / SDF view, readout overlay, slice inset            |
| Refresh timing           | Per-submission times of the last refresh                                              |

The levelspace block at the end writes the record; keys marked `# proposed` are new for the C++ step, the others are
already read by `SceneWorld.cpp`. The à-trous rows show wavelet levels: `VarianceIntegrator` dispatches
`min(4, WaveletIterations + 1)`, so the demo's glossy filter runs 3 and the diffuse filter 4.

## Found while building it

- Inspection view 6 (BVH / distance field) was black in the C++ port: nothing wrote `InspectionRadiance`, because the
  demo's `InspectionMarch` was not ported. It is now, and the prototype shows the new capture.
- Cluster views 10 and 11 show the demo's own diagnosis without virtualized geometry: the orange checker
  (`ClusterDebugFallback`) and an empty visibility buffer. The prototype switches to the virtualized captures when
  Virtualized geometry is on.
- `BloomIntensity`, `AcesFilmicTonemap`, `ScreenSpaceReflections` and `InspectionDistanceMode` in
  `RenderPipelineConfiguration` have no reader in the renderer; the prototype leaves them out.
- The viewport forwards only GI, TAA, exposure, inspection, virtualized geometry, analytic illumination and the
  refinements today; quality and the RTX tier apply at construction.

## C++ step (approved and built, 2026-11-03)

The owner approved the prototype; the C++ editor now has it: `Frontier/Engine/Interface/Inspectors/`
`RenderSettingsInspector` and `RenderSettings/`, one file per section, built from the shared controls in
`Interface/Controls`. Render settings is not removable.

| Step                               | In C++                                                                 |
| ---------------------------------- | ---------------------------------------------------------------------- |
| One component, one inspector       | The `RenderPipeline` record "Render settings", first in the Outliner   |
| The four absorbed records          | Removed; older levels load without them (none reached the renderer)    |
| Every setting reaches the renderer | `ViewportRendering`: toggles, trace scale, card tiles, both filters    |
| Trace resolution, viewport scale   | Rebuild the screen targets; quality and RTX / GTX rebuild the viewport |
| View 6 (BVH / distance field)      | Ported: `InspectionMarch.slang` (normals, step heat, light)            |
| The fast analytic renderer         | A debug view of its own (view 15), on the page and the viewport bar    |
| Timing                             | Measured refresh times (latest, smoothed, median, slowest), charted    |

Differences from the prototype, all deliberate:

- **Measured, not modelled.** The timing card shows each refresh's wall time from its first submission to its fence
  (encoding and GPU work). Per-stage costs exist only with staged submissions (`SubmissionTiming`), so the page shows
  no stage breakdown.
- **Sections are panes.** The nine sections sit under two rows of tabs below the timing card, as the Sun and Stars
  pages do, instead of one long scroll.
- **Left out because the renderer has no such switch:** GPU cluster culling on/off, triangles per leaf, the cluster
  bounds overlay, freezing the distance field and the distance-slice inset. The readout overlay stays on the viewport
  bar (H).
- **The analytic renderer is a debug view** (owner's request), not a third renderer tab.
- Bloom, the filmic tonemap toggle, screen-space reflections and the probe range had no reader; their keys are
  ignored when an older level carries them.
