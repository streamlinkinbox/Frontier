# Frontier Editor components and their native C++ counterparts

Reference UI: `Experimental/FrontierEditor` — the React outliner/inspector prototype that the native editor is
converted from. This document is the cumulative component register. One row per browser component, the native
route that mirrors it, and the honest current standing. Update the row in the same change that moves the code.

Status legend: **Native** = a dedicated C++ route draws the approved cards; **Partial** = native behaviour exists
but presentation, order or content differs; **Browser only** = no dedicated native route yet.

## Component register

| Browser component                   | Inspector family            | Native C++ route                                   | Status       |
| ----------------------------------- | --------------------------- | -------------------------------------------------- | ------------ |
| `light-inspectors.jsx`              | Point / Spot / Area / Tube / Strip | `Engine/Editor/LightInspectorPanel.cpp`      | Native       |
| `light-graphics.jsx`                | Emitter instruments         | `LightInspectorPanel.cpp` — polar, cone, falloff, figure | Native   |
| `celestial-controls.jsx`            | Sun / Stars                 | `SunInspectorPanel.cpp`, `StarsInspectorPanel.cpp`  | Native       |
| `moon-controls.jsx`                 | Moon                        | `MoonInspectorPanel.cpp`                            | Native       |
| `flare-inspector.jsx`               | Lens flare                  | `LensFlareInspectorPanel.cpp`                       | Native       |
| `atmosphere-shared-controls.jsx`    | Atmosphere / Clouds / Fog   | `AtmosphereSkyInspectorPanel.cpp`, `CloudsInspectorPanel.cpp`, `FogInspectorPanel.cpp` | Native |
| `weather-graphics.jsx`              | Wind / Precipitation / Rainbow | `WeatherInspectorPanel.cpp`                      | Native       |
| `local-volumes.jsx`                 | Local cloud / Local fog     | `FogInspectorPanel.cpp`, `CloudsInspectorPanel.cpp` | Partial      |
| `camera-graphics.jsx`, `optics.js`  | Camera                      | `CameraInspectorPanel.cpp`                          | Native       |
| `organization.jsx`                  | Folder / collection         | `InspectorPanel.cpp`, `CollectionSequence.h`        | Native       |
| `construct-menu.jsx`                | Construct palette           | `NativeConstructPanel.h`, `ConstructWorld.cpp`      | Native       |
| `outliner-icons.jsx`                | Outliner artwork            | `OutlinerPanel.cpp`                                 | Partial      |
| `material-inspector.jsx`            | Material channels           | Generic Surface group only                          | Browser only |
| `object-inspector.jsx`              | Mesh transform / material   | Generic Transform group only                        | Browser only |
| `foliage-inspectors.jsx`            | Forest / species            | None                                                | Browser only |
| `fluid-graphics.jsx`                | Lake / ocean / river / liquid | None                                              | Browser only |
| `property-graphics.jsx`             | Terrain instruments         | None                                                | Browser only |
| `bake-quick-tiles.jsx`              | Bake tiles                  | Terminal bake sections in the dedicated panels      | Partial      |

## 2026-10-07 — Scene emitters added to the browser editor

The editor had no scene lights at all. Its object list ran Sun → Moon → Stars → Atmosphere → Clouds → Wind →
Forest → Terrain → water bodies → meshes → Material → Camera, and the only lighting entry anywhere was a single
`Area emitter` row in the Construct palette. The Sun is the scene's directional source and is deliberately
untouched by this change.

### Added

- `light-inspectors.jsx` — five emitters matching the native `PunctualLuminaireRecord` core types: **Point**,
  **Spot**, **Area** (rectangle), **Tube** and **Strip**. Directional remains the Sun's own inspector.
- `light-graphics.jsx` — four instruments drawn from the authored values:
  - `PhotometricPolar` — luminous-intensity distribution. Uniform sphere for a point source, a cone with a soft
    shoulder between the inner and outer angle for a spot, a Lambertian cosine lobe for the surface emitters.
  - `BeamCone` — side elevation of the cone against a floor plane, with the lit pool at the authored reach.
    Dragging horizontally widens or narrows the outer cone.
  - `FalloffCurve` — inverse-square attenuation across the authored reach, marking the half-illuminance distance.
  - `EmitterFigure` — dimensioned schematic of the rectangle, tube or strip.
- A `Lighting` group in the outliner, its folder in the organization migration (bumped to version 3), and the
  existing approved artwork for the three emitters that have it (`editor-point-light.svg`,
  `editor-spotlight.svg`, `editor-area-light.svg`). Tube and Strip keep their own symbols rather than being
  relabelled with artwork that does not depict them.

### Card order

The order follows the sequence already accepted for the native light panel in C071/C072, so the conversion is a
redraw rather than a rearrangement:

```text
Luminous output → Colour temperature → Reach & falloff → Beam shape (spot)
   → Emitter dimensions (surface) → Placement → Shadows & response
   → Luminous distribution (punctual) → Renderer support
```

### Photometry

Flux is the authored quantity; everything else is derived from it, so the readouts cannot drift away from the
slider. Strip output is authored per metre and multiplied by the run length.

| Quantity              | Derivation                                                      |
| --------------------- | --------------------------------------------------------------- |
| Intensity, punctual   | `I = Φ / Ω`, with `Ω = 4π` for a point and `2π(1 − cos(θ/2))` for a spot |
| Intensity, surface    | `I = Φ / π` — Lambertian                                         |
| Illuminance at reach  | `E = I / d²`                                                     |
| Luminance, surface    | `L = Φ / (π · A)`                                                |
| Spot pool diameter    | `2 · d · tan(θ_outer / 2)`                                       |

### Renderer support is reported, not implied

Every emitter ends with a support card carrying three explicit states, matching what the engine actually does
today: the scene record persists; punctual sources are consumed by the lighting kernel while extended emitters
are not; and no lightmap bake path exists. Unsupported states are labelled rather than drawn as if they worked.

### Executed verification

`Exhibits/Workbench/Lighting/CaptureEmitterCards.mjs` drives the running editor in headless Chromium and asserts
the result rather than only photographing it.

```sh
npm --prefix Experimental/FrontierEditor install --no-save puppeteer
npm --prefix Experimental/FrontierEditor run dev -- --port 5173
node Exhibits/Workbench/Lighting/CaptureEmitterCards.mjs
```

Result on 2026-10-07: **162 checks passed**, 11 images written to `Exhibits/Gallery/Lighting/`. The checks cover
every card heading per emitter at 1600 px and 1120 px, the conditional cards appearing only on the emitters that
own them, finite derived photometry, the three support rows, the `Lighting` folder and all five outliner rows.
Uncaught script errors and unexpected failed requests fail the run; the absent native Construct bridge and the
unreachable Google Fonts CDN are the two allowed network exceptions and are named in the source.

### Not claimed

This is the browser reference. It is not a renderer change, and no native C++ was modified by this entry — the
native `LightInspectorPanel.cpp` still draws the earlier card set. The emitter instruments are authoring
diagnostics computed from the authored record, not scene-camera imagery.

## 2026-10-07 — Scene emitters converted to native C++

`Engine/Editor/LightInspectorPanel.cpp` was rewritten as a redraw of the browser cards rather than a second
design. The previous contents were built against `ProjectZeroEditor` and drew a different card set
(`Main light visual / data`, `Statistics`, `Quick controls`, `Source & response`, plus a separate LED-strip
electrical panel). Those cards no longer exist in the reference, so they were removed.

### What now matches exactly

Card order, headings, body sentences, pill text, range captions and status wording are transcribed from
`light-inspectors.jsx`. The palette constants at the top of the file are transcribed from `style.css`
(`.card` gradient `#222222 → #1f1f1f`, border `#2f2f2f`, radius 18, padding 22/23; `.metric` 45 px weight 300
`#dfdfdf`; `.small-pill`; `.muted`; `.range-labels`; grid gap 14). The four instruments are reimplemented on
`ImDrawList`: the photometric polar, the beam cone, the falloff curve and the emitter figure.

Derived photometry uses the same expressions as the browser, so both produce identical readouts:

| Emitter   | Flux     | Intensity | At 12 m  | Extra                      |
| --------- | -------- | --------- | -------- | -------------------------- |
| Point     | 1,500 lm | 119 cd    | 0.83 lx  | —                          |
| Spot      | 1,500 lm | 2,113 cd  | 15 lx    | pool 12.5 m across         |
| Area      | 1,500 lm | 477 cd    | 3.32 lx  | 663 nit, 0.720 m²          |
| Tube      | 1,500 lm | 477 cd    | 3.32 lx  | luminance from 2πrl        |
| LED Strip | 1,350 lm | 430 cd    | 2.98 lx  | 900 lm/m × 1.5 m           |

### Supporting changes

- `PunctualLuminaireRecord` gained `ShadowSoftness`, `DiffuseResponse` and `SpecularResponse`. The
  "Shadows & response" card is authored in the reference, so it needed real persistent storage rather than
  values inferred from intensity.
- `EditorFeedSequence.cpp` publishes an `Emission` group carrying `Luminous flux`, `Colour temperature`,
  `Shadow softness`, `Diffuse response` and `Specular response`.

### One deliberate difference

The browser authors **Aim** with azimuth and elevation sliders. Natively the beam axis is owned by the
placement transform, so the native card reports azimuth and elevation as readouts derived from that transform
and says so in the caption. Adding a second authority that writes back into the transform would have been a
behaviour change, not a conversion. This is the only place the two differ, and it is visible in the capture.

### Executed verification

```sh
python3 Exhibits/Workbench/Lighting/RunNativeEmitterCards.py
```

Compiles ImGui, `ControlPanel.cpp`, `LightInspectorPanel.cpp` and the proof, runs it, and rasterises the real
draw commands through the CPU rasteriser. Result on 2026-10-07: **19 checks passed**, 10 captures in
`Exhibits/Gallery/LightingNative/` — each emitter at 1180 px and at 760 px. The derived photometry is asserted
against the browser figures in the proof itself, so the two cannot drift apart silently.

## 2026-10-07 — Exactness corrections to the native conversion

Three defects were reported against the first conversion. All three are fixed and visible in the captures.

| Defect                | What was wrong                                                     | Now                                              |
| --------------------- | ------------------------------------------------------------------ | ------------------------------------------------ |
| Card icons            | The heading drew an accent swatch, not the lucide glyph             | `StrokeGlyph` redraws the lucide 24-unit geometry; LED Strip carries the stacked-sheet `Layers` mark |
| Sliders               | Rail `#3a3a3a`, fill `#dcdcdc`, 13 px thumb, no ring                | Rail `#444`, fill `#c5c5c5`, 3 px height, 11 px thumb with the `0 0 0 4px #222` ring |
| Transform             | A single "Placement" position row                                   | A `Transform` card with POSITION / ROTATION / SCALE, three columns each |

`PunctualLuminaireRecord` placement now publishes `Rotation` and `Scale`, recovered from the placement basis
by normalising each column, so the grid edits the same matrix the gizmo does rather than a parallel copy.

## 2026-10-07 — Retracted: the reference editor was the wrong one

An earlier revision of this file claimed the redesigned cards — the Fog set, the card note and the
Transform grid — were "not reachable in any published ref". That was wrong, and so was everything built on
it. The search only ever covered `Experimental/FrontierEditor`. All of it lives in
`Experimental/ProjectZeroEditor/InspectorDepot`, which is the editor that actually carries the light family:
`panels/lights.js` for point and spot, `panels/advancedLights.js` for IES, area and tube, `panels/fog.js`
for the Fog cards and the `mp-note` card note, and `TransformPanel.jsx` for the Position/Rotation/Scale grid.

Consequences, all settled with the author:

- The scene emitters added to `Experimental/FrontierEditor` were invented rather than converted, because that
  editor has no lights to convert. They have been reverted; `src.jsx`, `organization.jsx`, `outliner-icons.jsx`
  and `style.css` are byte-identical to the upstream originals again, and `light-inspectors.jsx`,
  `light-graphics.jsx`, the browser capture driver and the static preview built from it are gone.
- `Engine/Editor/LightInspectorPanel.cpp` was converted from those invented cards, so it matches no reference.
  It is to be rebuilt against `panels/lights.js` and `panels/advancedLights.js`.
- The `Statistics` and LED-strip electrical cards deleted as "approximations" had in fact been built from
  this reference and were correct. They are not being restored for now, by the author's choice.

The card-by-card inventory taken from the real reference, and the list of what remains to convert, are in
`Docs/InspectorDepotConversion.md`. Entries above this section that describe the browser editor's emitters
describe reverted work and are kept only as history.
