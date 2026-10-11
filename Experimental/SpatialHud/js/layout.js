// The car HUD tablet — the browser's version of a project-side composition.
//
// This is the layer Projects/Project-Zero/Source/InterfaceTrialSequence.cpp occupies: the engine knows
// rounded rectangles, arcs, needles, digits, glyphs and a streak field; THIS file decides that
// twenty-odd of them make a tablet, where they sit, and what each one shows. Nothing here is reusable
// and nothing here is meant to be (CLAUDE.md §6).
//
// The trial panel it grows out of was 0.36 x 0.22 m and called itself "roughly a large tablet". This
// is 0.46 x 0.27 — a fascia screen rather than a control panel — laid out in the three bands a car
// cluster actually uses: a gauge on the left, the number you are looking for in the middle, and the
// controls you reach for on the right.
//
// Layout is the VERB. ConstructHudLayout places the parts; it is not the name of a drawable.

import { Category, Slot, Structure, Figure } from './figures.js';

export const PanelHalfWidth = 0.230;    // [m]
export const PanelHalfHeight = 0.135;   // [m]

const FaceInset = 0.008;                // [m] the bezel's visible width
const Forward = 0.0010;                 // [m] one layer off the one below, so the stack reads as layers

// The gauge, left band.
const GaugeX = -0.138, GaugeY = -0.004, GaugeRadius = 0.070;

// The readout, middle band.
const DigitX = 0.024, DigitY = 0.034;
const DigitHalf = [0.020, 0.034], DigitStep = 0.050, DigitBar = 0.0072;

// The controls, right band.
const ControlX = 0.158;

// ── text ─────────────────────────────────────────────────────────────────────────────────────────
// One Glyph figure per character. The stroke font is the engine's own (DistanceStrokeGlyph), so a
// label here is drawn by the same code that will draw it natively — there is no second font.

export function text(structure, parent, words, options) {
  // 🔴 keepSpaces emits a figure for a space too. The stroke font returns 1e9 for code 32, so it
  //    draws nothing either way — but the run then has one figure PER CHARACTER, which is the only
  //    way a caller can rewrite the words later by index without the spaces shifting everything
  //    after them. Static labels leave it off and save the figures.
  const { x, y, z = Forward, size, palette = Slot.Marking, rank = 10,
          align = 'left', weight = 0.085, tracking = 0.30, opacity = 1,
          keepSpaces = false } = options;
  const emWidth = size * 0.58;
  const advance = emWidth + size * tracking * 0.42;
  const span = advance * (words.length - 1) + emWidth;
  const start = align === 'centre' ? x - span * 0.5 + emWidth * 0.5
              : align === 'right' ? x - span + emWidth * 0.5
              : x + emWidth * 0.5;

  const made = [];
  for (let at = 0; at < words.length; at++) {
    const code = words.charCodeAt(at);
    if (code === 32 && !keepSpaces) continue;        // a space advances and draws nothing
    const index = structure.construct(Figure({
      category: Category.Glyph,
      origin: [start + advance * at, y, z],
      // The em box: cap height 1.0, so the figure's half height IS half the cap height.
      halfWidth: emWidth * 0.5,
      halfHeight: size * 0.5,
      scalarAlpha: code,
      scalarBeta: size * weight,                     // stroke half width, in local metres
      palette,
      opacity,
      overlay: true,                                 // a marking: bright to read, lights nothing
      orderingRank: rank,
    }));
    structure.attach(index, parent);
    made.push(index);
  }
  return made;
}

// ── the composition ──────────────────────────────────────────────────────────────────────────────

export function constructHudLayout() {
  const structure = new Structure();

  // The palette is configuration, not code — PaletteConfiguration exists so a project can set its own
  // instrument colour without touching a figure. A car cluster is cyan and amber on near-black.
  structure.palette[Slot.Accent] = [0.08, 0.72, 0.95, 1.0];
  structure.palette[Slot.Caution] = [1.00, 0.60, 0.12, 1.0];
  structure.palette[Slot.Marking] = [0.86, 0.90, 0.95, 1.0];
  structure.palette[Slot.MarkingMute] = [0.16, 0.22, 0.26, 1.0];
  structure.tokenRadius = 0.006;

  const handles = {};

  // ── housing ────────────────────────────────────────────────────────────────────────────────────
  // Stands upright: a quarter turn about X carries the local +Y (panel up) onto world +Z (world up).
  const housing = structure.construct(Figure({
    category: Category.Surface,
    rotationX: Math.PI / 2,
    halfWidth: PanelHalfWidth, halfHeight: PanelHalfHeight,
    cornerRadius: 0.020,
    palette: Slot.Housing,
    emissiveWeight: 0,                 // a dark bezel is a physical surface, not a glowing decal
    orderingRank: 0,
  }));
  handles.housing = housing;

  const face = structure.construct(Figure({
    category: Category.Surface,
    origin: [0, 0, Forward],
    halfWidth: PanelHalfWidth - FaceInset, halfHeight: PanelHalfHeight - FaceInset,
    cornerRadius: 0.014,
    palette: Slot.Surface,
    emissiveWeight: 0,                 // the card the controls sit on: albedo, lit by the room
    orderingRank: 1,
  }));
  structure.attach(face, housing);

  // ── the streak field ───────────────────────────────────────────────────────────────────────────
  // 🔴 The background is a LAYER, not a wallpaper: it sits on the face, under every control, and it
  //    is an Illuminant — it is light inside the glass, so it feeds the panel's scene luminaire and
  //    pools on whatever the tablet is standing on. A backdrop that lit nothing would read as a
  //    sticker the moment the room went dark.
  handles.streaks = structure.construct(Figure({
    category: Category.StreakField,
    origin: [0, 0, Forward * 0.6],
    halfWidth: PanelHalfWidth - FaceInset - 0.004,
    halfHeight: PanelHalfHeight - FaceInset - 0.004,
    palette: Slot.Accent,
    emissiveWeight: 1,
    opacity: 0.85,
    orderingRank: 2,
    //        strands  amplitude  waves  speed | tail  seed  intensity  core
    streak: [18, 0.019, 2.6, 0.55, 0.42, 11, 1.35, 0.0028],
  }));
  structure.attach(handles.streaks, face);

  // ── left band: the gauge ───────────────────────────────────────────────────────────────────────
  const gauge = structure.construct(Figure({
    category: Category.Lamp,
    origin: [GaugeX, GaugeY, Forward * 2],
    halfWidth: GaugeRadius + 0.012, halfHeight: GaugeRadius + 0.012,   // the sunk bed the dial sits in
    palette: Slot.SurfaceSunk,
    emissiveWeight: 0,
    opacity: 0.88,
    orderingRank: 3,
  }));
  structure.attach(gauge, face);

  const ticks = structure.construct(Figure({
    category: Category.TickRing,
    origin: [0, 0, Forward * 0.4],
    halfWidth: GaugeRadius, halfHeight: GaugeRadius,
    scalarBeta: 13,                    // mark count
    palette: Slot.MarkingMute,
    overlay: true,
    orderingRank: 4,
  }));
  structure.attach(ticks, gauge);

  handles.gaugeFill = structure.construct(Figure({
    category: Category.Arc,
    origin: [0, 0, Forward * 0.5],
    halfWidth: GaugeRadius - 0.016, halfHeight: GaugeRadius - 0.016,
    scalarAlpha: 0,                    // fill fraction — the spring writes this
    scalarBeta: 0.0085,                // thickness [m]
    palette: Slot.Accent,
    orderingRank: 5,
  }));
  structure.attach(handles.gaugeFill, gauge);

  // The redline band: a second arc, rotated so its own sweep starts where the warning does. The
  // engine shares one arc opening across every figure precisely so this costs a rotation and not a
  // new field in the slot.
  const redline = structure.construct(Figure({
    category: Category.Arc,
    origin: [0, 0, Forward * 0.45],
    rotationZ: -4.1887902 * 0.78,      // kArcSweep x the fraction the band begins at
    halfWidth: GaugeRadius - 0.016, halfHeight: GaugeRadius - 0.016,
    scalarAlpha: 0.22,
    scalarBeta: 0.0085,
    palette: Slot.Warning,
    opacity: 0.85,
    orderingRank: 4,
  }));
  structure.attach(redline, gauge);

  handles.needle = structure.construct(Figure({
    category: Category.Needle,
    origin: [0, 0, Forward * 0.8],
    halfWidth: GaugeRadius - 0.004, halfHeight: GaugeRadius - 0.004,
    scalarAlpha: 0,                    // angle fraction — the spring writes this
    scalarBeta: 0.0055,                // hub radius [m]
    palette: Slot.Marking,
    overlay: true,
    orderingRank: 7,
  }));
  structure.attach(handles.needle, gauge);

  // 🔴 ABOVE the hub, not below it. kArcStart/kArcSweep open the dial at the TOP (150 deg round
  //    through the bottom to 30 deg), so the needle sweeps the lower half and a label under the hub
  //    is a label the needle draws through. The opening is the one place on a dial nothing moves.
  text(structure, gauge, 'BOOST', { x: 0, y: GaugeRadius * 0.54, z: Forward,
                                    size: 0.0105, align: 'centre', palette: Slot.MarkingMute, rank: 8 });

  // ── middle band: the number you are looking for ────────────────────────────────────────────────
  handles.digits = [];
  for (let at = 0; at < 3; at++) {
    const cell = structure.construct(Figure({
      category: Category.SegmentCell,
      origin: [DigitX + (at - 1) * DigitStep, DigitY, Forward * 2],
      halfWidth: DigitHalf[0], halfHeight: DigitHalf[1],
      scalarAlpha: 10,                 // 10 = blank, until a value arrives
      scalarBeta: DigitBar,
      palette: Slot.Marking,
      overlay: true,
      orderingRank: 9,
    }));
    structure.attach(cell, face);
    handles.digits.push(cell);
  }

  text(structure, face, 'KM/H', { x: DigitX, y: DigitY - 0.058, z: Forward * 2,
                                  size: 0.0115, align: 'centre', palette: Slot.MarkingMute, rank: 10 });

  // ── right band: the controls you reach for ─────────────────────────────────────────────────────
  // The toggle. A bed, a knob that slides across it, and the knob's own LED — the LED is the
  // Illuminant here, which is why a toggle that is ON lights the room a little and one that is OFF
  // does not.
  const toggleY = 0.052;
  text(structure, face, 'SPORT', { x: ControlX, y: toggleY + 0.026, z: Forward * 2,
                                   size: 0.0105, align: 'centre', palette: Slot.MarkingMute, rank: 10 });

  const toggleBed = structure.construct(Figure({
    category: Category.Surface,
    origin: [ControlX, toggleY, Forward * 2],
    halfWidth: 0.027, halfHeight: 0.0125,
    cornerRadius: 0.0125,
    palette: Slot.SurfaceSunk,
    emissiveWeight: 0,
    orderingRank: 11,
  }));
  structure.attach(toggleBed, face);

  handles.toggleGlow = structure.construct(Figure({
    category: Category.Surface,
    origin: [0, 0, Forward * 0.3],
    halfWidth: 0.027, halfHeight: 0.0125,
    cornerRadius: 0.0125,
    palette: Slot.Confirm,
    opacity: 0,                        // the spring writes this
    orderingRank: 12,
  }));
  structure.attach(handles.toggleGlow, toggleBed);

  handles.toggleKnob = structure.construct(Figure({
    category: Category.Lamp,
    origin: [-0.0145, 0, Forward * 0.8],
    halfWidth: 0.0095, halfHeight: 0.0095,
    palette: Slot.Marking,
    overlay: true,
    orderingRank: 13,
  }));
  structure.attach(handles.toggleKnob, toggleBed);

  // The slider.
  const sliderY = -0.022;
  text(structure, face, 'REGEN', { x: ControlX, y: sliderY + 0.024, z: Forward * 2,
                                   size: 0.0105, align: 'centre', palette: Slot.MarkingMute, rank: 10 });

  const trough = structure.construct(Figure({
    category: Category.Surface,
    origin: [ControlX, sliderY, Forward * 2],
    halfWidth: 0.044, halfHeight: 0.0045,
    cornerRadius: 0.0045,
    palette: Slot.SurfaceSunk,
    emissiveWeight: 0,
    orderingRank: 11,
  }));
  structure.attach(trough, face);
  handles.troughHalfWidth = 0.044;

  handles.sliderFill = structure.construct(Figure({
    category: Category.Surface,
    origin: [-0.044, 0, Forward * 0.3],
    halfWidth: 0.001, halfHeight: 0.0045,
    cornerRadius: 0.0045,
    palette: Slot.Accent,
    orderingRank: 12,
  }));
  structure.attach(handles.sliderFill, trough);

  handles.sliderKnob = structure.construct(Figure({
    category: Category.Lamp,
    origin: [-0.044, 0, Forward * 0.9],
    halfWidth: 0.0082, halfHeight: 0.0082,
    palette: Slot.Marking,
    overlay: true,
    orderingRank: 13,
  }));
  structure.attach(handles.sliderKnob, trough);

  // The range readout beside the slider, in the same seven-segment face as the speed so the two read
  // as one instrument rather than two components that happened to be placed together.
  handles.rangeDigits = [];
  for (let at = 0; at < 2; at++) {
    const cell = structure.construct(Figure({
      category: Category.SegmentCell,
      origin: [ControlX - 0.016 + at * 0.030, -0.072, Forward * 2],
      halfWidth: 0.011, halfHeight: 0.019,
      scalarAlpha: 10,
      scalarBeta: 0.0042,
      palette: Slot.Accent,
      overlay: true,
      orderingRank: 9,
    }));
    structure.attach(cell, face);
    handles.rangeDigits.push(cell);
  }
  text(structure, face, 'PCT', { x: ControlX + 0.036, y: -0.072, z: Forward * 2,
                                 size: 0.0090, align: 'left', palette: Slot.MarkingMute, rank: 10 });

  // ── the strips: title above, telltales below ───────────────────────────────────────────────────
  text(structure, face, 'FRONTIER', { x: -PanelHalfWidth + FaceInset + 0.012, y: 0.108, z: Forward * 2,
                                      size: 0.0105, palette: Slot.MarkingMute, rank: 10, tracking: 0.9 });
  text(structure, face, 'SPATIAL INTERFACE', { x: PanelHalfWidth - FaceInset - 0.012, y: 0.108, z: Forward * 2,
                                               size: 0.0085, align: 'right', palette: Slot.MarkingMute,
                                               rank: 10, tracking: 0.5, opacity: 0.8 });

  handles.telltales = [];
  const telltaleSlots = [Slot.Confirm, Slot.Caution, Slot.Warning, Slot.Accent];
  for (let at = 0; at < telltaleSlots.length; at++) {
    const lamp = structure.construct(Figure({
      category: Category.Lamp,
      origin: [-PanelHalfWidth + FaceInset + 0.018 + at * 0.026, -0.104, Forward * 2],
      halfWidth: 0.0062, halfHeight: 0.0062,
      palette: telltaleSlots[at],
      scalarAlpha: 1,
      opacity: 0.18,                   // dark until the project lights it
      orderingRank: 11,
    }));
    structure.attach(lamp, face);
    handles.telltales.push(lamp);
  }

  return { structure, handles };
}

// ── values ───────────────────────────────────────────────────────────────────────────────────────
// Everything above is placement. This is the only thing that runs per frame, and it writes floats
// onto figures that already exist — no figure is constructed, destroyed or re-tessellated here. That
// is the whole claim of a retained graph, and it is why the frame cost does not depend on how much
// is moving.

export function assignValues(structure, handles, values) {
  const { speed = 0, boost = 0, regen = 0, sport = 0, time = 0 } = values;

  structure.query(handles.gaugeFill).scalarAlpha = Math.min(Math.max(boost, 0), 1);
  structure.query(handles.needle).scalarAlpha = Math.min(Math.max(boost, 0), 1);

  // 🔴 A blank leading digit, not a zero. "007 KM/H" is a clock, not a speedometer, and the engine
  //    gives blank its own code (10) rather than leaving it to be faked with an unlit colour.
  const shown = Math.round(Math.min(Math.max(speed, 0), 999));
  const letters = String(shown).padStart(3, ' ');
  for (let at = 0; at < 3; at++) {
    const letter = letters[at];
    structure.query(handles.digits[at]).scalarAlpha = letter === ' ' ? 10 : Number(letter);
  }

  const percent = Math.round(Math.min(Math.max(regen, 0), 1) * 99);
  const pair = String(percent).padStart(2, ' ');
  for (let at = 0; at < 2; at++) {
    structure.query(handles.rangeDigits[at]).scalarAlpha = pair[at] === ' ' ? 10 : Number(pair[at]);
  }

  // The slider: the fill grows from the left end, so its half width is half the travel and its
  // centre sits at half of that — the arithmetic a bar needs when it is drawn from its middle.
  const span = handles.troughHalfWidth;
  const travel = Math.min(Math.max(regen, 0), 1) * span * 2;
  const fill = structure.query(handles.sliderFill);
  fill.halfWidth = Math.max(travel * 0.5, 0.001);
  fill.origin[0] = -span + travel * 0.5;
  structure.query(handles.sliderKnob).origin[0] = -span + travel;

  const engaged = Math.min(Math.max(sport, 0), 1);
  structure.query(handles.toggleKnob).origin[0] = -0.0145 + engaged * 0.029;
  structure.query(handles.toggleGlow).opacity = engaged;

  // The telltales breathe rather than blink: a cluster that flashes is reporting a fault, and this
  // one is not. Each runs at its own rate so the row never pulses in unison.
  handles.telltales.forEach((lamp, at) => {
    const pulse = 0.5 + 0.5 * Math.sin(time * (0.7 + at * 0.21) + at * 1.7);
    structure.query(lamp).opacity = 0.14 + 0.5 * pulse * (at === 0 ? engaged : 1);
  });
}
