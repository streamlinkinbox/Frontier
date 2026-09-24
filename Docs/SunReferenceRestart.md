# Sun restart — source-transcribed drawing primitives

Browser reference paths in this document are relative to `Experimental/FrontierEditor/`.

**Historical drawing-stage checkpoint. The full panel has now been assembled; see [SunFullPanel.md](SunFullPanel.md).**

At this checkpoint the restart was in progress, NOT a completed 1:1 inspector. The previous six-card Sun layout and circular clock were rejected. They are not the reference or the acceptance target for this work.

## Actual source, not replacement illustrations

`Engine/DisplayPresentation/SunReferenceDraw.h` now transcribes these actual reference components into native ImGui drawing commands:

- `environment-graphics.jsx` → **SunGizmo**: 360×280 coordinate space; 103-radius projected orbital plane; clipped horizon grid; 81-point daytime and nighttime arcs; source dash lengths, projection line, horizon marker, glow, Sun circles and cardinal/time labels. Azimuth/elevation inputs move the same geometry, including below the horizon.
- `src.jsx` → **DayCurve**: 500×123 coordinate space; the original 97-point full-day and 49-point daylight sine paths; night bands, horizon, selected-time line and marker. This is **not the rejected circular clock**.
- `environment-graphics.jsx` → **IlluminanceCurve**: 360×100 coordinate space; original 61-point cosine response, selected fill/gradient, grid, guide and marker. Native application intensity remains a gain, not klux. The proof's explicit test inputs do **not** create a fake live illuminance binding.

No HTML or screenshot is embedded into the C++ rendering. Every native pixel comes from the draw list and CPU texture sampler. SVG coordinates, sample counts and colours come from the existing JSX. Radial/linear gradients use native vertex colours. ImGui path storage is heap-managed; fixed local point arrays are at most 97 points.

## Independent comparison

`Reference.mjs` executes the **actual React components** through React server rendering. DayCurve is extracted from its actual function in src.jsx; its formula is not independently retyped into the reference generator. Source hashes are recorded.

Skia's SVG reader does not support CSS eight-digit hex colours correctly, so the reference exporter losslessly expands `#RRGGBBAA` into six-digit colour plus the corresponding fill/stroke opacity. This fixes the reference's initially black night panels and missing white grid. Text styling inherited from the HTML stylesheet is made explicit in the standalone SVG. Both renderers use the same pinned DM Sans proof font; complete browser typography remains unverified.

Four states per drawing at both 1x and 2x produce **24 independent comparisons**. Maximum measured differences:

| Drawing | Mean RGB error | Pixels with average RGB error >12/255 |
|---|---:|---:|
| Orbital gizmo | 0.230% | 0.793% |
| Full-day curve | 0.174% | 0.767% |
| Illuminance curve | 0.120% | 0.300% |

The isolated-drawing regression gate is mean RGB error ≤0.35% and ≤1.5% pixels above the per-pixel threshold. **This is not pixel identity, nor acceptance of the whole inspector.** Differences remain in text rendering, stroke antialiasing and gradient tessellation. Background-inclusive averages alone cannot certify the full UI. The viewer puts the independently rendered source SVG and native output beside each other for direct inspection.

## Stack and sanitizers

Release and debug proof executions pass with a **256 KiB Linux stack limit**. ASan, UBSan and leak detection pass separately with the normal stack allowance.

Largest measured release drawing frames: Orbit 880 B, Day 1,344 B, Illuminance 1,216 B. Debug: Orbit 832 B, Day 1,344 B, Illuminance 1,328 B. These are compiler-reported function frames, not a full application's runtime high-water mark. The 8 KiB per-function gate remains. Windows/MSVC and the production call chain are still untested.

## What remains before calling Sun a 1:1 port

- Exact five-card arrangement, responsive rules and typography from the HTML, including direction/illuminance/temperature/daylight/disc.
- Temperature strip, CSS solar-disc treatment, exact heading icons, feature tiles, child links and bake tile composition.
- Native SliderPill substitution within that reference layout; no new slider design.
- Real binding of supported fields; unbound states for unavailable lux, temperature, manual direction and independent feature/bake controls.
- Native-only properties in a separate supplementary section, without repurposing the reference cards.
- Whole-panel browser/native captures and visual comparison at agreed sizes.

Chromium downloads failed in this workspace (TLS connection failures; the package mirror was also unavailable). Therefore **no full browser screenshot comparison was executed**. Native card-layout work has not been presented as finished. The source-derived SVG comparison above did execute successfully and does not depend on Chromium.

## Reproduce

```sh
npm ci
python3 Exhibits/Workbench/IconArt/PrepareTarget.py
python3 Exhibits/Workbench/SunReference/RunProof.py
node Exhibits/Workbench/SunReference/Reference.mjs
python3 Exhibits/Workbench/SunReference/RunProof.py --debug
python3 Exhibits/Workbench/SunReference/RunProof.py --sanitize
```

Outputs: `Exhibits/Gallery/SunReference/`. The native primitives are deliberately not connected to the rejected inspector layout. The next integration must use the approved composition, not adapt the rejected cards again.
