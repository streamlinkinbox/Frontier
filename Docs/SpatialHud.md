# The spatial HUD tablet

`Experimental/SpatialHud/` — the Project-Zero trial panel reworked as a car fascia, with the particle
editor's light streaks behind it. WebGPU only.

```
python3 Tools/Build/ServeExperiments.py --port 8099
```

then open `/Experimental/SpatialHud/index.html`.

🔴 **Serve it with that, not with `python3 -m http.server`.** The plain handler sends no
`Cache-Control` at all, so a browser applies a heuristic and may reuse a response without
revalidating. These pages are ES modules that import each other, which means the browser will
cheerfully load a new `index.html` against an `app.js` from five minutes ago. That failure is silent
and looks like an impossible bug: the symptom here was a black canvas, an untouched readout, and a
stack trace naming line numbers that no longer existed in the file — all caused by nothing worse
than a control attribute renamed in markup the cached script had never seen.

**`no-store` alone was not enough**, and that is the part worth remembering. It governs the *next*
response; it says nothing about the copy already in the cache, which stays fresh on its own terms
and is served again on an ordinary reload. So the first load after the fix still ran the stale
module. `ServeExperiments.py` therefore **versions every module URL** by its target's modification
time — `./figures.js` is rewritten to `./figures.js?v=1791531925` in whatever file imports it, right
through the nested graph. A changed file is a different URL, so no cache can answer it, and nobody
has to remember to hard-refresh. It also corrects the MIME type for `.mjs`, which the standard
handler does not know and which makes a module script fail to load outright.

It is a development server: it rewrites what it serves, which is exactly what a production server
must never do.

This is the browser stage. Nothing here has crossed into C++ yet, by the standing rule that a UI is
designed where it can be looked at before it is ported.

## What it is

`Projects/Project-Zero/Source/InterfaceTrialSequence.cpp` already builds a 3D panel out of the
engine's spatial interface: a housing, two buttons, a toggle, a progress bar, an arc meter with a
spring needle and a two-digit readout, at 0.36 × 0.22 m — a panel its own header calls "roughly a
large tablet". This is the same vocabulary composed as a fascia screen instead, at 0.46 × 0.27 m, in
the three bands a car cluster actually uses:

| band | what is in it |
|---|---|
| left | boost dial — tick ring, lit arc, redline band, spring needle |
| middle | speed, three seven-segment cells, `KM/H` under it |
| right | `SPORT` toggle, `REGEN` slider, a two-cell percentage |
| strips | title above, four breathing telltales below |

Behind all of it, filling the face, the streak field.

Still one `draw(4, instances)`. Twenty-odd figures, no vertex buffer, no texture, nothing
re-tessellated when a value moves.

## The browser shader is generated, not transcribed

`Tools/Build/GenerateHudShader.py` converts `Engine/Shaders/InterfaceSignedDistance.slang` into
`Experimental/SpatialHud/js/sdf.generated.js`. An arc in the browser *is* the engine's arc, because
it is the engine's text with its types rewritten — there is no second authority to drift from.

The converter is narrow on purpose and fails loudly rather than emitting plausible WGSL. Four things
GLSL permits and WGSL does not, each of which it had to learn:

- the ternary operator → `select(falseValue, trueValue, condition)`, scanned rather than
  regex-matched. The first attempt turned
  `(Offset - Span) < (kInterfaceTau - Offset) ? Span : 0.0` into
  `select(0.0, Span, kInterfaceTau - Offset)` — valid WGSL, wrong arc, and nothing downstream could
  have caught it. The three conversions this file needs are pinned as a self-test.
- single-statement `if` bodies, on the same line and on the next one → braces.
- `atan(y, x)` → `atan2`.
- GLSL declarations → `var`, and a function-scope `const` → `let`.

`--check` fails if the written file is stale; CI runs it.

## The backdrop ladder

The backdrop has three rungs, switchable in the page, because one answer cannot serve a panel you are
pressing your nose against and a panel reflected in a windscreen forty metres away.

| rung | what it is | for |
|---|---|---|
| **Live** | the particle editor's real 3D Bézier fibres, as geometry | the panel being looked at |
| **Field** | an analytic field evaluated in the panel's plane | distant panels, and reflections |
| **Average** | one colour — the existing `Low` tier | far reflections |

### Live — the real fibres, and why it is not a render target

`js/fibres.js` ports `fbHash`, `fbUnit`, `fbBezier`, `fbPulse`, `fbRamp`, `fbColourAt` and both the
fibre and spark stages out of `Experimental/ParticleEditor/js/shaders.js`, keeping the editor's own
uniform slot numbering so the two can be read side by side. Not ported: ribbon and trail. A trail
needs the arc-length path table as a storage buffer and a ribbon is a sheet rather than a volume, so
the packer **refuses** any shape but `streak` instead of quietly falling back to it.

The obvious way to put a 3D world behind a UI panel is to render it offscreen and sample the texture.
That is a sampler binding, a texture lifetime, a second pass, and a result a reflection ray cannot
cheaply ask about — the objection `InterfacePanelSample.slang` already makes.

But the engine has the mechanism a portal actually needs: the ⑥ **shader clip**, "a rounded rectangle
in the figure's own plane ... works under any transform, unlike a scissor rectangle". So the fibres
are drawn **in the same render pass**, in the panel's own local space, clipped by the panel's rounded
rectangle in the fragment shader. Own pipeline, own vertex program, own geometry — a custom raster in
every way that matters — and no offscreen target.

An offscreen target earns its cost for exactly three things, none of which is "3D": **bloom**, a
**camera of its own**, or a **resolution and refresh rate decoupled** from the panel's. Bloom is the
real one, and part of why the editor's own streaks look as good as they do. Add it when it is wanted.

Two things are new, because a fibre in a panel is not a fibre in a scene. The curve is built in
**panel-local metres** and carried to the world by the panel's own transform rows, so moving the
tablet moves the light inside it. And depth is handled at both ends: a strand dims with distance
behind the glass (the cue that makes a volume read as a volume), and dims again over a 15 mm band as
it approaches the surface, because a strand must not cross the glass and hang in the room — and a
hard cut at the plane would show as a bright edge the moment the tablet is tilted.

🔴 **The hull bound caught a real bug.** A cubic Bézier lies in the convex hull of its control points,
so the volume can be bounded from the preset's numbers alone — no hash, no sampling, and no second
copy of `fbBezier` to drift from the shader's. At the preset's own `spread` 1.2 and `amplitude` 2.4
that hull reached about 0.14 m perpendicular to the axis, which through the curve-to-panel mapping put
strands **ten centimetres in front of the glass** — light glowing in the room outside the tablet,
where the lateral clip could not see it. They are 0.9 and 1.3 here, and `CheckHud` fails if the volume
ever escapes forward again.

The interface is still one `draw(4, instances)`; the world is inserted between its two halves —
housing, face and field rung, then the fibres and sparks, then every control.

### Field — the analytic rung

A new category, ordinal **7**, after every category the engine already ships, hand-written in
`js/streaks.js`.

It is **flat by construction**: every strand sits at the same depth, so it slides with the surface
instead of swimming behind it, and the eye reads that as paint rather than as light in a volume. That
is not a tuning problem and no parameter work fixes it.

It is kept anyway, because it is the only form of this backdrop that **can be answered at a hit
point** — a closed-form function of a plane coordinate, which geometry is not. A reflection of the
tablet, or a tablet at forty metres, gets this rung.

Each strand is an explicit curve `y = f(x)` of two summed harmonics, so the distance to it is the
vertical gap corrected by the slope — about twenty instructions, and **a strand cannot double back on
itself**. Everything that makes the preset read as light streaks is kept, because none of it needed
the Bézier: the running head, the exponential wake, the hard leading edge, the spark, per-strand phase
and pace from the editor's own hash (`fbHash`, bit for bit), and one additive tone map.

Eight parameters ride in two vectors rather than in `ScalarAlpha`/`ScalarBeta`. Six do not fit in two
floats, and a parameter smuggled into another one's slot is how a slot stops meaning anything. **The
native 112-byte instance slot will have to grow the same way** — the first real cost of this category,
and it should be decided deliberately rather than discovered.

### Either way, it is light

The backdrop is an **Illuminant**, not an Overlay: it is light inside the glass, it feeds the panel's
scene luminaire and pools on whatever the tablet stands on. A backdrop that lit nothing would read as
a sticker the moment the room went dark. Drop the ambient slider to see the bezel sink while the
streaks, the arc and the lit knob stay.

## 🔴 The clip belongs at the aperture, not at the strand

The first version of the fibre clip asked whether the strand's own panel `xy` fell inside the face
rectangle. That is a **flat** test — correct for a decal, wrong for a window — and it looked
perfectly fine in every screenshot anyone had taken, because head-on the two agree.

Orbit past about sixty degrees and it falls apart: a volume sitting 80–140 mm **behind** the glass
projects **outside** the panel's screen outline at a steep angle. A strand comfortably inside the
rectangle in panel space ends up well outside the tablet on screen, so the light sprayed off the
side and hung in the room.

A window decides what can be seen through it **at the window**. `fbAperture` carries the sightline
from the eye to the strand forward to the glass plane (`panel z = 0`) and evaluates the rounded
rectangle there:

```
cross     = (0 - eye.z) / (panel.z - eye.z)
aperture  = eye.xy + (panel.xy - eye.xy) * cross
```

That is a portal, and it costs one ray-plane intersection in panel space — still no stencil, no
scissor, no render target, and it still holds under any transform. The eye reaches panel space
through the **transpose** of the panel rows, which is free because the rotation is orthonormal.

The back of the tablet is opaque as well (`fbEyePanel().z <= 0` discards). Without it the volume is
simply visible from behind, which is the same mistake wearing different clothes.

`Stills/Grazing.png` is the angle that exposed it. `CheckHud` pins the clip **site**, not just its
presence, so nobody quietly puts it back on the strand.

## 🔴 The volume behind a panel is a slab, not a cylinder

`fbBezier` scatters its root and reach on a **disc** perpendicular to the axis, so one `spread`
sets the extent in both perpendicular directions at once. In a scene that is right. Behind a
tablet it is not: the volume has ~120 mm of height to fill and perhaps 30 mm of depth before a
strand is through the glass.

With one number the two fight, and that fight is what kept the Live rung to a narrow band across
the middle while the Field rung covered the whole face. Raising `spread` to fill the height pushed
strands out the front — which is precisely what the hull bound caught the first time, so the
response then was to lower `spread`, and the band was the price.

The cross-section is now shaped where the curve meets the **panel** rather than in the curve:

```
fbPanel:  panel = org + vec3(along, lift * liftGain, lateral * depthGain) * scale
          liftGain 1.70      depthGain 0.55
```

`fbBezier` stays the editor's, verbatim. `fbPanel` was already the function that exists *because a
fibre in a panel is not a fibre in a scene*, and this is the same thought. With depth squashed,
`spread` could go back up (0.9 → 1.1) and `amplitude` with it (1.3 → 1.5), and the volume now
fills the face while sitting further behind the glass than it did before.

`CheckHud` holds the two gains apart (`liftGain > 2 × depthGain`) and carries them through the
hull bound, so the bound still bounds the thing that is actually drawn.

## 🔴 The tablet is an object, not a quad

The panel used to be a rectangle floating in a clear colour. Everything on it was right and the
whole thing still read as a picture of a tablet rather than a tablet, because nothing in the image
said there was a thing there: no thickness, no edge, no surface for the light to find.

`js/chassis.js` raymarches a rounded box — 464 × 274 × 21 mm, 21 mm corner radius — in panel space,
in two extra passes around the existing ones:

| # | target | pass | what |
|---|---|---|---|
| 1 | **display** 1440×845 rgba16f | interface, first half → fibres + sparks → interface, second half | the panel's own camera: an off-axis frustum whose near plane is the panel |
| 2 | **glow** 360×212 ×2 | `fsBloomCut` → `fsBloomAcross` → `fsBloomDown` | bright pass and a separable Gaussian |
| 3 | **window** | `fsRoom` (opaque) → `fsGlass` (over) | the tablet in the room, sampling the display |

`rgba16float`, not the swapchain's 8 bits: the interface is additive in places and the bright pass
has to tell a lit element from a blown one. Both need values above 1.0 to survive the trip.

### 🔴 It has to stand somewhere

The glass work above was answering the wrong question. The only thing producing a sense of depth
was the **particle volume behind the screen behaving like parallax** — the device itself was still
a slab floating in a void, and no amount of material work on a slab makes it an object. A slab
with no shadow is a picture of a slab.

So the tablet now **leans back 9.7° in a dock, standing on a desk, with a marched contact shadow**:

- **The march moved into world space.** It used to step through panel space, which is fine while
  the only thing in the scene is the panel. A desk is flat in the *world* and the panel is leaning,
  so a desk written in panel coordinates would be a slope. Marching the world lets each object be
  written in whatever frame suits it — the body in panel space, the desk and dock in world space —
  and a rigid rotation preserves distance, so the marcher doesn't care.
- **The contact shadow** is the standard ratio-of-distance-to-travel march. That ratio *is* the
  penumbra, so the shadow is soft where the tablet is far from the desk and tightens to a hard line
  where it meets it, for free.
- **The screen pools its own light on the desk**, shadowed by the body so it doesn't leak out
  behind the device. This is the other half of "it's really there".
- **The lean lives in the scene, not the layout.** `layout.js` stands the panel bolt upright, which
  is the right *authoring* frame — every figure is placed against it and every check is pinned to
  it. The lean is applied to the structure at scene level and the layout never learns about it.
- **The desk fades into the room over 1.5 m.** A plane is infinite and a marcher is not: left
  alone, a near-grazing ray creeps until it hits the step limit and draws a hard horizon with black
  above it. In the first render that read as two slabs floating behind the tablet. The fade fixes
  the look *and* the cost — past it there is nothing left to march toward.
- **The bezel went from 2 mm to 12 mm.** 2 mm is not a bezel, it is a tolerance, and it left the
  camera and speaker underneath the glass quad where they could never be seen. A device is bigger
  than its display.

There is still no contact *reflection* of the tablet in the desk, deliberately: a floor wants the panel leaned back, and the
lean would move the housing's quarter turn about X that the whole layout and all its checks are
built on.

### 🔴 Additive glass was never glass

The first version of the chassis composited the glass over the finished interface with an additive
blend. That is a decal, and it was wrong in a way no screenshot would have shown: **additive light
can only add.** It could paint a reflection on the picture, but it could never bend, displace, dim
or tint what was underneath it — and bending what is underneath is the entire physical content of
the word *glass*.

So the panel renders into a texture of its own and the tablet **samples** it, which is how a device
screen is done in an engine. `js/display.js` owns the target and the pass that reads it:

| | |
|---|---|
| **refraction** | the emitting plane sits 2.4 mm behind the outer face, so the image slides under its own glass |
| **bloom** | emissive elements bleed, added *inside* the glass so Fresnel attenuates it too |
| **a real split** | `transmitted × (1 − F) + reflected × F` — energy *leaves* the interface as the room takes over |
| **its own pixels** | 1440 × 845, fixed, independent of the window |

The split is the one you feel. Additively, the interface stayed at full brightness at a grazing
angle with a highlight added on top; now it correctly fades as the room wins.

#### The projection is off-axis, and it has to be

The obvious way to fill a screen texture is to render the interface orthographically, face on. That
throws away the thing this whole experiment is about: the fibre volume is a hundred-odd millimetres
deep, and an orthographic bake flattens it to a sticker the moment the camera moves.

So it uses the projection a portal or a mirror uses — apex at the **real eye**, near plane the
**panel rectangle** rather than a symmetric window about the view axis. Then texture UV and panel
coordinate are the same quantity by construction, depth behind the glass projects exactly as it did
when it was drawn straight to the screen, and sampling at the sightline's crossing reproduces the
old image to the pixel. Which is what makes any offset from that point *real refraction measured
against a correct baseline* rather than a smear that happens to look busy.

`CheckHud` pushes points through the matrix and compares them against the ray-plane crossing to
float precision, including points behind the glass. If that mapping were even slightly off, every
pixel would be displaced by an amount indistinguishable from the refraction the same shader applies
on purpose, and one of the two would be silently wrong while the picture still looked plausible.

#### 🔴 Measure the effect before you ship it

Refraction, at the display's own resolution:

| incidence | 15° | 30° | 45° | 60° | 75° |
|---|---|---|---|---|---|
| shift (display px) | −0.71 | −1.71 | −3.55 | −7.79 | −21.84 |

Visible, and visibly moving. The first version also sampled **red, green and blue separately**,
since the index of refraction is wavelength dependent and colour fringing at the rim of a thick
cover is a real thing. Then it got measured: across crown glass's actual spread (n = 1.505 … 1.529)
the red and blue sample points differ by **0.12 of a pixel** at 60°, and 0.17 at 75°. Below the
grid. It cannot appear. That was two extra texture reads per screen pixel per frame to produce
nothing resolvable, so it is gone — you would need about two centimetres of cover glass before it
crossed one pixel. The check keeps the measurement so the decision can be revisited if the glass
ever gets thicker.

### 🔴 A light is a card, and the glass does not reflect the key

Two findings, and they cost a day between them if you meet them one at a time.

**A point lobe cannot appear in a mirror.** The first version lit everything with
`pow(dot(direction, key), n)`. On the machined body that is fine. On the screen it produces
*nothing*: a flat mirror reflects exactly one direction per pixel, so a lobe tight enough to read
as sharp is tight enough that the eye never lands inside it, and the result is a black sheet with
a bright rim. What a photograph of a device actually shows is a long soft **strip**, and that
strip is the shape of the *light*, not of the material. So `RoomCard` takes a source as a
rectangle measured in its own tangent plane — `wide`, `tall` — and roughness only decides how much
to swell it. Material sharpness and light shape stopped being the same number.

**And the card the glass shows is not the key.** Work out where the reflection goes rather than
guessing. This panel stands upright, so for any eye above its centre the mirror direction points
*down and forward* — a vertical screen reflects what is in front of it, never the ceiling. A key
placed where a key belongs, high and over the left shoulder, reflects to somewhere below the
floor. So there are two sources: `kKeyDirection` high, narrow, which rakes the chamfer along the
top edge and gives the slab its form; and `kFrontCard` **low**, broad, which in a real room is the
lit table the device is standing on, and which is the only thing the glass shows.

That split buys the behaviour as well as the look: head-on the card sits near the edge of the lobe
and the screen is nearly clear, and as the tablet turns the reflection blooms across it. Tilt a
real tablet under a desk lamp and that is exactly what happens.

`CheckHud` pins the two directions apart, because collapsing them back into one light is the
obvious simplification and it returns the black sheet.

## 🔴 A finding about the native sort key

`ComposeInterfaceSortKey` puts transparency in the **top** bit, so every transparent figure submits
after every opaque one whatever its ordering rank. That is correct when a depth buffer is doing the
occlusion. It is wrong the moment a large *transparent* figure has to sit *under* opaque ones — which
is exactly what the streak field is: rank 2, opacity 0.85, beneath an opaque needle at rank 7 and
opaque digits at rank 9. Under the engine's key the backdrop submits last and paints over its own
instrument.

The browser submits by ordering rank instead (every figure in this composition is coplanar to within
three millimetres, so a depth test would resolve nothing and z-fight instead). `CheckHud.mjs` pins
both halves: that the engine's key really does order them wrongly, and that rank ordering does not.

**The native side has to answer this when the category crosses over:** either the backdrop becomes
opaque, or transparency stops being the most significant bit of the key.

## Checks

```
python3 Tools/Build/GenerateHudShader.py --check     # the WGSL port is current
node Experimental/SpatialHud/CheckHud.mjs            # PASS 176
```

Both run in `frontier-build.yml`. `CheckHud` covers the composition arithmetic (a quarter turn about
X really does send panel-up to world-up), the two-sweep resolver and its cycle refusal, the layout's
structural claims, that no figure reaches past the bezel, the sort finding, the value writes, and the
packed buffer.

Two of its checks are worth naming:

- **Every character on the panel has a glyph.** The stroke font draws *nothing* for a character it
  does not know, by design — "a missing label is obvious, a wrong one is not". That makes a typo
  invisible in the source and invisible in review, so the supported set is read out of the shader
  text and the labels are checked against it rather than against a list maintained by hand.
- **A blank leading digit, not a zero.** `007 KM/H` is a clock, not a speedometer. The engine gives
  blank its own code (10); the check holds `7`, `42`, `180`, an over-range value and a negative one.

## Pixels without a GPU

```
node Experimental/SpatialHud/RenderStill.mjs            # three stills into Experimental/SpatialHud/Stills/
node Experimental/SpatialHud/RenderStill.mjs --check    # small, fast, fails if the panel is blank
```

There is no headless WebGPU in this environment, so for a while nothing could say what the panel
*looked* like — `CheckHud` is careful to check numbers and not pixels, and the page was the only
proof of appearance. That is a bad place for a user interface to live, because the one failure that
matters most to a reader (a black rectangle) was the one failure no check could see.

`RenderStill.mjs` closes it. It is **not** a mock-up: it imports `constructHudLayout`, `resolve` and
`pack` from the same modules the host uses, reads the identical forty-float figure slots in the
identical submission order, evaluates the same signed distance functions, builds the fibre uniform
with the same `packFibre`, and writes a PNG with no dependencies. The glyph outlines are converted
out of `InterfaceSignedDistance.slang` **at run time** and pinned by a self-check, so the font still
has exactly one authority.

Where it differs from the GPU, stated plainly: figures are resolved by ray-plane intersection per
pixel rather than by rasterising a strip; `fwidth` is a finite difference against the neighbouring
pixel's ray; fibres take the minimum perpendicular distance to the strand polyline, which is the
same one-fragment-per-pixel-per-strand the expanded quad produces; and there is a 2× supersample in
place of MSAA. No gamma is applied on the way out, because the canvas format is `bgra8unorm` and the
browser shows those numbers unconverted — encoding here would make the still brighter than the panel
is, which is the one lie this tool must not tell.

`--check` runs in `frontier-build.yml`. It asserts the mean luminance clears a floor, so **a
composition that renders to black now fails the build** rather than waiting for someone to open the
page.

What `CheckHud` does **not** claim is that it looks right — only `RenderStill` shows that, and only
to a reader. Neither is a substitute for the page: the stills are one instant with the springs at
rest, and they cannot show the envelope, the orbit, or the frame rate.

## When the page is blank

Every path into `start()` ends somewhere visible now — a thrown error, a rejected promise, or a throw
inside the animation callback all print into the red box over the canvas instead of leaving a black
rectangle. The readout reports on the **first** frame rather than after half a second, so a page that
renders once and then dies still says what it managed. A control missing from the markup warns and is
skipped rather than taking the whole panel down.

If the box is empty and the readout still says `starting`, the script never ran at all: check the
browser console for a module that failed to load.

## Still open

Three things the first CPU stills exposed, none of which any numeric check could have caught:

- ~~**The seven-segment bars do not meet.**~~ **Withdrawn — this was a misreading of a small
  image.** Measured on a zoomed still, the capsule tips are about 0.4 mm apart on the diagonal: the
  corners are *mitred*, which is what a seven-segment face is supposed to look like, and ` 90`
  reads as ` 90`. The horizontal arms *are* about a quarter shorter than the gap between the
  verticals, which makes the digits look slightly pinched at small sizes, but that is taste and not
  a defect. **Render it large before calling it broken.**
- ~~**`BOOST` sits under the needle.**~~ Fixed. It was below the hub, which is exactly where the
  needle sweeps: `kArcStart`/`kArcSweep` open the dial at the **top** (150° round through the
  bottom to 30°), so the lower half is the moving half. The label is now in the opening, the one
  place on a dial where nothing moves.
- ~~**The Live rung covers a band, not the face.**~~ Fixed by the slab, below.

- Nothing is ported to C++ yet. The order would be: the fibre stages as a second pipeline beside the
  interface raster (they are already the editor's own code, which the engine does not yet have at
  all), the field rung into `InterfaceSignedDistance.slang`, the slot growth for its eight
  parameters, the sort-key decision above, then the composition into a new project-side sequence
  beside `InterfaceTrialSequence`.
- **Bloom.** The one thing that would justify an offscreen target. The Live rung is additive glow in
  the main pass, which gets most of the way and not all of it.
- **Which rung, chosen by what.** The ladder exists but nothing selects between its rungs
  automatically — the page has three buttons. Native, this wants the same treatment as the gas
  quality ladder: a distance and a budget, not a switch.
- **The panel is not interactive.** `InterfacePointerProjection` already exists natively and the
  trial panel already handles a pointer contact; the browser page drives values from sliders and a
  scripted cycle instead. Pressing the toggle and dragging the slider *on the panel* is the next
  thing it needs.
- The telltales are decoration — they breathe on a timer and mean nothing.
- The redline band is a second arc rotated to start where the warning does. That works and costs
  nothing, but a dial that wanted several bands would want them as one figure.
- Glyph labels are per-character figures. Twenty-six of the panel's figures are letters, which is
  fine at this size and would not be at paragraph length — the stroke font is for labels and units,
  as its own header says.

## FRONTIER OS — the home screen

The device is an OS, not a dashboard. `js/shell.js` is the home screen: a status strip, a clock, a
search field, a wall of eight app tiles and a dock. `js/layout.js` — the instrument composition the
tablet started life as — is now one app among those eight, the one the DRIVE tile stands for. The
**Screen** control in the rail switches between them; both structures are built once at start-up and
the frame loop picks, so a switch costs nothing.

The reference is `streamlinkinbox/Frontier`, branch `arena/01a06c6c-frontier`, `app/index.html` — a
browser mock of this same screen. That mock is portrait CSS on a 520 px column; a tablet face is
0.444 x 0.254 m, so the column became a left-hand band and the app wall took the room that freed up.
`app/lobby.html` and `app/wallet.html` on that branch, and `app/chat.html` on `arena/837a2406-frontier`,
are the apps behind the LOBBY, WALLET and CHAT tiles — they are not ported yet.

Every mark on every tile is drawn with primitives the engine already ships. The settings cog is a
`TickRing` — the same category that draws the gauge's ticks — and the DRIVE icon is a real `Arc` and
a real `Needle`, which is to say the dashboard at 4 mm rather than a picture of it.

### 🔴 Two colour bugs, both found by rendering and measuring

**A pixel on a screen is emitted, not reflected.** The chrome was first built at `emissiveWeight: 0`
— "albedo, lit by the room" — copying the dashboard, where the face really is a card the room shines
on. `InterfaceRaster.frag` computes `mix(Received, Emitted, EmissiveWeight)`, so a plate at weight 0
inside the display target shows only the room bouncing off it, and there is almost no room in there.
The dock bar, the search pill and the link strip all rendered invisible. A horizontal scan across the
dock read **2.47 … 3.26 against a bare face of 2.00**, out of 255. One part in 255 is not a panel.

**And the palette is in display values, not linear.** The fix above barely helped: 3.9 against 3.0.
The swap chain is `bgra8unorm`, *not* `bgra8unorm-srgb`, so a figure's tint reaches the screen
unconverted — and `RenderStill.resolve()` deliberately matches that rather than flattering it.
Converting the reference's `#202020` sRGB → linear put the dock panel at 0.0145. Correct maths for a
renderer that encodes on the way out; wrong renderer. The hexes go in as fractions, directly.

Both are pinned in `CheckHud.mjs`, and both were verified by reintroducing the bug and watching the
check fail.

### 🔴 A run of glyphs that gets rewritten needs one figure per character

`text()` normally makes no figure at all for a space. That is right for a static label and wrong for
anything rewritten later: the clock's colon blinks, so "09:41" becomes "09 41", and a run without a
figure for the space would have written the minutes one glyph early and blanked the last digit. The
date is worse — month names differ in length. `text(..., { keepSpaces: true })` emits a figure per
character (the stroke font returns `1e9` for code 32, so it still draws nothing) and `respell()`
writes positionally. Three checks cover it.

### Serving it

```
python3 Tools/Build/ServeExperiments.py --port 8099 --bind 0.0.0.0
```

`/` redirects to `/Experimental/SpatialHud/index.html`. That redirect exists because the repository
root is a directory listing of forty folders with no hint that an experiment is in any of them — a
preview opens at `/`, so `/` has to be the thing you came to look at. Pass `--landing ''` for the
ordinary listing.

🔴 **If the page is blank or stale, check the server is still up before you debug the code.** It has
died silently once. `app.js` can be booted headlessly — `node CheckHost.mjs` — and the number of
instances in the display pass says which composition is live: **190 figures is the home screen, 69
is the dashboard.** That distinguishes "the code is wrong" from "you are looking at a dead port" in
one command, and the second was the answer the one time it came up.

### 🔴 The near plane cannot be the panel, even though the window is

The off-axis projection's *window* is the panel rectangle — that is the point of it, and it is what
makes a texture coordinate equal a panel coordinate. Putting the near *plane* there too clips away
everything in front of it, and **the entire interface is in front of it**: the layout stacks figures
toward the viewer a millimetre per layer, reaching 4.4 mm. The GPU discarded every one of them. All
that survived were the fibres, which live *behind* the glass at negative z — so the device showed a
black screen with the wallpaper still moving on it.

`DisplayClip` now pulls the plane `Approach` (20 mm) toward the eye and shrinks the window by the
same ratio. Because the window and the plane scale together, `2n/(r-l)` and `(r+l)/(r-l)` are both
unchanged: every sightline still crosses the glass exactly where it did, the corners still land on
±1, and only the depth range moves. The existing corner, parallax and depth-behind-glass pins all
still pass, which is the evidence that the parallax was not disturbed.

**🔴 Why it survived so long: `RenderStill.mjs` had no near plane.** My own rasteriser drew the whole
interface while the hardware was throwing it away, so the stills looked right and the device was
black — for several commits, on *both* screens. **A renderer more forgiving than the hardware is not
a proof.** `DisplayCamera.project()` now clips at `Approach` too, and with the bug reintroduced the
still's mean luminance drops 0.0340 → 0.0232, which is the still going dark exactly as the device
did.

`CheckHud` now pushes **every figure of both compositions** through the real matrix the host uploads,
at four camera angles, and demands `w > 0` and `0 ≤ ndc.z ≤ 1`. That is the check whose absence cost
a whole screen.
