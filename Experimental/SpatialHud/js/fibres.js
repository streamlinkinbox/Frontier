// The Live rung of the backdrop ladder: the particle editor's actual light fibres, as real 3D
// geometry in the volume behind the glass.
//
// 🔴 WHY THIS EXISTS WHEN js/streaks.js ALREADY DREW STREAKS.
//
//    It does not, really — it drew a picture of streaks. The analytic field is evaluated in the
//    panel's own plane, so every strand is at the same depth, there is no parallax, no perspective
//    and no occlusion between strands. Tilt the tablet and a flat field slides with the surface
//    instead of swimming behind it, and the eye reads that immediately as paint rather than as
//    light in a volume. That is not a tuning problem; it is what "flat" means, and no amount of
//    parameter work fixes it.
//
// 🔴 WHY IT IS STILL NOT A RENDER TARGET.
//
//    The obvious way to put a 3D world behind a UI panel is to render it offscreen and sample the
//    texture. That is a sampler binding, a texture lifetime, a second pass, and a result a
//    reflection ray cannot cheaply ask about — the objection InterfacePanelSample.slang already
//    makes. But the engine has the mechanism a portal actually needs: the shader clip, "a rounded
//    rectangle in the figure's own plane ... works under any transform, unlike a scissor rectangle".
//
//    So the fibres are drawn in the SAME render pass, in the panel's own local space, and clipped by
//    the panel's rounded rectangle in the fragment shader. Own pipeline, own vertex program, own
//    geometry — a custom raster in every way that matters — and no offscreen target.
//
//    An offscreen target earns its cost for exactly three things, none of which is "3D": bloom, a
//    camera of its own, or a resolution and refresh rate decoupled from the panel's. Bloom is the
//    real one, and part of why the editor's own streaks look as good as they do. It should be added
//    when it is wanted, not pre-emptively.
//
// 🔴 WHAT IS PORTED AND WHAT IS NOT.
//
//    Ported verbatim from Experimental/ParticleEditor/js/shaders.js: fbHash, fbUnit, fbLocalAxis,
//    fbBezier, fbPeriodicGap, fbPulseLevel, fbSweep, fbPulse, fbStopPos, fbRamp, fbColourAt, and
//    both the fibre and spark stages. The uniform keeps the editor's own slot numbering so the two
//    can be read side by side.
//
//    NOT ported: the ribbon and trail shapes. A trail needs the arc-length path table as a storage
//    buffer, and a ribbon is a sheet rather than a volume. Shape is streak, and asking for another
//    one is a hard failure in the packer rather than a silent fallback to streak.
//
//    Two things are new, because a fibre in a panel is not a fibre in a scene: the curve is built in
//    PANEL-LOCAL metres and then carried to the world by the panel's own transform rows, so moving
//    the tablet moves the light inside it; and a depth fade dims strands further behind the glass,
//    which is the cue that sells the volume.

import { PanelHalfWidth, PanelHalfHeight } from './layout.js';

// The light-streaks preset, in panel metres. The preset is authored at scene scale — 12 m long on a
// 0.46 m panel — so `scale` is the one number that had to change, and everything else is the
// preset's own.
export const StreakPreset = {
  shape: 'streak',
  strands: 150, segments: 56, seed: 11, loopSeconds: 12,
  scale: 0.033,                   // [m per curve unit] 12 curve units -> 0.40 m, just under the face
  length: 12, spread: 1.1, amplitude: 1.5, frequency: 1,
  thickness: 1.5, taper: 0.6,
  intensity: 1.15, halo: 0.12, baseline: 0.1,
  window: 0.55, windowCycles: 1,
  accentMix: 0.25,
  sparks: 0.8, sparkSize: 0.02, sparkBrightness: 3,
  pulseRate: 1, pulseDepth: 0.25, pulseShape: 1,        // 1 = Breathe
  colourMode: 1,                                        // 1 = ramp
  stops: [[0.094, 0.706, 1.0], [0.37, 0.85, 1.0]],
  accent: [0.92, 1.0, 1.0],

  // Panel placement. Curve space is (along, lateral, lift); the editor's world map sends lateral to
  // DEPTH and lift to UP, which is what puts the spread into the volume instead of across the face.
  // 🔴 spread and amplitude are BELOW the preset's (1.2 and 2.4). Not taste: a cubic Bézier lies in
  //    the convex hull of its control points, and at the preset's values that hull reaches about
  //    0.14 m perpendicular to the axis — which, mapped to depth, put strands ten centimetres IN
  //    FRONT of the glass, glowing in the room outside the tablet. CheckHud bounds the hull and
  //    fails if the volume ever escapes forward again.
  origin: [-0.185, -0.035, -0.080],                     // [m] panel-local, z negative = behind glass
  direction: [1.0, -0.10, 0.22],                        // curve space
  depthFade: 0.110,                                     // [m] e-folding distance behind the glass
  glassFade: 0.015,                                     // [m] band in which a strand dims into the glass

  // 🔴 THE VOLUME BEHIND A PANEL IS A SLAB, NOT A CYLINDER.
  //
  //    fbBezier scatters its root and reach on a DISC perpendicular to the axis, so one `spread`
  //    sets the extent in both perpendicular directions at once. In a scene that is right. Behind
  //    a tablet it is not: the volume has a hundred and twenty millimetres of height to fill and
  //    perhaps thirty of depth before a strand is through the glass. With one number the two fight,
  //    and the fight is what kept the Live rung to a narrow band across the middle while the Field
  //    rung covered the whole face — raising spread to fill the height pushed strands out the
  //    front, which is exactly what the hull bound caught the first time.
  //
  //    So the cross-section is shaped where the curve meets the PANEL rather than in the curve: the
  //    disc is stretched along panel up and squashed along panel depth. fbBezier stays the
  //    editor's, verbatim; fbPanel was already the function that exists because a fibre in a panel
  //    is not a fibre in a scene, and this is the same thought.
  liftGain: 1.70,                                       // [-] panel up, where there is room
  depthGain: 0.55,                                      // [-] panel depth, where there is not
};

export const FibreFloats = 108;                         // 27 vec4s
export const MaxStops = 8;

// Mirrors PE.fillFibre's slot numbering for slots 0..21, then appends what a panel needs.
export function packFibre(out, preset, view) {
  if (preset.shape !== 'streak') {
    throw new Error(`packFibre: only the streak shape is ported; asked for "${preset.shape}"`);
  }
  out.fill(0);
  const put = (slot, a, b, c, d) => out.set([a, b, c, d], slot * 4);

  const d = preset.direction;
  const dl = Math.hypot(d[0], d[1], d[2]) || 1;
  const loop = (view.time / preset.loopSeconds) % 1;

  put(0, preset.origin[0], preset.origin[1], preset.origin[2], preset.scale);
  put(1, d[0] / dl, d[1] / dl, d[2] / dl, 0);
  put(2, preset.length, preset.spread, preset.amplitude, preset.frequency);
  put(3, 0, 0, 0, 0);                                   // the ribbon block, unused by a streak
  put(4, 0, loop, preset.thickness, preset.taper);
  put(5, preset.intensity, preset.halo, preset.baseline, preset.window);
  put(6, preset.accentMix, preset.pulseDepth, view.projectionScale, view.height / 1080);
  put(7, 1, preset.windowCycles, preset.pulseRate, preset.pulseShape);
  put(8, 0, preset.strands, preset.segments, preset.seed);
  put(9, 0, preset.sparks, preset.sparkSize, preset.sparkBrightness);

  const stops = preset.stops.slice(0, MaxStops);
  put(10, view.width, view.height, stops.length, preset.colourMode);
  put(11, preset.accent[0], preset.accent[1], preset.accent[2], 0);
  stops.forEach((colour, at) => out.set([colour[0], colour[1], colour[2], 1], (12 + at) * 4));
  for (let at = 0; at < MaxStops; at++) {
    const position = stops.length > 1 ? Math.min(at, stops.length - 1) / (stops.length - 1) : 0;
    out[(20 + (at >> 2)) * 4 + (at & 3)] = at < stops.length ? position : 1;
  }

  // The panel's world rows, so the light moves with the tablet.
  out.set(view.rows[0], 22 * 4);
  out.set(view.rows[1], 23 * 4);
  out.set(view.rows[2], 24 * 4);
  // The clip: the face's rounded rectangle, and the depth fade.
  put(25, PanelHalfWidth - 0.012, PanelHalfHeight - 0.012, 0.014, preset.depthFade);
  put(26, preset.glassFade, preset.liftGain, preset.depthGain, 0);
  return out;
}

export const FIBRE_WGSL = String.raw`
//------------------------------------------------------------------------------------------------------------------------
//                                              LIGHT FIBRES, IN A PANEL
//------------------------------------------------------------------------------------------------------------------------
// Ported from Experimental/ParticleEditor/js/shaders.js. Slot numbering is the editor's.

struct Fib {
  org   : vec4f,    // xyz panel-local origin [m], w scale [m per curve unit]
  dir   : vec4f,    // xyz streak axis in curve space
  p0    : vec4f,    // x length, y spread, z amplitude, w frequency
  p1    : vec4f,    // (the ribbon block; a streak does not read it)
  p2    : vec4f,    // x trail length, y loop fraction, z thickness px, w taper
  p3    : vec4f,    // x intensity, y halo, z baseline, w light window
  p4    : vec4f,    // x accent mix, y pulse depth, z projection scale px/m at w=1, w pixel scale
  p5    : vec4f,    // x harmonic, y light heads per loop, z pulse rate per loop, w pulse shape
  p6    : vec4f,    // x shape, y strands, z segments, w seed
  p7    : vec4f,    // x path samples, y spark share, z spark size, w spark brightness
  p8    : vec4f,    // x viewport w px, y viewport h px, z stop count, w colour mode
  colC  : vec4f,    // accent
  stopC : array<vec4f, 8>,
  stopP : array<vec4f, 2>,
  rowX  : vec4f,    // the panel's world rows - the light lives in the tablet, not in the room
  rowY  : vec4f,
  rowZ  : vec4f,
  clip  : vec4f,    // xy face half extent [m], z corner radius [m], w depth fade [m]
  glass : vec4f,    // x the band in which a strand dims into the glass [m]
};

@group(0) @binding(2) var<uniform> FB : Fib;

const FTAU : f32 = 6.283185307;
const FPI  : f32 = 3.14159265;

fn fbHash(k0 : u32) -> u32 {
  var k = k0;
  k ^= k >> 16u;
  k *= 0x7feb352du;
  k ^= k >> 15u;
  k *= 0x846ca68bu;
  k ^= k >> 16u;
  return k;
}

fn fbUnit(id : f32, salt : f32) -> f32 {
  let key = u32(id) * 747796405u + u32(salt) * 2891336453u + u32(FB.p6.w) * 196613u;
  return f32(fbHash(key)) * (1.0 / 4294967296.0);
}

fn fbLocalAxis() -> vec3f {
  let d = FB.dir.xyz;
  return select(vec3f(1.0, 0.0, 0.0), normalize(d), length(d) > 1e-5);
}

// Streak: a cubic Bezier from a root cluster to a reach, wobbling with the loop phase.
fn fbBezier(s : f32, id : f32) -> vec3f {
  let len = FB.p0.x;
  let spread = FB.p0.y;
  let amp = FB.p0.z;
  let k = max(1.0, floor(FB.p0.w + 0.5));
  let axis = fbLocalAxis();
  let up = select(vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, 1.0), abs(axis.y) > 0.9);
  let lateral = normalize(cross(up, axis));
  let normal = cross(axis, lateral);
  let angle = FTAU * fbUnit(id, 1.0);
  let radius = spread * sqrt(fbUnit(id, 2.0));
  let root = (lateral * cos(angle) + normal * sin(angle)) * radius;
  let tipAngle = FTAU * fbUnit(id, 3.0);
  let tipRadius = spread * 1.6 * sqrt(fbUnit(id, 4.0));
  let reach = axis * len + (lateral * cos(tipAngle) + normal * sin(tipAngle)) * tipRadius;
  let wt = FTAU * FB.p2.y * FB.p5.x;
  let c1 = root + axis * (len * 0.34)
         + (lateral * sin(k * wt + FTAU * fbUnit(id, 5.0)) + normal * cos(2.0 * k * wt + FTAU * fbUnit(id, 6.0))) * amp;
  let c2 = reach - axis * (len * 0.34)
         + (lateral * sin(k * wt + FTAU * fbUnit(id, 7.0)) + normal * sin(2.0 * k * wt + FTAU * fbUnit(id, 8.0))) * amp;
  let u = 1.0 - s;
  return u * u * u * root + 3.0 * u * u * s * c1 + 3.0 * u * s * s * c2 + s * s * s * reach;
}

// Curve space (along, lateral, lift) -> the PANEL's plane, metres. The editor sends lift to its
// world up; here it goes to panel up, and lateral goes to DEPTH — which is what puts the strands
// into a volume behind the glass instead of scattering them across the face.
fn fbPanel(local : vec3f) -> vec3f {
  // The disc fbBezier scattered perpendicular to the axis becomes a SLAB here: stretched along
  // panel up, squashed along panel depth. See the note beside liftGain in the preset.
  return FB.org.xyz + vec3f(local.x, local.z * FB.glass.y, local.y * FB.glass.z) * FB.org.w;
}

fn fbWorld(panel : vec3f) -> vec3f {
  return vec3f(dot(FB.rowX.xyz, panel) + FB.rowX.w,
               dot(FB.rowY.xyz, panel) + FB.rowY.w,
               dot(FB.rowZ.xyz, panel) + FB.rowZ.w);
}

fn fbPeriodicGap(a : f32, b : f32) -> f32 {
  let g = abs(fract(a) - fract(b));
  return min(g, 1.0 - g);
}

fn fbPulseLevel(cycle : f32) -> f32 {
  if (i32(FB.p5.w) == 1) {
    let first = exp(-pow(fbPeriodicGap(cycle, 0.10) / 0.045, 2.0));
    let second = 0.55 * exp(-pow(fbPeriodicGap(cycle, 0.27) / 0.055, 2.0));
    return clamp(first + second, 0.0, 1.0);
  }
  return 0.5 - 0.5 * cos(FTAU * cycle);
}

// One front runs along the fibre once per cycle, then holds and fades.
fn fbSweep(pos : f32, cycle : f32) -> f32 {
  let front = -0.03 + 1.06 * min(cycle / 0.45, 1.0);
  let done = smoothstep(1.0, 1.01, front - 0.02);
  let origin = mix(smoothstep(0.0, 0.04, pos), 1.0, done);
  let body = origin * (1.0 - smoothstep(front - 0.02, front + 0.005, pos));
  let off = (pos - (front - 0.012)) / 0.012;
  let ridge = origin * exp(-0.5 * off * off) * smoothstep(0.0, 0.04, cycle)
            * (1.0 - smoothstep(0.85, 1.0, front - 0.012));
  let hold = 1.0 - smoothstep(0.75, 1.0, cycle);
  return clamp(hold * clamp(0.65 * body + 0.35 * ridge, 0.0, 1.0), 0.0, 1.0);
}

fn fbPulse(along : f32) -> f32 {
  let rate = FB.p5.z;
  let depth = FB.p4.y;
  let shape = i32(FB.p5.w);
  if (rate == 0.0 || depth <= 0.0) { return 1.0; }
  if (shape == 3) {
    return 1.0 - depth + depth * fbSweep(along, fract(rate * FB.p2.y));
  }
  let phase = rate * FB.p2.y - select(0.0, along, shape == 2);
  return 1.0 - depth + depth * fbPulseLevel(fract(phase));
}

fn fbStopPos(i : u32) -> f32 {
  return FB.stopP[i / 4u][i % 4u];
}

fn fbRamp(t : f32) -> vec3f {
  let n = u32(FB.p8.z);
  if (n <= 1u || t <= fbStopPos(0u)) { return FB.stopC[0].xyz; }
  var col = FB.stopC[n - 1u].xyz;
  for (var k = 0u; k + 1u < n; k++) {
    let p0 = fbStopPos(k);
    let p1 = fbStopPos(k + 1u);
    if (t < p1) {
      return mix(FB.stopC[k].xyz, FB.stopC[k + 1u].xyz, clamp((t - p0) / max(p1 - p0, 1e-5), 0.0, 1.0));
    }
  }
  return col;
}

fn fbColourAt(t : f32, id : f32) -> vec3f {
  let mode = u32(FB.p8.w);
  let n = max(u32(FB.p8.z), 1u);
  if (mode == 0u) { return FB.stopC[0].xyz; }
  if (mode == 2u) {
    let k = min(u32(fbUnit(id, 14.0) * f32(n)), n - 1u);
    return FB.stopC[k].xyz;
  }
  return fbRamp(t);
}


// Depth, both ways. Behind the glass a strand dims with distance, which is the cue that makes the
// volume read as a volume. Approaching the glass it dims too, over a short band — a strand must not
// cross the surface and hang in the room, and a HARD cut at the plane would show as a bright edge
// the moment the tablet is tilted. Frosted glass does exactly this.
// 🔴 THE APERTURE — where a sightline crosses the glass, which is NOT where the strand is.
//
//    The first version of this clip asked whether the strand's own panel xy fell inside the face
//    rectangle. That is a flat test, correct for a decal and wrong for a window, and the error is
//    invisible head-on and glaring the moment the tablet is turned: a volume sitting eighty to a
//    hundred and forty millimetres BEHIND the glass projects OUTSIDE the panel's screen outline at
//    a steep angle, so the light sprayed off the side of the tablet and hung in the room.
//
//    A window does not work that way. What you can see through an aperture is decided at the
//    aperture, so the sightline from the eye to the strand is carried forward to the glass plane
//    (panel z = 0) and the rounded rectangle is evaluated THERE. That is a portal, and it costs one
//    ray-plane intersection in panel space — no stencil, no scissor, no render target, and it still
//    holds under any transform the tablet is given.
fn fbEyePanel() -> vec3f {
  // The panel rows carry panel -> world. The rotation is orthonormal, so the inverse is its
  // transpose: the COLUMNS of the rows, applied to the eye relative to the panel's origin.
  let offset = G.eye.xyz - vec3f(FB.rowX.w, FB.rowY.w, FB.rowZ.w);
  return vec3f(dot(vec3f(FB.rowX.x, FB.rowY.x, FB.rowZ.x), offset),
               dot(vec3f(FB.rowX.y, FB.rowY.y, FB.rowZ.y), offset),
               dot(vec3f(FB.rowX.z, FB.rowY.z, FB.rowZ.z), offset));
}

fn fbAperture(panel : vec3f) -> vec2f {
  let eye = fbEyePanel();
  let rise = panel.z - eye.z;
  if (abs(rise) < 1.0e-7) { return panel.xy; }     // the sightline runs along the glass
  let cross = (0.0 - eye.z) / rise;
  return eye.xy + (panel.xy - eye.xy) * cross;
}

fn fbGlassDepth(z : f32) -> f32 {
  let sink = exp(min(z, 0.0) / max(FB.clip.w, 1e-4));
  let band = max(FB.glass.x, 1e-4);
  return sink * (1.0 - smoothstep(-band, 0.0, z));
}

// The clip and the depth fade: everything that makes this light INSIDE a tablet rather than light
// in the room that happens to be in front of one.
//
// The panel coordinate carried to the fragment stage is the SPINE's, not the expanded ribbon
// corner's. A ribbon is a few pixels wide, so the error at the clip edge is sub-pixel; the
// alternative is to carry the lateral offset back through the panel basis for every vertex, which
// costs more than the artefact it removes.
struct FO {
  @builtin(position) pos : vec4f,
  @location(0) along : f32,
  @location(1) across : f32,
  @location(2) id : f32,
  @location(3) sigma : f32,
  @location(4) halo : f32,
  @location(5) fade : f32,
  @location(6) panel : vec3f,
};

@vertex
fn vsFibre(@builtin(vertex_index) vi : u32) -> FO {
  var o : FO;
  let segs = u32(FB.p6.z);
  let quad = vi / 6u;
  let corner = vi - quad * 6u;
  let strand = quad / segs;
  let segment = quad - strand * segs;
  let alongStep = select(0.0, 1.0, corner == 2u || corner == 3u || corner == 5u);
  let sideStep = select(0.0, 1.0, corner == 1u || corner == 4u || corner == 5u);
  let id = f32(strand);
  let s = (f32(segment) + alongStep) / f32(segs);
  let side = sideStep * 2.0 - 1.0;

  let panel = fbPanel(fbBezier(s, id));
  let centre = fbWorld(panel);
  let ahead = fbPanel(fbBezier(min(s + 0.002, 1.0), id));
  let behind = fbPanel(fbBezier(max(s - 0.002, 0.0), id));
  let tangent = fbWorld(ahead) - fbWorld(behind);
  var lateral = cross(tangent, G.eye.xyz - centre);
  let ll = length(lateral);
  lateral = select(vec3f(0.0, 0.0, 1.0), lateral / ll, ll > 1e-7);

  // Core and halo widths are in pixels, so a fibre keeps its width at any distance.
  let clip = G.viewClip * vec4f(centre, 1.0);
  let ppm = FB.p4.z / max(clip.w, 1e-3);
  let pixelScale = FB.p4.w;
  let sigmaPx = max(0.8, FB.p2.z * pixelScale) * 0.42466;
  let haloPx = max(2.4 * pixelScale, 2.0 * sigmaPx);
  let envPx = 3.6 * haloPx;
  let fade = mix(1.0, pow(max(sin(FPI * s), 0.0), 0.5), FB.p2.w);
  let world = centre + lateral * (side * envPx / max(ppm, 1e-6));

  o.pos = G.viewClip * vec4f(world, 1.0);
  o.along = s;
  o.across = side * envPx;
  o.id = id;
  o.sigma = sigmaPx;
  o.halo = haloPx;
  o.fade = fade;
  o.panel = panel;
  return o;
}

@fragment
fn fsFibre(i : FO) -> @location(0) vec4f {
  // The portal: the panel's own rounded rectangle, evaluated where this sightline crosses the
  // glass. Derivatives are taken before any discard, so the ramp stays one pixel wide.
  let aperture = fbAperture(i.panel);
  let edge = max(fwidth(aperture.x), fwidth(aperture.y));
  let inside = CoverageFromDistance(
      DistanceRoundedRectangle(aperture, FB.clip.xy, FB.clip.z), max(edge, 1e-6));

  // The back of a tablet is opaque. Without this the volume is simply visible from behind, which
  // is the same mistake as the flat clip wearing different clothes.
  if (inside <= 0.0 || fbEyePanel().z <= 0.0) { discard; }

  let line = exp(-0.5 * i.across * i.across / (i.sigma * i.sigma));
  let glow = FB.p3.y * exp(-0.5 * i.across * i.across / (i.halo * i.halo));
  let head = fract(fbUnit(i.id, 9.0) + FB.p5.y * FB.p2.y);
  let behind = fract(head - i.along);
  let win = max(FB.p3.w, 1e-3);
  let front = smoothstep(0.0, 0.05, behind);
  let streak = select(0.0, pow(1.0 - behind / win, 2.0) * front, behind < win);
  let lit = FB.p3.z + (1.0 - FB.p3.z) * streak;
  let ends = smoothstep(0.0, 0.04, i.along) * (1.0 - smoothstep(0.96, 1.0, i.along));
  let base = fbColourAt(i.along, i.id);
  let tint = mix(base, FB.colC.xyz, FB.p4.x * fbUnit(i.id, 12.0));
  let vary = 0.4 + 0.9 * fbUnit(i.id, 13.0);

  let sink = fbGlassDepth(i.panel.z);
  if (sink <= 0.0) { discard; }

  let level = FB.p3.x * vary * (line + glow) * lit * ends * i.fade * fbPulse(i.along) * inside * sink;
  return vec4f(tint * level, 1.0);
}

// Head sparks: one small quad per fibre at its light head, sized in pixels.
struct SO {
  @builtin(position) pos : vec4f,
  @location(0) uv : vec2f,
  @location(1) col : vec3f,
  @location(2) panel : vec3f,
};

@vertex
fn vsSpark(@builtin(vertex_index) vi : u32) -> SO {
  var o : SO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);      // off the clip volume: a spark that is not drawn
  o.uv = vec2f(0.0);
  o.col = vec3f(0.0);
  o.panel = vec3f(0.0, 0.0, -1.0);
  let strand = vi / 6u;
  let corner = vi - strand * 6u;
  if (strand >= u32(FB.p6.y)) { return o; }
  let id = f32(strand);
  if (fbUnit(id, 11.0) > FB.p7.y) { return o; }
  let s = fract(fbUnit(id, 9.0) + FB.p5.y * FB.p2.y);
  let panel = fbPanel(fbBezier(s, id));
  let centre = fbWorld(panel);
  let clip = G.viewClip * vec4f(centre, 1.0);
  let depth = max(clip.w, 0.001);
  let sizePx = clamp(FB.p7.z * FB.org.w * FB.p4.z / depth, 1.0, 48.0);
  var corners = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                                vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = corners[corner];
  let half = sizePx * 0.5;
  let ndc = vec2f(c.x * half * 2.0 / FB.p8.x, c.y * half * 2.0 / FB.p8.y);
  o.pos = vec4f(clip.xy + ndc * clip.w, clip.z, clip.w);
  o.uv = c;
  o.col = fbColourAt(s, id) * FB.p7.w * (0.6 + 0.8 * fbUnit(id, 10.0)) * fbPulse(s);
  o.panel = panel;
  return o;
}

@fragment
fn fsSpark(i : SO) -> @location(0) vec4f {
  // A spark goes through the same aperture — a head that has run off the face must not glow over
  // the bezel. Its quad is screen-aligned, so the whole quad lives or dies with its centre.
  let d = DistanceRoundedRectangle(fbAperture(i.panel), FB.clip.xy, FB.clip.z);
  if (d > 0.0 || fbEyePanel().z <= 0.0) { discard; }
  let r2 = dot(i.uv, i.uv);
  let disc = exp(-r2 * 7.0) * (1.0 - smoothstep(0.7, 1.0, r2));
  let sink = fbGlassDepth(i.panel.z);
  return vec4f(i.col * disc * sink, 1.0);
}
`;
