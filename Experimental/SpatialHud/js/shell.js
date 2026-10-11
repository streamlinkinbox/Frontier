// FRONTIER OS — the tablet's home screen.
//
// The dashboard in layout.js answers "what is this car doing". This file answers the question a
// tablet actually asks when you pick it up: "what do you want to open". It is the same layer —
// a project-side composition, not an engine feature — and it is built out of exactly the figures
// the engine already ships: rounded rectangles, arcs, tick rings, needles, discs and stroke glyphs.
//
// 🔴 THE BACKDROP IS NOW THE WALLPAPER. In layout.js the streak field was the subject and the
//    instruments sat on top of it. Here it is what it is on every real device: the thing behind
//    the icons, dimmed so it never competes with a label. It is still an Illuminant, so it still
//    pools light on the desk — a wallpaper you cannot see in a dark room is a sticker.
//
// The reference is streamlinkinbox/Frontier arena/01a06c6c-frontier app/index.html, a browser
// mock-up of this same screen: status bar, clock, search, an app wall and a dock. That mock-up is
// portrait CSS on a 520 px column. A tablet face is 0.444 x 0.254 m — landscape, 1.75:1 — so the
// column becomes a left-hand band and the app wall takes the room that frees up. Nothing here is
// reusable and nothing here is meant to be (CLAUDE.md §6).

import { Category, Slot, Structure, Figure } from './figures.js';
import { PanelHalfWidth, PanelHalfHeight, text } from './layout.js';

const FaceInset = 0.008;                 // [m] the bezel's visible width, as layout.js has it
const Forward = 0.0010;                  // [m] one layer off the one below

// The face, inset by a margin the way every tablet shell keeps its content off the glass edge.
const Margin = 0.014;                    // [m]

// WEDNESDAY 09 SEPTEMBER — the longest date the calendar can produce, and therefore the number of
// glyph figures the date run has to own from the start.
const DateWidth = 22;
const EdgeX = PanelHalfWidth - FaceInset - Margin;     // 0.208
const EdgeY = PanelHalfHeight - FaceInset - Margin;    // 0.113

// ── the three bands ──────────────────────────────────────────────────────────────────────────────
const StatusY = 0.1055;                  // the status strip, hard against the top
const RuleY = 0.0920;                    // the hairline under it

const ColumnRight = -0.0620;             // the left band ends here; the app wall begins at WallLeft
const WallLeft = -0.0460;

const Columns = 4;
const WallPitch = (EdgeX - WallLeft) / Columns;        // 0.0635
const TileHalf = 0.0215;                 // [m] the icon plate — 43 mm on a 444 mm face
const RowY = [0.0420, -0.0320];          // the two rows of the app wall
const LabelDrop = TileHalf + 0.0098;     // label baseline below the plate

const DockY = -0.0940;
const DockHalf = [0.1180, 0.0175];
const DockPitch = 0.0660;
const DockTileHalf = 0.0125;

// ── the apps ─────────────────────────────────────────────────────────────────────────────────────
// `live` is the real distinction a home screen makes: an app you can open is a lit plate, an app
// that is not built yet is a dark one. The reference calls the second kind "soon" and refuses the
// tap; here it simply does not light.
const ShellApps = [
  { name: 'LOBBY',    mark: 'lobby',    live: true,  dot: true  },
  { name: 'APPS',     mark: 'apps',     live: true,  dot: false },
  { name: 'CHAT',     mark: 'chat',     live: true,  dot: true  },
  { name: 'STORE',    mark: 'store',    live: true,  dot: false },
  { name: 'WALLET',   mark: 'wallet',   live: true,  dot: false },
  { name: 'DRIVE',    mark: 'drive',    live: true,  dot: false },
  { name: 'GARAGE',   mark: 'garage',   live: false, dot: false },
  { name: 'SETTINGS', mark: 'settings', live: false, dot: false },
];

// The dock holds the four the player reaches for, which are the four named in the brief.
const DockApps = ['lobby', 'chat', 'store', 'wallet'];

// ── icon marks ───────────────────────────────────────────────────────────────────────────────────
// Every mark is built from engine primitives at a scale relative to the plate it sits on, so the
// same eight marks draw correctly at 43 mm on the wall and at 25 mm in the dock without a second
// set of numbers. `s` is the plate's half extent; a mark reads in fractions of it.

// 🔴 A PIXEL ON A SCREEN IS EMITTED, NOT REFLECTED.
//
//    The first cut built every plate with emissiveWeight 0 — "albedo, lit by the room" — copying
//    the dashboard, where the face really is a card the room shines on. On a running display that
//    is wrong, and the still proved it: InterfaceRaster.frag computes
//        Colour = mix(Received, Emitted, EmissiveWeight)
//    so a plate at weight 0 shows ONLY the room light bouncing off it. Inside the display target
//    there is barely any room light, so the dock bar, the search pill and the link strip all came
//    out at 2.5/255 against a 2.0/255 face. Measured, not guessed: a horizontal scan across the
//    dock read 2.47 … 3.26 where bare face read 2.00. One part in 255 is not a panel, it is
//    nothing — and that is exactly what the render showed.
//
//    A screen showing #202020 is EMITTING #202020. So every plate here is an emitter, the palette
//    below is an honest sRGB → linear conversion of the reference's hexes, and the chrome lights
//    the desk the way a real tablet does.
function plate(structure, parent, origin, half, radius, palette, extra = {}) {
  const index = structure.construct(Figure(Object.assign({
    category: Category.Surface,
    origin,
    halfWidth: half[0], halfHeight: half[1],
    cornerRadius: radius,
    palette,
    emissiveWeight: 1,
    orderingRank: 20,
  }, extra)));
  structure.attach(index, parent);
  return index;
}

const Hairline = 0.00045;                // [m] the width a 1 px CSS border comes out at here

function framedPlate(structure, parent, origin, half, radius, palette, extra = {}) {
  const rule = plate(structure, parent, origin, [half[0] + Hairline, half[1] + Hairline],
                     radius + Hairline, Slot.Stroke,
                     Object.assign({ orderingRank: (extra.orderingRank ?? 20) - 1 }, {}));
  const inner = plate(structure, parent, [origin[0], origin[1], origin[2] + Forward * 0.2],
                      half, radius, palette, extra);
  return { rule, inner };
}

function bar(structure, parent, x, y, hw, hh, palette, rank, radius = 0) {
  const index = structure.construct(Figure({
    category: Category.Surface,
    origin: [x, y, Forward * 0.5],
    halfWidth: hw, halfHeight: hh,
    cornerRadius: radius,
    palette,
    overlay: true,
    orderingRank: rank,
  }));
  structure.attach(index, parent);
  return index;
}

function disc(structure, parent, x, y, r, palette, rank, opacity = 1) {
  const index = structure.construct(Figure({
    category: Category.Lamp,
    origin: [x, y, Forward * 0.5],
    halfWidth: r, halfHeight: r,
    scalarAlpha: 1,
    palette,
    overlay: true,
    opacity,
    orderingRank: rank,
  }));
  structure.attach(index, parent);
  return index;
}

function inscribeMark(structure, parent, kind, s, rank,
                      ink = Slot.Marking, hole = Slot.Accent) {
  const z = Forward * 0.6;
  const made = [];
  const keep = (index) => { made.push(index); return index; };

  switch (kind) {
    // A pennant on a staff — the lobby you stand in before a race.
    case 'lobby':
      keep(bar(structure, parent, -0.46 * s, 0, 0.055 * s, 0.62 * s, ink, rank));
      keep(structure.construct(Figure({
        category: Category.Surface, origin: [0.10 * s, 0.26 * s, z],
        halfWidth: 0.46 * s, halfHeight: 0.28 * s, cornerRadius: 0.05 * s,
        palette: ink, overlay: true, orderingRank: rank,
      })));
      structure.attach(made[made.length - 1], parent);
      break;

    // Four squares: the universal "everything you have" mark.
    case 'apps':
      for (const sx of [-1, 1]) {
        for (const sy of [-1, 1]) {
          keep(bar(structure, parent, sx * 0.31 * s, sy * 0.31 * s,
                   0.22 * s, 0.22 * s, ink, rank, 0.06 * s));
        }
      }
      break;

    // A speech bubble: a rounded plate with a tail dropped off its lower left.
    case 'chat':
      keep(bar(structure, parent, 0, 0.10 * s, 0.62 * s, 0.44 * s, ink, rank, 0.20 * s));
      keep(disc(structure, parent, -0.30 * s, -0.40 * s, 0.11 * s, ink, rank));
      break;

    // A bag: a body with an arc for a handle. The engine's arc opens at the TOP and sweeps the
    // bottom, so a handle is that same arc turned half a revolution.
    case 'store':
      keep(bar(structure, parent, 0, -0.16 * s, 0.56 * s, 0.40 * s, ink, rank, 0.10 * s));
      keep(structure.construct(Figure({
        category: Category.Arc, origin: [0, 0.20 * s, z],
        rotationZ: 3.6651914,                  // -kArcStart: the sweep now begins at three o'clock
        halfWidth: 0.26 * s, halfHeight: 0.26 * s,
        scalarAlpha: 0.75, scalarBeta: 0.075 * s,   // 0.75 of kArcSweep is a clean half turn
        palette: ink, overlay: true, orderingRank: rank,
      })));
      structure.attach(made[made.length - 1], parent);
      break;

    // A card with a magnetic stripe and a contact.
    case 'wallet':
      keep(bar(structure, parent, 0, 0, 0.66 * s, 0.46 * s, ink, rank, 0.12 * s));
      keep(bar(structure, parent, 0, 0.14 * s, 0.66 * s, 0.075 * s, hole, rank + 1));
      keep(disc(structure, parent, 0.38 * s, -0.16 * s, 0.09 * s, hole, rank + 1));
      break;

    // 🔴 The drive app's mark IS the dashboard, in miniature, drawn by the same Arc and Needle
    //    categories layout.js uses for the real one. An icon that is a screenshot would be a lie;
    //    this one is the instrument at 4 mm.
    case 'drive':
      keep(structure.construct(Figure({
        category: Category.Arc, origin: [0, -0.12 * s, z],
        halfWidth: 0.62 * s, halfHeight: 0.62 * s,
        scalarAlpha: 0.78, scalarBeta: 0.075 * s,
        palette: ink, overlay: true, orderingRank: rank,
      })));
      structure.attach(made[made.length - 1], parent);
      keep(structure.construct(Figure({
        category: Category.Needle, origin: [0, -0.12 * s, z + Forward * 0.2],
        halfWidth: 0.50 * s, halfHeight: 0.50 * s,
        scalarAlpha: 0.66, scalarBeta: 0.075 * s,
        palette: ink, overlay: true, orderingRank: rank + 1,
      })));
      structure.attach(made[made.length - 1], parent);
      break;

    // A car in profile: body, cabin, two wheels.
    case 'garage':
      keep(bar(structure, parent, 0, -0.10 * s, 0.70 * s, 0.20 * s, ink, rank, 0.09 * s));
      keep(bar(structure, parent, 0, 0.18 * s, 0.42 * s, 0.19 * s, ink, rank, 0.08 * s));
      keep(disc(structure, parent, -0.44 * s, -0.34 * s, 0.12 * s, ink, rank));
      keep(disc(structure, parent, 0.44 * s, -0.34 * s, 0.12 * s, ink, rank));
      break;

    // 🔴 A cog is a tick ring. The engine already repeats radial marks around a circle for the
    //    gauge; eight of them with a hub is the settings icon, at no new category.
    case 'settings':
      keep(structure.construct(Figure({
        category: Category.TickRing, origin: [0, 0, z],
        halfWidth: 0.68 * s, halfHeight: 0.68 * s,
        scalarBeta: 8,
        palette: ink, overlay: true, orderingRank: rank,
      })));
      structure.attach(made[made.length - 1], parent);
      keep(disc(structure, parent, 0, 0, 0.42 * s, ink, rank + 1));
      keep(disc(structure, parent, 0, 0, 0.17 * s, hole, rank + 2));
      break;
  }
  return made;
}

// A label that is wider than its column is a label that collides with the next one. The stroke font
// advances at a known rate, so the fit is arithmetic rather than a guess.
function fitSize(words, room, wanted, tracking = 0.30) {
  const span = (size) => (size * 0.58 + size * tracking * 0.42) * (words.length - 1) + size * 0.58;
  return span(wanted) <= room ? wanted : wanted * room / span(wanted);
}

// ── the composition ──────────────────────────────────────────────────────────────────────────────

export function constructShellLayout() {
  const structure = new Structure();

  // FRONTIER's own palette, converted from the reference's sRGB hexes to linear, because the
  // renderer works in linear and a hex pasted straight in comes out washed.
  //   --blue #1f5eff · --bg #0a0a0a · --panel #121212 · --inset #1a1a1a · --dim #8a8a8a
  // 🔴 MEASURED, NOT PASTED. The first attempt converted #0a0a0a / #121212 / #1a1a1a straight to
  //    linear and the dock, the search pill and the link strip all came out invisible — the still
  //    showed four icons floating on nothing. sRGB #121212 is 0.0061 linear, and 0.0061 against a
  //    0.0030 face is a difference no tone curve is going to hand back. A panel that is meant to
  //    READ as a raised surface has to be lifted until it does, so these are the browser's
  //    RELATIONSHIPS at luminances that survive the display pass.
  // 🔴 DISPLAY VALUES, NOT LINEAR ONES. The swap chain is bgra8unorm — NOT bgra8unorm-srgb — so
  //    whatever a figure writes is what the screen shows, unconverted, and RenderStill.resolve()
  //    deliberately matches that rather than flattering it. The second attempt here converted the
  //    reference's hexes sRGB → linear, which is the right maths for a renderer that encodes on
  //    the way out and the wrong maths for this one: #202020 became 0.0145 and the dock went from
  //    invisible to very slightly less invisible. These are the hexes as fractions, directly.
  //      #0a0a0a screen · #202020 panel · #161616 field · #2f2f2f hairline · #8a8a8a dim
  structure.palette[Slot.Housing] = [0.0250, 0.0250, 0.0290, 1.0];
  structure.palette[Slot.Surface] = [0.1250, 0.1250, 0.1370, 1.0];
  structure.palette[Slot.SurfaceSunk] = [0.0780, 0.0780, 0.0880, 1.0];
  structure.palette[Slot.Stroke] = [0.1850, 0.1850, 0.1980, 1.0];
  structure.palette[Slot.Marking] = [0.9200, 0.9300, 0.9500, 1.0];
  structure.palette[Slot.MarkingMute] = [0.4600, 0.4650, 0.4850, 1.0];
  structure.palette[Slot.Accent] = [0.1220, 0.3690, 1.0000, 1.0];
  structure.palette[Slot.Confirm] = [0.1800, 0.8200, 0.3600, 1.0];
  structure.tokenRadius = 0.005;

  const handles = {};

  // ── housing ────────────────────────────────────────────────────────────────────────────────────
  // Bolt upright, exactly as layout.js builds it. The lean belongs to the scene, never to the
  // authoring frame — app.js and RenderStill both apply it afterwards.
  const housing = structure.construct(Figure({
    category: Category.Surface,
    rotationX: Math.PI / 2,
    halfWidth: PanelHalfWidth, halfHeight: PanelHalfHeight,
    cornerRadius: 0.020,
    palette: Slot.Housing,
    emissiveWeight: 0,
    orderingRank: 0,
  }));
  handles.housing = housing;

  const face = structure.construct(Figure({
    category: Category.Surface,
    origin: [0, 0, Forward],
    halfWidth: PanelHalfWidth - FaceInset, halfHeight: PanelHalfHeight - FaceInset,
    cornerRadius: 0.014,
    palette: Slot.Housing,
    emissiveWeight: 1,            // the screen itself: a black panel emitting almost nothing
    orderingRank: 1,
  }));
  structure.attach(face, housing);
  handles.face = face;

  // ── the wallpaper ──────────────────────────────────────────────────────────────────────────────
  // 🔴 Full bleed and dim. On the dashboard this field was the subject at 0.85; here it is behind
  //    eight labels and a clock, so it runs at less than half that. It stays an Illuminant: the
  //    wallpaper is the only thing lighting the desk when the room goes down.
  handles.streaks = structure.construct(Figure({
    category: Category.StreakField,
    origin: [0, 0, Forward * 0.6],
    halfWidth: PanelHalfWidth - FaceInset - 0.002,
    halfHeight: PanelHalfHeight - FaceInset - 0.002,
    palette: Slot.Accent,
    emissiveWeight: 1,
    opacity: 0.38,
    orderingRank: 2,
    //        strands  amplitude  waves  speed | tail  seed  intensity  core
    streak: [22, 0.022, 2.2, 0.38, 0.55, 11, 0.85, 0.0026],
  }));
  structure.attach(handles.streaks, face);

  // ── the status strip ───────────────────────────────────────────────────────────────────────────
  handles.statusClock = text(structure, face, '00:00', {
    x: -EdgeX, y: StatusY, z: Forward * 2, size: 0.0092, palette: Slot.Marking, rank: 30,
    keepSpaces: true,
  });

  // Signal, as three rising bars. A real status bar is three rectangles and nobody has ever needed
  // it to be anything else.
  for (let at = 0; at < 3; at++) {
    const height = 0.0022 + at * 0.0018;
    bar(structure, face, 0.1700 + at * 0.0052, StatusY - 0.0040 + height, 0.0017, height,
        Slot.Marking, 30);
  }
  // Reception, as the arcs a radiating source actually draws.
  for (let at = 0; at < 2; at++) {
    const ring = structure.construct(Figure({
      category: Category.Arc,
      origin: [0.1900, StatusY - 0.0050, Forward * 2],
      rotationZ: Math.PI,
      halfWidth: 0.0030 + at * 0.0026, halfHeight: 0.0030 + at * 0.0026,
      scalarAlpha: 0.30, scalarBeta: 0.0011,
      palette: Slot.Marking, overlay: true, orderingRank: 30,
    }));
    structure.attach(ring, face);
  }
  disc(structure, face, 0.1900, StatusY - 0.0046, 0.0012, Slot.Marking, 30);

  // Charge, as a cell and a cap.
  const cell = plate(structure, face, [0.2020, StatusY, Forward * 2], [0.0092, 0.0045], 0.0016,
                     Slot.Stroke, { overlay: true, orderingRank: 30 });
  bar(structure, cell, -0.0010, 0, 0.0068, 0.0029, Slot.Marking, 31, 0.0008);
  bar(structure, face, 0.2128, StatusY, 0.0012, 0.0019, Slot.Stroke, 30, 0.0006);

  const rule = structure.construct(Figure({
    category: Category.Surface,
    origin: [0, RuleY, Forward * 1.6],
    halfWidth: EdgeX, halfHeight: 0.00022,
    palette: Slot.Stroke,
    overlay: true,
    orderingRank: 29,
  }));
  structure.attach(rule, face);

  // ── the left band: clock, identity, search, link ───────────────────────────────────────────────
  // Built at the widest date there is — WEDNESDAY 09 SEPTEMBER — so the run has a figure for every
  // character any date can need, and the size is chosen once against the band it has to fit.
  handles.date = text(structure, face, ''.padEnd(DateWidth, ' '), {
    x: -EdgeX, y: 0.0680, z: Forward * 2,
    size: fitSize(''.padEnd(DateWidth, ' '), 0.146, 0.0080),
    palette: Slot.MarkingMute, rank: 30, keepSpaces: true,
  });

  // 🔴 Glyphs, not seven-segment cells. The dashboard's speed is an instrument and reads correctly
  //    as segments; a lock-screen clock is typography, and the stroke font is the typography the
  //    engine has. Same category the labels use, four times the size.
  handles.clock = text(structure, face, '00:00', {
    x: -EdgeX, y: 0.0350, z: Forward * 2, size: 0.0300, weight: 0.055, tracking: 0.12,
    palette: Slot.Marking, rank: 31, keepSpaces: true,
  });

  text(structure, face, 'FRONTIER OS', {
    x: -EdgeX, y: 0.0080, z: Forward * 2, size: 0.0078, palette: Slot.Accent, rank: 30,
  });

  // Search: an inset pill with a lens and a prompt, the first control on the reference screen.
  const search = framedPlate(structure, face, [-0.1350, -0.0225, Forward * 2], [0.0730, 0.0128],
                             0.0128, Slot.SurfaceSunk, { orderingRank: 24 }).inner;
  const lens = structure.construct(Figure({
    category: Category.Arc,
    origin: [-0.0610, 0.0004, Forward * 0.5],
    halfWidth: 0.0036, halfHeight: 0.0036,
    scalarAlpha: 1.0, scalarBeta: 0.0009,
    palette: Slot.MarkingMute, overlay: true, orderingRank: 25,
  }));
  structure.attach(lens, search);
  const handle = structure.construct(Figure({
    category: Category.Surface,
    origin: [-0.0558, -0.0044, Forward * 0.5],
    rotationZ: -Math.PI / 4,
    halfWidth: 0.0021, halfHeight: 0.0008,
    palette: Slot.MarkingMute, overlay: true, orderingRank: 25,
  }));
  structure.attach(handle, search);
  text(structure, search, 'SEARCH APPS', {
    x: -0.0500, y: 0, z: Forward * 0.6, size: 0.0070, palette: Slot.MarkingMute, rank: 25,
  });

  // The link strip. The reference keeps its EOS calls on screen because the whole mock exists to
  // show what the backend is doing; the same honesty is worth keeping.
  const link = framedPlate(structure, face, [-0.1350, -0.0550, Forward * 2], [0.0730, 0.0112],
                           0.0112, Slot.Surface, { orderingRank: 24 }).inner;
  handles.linkLamp = disc(structure, link, -0.0600, 0, 0.0028, Slot.Confirm, 25);
  text(structure, link, 'EOS CONNECTED', {
    x: -0.0530, y: 0, z: Forward * 0.6, size: 0.0068, palette: Slot.MarkingMute, rank: 25,
  });

  // ── the app wall ───────────────────────────────────────────────────────────────────────────────
  text(structure, face, 'APPS', {
    x: WallLeft, y: 0.0750, z: Forward * 2, size: 0.0078, palette: Slot.MarkingMute, rank: 30,
  });
  text(structure, face, '6 INSTALLED', {
    x: EdgeX, y: 0.0750, z: Forward * 2, size: 0.0070, align: 'right',
    palette: Slot.MarkingMute, rank: 30,
  });

  handles.tiles = [];
  for (let at = 0; at < ShellApps.length; at++) {
    const app = ShellApps[at];
    const column = at % Columns, row = (at / Columns) | 0;
    const x = WallLeft + WallPitch * (column + 0.5);
    const y = RowY[row];

    // A live plate is the accent colour and carries its own light; a plate for something unbuilt is
    // the sunk surface and carries none. That difference is the whole state of a home screen.
    const tile = plate(structure, face, [x, y, Forward * 2], [TileHalf, TileHalf], 0.0062,
                       app.live ? Slot.Accent : Slot.SurfaceSunk, {
      emissiveWeight: 1,
      opacity: app.live ? 1 : 0.85,
      orderingRank: 20,
    });
    inscribeMark(structure, tile, app.mark, TileHalf, 22,
                 app.live ? Slot.Marking : Slot.MarkingMute,
                 app.live ? Slot.Accent : Slot.SurfaceSunk);
    if (app.dot) disc(structure, tile, TileHalf * 0.74, TileHalf * 0.74, 0.0030, Slot.Marking, 26);

    text(structure, face, app.name, {
      x, y: y - LabelDrop, z: Forward * 2,
      size: fitSize(app.name, WallPitch * 0.94, 0.0082), align: 'centre',
      palette: app.live ? Slot.Marking : Slot.MarkingMute, rank: 30,
    });
    handles.tiles.push(tile);
  }

  // ── the dock ───────────────────────────────────────────────────────────────────────────────────
  const dock = framedPlate(structure, face, [0, DockY, Forward * 2], DockHalf, DockHalf[1],
                           Slot.Surface, { orderingRank: 20 }).inner;
  handles.dock = dock;
  for (let at = 0; at < DockApps.length; at++) {
    const app = ShellApps.find((each) => each.mark === DockApps[at]);
    const x = DockPitch * (at - (DockApps.length - 1) / 2);
    const tile = plate(structure, dock, [x, 0, Forward * 0.6],
                       [DockTileHalf, DockTileHalf], 0.0038, Slot.Accent, {
      orderingRank: 22,
    });
    inscribeMark(structure, tile, app.mark, DockTileHalf, 24, Slot.Marking, Slot.Accent);
  }

  // The home indicator, the one piece of furniture every tablet keeps below the dock.
  bar(structure, face, 0, -0.1200, 0.0300, 0.0009, Slot.MarkingMute, 30, 0.0009);

  return { structure, handles };
}

// ── per frame ────────────────────────────────────────────────────────────────────────────────────
// 🔴 The only per-frame code, and it writes floats onto figures that already exist. A home screen
//    that rebuilt its wall every frame would be re-tessellating eight icons to move a colon.

const Weekdays = ['SUNDAY', 'MONDAY', 'TUESDAY', 'WEDNESDAY', 'THURSDAY', 'FRIDAY', 'SATURDAY'];
const Months = ['JANUARY', 'FEBRUARY', 'MARCH', 'APRIL', 'MAY', 'JUNE',
                'JULY', 'AUGUST', 'SEPTEMBER', 'OCTOBER', 'NOVEMBER', 'DECEMBER'];

// text() emits one figure per character and draws nothing for a space, so a run of glyph handles
// lines up with the non-space characters of the words it was built from — and only with those.
function respell(structure, glyphs, words) {
  for (let at = 0; at < glyphs.length; at++) {
    // Past the end of the new words is a space, not the character that used to be there.
    structure.query(glyphs[at]).scalarAlpha = at < words.length ? words.charCodeAt(at) : 32;
  }
}

export function assignShellValues(structure, handles, values) {
  const { clock = new Date(), time = 0 } = values;

  const hour = String(clock.getHours()).padStart(2, '0');
  const minute = String(clock.getMinutes()).padStart(2, '0');
  // The colon blinks on the half second, which is the one thing a clock is allowed to animate.
  const divider = (time % 1) < 0.5 ? ':' : ' ';
  respell(structure, handles.clock, `${hour}${divider}${minute}`);
  respell(structure, handles.statusClock, `${hour}:${minute}`);

  const date = `${Weekdays[clock.getDay()]} ${String(clock.getDate()).padStart(2, '0')} `
             + `${Months[clock.getMonth()]}`;
  respell(structure, handles.date, date.padEnd(DateWidth, ' '));

  // The link lamp breathes rather than sits, so a screen with nothing happening on it is still
  // obviously running.
  structure.query(handles.linkLamp).opacity = 0.62 + 0.38 * (0.5 + 0.5 * Math.sin(time * 2.2));
}

export { ShellApps };
