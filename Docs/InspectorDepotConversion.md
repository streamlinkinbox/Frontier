# Browser light panel → native C++ port register

Reference: **`Experimental/ProjectZeroEditor/index.html`**, the 4.87 MB self-contained bundle, confirmed by the
author on 2026-10-08. Built by `Build.mjs` from the sources beside it. The light panel is `LightPanel.js` +
`LightPanel.css` + `LightProjection.js`, with its transform block from `EmitterPanel.jsx` → `TransformPanel.jsx`.

## History

The panel was first built from `Experimental/FrontierEditor` and then from `InspectorDepot/panels/lights.js`;
both were wrong. `Engine/Editor/LightDepotSurface.h` has been deleted and `LightInspectorPanel.cpp` rewritten
against `LightPanel.js`.

Two earlier misses, recorded so they are not repeated: the first attempt ported `Experimental/FrontierEditor`
(an older editor with no light family, so five emitters were invented); the second ported `InspectorDepot`.
Neither is the shipped UI.

## What a light inspector is

`LightPanel.js` handles seven styles — `pointlight`, `spotlight`, `ieslight`, `arealight`, `tubelight`,
`ledlight`, `ledstrip`. The host is `div.mpanel.lighting-panel`, `display:flex; flex-direction:column; gap:12px`,
and the children are appended in this order by the final `replaceChildren`:

| # | Block | Class | Head / caption |
| --- | --- | --- | --- |
| 1 | Preview | `pcard lp-card lp-preview` | Radial falloff · Beam envelope · Photometric distribution · Luminous surface · Linear radiance · LED emitter · Ribbon light |
| 2 | Readings rail | `mp-rail lp-readings` | two tiles, type-specific |
| 3 | Scene participation | `pcard lp-card lp-participation` | caption `FLAGS` |
| 4 | Output | `pcard lp-card lp-output` | Source & colour · Driver & colour · Output per metre — caption `OUTPUT` |
| 5 | Response | `lp-response`, appended **inside** Output | Distance response · Beam section · Angular response · Aperture balance · Electrical budget · Conversion budget · Linear output — caption `ANALYTICAL` |
| 6 | Shape | `pcard lp-card lp-shape` | Attenuation · Beam shaping · Distribution profile · Package & optic · Layout & segments · Emitter dimensions — caption `OPTICS` |
| 7 | Transform | `lp-card lp-transform` | `TransformPanel.jsx`, head `Transform`, kicker `WORLD SPACE` |

The Preview header carries `lp-source-icon` (25 × 25) **prepended before the title** — this is the LED Strip
icon. It is `LightIcons[Style]` from `LightSpecification.js`, inlined as base64 SVG by `Build.mjs`.

## Geometry and palette — `LightPanel.css`

The palette is green-tinted, not the neutral grey of InspectorDepot. That alone distinguishes a correct port.

| Element | Spec |
| --- | --- |
| `.lp-card` | bg `#191919`, border `#ffffff0c`, radius 18, padding 17 × 15, `overflow:hidden` |
| `.lp-card > header` | flex, space-between, margin-bottom 15 |
| `.lp-card h3` | 14 px, weight 500, `#dedede`, no tracking, sentence case |
| `.lp-card > header > span` | 8 px, `#7c887c`, letter-spacing 1, right-aligned |
| `.lp-preview` | padding 15 / 12 / 12 |
| `.lp-primary` | 42 px, weight 300, letter-spacing −1.8, `#edf0e7`, tabular; `.lp-decimal` `#768074`; `small` 14 px `#a9b3a4` margin-left 7 |
| `.lp-primary-caption` | 10 px `#98a091`, margin-top 8 |
| `.lp-descriptor` | 10 px `#b1b9a8`, line-height 1.5, border-top `#ffffff0c`, margin-top 16, padding-top 11 |
| `.lp-canvas` | full width, radius 6; preview 260 px, response 180 px for point else 100 px |
| `.lp-study-note` | 9 px `#828d7e`, margin-top 3 |
| `.lp-readings` | grid, 2 × 1fr, gap 8 |
| `.lp-readings .mp-pill` | radius 6, min-height 80, bg `#20231f`, padding 13 × 12, **`column-reverse`** so the label sits above the value, gap 12 |
| `.lp-readings .v` | 24 px, weight 300, `#dce4d5`, letter-spacing −0.6 |
| `.lp-readings .k` | 9 px, `#9ba993`, sentence case |
| `.field` | margin 17 0; label 10 px `#999`, margin-bottom 9 |
| `.field input[type=range]` | height 26, radius 30, filled `#454545` to `--fill`, unfilled `#242424` |
| `.split-value input` | bg `#000`, width 58, 11 px |
| `.lp-colour` | flex, gap 10, 10 px `#999`, margin-top 17 |
| `.lp-profiles` | grid 1fr 1fr, gap 6; button padding 9 × 6, radius 4, bg `#242424`, `#909590`, 10 px, border `#ffffff0a` |
| `.lp-profiles button[aria-pressed=true]` | bg `#363e37`, border `#788578`, `#eef0eb` |
| `.lp-note` | 9 px `#6e756f`, line-height 1.5, margin 12 0 |
| `.lp-toggle-row` | flex, space-between, 11 px `#999`, margin 15 0 |
| `.lp-switch` | 38 × 21, radius 24, bg `#3c3c3c`, padding 2; knob 17 × 17 `#ddd`; checked bg `#32c763`, knob translateX 17 |
| `.lp-transform .transform-card` | padding 0, no background, no border — the grid sits directly in the card |

## Controls per type

Flux: `ledlight` → `watts × efficacy × dimmer`; `ledstrip` → `lumensPerMetre × length × dimmer`;
point and spot → `intensity`; everything else → `lumens`.

| Type | Output fields | Shape fields | Rail tiles |
| --- | --- | --- | --- |
| point | Intensity 0–60 cd | Reach 1–120 m, Decay exponent 0–4 | At 5 m · estimate (lx), Reach (m) |
| spot | Intensity 0–200 cd | Full cone angle 2–80°, Penumbra 0–1 | Full cone (°), Soft edge (%) |
| ies | Luminous flux 0–8000 lm, Colour temperature 1800–12000 K | 8 profile buttons, note *Preset illustration · IES file import pending.*, Profile multiplier 0–4 ×, Field angle 5–100°, Cut-off pitch −5–5°, Photometric range 1–250 m | Scaled output (lm), Field angle (°) |
| area | Luminous flux 0–20000 lm | aperture Rectangle/Disk, Width 0.1–20 m, Height 0.1–20 m, Beam spread 1–180°, toggle Two-sided emission | Aperture (m²), Emission |
| tube | Luminous flux 0–12000 lm, Colour temperature | Length 0.1–20 m, Tube radius 0.01–1 m, Reach 1–120 m | Linear output (lm/m), Tube radius (mm) |
| led | Driver power 0.1–100 W, Efficacy target 10–250 lm/W, Dimmer 0–1, Colour temperature | Package diameter 5–120 mm, Emission angle 10–180° | Driver power (W), Efficacy target (lm/W) |
| strip | Flux per metre 10–4000 lm/m, Load per metre 1–50 W/m, Dimmer 0–1, Colour temperature | Strip length 0.1–20 m, Emitter density 10–240 /m, Supply voltage 5–48 V, toggle Opal diffuser | Connected load (W), Emitter count (LEDs) |

Scene participation: `Cast shadows`, plus `Draw distribution` / `Draw cone` / `Show glow` / `Draw emitter`.

Transform rows are fixed and include rotation and scale:
`["Position","m",-100000,100000,0.01]`, `["Rotation","deg",-36000,36000,0.1]`, `["Scale","×",[1,1,1],0.001,1000,0.01]`.
`LightRotation()` recovers rotation from a legacy `target` by `asin(dy/len)` pitch and `atan2(-dx,-dz)` yaw.

## Illustrations — `LightProjection.js`, 627 lines

`ProjectLight` (preview) and `ProjectResponse` (response) each branch over all seven styles and draw in a fixed
design space — the point preview centres on (190, 123) with four 24 px reach rings, twelve spokes, a glow at
`min(0.45, flux/100)` and a 6 px core, labelled `OMNIDIRECTIONAL`. The point response is an illuminance plot
with left 43, right `width − 15`, top 28, bottom `height − 30`, five gridlines at `#ffffff14` and labels in
`#929b8f`. These are a second port of comparable size to the chrome and have not been started.

## Status

| Item | Status |
| --- | --- |
| Reference identified and recorded in `CLAUDE.md` | Done |
| Card set, palette, geometry, per-type controls transcribed | Done — above |
| Native chrome port — all six blocks, seven styles | Done — `LightInspectorPanel.cpp` |
| `ProjectLight` illustrations, seven styles | Done — `PaintStudy` |
| `ProjectResponse` plot and sections | Done — `PaintResponse` |
| Superseded InspectorDepot conversion removed | Done — `LightDepotSurface.h` deleted |
| Header source glyph (`lp-source-icon`) | Done — shipped artwork, baked for the harness |
| Control interaction (drag, click, write-back) | Outstanding — the port draws, it does not yet edit |
| Fog cards (`FogPanel.jsx`, `FogShapePanel.jsx`) | Not started |

### The source glyph

`LightPanel.js` prepends a 25 x 25 `img` into each card header, `LightIcons[Style]` from
`LightSpecification.js`: `editor-point-light`, `editor-spotlight`, `editor-dome-light`, `editor-area-light`,
`light-area-2d`, `light-point-2d`, `slate-ring-light`. All seven are real `IconSymbol` entries, so the panel
asks for the shipped artwork rather than redrawing it, through the `LightGlyphSource` hook declared in
`LightInspectorPanel.h`. The editor answers that from `IconArt` / ThorVG.

The harness cannot — this sandbox has no SVG rasteriser, `rsvg-convert` is absent and ImageMagick's SVG
delegate fails. `BakeLightGlyphs.mjs` rasterises the same seven SVGs with `@resvg/resvg-js` to straight-alpha
RGBA8 at 2x into `Glyphs/*.rgba`, which the harness reads with a plain `fread` and hands to the CPU backend as
its one custom sheet. Re-bake with `node Exhibits/Workbench/Lighting/BakeLightGlyphs.mjs` after
`npm i @resvg/resvg-js`; the baked files are committed so the proof runs without node.

### Two fills ImGui cannot do directly

`AddConvexPolyFilled` silently misdraws concave shapes. The area under a 1 / d^n illuminance curve is filled as
one quad per segment, and a photometric lobe — star-shaped about the origin but not convex — as a triangle fan
from the centre. Both looked plausible but wrong before this was caught by eye.

Acceptance test: the scripts already beside the sources — `CheckLightDesign.mjs`, `CheckLighting.mjs`,
`CheckInspectorLayout.mjs`, `CheckSharedCards.mjs`.

### The controls drive the sheet, they do not only draw it

`Adopt` takes a snapshot of the published properties, so writing to that snapshot would be discarded at the top
of the next frame. `Bind(Sheet, Label)` resolves the live `EditorProperty` behind a label and every control
takes one:

| Control | Browser behaviour reproduced | Native |
|---|---|---|
| Range | `input[type=range]` — pressing anywhere on the track seeks to it, a drag keeps following | `InvisibleButton` over the track, value from the pointer's share of the span, snapped to the field's step and clamped to its range |
| Switch | `.lp-switch` click toggles | 38 x 21 `InvisibleButton` flipping `On` |
| Profile cell | the pressed cell is the picked one | per-cell `InvisibleButton` setting `Picked` |
| Transform axis | `TransformPanel.jsx`'s drag ref — horizontal pointer travel scrubs the axis | horizontal `MouseDelta.x * Step`, clamped per row: Position +/-100000 step 0.01, Rotation +/-36000 step 0.1, Scale 0.001-1000 step 0.01 |

Every control is gated on `Editable`, so a read-only property stays inert.

Three of the harness's checks prove this rather than asserting it: they feed ImGui synthetic mouse events and
then read the sheet back, so a control that draws correctly but is wired to nothing fails the proof.

## Fog cards

The fog card family already existed natively in `Engine/Editor/FogInspectorPanel.cpp` — header, `Fog settings`,
`Visibility through fog`, `Medium`, a model-specific technical card and `Wind binding`. Three visuals inside it
were missing or had drifted from the reference, and only those three were converted.

| Browser source | Native | State before |
|---|---|---|
| `FogPanel.jsx` `FogBeamChamber` | `FogCards::PaintChamber` | Existed, drifted |
| `HeightFogVisual.jsx` | `FogCards::PaintProfile` | A different graph entirely |
| `FogShapePanel.jsx` | `FogCards::PaintVolume` | Absent |

Card order, read off `Inspectors.jsx`: `Fog settings` (142) · `Visibility through fog` (435) · a two-column grid
of `Medium` (416, with the chamber at its foot) and the technical card · `Wind binding`. The technical card is
`Height and tint` for height fog, `Spectral transmission` for aerial, and — in the shipped bundle — **`Fog volume
shape`** for local fog. The native panel titled that card `Local bounds`, which is the *cloud* card's title;
`CheckFogCards.mjs` still expects the old name and is stale against `index.html`, so the bundle won.

### What had drifted in the beam chamber

| Quantity | Reference | Native before |
|---|---|---|
| Samples | 70 rects, width 4.2 | 68 lines, width 3.2 |
| Half height | `3 + f² (11 + Spread·24)` | `3 + f² (11 + Spread·22)` |
| Exposure | `0.32 + 0.68·T^0.15` | `0.35 + 0.65·T^0.15` |
| Opacity | `(0.1 + Spread·0.2)·Exposure` | `(0.13 + min(2,Spread)·0.2)·Exposure` |
| 2 % marker | always drawn, clamped to the span | drawn only when inside the span |
| Footer | `2% · 208 m` / `10% PHYSICAL AT 120 m` | `2% range inside diagnostic span` |

Extinction is `LiveGraph.jsx`'s `FogDensity` with `AuthoredPreview` set, so a disabled medium still previews its
authored curve: height folds the density to a 25 m probe, aerial scales by a thousandth, local multiplies density
by coverage and divides by a hundred.

### The density profile is an editor, not a graph

`HeightFogVisual.jsx` is a 300 x 210 space, `preserveAspectRatio="none"`, plotting density against altitude —
the axes the old native sketch had swapped. Pressing it authors **both** values at once: falloff from the X
position (`10 + share·2990`, rounded to the metre) and density from the Y position (`share·0.2`, rounded to four
decimals), which is why the check asserts both moved from one press.

### Volume shape

`FogShape.js` is reproduced branch for branch so the vertex indices match: Box, Custom and Prism/Cylinder extrude
a ring; Cone fans 48 base vertices to an apex with a spoke every sixth; Diamond is six points; Sphere and
Ellipsoid sweep 13 latitudes by 24 longitudes. The preview is the same isometric,
`[(x−y)/√2, (x+y)/√6 − z·√(2/3)]`, fitted to 270 x 190 and centred at 160, 120. Bounds are the mesh extremes plus
Centre; Size is the mesh alone.

Two deviations, both forced and both recorded here rather than hidden:

- **Ear clipping for the caps.** A Custom outline extrudes to concave end caps, and `AddConvexPolyFilled`
  misdraws those silently — the same trap the light response curve hit. The caps are triangulated instead.
- **The `<select>` cycles.** A native popup list would have to be styled by ImGui, and the CSS says nothing about
  a dropdown's appearance, so clicking the control advances to the next entry in `FogShapes`.

Text keeps CSS letter-spacing through a per-codepoint `Tracked` helper; ImGui has no tracking of its own and the
8 px uppercase micro-labels are unreadable without it. Anisotropic glyph scaling is the one thing the stretched
profile cannot reproduce — positions and glyph height are exact, glyph width is natural.

Acceptance test: `python3 Exhibits/Workbench/Fog/RunNativeFogCards.py`, 56 checks, captures in
`Exhibits/Gallery/FogNative/`.

## Wind binding card

`WindPanel.jsx` ships `WindBinding` as a card (`data-card="Wind binding"`); the native side carried only
`Engine/Editor/WindBindingControls.h`, a bare `SeparatorText` + `Checkbox` + `BeginCombo` stub. The fog panel
already drew a card *titled* "Wind binding" with that stub inside it, so only the card's contents were missing.

| Browser | Native | Note |
| --- | --- | --- |
| `WindBinding` (`WindPanel.jsx`) | `WindCards::PaintBindingBody` | select, editor shortcut, preview, note |
| `WindCanvas` (`WindPanel.jsx`) | `WindCards::PaintCanvas` | heat field, lattice, arrows, flow strokes |
| `ResolveWind` / `EvaluateWind` (`WindSpecification.js`) | `WindCards::Resolve` / `Evaluate` | clamps and four kinds |

New file `Engine/Editor/WindPanelSurface.h` (namespace `Frontier::WindCards`). Mounted from
`FogInspectorPanel.cpp` inside the existing card; the native-only "Own wind component" switch is kept beneath
the preview as a supplement, so no native capability was removed.

### Measurements

Card chrome comes from **Editor.css** `.property-card` (padding 23/24/22, radius 22, border `#343434`,
`linear-gradient(135deg,#252525,#202020)`), not from the light or fog card rules: `h3` 12 px `#cacaca` with a
28 px drop, `> p` 11 px `#919191`, body text 13 px `#f0f0f0`. From **WindPanel.css**: the select is full width,
32 px tall, radius 16, margin `10px 0 18px`; the wide button is full width at 12 px padding with a 14 px drop;
the canvas is 180 px tall in this card (280 px elsewhere), radius 14, border `#ffffff12`; the legend is a 10 px
row with a 4 px bar running `#306e7c → #58a56e → #d3b565`; the caption is 10 px `#83948e`.

### The flow field

Canvas clear `#111b20`. A 64 × 48 speed field is drawn back at canvas size with `imageSmoothingEnabled` at
`globalAlpha .5`; bilinear smoothing between texel centres is reproduced with four-corner gradient quads. The
32 px lattice is `#ffffff09`. 190 motes are seeded deterministically from two coprime strides
(`(i·73 % 151)/151`, `(i·43 % 149)/149`), advected at eight times real time, and drawn as trails that walk five
steps upstream and fade from the tail. Speed colours are `hsl(190 − s·150, 48%, 32% + s·28%)` for
`s = min(1, speed/30)`: teal `(42,108,121)` at rest, green `(70,174,61)` at 15 m/s, gold `(202,169,104)` at 30.

### Recorded deviations

- A canvas clips its own `border-radius`; an ImGui clip rectangle cannot, so the four corner notches are
  repainted in the backdrop colour with a triangle fan (`RoundNotch`). `AddConvexPolyFilled` cannot fill them —
  each notch is concave.
- ImGui cannot fade a stroke's alpha along its length, so every trail leg is cut into four graded pieces.
- The `<select>` cycles on click rather than opening a popup, as the fog shape select already does.
- `WindEditor` — the modal "place and combine components" window, `wind-editor` in the bundle — is **not** ported.
  The shortcut button is drawn and returns its hit rectangle, but nothing is mounted behind it yet.
### Mounted in both panels

`FogInspectorPanel.cpp` reuses the card it already drew. `CloudsInspectorPanel.cpp` previously called the stub
bare at the top of the function — before the panel chrome and before its own property guard; the card now sits
**last**, which is where `WindPanel.jsx` puts `WindBinding` in the clouds branch.

One semantic correction came out of the clouds wiring: native `Wind Source` entry 0 is `WindNames[0]`,
**"Global wind"** — the shared field, not the browser's still-air blank. The native select therefore always
resolves to a named field, so both panels pass `Assigned` and the option text straight through. The
still-air and dangling-id states remain implemented and proved, they simply cannot arise from this sheet.

77 checks: `python3 Exhibits/Workbench/Wind/RunNativeWindCards.py` → `Exhibits/Gallery/WindNative`.

## The wind panel — which half actually ships

The earlier note that `InspectorDepot/` is "the wrong half" is true for lights and **false for wind**, and the
reason is one line of the build.

`Build.mjs` has two entry points. The second one is `InspectorHost.js`, and that host mounts the depot panels:

```js
import { CUSTOM_PANELS } from "./InspectorDepot/panels/index.js";
CUSTOM_PANELS[Type] = { ...CUSTOM_PANELS[Type], build: LightPanel };
```

It replaces exactly one family — lights — with `LightPanel.js`. Every other depot panel ships as written. So the
wind panel the bundle mounts is `InspectorDepot/panels/wind.js`, which is why `Anemometer` and `Beaufort` are in
`index.html` at all. Verify per family before porting; do not generalise from the light case in either direction.

`Build.mjs` also rewrites `wind.js` and `fog.js` on load through `RecolourInstrument` in
`InstrumentSpecification.js`. **Reading `wind.js` alone gives the wrong trace.** The shipped anemometer differs
from the checked-in source in three anchored replacements, all recorded below.

### The reference wind panel, card by card

| # | Card | Class | Native today | State |
|---|------|-------|--------------|-------|
| 1 | Hero flow field | `pcard mp-hero wf-hero` | `Composite wind field` (a compass rose) | **converted** |
| 2 | Rail, four pills | `mp-rail` | — | **converted** |
| 3 | Gusting / lulling duo | `mp-duo` | — | **converted** |
| 4 | Anemometer | `pcard mp-metric wf-trace` | `Anemometer` (speed text + 10 m vector) | **converted** |
| 5 | Beaufort | `pcard mp-light wf-scale` | folded into the native Anemometer text | **converted** |
| 6 | Steadiness | `pcard` | `Variation controls` + `Gust envelope` | **converted** |
| 7 | Driving | `pcard` | — | **converted** |

The native `Composite wind field`, `Variation controls` and `Gust envelope` are **not in the bundle at all** —
they are native inventions. They have been left in place: the brief was to add what is missing, not to redesign
what is already there. Deleting them is a one-line change in `WeatherInspectorPanel.cpp` if that is wanted.

`.mpanel` is `display:flex; flex-direction:column; gap:10px` and `.pcard` carries its own `margin-bottom:10px`,
so a card is followed by 20 px of air and a bare block (the rail, the duo) by 10.

### The anemometer — `Engine/Editor/WindInstrumentSurface.h`

Namespace `Frontier::WindInstrument`. Palette read from `InspectorDepot/styles.css` `:root`, which is a neutral
grey kit and shares nothing with the light family's green-tinted one: `--inset #1a1a1a`, `--field #000000`,
`--stroke rgba(255,255,255,.05)`, `--text #f0f0f0`, `--text-dim #888888`, `--text-faint #5c5c5c`, `--r-inset 18px`.

Arithmetic, straight off the source:

- `gustAt(ph) = 1 + Gust·(swell·0.55) + Turbulence·(grain·0.18)` where
  `swell = sin(ph·0.9)·0.6 + sin(ph·2.3+1.7)·0.3 + sin(ph·5.1)·0.1` and
  `grain = (sin(ph·17.3) + sin(ph·29.7+2.1))·0.5`
- the clock runs at `0.4 + Turbulence·2.2`
- 240 samples — sixty seconds at four a second — prefilled backwards from the current phase so the trace is never
  blank, then shifted one sample per quarter second with the remainder carried
- defaults `speed 4.2 · direction 214 · gust 0.3 · turbulence 0.24`

Trace geometry: `R 30 · L 2 · T 8 · B 15`, height 112 (170 when `.tall`), `top = max(2, max(trace, speed)·1.18)`,
four dashed gridlines at quarters of `top` labelled `%.1f` below 12 and `%.0f` above, a 4/4 dashed mean rule, and
`−60 s · −30 s · now` along the foot. Readouts: `floor(inst)` and `.{round(inst·10)%10}` at 46 px with the
fraction in `--text-faint`, `Mean {speed} m/s · {band}, force {n}`, and four spec tiles — gust factor `hi/speed`,
spread `hi−lo`, pressure `½·1.225·speed²`, and `km/h · kn`.

The three `RecolourInstrument` replacements, which are the shipped behaviour:

1. the green `rgba(137,224,196,.07)` gust band is replaced by a vertical wash under the trace path,
   `rgba(224,224,224,.10)` at the top inset falling to `rgba(224,224,224,.01)` at the foot
2. the trace stroke becomes `rgba(210,210,210,.36)` at 1.2 px
3. the samples that sit above the mean are re-stroked in place at `rgba(242,242,242,.95)`, and the peak and
   trough get 2.3 px dots in `#eeeeee` and `#d69a54`

Recorded deviations: a flat ImGui quad cannot carry a gradient, so the wash is banded ten deep per column; the
`.mp-x` button is drawn with its `arrowout` icon and no ground, because the CSS ground is transparent until hover.

Proof: `python3 Exhibits/Workbench/Wind/RunNativeWindInstrument.py` → **PASS 36**, four captures in
`Exhibits/Gallery/WindInstrument/`. The harness draws at 3× and box filters down — the CPU backend takes one
sample per pixel with a hard inside test, which is fine for 46 px numerals and loses thin strokes at 9 px.

### The rest of the panel

All seven blocks now live in `Engine/Editor/WindInstrumentSurface.h`.

**Hero** — 190 motes seeded off the integer sieve `x=(i·977)%1000/1000`, `y=(i·613)%1000`, `life=(i·37)%100`,
`seed=(i·131)%1000`. Advection is a two-term curl, `sin(x·7.1+t·0.7)·cos(y·6.3−t·0.5) +
½·sin(x·13.7−t·1.1)·cos(y·11.3+t·0.9)`, rotated into the heading and stepped by
`(0.03 + norm·0.55)·gust·dt·(0.65 + seed·0.7)`, with `y` scaled by the canvas aspect. Motes recycle off the
upwind edge. Streaks are `rgba(137,224,196,·)` — the hero is *not* touched by `RecolourInstrument`, only the
trace is. Bearing is meteorological: 214° means the air travels towards 34°.

**Rail** — `repeat(3,1fr) 1.25fr`, so the Force pill is a quarter wider. **Duo** — two `.mp-stat` cards; past
17 m/s the gusting card flips from `--ok` to `--danger` and swaps its wind glyph for the alert triangle.

**Beaufort** — the `.mp-meter` block scale: twelve blocks (eleven forces plus a run-out to 30 m/s) coloured
`rgb(60+t·195, 200−t·120, 180−t·120)` at 85% over black, even forces numbered in `rgba(0,0,0,.55)`, ticks at
0/10/20/30, and a pennant marker. Below it the `Coming from` tape and the land-sign note.

**Steadiness** — two tapes, `Gustiness` (STEADY/BREEZY/SQUALLY at 0/.3/1) and `Turbulence`
(LAMINAR/OPEN AIR/ROTOR at 0/.24/1), over two spec tiles.

**Driving** — `pillToggle` tags for everything carrying `windLinked`, lit when followed.

**The tape**, shared by three cards: 41 divisions, major every ten at `rgba(255,255,255,.30)`, the rest lit to
`.22` below the value and `.09` above; lengths 9 / 6 / 4; a baseline at `.10`; named marks at 8 px that are
**dropped when they would collide** within 5 px; a pennant at `base−13 → base−19` over a 1.4 px stem.

Recorded deviations: the hero's rounded clip is faked by repainting the four corner notches, because an ImGui
clip rectangle cannot be rounded; mote streaks are laid down in eight graded pieces, because ImGui cannot fade a
stroke along its length; the caption and sky gradients are banded.

Proof: `python3 Exhibits/Workbench/Wind/RunNativeWindInstrument.py` → **PASS 240**, six captures.

## The cloud panel

Same architecture as wind: `InspectorHost.js` overrides only `pointlight`, `spotlight`, `ieslight`, `arealight`,
`tubelight`, `ledlight` and `ledstrip`, so `clouds: { build: cloudsPanel, owns: ['Layer', 'Motion & tint'] }`
ships from `InspectorDepot/panels/clouds.js`. Unlike `wind.js` and `fog.js` it is **not** passed through
`RecolourInstrument`, so the checked-in source is what the bundle draws.

`Engine/Editor/CloudInstrumentSurface.h`, namespace `Frontier::CloudInstrument`. It takes the depot's shared
furniture — palette, text primitives, tape, stepper, card head, corner notch — from `WindInstrumentSurface.h`
under the alias `Kit`, because wind was converted first. It is the same kit, not a copy.

| # | Block | Class | State |
|---|-------|-------|-------|
| 1 | Satellite map | `pcard mp-hero cl-hero` | converted |
| 2 | Rail — Coverage · Optical · Base · Drift | `mp-rail` | converted |
| 3 | Duo — Sun reaching datum · Sky cover | `mp-duo` | converted |
| 4 | Coverage distribution | `pcard mp-metric cl-cover` | converted |
| 5 | Cloud deck | `pcard mp-light cl-layer` | converted |
| 6 | Morphology | `pcard mp-light cl-form` | converted |

Defaults `coverage .46 · density .62 · altitude 130 m · scale 1 · detail .55 · speed 1 · tint #eef3f8 ·
shade #5c6a7c · windLinked true`.

**The field.** `hash(x,y) = frac(sin(x·127.1 + y·311.7)·43758.5453)`, bilinear value noise with a
`t²(3−2t)` fade, then `n = vn(·028f)·.55 + vn(·067f+9,·067f−4)·.3 + vn(·16f−3,·16f+7)·(.07+d·.08)`, all over
`.92 + d·.08` and clamped. `f = max(.2, scale)`. **Run it in doubles** — the browser does, and single precision
walks off the lattice. The condensate threshold is `1 − coverage·.78`.

**Map** — 3 px cells over a corner-to-corner ramp `#111820 → #050708`; a cell draws when `n > threshold`, tinted
`shade + (tint − shade)·(body·.55 + .2)` at `(.16 + body·.72)·density`. An eight-by-five graticule, `N`, a
`12/scale km SWATH` legend, and the wind arrow at `(w−30, h−22)` pointing `(direction + 90)°`.

**Coverage** — 1600 probes of the same field binned twenty ways; bins past the threshold are
`rgba(238,243,248,.72)`, the rest `rgba(255,255,255,.11)`, with a dashed cut at the threshold.

**Cloud deck** — a 0–400 m section, `py(m) = h − 12 − m/400·(h−20)`, columns 4 px wide with
`top = base − thick·(.65 + .3·sin(x·.08·scale) + .12·sin(x·.31))` and `thick = 10 + 34·density`, each a
three-stop ramp from a `.05` wisp through `.75·density` body to a `.72·density` shadow. Then the `Cloud base`
and `Optical density` tapes.

**Morphology** — `Feature size`, `Edge detail` and `Drift speed` tapes, the sunlit/shadowed colour chips
(30 px, radius 10, hex at 11.5 px), the `FOLLOW WIND` tag, and the note.

Recorded deviations: the hero's rounded clip is faked with corner notches; the diagonal sea ramp and the column
ramps are banded, since a flat ImGui quad carries one colour; the `<input type=color>` chip is drawn as a
swatch, as there is no native colour picker in this surface.

Proof: `python3 Exhibits/Workbench/Clouds/RunNativeCloudCards.py` → **PASS 422**, three captures in
`Exhibits/Gallery/CloudNative/`.

### Mounted in the clouds inspector

`CloudsInspectorPanel.cpp` now paints the first four blocks after its own cards, in the reference's order:
satellite map, rail, duo, coverage distribution. The bind:

| Card input | Engine sheet | Mapping |
|---|---|---|
| `Coverage` | `Coverage` 0–1 | direct |
| `Density` (optical 0–1) | `Density` 0–4 × | `clamp(Density / 4, 0, 1)` |
| `Scale` | `Feature Scale` 0.2–3 × | direct; the card's own domain runs to 4 |
| `Altitude` | `Base` 100 m–ceiling | direct, display only |
| `Linked` | `Follow Wind` | direct |
| `Detail`, `Speed`, `Tint`, `Shade` | — | no counterpart; keep the panel's defaults |
| wind bearing | — | 214°, which is `clouds.js`'s own fallback when no wind node exists |

**The deck and morphology cards are deliberately not mounted.** Their domains are not what this engine stores:
the vertical section is a fixed 0–400 m window while the engine's base runs to the ceiling in the thousands,
and the colour chips have no cloud tint behind them. The native `Cloud base` / `Layer thickness` / `Cloud body`
cards already cover that ground in the engine's own units. Forcing the bind would peg both tapes and empty the
section — `PanelMounted.png` shows exactly that happening at a 1500 m base.

That capture also found a real defect: a canvas clips its own overflow and an `ImDrawList` does not, so a base
past the window painted the section's columns over the card above it. `PaintSection` and `PaintHistogram` now
push their own clip rectangles.

## The WindEditor dialog

`WindPanel.jsx:503`, `export default function WindEditor` — the full-screen modal the wind inspector raises
from *Open WindEditor · place and combine components*. It is the only authoring surface in the reference that
is not a card, and `CheckReference.mjs` guards it by name ("Pre-C041 composite WindEditor is restored and
opens"). Ported whole into `Engine/Editor/WindEditorSurface.h` (`Frontier::WindEditor`), over the evaluator,
mote field and canvas already in `WindPanelSurface.h`.

### The type was wrong everywhere, and is now right here

ImGui bakes a face so that **ascent − descent** equals the size you ask for. CSS sizes the **em box**. DM Sans
reports `hhea` ascent 992, descent −310, lineGap 0 per 1000 em, so the two differ by exactly **1.302** — a run
drawn at `AddText(Face, 12, …)` is 23% narrower than the same run at `font-size: 12px` in Chrome. Measured, not
assumed: `Pause preview` is **70.980 px** at 12 px per the font's own `hmtx` table, and ImGui returned 54.516.

`WindCards::Grind(Size) = Size * EmScale` now sits inside `Measured`, `Inked`, `Boxed`, `Stacked` and `Flowed`,
so every call site keeps passing CSS pixels and gets browser metrics back. Two consequences fall out for free:

* 1.302 is also DM Sans's `normal` line-height, so one baked line of ImGui text is exactly one CSS line box
  tall, and `Flowed` wraps and stacks like a block of copy.
* The ascent share is 0.992, so a baseline — canvas `fillText`, or an SVG `<text y>` — sits `0.992 × size`
  below the top of the box ImGui draws. `BaselineShare` was 0.792; it is now `AscentShare`.

The binding card picked the correction up through the shared kit: its note now wraps to two lines at 352 px, as
it does in the browser, and `RunNativeWindCards.py` went from 77 to **PASS 78** with the claim restated.

**The other native surfaces have not been swept yet.** `WindInstrumentSurface.h`, `CloudInstrumentSurface.h`,
the fog cards and the light panel each carry their own copies of these primitives and still draw CSS pixels at
ImGui sizes. The same three-line change applies to each, and each needs its harness re-run.

Two weights are baked, which no earlier surface needed: the dialog is DM Sans **Light 300** throughout —
`body` sets it and `h1,h2,h3,h4` keep it — except `.wind-section-head h2/h3` and `.wind-dimensions h3`, which
are **Regular 400**. `h3` is also `#cacaca` from `Editor.css`, not the dialog's `#d3d9d4`; `h2` inherits.

### The box model, worked through

Every height below is padding + border + line boxes. None of them is a number from a stylesheet on its own.

| Element | Composition | Height |
|---|---|---|
| `input` / `select` | 9 + 1 + 12 × 1.302 + 1 + 9 | **35.624** |
| `label` | 11 × 1.302 + 8 gap + input | **57.946** |
| `.wind-parts > div` | 1 + 7 + 9 × 1.302 + 6 + 12 × 1.302 + 7 + 1 | **49.342** |
| `.wind-add-buttons button` | 8 + 1 + 11 × 1.302 + 1 + 8, past `min-height: 32` | **32.322** |
| `.wind-editor-header` | 22 + 13 × 1.302 strut + 5 + 26 × 1.302 + 22 + 1 rule | **100.778** |
| `.wind-editor > footer` | 1 rule + 14 + 10 × 1.302 + 14 | **42.02** |
| `.wind-editor p` | 14 margin + 11 × 1.6 + 14 margin | **45.6** per line |
| `.wind-view-switches label` | 13 checkbox + 3 + 3 user agent margins | **19** |

The eyebrow is a 9 px `<span>` in a 13 px block, so its line box is the **strut's** 16.926 and its baseline is
12.896 down, not 11.718 and 8.928. `h1` has `margin: 5px 0 0`. `.wind-name`'s 24 px bottom margin collapses
over `.wind-section-head`'s 12, and that 12 collapses under `.wind-add-buttons`'s 14 — so the gaps down the
aside are 24 and 14, never 36 or 26. At 1600 × 950 the two view columns are **649.5** px and nothing scrolls.

### The placement map

`<svg viewBox="0 0 600 400" preserveAspectRatio="none">` inside a 649.5 × 330 border box, so the user space is
stretched **1.079 across and 0.82 down** and the two axes genuinely disagree:

* a `<circle r=13>` handle is drawn as an ellipse 14.03 × 10.66, and the selected `r=16` as 17.27 × 13.12;
* `stroke-width: 1` is 1.079 px on a vertical rule and 0.82 px on a horizontal one;
* `stroke-dasharray="4 5"` on the centre cross measures 4.32/5.40 across and 3.28/4.10 down;
* `<text font-size="11">` is baked at 11 × 0.82 and walked at 11 × 1.079, so every glyph lands where the
  browser puts it. Only the outlines themselves are not widened — recorded as a deviation.

Placement: directional components ignore X/Z and pin to `x = 30`, `z = 52 + 34 × index`; everything else maps
`(X / Width + 0.5) × 600` and `(Z / Depth + 0.5) × 400`, and its radius draws as
`(Radius / Width) × 600` by `(Radius / Depth) × 400`. `CheckWind.mjs` drags a handle to .36 / .62 and expects
−140 m and 120 m back on a 1000 m slice; the native check asserts the same inverse. A disabled component sets
`opacity` on the whole `<g>`, so the fill, the stroke, the number and the caption all drop to 0.4 together.

### What the dialog is made of

| Region | Contents | State |
|---|---|---|
| Header | eyebrow, `WindEditor`, field `select` 200 px, `+ Wind field`, pause/resume, `×` 36 px | converted |
| Component list | field name, `Components n / 64`, four add buttons, the rows, `Preview bounds · m` | converted |
| Place components | the stretched SVG placement map and its hint paragraph | converted |
| Combined vector field | `WindCanvas` from the shared kit, with the three view switches | converted |
| Selected component | heading, `Remove component`, the 4-column property grid, the per-kind paragraph | converted |
| Footer | the `.green`/`.red` 7 px dot, the evaluated/hidden note, the pending note | converted |

The property grid follows the JSX branches exactly: name, type, strength, X, Z, radius always; **bearing** for
directional and gust; **frequency** for gust alone. So a tornado or a radial shows 6 cells in two rows, a
directional 7, a gust 8. With nothing selected the card collapses to the heading *Select or add a component*.

Recorded deviations: the shell's and the map's rounded clips are faked with corner notches, since an ImGui clip
rectangle has square corners; the backdrop's `backdrop-filter: blur(8px)` is painted as the flat `#000b` over
a dark ground; the `select` chevron and the checkbox tick are drawn rather than taken from a user agent; SVG
glyph outlines are positioned on a stretched pen but not themselves widened.

Proof: `python3 Exhibits/Workbench/WindEditor/RunNativeWindEditor.py` → **PASS 38**, four 1648 × 998 captures
in `Exhibits/Gallery/WindEditorNative/`.

**Not mounted.** The dialog has nothing to open it from yet: the wind panel it belongs to is itself proved but
unmounted, so wiring the modal waits on `WeatherInspectorPanel.cpp` gaining the inspector first.

## Sweeping the cloud and wind panels to browser-sized type

`WindInstrumentSurface.h` carries its own copies of the text primitives, and `CloudInstrumentSurface.h`
aliases that whole kit as `Kit`, so the two panels share one correction: `Grind(Size) = Size * 1.302` now
sits inside `Measured`, `Boxed`, `Inked` and `Tracked`, and `BaselineShare` is `AscentShare` (0.992). Both
harnesses still pass — **PASS 429** for clouds and **PASS 240** for wind — because their claims are about the
field maths, not about text extents.

Growing the glyphs to their true size exposed three layout bugs that the undersized type had been hiding.
Each was a place where a CSS box had been approximated by a constant tuned to the smaller text.

**`.mp-stat` is a grid, not a stack.** `grid-template-columns: 1fr auto` with `align-items: end`, the icon
spanning both columns above. The number takes its own width and the label takes what is left, so
*Sun reaching datum* **wraps inside its column** instead of running under the `53`. The row gap is 2, not the
6 that had been guessed. And `.mp-duo` is itself a grid, so its two cells **stretch to the taller one**: the
slack is shared between the stat's two auto rows, which drops the shorter card's icon by half of it and
leaves the bottom-aligned label and number on the padding edge. `StatHeight` and `DuoHeight` replace the
`StatTall` constant everywhere, including the mount in `CloudsInspectorPanel.cpp`.

**`.mp-chead .l` is a flex column.** The title and the subtitle each occupy a full line box — 15 × 1.302 and
9.5 × 1.302 — not their font sizes. `HeadHeight()` was 7.4 px short per card, which is why *Coverage* and
*CONDENSATE THRESHOLD · CELL POPULATION* had started to touch.

**Notes wrap; they are not split by hand.** The driving note had been hard-coded as two string literals
broken at a point that only worked at the old scale, and it was drawn at 10.5 px when `.mpanel .mp-note`
overrides the size to 10. The hero caption had the same problem in a flex row: `.mp-cap .l` has
`min-width: 0`, so it shrinks and its sub-line wraps, and the band grows upward from the card's foot rather
than letting the text run under `force 3`. Both now go through `TrackedWrap` / `PaintTrackedFlow`, a greedy
word wrapper that measures with letter-spacing included — ImGui's own wrapping does not know about tracking.

The Beaufort meter's first row is a label beside a stepper, so the row is `StepBox` tall, not `MeterKey`
tall; the scale's marker had been riding up into *WIND SPEED*.

Still unswept: the fog cards and the light panel.

## CloudDeckPanel.jsx — the vertical section the engine range actually needs

`Experimental/ProjectZeroEditor/CloudDeckPanel.jsx` (4,845 B) is imported by `Inspectors.jsx:2` and rendered
at `:1557`, inside `<Card Title="Cloud base" Height={452}>` between the `Metric` and `{!Local && F("Base")}`.
It had never been looked at: every cloud conversion so far came from `InspectorDepot/panels/clouds.js`.

Its own header comment explains why it exists — "adapts the ported InspectorDepot cloud-deck band to the
native base/thickness range". The depot band fixes its window at 400 m, so an engine base of 1500 m simply
leaves the canvas; this one sizes the window to the deck:

| Quantity | Expression | Note |
| --- | --- | --- |
| Floor | `Local ? min(0, floor((Base − Thickness·0.25)/100)·100) : 0` | a world layer always stands on the datum |
| Window | `max(400, ceil((Base − Floor + Thickness)·1.25/500)·500)` | the 500 m step is the real floor; the stated 400 m minimum only bites on a deck of no depth on the datum |
| Y(a) | `176 − 18 − ((a − Floor)/Window)·(176 − 48)` | so the floor rule is y 158 and the head rule y 30, always |
| Depth | `(Thickness/Window)·128` | the deck's drawn thickness |
| Crown(x) | `0.65 + 0.3·sin(0.08x) + 0.12·sin(0.31x)` | the crest; min .236, max 1.042 — it tops the nominal thickness, which is why the surface clips |
| Clamp | world `100 … 12000`, local `−100000 − T/2 … 100000 − T/2` | arrow keys step 10 m, not the engine's old 100 |

Columns every 4 px are drawn 4.3 px wide — a deliberate 0.3 px overlap so there is no seam — filled with a
`createLinearGradient(0, Top, 0, Datum+4)` of three stops: `rgba(238,243,248,.05)`, `.35 → rgba(238,243,248,
.75·D)`, `1 → rgba(92,108,128,.72·D)`, where `D` is the engine's 0–4 `Density` normalised to the unit range.
Five rules, labels at `(6, y−3)`; only an altitude of **exactly** nought prints `DATUM`, so a local floor of
−200 m prints as a signed metre mark. Then the dashed `[3,3]` base line, then the title at `(6, 11)`.

Ported to `Engine/Editor/CloudDeckSurface.h` (`Frontier::CloudDeck`, `namespace Kit = Frontier::WindInstrument`
for the text primitives, so it inherits the 1.302 em scale). Proof
`Exhibits/Workbench/Clouds/NativeCloudDeck.cpp` — **PASS 54**, five captures in
`Exhibits/Gallery/CloudDeckNative/`.

**Mounted**, replacing an approximation. `CloudsInspectorPanel.cpp`'s non-local *Cloud base* card drew its own
graph: the `Cached.Side` density image over a fixed `0 … Ceiling` axis, labelled in km, dragged against
`Ceiling` and keyed in 100 m steps. None of that is in the bundle. It is now `CloudDeck::PaintDeck` at
`x 24, y Y+159, w CW−48`, with the reference's own drag (`Commit`) and ±10 m keys. The card's metric, caption
and `Base` slider already matched and were left alone; the engine's extra "0 m world datum" note is kept.

**Deviations.** The gradient is banded into 18 slices (an ImGui quad carries one colour). The canvas sets
`8px sans-serif` rather than inheriting DM Sans; the port uses the baked DM Sans at 8 px, the only face the
engine ships. `border-radius: 8` is faked with four `CornerNotch` wedges against the card's `#232323`, since
an ImGui clip rectangle has square corners. The 0.3 px column overlap reads as a faint bright seam under the
harness's ×3 supersampling, where the browser's analytic coverage makes it a faint dark one.

`CloudSection` (`LiveGraph.jsx:637`), the sibling in the *Layer thickness* card, is **not** ported: it is a
thin wrapper over the generic `Plot` component, which is shared framework rather than cloud UI.


## FracturePanel.jsx — the per-object fracture card

`Experimental/ProjectZeroEditor/FracturePanel.jsx` (5,064 B) is imported by `Inspectors.jsx:5` and rendered
at `:693` whenever `Subject.Panel === "geometry"`, after every property group. It pulls two modules out of
the sibling app `Experimental/FractureEditor/`: `FractureSpecification.js` for the recipe and its clamps, and
`FractureProjection.js` for the shaded solid. **Nothing in `Engine/` carried any of it** — this is purely
additive.

Ported to `Engine/Editor/FractureCardSurface.h` (`Frontier::Fracture`, `namespace Kit =
Frontier::WindInstrument` for text, so it inherits the 1.302 em scale). Proof
`Exhibits/Workbench/Fracture/NativeFractureCard.cpp` — **PASS 91**, five captures in
`Exhibits/Gallery/FractureNative/`.

### The recipe

`Normalize()` clamps, and two of its keys round. These are reproduced exactly:

| Key | Default | Limit |
| --- | --- | --- |
| `Energy` | 2500 | 0 … 50000 |
| `Seed` | 42 | 1 … 999999, rounded |
| `Ceiling` | 48 | 2 … 160, rounded |
| `MinimumSize` | 0.045 | 0.002 … 0.3 |
| `X` / `Y` / `Z` | 0 | ±1000 |
| `SdfResolution` | 64 | **falls back** to 64 — it is a list of 32/64/128, not a range |
| `Mode` | `dynamic` | `baked` or `dynamic` |

### The card is a block flow

Four disclosure stages, and the heights are collapsed margins rather than sums — the point the checks pin:

| State | Height |
| --- | --- |
| Disabled | 136.248 |
| Enabled, dynamic, cube | 442.59 |
| Baked, no per-piece SDF | 513.59 |
| Baked with per-piece SDF | 635.912 |

`BAKE` sits **18 px** under the diagram, not 26: the diagram's 8 px bottom margin collapses into the heading's
18 px top margin. `.fracture-sdf` has a border-top and padding-top, which *breaks* the collapse, so its first
switch keeps its full 20 px. The header is 36.248 px — `Grind(15) + 5 + Grind(9)` — because the stacked title
and eyebrow outgrow the 30 px button they sit beside.

### The diagram

`FractureGlyph()` is a genuine Voronoi diagram: nine fixed seeds, each cell the 36-gon of radius 58 clipped by
the perpendicular bisector against every other seed (Sutherland–Hodgman on a half-plane). Cells are sorted by
their first vertex's Y into painter's order — `Array.prototype.sort` is stable, so `std::stable_sort`. Each
cell is shaded from `133 − 0.36·cx − 0.35·cy`, clamped to 35…210, with every seventh facet tinted green, and
projected by `(140 + 1.05x + 0.18cx, 61 + 0.77y + 0.16cy − 0.12·√(58² − cx² − cy²))`. The nine cells tile the
perimeter to within 1e−6 of its area, which is the check that proves the port rather than a screenshot.

### Mounted

`InspectorPanel::RecordFracture` in `InspectorPanel.cpp`, called from the generic object path right after the
`RecordCard` loop and before `RecordStanding` — the same place the JSX appends it. The recipe lives in a
16-slot `FractureRecord` table keyed by instance index, since the engine has no fracture component yet and the
browser keeps these in `localStorage` under a scene ID. Hit targets are wired for the enable switch, the two
mode buttons, the SDF switch and the resolution field.

**Deviations.** The engine's geometry rows carry no primitive kind — the browser reads it off the outliner
icon — so the mount infers one from the row's name and otherwise shows the "pending" note. `<select>` is drawn
as its closed box with a chevron and cycles 32 → 64 → 128 on click. The expand button is drawn and hit-tested
but opens nothing: the fracture editor itself (`Experimental/FractureEditor/`, a separate 788 KB bundle over
`FracturePanel.js` 25,953 B, `FractureStructure.js` 22,026 B and `FracturePanel.css` 14,461 B) is **not yet
ported** and is the obvious next piece.

## Verifying against the bundle instead of the sources

The reference is `Experimental/ProjectZeroEditor/index.html` — a built, minified, self-contained file.
The `.js` and `.css` beside it are only *mirrors* of what it was built from, and reading them is not the
same thing as reading the build. Twice the question "which half ships?" has only been answerable from the
bundle, so this is now mechanical rather than a matter of care.

`Exhibits/Workbench/BundleParity.py` walks the other way round: for each ported surface it locates the
shipped code inside the bundle, un-escapes it (the depot kit is embedded as a doubly-escaped JS string;
`Editor.css` is inlined in a single `<style>`), and asserts that every literal and every numeric constant
the native header depends on is present there. **PASS 199.**

What it settles, with byte offsets into the 4,869,252-byte file:

| Port | Shipped where | Verdict |
| --- | --- | --- |
| `CloudInstrumentSurface.h` | depot kit, byte 4,378,455 | all 6 blocks, 30 literals and 17 constants match |
| `CloudDeckSurface.h` | React chunk, byte 4,598,926 | ships verbatim; 17 literals and constants match |
| `FractureCardSurface.h` | React chunk byte 4,623,565; CSS inlined | 13 literals, 8 CSS rules, 10 solid constants match |

The constants are the part worth having, because that is where an approximation hides rather than in the
strings. The cloud panel's checks pin, among others: `Math.max(.2, n)` on the feature scale before the
octaves; the octave offsets `+9/−4` and `−3/+7`; the divisor `.92 + r·.08`; the 1600 probes laid out on a
40-wide lattice at `(i%40·7, ⌊i/40⌋·5)`; the section's fixed `A−12−K/400·(A−20)` window; the deck body
`10 + 34·density`; and the crest frequency `sin(K·.08·scale)`, which is multiplied by the feature scale and
would read as plausible if it were not.

**Correction to this register.** Earlier entries, and the gallery pages, cited `InspectorDepot/panels/clouds.js`
and `CloudDeckPanel.jsx` as "the source". That phrasing was wrong even though the ports were right: the
source of truth is the bundle, and the files on disk are mirrors of it. The gallery pages now cite the
bundle and its offsets.

## EntityNotes — the note, and the folder branch that never drew one

`EntityNotes` (`Inspectors.jsx:409`) is rendered from the shared `Header()`, so **every** inspector subject
carries it — `geometry`, `light`, `post` and `group` alike, and a folder is a `group`. Two states:

| State | Shape |
| --- | --- |
| closed | `.entity-notes-add` — a 30 px dashed `#424242` button, `padding:5px 9px`, a 13 px plus, a 6 px gap, "Add notes" at 9 px, pinned `top:0;right:0` in the heading |
| open | `.entity-notes` — `padding:10`, `radius:9`, `#191919` on `#303030`; a row of **Notes** / OPTIONAL / Hide; a 76 px `#111` textarea at `radius:6` |

It opens itself from `useState(Boolean(Value))` — a subject that already has a note shows it.

Measured heights: the header row is **22 px**, the Hide button being taller than either piece of type beside
it, so the panel is `1 + 10 + 22 + 8 + 76 + 10 + 1 = 128`. The heading is 110 px and grows to 224 via
`:has(> .entity-notes)`, but the panel is absolutely positioned at `top:105px`, so it reaches 233 and
**overhangs its own heading by nine pixels**. That is the reference's behaviour, and the harness pins it
rather than tidying it away.

Ported to `Engine/Editor/EntityNotesSurface.h`. Proof `Exhibits/Workbench/Notes/NativeEntityNotes.cpp` —
**PASS 15**, four captures in `Exhibits/Gallery/EntityNotesNative/`. `BundleParity.py` now covers it too:
**PASS 235**.

### Two defects this found

1. **The native note was the wrong component.** `InspectorPanel::RecordNotes` drew a chevron disclosure in a
   rounded 18 px inset card, with a letterspaced caps header and a 64 px field. No part of that shape is in
   the bundle. Replaced with the ported component; the field stays a live `InputTextMultiline`, drawn
   transparent over the painted box so the reference's geometry is what shows.
2. **Folders had no note at all.** Of the fourteen branches in `InspectorPanel::Record`, thirteen called
   `RecordNotes`; the folder branch (`Category == Folder`, line 244) returned early without it. Fixed.

`NotesSeen_` resets with `SheetFor_` so each new selection re-derives its own disclosure.

### Folders, checked

`.folderpanel`, `.fd-eye` and `.fd-empty` **are** in the bundle's stylesheet but appear nowhere in its
JavaScript — dead CSS for a depot folder panel that is not mounted. The folder UI that does ship is the
React `group` subject (`ScenePolicy.js:33`, `ReferencePanel.jsx:34`), which uses the ordinary inspector
header. So apart from the missing note there is no separate folder panel to port.

## Base meshes — the five the editor opens with, and the primitive it derives from them

`Editor.jsx:46` seeds the outliner with one `Showcase` collection and five geometry subjects — Cube,
Sphere, Cylinder, Torus, Cone — each carrying its own `editor-<primitive>` icon. Everything downstream
reads the **icon**, never the name: `FractureSpecification.js` computes
`Primitive = Subject.Icon.replace(/^editor-/, "")`, and the fracture card's support line, its
`Signature`, its specimen art and the `?object=` link the arrow opens all hang off that one string.

`CheckerViewport.jsx` draws each subject as an analytical marker — eleven SVG path and ellipse commands
over an 80 × 76 viewBox, on a `repeating-conic-gradient` checker — inside `.preview-placement`, a
126 px minimum cell in an `auto-fill minmax(110px, 1fr)` grid.

Ported to `Engine/Editor/BaseMeshSurface.h`: the roster, `PrimitiveOfIcon`, `Partitionable`, the
transcribed marker artwork (cubics flattened to 16 segments), `PaintMarkerIn` / `PaintPlacement` /
`PaintChecker` / `Columns` / `PaintPane`. Proof `Exhibits/Workbench/BaseMesh/NativeBaseMesh.cpp` —
**PASS 66**, four captures in `Exhibits/Gallery/BaseMeshNative/`.

### The defect this found

The five primitive SVGs ship in `EngineContent/Icons/` but were **absent from `IconSymbols.inc`**, so
every geometry row in the native outliner collapsed to `IconSymbol::EditorMesh` and the fracture card
recovered its primitive by sniffing `Picked.Label` — which gets "Cube 2" wrong in one direction and an
imported "Cone bracket" wrong in the other. `EditorGlyph` mirrors `OutlinerIconCategory` 1:1 offset by
`Auto`, so widening that enumeration is invasive; the fix rides on `EditorInstance::Artwork` instead.

Identity now round-trips: `ConstructEntity` → `PlacementRecord::BaseMesh` → `ConstructArtwork` →
`Row.Artwork` → `BaseMeshPrimitive` → the fracture card. Both baked icon manifests are name-keyed, so
inserting the five enumerators in alphabetical position is safe. ⚠️ `ConstructKind` is
Cube/Sphere/Cylinder/Cone/Plane/Torus and the roster is Cube/Sphere/Cylinder/Torus/Cone — the two orders
disagree, so the crossing always goes through `ConstructArtwork` / `MarkerPrimitive`, never a cast.

## The fracture editor — the second page

`Experimental/FractureEditor/index.html` is a separate built bundle, opened by the fracture card's ↗ at
`../FractureEditor/index.html?object=<Id>`. UI only here: no solver runs behind the port.

| Band | Shape |
| --- | --- |
| `.titlebar` | 39 px `#121212` — brand, divider, `HTML AUTHORING PREVIEW` tail at 9 px / 1.3 tracking |
| `.workspace-bar` | 40 px `#202020` — the 150 px tab, breadcrumb, `Export fracture…` |
| `.workspace` | grid `240px minmax(260px, 1fr) 330px`, 1 px gaps on `#080808`; ≥ 1600 → 270 / 350; ≤ 1180 → 190 / 300; ≤ 900 drops the object pane |
| `.statusbar` | 26 px `#111` |

Panels are `#1b1b1b` under a 39 px `#222` `.pane-heading`; the viewport is `#151515` with a 40 px
toolbar, a title at 27 / 27, a centred help line and a four-column `#161616` metrics strip; cards are
`#191919` on `#ffffff0b` at **radius 22**, padding 20 / 17.

Five cards, in order: Material, Impact, Fragment quality, Bake, Geometry receipt. The material figures
are not inlined in the page — they come from `SourceDepot/Fragmentation/src/fracture/materials.ts`
(Gc / density): concrete 140 / 2 350, rock 95 / 2 700, wood 320 / 520, glass 7 / 2 500, tempered
7 / 2 500, plastic 460 / 1 050 — and are grouped as `toLocaleString` groups them.

`QualityGlyph(Ceiling, MinimumSize)` (`FractureProjection.js`) is a real Voronoi diagram, not a picture
of one: `Count = clamp(6…28, round(6 + √Ceiling × 1.6 − MinimumSize × 30))` golden-ratio sites, each cell
clipped out of a ±114 × ±36 frame and pulled toward its own centroid by `max(0.35, 1 − Gap/len)`. The
proof asserts the cells tile that frame to within 1e-6 of its area.

Ported to `Engine/Editor/FractureEditorSurface.h`. Proof
`Exhibits/Workbench/FractureEditor/NativeFractureEditor.cpp` — **PASS 121**, six captures in
`Exhibits/Gallery/FractureEditorNative/`. `BundleParity.py` now reads both built bundles and
`materials.ts`: **PASS 428**.

### Deviations

- The `#viewport` canvas has no renderer behind it, so it is stood in for by `CheckerViewport`'s own
  analytical marker for the same primitive. The overlay, toolbar, controls and metrics are as shipped.
- Naming: the module keeps the sibling `*Surface.h` vocabulary (`Paint…`, `Settings`, `Mode`). The
  invented spellings that had no reference anchor were renamed to clear `SKILL-Naming.md` —
  `Model` → `Subject`, `Kind` → `Solid` / `Shape`, `Bake` (the enumeration) → `Freshness`. `BaseMesh`
  is kept: it is the reference's own term for the five analytical primitives.

## The outliner row metadata

Every outliner row in the reference carries a small live figure under its name — `Editor.jsx`
`OutlinerMetadata(Row, Record, Rows)`, lines 225–378, drawn at 1295–1400. The native outliner already
drew a `Row.Meta` line, but only the celestial feed filled it; geometry, cameras, lights, folders, the
editor camera and post process came through blank. The contract is now in one place,
`Engine/Editor/OutlinerMetadata.h`, and the row feeds call it.

The separator is U+00B7 with a space either side, the multiplication sign U+00D7 and the degree sign
U+00B0, as the bundle ships them. Where a row has no stored value the browser falls back to the panel
field's own default, so `struct RowReading` carries those defaults rather than zeros.

| Panel | Figure |
| --- | --- |
| editor camera | `Editor · permanent` |
| `group` | `N item` / `N items` |
| `geometry` | `Position x,y,z m` |
| `camera` | `{Focal:0} mm · f/{Aperture:1}` |
| `post` | `EV {+}{Compensation:1}` |
| `atmosphere` | `Mie {:1}× · ozone {:1}` |
| `sun` | `{Intensity:1}× · {Angular diameter:2}°` |
| `flare` | `{Intensity:1}× · {Ghosts:0} ghosts` |
| `moon` | `Lunar phase {Phase×360:0}° · {Phase×100:0}%` |
| `stars` | `{Limiting magnitude:1} mag · {Brightness:1}×` |
| `wind` | `{Speed:1} m/s · {Bearing:0}°` |
| `height-fog` | `{Density:2}× · {Falloff height:0} m` |
| `aerial-fog` | `{Density:1}× · starts {Start:0} m` |
| `local-fog` | `{Density:1}× · {Coverage×100:0}%` |
| `clouds`, `local-cloud` | `{Coverage×100:0}% · {Density:1}×` |
| `precipitation` | `{Rain\|Drizzle\|Hail\|Snow\|Sleet} · {Intensity:1} mm/h` |
| `rainbow` | `{Intensity:1}× · {Minimum path:0} m` |
| `light` | `{Output:0} {cd\|lm} · {Shape} · {x,y,z}` |

A light reads its reference luminaire: candela for point and spot, lumens for everything else, and
plain `lx` off the row's own intensity when no luminaire is referenced. An LED reads
watts × efficacy × dimmer; a strip reads lumens-per-metre × length × dimmer. The shape names are the
bundle's own — `Point`, `Spot`, `IES`, `Area`, `Tube`, `LED`, `Strip`.

Numbers go through `CompactNumber`, which is `toLocaleString("en-US", { maximumFractionDigits: n })`:
thousands grouped, trailing zeros dropped, no minimum fraction digits, and halves rounded away from
zero rather than to even as `printf` does. A non-finite reading answers with an em dash.

`EditorInstance::Meta` was widened from 24 to 48 bytes; the longest figure a light can print is 41.

Proof `Exhibits/Workbench/OutlinerMetadata/NativeOutlinerMetadata.cpp` — **PASS 51**, one capture in
`Exhibits/Gallery/OutlinerMetadataNative/`.

### Deviations

- The celestial rows keep the richer engine-derived wording they already had where the engine actually
  computes a better figure than the browser can — air mass on the sun, the solved limiting magnitude on
  the stars, the fog visibility distance. Those are readings, not placeholders. Only the three
  placeholder strings (`Live`, `Study`, `+0.0 EV`) were replaced.

## The folder inspector

`RecordCollection()` in `Engine/Editor/InspectorPanel.cpp` was already a close port of
`FolderInspector.jsx` and `FolderInventory.mjs`. Six differences were found and closed.

| Was | Now |
| --- | --- |
| Composition footer without a count, and no "usage" | `{N} constructed preview markers · visibility includes ancestor folders. Counts are scene records, not render workload or memory usage.` |
| Nothing shown for an empty folder | `This folder is empty. Newly added children will appear here.` |
| No-match state had no way out | A **Clear filters** button, which clears search, scope, type, visibility and the page |
| Every entry read `Visible` or `Hidden` | A row hidden only by an ancestor reads `Hidden by ancestor` |
| Entry type came from the four-way category, depth was absolute | `CollectionTypeName(PanelOfRow(…))`, depth relative to the selected folder |
| Only seven entries of a page were drawn | The whole page slice, with the card growing to hold it |

`CollectionSequence` gained `Constructed` (entries with artwork, the browser's `Row.Preview`) and
`RowSelection::Own` (the row's own visibility flag, so inherited hiding can be told apart from it).

Proof `Exhibits/Workbench/Folder/NativePanelProof.cpp` with the new
`Exhibits/Workbench/Folder/RunNativePanelProof.py` — seven captures in `Exhibits/Gallery/FolderNative/`,
including the empty-folder and no-match states.
