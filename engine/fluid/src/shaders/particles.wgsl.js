// GPU particle system for spray, mist, foam and rain.
//  * Emission from the heightfield uses the potentials of Ihmsen et al. 2012 ("Unified spray, foam
//    and air bubbles for particle-based fluids") adapted to a 2.5D surface: trapped-air (shear /
//    colliding flow), wave-crest (convex surface moving along its normal) and kinetic energy.
//  * Spray carries real water volume: it is removed from the grid on emission and deposited back
//    (into water or onto dry ground) when it lands, so splashes genuinely move water around.
//  * Spray hitting water turns into foam; foam rides and is advected by the simulated surface flow,
//    turns with its vorticity and decays (faster in turbulence and when stranded on dry ground).
//    Foam particles are rafts of bubbles rendered as a continuous surface (FOAM_SPLAT/COMPOSITE).
import { SIM_STRUCT, PART_STRUCT, HELPERS, IDX, VEL_ACCESS, STATE_ACCESS } from './common.wgsl.js';

const ALLOC = /* wgsl */ `
fn allocParticle() -> i32 {
  let prev = atomicSub(&counters[0], 1);
  if (prev <= 0) { atomicAdd(&counters[0], 1); return -1; }
  return i32(deadList[prev - 1]);
}
fn depositAt(lp: vec2f, vol: f32, mud: f32) {
  let i = clamp(i32(floor(lp.x / S.dx)), 0, S.nx - 1);
  let j = clamp(i32(floor(lp.y / S.dx)), 0, S.ny - 1);
  let k = j * S.nx + i;
  atomicAdd(&deposit[2 * k], i32(round(vol * DEPOSIT_SCALE)));
  atomicAdd(&deposit[2 * k + 1], i32(round(vol * mud * DEPOSIT_SCALE)));
}
`;

// ---------------------------------------------------------------------------------------------
export const EMIT_SURFACE = /* wgsl */ `
${SIM_STRUCT}
${PART_STRUCT}
${HELPERS}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<uniform> PP: PartParams;
@group(0) @binding(2) var<storage, read> hc: array<vec2f>;
@group(0) @binding(3) var<storage, read> vel: array<vec2f>;
@group(0) @binding(4) var<storage, read> bed: array<f32>;
@group(0) @binding(5) var<storage, read> aux: array<vec4f>;
@group(0) @binding(6) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(7) var<storage, read> deadList: array<u32>;
@group(0) @binding(8) var<storage, read_write> counters: array<atomic<i32>>;
@group(0) @binding(9) var<storage, read_write> deposit: array<atomic<i32>>;
${IDX}
${VEL_ACCESS}
${ALLOC}
fn etaOr(i: i32, j: i32, fallback: f32) -> f32 {
  let k = ci(i, j);
  let h = hc[k].x;
  if (h < PP.minDepth * 0.5) { return fallback; }
  return bed[k] + h;
}
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = i32(gid.x); let j = i32(gid.y);
  if (PP.enabled == 0u || i < 1 || j < 1 || i >= S.nx - 1 || j >= S.ny - 1) { return; }
  let k = j * S.nx + i;
  let d = hc[k];
  let h = d.x;
  if (h < PP.minDepth) { return; }
  let eta = bed[k] + h;
  let w = clamp((eta - aux[k].x) / max(S.frameDt, 1e-4), -20.0, 20.0);   // surface vertical speed
  let U = velC(i, j);
  let eE = etaOr(i + 1, j, eta); let eW = etaOr(i - 1, j, eta);
  let eN = etaOr(i, j + 1, eta); let eS = etaOr(i, j - 1, eta);
  let grad = vec2f(eE - eW, eN - eS) / (2.0 * S.dx);
  let n = normalize(vec3f(-grad.x, 1.0, -grad.y));
  let lap = (eE + eW + eN + eS - 4.0 * eta) / S.dx;     // dimensionless curvature (x dx)
  let curv = max(-lap, 0.0);
  let vs = vec3f(U.x, w, U.y);
  let vlen = length(vs);
  // wave-crest potential: convex surface moving outward along its normal
  var wc = 0.0;
  if (vlen > 1e-3 && dot(vs / vlen, n) >= 0.6) { wc = curv; }
  // trapped-air potential: relative velocity of neighbours, weighted towards colliding motion
  var ta = 0.0;
  var offs = array<vec2i, 4>(vec2i(1, 0), vec2i(-1, 0), vec2i(0, 1), vec2i(0, -1));
  for (var q = 0; q < 4; q++) {
    let o = offs[q];
    let kq = ci(i + o.x, j + o.y);
    if (hc[kq].x < PP.minDepth) { continue; }
    let vij = U - velC(i + o.x, j + o.y);
    let l = length(vij);
    if (l < 1e-4) { continue; }
    let xij = -vec2f(f32(o.x), f32(o.y));
    ta += l * (1.0 - dot(vij / l, xij));
  }
  let ke = 0.5 * vlen * vlen;
  let pWC = phi(wc, PP.wcMin, PP.wcMax);
  let pTA = phi(ta, PP.taMin, PP.taMax);
  let pot = phi(ke, PP.keMin, PP.keMax) * (PP.kTA * pTA + PP.kWC * pWC);
  if (pot <= 0.0) { return; }
  var seed = hashu(u32(k) * 9781u + S.frame * 6271u + 17u);
  let expected = pot * PP.emitScale * S.frameDt * S.dx * S.dx;
  var count = i32(floor(expected));
  if (rnd(&seed) < fract(expected)) { count += 1; }
  count = min(count, i32(PP.maxPerCell));
  // never throw more water than a quarter of the column holds
  count = min(count, i32(0.25 * h * S.dx * S.dx / max(PP.sprayVolume, 1e-9)));
  if (count <= 0) { return; }
  let sprayBias = (pWC + 0.15) / (pWC + pTA + 0.15);
  let wx = S.originX; let wz = S.originZ;
  var thrown = 0.0;
  for (var e = 0; e < count; e++) {
    let isSpray = rnd(&seed) < mix(1.0 - PP.foamFraction, 0.97, sprayBias) && vlen > 0.25;
    // foam rafts are large: only a share of the foam emissions spawn one
    if (!isSpray && rnd(&seed) >= PP.foamEmitProb) { continue; }
    let idx = allocParticle();
    if (idx < 0) { break; }
    let lp = (vec2f(f32(i), f32(j)) + vec2f(rnd(&seed), rnd(&seed))) * S.dx;
    var p: Particle;
    if (isSpray) {
      let isMist = rnd(&seed) < PP.mistFraction;
      let spread = (vec2f(rnd(&seed), rnd(&seed)) - 0.5) * (0.45 * vlen + 0.3);
      let up = max(w, 0.0) * (0.5 + 0.9 * rnd(&seed)) + PP.sprayUp * vlen * (0.3 + 0.7 * rnd(&seed)) + 0.2;
      let fw = 0.8 + 0.5 * rnd(&seed);
      p.pos = vec3f(wx + lp.x, eta + 0.02, wz + lp.y);
      p.vel = vec3f(U.x * fw + spread.x, up, U.y * fw + spread.y);
      p.mud = d.y;
      if (isMist) {
        p.kind = KIND_MIST; p.vol = 0.0;
        p.size = PP.mistSize * (0.6 + 0.8 * rnd(&seed));
        p.maxLife = PP.mistLife * (0.5 + rnd(&seed));
        p.vel *= 0.6;
      } else {
        p.kind = KIND_SPRAY; p.vol = PP.sprayVolume;
        p.size = PP.spraySize * (0.5 + rnd(&seed));
        p.maxLife = PP.sprayLife * (0.6 + 0.8 * rnd(&seed));
        thrown += PP.sprayVolume;
      }
    } else {
      p.kind = KIND_FOAM; p.vol = 0.0; p.mud = d.y;
      p.pos = vec3f(wx + lp.x, eta + 0.01, wz + lp.y);
      p.vel = vec3f(U.x, 1.0, U.y);   // vel.y: full-thickness whitewater
      p.size = PP.foamSize * (0.5 + rnd(&seed));
      p.maxLife = PP.foamLife * (0.4 + 0.9 * rnd(&seed));
    }
    p.life = p.maxLife;
    particles[idx] = p;
  }
  if (thrown > 0.0) {
    atomicAdd(&deposit[2 * k], -i32(round(thrown * DEPOSIT_SCALE)));
    atomicAdd(&deposit[2 * k + 1], -i32(round(thrown * d.y * DEPOSIT_SCALE)));
  }
}
`;

// ---------------------------------------------------------------------------------------------
// CPU-driven emitters (wheel spray, mud roost, rain, scripted splashes)
export const EMIT_CPU = /* wgsl */ `
${SIM_STRUCT}
${PART_STRUCT}
${HELPERS}
struct Emitter {
  pos: vec4f,     // world xyz, count (particles this frame)
  vel: vec4f,     // base velocity xyz, spread (m/s)
  shape: vec4f,   // radiusX, radiusZ, kind, takeFromWater (0/1)
  prm: vec4f,     // size, volume, life, mud (-1 = sample from grid)
};
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<uniform> PP: PartParams;
@group(0) @binding(2) var<storage, read> emitters: array<Emitter>;
@group(0) @binding(3) var<storage, read> hc: array<vec2f>;
@group(0) @binding(4) var<storage, read> bed: array<f32>;
@group(0) @binding(5) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(6) var<storage, read> deadList: array<u32>;
@group(0) @binding(7) var<storage, read_write> counters: array<atomic<i32>>;
@group(0) @binding(8) var<storage, read_write> deposit: array<atomic<i32>>;
${IDX}
${STATE_ACCESS}
${ALLOC}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let ei = gid.y;
  if (ei >= PP.numEmitters || PP.enabled == 0u) { return; }
  let E = emitters[ei];
  let count = u32(E.pos.w);
  var t = gid.x;
  var seed = hashu(gid.x * 7919u + ei * 104729u + S.frame * 1299709u);
  // grid-stride over this emitter's particles (dispatch x covers up to 64*X threads)
  loop {
    if (t >= count) { break; }
    let r = (vec2f(rnd(&seed), rnd(&seed)) * 2.0 - 1.0);
    var wp = E.pos.xyz + vec3f(r.x * E.shape.x, 0.0, r.y * E.shape.y);
    let lp = vec2f(wp.x - S.originX, wp.z - S.originZ);
    let kind = u32(E.shape.z);
    var mud = E.prm.w;
    var ok = true;
    if (E.shape.w > 0.5) {
      // must draw real water from the grid; skip if there is none here
      let st = sampleState(lp);
      if (st.y < 0.004) { ok = false; }
      if (mud < 0.0) { mud = st.z; }
      wp.y = max(wp.y, st.w);
    }
    if (mud < 0.0) { mud = 0.0; }
    if (ok) {
      let idx = allocParticle();
      if (idx < 0) { break; }
      var p: Particle;
      p.pos = wp;
      let sp = vec3f(rnd(&seed) - 0.5, rnd(&seed) - 0.5, rnd(&seed) - 0.5) * 2.0 * E.vel.w;
      p.vel = E.vel.xyz + sp;
      p.kind = kind;
      p.size = E.prm.x * (0.6 + 0.8 * rnd(&seed));
      p.vol = E.prm.y;
      // foam carries no water: vel.y = full thickness, vol = rotation (see PART_STRUCT)
      if (kind == KIND_FOAM) { p.vel.y = 1.0; p.vol = 0.0; }
      p.mud = mud;
      p.maxLife = E.prm.z * (0.6 + 0.8 * rnd(&seed));
      p.life = p.maxLife;
      particles[idx] = p;
      if (E.shape.w > 0.5 && p.vol > 0.0) { depositAt(lp, -p.vol, mud); }
    }
    t += 64u * 16u;
  }
}
`;

// ---------------------------------------------------------------------------------------------
export const SIMULATE = /* wgsl */ `
${SIM_STRUCT}
${PART_STRUCT}
${HELPERS}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<uniform> PP: PartParams;
@group(0) @binding(2) var<storage, read> hc: array<vec2f>;
@group(0) @binding(3) var<storage, read> vel: array<vec2f>;
@group(0) @binding(4) var<storage, read> bed: array<f32>;
@group(0) @binding(5) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(6) var<storage, read_write> deadList: array<u32>;
@group(0) @binding(7) var<storage, read_write> drawList: array<u32>;
@group(0) @binding(8) var<storage, read_write> counters: array<atomic<i32>>;
@group(0) @binding(9) var<storage, read_write> deposit: array<atomic<i32>>;
${IDX}
${VEL_ACCESS}
${STATE_ACCESS}
fn depositAt(lp: vec2f, vol: f32, mud: f32) {
  let i = clamp(i32(floor(lp.x / S.dx)), 0, S.nx - 1);
  let j = clamp(i32(floor(lp.y / S.dx)), 0, S.ny - 1);
  let k = j * S.nx + i;
  atomicAdd(&deposit[2 * k], i32(round(vol * DEPOSIT_SCALE)));
  atomicAdd(&deposit[2 * k + 1], i32(round(vol * mud * DEPOSIT_SCALE)));
}
fn kill(idx: u32) {
  particles[idx].life = 0.0;
  let slot = atomicAdd(&counters[0], 1);
  deadList[slot] = idx;
}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let idx = gid.x;
  if (idx >= PP.maxParticles) { return; }
  var p = particles[idx];
  if (p.life <= 0.0) { return; }
  let dt = PP.dt;
  var seed = hashu(idx * 7919u + S.frame * 104729u);
  let L = vec2f(f32(S.nx), f32(S.ny)) * S.dx;
  var lp = vec2f(p.pos.x - S.originX, p.pos.z - S.originZ);
  if (lp.x < 0.0 || lp.y < 0.0 || lp.x >= L.x || lp.y >= L.y) { kill(idx); return; }

  if (p.kind == KIND_FOAM) {
    let st = sampleState(lp);
    let U = sampleVel(lp);
    var decay = 1.0;
    if (st.y < 0.003) { decay = PP.foamStrandDecay; }
    // bubbles burst faster in fast, turbulent flow
    decay += PP.foamDecayTurb * length(U);
    let jit = (vec2f(rnd(&seed), rnd(&seed)) - 0.5) * PP.foamDiffusion * sqrt(dt) * 3.46;
    let drift = U * PP.surfaceDrift + vec2f(PP.windX, PP.windZ) * 0.015;
    // a raft turns with the local fluid rotation (half the vorticity), so foam swirls in eddies.
    // Foam carries no water, so its vol field holds this accumulated angle (radians).
    let ex = vec2f(S.dx, 0.0); let ez = vec2f(0.0, S.dx);
    let curl = ((sampleVel(lp + ex).y - sampleVel(lp - ex).y) - (sampleVel(lp + ez).x - sampleVel(lp - ez).x)) / (2.0 * S.dx);
    p.vol = p.vol + 0.5 * clamp(curl, -20.0, 20.0) * dt;
    p.pos.x += drift.x * dt + jit.x;
    p.pos.z += drift.y * dt + jit.y;
    lp = vec2f(p.pos.x - S.originX, p.pos.z - S.originZ);
    let st2 = sampleState(lp);
    p.pos.y = st2.w + 0.012;
    p.vel = vec3f(drift.x, p.vel.y, drift.y);   // vel.y keeps the raft's birth thickness
    p.life -= dt * decay;
  } else {
    // ballistic droplet with quadratic air drag relative to the wind
    let rel = p.vel - vec3f(PP.windX, PP.windY, PP.windZ);
    // drag/mass ~ 1/droplet diameter; mist = fine droplets (terminal ~2 m/s), rain ~7 m/s
    var kd = PP.airDrag / max(p.size, 0.002);
    if (p.kind == KIND_MIST) { kd = 2.0; }
    if (p.kind == KIND_RAIN) { kd = 0.2; }
    let speed = length(rel);
    p.vel = p.vel - rel * min(kd * speed * dt, 0.95);
    p.vel.y -= PP.gravity * dt;
    p.pos += p.vel * dt;
    p.life -= dt;
    lp = vec2f(p.pos.x - S.originX, p.pos.z - S.originZ);
    if (lp.x < 0.0 || lp.y < 0.0 || lp.x >= L.x || lp.y >= L.y) { kill(idx); return; }
    let st = sampleState(lp);
    if (p.pos.y <= st.x) {
      // landed on dry ground: water soaks in (handled by the grid), mud leaves a deposit
      if (p.vol > 0.0) { depositAt(lp, p.vol, p.mud); }
      kill(idx); return;
    }
    if (p.pos.y <= st.w && st.y > 0.002) {
      if (p.vol > 0.0) { depositAt(lp, p.vol, p.mud); }
      if ((p.kind == KIND_SPRAY || p.kind == KIND_RAIN) && p.mud < 0.35 && rnd(&seed) < PP.impactFoamProb * select(1.0, 0.25, p.kind == KIND_RAIN)) {
        p.kind = KIND_FOAM;
        p.vol = 0.0;
        p.pos.y = st.w + 0.012;
        p.vel = vec3f(p.vel.x * 0.3, PP.impactFoamAmount, p.vel.z * 0.3);   // a thin film of droplet foam
        p.size = PP.foamSize * (0.35 + 0.6 * rnd(&seed));
        p.maxLife = PP.foamLife * (0.25 + 0.6 * rnd(&seed));
        p.life = p.maxLife;
      } else { kill(idx); return; }
    }
  }
  if (p.life <= 0.0) { kill(idx); return; }
  particles[idx] = p;
  let slot = atomicAdd(&counters[1], 1);
  drawList[slot] = idx;
}
`;

// ---------------------------------------------------------------------------------------------
export const FINALIZE = /* wgsl */ `
@group(0) @binding(0) var<storage, read_write> counters: array<atomic<i32>>;
@group(0) @binding(1) var<storage, read_write> drawArgs: array<u32>;
@compute @workgroup_size(1)
fn main() {
  let alive = atomicExchange(&counters[1], 0);
  drawArgs[0] = 6u;
  drawArgs[1] = u32(max(alive, 0));
  drawArgs[2] = 0u;
  drawArgs[3] = 0u;
  atomicStore(&counters[2], alive);
}
`;
