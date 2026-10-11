// The tablet as an OBJECT: a body with thickness, edges that catch light, and glass that reflects
// the room it is standing in.
//
// 🔴 WHY THIS IS A RAYMARCH AND NOT A PICTURE OF A BEZEL.
//
//    Everything in Engine/SpatialInterface is a flat figure on a plane. That is exactly right for
//    the interface — a needle has no thickness — and exactly wrong for the thing the interface is
//    printed on. A tablet read as a tablet because of three cues, none of which a plane can give:
//
//      1. A SILHOUETTE WITH DEPTH. Turn a real device and its side wall comes into view. A quad
//         has no side wall; it just gets narrower, which is what a sticker does.
//      2. AN EDGE THAT CATCHES THE KEY. The bright chamfer running along the top edge is the
//         single strongest "machined object" cue there is, and it exists because the edge is a
//         curved surface whose normal sweeps through the light.
//      3. GLASS THAT REFLECTS SOMETHING. A screen that emits and never reflects reads as a lamp.
//         The reflection has to MOVE against the surface as the eye moves, which means it has to
//         be computed from the view direction and not painted on.
//
//    All three are properties of a surface normal, so all three need a surface. The body is a
//    rounded box signed distance evaluated in the panel's own space — the same kind of function
//    the interface already is, one dimension up — marched in a pass of its own before the
//    interface draws, and the glass is a second quad composited after it.
//
//    This is the "custom raster" rung. It is not a render target: still no offscreen texture, no
//    sampler, no second camera. One more pass over the same frame.
//
// 🔴 WHAT IS DELIBERATELY NOT HERE YET.
//
//    No floor and no contact shadow, so the tablet is a product shot in a dark room rather than an
//    object resting on something. A floor wants the panel leaned back on a stand, which moves the
//    housing's quarter turn about X that the layout and its checks are built on. Worth doing;
//    worth doing on purpose.
//
//    No refraction through the glass, and no parallax between the glass surface and the interface
//    beneath it. Both want the interface rendered before the glass is shaded, which is the first
//    thing here that would actually need an offscreen target.

import { PanelHalfWidth, PanelHalfHeight } from './layout.js';

export const RoomFloats = 36;                  // 9 vec4s

// The body, in panel metres. The front face sits at panel z = 0, so the interface — which is
// authored on that plane — lands exactly on the glass.
export const Chassis = {
  // 🔴 12 mm proud of the screen on every side. The first pass used 2 mm, which is not a bezel,
  //    it is a tolerance — and it left the camera and the speaker underneath the glass quad where
  //    they could never be seen. A device is bigger than its display; give the hardware somewhere
  //    to live.
  halfWidth: 0.2420,
  halfHeight: 0.1470,
  halfDepth: 0.0105,          // [m] 21 mm thick: a rugged fascia, not a phone, and thick
                              //     enough that the side wall is a surface you can see
  cornerRadius: 0.0240,       // [m] the corner of the slab, in all three dimensions
  bodyRoughness: 0.34,        // anodised aluminium, bead blasted
  glassRoughness: 0.055,      // a hard coat: a tight reflection that still is not a mirror
  keyIntensity: 2.6,
  lean: 0.1700,               // [rad] ~9.7 deg. A tablet on a stand leans BACK. It is also what
                              //       finally lets the glass catch the high key, not just the desk.
  plinthHalf: [0.1760, 0.0520, 0.0100],
  plinthRadius: 0.0060,
  screenSpill: 0.55,          // how much of the screen's own light lands on the bezel around it
};

export function packRoom(out, rows, camera) {
  out.fill(0);
  const put = (slot, a, b, c, d) => out.set([a, b, c, d], slot * 4);

  out.set(rows[0], 0);
  out.set(rows[1], 4);
  out.set(rows[2], 8);
  put(3, Chassis.halfWidth, Chassis.halfHeight, Chassis.halfDepth, Chassis.cornerRadius);
  put(4, camera.forward[0], camera.forward[1], camera.forward[2], camera.tanHalf);
  put(5, camera.right[0], camera.right[1], camera.right[2], camera.aspect);
  put(6, camera.up[0], camera.up[1], camera.up[2], Chassis.screenSpill);
  put(7, Chassis.bodyRoughness, Chassis.glassRoughness, Chassis.keyIntensity, 0);
  // The PANEL rectangle, which is what the display texture covers and so what the glass
  // samples across. It is not the body's: the body stands a couple of millimetres proud.
  // 🔴 The desk, found rather than guessed. Push all eight corners of the body through the
  //    placement and take the lowest world z: the tablet then rests ON the surface for any lean,
  //    instead of hovering or sinking the moment the rotation is touched.
  let floor = Infinity;
  for (const sx of [-1, 1]) {
    for (const sy of [-1, 1]) {
      for (const pz of [0, -2 * Chassis.halfDepth]) {
        const z = rows[2][0] * sx * Chassis.halfWidth + rows[2][1] * sy * Chassis.halfHeight
                + rows[2][2] * pz + rows[2][3];
        floor = Math.min(floor, z);
      }
    }
  }
  put(8, PanelHalfWidth, PanelHalfHeight, floor, 0);
  return out;
}

export const CHASSIS_WGSL = String.raw`
//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TABLET ITSELF
//------------------------------------------------------------------------------------------------------------------------

struct Room {
  rowX : vec4f,     // panel -> world, as the interface's own placement rows
  rowY : vec4f,
  rowZ : vec4f,
  body : vec4f,     // xyz half extent [m] (z = half depth), w corner radius [m]
  look : vec4f,     // xyz camera forward, w tan(fov / 2)
  side : vec4f,     // xyz camera right, w aspect
  rise : vec4f,     // xyz camera up, w screen spill
  tone : vec4f,     // x body roughness, y glass roughness, z key intensity
  face : vec4f,     // xy panel half extent [m], z the desk's world height
};

@group(0) @binding(3) var<uniform> R : Room;

// The key. One hard source high and over the viewer's left shoulder, which is where a product shot
// puts it, and the one the glass reflection is built around.
const kKeyDirection = vec3f(-0.3827, -0.6428, 0.6634);

// 🔴 AND THE THING THE GLASS REFLECTS IS A DIFFERENT LIGHT ENTIRELY.
//
//    The key above is high, which is where a key belongs: it rakes the top edge and gives the slab
//    its form. It is useless to the glass. This panel stands UPRIGHT, so its mirror direction for
//    any eye at roughly screen height points DOWNWARD and slightly forward — a vertical screen
//    reflects what is in front of it, never the ceiling. Aim the key at the glass and the maths
//    says the highlight is somewhere below the floor, which is why the first attempt produced a
//    black sheet with a rim.
//
//    A studio solves this with a second thing: a large soft card in front of the subject. Work out
//    where it has to be rather than guessing - for an eye above the panel's centre the mirror
//    direction points DOWN, so the card belongs LOW, which in a real room is the lit table the
//    device is standing on. That is also what you are looking at in the reflection of every dark
//    television you have ever seen.
//
//    The consequence is worth having: head-on the card is near the edge of the lobe and the glass
//    is almost clear, and as the panel tilts the reflection blooms across it. That swing is the
//    behaviour, not a side effect of it.
const kFrontCard = vec3f(-0.3302, -0.8805, -0.3402);

// The room behind the tablet is the same studio seen directly rather than in a surface, and it is
// dimmed because it is scenery. The tablet is the subject and has to stay the brightest thing in
// the picture at every orbit, including the grazing ones where the front card swings into view.
const kRoomFalloff = 0.55;

// 🔴 The desk has to END, and not by running out of march steps.
//
//    A plane is infinite and the marcher is not. Left alone, a near-grazing ray creeps along the
//    surface until it hits the step limit and simply stops, which draws a hard horizontal edge
//    with black above it — in the first render it read as two slabs floating behind the tablet.
//    Fading the desk into the room over a metre and a half fixes the look AND the cost: once the
//    fade is complete there is nothing left to march toward.
const kDeskFade = 1.5;

// ── the room ─────────────────────────────────────────────────────────────────────────────────────
// There is no environment map and there is not going to be one — a sampled cube is a texture
// binding, which this exchange does not have and does not want. The room is a closed-form function
// of a direction, exactly as the streak field is a closed-form function of a plane coordinate, and
// for the same reason: a surface has to be able to answer what it reflects at a hit point.

// 🔴 THE LIGHT IS A CARD, NOT A POINT, AND THAT IS THE WHOLE TRICK.
//
//    A first attempt used pow(dot(dir, key), n) for the source. On a rough surface that is fine.
//    On GLASS it produces nothing: a flat mirror reflects one direction per pixel, so a lobe tight
//    enough to look sharp is tight enough that the eye never lands in it, and the screen comes out
//    as a dark sheet with a rim. Every photograph of a device in the world shows a long soft STRIP
//    lying across the glass, and that shape is the light's shape, not the material's.
//
//    So a source here is a rectangle, measured in the tangent plane about its own axis: wide in
//    one direction, narrow in the other. The surface decides how blurred it is, the card decides
//    what it looks like, and the two are no longer the same number.
fn RoomCard(direction : vec3f, axis : vec3f, wide : f32, tall : f32, sharpness : f32) -> f32 {
  let depth = dot(direction, axis);
  if (depth <= 0.02) { return 0.0; }

  let sideways = normalize(cross(axis, vec3f(0.0, 0.0, 1.0)));
  let upward = cross(sideways, axis);
  let spread = mix(3.0, 1.0, sharpness);         // a rough surface sees a bigger, softer card

  let across = dot(direction, sideways) / depth / (wide * spread);
  let along = dot(direction, upward) / depth / (tall * spread);
  return exp(-(across * across + along * along));
}

fn RoomLight(direction : vec3f, sharpness : f32) -> vec3f {
  // The shell: a dark studio, a little lift toward the ceiling, a cool floor bounce.
  let height = clamp(direction.z * 0.5 + 0.5, 0.0, 1.0);
  var light = mix(vec3f(0.0070, 0.0080, 0.0105), vec3f(0.0360, 0.0400, 0.0500), height * height);
  light += vec3f(0.0130, 0.0145, 0.0175) * pow(clamp(-direction.z, 0.0, 1.0), 2.0);

  // The key: a long narrow strip high and over the viewer's left shoulder. This is the one that
  // draws the chamfer along the top edge of the body.
  light += vec3f(1.00, 0.985, 0.955)
         * RoomCard(direction, kKeyDirection, 0.55, 0.070, sharpness) * mix(1.1, 6.0, sharpness);

  // The front card: large, soft, nearly horizontal, standing where a photographer would stand.
  // This is what the GLASS shows.
  light += vec3f(0.94, 0.965, 1.00)
         * RoomCard(direction, kFrontCard, 1.10, 0.26, sharpness) * mix(0.7, 2.4, sharpness);

  // A second, cooler and much softer card on the right, so the dark side of the slab is not black
  // and the glass has more than one thing to show.
  light += vec3f(0.30, 0.42, 0.62)
         * RoomCard(direction, normalize(vec3f(0.80, -0.36, 0.22)), 0.45, 0.30, sharpness)
         * mix(0.35, 1.5, sharpness);
  return light;
}

// ── panel space ──────────────────────────────────────────────────────────────────────────────────
// The rows carry panel -> world and the rotation is orthonormal, so the inverse is the transpose:
// the COLUMNS of the rows. The whole body is solved in panel metres, where it is axis aligned.

fn RoomToPanelPoint(world : vec3f) -> vec3f {
  let offset = world - vec3f(R.rowX.w, R.rowY.w, R.rowZ.w);
  return vec3f(dot(vec3f(R.rowX.x, R.rowY.x, R.rowZ.x), offset),
               dot(vec3f(R.rowX.y, R.rowY.y, R.rowZ.y), offset),
               dot(vec3f(R.rowX.z, R.rowY.z, R.rowZ.z), offset));
}

fn RoomToPanelDirection(world : vec3f) -> vec3f {
  return vec3f(dot(vec3f(R.rowX.x, R.rowY.x, R.rowZ.x), world),
               dot(vec3f(R.rowX.y, R.rowY.y, R.rowZ.y), world),
               dot(vec3f(R.rowX.z, R.rowY.z, R.rowZ.z), world));
}

fn RoomToWorldDirection(panel : vec3f) -> vec3f {
  return vec3f(dot(R.rowX.xyz, panel), dot(R.rowY.xyz, panel), dot(R.rowZ.xyz, panel));
}

// ── the body ─────────────────────────────────────────────────────────────────────────────────────
// A rounded box: the three dimensional sibling of DistanceRoundedRectangle, and the same identity —
// push the half extent in by the radius, measure to the shrunken box, push back out.

fn RoomBody(panel : vec3f) -> f32 {
  let radius = R.body.w;
  let centre = vec3f(0.0, 0.0, -R.body.z);                 // front face lands on panel z = 0
  let inner = max(R.body.xyz - vec3f(radius), vec3f(0.0));
  let delta = abs(panel - centre) - inner;
  return length(max(delta, vec3f(0.0))) + min(max(delta.x, max(delta.y, delta.z)), 0.0) - radius;
}

// 🔴 THE MARCH MOVED INTO WORLD SPACE, AND THAT IS WHY THERE CAN BE A DESK.
//
//    It used to step through panel space, which is fine while the only thing in the scene is the
//    panel. The moment the tablet has to STAND somewhere, it does not work: a desk is flat in the
//    world and the panel is leaning, so a desk expressed in panel coordinates is a slope. Marching
//    in the world instead lets each object be written in whatever frame suits it — the body in
//    panel space, the desk and the dock in world space — and a rigid rotation preserves distance,
//    so the marcher does not care.
//
//    This is also what makes the contact shadow possible, and the contact shadow is most of why
//    the thing reads as an object at all. A slab with no shadow is a picture of a slab.

const kBody = 0.0;
const kDock = 1.0;
const kDesk = 2.0;

// The dock the tablet stands in. A slim rounded plinth, flat on the desk, world aligned — it does
// not lean with the panel, because a foot that leaned would be a foot that had fallen over.
fn RoomDock(world : vec3f) -> f32 {
  let half = vec3f(${Chassis.plinthHalf[0]}, ${Chassis.plinthHalf[1]}, ${Chassis.plinthHalf[2]});
  let radius = ${Chassis.plinthRadius};
  let centre = vec3f(0.0, -0.004, R.face.z + half.z);
  let inner = max(half - vec3f(radius), vec3f(0.0));
  let delta = abs(world - centre) - inner;
  return length(max(delta, vec3f(0.0))) + min(max(delta.x, max(delta.y, delta.z)), 0.0) - radius;
}

struct RoomSample {
  distance : f32,
  material : f32,
};

fn RoomScene(world : vec3f) -> RoomSample {
  var out : RoomSample;
  out.distance = RoomBody(RoomToPanelPoint(world));
  out.material = kBody;

  let dock = RoomDock(world);
  if (dock < out.distance) { out.distance = dock; out.material = kDock; }

  let desk = world.z - R.face.z;
  if (desk < out.distance) { out.distance = desk; out.material = kDesk; }
  return out;
}

fn RoomNormal(world : vec3f) -> vec3f {
  // Tetrahedral differences: four evaluations instead of six, and no bias toward an axis.
  let h = 2.0e-5;
  let a = vec3f( 1.0, -1.0, -1.0);
  let b = vec3f(-1.0, -1.0,  1.0);
  let c = vec3f(-1.0,  1.0, -1.0);
  let d = vec3f( 1.0,  1.0,  1.0);
  return normalize(a * RoomScene(world + a * h).distance + b * RoomScene(world + b * h).distance
                 + c * RoomScene(world + c * h).distance + d * RoomScene(world + d * h).distance);
}

struct RoomHit {
  struck   : bool,
  world    : vec3f,
  panel    : vec3f,
  travel   : f32,
  material : f32,
};

fn RoomMarch(origin : vec3f, direction : vec3f) -> RoomHit {
  var hit : RoomHit;
  hit.struck = false;
  hit.world = origin;
  hit.panel = origin;
  hit.travel = 0.0;
  hit.material = kDesk;

  var travel = 0.0;
  for (var step = 0; step < 96; step = step + 1) {
    let here = origin + direction * travel;
    let scene = RoomScene(here);
    if (scene.distance < 1.5e-5) {
      hit.struck = true;
      hit.world = here;
      hit.panel = RoomToPanelPoint(here);
      hit.travel = travel;
      hit.material = scene.material;
      return hit;
    }
    travel += max(scene.distance, 1.0e-5);
    if (travel > kDeskFade * 1.6) { break; }
  }
  return hit;
}

// 🔴 The contact shadow, which is the single cue that turns a floating slab into an object.
//
//    The standard trick: march toward the light and keep the smallest ratio of distance-to-scene
//    against distance-travelled. Near an occluder that ratio collapses and the point is dark; far
//    from one it stays high. The ratio IS the penumbra, so the shadow is soft where the tablet is
//    far from the desk and tightens to a hard line where it meets it, for free.
fn RoomShadow(origin : vec3f, direction : vec3f, sharpness : f32) -> f32 {
  var shade = 1.0;
  var travel = 0.004;                        // step off the surface, or it shadows itself
  for (var step = 0; step < 40; step = step + 1) {
    let distance = RoomScene(origin + direction * travel).distance;
    if (distance < 1.0e-5) { return 0.0; }
    shade = min(shade, sharpness * distance / travel);
    travel += clamp(distance, 0.002, 0.08);
    if (travel > 1.2) { break; }
  }
  return clamp(shade, 0.0, 1.0);
}

// ── shading ──────────────────────────────────────────────────────────────────────────────────────
// Anodised aluminium: a dark, slightly metallic albedo with a broad specular lobe. The point of the
// material is not that it is physically complete; it is that the EDGE behaves — the normal sweeps
// through the key along the chamfer, so the rim lights up and the slab reads as machined.

fn RoomSchlick(cosine : f32, base : f32) -> f32 {
  let f = clamp(1.0 - cosine, 0.0, 1.0);
  let f2 = f * f;
  return base + (1.0 - base) * f2 * f2 * f;
}

fn RoomBodyShade(worldPoint : vec3f, worldNormal : vec3f, view : vec3f, screenNear : f32) -> vec3f {
  let roughness = R.tone.x;
  let sharpness = clamp(1.0 - roughness, 0.0, 1.0);

  let albedo = vec3f(0.0320, 0.0345, 0.0400);
  let lambert = max(dot(worldNormal, kKeyDirection), 0.0);
  var light = albedo * (lambert * R.tone.z + 0.14);

  // Ambient from the room, in the direction the surface faces.
  light += albedo * RoomLight(worldNormal, 0.0) * 3.4;

  // The reflection. A rough metal still mirrors the softbox, just widely.
  let bounce = reflect(-view, worldNormal);
  let fresnel = RoomSchlick(max(dot(worldNormal, view), 0.0), 0.055);
  light += RoomLight(bounce, sharpness * 0.55) * fresnel * 1.9;

  // The chamfer. Where the slab turns, its normal sweeps through the key in the space of a couple
  // of millimetres and a bright line runs along the edge. It is the strongest "machined from a
  // solid" cue there is, and on a plane it simply cannot happen.
  let turn = 1.0 - abs(dot(worldNormal, normalize(vec3f(R.rowX.z, R.rowY.z, R.rowZ.z))));
  light += vec3f(0.30, 0.33, 0.40) * pow(clamp(turn, 0.0, 1.0), 2.0) * lambert * 0.55;

  // The screen spills onto the bezel around it. A lit panel in a dark room does this, and leaving
  // it out is what makes a bezel look painted on rather than adjacent to something bright.
  light += vec3f(0.055, 0.195, 0.300) * screenNear * R.rise.w;
  return light;
}

// ── the pass ─────────────────────────────────────────────────────────────────────────────────────

struct RoomVarying {
  @builtin(position) Position : vec4f,
};

@vertex
fn vsRoom(@builtin(vertex_index) Vertex : u32) -> RoomVarying {
  // One oversized triangle. Cheaper than a quad and with no seam down the diagonal.
  var corners = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  var out : RoomVarying;
  out.Position = vec4f(corners[Vertex], 0.0, 1.0);
  return out;
}

@fragment
fn fsRoom(in : RoomVarying) -> @location(0) vec4f {
  let screen = in.Position.xy / G.extent.xy * 2.0 - vec2f(1.0);
  let direction = normalize(R.look.xyz
                          + R.side.xyz * (screen.x * R.side.w * R.look.w)
                          + R.rise.xyz * (-screen.y * R.look.w));

  let hit = RoomMarch(G.eye.xyz, direction);
  if (!hit.struck) {
    return vec4f(RoomLight(direction, 0.0) * kRoomFalloff, 1.0);
  }

  let worldNormal = RoomNormal(hit.world);
  let view = -direction;

  // The screen is an area light and the desk is right underneath it. This is the other half of
  // "the tablet is really there": its own glow pooling on the surface in front of it.
  let toPanel = hit.world - vec3f(R.rowX.w, R.rowY.w, R.rowZ.w);
  let spillFall = 1.0 / (1.0 + dot(toPanel, toPanel) * 26.0);
  let panelFace = normalize(vec3f(R.rowX.z, R.rowY.z, R.rowZ.z));
  let spillFacing = clamp(dot(worldNormal, panelFace) * 0.5 + 0.5, 0.0, 1.0);
  let spill = spillFall * spillFacing;

  let shadow = RoomShadow(hit.world, kKeyDirection, 9.0);

  if (hit.material == kDesk) {
    // 🔴 The desk. Deliberately almost black and quite rough: it exists to take a shadow and a
    //    pool of the screen's light, not to be looked at. A bright floor would read as a studio
    //    sweep and pull the eye off the device, and the device is the subject.
    let albedo = vec3f(0.0115, 0.0122, 0.0140);
    let lambert = max(dot(worldNormal, kKeyDirection), 0.0);
    var light = albedo * (lambert * R.tone.z * shadow + 0.10);
    light += albedo * RoomLight(worldNormal, 0.0) * 3.0;

    // A wide, low grazing sheen, which is what a matt surface does under a big soft source.
    let bounce = reflect(-view, worldNormal);
    light += RoomLight(bounce, 0.25) * RoomSchlick(max(dot(worldNormal, view), 0.0), 0.030) * 0.9;

    // The pool. Tinted like the interface, shadowed by the tablet's own body so it does not leak
    // out behind the device.
    light += vec3f(0.055, 0.175, 0.280) * spill * 1.35
           * RoomShadow(hit.world, normalize(-panelFace), 5.0);

    let away = clamp(hit.travel / kDeskFade, 0.0, 1.0);
    return vec4f(mix(light, RoomLight(direction, 0.0) * kRoomFalloff, away * away), 1.0);
  }

  if (hit.material == kDock) {
    let albedo = vec3f(0.0150, 0.0160, 0.0184);
    let lambert = max(dot(worldNormal, kKeyDirection), 0.0);
    var light = albedo * (lambert * R.tone.z * shadow + 0.14);
    light += albedo * RoomLight(worldNormal, 0.0) * 3.2;
    let bounce = reflect(-view, worldNormal);
    light += RoomLight(bounce, 0.55) * RoomSchlick(max(dot(worldNormal, view), 0.0), 0.045) * 0.7;
    light += vec3f(0.055, 0.175, 0.280) * spill * 0.85;
    return vec4f(light, 1.0);
  }

  // How close this point is to the lit face, for the spill. Measured in the panel's own plane and
  // only on the front, so the back of the device stays dark.
  let faceReach = DistanceRoundedRectangle(hit.panel.xy, R.face.xy, R.body.w * 0.86);
  let facing = clamp(RoomToPanelDirection(worldNormal).z, 0.0, 1.0);
  let screenNear = facing * exp(-max(faceReach, 0.0) / 0.010);

  // The glass itself is not shaded here: the glass pass draws over this region next. What is left
  // is a near-black well, so a pixel the interface never covers still reads as switched-off
  // screen rather than as a hole in the device.
  if (faceReach < 0.0 && RoomToPanelDirection(worldNormal).z > 0.86) {
    return vec4f(vec3f(0.0042, 0.0048, 0.0060), 1.0);
  }

  var body = RoomBodyShade(hit.world, worldNormal, view, screenNear) * mix(0.55, 1.0, shadow);

  // 🔴 Hardware. A bezel with nothing on it is a picture of a bezel. These two marks cost four
  //    lines and do more for "this is a device" than any amount of shading on the slab, because
  //    they are the things a person looks for without knowing they are looking.
  if (facing > 0.86) {
    // The camera, centred in the top bezel: a dark pit with a hard little catchlight.
    let lens = length(hit.panel.xy - vec2f(0.0, R.face.y + 0.0062)) - 0.0024;
    body = mix(body, vec3f(0.0030, 0.0034, 0.0046), 1.0 - smoothstep(0.0, 0.0006, lens));
    body += vec3f(0.26, 0.30, 0.40) * (1.0 - smoothstep(0.0, 0.0010, abs(lens + 0.0014)));

    // The speaker, a slot of fine perforations in the bottom bezel.
    let slot = DistanceRoundedRectangle(hit.panel.xy - vec2f(0.0, -R.face.y - 0.0060),
                                        vec2f(0.0220, 0.0011), 0.0011);
    let holes = 0.5 + 0.5 * cos(hit.panel.x * 2400.0);
    body = mix(body, body * 0.18, (1.0 - smoothstep(0.0, 0.0004, slot)) * holes);
  }
  return vec4f(body, 1.0);
}
`;

