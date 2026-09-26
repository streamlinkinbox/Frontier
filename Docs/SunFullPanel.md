# Full native Sun inspector

The complete Sun composition is now drawn by `Engine/Editor/SunInspectorPanel.cpp`, called from the **real InspectorPanel** through an explicit `EditorSheetAppearance::Sun` tag. It is no longer just a gallery of isolated widgets and does not reuse the rejected circular-clock layout.

## Included in the panel

- Text-only Sun header, native row visibility control, and visible (unavailable) Reset action.
- **Top baking area**, as requested: separate **Sun lighting bake** and **Sun disk bake** panels, each with Use baked image, Bake, Load image, source caption and status.
- **Quick controls** for Sunlight, Sun disc, Day cycle and Dynamic / Static.
- Lens Flare optical-subcomponent row.
- Reference composition: **Sun direction** spanning the Sunlight intensity and Temperature cards; **Daylight cycle** and **Sun disc** below.
- The source-transcribed orbit and full-day drawings, illuminance response curve, temperature spectrum, and solar-disc illustration with guides/glow.
- **Existing C++ SliderPill controls**, not HTML sliders, including shared time controls in both direction and daylight cards.
- A **Dynamic settings** card with real-hour full-cycle duration, and a rounded temperature spectrum pill.
- A rounded, collapsible **Additional Sun settings** card containing Direct, Sun Tint, Day of Month and Month.
- Narrow single-column layout, compact stacked direction controls, native scrolling, and high-DPI rendering.

Both bake panels are above the properties. The original JSX currently places BakeQuickTiles after the cards, but the user's explicit request for top quick bake tiles takes precedence. The native app's existing viewport and outliner were not removed or redesigned.

## Binding and availability

| Control | Actual native behavior |
|---|---|
| Enabled header | Writes the selected EditorInstance's Visible field, as the original native header did. |
| Sun disc diameter | Original Angular Diameter property, 0.1–5 degrees; actual ApplySheet write-back. The native range is preserved rather than silently shrinking it to the HTML's 2-degree ceiling. |
| Local time | Original Local Hours property, shared by native sliders in both Sun direction and Daylight cycle; actual ApplySheet and ephemeris Tick. |
| Dynamic / Static | Same Clock.Animate binding as Day cycle, not a second conflicting switch. Dynamic advances the clock; Static holds it while allowing manual time edits. |
| Day duration | New 0.01–168 real hours per full 24-hour solar cycle; derives from and writes Clock.SpeedTimes. 6 h means 4× clock speed. |
| Day cycle quick control | Explicitly maps to Clock.Animate: ON advances time, OFF stops time advancement. Time remains manually editable while animation is stopped. This is not a fabricated independent feature flag. |
| Azimuth / elevation | Real solver readouts, passed numerically through the existing property records. Native sliders are visibly accompanied by “read only” and cannot write manual directions. |
| Sunlight / Sun disc quick flags | Drawn as unavailable; the existing sheet does not expose independent enable flags. They do not silently toggle the whole Sun. |
| Sunlight intensity | Original Intensity property, 0–60×; actual Light.Intensity write-back. The reference curve now tracks normalized native gain. No calibrated lux or luminance is invented. |
| Temperature | New 2000–10000 K property and RGB tint / Temperature source selector. Kelvin drives linear RGB Light.Colour; editing Kelvin activates temperature. RGB mode shows “RGB”, not a guessed Kelvin. |
| Both bake panels | Complete controls are drawn but disabled, with explicit capability tooltips/status. No Sun-specific bake, file import or image binding is claimed, and neither target is aliased to Sky's existing dome bake. |
| Reset / Lens Flare navigation | Shown but unavailable because those project actions are not exposed by this property-sheet interface. Lens Flare remains selectable through the native outliner. |

All **15 original Sun properties** remain in the backend sheet/API: Angular Diameter, Sun Tint, Intensity, Direct, Local Hours, Animate, Speed, Latitude, Longitude, Day of Month, Month, Azimuth, Elevation, Declination and Equation of Time. **Temperature**, **Colour source** and **Day duration** are additions, giving **18 properties**. The visible supplementary card now keeps only Direct, Sun Tint, Day of Month and Month. Latitude/Longitude and the Declination/Equation of Time diagnostics are hidden from this inspector; Speed is also hidden because Day duration already controls it. The underlying values and solver are preserved, not deleted. Intensity is on the main card rather than duplicated there.

All six HTML scalar controls are accounted for: time, diameter, intensity and temperature are editable; azimuth and elevation are solver-owned readouts. This does **not** mean all HTML actions are implemented: the unavailable capabilities in the table remain unavailable.

### Motion and synchronization

The Sun-direction time slider and Daylight-cycle time slider edit the **same Local Hours property**, not independent copies. The solver then updates azimuth, elevation, renderer light direction and the next frame's orbit. Azimuth/elevation remain solver outputs rather than unconstrained angles that could disagree with the clock and observer location.

Dynamic / Static and Day cycle intentionally expose the **same animation state**. Static stops elapsed-time advancement; manual time/date/location edits can still reposition the Sun. The new tile wraps beside Day cycle on narrow panels.

Duration is `24 / Clock.SpeedTimes`, so no competing duration state or second clock is introduced. A complete solar cycle still runs through local hours 00–24: changing duration accelerates or slows that cycle rather than redefining astronomy. The existing x1/x8/x30/x100 presets remain in the property API and update duration; arbitrary rates display Custom. An edited duration wins over a simultaneous preset edit. Nonfinite duration input is ignored; finite input is clamped to 0.01–168 real hours. Existing default speed is preserved (8×, a three-real-hour cycle); the main capture demonstrates six hours.

### Temperature and brightness semantics

- Default RGB mode preserves existing appearance. Merely opening the sheet never infers temperature or replaces manual RGB.
- Temperature uses a Planckian-locus xy approximation, XYZ to **linear sRGB/Rec.709**, negative-channel clipping and peak normalization. Display swatches alone use the sRGB display transfer.
- Kelvin changes actual renderer colour; intensity remains the original independent scalar gain. This is a tint approximation, not spectral blackbody simulation or constant photometric brightness. Changing tint can change luminance even with gain fixed.
- Entering temperature mode remembers manual RGB, including HDR values. Returning to RGB restores it. Editing Sun Tint explicitly switches to RGB. For simultaneous changes, a valid changed manual tint wins, then a changed selector, then changed Kelvin.
- Finite Kelvin is clamped to 2000–10000 K; nonfinite sheet input retains the last valid value. Tick refreshes temperature-derived light colour when temperature mode is active.
- Luminance (cd/m²) and illuminance (lux) are different physical quantities. Neither is calibrated by this native gain, so no redundant or misleading “luminance” slider was added.

The engine panel does not include any project header. CelestialSequence selects the presentation tag and supplies the values. Other sheets reset the tag to Generic. Font loading happens at startup, is context-owned and idempotent, and preserves ImGui's existing default font. There are no new per-frame file reads or application-owned heap allocations in the drawing code.

## Executed validation

`RunFullPanelProof.py` stages the pinned target separately in `.cache/cpp-sun-full`, applies the integration patches, builds the actual InspectorPanel and project sequence, then runs `FullPanelProof.cpp`.

- **326 assertions passed** in release, debug and ASan/UBSan with leak detection.
- Actual pointer input edits native disc, time, intensity and temperature sliders and writes back through ApplySheet.
- Actual pointer tests exercise both shared-time controls, duration, and the Dynamic / Static tile. Six-hour advancement, midnight wrapping, static renderer-record stability, preset synchronization and duration validation pass.
- Actual quick-control input changes Clock.Animate; the header changes row visibility.
- Read-only direction and unavailable controls do not invent writes.
- Eight scalar property round trips, RGB tint and speed round trips; all 15 original properties retained, plus three new properties.
- Temperature mode switching, manual HDR tint restoration, bounds/nonfinite inputs, repeated-sheet stability and chromaticity checks.
- Actual PackSkyRecord radiance/direct-sunlight gain response and CPU AtmosphereModel warm/cool response. Mode selection is checked through the property API; slider gestures use actual pointer input.
- Reusing a Sun sheet for Stars resets the presentation tag to Generic.
- Rounded supplementary-card expansion, scrolling, 320px compact direction layout, 480px single-column layout, and 2x framebuffer scale.
- New renderer source compiles with `-Wall -Wextra -Werror`.
- EditorHost and SolidArc host syntax checks passed. Updated target CMake wiring apply-check passed; no full device build is claimed.
- The existing isolated SVG drawing comparison still passes all **24 comparisons** after adding the unbound illuminance state.

Eight captures (including static mode and warm and cool temperature states) come from actual ImGui draw commands: full panel, native supplement open, narrow panel, normal-height scrolled panel, and 2x top section. PNG scanlines are losslessly recompressed after native output; the pixels are not retouched or composed in a browser.

### Stack

Both full-panel release and debug proofs pass under a **256 KiB Linux stack limit**. Compiler-reported function frames:

| Function | Release | Debug |
|---|---:|---:|
| RecordSunInspector | 1,648 B | 2,112 B |
| BuildSunSheet | 416 B | 5,056 B |
| BuildSheet dispatcher | 16 B | 112 B |
| Full-panel proof main | 1,936 B | 2,320 B |

The script enforces an 8 KiB per-function ceiling for the measured production entry points and proof. These are individual frame measurements, not a summed runtime high-water mark. Sanitizers run with their normal stack allowance. **Windows/MSVC stack values, PE reserve/commit and the entire application call chain remain unverified.**

## Scope of visual proof

This delivers the complete native panel composition and controls, not a claim of browser-pixel identity. The individual source-derived diagrams have independent SVG comparisons. A full browser/native pixel comparison was not executed: browser acquisition was blocked in the previous step. Native slider dimensions, panel-width breakpoints, unavailable states, icon tessellation, font rasterization and shadows can differ from browser rendering. The screenshots are exposed for direct review rather than described as a certified pixel-perfect match.

The viewer is an artifact viewer, **not an interactive browser reimplementation**. GPU execution was not tested.

## Reproduce and integrate

```sh
python3 Exhibits/Workbench/IconArt/PrepareTarget.py
python3 Exhibits/Workbench/SunInspector/RunFullPanelProof.py
python3 Exhibits/Workbench/SunInspector/RunFullPanelProof.py --debug
python3 Exhibits/Workbench/SunInspector/RunFullPanelProof.py --sanitize
```

Target revision: `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`.

Apply patches in order: `NativeOutliner.patch`, `SunInspector.patch` (property grouping and stack-safe builders), then `FullSunInspector.patch` (replaces Sun's rendering route), then `SunLightingBindings.patch` (project-owned temperature source and renderer bindings), then `SunMotionBindings.patch` (duration and synchronized speed presets). Copy SunInspectorPanel.h/.cpp, SunReferenceDraw.h, SunColourTemperature.h, and the SunReference fonts alongside the preceding IconArt integration. FullSunInspector.patch includes the new CMake source entry. The existing IconArt-TargetCMake.patch remains applicable for the device build.

Proofs and hashes: `Exhibits/Gallery/SunFullPanel/`. The earlier rejected SunInspector images are historical only; this is the current full-panel output.

## Lens Flare next step

See [LensFlareNativeAudit.md](LensFlareNativeAudit.md) for the inspected renderer paths, four existing presets versus three HTML effect components, current halo binding, missing controls, and shared preview/bake design. The first native implementation is now available in [LensFlareNativePanel.md](LensFlareNativePanel.md), with captures on 5179. Sun remains unchanged visually; its runner also applies the Lens Flare integration for regression coverage.
