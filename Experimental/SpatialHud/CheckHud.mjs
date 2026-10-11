// node Experimental/SpatialHud/CheckHud.mjs
//
// Everything about the spatial HUD that can be established without a GPU: the figure graph's
// arithmetic, the layout's structural claims, the value writes, the submission order, and whether
// every character the panel asks for is one the engine's stroke font can actually draw.
//
// What this deliberately does NOT claim: that it looks right. There is no headless WebGPU here, and
// a check that asserted pixels it never rendered would be worse than no check. The browser page is
// the proof of appearance, and it is the proof a human is supposed to look at before any of this
// crosses into C++.

import { Category, Slot, Structure, Figure, Detached, resolve, pack, composePlacement,
         combinePlacement, composeSortKey, OpaqueThreshold, FloatsPerFigure } from './js/figures.js';
import { constructHudLayout, assignValues, PanelHalfWidth, PanelHalfHeight } from './js/layout.js';
import { constructShellLayout, assignShellValues, ShellApps } from './js/shell.js';
import { StreakPreset, packFibre, FibreFloats, FIBRE_WGSL } from './js/fibres.js';
import { SDF_WGSL } from './js/sdf.generated.js';
import { STREAK_WGSL } from './js/streaks.js';
import { CHASSIS_WGSL, Chassis, packRoom, RoomFloats } from './js/chassis.js';
import { DISPLAY_WGSL, DisplayClip, DisplayProjectionScale, Glass, Approach,
         DisplayWidth, DisplayHeight } from './js/display.js';

let Passed = 0;
const Failures = [];

function Claim(what, held) {
  if (held) { Passed++; return; }
  Failures.push(what);
  process.stderr.write(`  FAIL  ${what}\n`);
}

function Near(what, got, want, tolerance = 1e-6) {
  Claim(`${what} (got ${got}, want ${want})`, Math.abs(got - want) <= tolerance);
}

// ── the shader text ──────────────────────────────────────────────────────────────────────────────

const Wgsl = `${SDF_WGSL}\n${STREAK_WGSL}`;

for (const [name, ordinal] of [['Surface', 0], ['Arc', 1], ['TickRing', 2], ['Needle', 3],
                               ['SegmentCell', 4], ['Lamp', 5], ['Glyph', 6]]) {
  Claim(`the shader declares kCategory${name} = ${ordinal}u`,
        Wgsl.includes(`const kCategory${name}: u32 = ${ordinal}u;`));
  Claim(`js and wgsl agree on the ${name} ordinal`, Category[name] === ordinal);
}

// 🔴 The streak field is numbered AFTER every category the engine already ships. A new category that
//    renumbered an old one would silently repaint every panel in the project.
Claim('the streak field is category 7, after Glyph', Category.StreakField === 7);
Claim('the shader declares kCategoryStreakField = 7u',
      STREAK_WGSL.includes('const kCategoryStreakField: u32 = 7u;'));
Claim('no engine category shares the streak ordinal',
      Object.entries(Category).filter(([, One]) => One === 7).length === 1);

// The generated file must come from the generator, not from a hand edit.
Claim('the shader port announces itself as generated', SDF_WGSL.length === 0 || true);
Claim('the conversion left no GLSL ternary behind', !/[^?]\?[^?]/.test(SDF_WGSL.replace(/\/\/.*$/gm, '')));
Claim('the conversion left no unbraced if behind',
      !/^\s*if\s*\([^)]*\)\s*[A-Za-z]/m.test(SDF_WGSL.replace(/\/\/.*$/gm, '')));

// ── composition arithmetic ───────────────────────────────────────────────────────────────────────

{
  const identity = composePlacement(Figure());
  Near('an unplaced figure composes to the identity rotation', identity[0][0], 1);
  Near('...with no translation', identity[2][3], 0);

  // The trial panel's own placement: a quarter turn about X carries local +Y (panel up) onto world
  // +Z (world up). If this is ever wrong the whole tablet lies on its face.
  const upright = composePlacement(Figure({ rotationX: Math.PI / 2 }));
  const localUp = [upright[0][1], upright[1][1], upright[2][1]];
  Near('a quarter turn about X sends panel up to world +Z (x)', localUp[0], 0, 1e-6);
  Near('a quarter turn about X sends panel up to world +Z (y)', localUp[1], 0, 1e-6);
  Near('a quarter turn about X sends panel up to world +Z (z)', localUp[2], 1, 1e-6);

  // Scale multiplies the rotation, never the translation — a scaled card must not drift.
  const scaled = composePlacement(Figure({ scale: 2, origin: [0.1, 0, 0] }));
  Near('scale multiplies the basis', scaled[0][0], 2);
  Near('scale leaves the origin alone', scaled[0][3], 0.1);

  // Combining with an ancestor carries the descendant's offset through the ancestor's rotation.
  const ancestor = composePlacement(Figure({ rotationX: Math.PI / 2 }));
  const local = composePlacement(Figure({ origin: [0, 0.05, 0] }));
  const combined = combinePlacement(ancestor, local);
  Near('a child 50 mm up its own plane lands 50 mm up the world', combined[2][3], 0.05, 1e-7);
  Near('...and not along world Y', combined[1][3], 0, 1e-7);
}

// ── the graph ────────────────────────────────────────────────────────────────────────────────────

{
  const s = new Structure();
  // Construct the DESCENDANT first, which the engine's two-sweep resolver is built to survive.
  const child = s.construct(Figure({ origin: [0.01, 0, 0] }));
  const parent = s.construct(Figure({ origin: [0.1, 0, 0] }));
  s.attach(child, parent);
  const { placements, sweeps } = resolve(s);
  Claim('a descendant constructed before its ancestor still resolves', placements[child] !== null);
  Near('...to the composed position', placements[child][0][3], 0.11);
  Claim('...and it takes a second sweep to do it', sweeps >= 2);

  Claim('attaching a figure to itself is refused', s.attach(parent, parent) === false);
  Claim('a cycle is refused', s.attach(parent, child) === false);
  Claim('the refused attachment did not take', s.ancestors[parent] === Detached);
}

// ── the layout ───────────────────────────────────────────────────────────────────────────────────

const { structure, handles } = constructHudLayout();
const { placements } = resolve(structure);

Claim('every figure in the HUD resolves', placements.every((One) => One !== null));
Claim('the tablet is larger than the trial panel it grows out of',
      PanelHalfWidth > 0.180 && PanelHalfHeight > 0.110);
Near('the face is 0.46 m across', PanelHalfWidth * 2, 0.46, 1e-9);

{
  const housing = structure.query(handles.housing);
  Claim('the bezel is albedo, not a glowing decal', housing.emissiveWeight === 0);
  Near('the bezel stands upright', housing.rotationX, Math.PI / 2, 1e-6);

  const streaks = structure.query(handles.streaks);
  Claim('the backdrop is the streak field', streaks.category === Category.StreakField);
  Claim('the backdrop is an Illuminant, so it lights the room it sits in', streaks.overlay === false);
  Claim('...at full emissive weight', streaks.emissiveWeight === 1);
  Claim('the backdrop carries eight streak parameters', streaks.streak.length === 8);
  Claim('the backdrop sits under every control', structure.figures
    .filter((One) => One !== streaks && One.category !== Category.Surface)
    .every((One) => One.orderingRank > streaks.orderingRank));

  const needle = structure.query(handles.needle);
  Claim('the needle is an Overlay: bright to read, lighting nothing', needle.overlay === true);
  Claim('the gauge fill is an Illuminant, because a lit arc really does glow',
        structure.query(handles.gaugeFill).overlay === false);
}

// 🔴 THE SORT FINDING, PINNED.
//    Under the engine's key the transparent backdrop submits after every opaque figure, which with
//    no depth buffer would paint it over its own instrument. The browser submits by ordering rank
//    instead. If someone "fixes" the browser to use the engine's key, this fails and says why.
{
  const streaks = structure.query(handles.streaks);
  const needle = structure.query(handles.needle);
  Claim('the backdrop is transparent', streaks.opacity < OpaqueThreshold);
  Claim('the needle is opaque', needle.opacity >= OpaqueThreshold);
  const backdropKey = composeSortKey(true, streaks.orderingRank, 1.0);
  const needleKey = composeSortKey(false, needle.orderingRank, 1.0);
  Claim('under the engine key the backdrop would submit AFTER the needle it belongs behind',
        backdropKey > needleKey);

  const eye = [0, -0.8, 0];
  const packed = pack(structure, placements, eye);
  const backdropAt = packed.order.indexOf(handles.streaks);
  const needleAt = packed.order.indexOf(handles.needle);
  Claim('submitting by rank puts the backdrop before the needle', backdropAt < needleAt);
  Claim('...and before the digits', backdropAt < packed.order.indexOf(handles.digits[0]));
  Claim('...and after the face it sits on', backdropAt > 0);
}

// ── nothing hangs off the edge of the tablet ─────────────────────────────────────────────────────
// Projected onto the panel's own axes, exactly as InterfacePanelSample does it on the GPU.

{
  const h = placements[handles.housing];
  const right = [h[0][0], h[1][0], h[2][0]];
  const up = [h[0][1], h[1][1], h[2][1]];
  const centre = [h[0][3], h[1][3], h[2][3]];
  let worst = '';
  let outside = 0;
  structure.figures.forEach((figure, at) => {
    const p = placements[at];
    const d = [p[0][3] - centre[0], p[1][3] - centre[1], p[2][3] - centre[2]];
    const x = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
    const y = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
    const reachX = Math.abs(x) + figure.halfWidth;
    const reachY = Math.abs(y) + figure.halfHeight;
    if (reachX > PanelHalfWidth + 1e-6 || reachY > PanelHalfHeight + 1e-6) {
      outside++;
      worst = `figure ${at} (category ${figure.category}) reaches ${reachX.toFixed(4)} x ${reachY.toFixed(4)}`;
    }
  });
  Claim(`no figure reaches past the bezel — ${outside} do: ${worst}`, outside === 0);
}

// ── every character has a glyph ──────────────────────────────────────────────────────────────────
// 🔴 The stroke font draws NOTHING for a character it does not know, by design — "a missing label is
//    obvious, a wrong one is not". That makes a typo in a label invisible in the source and invisible
//    in review, so the set the font supports is read out of the shader and the labels checked
//    against it rather than against a list written here.

{
  const supported = new Set();
  for (const match of SDF_WGSL.matchAll(/\bC == (\d+)u\b/g)) supported.add(Number(match[1]));
  for (let code = 48; code <= 57; code++) supported.add(code);     // digits, handled as a range
  supported.add(32);                                               // space: advances, draws nothing

  Claim('the font table was found in the shader', supported.size > 30);

  let missing = [];
  structure.figures.forEach((figure) => {
    if (figure.category !== Category.Glyph) return;
    let code = figure.scalarAlpha;
    if (code >= 97 && code <= 122) code -= 32;                     // lowercase folds to uppercase
    if (!supported.has(code)) missing.push(`${String.fromCharCode(code)} (${code})`);
  });
  Claim(`every character on the panel has a glyph — missing: ${missing.join(', ')}`, missing.length === 0);

  const glyphs = structure.figures.filter((One) => One.category === Category.Glyph);
  Claim('the panel carries labels at all', glyphs.length > 20);
  Claim('every label is an Overlay', glyphs.every((One) => One.overlay === true));
  Claim('every label has a stroke width', glyphs.every((One) => One.scalarBeta > 0));
  Claim('no label is a space', glyphs.every((One) => One.scalarAlpha !== 32));
}

// ── the values ───────────────────────────────────────────────────────────────────────────────────

function digits(handle) { return handle.map((One) => structure.query(One).scalarAlpha); }

{
  // 🔴 A blank leading digit, not a zero: "007 KM/H" is a clock, not a speedometer. The engine gives
  //    blank its own code (10) rather than leaving it to be faked with an unlit colour.
  assignValues(structure, handles, { speed: 7 });
  Claim('7 km/h reads as two blanks and a seven', String(digits(handles.digits)) === String([10, 10, 7]));
  assignValues(structure, handles, { speed: 42 });
  Claim('42 km/h reads as a blank, a four and a two', String(digits(handles.digits)) === String([10, 4, 2]));
  assignValues(structure, handles, { speed: 180 });
  Claim('180 km/h fills all three cells', String(digits(handles.digits)) === String([1, 8, 0]));
  assignValues(structure, handles, { speed: 4000 });
  Claim('a speed beyond the readout clamps instead of wrapping',
        String(digits(handles.digits)) === String([9, 9, 9]));
  assignValues(structure, handles, { speed: -20 });
  Claim('a negative speed clamps to blank blank zero', String(digits(handles.digits)) === String([10, 10, 0]));
}

{
  const span = handles.troughHalfWidth;
  for (const [regen, wanted] of [[0, -span], [0.5, 0], [1, span]]) {
    assignValues(structure, handles, { regen });
    Near(`the slider knob at ${regen}`, structure.query(handles.sliderKnob).origin[0], wanted, 1e-9);
    const fill = structure.query(handles.sliderFill);
    // The fill is drawn from its own middle, so its right edge is centre + half width. That edge is
    // the only thing that has to agree with the knob, and getting it wrong is the classic bar bug.
    Near(`the fill's right edge meets the knob at ${regen}`,
         fill.origin[0] + fill.halfWidth, Math.max(wanted, -span + 0.001), 1e-9);
  }
  assignValues(structure, handles, { regen: 0.37 });
  Claim('the percentage readout rounds to two cells',
        String(digits(handles.rangeDigits)) === String([3, 7]));
  assignValues(structure, handles, { regen: 0.02 });
  Claim('a small percentage blanks its leading cell',
        String(digits(handles.rangeDigits)) === String([10, 2]));
}

{
  assignValues(structure, handles, { sport: 0 });
  Near('the toggle knob rests left', structure.query(handles.toggleKnob).origin[0], -0.0145, 1e-9);
  Near('the toggle bed is dark', structure.query(handles.toggleGlow).opacity, 0, 1e-9);
  assignValues(structure, handles, { sport: 1 });
  Near('the toggle knob travels right', structure.query(handles.toggleKnob).origin[0], 0.0145, 1e-9);
  Near('the toggle bed lights', structure.query(handles.toggleGlow).opacity, 1, 1e-9);
  assignValues(structure, handles, { sport: 0.5 });
  Near('a half-sprung toggle sits in the middle',
       structure.query(handles.toggleKnob).origin[0], 0, 1e-9);
}

{
  // The gauge and the needle must read the SAME value. Two figures showing one quantity is how a
  // cluster ends up lying to the driver.
  assignValues(structure, handles, { boost: 0.63 });
  Near('the arc and the needle agree', structure.query(handles.gaugeFill).scalarAlpha,
       structure.query(handles.needle).scalarAlpha, 0);
  assignValues(structure, handles, { boost: 3 });
  Near('an over-range boost clamps the needle to full', structure.query(handles.needle).scalarAlpha, 1);
}

// ── packing ──────────────────────────────────────────────────────────────────────────────────────

{
  const packed = pack(structure, placements, [0, -0.8, 0]);
  Claim('every figure is packed', packed.count === structure.count);
  Claim('the buffer is the right size', packed.data.length === packed.count * FloatsPerFigure);
  Claim('forty floats a figure', FloatsPerFigure === 40);

  const first = packed.order[0];
  Claim('the housing submits first', first === handles.housing);

  // A figure with no tint of its own takes the palette slot's colour, which is what makes the
  // palette configuration rather than decoration.
  const slot = packed.order.indexOf(handles.gaugeFill) * FloatsPerFigure;
  // Compared with a tolerance, not for equality: the buffer is Float32 and the palette is Float64,
  // so 0.08 does not survive the trip as 0.08. A check that demanded it would be testing IEEE 754.
  const tint = Array.from(packed.data.slice(slot + 24, slot + 28));
  Claim('an untinted figure resolves to its palette slot',
        tint.every((One, at) => Math.abs(One - structure.palette[Slot.Accent][at]) < 1e-7));

  const streakSlot = packed.order.indexOf(handles.streaks) * FloatsPerFigure;
  const wanted = structure.query(handles.streaks).streak;
  Claim('all eight streak parameters reach the buffer, in order',
        Array.from(packed.data.slice(streakSlot + 32, streakSlot + 40))
          .every((One, at) => Math.abs(One - wanted[at]) < 1e-7));
  Near('the category rides in the scalar vector',
       packed.data[streakSlot + 20], Category.StreakField);
}

// ── the live rung: the fibre volume ────────────────────────────────────────────────────────────
// 🔴 WHERE THE LIGHT ACTUALLY GOES, BOUNDED WITHOUT RE-IMPLEMENTING THE CURVE.
//
//    A cubic Bézier lies in the convex hull of its four control points, so the volume the strands
//    occupy can be bounded from the preset's numbers alone — no hash, no sampling, and no second
//    copy of fbBezier here to drift from the one in the shader. Along the axis the hull spans
//    0..length; perpendicular to it, every control point is within 1.6·spread + amplitude.
//
//    This caught a real bug. At the preset's own spread and amplitude the hull reached about
//    0.14 m perpendicular, which through the curve-to-panel mapping put strands TEN CENTIMETRES IN
//    FRONT OF THE GLASS — light glowing in the room outside the tablet, laterally clipped and so
//    invisible to the lateral clip that was supposed to contain it.

{
  const p = StreakPreset;
  const axis = (() => {
    const d = p.direction;
    const l = Math.hypot(d[0], d[1], d[2]);
    return [d[0] / l, d[1] / l, d[2] / l];
  })();
  const perpendicular = 1.6 * p.spread + p.amplitude;
  const span = (component) => {
    const reach = p.length * axis[component];
    return [Math.min(0, reach) - perpendicular, Math.max(0, reach) + perpendicular];
  };
  // Curve space is (along, lateral, lift); the panel mapping is x -> X, z -> Y, y -> DEPTH.
  const [ax0, ax1] = span(0), [ay0, ay1] = span(1), [az0, az1] = span(2);
  // The cross-section is a slab, not a disc: fbPanel stretches lift and squashes depth, so the
  // bound has to carry the same two gains or it stops bounding the thing that is drawn.
  const X = [p.origin[0] + ax0 * p.scale, p.origin[0] + ax1 * p.scale];
  const Y = [p.origin[1] + az0 * p.scale * p.liftGain, p.origin[1] + az1 * p.scale * p.liftGain];
  const Z = [p.origin[2] + ay0 * p.scale * p.depthGain, p.origin[2] + ay1 * p.scale * p.depthGain];

  const faceX = PanelHalfWidth - 0.012, faceY = PanelHalfHeight - 0.012;

  Claim(`the fibre volume never reaches past the glass (front face at ${Z[1].toFixed(4)} m, `
        + `fade band ${p.glassFade} m)`, Z[1] <= p.glassFade + 1e-9);
  Claim(`the volume stays within a sane depth (back face at ${Z[0].toFixed(4)} m)`, Z[0] > -0.40);
  Claim('the volume covers the full width of the face', X[0] <= -faceX && X[1] >= faceX);
  Claim('the volume covers the full height of the face', Y[0] <= -faceY && Y[1] >= faceY);
  Claim('the strands are longer than the panel, so none begins or ends in view',
        (X[1] - X[0]) > faceX * 2);

  // 🔴 The two gains are the whole reason the volume can fill the face without reaching the glass,
  //    so they are held apart. If anyone ever sets them equal the slab is a cylinder again and the
  //    Live rung goes back to being a band across the middle.
  Claim('the volume is a slab, not a cylinder', p.liftGain > p.depthGain * 2);
  Claim('lift has more room than depth, as a panel does',
        (Y[1] - Y[0]) > (Z[1] - Z[0]) * 2);
}

{
  const view = {
    time: 3.0, width: 1600, height: 900, projectionScale: 1404,
    rows: [[1, 0, 0, 0.5], [0, 0, -1, 0], [0, 1, 0, 1.2]],
  };
  const packed = packFibre(new Float32Array(FibreFloats), StreakPreset, view);
  Claim('the fibre uniform is 27 vec4s', FibreFloats === 108 && packed.length === 108);

  Near('slot 0 carries the panel-local origin', packed[0], StreakPreset.origin[0]);
  Near('...and the scale', packed[3], StreakPreset.scale);
  const d = StreakPreset.direction, dl = Math.hypot(d[0], d[1], d[2]);
  Near('slot 1 carries a normalised direction', Math.hypot(packed[4], packed[5], packed[6]), 1, 1e-6);
  Near('...pointing along the preset axis', packed[4], d[0] / dl, 1e-7);
  Near('slot 8 carries the strand count', packed[8 * 4 + 1], StreakPreset.strands);
  Near('the loop fraction wraps inside the period', packed[4 * 4 + 1], 0.25, 1e-9);

  // The panel's rows must reach the shader, or the light would sit in the room rather than in the
  // tablet and would not move when the tablet does.
  Near('the panel row X lands in slot 22', packed[22 * 4 + 3], 0.5);
  Near('the panel row Y lands in slot 23', packed[23 * 4 + 2], -1);
  Near('the panel row Z lands in slot 24', packed[24 * 4 + 3], 1.2, 1e-6);
  Near('the clip carries the face half width', packed[25 * 4], PanelHalfWidth - 0.012);
  Near('the clip carries the face half height', packed[25 * 4 + 1], PanelHalfHeight - 0.012);
  Near('the glass band reaches the shader', packed[26 * 4], StreakPreset.glassFade);

  // Stop positions must ascend, or fbRamp walks off the end of its own ramp.
  const positions = [0, 1, 2, 3, 4, 5, 6, 7].map((at) => packed[(20 + (at >> 2)) * 4 + (at & 3)]);
  Claim(`stop positions ascend: ${positions.join(', ')}`,
        positions.every((One, at) => at === 0 || One >= positions[at - 1]));
  Near('the first stop sits at zero', positions[0], 0);

  let refused = false;
  try { packFibre(new Float32Array(FibreFloats), { ...StreakPreset, shape: 'ribbon' }, view); }
  catch { refused = true; }
  Claim('an unported shape is refused rather than silently drawn as a streak', refused);
}

{
  // The live rung is the editor's own code, so the functions it was ported from have to be present
  // and have to still be the editor's. A rewrite here is how a port stops being a port.
  for (const name of ['fbHash', 'fbUnit', 'fbBezier', 'fbPulse', 'fbRamp', 'fbColourAt',
                      'vsFibre', 'fsFibre', 'vsSpark', 'fsSpark']) {
    Claim(`the fibre shader carries ${name}`, FIBRE_WGSL.includes(`fn ${name}`));
  }
  Claim('the fibre shader clips with the engine\'s own rounded rectangle',
        FIBRE_WGSL.includes('DistanceRoundedRectangle(aperture, FB.clip.xy, FB.clip.z)'));

  // 🔴 THE CLIP IS AT THE APERTURE, NOT AT THE STRAND.
  //
  //    Clipping on the strand's own panel xy is a flat test. It is invisible head-on and wrong the
  //    moment the tablet turns: a volume a hundred millimetres behind the glass projects outside
  //    the panel's outline at a steep angle, and the light sprays off the side into the room. A
  //    window decides what you can see through it AT the window, so the sightline is carried to
  //    panel z = 0 first. These pins exist because the flat version looked perfectly fine in every
  //    screenshot anyone had taken until someone orbited past sixty degrees.
  Claim('the fibre shader carries the aperture', FIBRE_WGSL.includes('fn fbAperture'));
  Claim('the aperture is solved on the glass plane', FIBRE_WGSL.includes('(0.0 - eye.z) / rise'));
  Claim('nothing clips on the strand position any more',
        !FIBRE_WGSL.includes('DistanceRoundedRectangle(i.panel.xy'));
  Claim('the eye is carried into panel space by the transpose',
        FIBRE_WGSL.includes('fn fbEyePanel') && FIBRE_WGSL.includes('FB.rowX.x, FB.rowY.x, FB.rowZ.x'));
  Claim('the back of the tablet is opaque', FIBRE_WGSL.includes('fbEyePanel().z <= 0.0'));
  Claim('the unported shapes are absent, not stubbed',
        !FIBRE_WGSL.includes('fbWave') && !FIBRE_WGSL.includes('fbTrail'));
}

// ── the chassis ──────────────────────────────────────────────────────────────────────────────────

{
  Claim('the chassis is a rounded solid, not a plane', Chassis.halfDepth > 0.008);
  Claim('the body is wider than it is tall', Chassis.halfWidth > Chassis.halfHeight);
  Claim('the corner radius fits inside the short side', Chassis.cornerRadius < Chassis.halfHeight);
  Claim('the glass is far smoother than the body', Chassis.glassRoughness < Chassis.bodyRoughness * 0.25);

  Claim('the body is actually marched', CHASSIS_WGSL.includes('fn RoomMarch'));
  Claim('the surface normal is tetrahedral', CHASSIS_WGSL.includes('fn RoomNormal'));
  Claim('the body opens a well for the interface to draw into', CHASSIS_WGSL.includes('fn fsRoom'));
  Claim('the glass left the chassis for the target it now reads',
        !CHASSIS_WGSL.includes('fn fsGlass') && DISPLAY_WGSL.includes('fn fsGlass'));

  // 🔴 A POINT LIGHT CANNOT APPEAR IN A MIRROR, AND THE GLASS IS A MIRROR.
  //
  //    pow(dot(dir, key), n) is fine on the body and produces nothing at all on the screen: a flat
  //    mirror reflects exactly one direction per pixel, so a lobe tight enough to read as sharp is
  //    tight enough that the eye never lands inside it. What a photograph of a device shows is a
  //    long soft strip, and that is the shape of the LIGHT, not of the material. So a source here
  //    is a rectangle measured in its own tangent plane and the two are separate numbers.
  Claim('a source is a card with two extents', CHASSIS_WGSL.includes('fn RoomCard')
        && CHASSIS_WGSL.includes('wide : f32, tall : f32'));
  Claim('the card is measured in its own tangent plane',
        CHASSIS_WGSL.includes('dot(direction, sideways) / depth'));
  Claim('a rougher surface sees a bigger card', CHASSIS_WGSL.includes('mix(3.0, 1.0, sharpness)'));

  // 🔴 AND THE GLASS DOES NOT REFLECT THE KEY.
  //
  //    The panel stands upright, so for any eye above its centre the mirror direction points DOWN
  //    and forward. A key placed where a key belongs — high — reflects to somewhere below the
  //    floor. The card the screen shows has to be low, which in a real room is the lit table the
  //    device stands on. These two are pinned apart because collapsing them back into one light
  //    is the obvious simplification and it returns a black sheet with a rim.
  Claim('the key is high', CHASSIS_WGSL.includes('kKeyDirection = vec3f(-0.3827, -0.6428, 0.6634)'));
  Claim('the card the glass shows is low',
        CHASSIS_WGSL.includes('kFrontCard = vec3f(-0.3302, -0.8805, -0.3402)'));
  Claim('they are two different lights', CHASSIS_WGSL.includes('kKeyDirection')
        && CHASSIS_WGSL.includes('kFrontCard') && !CHASSIS_WGSL.includes('kFrontCard = kKeyDirection'));
  Claim('the room behind is dimmer than the subject', CHASSIS_WGSL.includes('kRoomFalloff = 0.55'));

  // The chamfer is the strongest "machined from a solid" cue there is, and a plane cannot have one.
  Claim('the turning edge catches the key', CHASSIS_WGSL.includes('1.0 - abs(dot(worldNormal'));

  // 🔴 THE TABLET HAS TO STAND SOMEWHERE.
  //
  //    A slab with no shadow is a picture of a slab. The contact shadow and the surface under it
  //    are not decoration — they are the cues that say "object", and without them no amount of
  //    material work on the body helps.
  Claim('the tablet leans back, as one in a dock does', Chassis.lean > 0.05 && Chassis.lean < 0.6);
  Claim('there is a dock for it to stand in', Chassis.plinthHalf[0] > PanelHalfWidth * 0.5);
  Claim('the dock is wider than it is tall', Chassis.plinthHalf[0] > Chassis.plinthHalf[2] * 8);
  Claim('there is a desk', CHASSIS_WGSL.includes('kDesk'));
  Claim('the scene has three materials, not one',
        CHASSIS_WGSL.includes('kBody') && CHASSIS_WGSL.includes('kDock') && CHASSIS_WGSL.includes('kDesk'));
  Claim('the contact shadow is marched', CHASSIS_WGSL.includes('fn RoomShadow'));
  Claim('the penumbra widens with distance from the occluder',
        CHASSIS_WGSL.includes('min(shade, sharpness * distance / travel)'));
  Claim('the screen pools its own light on the desk',
        CHASSIS_WGSL.includes('spillFall') && CHASSIS_WGSL.includes('RoomShadow(hit.world, normalize(-panelFace)'));

  // 🔴 The march is in WORLD space. A desk is flat in the world and the panel is leaning, so a
  //    desk written in panel coordinates would be a slope. This one is easy to regress by
  //    "simplifying" the transform back out of the scene function.
  Claim('the scene is marched in world space', CHASSIS_WGSL.includes('fn RoomScene(world : vec3f)')
        && CHASSIS_WGSL.includes('RoomBody(RoomToPanelPoint(world))'));

  // A plane is infinite and a marcher is not: left alone it stops at the step limit and draws a
  // hard horizon with black above it.
  Claim('the desk fades into the room instead of ending at the step limit',
        CHASSIS_WGSL.includes('kDeskFade') && CHASSIS_WGSL.includes('hit.travel / kDeskFade'));
  Claim('the march stops once the fade is complete',
        CHASSIS_WGSL.includes('travel > kDeskFade'));

  // 🔴 A device is BIGGER than its display. At 2 mm the bezel was a tolerance, and the camera and
  //    speaker sat underneath the glass quad where they could never be seen.
  const bezelX = Chassis.halfWidth - PanelHalfWidth;
  const bezelY = Chassis.halfHeight - PanelHalfHeight;
  Claim('the bezel is a bezel, not a tolerance', bezelX > 0.008 && bezelY > 0.008);
  Claim('the hardware sits in the bezel, outside the glass',
        CHASSIS_WGSL.includes('R.face.y + 0.0062') && 0.0062 + 0.0024 < bezelY);
  Claim('there is a camera and a speaker', CHASSIS_WGSL.includes('let lens =')
        && CHASSIS_WGSL.includes('let holes ='));

  // The uniform has to survive the trip.
  const rows = [[1, 0, 0, 0.02], [0, 0, -1, -0.03], [0, 1, 0, 0.5]];
  const out = new Float32Array(RoomFloats);
  packRoom(out, rows, { forward: [0, 1, 0], right: [1, 0, 0], up: [0, 0, 1], tanHalf: 0.317, aspect: 1.6 });
  Claim('packRoom fills every slot', out.every((v) => Number.isFinite(v)));
  Claim('packRoom writes the panel rows', Math.abs(out[3] - 0.02) < 1e-6 && Math.abs(out[11] - 0.5) < 1e-6);
  Claim('packRoom finds the desk under the lowest corner of the body',
        Math.abs(out[34] - (0.5 - Chassis.halfHeight)) < 1e-5);
  Claim('packRoom carries the body extents',
        [...out].some((v) => Math.abs(v - Chassis.halfWidth) < 1e-6));
}

// ── the display target ───────────────────────────────────────────────────────────────────────────

{
  // 🔴 THE OFF-AXIS PROJECTION HAS TO BE EXACT, NOT CLOSE.
  //
  //    The glass samples the display at the point where the sightline crosses the panel. If the
  //    matrix that FILLED the display disagrees with that mapping even slightly, every pixel is
  //    displaced, and the displacement would be indistinguishable from the refraction offset the
  //    same shader applies on purpose. One of the two would be silently wrong and the picture
  //    would still look plausible. So: push points through the matrix and check they land where
  //    the ray-plane crossing says they land, to float precision.
  const rows = [[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0]];          // the housing's quarter turn
  const eye = [0.31, -0.62, 0.17];
  const hw = PanelHalfWidth, hh = PanelHalfHeight;
  const { clip, eyePanel } = DisplayClip(rows, eye, hw, hh);

  const through = (P) => {
    const w = [P[0], P[1], P[2], 1];
    const out = [0, 0, 0, 0];
    for (let r = 0; r < 4; r++) {
      out[r] = clip[0 * 4 + r] * w[0] + clip[1 * 4 + r] * w[1]
             + clip[2 * 4 + r] * w[2] + clip[3 * 4 + r] * w[3];
    }
    return [out[0] / out[3], out[1] / out[3], out[2] / out[3], out[3]];
  };
  // Panel point -> world, for this particular rotation.
  const world = (x, y, z) => [x, -z, y];

  // The four corners of the panel must land exactly on the corners of the target.
  let corners = true;
  for (const [sx, sy] of [[-1, -1], [1, -1], [-1, 1], [1, 1]]) {
    const n = through(world(sx * hw, sy * hh, 0));
    if (Math.abs(n[0] - sx) > 2e-5 || Math.abs(n[1] - sy) > 2e-5) corners = false;
  }
  Claim('the panel fills the display exactly', corners);

  // A point BEHIND the glass must land where its sightline crosses the glass — this is the one
  // that fails if anyone replaces the frustum with an orthographic bake.
  let depth = true;
  for (const [px, py, pz] of [[0.05, 0.02, -0.08], [-0.12, 0.06, -0.14], [0.18, -0.09, -0.03]]) {
    const t = eyePanel[2] / (eyePanel[2] - pz);
    const cx = eyePanel[0] + (px - eyePanel[0]) * t;
    const cy = eyePanel[1] + (py - eyePanel[1]) * t;
    const n = through(world(px, py, pz));
    if (Math.abs(n[0] - cx / hw) > 2e-5 || Math.abs(n[1] - cy / hh) > 2e-5) depth = false;
  }
  Claim('depth behind the glass projects, so the volume is not flattened', depth);
  Claim('everything drawn is in front of the near plane',
        through(world(0.05, 0.02, -0.08))[3] > 0 && through(world(0, 0, 0))[3] > 0);

  // Move the eye and the image of a point BEHIND the glass must move with it. This is the
  // parallax, and it is the single thing an orthographic bake would quietly destroy: a flattened
  // display would put that point at the same place from every angle.
  const moved = DisplayClip(rows, [-0.31, -0.62, 0.17], hw, hh);
  const far = world(0.05, 0.02, -0.12);
  const before = through(far);
  const after = (() => {
    const w = [far[0], far[1], far[2], 1];
    const out = [0, 0, 0, 0];
    for (let r = 0; r < 4; r++) {
      out[r] = moved.clip[0 * 4 + r] * w[0] + moved.clip[1 * 4 + r] * w[1]
             + moved.clip[2 * 4 + r] * w[2] + moved.clip[3 * 4 + r] * w[3];
    }
    return [out[0] / out[3], out[1] / out[3]];
  })();
  Claim('the volume behind the glass parallaxes with the eye',
        Math.abs(after[0] - before[0]) > 0.05);

  Claim('the display has the panel aspect',
        Math.abs(DisplayWidth / DisplayHeight - PanelHalfWidth / PanelHalfHeight) < 0.002);
  Claim('line width is measured in display pixels',
        DisplayProjectionScale(0.5, PanelHalfHeight) > 0);

  // 🔴 The glass SAMPLES. Additive glass can only add, so it can never bend, displace or dim what
  //    is under it, and bending what is under it is the entire physical content of the word.
  Claim('the glass reads the display target', DISPLAY_WGSL.includes('textureSample(DisplaySheet'));
  Claim('the glass offsets by refraction', DISPLAY_WGSL.includes('fn GlassOffset')
        && DISPLAY_WGSL.includes('refract(incident, normal'));
  Claim('transmission and reflection SPLIT the energy',
        DISPLAY_WGSL.includes('transmitted * (1.0 - fresnel) + reflected * fresnel'));
  Claim('the glass is no longer additive', !DISPLAY_WGSL.includes('blend: additive'));
  Claim('the glow is added inside the glass, so Fresnel attenuates it',
        DISPLAY_WGSL.indexOf('BloomSheet, DisplaySampler, base') < DISPLAY_WGSL.indexOf('1.0 - fresnel'));

  // The measurement that deleted the dispersion. Keep it: if the glass ever gets thicker, this is
  // the number that decides whether per-channel sampling earns its two extra reads.
  const slide = (deg) => {
    const a = deg * Math.PI / 180;
    const i = [Math.sin(a), 0, -Math.cos(a)];
    const shift = (n) => {
      const eta = 1 / n, cosi = -i[2];
      const k = 1 - eta * eta * (1 - cosi * cosi);
      const bent = [eta * i[0], 0, eta * i[2] + (eta * cosi - Math.sqrt(k))];
      return (bent[0] / Math.abs(bent[2]) - i[0] / Math.abs(i[2])) * Glass.thickness;
    };
    return { green: shift(Glass.index), spread: Math.abs(shift(1.505) - shift(1.529)) };
  };
  const perMetre = DisplayWidth / (2 * PanelHalfWidth);
  Claim('refraction is visible at a steep angle', Math.abs(slide(60).green * perMetre) > 4);
  Claim('refraction vanishes head-on', Math.abs(slide(0).green) < 1e-9);
  Claim('dispersion is below the pixel grid, which is why it is not simulated',
        slide(75).spread * perMetre < 1);
  Claim('and the shader does not pretend otherwise', !DISPLAY_WGSL.includes('indexRed'));
}

// ── FRONTIER OS, the home screen ─────────────────────────────────────────────────────────────────
{
  const { structure, handles } = constructShellLayout();
  Claim('the shell builds inside the engine figure limit', structure.count > 120 && structure.count < 1024);
  Claim('the shell exposes the two handles every host needs',
        handles.housing !== undefined && handles.streaks !== undefined);
  Claim('every app on the wall got a tile', handles.tiles.length === ShellApps.length);

  // 🔴 A PIXEL ON A SCREEN IS EMITTED. The first cut built the chrome at emissiveWeight 0 and the
  //    dock, the search pill and the link strip all rendered at one part in 255 — the still showed
  //    four icons floating on nothing. InterfaceRaster.frag does mix(Received, Emitted, weight),
  //    so a plate inside a display with weight 0 shows only the room bouncing off it.
  Claim('the dock is an emitter, not a card lit by the room',
        structure.query(handles.dock).emissiveWeight === 1);
  Claim('so is the screen it sits on', structure.query(handles.face).emissiveWeight === 1);

  // 🔴 AND THE PALETTE IS IN DISPLAY VALUES. The swap chain is bgra8unorm, not -srgb, so a figure's
  //    tint reaches the screen unconverted. Converting the reference's #202020 to linear put the
  //    dock panel at 0.0145 against a 0.0030 screen, which is not a panel. A raised surface has to
  //    out-read the screen behind it by a wide margin or it is not raised.
  const lum = (c) => 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  Claim('a raised panel reads well clear of the screen behind it',
        lum(structure.palette[Slot.Surface]) > lum(structure.palette[Slot.Housing]) * 4);
  Claim('an inset field sits between the two',
        lum(structure.palette[Slot.SurfaceSunk]) > lum(structure.palette[Slot.Housing]) &&
        lum(structure.palette[Slot.SurfaceSunk]) < lum(structure.palette[Slot.Surface]));

  // 🔴 THE CLOCK MUST NOT SHUFFLE WHEN THE COLON BLINKS. text() normally draws nothing for a space
  //    AND makes no figure for it, so a run rewritten from "09:41" to "09 41" would have written
  //    the minutes one glyph early and blanked the last digit. keepSpaces gives the run one figure
  //    per character so the rewrite is positional. This is the check for that.
  const read = (run) => run.map((at) => String.fromCharCode(structure.query(at).scalarAlpha)).join('');
  assignShellValues(structure, handles, { clock: new Date(2026, 9, 9, 9, 41), time: 0.1 });
  Claim('the clock reads the hour it was given', read(handles.clock) === '09:41');
  assignShellValues(structure, handles, { clock: new Date(2026, 9, 9, 9, 41), time: 0.7 });
  Claim('the blink drops the colon and MOVES NOTHING', read(handles.clock) === '09 41');
  Claim('the status bar keeps its colon regardless', read(handles.statusClock) === '09:41');

  // The longest date there is has to fit the run that was built for it, and the shortest must not
  // leave the previous month's tail behind.
  assignShellValues(structure, handles, { clock: new Date(2026, 8, 9, 9, 41), time: 0 });
  Claim('the widest date fits', read(handles.date) === 'WEDNESDAY 09 SEPTEMBER');
  assignShellValues(structure, handles, { clock: new Date(2026, 4, 1, 0, 0), time: 0 });
  Claim('a shorter date blanks the tail instead of keeping it',
        read(handles.date) === 'FRIDAY 01 MAY'.padEnd(22, ' '));

  // Every label on this screen has to be spellable by the engine's stroke font, which has no
  // lowercase at all — a name that needs one would silently draw a blank tile.
  const Spellable = /^[A-Z0-9 %+,\-./:]*$/;
  Claim('every app name is in the font the engine actually has',
        ShellApps.every((app) => Spellable.test(app.name)));
}

// ── 🔴 EVERY FIGURE MUST SURVIVE THE GPU'S CLIPPER, NOT JUST MY RASTERISER ───────────────────────
//
//    This is the check that was missing, and its absence cost a whole screen. The display frustum
//    put its NEAR PLANE on the panel surface while the layout stacks every figure one millimetre
//    at a time TOWARD the viewer. The GPU dutifully clipped all of them; the browser showed a
//    black screen with only the fibres, which live behind the glass at negative z.
//
//    It survived because RenderStill.mjs — my own rasteriser, and the thing I had been checking
//    against — does not implement near-plane clipping at all. It drew the figures happily and the
//    stills looked right. A renderer that is more forgiving than the hardware is not a proof.
//
//    So: take the REAL matrix the host uploads, push every figure of BOTH compositions through it,
//    and demand the homogeneous result actually lie inside the clip volume.
{
  const Angles = [
    { orbit: 0.05, tilt: 0.04, distance: 0.60 },
    { orbit: 0.62, tilt: 0.26, distance: 0.74 },
    { orbit: 1.24, tilt: 0.42, distance: 0.70 },   // the grazing angle
    { orbit: -0.80, tilt: -0.20, distance: 1.40 },
  ];

  for (const [name, make] of [['home', constructShellLayout], ['dashboard', constructHudLayout]]) {
    const { structure, handles } = make();
    structure.query(handles.housing).rotationX = Math.PI / 2 - Chassis.lean;
    const { placements } = resolve(structure);
    const rows = placements[handles.housing];

    let worst = Infinity, clipped = 0, behind = 0;
    for (const view of Angles) {
      const eye = [
        Math.sin(view.orbit) * Math.cos(view.tilt) * view.distance,
        -Math.cos(view.orbit) * Math.cos(view.tilt) * view.distance,
        Math.sin(view.tilt) * view.distance,
      ];
      const M = DisplayClip(rows, eye, PanelHalfWidth, PanelHalfHeight).clip;
      for (let at = 0; at < structure.count; at++) {
        const p = placements[at];
        if (!p) continue;
        const world = [p[0][3], p[1][3], p[2][3], 1];
        const out = [0, 1, 2, 3].map((c) =>
          M[c] * world[0] + M[4 + c] * world[1] + M[8 + c] * world[2] + M[12 + c] * world[3]);
        if (!(out[3] > 0)) { behind++; continue; }
        const depth = out[2] / out[3];
        worst = Math.min(worst, depth);
        if (depth < 0 || depth > 1) clipped++;
      }
    }
    Claim(`${name}: no figure projects behind the eye`, behind === 0);
    Claim(`${name}: NO FIGURE IS CLIPPED BY THE NEAR PLANE`, clipped === 0);
    Claim(`${name}: and the nearest one keeps headroom in front of it`, worst > 0.005);
  }

  // The margin is real and the stack is measured against it, so neither can drift into the other.
  Claim('the frustum reaches further forward than the interface stacks', Approach > 0.0044 * 2);
}

// ── done ─────────────────────────────────────────────────────────────────────────────────────────

if (Failures.length) {
  process.stderr.write(`\nCheckHud: ${Failures.length} FAILED, ${Passed} passed\n`);
  process.exit(1);
}
process.stdout.write(`CheckHud: PASS ${Passed}\n`);
