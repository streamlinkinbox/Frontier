// Lit scene: sand ground, crates, car body + wheels. Instanced meshes with per-instance model/colour.
import { mat4 } from './math.js?v=21';

const SHADER = /* wgsl */`
struct Cam { viewProj: mat4x4f, invViewProj: mat4x4f, camPos: vec4f, lightDir: vec4f, screen: vec4f, extra: vec4f, extra2: vec4f, smO: vec4f, smD: vec4f, pud: array<vec4f, 3> };
struct Inst { model: mat4x4f, color: vec4f };
@group(0) @binding(0) var<uniform> cam: Cam;
@group(0) @binding(1) var<storage, read> insts: array<Inst>;
@group(0) @binding(2) var<storage, read> smokeLight: array<vec4f>;
// smoke shadow: opacity-weighted optical depth toward the sun, trilinear from the smoke light volume
fn smokeShadow(wp: vec3f) -> f32 {
  if (cam.smD.w <= 0.0) { return 1.0; }
  let dims = vec3<i32>(cam.smD.xyz);
  let g = (wp - cam.smO.xyz) / cam.smO.w - 0.5 + vec3f(0.0, 0.6, 0.0);
  if (any(g < vec3f(0.0)) || any(g > vec3f(dims - vec3<i32>(1)))) { return 1.0; }
  let b = floor(g); let f = g - b; let i0 = vec3<i32>(b);
  var od = 0.0;
  for (var k = 0; k < 8; k++) {
    let o = vec3<i32>(k & 1, (k >> 1) & 1, (k >> 2) & 1);
    let q = min(i0 + o, dims - vec3<i32>(1));
    let wgt = select(1.0 - f.x, f.x, o.x == 1) * select(1.0 - f.y, f.y, o.y == 1) * select(1.0 - f.z, f.z, o.z == 1);
    od += smokeLight[(q.z * dims.y + q.y) * dims.x + q.x].w * wgt;
  }
  return mix(1.0, exp(-od), cam.smD.w);
}
struct VO { @builtin(position) pos: vec4f, @location(0) wp: vec3f, @location(1) n: vec3f, @location(2) col: vec4f, @location(3) on: vec3f };
@vertex fn vs(@location(0) p: vec3f, @location(1) n: vec3f, @builtin(instance_index) ii: u32) -> VO {
  let I = insts[ii];
  var o: VO;
  let w = I.model * vec4f(p, 1.0);
  o.pos = cam.viewProj * w;
  o.wp = w.xyz;
  o.n = normalize((I.model * vec4f(n, 0.0)).xyz);
  o.on = n;
  o.col = I.color;
  return o;
}
fn hash2(p: vec2f) -> f32 { return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453); }
fn vnoise(p: vec2f) -> f32 {
  let i = floor(p); let f = fract(p); let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash2(i), hash2(i + vec2f(1, 0)), u.x), mix(hash2(i + vec2f(0, 1)), hash2(i + vec2f(1, 1)), u.x), u.y);
}
// puddle dents: normalised distance to the irregular shoreline (same formula as water.js)
fn pudDn(i: i32, p: vec2f) -> f32 {
  let P = cam.pud[i];
  if (P.z <= 0.0) { return 9.0; }
  let q = p - P.xy; let a = atan2(q.y, q.x); let s = f32(i) * 2.7 + 1.3;
  return length(q) / (P.z * (1.0 + 0.12 * sin(3.0 * a + s) + 0.07 * sin(5.0 * a + s * 2.1) + 0.04 * sin(9.0 * a + s * 0.7)));
}
fn boxShadow(wp: vec3f, c: vec2f, yaw: f32, he: vec2f, soft: f32) -> f32 {
  let d = wp.xz - c;
  let l = vec2f(d.x * cos(yaw) - d.y * sin(yaw), d.x * sin(yaw) + d.y * cos(yaw));
  let q = abs(l) - he;
  let dist = length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0);
  return 1.0 - smoothstep(-soft, soft, dist);
}
@fragment fn fs(i: VO) -> @location(0) vec4f {
  let L = normalize(cam.lightDir.xyz);
  var n = normalize(i.n);
  var base = i.col.rgb;
  let mat = i.col.w;
  if (mat > 0.5 && mat < 1.5) {
    // procedural sand ground with wind ripples
    let p = i.wp.xz;
    // the dented puddle beds are drawn by water.js: cut the flat ground away inside each shoreline
    let dnMin = min(pudDn(0, p), min(pudDn(1, p), pudDn(2, p)));
    if (dnMin < 1.0) { discard; }
    let warp = vnoise(p * 0.15) * 6.0;
    let rip = sin(dot(p, vec2f(0.8, 0.45)) * 3.2 + warp);
    let nz = vnoise(p * 1.7) * 0.6 + vnoise(p * 9.0) * 0.4;
    base = mix(vec3f(0.74, 0.58, 0.38), vec3f(0.86, 0.72, 0.50), nz * 0.8 + 0.1 * rip);
    n = normalize(vec3f(0.08 * rip * 0.8, 1.0, 0.05 * rip));
    let g = abs(fract(p / 10.0 + 0.5) - 0.5) * 10.0;
    base *= mix(0.93, 1.0, smoothstep(0.0, 0.06, min(g.x, g.y)));
    base *= mix(0.62, 1.0, smoothstep(1.0, 1.18, dnMin));   // damp sand ring around each puddle
    // soft contact shadow under the car
    let sh = boxShadow(i.wp, cam.extra.xy, cam.extra.z, vec2f(1.0, 2.2), 0.6);
    base *= 1.0 - 0.45 * sh;
  } else if (mat > 1.5 && mat < 2.5) {
    // tyre: hub on the side faces
    if (abs(i.on.x) > 0.9) { base = vec3f(0.55, 0.56, 0.6); }
  }
  let ssh = smokeShadow(i.wp);
  let diff = max(dot(n, L), 0.0) * ssh;
  let hemi = mix(vec3f(0.35, 0.3, 0.25), vec3f(0.55, 0.65, 0.8), n.y * 0.5 + 0.5);
  var c = base * (hemi * (0.55 + 0.2 * ssh) + vec3f(1.0, 0.94, 0.85) * diff * 0.95);
  let v = normalize(cam.camPos.xyz - i.wp);
  let hv = normalize(L + v);
  if (mat < 0.5) { c += vec3f(0.25) * pow(max(dot(n, hv), 0.0), 40.0); }
  // explosion flash: warm point light
  if (cam.extra2.w > 0.01) {
    let lv = cam.extra2.xyz - i.wp;
    let li = cam.extra2.w / (1.0 + dot(lv, lv) * 0.06);
    c += base * vec3f(1.0, 0.55, 0.22) * li * (0.35 + 0.65 * max(dot(n, normalize(lv)), 0.0));
  }
  let fog = 1.0 - exp(-distance(cam.camPos.xyz, i.wp) * 0.006);
  c = mix(c, vec3f(0.78, 0.8, 0.82), fog);
  return vec4f(c, 1.0);
}
`;

function cube() {
  const f = [
    [[1, 0, 0], [0, 1, 0], [0, 0, 1]], [[-1, 0, 0], [0, 1, 0], [0, 0, -1]],
    [[0, 1, 0], [0, 0, 1], [1, 0, 0]], [[0, -1, 0], [0, 0, -1], [1, 0, 0]],
    [[0, 0, 1], [1, 0, 0], [0, 1, 0]], [[0, 0, -1], [-1, 0, 0], [0, 1, 0]],
  ];
  const v = [];
  for (const [n, a, b] of f) {
    const corner = (s, t) => [n[0] + a[0] * s + b[0] * t, n[1] + a[1] * s + b[1] * t, n[2] + a[2] * s + b[2] * t];
    const q = [corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, -1), corner(1, 1), corner(-1, 1)];
    // ensure CCW when viewed from outside
    const e1 = q[1].map((x, i) => x - q[0][i]), e2 = q[2].map((x, i) => x - q[0][i]);
    const cr = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    if (cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2] < 0) { [q[1], q[2]] = [q[2], q[1]]; [q[4], q[5]] = [q[5], q[4]]; }
    for (const p of q) v.push(...p, ...n);
  }
  return new Float32Array(v);
}
function cylinder(seg = 20) { // axis X, radius 1, x in [-1,1]
  const v = [];
  for (let i = 0; i < seg; i++) {
    const a0 = (i / seg) * Math.PI * 2, a1 = ((i + 1) / seg) * Math.PI * 2;
    const y0 = Math.cos(a0), z0 = Math.sin(a0), y1 = Math.cos(a1), z1 = Math.sin(a1);
    // side (CCW from outside)
    v.push(-1, y0, z0, 0, y0, z0, -1, y1, z1, 0, y1, z1, 1, y1, z1, 0, y1, z1);
    v.push(-1, y0, z0, 0, y0, z0, 1, y1, z1, 0, y1, z1, 1, y0, z0, 0, y0, z0);
    // caps
    v.push(1, 0, 0, 1, 0, 0, 1, y0, z0, 1, 0, 0, 1, y1, z1, 1, 0, 0);
    v.push(-1, 0, 0, -1, 0, 0, -1, y1, z1, -1, 0, 0, -1, y0, z0, -1, 0, 0);
  }
  return new Float32Array(v);
}
function plane(s) {
  return new Float32Array([-s, 0, -s, 0, 1, 0, -s, 0, s, 0, 1, 0, s, 0, s, 0, 1, 0, -s, 0, -s, 0, 1, 0, s, 0, s, 0, 1, 0, s, 0, -s, 0, 1, 0]);
}

export class Scene {
  constructor(device, format, checkModule, crates) {
    this.device = device;
    this.crates = crates;
    const mod = checkModule(device.createShaderModule({ code: SHADER, label: 'scene' }), 'scene');
    this.pipe = device.createRenderPipeline({
      layout: 'auto', label: 'scene',
      vertex: { module: mod, entryPoint: 'vs', buffers: [{ arrayStride: 24, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }, { shaderLocation: 1, offset: 12, format: 'float32x3' }] }] },
      fragment: { module: mod, entryPoint: 'fs', targets: [{ format }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less' },
    });
    this.meshes = {};
    for (const [k, data] of Object.entries({ cube: cube(), cyl: cylinder(), plane: plane(400) })) {
      const b = device.createBuffer({ size: data.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
      device.queue.writeBuffer(b, 0, data);
      this.meshes[k] = { buf: b, count: data.length / 6 };
    }
    this.maxInst = 128;
    this.inst = new Float32Array(this.maxInst * 20);
    this.ibuf = device.createBuffer({ size: this.inst.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
    this.camUbo = device.createBuffer({ size: 304, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.smokeInfo = [0, 0, 0, 1, 1, 1, 1, 0];
  }

  setSmokeLight(buf) {
    this.bg = this.device.createBindGroup({ layout: this.pipe.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: this.camUbo } }, { binding: 1, resource: { buffer: this.ibuf } }, { binding: 2, resource: { buffer: buf } }] });
  }

  writeCamera(viewProj, camPos, lightDir, w, h, frame, car, flash = [0, 0, 0, 0]) {
    const d = new Float32Array(76);
    d.set(viewProj, 0); d.set(mat4.invert(viewProj), 16);
    d.set([...camPos, 1], 32); d.set([...lightDir, 0], 36); d.set([w, h, frame % 1000, 0], 40);
    d.set([car.x, car.z, car.heading, 0], 44);
    d.set(flash, 48);
    d.set(this.smokeInfo, 52);
    (this.puddles || []).slice(0, 3).forEach((p, k) => d.set([p.x, p.z, p.r, p.mud], 60 + k * 4));   // Cam.pud starts at float 60 (after smO/smD)
    this.device.queue.writeBuffer(this.camUbo, 0, d);
  }

  buildInstances(car, debris = []) {
    const list = { plane: [], cube: [], cyl: [] };
    const push = (mesh, m, col) => list[mesh].push([m, col]);
    push('plane', mat4.identity(), [1, 1, 1, 1]);
    for (const c of this.crates) if (!c.exploded) push('cube', mat4.trs(c.x, c.hy + (c.y || 0), c.z, c.yaw, c.hx, c.hy, c.hz, c.rx || 0, c.rz || 0), c.drawColor || c.color);
    for (const b of debris) push('cube', mat4.trs(b.x, b.y, b.z, b.yaw, b.s, b.s, b.s, b.rx, b.rz), b.color);
    // car body (roll about forward axis, pitch about right axis)
    const [fx, fz] = car.fwd();
    const body = (lx, ly, lz, hx, hy, hz, col) => {
      const [wx, wz] = car.toWorld(lx, lz);
      push('cube', mat4.trs(car.x + wx, ly + (car.bodyOff || 0), car.z + wz, car.heading, hx, hy, hz, -car.pitch + (car.tPitch || 0), car.roll + (car.tRoll || 0)), col);
    };
    body(0, car.bodyY, 0, car.halfExt[0], car.halfExt[1] * 0.75, car.halfExt[2], [0.85, 0.12, 0.1, 0]);
    body(0, car.bodyY + 0.5, -0.25, 0.78, 0.26, 1.05, [0.12, 0.14, 0.18, 0]);
    body(0, car.bodyY + 0.12, car.halfExt[2] - 0.05, 0.7, 0.08, 0.06, [1, 0.95, 0.75, 0]);
    for (const wh of car.wheels) {
      const yaw = car.heading + (wh.front ? car.steer : 0);
      push('cyl', mat4.trs(wh.pos[0], car.wheelR + (wh.gy || 0), wh.pos[2], yaw, car.wheelW / 2, car.wheelR, car.wheelR, wh.spin), [0.08, 0.08, 0.09, 2]);
    }
    void fx; void fz;
    let k = 0;
    this.draws = [];
    for (const mesh of ['plane', 'cube', 'cyl']) {
      const first = k;
      for (const [m, col] of list[mesh]) { this.inst.set(m, k * 20); this.inst.set(col, k * 20 + 16); k++; }
      this.draws.push({ mesh, first, count: k - first });
    }
    this.device.queue.writeBuffer(this.ibuf, 0, this.inst, 0, k * 20);
  }

  draw(pass) {
    pass.setPipeline(this.pipe);
    pass.setBindGroup(0, this.bg);
    for (const d of this.draws) {
      if (!d.count) continue;
      const m = this.meshes[d.mesh];
      pass.setVertexBuffer(0, m.buf);
      pass.draw(m.count, d.count, 0, d.first);
    }
  }
}
