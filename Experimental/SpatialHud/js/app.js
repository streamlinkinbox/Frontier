// The browser host: a device, one pipeline, one draw.
//
// The whole interface is a single vkCmdDraw(4, instances) in the engine and a single
// pass.draw(4, count) here. There is no vertex buffer and no index buffer: the vertex stage derives
// the corner from its own index, which is why adding a figure costs 160 bytes and nothing else.

import { pack, resolve, FloatsPerFigure } from './figures.js';
import { SDF_WGSL } from './sdf.generated.js';
import { STREAK_WGSL } from './streaks.js';
import { constructHudLayout, assignValues, PanelHalfWidth, PanelHalfHeight } from './layout.js';
import { constructShellLayout, assignShellValues } from './shell.js';
import { FIBRE_WGSL, packFibre, StreakPreset, FibreFloats } from './fibres.js';
import { CHASSIS_WGSL, packRoom, RoomFloats, Chassis } from './chassis.js';
import { DISPLAY_WGSL, DisplayClip, DisplayProjectionScale,
         DisplayWidth, DisplayHeight, BloomWidth, BloomHeight } from './display.js';

// Module-scope declarations come first so the fibre stages can reach the globals the figure stages
// also use. One module, one shader compilation, three pipelines off it.
const Prelude = `
struct Globals {
  viewClip : mat4x4f,
  eye      : vec4f,     // xyz = eye
  extent   : vec4f,     // xy = render size [px]
  ambient  : vec4f,     // xyz = scene-average irradiance on the panel
  timing   : vec4f,     // x = elapsed [s]
};

struct Fig {
  rowX    : vec4f,
  rowY    : vec4f,
  rowZ    : vec4f,
  size    : vec4f,      // xy half extent [m], z corner radius [m], w opacity
  clip    : vec4f,      // xy minimum, zw maximum, figure-local
  scal    : vec4f,      // x category, y ScalarAlpha, z ScalarBeta, w emissive weight
  tint    : vec4f,
  base    : vec4f,      // xyz albedo, w light role (1 = overlay, lights nothing)
  streakA : vec4f,
  streakB : vec4f,
};

@group(0) @binding(0) var<uniform> G : Globals;
@group(0) @binding(1) var<storage, read> Figures : array<Fig>;
`;

const Stages = `
struct Varying {
  @builtin(position) Position : vec4f,
  @location(0) @interpolate(flat) Figure : u32,
  @location(1) Local : vec2f,
};

// gl_VertexIndex 0..3 -> the corners of a triangle strip on the figure's local plane.
fn InterfaceCorner(VertexIndex : u32, HalfExtent : vec2f) -> vec2f {
  let Sign = vec2f(select(-1.0, 1.0, (VertexIndex & 1u) != 0u),
                   select(-1.0, 1.0, (VertexIndex & 2u) != 0u));
  return Sign * HalfExtent;
}

@vertex
fn VertexMain(@builtin(vertex_index) Vertex : u32,
              @builtin(instance_index) Instance : u32) -> Varying {
  let F = Figures[Instance];
  let Corner = InterfaceCorner(Vertex, F.size.xy);
  let Local = vec3f(Corner, 0.0);

  let World = vec3f(dot(F.rowX.xyz, Local) + F.rowX.w,
                    dot(F.rowY.xyz, Local) + F.rowY.w,
                    dot(F.rowZ.xyz, Local) + F.rowZ.w);

  var Out : Varying;
  Out.Position = G.viewClip * vec4f(World, 1.0);
  Out.Figure = Instance;
  Out.Local = Corner;
  return Out;
}

@fragment
fn FragmentMain(In : Varying) -> @location(0) vec4f {
  let F = Figures[In.Figure];

  // One screen pixel, in the figure's local metres. fwidth is the sum of the absolute derivatives —
  // the conservative estimate, and the reason the edge stays one pixel wide under perspective and
  // rotation alike.
  let PixelWidth = max(fwidth(In.Local.x), fwidth(In.Local.y));

  let Category = u32(F.scal.x + 0.5);

  var Coverage : f32;
  if (Category == kCategoryStreakField) {
    // The streak field answers with light, not with a distance: there is no edge for a coverage ramp
    // to be a pixel wide across, so asking CoverageFromDistance for one would be meaningless.
    Coverage = StreakFieldGlow(In.Local, F.size.xy, F.streakA, F.streakB, G.timing.x);
  } else {
    let Distance = DistanceFigure(Category, In.Local, F.size.xy, F.size.z, F.scal.y, F.scal.z);
    Coverage = CoverageFromDistance(Distance, PixelWidth);
  }

  Coverage *= ClipCoverage(In.Local, F.clip, 0.0, PixelWidth);
  if (Coverage <= 0.0) { discard; }

  let Alpha = Coverage * F.size.w * F.tint.w;
  if (Alpha <= 0.0) { discard; }

  // Surface response. An emissive element shows its tint whatever the room is doing; an albedo one
  // is lit by the ambient term, so a dark bezel sits DOWN in a dim room instead of glowing.
  let Emitted = F.tint.xyz;
  let Received = F.base.xyz * G.ambient.xyz;
  let Colour = mix(Received, Emitted, clamp(F.scal.w, 0.0, 1.0));

  return vec4f(Colour * Alpha, Alpha);   // premultiplied
}
`;

// ── matrices ─────────────────────────────────────────────────────────────────────────────────────
// World is right-handed Z-up, as the engine's is. Clip depth is 0..1, which is WebGPU's convention
// and Vulkan's both.

function lookAt(eye, at, up) {
  const f = normalise([at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]]);
  const s = normalise(cross(f, up));
  const u = cross(s, f);
  return [
    s[0], u[0], -f[0], 0,
    s[1], u[1], -f[1], 0,
    s[2], u[2], -f[2], 0,
    -dot(s, eye), -dot(u, eye), dot(f, eye), 1,
  ];
}

function perspective(fovY, aspect, near, far) {
  const t = 1 / Math.tan(fovY * 0.5);
  return [
    t / aspect, 0, 0, 0,
    0, t, 0, 0,
    0, 0, far / (near - far), -1,
    0, 0, (far * near) / (near - far), 0,
  ];
}

function multiply(A, B) {
  const out = new Float32Array(16);
  for (let c = 0; c < 4; c++) {
    for (let r = 0; r < 4; r++) {
      out[c * 4 + r] = A[0 * 4 + r] * B[c * 4 + 0] + A[1 * 4 + r] * B[c * 4 + 1]
                     + A[2 * 4 + r] * B[c * 4 + 2] + A[3 * 4 + r] * B[c * 4 + 3];
    }
  }
  return out;
}

const dot = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const normalise = (A) => { const L = Math.hypot(A[0], A[1], A[2]) || 1; return [A[0] / L, A[1] / L, A[2] / L]; };

// ── springs ──────────────────────────────────────────────────────────────────────────────────────
// MotionIntegrator's channels, in miniature: a second-order step per value, so a number never jumps.
// The needle's envelope is the trial panel's own (zeta ~ 0.68, about seven percent overshoot); the
// bar is critically damped, because a progress bar that overshoots reads as a fault.

class Channel {
  constructor(stiffness, damping, value = 0) {
    this.k = stiffness; this.c = damping; this.value = value; this.rate = 0; this.target = value;
  }
  advance(dt) {
    // Sub-stepped at a fixed 240 Hz: a stiff spring integrated at the display's refresh explodes on
    // a slow frame, and a HUD that detonates when the tab is backgrounded is not acceptable.
    const step = 1 / 240;
    let left = Math.min(dt, 0.25);
    while (left > 0) {
      const h = Math.min(step, left);
      this.rate += (this.k * (this.target - this.value) - this.c * this.rate) * h;
      this.value += this.rate * h;
      left -= h;
    }
    return this.value;
  }
}

// ── the host ─────────────────────────────────────────────────────────────────────────────────────

const State = {
  speed: 0, boost: 0.0, regen: 0.35, sport: 0,
  ambient: 0.35,
  orbit: 0.42, tilt: 0.18, distance: 0.78,
  running: true, demo: true,
  streak: null,
  // The backdrop ladder. 'live' is the real 3D fibres; 'field' is the analytic plane field, which
  // is the rung a reflection or a distant panel gets; 'off' is neither.
  backdrop: 'live',
  // 🔴 The tablet is an OS, not a dashboard. 'home' is FRONTIER OS — the clock, the app wall and
  //    the dock; 'dash' is the instrument composition the device started life as, which is now
  //    one app among the eight rather than the whole device.
  screen: 'home',
};

async function start() {
  const canvas = document.getElementById('viewport');
  const notice = document.getElementById('notice');

  if (!navigator.gpu) {
    notice.hidden = false;
    notice.textContent = 'This reference needs WebGPU. Chrome or Edge 113+, or Safari 18+.';
    return;
  }
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) {
    notice.hidden = false;
    notice.textContent = 'WebGPU is present but no adapter answered. Hardware acceleration may be off.';
    return;
  }
  const device = await adapter.requestDevice();
  const context = canvas.getContext('webgpu');
  const format = navigator.gpu.getPreferredCanvasFormat();
  context.configure({ device, format, alphaMode: 'opaque' });

  const module = device.createShaderModule({
    label: 'spatial interface',
    code: `${Prelude}\n${SDF_WGSL}\n${STREAK_WGSL}\n${FIBRE_WGSL}\n${CHASSIS_WGSL}\n${DISPLAY_WGSL}\n${Stages}`,
  });

  const info = await module.getCompilationInfo();
  const errors = info.messages.filter((One) => One.type === 'error');
  if (errors.length) {
    notice.hidden = false;
    notice.textContent = errors.map((One) => `${One.lineNum}:${One.linePos} ${One.message}`).join('\n');
    throw new Error(errors[0].message);
  }

  // Premultiplied alpha, composited against the resolved scene — the engine's own blend.
  const over = {
    color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha' },
    alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha' },
  };

  // 🔴 The interface targets the DISPLAY, not the window. Everything from here to the glass pass
  //    is drawn at the panel's own resolution, through the panel's own off-axis projection.
  const pipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'VertexMain' },
    fragment: {
      module,
      entryPoint: 'FragmentMain',
      targets: [{
        format: 'rgba16float',
        blend: over,
      }],
    },
    primitive: { topology: 'triangle-strip' },
  });

  // The world behind the glass. Additive, because light adds: two strands crossing are brighter
  // than either, and that is not an alpha blend. No depth buffer is bound at all — every figure in
  // this composition is coplanar to within three millimetres, so depth would z-fight rather than
  // resolve, and the fibres are glow that should never occlude anything anyway.
  const additive = {
    color: { srcFactor: 'one', dstFactor: 'one' },
    alpha: { srcFactor: 'one', dstFactor: 'one' },
  };
  const fibrePipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'vsFibre' },
    fragment: { module, entryPoint: 'fsFibre', targets: [{ format: 'rgba16float', blend: additive }] },
    primitive: { topology: 'triangle-list' },
  });
  const sparkPipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'vsSpark' },
    fragment: { module, entryPoint: 'fsSpark', targets: [{ format: 'rgba16float', blend: additive }] },
    primitive: { topology: 'triangle-list' },
  });

  // 🔴 THE DISPLAY IS A TEXTURE NOW.
  //
  //    rgba16float, not the swapchain's 8-bit format. The interface is additive in places and the
  //    bloom bright pass has to be able to tell a lit element from a blown one, and both of those
  //    need values above 1.0 to survive the trip. An 8-bit target clamps them flat and the glow
  //    comes out as a uniform halo around everything white.
  const displayFormat = 'rgba16float';
  const displayTexture = device.createTexture({
    size: [DisplayWidth, DisplayHeight],
    format: displayFormat,
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
  });
  const bloomTexture = [0, 1].map(() => device.createTexture({
    size: [BloomWidth, BloomHeight],
    format: displayFormat,
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
  }));
  const displayView = displayTexture.createView();
  const bloomView = bloomTexture.map((T) => T.createView());

  // Clamp, because the refraction offset walks the sample point past the edge at a steep angle
  // and a repeat would wrap the opposite side of the interface into the rim.
  const displaySampler = device.createSampler({
    magFilter: 'linear', minFilter: 'linear',
    addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge',
  });

  const coverPipeline = (entryPoint, target) => device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'vsCover' },
    fragment: { module, entryPoint, targets: [{ format: target }] },
    primitive: { topology: 'triangle-list' },
  });
  const bloomCutPipeline = coverPipeline('fsBloomCut', displayFormat);
  const bloomAcrossPipeline = coverPipeline('fsBloomAcross', displayFormat);
  const bloomDownPipeline = coverPipeline('fsBloomDown', displayFormat);

  // The tablet as an object. The body is opaque and goes down first, so everything else lands on
  // a real surface; the glass reads the display target and is the last thing drawn.
  const roomPipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'vsRoom' },
    fragment: { module, entryPoint: 'fsRoom', targets: [{ format }] },
    primitive: { topology: 'triangle-list' },
  });
  const glassPipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'vsGlass' },
    // Over, not additive. The glass OWNS the face now: it decides how much of the interface you
    // see and how much of the room, and a split needs to be able to take light away.
    fragment: { module, entryPoint: 'fsGlass', targets: [{ format, blend: over }] },
    primitive: { topology: 'triangle-strip' },
  });

  // 🔴 TWO COMPOSITIONS, ONE DEVICE. They are separate structures rather than two branches of one,
  //    because they share nothing except the housing they are built on — and because switching
  //    screens must not cost a rebuild. Both are constructed once, here; the loop picks.
  const screens = {
    home: constructShellLayout(),
    dash: constructHudLayout(),
  };
  // 🔴 The lean. Each layout stands its panel bolt upright, which is the right AUTHORING frame —
  //    every figure is placed against it and every check is pinned to it. A tablet in a room is
  //    not bolt upright, though: it sits in its dock and leans back. So the lean is applied here,
  //    to the scene, and no layout ever learns about it.
  for (const each of Object.values(screens)) {
    each.structure.query(each.handles.housing).rotationX = Math.PI / 2 - Chassis.lean;
  }

  State.streak = screens.dash.structure.query(screens.dash.handles.streaks).streak.slice();

  // 🔴 TWO CAMERAS, TWO UNIFORM BUFFERS.
  //
  //    The interface and the fibres are drawn through the panel's off-axis frustum into the
  //    display; the body and the glass are drawn through the window's ordinary perspective. They
  //    share a struct and nothing else. The eye position inside both is the same real world eye,
  //    which is what keeps the fibre aperture and the parallax honest.
  //
  //    Both live here, above the first bind group that reads either of them. `const` is hoisted
  //    but NOT initialised, so a reference from above is a hard ReferenceError rather than an
  //    undefined — and nothing but the browser will tell you.
  const GlobalFloats = 32;
  const globals = device.createBuffer({ size: 128, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
  const displayGlobalData = new Float32Array(GlobalFloats);
  const displayGlobals = device.createBuffer({
    size: displayGlobalData.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  let capacity = Math.max(64, ...Object.values(screens).map((each) => each.structure.count));
  let figureBuffer = device.createBuffer({
    size: capacity * FloatsPerFigure * 4,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  let bind = device.createBindGroup({
    layout: pipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: displayGlobals } }, { binding: 1, resource: { buffer: figureBuffer } }],
  });

  const roomData = new Float32Array(RoomFloats);
  const roomBuffer = device.createBuffer({
    size: roomData.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const roomBind = device.createBindGroup({
    layout: roomPipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: globals } },
              { binding: 3, resource: { buffer: roomBuffer } }],
  });
  const glassBind = device.createBindGroup({
    layout: glassPipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: globals } },
              { binding: 3, resource: { buffer: roomBuffer } },
              { binding: 4, resource: displayView },
              { binding: 5, resource: displaySampler },
              { binding: 6, resource: bloomView[1] }],
  });

  // The glow chain: bright pass off the display into the first small target, then one blur along
  // each axis, ping-ponging between the two.
  const bloomCutBind = device.createBindGroup({
    layout: bloomCutPipeline.getBindGroupLayout(0),
    entries: [{ binding: 4, resource: displayView }, { binding: 5, resource: displaySampler }],
  });
  const bloomAcrossBind = device.createBindGroup({
    layout: bloomAcrossPipeline.getBindGroupLayout(0),
    entries: [{ binding: 5, resource: displaySampler }, { binding: 6, resource: bloomView[0] }],
  });
  const bloomDownBind = device.createBindGroup({
    layout: bloomDownPipeline.getBindGroupLayout(0),
    entries: [{ binding: 5, resource: displaySampler }, { binding: 6, resource: bloomView[1] }],
  });

  const fibreData = new Float32Array(FibreFloats);
  const fibreBuffer = device.createBuffer({
    size: fibreData.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const fibreEntries = [{ binding: 0, resource: { buffer: displayGlobals } },
                        { binding: 2, resource: { buffer: fibreBuffer } }];
  const fibreBind = device.createBindGroup({ layout: fibrePipeline.getBindGroupLayout(0), entries: fibreEntries });
  const sparkBind = device.createBindGroup({ layout: sparkPipeline.getBindGroupLayout(0), entries: fibreEntries });

  const boostChannel = new Channel(260, 22);
  const fillChannel = new Channel(220, 30, State.regen);
  const sportChannel = new Channel(300, 26);
  const speedChannel = new Channel(90, 17);

  wireControls(screens);
  wireCamera(canvas);

  // Reported on the first frame, not after the first half second, so a page that renders once and
  // then dies still says what it managed.
  let firstFrame = true;

  const globalData = new Float32Array(GlobalFloats);
  let last = performance.now() / 1000;
  let elapsed = 0;
  let frames = 0, frameClock = last, rate = 0;

  function frame() {
    const now = performance.now() / 1000;
    const dt = Math.min(now - last, 0.1);
    last = now;
    if (State.running) elapsed += dt;

    if (State.demo) {
      // The scripted cycle, so the panel is alive before anything is touched.
      const cycle = (elapsed % 12) / 12;
      State.boost = 0.5 - 0.5 * Math.cos(cycle * Math.PI * 2 * 2);
      State.speed = 18 + 160 * State.boost;
      State.sport = cycle > 0.5 ? 1 : 0;
      State.regen = 0.5 + 0.45 * Math.sin(cycle * Math.PI * 2);
      reflectControls();
    }

    boostChannel.target = State.boost;
    fillChannel.target = State.regen;
    sportChannel.target = State.sport;
    speedChannel.target = State.speed;

    // Which composition is on the glass this frame. Everything downstream — the resolve, the
    // pack, the split, the panel camera — reads these two and does not care which one it got.
    const { structure, handles } = screens[State.screen] ?? screens.home;

    if (State.screen === 'dash') {
      assignValues(structure, handles, {
        speed: speedChannel.advance(dt),
        boost: boostChannel.advance(dt),
        regen: fillChannel.advance(dt),
        sport: sportChannel.advance(dt),
        time: elapsed,
      });
    } else {
      assignShellValues(structure, handles, { clock: new Date(), time: elapsed });
    }

    // The streak sliders edit one shared preset, so whichever screen is up gets the current one.
    structure.query(handles.streaks).streak = State.streak.slice();

    const width = Math.max(1, Math.floor(canvas.clientWidth * devicePixelRatio));
    const height = Math.max(1, Math.floor(canvas.clientHeight * devicePixelRatio));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width; canvas.height = height;
    }

    const eye = [
      Math.sin(State.orbit) * Math.cos(State.tilt) * State.distance,
      -Math.cos(State.orbit) * Math.cos(State.tilt) * State.distance,
      Math.sin(State.tilt) * State.distance,
    ];
    const fieldOfView = 0.62;
    const forward = normalise([-eye[0], -eye[1], -eye[2]]);
    const right = normalise(cross(forward, [0, 0, 1]));
    const rise = cross(right, forward);
    const view = lookAt(eye, [0, 0, 0], [0, 0, 1]);
    const projection = perspective(fieldOfView, width / height, 0.02, 40);
    const viewClip = multiply(projection, view);

    // Pixels per world metre at w = 1, which is how the fibres keep a constant pixel width at any
    // distance — the same quantity the particle editor passes as projScale.
    const projectionScale = 0.5 * height / Math.tan(fieldOfView * 0.5);

    // The analytic field is a RUNG, not a fallback: it is the only form of this backdrop that can
    // be answered at a hit point, because it is a closed-form function of a plane coordinate and
    // geometry is not. It is drawn only when it is the rung in use.
    structure.query(handles.streaks).opacity = State.backdrop === 'field'
      ? State.fieldOpacity * (State.screen === 'home' ? 0.45 : 1)
      : 0;

    globalData.set(viewClip, 0);
    globalData.set([eye[0], eye[1], eye[2], 0], 16);
    globalData.set([width, height, 0, 0], 20);
    const a = State.ambient;
    globalData.set([a, a * 1.02, a * 1.08, 0], 24);
    globalData.set([elapsed, dt, 0, 0], 28);
    device.queue.writeBuffer(globals, 0, globalData);

    const { placements } = resolve(structure);
    const packed = pack(structure, placements, eye);

    // Where the world goes: after the housing, the face and the field rung, before every control.
    const backdropRank = structure.query(handles.streaks).orderingRank;
    let splitAt = packed.order.findIndex((at) => structure.query(at).orderingRank > backdropRank);
    if (splitAt < 0) splitAt = packed.count;

    packRoom(roomData, placements[handles.housing], {
      forward, right, up: rise, tanHalf: Math.tan(fieldOfView * 0.5), aspect: width / height,
    });
    device.queue.writeBuffer(roomBuffer, 0, roomData);

    // The panel's own camera: same eye, different window. The near plane IS the panel, so the
    // interface fills the display exactly and the depth behind the glass still projects.
    const display = DisplayClip(placements[handles.housing], eye, PanelHalfWidth, PanelHalfHeight);
    const displayScale = DisplayProjectionScale(display.eyePanel[2], PanelHalfHeight);

    displayGlobalData.set(display.clip, 0);
    displayGlobalData.set([eye[0], eye[1], eye[2], 0], 16);
    displayGlobalData.set([DisplayWidth, DisplayHeight, 0, 0], 20);
    displayGlobalData.set([a, a * 1.02, a * 1.08, 0], 24);
    displayGlobalData.set([elapsed, dt, 0, 0], 28);
    device.queue.writeBuffer(displayGlobals, 0, displayGlobalData);

    if (State.backdrop === 'live') {
      // Line width is in the DISPLAY's pixels now, because that is where the strip is rastered.
      packFibre(fibreData, StreakPreset, {
        time: elapsed, width: DisplayWidth, height: DisplayHeight, projectionScale: displayScale,
        rows: placements[handles.housing],
      });
      device.queue.writeBuffer(fibreBuffer, 0, fibreData);
    }

    if (packed.count > capacity) {
      capacity = packed.count * 2;
      figureBuffer.destroy();
      figureBuffer = device.createBuffer({
        size: capacity * FloatsPerFigure * 4,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
      });
      bind = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: { buffer: displayGlobals } },
                  { binding: 1, resource: { buffer: figureBuffer } }],
      });
    }
    device.queue.writeBuffer(figureBuffer, 0, packed.data);

    const encoder = device.createCommandEncoder();
    const sheet = (view, clear) => encoder.beginRenderPass({
      colorAttachments: [{ view, clearValue: clear, loadOp: 'clear', storeOp: 'store' }],
    });

    // ── pass one: the display, into its own texture, through the panel's own frustum ───────────
    //
    // The clear is the black an unlit LCD sits at rather than the room's colour: this is inside
    // the device now and the room cannot reach it.
    const screen = sheet(displayView, { r: 0.0032, g: 0.0036, b: 0.0048, a: 1 });
    screen.setPipeline(pipeline);
    screen.setBindGroup(0, bind);
    screen.draw(4, splitAt, 0, 0);

    if (State.backdrop === 'live') {
      screen.setPipeline(fibrePipeline);
      screen.setBindGroup(0, fibreBind);
      screen.draw(StreakPreset.strands * StreakPreset.segments * 6);
      screen.setPipeline(sparkPipeline);
      screen.setBindGroup(0, sparkBind);
      screen.draw(StreakPreset.strands * 6);
    }

    screen.setPipeline(pipeline);
    screen.setBindGroup(0, bind);
    screen.draw(4, packed.count - splitAt, 0, splitAt);
    screen.end();

    // ── pass two: the glow. Bright pass down to a quarter, then one blur along each axis ───────
    const cut = sheet(bloomView[0], { r: 0, g: 0, b: 0, a: 1 });
    cut.setPipeline(bloomCutPipeline);
    cut.setBindGroup(0, bloomCutBind);
    cut.draw(3);
    cut.end();

    const across = sheet(bloomView[1], { r: 0, g: 0, b: 0, a: 1 });
    across.setPipeline(bloomAcrossPipeline);
    across.setBindGroup(0, bloomAcrossBind);
    across.draw(3);
    across.end();

    const down = sheet(bloomView[0], { r: 0, g: 0, b: 0, a: 1 });
    down.setPipeline(bloomDownPipeline);
    down.setBindGroup(0, bloomDownBind);
    down.draw(3);
    down.end();

    // ── pass three: the tablet in the room, which reads the display as a texture ───────────────
    const pass = sheet(context.getCurrentTexture().createView(),
                       { r: 0.012, g: 0.014, b: 0.018, a: 1 });
    pass.setPipeline(roomPipeline);
    pass.setBindGroup(0, roomBind);
    pass.draw(3);

    pass.setPipeline(glassPipeline);
    pass.setBindGroup(0, glassBind);
    pass.draw(4);
    pass.end();
    device.queue.submit([encoder.finish()]);

    frames++;
    if (firstFrame || now - frameClock > 0.5) {
      firstFrame = false;
      rate = frames / (now - frameClock);
      frames = 0; frameClock = now;
      const strands = State.backdrop === 'live' ? ` · ${StreakPreset.strands} fibres` : '';
      document.getElementById('readout').textContent =
        `${packed.count} figures${strands} · ${rate.toFixed(0)} fps`;
    }
    requestAnimationFrame(guarded);
  }

  // A throw inside a requestAnimationFrame callback is logged and then swallowed: the loop simply
  // stops and the canvas freezes with no explanation. Catch it where it can still be shown.
  function guarded() {
    try { frame(); } catch (trouble) { announce(trouble); }
  }
  requestAnimationFrame(guarded);
}

// ── the control rail ─────────────────────────────────────────────────────────────────────────────

const RungNotes = {
  live: 'Real 3D Bézier fibres, drawn as geometry in the volume behind the glass and clipped by the '
      + "panel's own rounded rectangle. Parallax is real — orbit the tablet and the light swims "
      + 'behind the surface.',
  field: 'The analytic plane field. Flat by construction: every strand is at the same depth, so it '
       + 'slides with the surface instead of swimming behind it. This is the rung a reflection or a '
       + 'distant panel gets, because it can be answered at a hit point.',
  off: 'No backdrop. The instrument on a bare card.',
};

const StreakFields = [
  ['strands', 0, 1, 48, 1], ['amplitude', 1, 0, 0.06, 0.001], ['waves', 2, 0.2, 8, 0.1],
  ['speed', 3, 0, 3, 0.01], ['tail', 4, 0.02, 1.5, 0.01], ['seed', 5, 0, 64, 1],
  ['intensity', 6, 0, 4, 0.01], ['core', 7, 0.0005, 0.012, 0.0001],
];

// Every lookup here goes through this. A control that is missing from the markup is a bug worth
// seeing, but it is not worth losing the whole panel over — the page reports it and carries on.
function control(selector) {
  const found = document.querySelector(selector);
  if (!found) console.warn(`SpatialHud: no control matches ${selector}`);
  return found;
}

function wireControls(screens) {
  for (const [name, index] of StreakFields) {
    const input = control(`[data-streak="${name}"]`);
    if (!input) continue;
    input.value = State.streak[index];
    const readout = input.nextElementSibling;
    const show = () => {
      if (readout) readout.textContent = Number(input.value).toString();
    };
    show();
    input.addEventListener('input', () => {
      // The frame hands the current preset to whichever screen is up, so the slider only has to
      // write the one copy that both of them read.
      State.streak[index] = Number(input.value);
      show();
    });
  }

  for (const button of document.querySelectorAll('[data-screen]')) {
    button.addEventListener('click', () => {
      State.screen = button.dataset.screen;
      showScreen();
    });
  }
  showScreen();

  const fieldOpacity = control('[data-field="fieldOpacity"]');
  if (fieldOpacity) {
    State.fieldOpacity = Number(fieldOpacity.value);
    fieldOpacity.addEventListener('input', (event) => {
      State.fieldOpacity = Number(event.target.value);
    });
  }

  for (const button of document.querySelectorAll('[data-rung]')) {
    button.addEventListener('click', () => {
      State.backdrop = button.dataset.rung;
      showRung();
    });
  }

  for (const name of ['speed', 'boost', 'regen', 'ambient']) {
    const input = control(`[data-field="${name}"]`);
    if (!input) continue;
    input.addEventListener('input', () => {
      State[name] = Number(input.value);
      if (name !== 'ambient') stopDemo();
    });
  }
  control('[data-field="sport"]')?.addEventListener('change', (event) => {
    State.sport = event.target.checked ? 1 : 0;
    stopDemo();
  });
  control('[data-field="demo"]')?.addEventListener('change', (event) => {
    State.demo = event.target.checked;
  });
  showRung();
  reflectControls();
}

function stopDemo() {
  State.demo = false;
  const demo = document.querySelector('[data-field="demo"]');
  if (demo) demo.checked = false;
}

const ScreenNotes = {
  home: 'FRONTIER OS — the clock, the app wall and the dock. The streak field is the WALLPAPER here, '
      + 'dimmed to 45% so it sits behind eight labels instead of competing with them.',
  dash: 'The instrument composition. On a device that is an OS, this is one app among the eight — '
      + 'the one the DRIVE tile opens.',
};

function showScreen() {
  const note = document.getElementById('screen-note');
  if (note) note.textContent = ScreenNotes[State.screen] ?? '';
  for (const button of document.querySelectorAll('[data-screen]')) {
    button.classList.toggle('on', button.dataset.screen === State.screen);
  }
}

function showRung() {
  const note = document.getElementById('rung-note');
  if (note) note.textContent = RungNotes[State.backdrop];
  for (const button of document.querySelectorAll('[data-rung]')) {
    button.classList.toggle('on', button.dataset.rung === State.backdrop);
  }
}

function reflectControls() {
  for (const name of ['speed', 'boost', 'regen']) {
    const input = document.querySelector(`[data-field="${name}"]`);
    if (input && document.activeElement !== input) input.value = State[name];
  }
  const sport = document.querySelector('[data-field="sport"]');
  if (sport) sport.checked = State.sport > 0.5;
}

function wireCamera(canvas) {
  let dragging = false, lastX = 0, lastY = 0;
  canvas.addEventListener('pointerdown', (event) => {
    dragging = true; lastX = event.clientX; lastY = event.clientY;
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener('pointerup', (event) => {
    dragging = false; canvas.releasePointerCapture(event.pointerId);
  });
  canvas.addEventListener('pointermove', (event) => {
    if (!dragging) return;
    State.orbit += (event.clientX - lastX) * 0.006;
    State.tilt = Math.max(-1.2, Math.min(1.2, State.tilt + (event.clientY - lastY) * 0.005));
    lastX = event.clientX; lastY = event.clientY;
  });
  canvas.addEventListener('wheel', (event) => {
    event.preventDefault();
    State.distance = Math.max(0.3, Math.min(3.0, State.distance * (1 + event.deltaY * 0.0012)));
  }, { passive: false });
}

// 🔴 NOTHING MAY FAIL SILENTLY.
//
//    The first time this page broke, it broke invisibly: one exception while wiring a control left
//    a black canvas, an untouched "starting" readout and no message anywhere. A WebGPU page that
//    cannot say why it is blank is almost impossible to report a bug against, so every path into
//    start() now ends somewhere visible.
function announce(trouble) {
  const notice = document.getElementById('notice');
  notice.hidden = false;
  notice.textContent = String(trouble && trouble.stack ? trouble.stack : trouble);
  const readout = document.getElementById('readout');
  if (readout) readout.textContent = 'stopped';
  console.error(trouble);
}

function boot() {
  try {
    start().catch(announce);
  } catch (trouble) {
    announce(trouble);
  }
}

// A module script is deferred, so DOMContentLoaded has already fired by the time this runs.
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', boot);
} else {
  boot();
}
