// =====================================================================================
//  Tyre physics on the GPU (WebGPU / WGSL).
//
//  One module holds every compute stage. Each entry point binds only the resources it
//  uses (WebGPU "auto" layouts), which keeps every pipeline under the per-stage storage
//  buffer limit.
//
//  Solvers
//    XPBD            : edges solved as graph-coloured Gauss-Seidel sweeps (no atomics),
//                      global inflation volume constraint solved with GPU reductions.
//    Shape matching  : overlapping clusters (vertex + 12 neighbours), polar decomposition
//                      per cluster, goal positions blended into the particles.
//  Shared stages     : rim attachment (pins + hub reaction), ground contact with Coulomb
//                      friction, rim cylinder collision, velocity update, telemetry.
//
//  Load return: contact and pin impulses are reduced on the GPU into TELEM, a 128-byte
//  block that the host copies into a staging ring and reads back asynchronously.
// =====================================================================================

struct Params {
  dt: f32,            // substep [s]
  invDt: f32,
  invDt2: f32,
  frameDt: f32,       // duration covered by this frame's substeps [s]
  gravityY: f32,      // negative
  totalMass: f32,     // hub + payload [kg]
  hubInertia: f32,    // about the axle [kg m^2]
  servoKv: f32,       // implicit speed servo gain [N s/m]
  speedTarget: f32,   // forward speed target [m/s]
  latTarget: f32,     // lateral speed target [m/s] (slip angle)
  servoKw: f32,       // implicit wheel-speed servo gain [N m s/rad]
  omegaTarget: f32,   // wheel spin target [rad/s] about +Z
  rimRadius: f32,
  rimHalfWidth: f32,
  contactRadius: f32,
  mu: f32,            // ground friction coefficient
  volAlpha: f32,      // inflation compliance alpha_v = V0 / (gamma * p0)  [m^3/Pa]
  volTarget: f32,     // rest volume for the gas constraint  V0 * (1 + 1/gamma)
  damping: f32,       // velocity damping [1/s]
  smBeta: f32,        // shape matching blend
  frameIndex: f32,
  simTime: f32,
  restVolume: f32,
  tyreMass: f32,
  numVerts: u32,
  numTris: u32,
  numAttach: u32,
  numEdges: u32,
  numVertWG: u32,
  numTriWG: u32,
  numAttachWG: u32,
  frameSamples: f32,  // iterations * substeps this frame (contact count normalisation)
  loadRamp: f32,      // 0..1 start-up ramp of the payload weight (settle-in, see README)
  payloadFrac: f32,   // payload / (hub + payload)
  hubDamp: f32,       // hub vertical damper [N s/m]: tyre structural damping stand-in (not a spring)
  pad0: f32,          // pads the uniform to 144 bytes
};

struct Attach {
  p: u32,
  phi: f32,
  r: f32,
  z: f32,
};

@group(0) @binding(0)  var<uniform> P: Params;
@group(0) @binding(1)  var<storage, read_write> POS: array<vec4f>;      // xyz, invMass
@group(0) @binding(2)  var<storage, read_write> PREV: array<vec4f>;     // start-of-substep xyz
@group(0) @binding(3)  var<storage, read_write> VEL: array<vec4f>;
@group(0) @binding(4)  var<storage, read_write> LAMC: array<f32>;       // contact lambda (normal)
@group(0) @binding(5)  var<storage, read_write> PINACC: array<vec4f>;   // pin impulse on particle
@group(0) @binding(6)  var<storage, read_write> ACC: array<vec4f>;      // frame contact impulses
@group(0) @binding(7)  var<storage, read> EDGES: array<vec2u>;
@group(0) @binding(8)  var<storage, read> EPARAM: array<vec2f>;        // rest length, compliance
@group(0) @binding(9)  var<storage, read_write> ELAM: array<f32>;      // XPBD edge lambda
@group(0) @binding(10) var<uniform> RANGE: vec4u;                      // colour: start, count
@group(0) @binding(11) var<storage, read> ATT: array<Attach>;
@group(0) @binding(12) var<storage, read_write> HUB: array<vec4f>;     // 0 pos+theta, 1 vel+omega, 2 pred pos+theta, 3 pred vel+omega, 4 reaction
@group(0) @binding(13) var<storage, read> TRIS: array<vec4u>;
@group(0) @binding(14) var<storage, read> VTRI: array<u32>;
@group(0) @binding(15) var<storage, read_write> VNRM: array<vec4f>;    // normal xyz, strain
@group(0) @binding(16) var<storage, read_write> RED: array<vec4f>;     // reduction partials
@group(0) @binding(17) var<storage, read_write> VOL: array<vec4f>;     // 0: lambda, dlambda, V, G   1: stats
@group(0) @binding(18) var<storage, read> TRIREST: array<f32>;
@group(0) @binding(19) var<storage, read_write> TELEM: array<vec4f>;
@group(0) @binding(20) var<storage, read> X0: array<vec4f>;
@group(0) @binding(21) var<storage, read> CL0: array<vec4f>;
@group(0) @binding(22) var<storage, read_write> CLC: array<vec4f>;
@group(0) @binding(23) var<storage, read_write> CLR: array<mat3x3f>;
@group(0) @binding(24) var<storage, read> VNBR: array<u32>;

// ---------------------------------------------------------------- helpers

var<workgroup> WG: array<vec4f, 256>;

// Deterministic tree reduction across a 256-thread workgroup. Every thread must call it.
fn wg_reduce(v: vec4f, lid: u32) -> vec4f {
  WG[lid] = v;
  workgroupBarrier();
  var s: u32 = 128u;
  loop {
    if (s == 0u) {
      break;
    }
    if (lid < s) {
      WG[lid] = WG[lid] + WG[lid + s];
    }
    workgroupBarrier();
    s = s >> 1u;
  }
  return WG[0];
}

// Gradient of the enclosed volume with respect to vertex i (uses its six triangles).
fn vertexGrad(i: u32) -> vec3f {
  var g = vec3f(0.0);
  for (var s = 0u; s < 6u; s = s + 1u) {
    let slot = VTRI[i * 6u + s];
    let tr = TRIS[slot >> 2u];
    let loc = slot & 3u;
    let pa = POS[tr.x].xyz;
    let pb = POS[tr.y].xyz;
    let pc = POS[tr.z].xyz;
    if (loc == 0u) {
      g = g + cross(pb, pc);
    } else if (loc == 1u) {
      g = g + cross(pc, pa);
    } else {
      g = g + cross(pa, pb);
    }
  }
  return g / 6.0;
}

fn inv3(m: mat3x3f) -> mat3x3f {
  let a = m[0];
  let b = m[1];
  let c = m[2];
  let det = dot(a, cross(b, c));
  if (abs(det) < 1e-20) {
    return mat3x3f(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, 1.0));
  }
  let r0 = cross(b, c) / det;
  let r1 = cross(c, a) / det;
  let r2 = cross(a, b) / det;
  return transpose(mat3x3f(r0, r1, r2));
}

// Rotation factor of A (polar decomposition) by the Higham iteration X <- (X + X^-T) / 2.
// A cluster is a nearly planar patch, so A is nearly rank-2 and its raw polar factor is
// ill-conditioned (sign flips, huge inverses). A small diagonal shift keeps the iteration
// well-posed and returns the rotation closest to the identity in the degenerate direction.
fn polarRotation(A: mat3x3f) -> mat3x3f {
  let s = sqrt(dot(A[0], A[0]) + dot(A[1], A[1]) + dot(A[2], A[2])) / 1.7320508;
  if (s < 1e-14) {
    return mat3x3f(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, 1.0));
  }
  let eta = 0.2;
  var X = A * (1.0 / s) + mat3x3f(vec3f(eta, 0.0, 0.0), vec3f(0.0, eta, 0.0), vec3f(0.0, 0.0, eta));
  for (var it = 0; it < 12; it = it + 1) {
    let XiT = transpose(inv3(X));
    X = 0.5 * (X + XiT);
  }
  if (determinant(X) < 0.0) {
    return mat3x3f(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, 1.0));
  }
  return X;
}

// ---------------------------------------------------------------- per-frame / per-substep resets

@compute @workgroup_size(256)
fn frame_reset(@builtin(global_invocation_id) gid: vec3u) {
  if (gid.x < P.numVerts) {
    ACC[gid.x] = vec4f(0.0);
  }
}

@compute @workgroup_size(256)
fn substep_reset(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i < P.numEdges) {
    ELAM[i] = 0.0;
  }
  if (i < P.numVerts) {
    LAMC[i] = 0.0;
    PINACC[i] = vec4f(0.0);
  }
  // VOL[0].x (the gas multiplier) is deliberately NOT reset here: it persists across
  // substeps (warm start) so the compliant gas force converges to -C / alpha. It is reset
  // by the host in reset(). VOL[1] holds per-iteration stats only.
  if (i < 1u) {
    VOL[1] = vec4f(0.0);
  }
}

// ---------------------------------------------------------------- hub (rigid wheel + payload)

// Predict the hub: implicit speed and wheel servos plus gravity and payload.
// The implicit servo form is unconditionally stable for any gain.
@compute @workgroup_size(1)
fn hub_predict() {
  let h = HUB[0];
  let v = HUB[1];
  let dt = P.dt;
  let M = P.totalMass;
  let kv = P.servoKv;
  let vx = (M * v.x + dt * kv * P.speedTarget) / (M + dt * kv);
  let vz = (M * v.z + dt * kv * P.latTarget) / (M + dt * kv);
  // The payload weight ramps in during start-up so the hub is not dropped onto an unloaded tyre.
  let gEff = P.gravityY * (1.0 - P.payloadFrac * (1.0 - P.loadRamp));
  // Implicit vertical damper: unconditionally stable. Damps the tyre's vertical mode (the hub
  // rests on the membrane, so it rings at ~3-5 Hz otherwise).
  let vy = (M * v.y + dt * M * gEff) / (M + dt * P.hubDamp);
  let I = P.hubInertia;
  let kw = P.servoKw;
  let om = (I * v.w + dt * kw * P.omegaTarget) / (I + dt * kw);
  let vp = vec3f(vx, vy, vz);
  HUB[2] = vec4f(h.xyz + dt * vp, h.w + dt * om);
  HUB[3] = vec4f(vp, om);
}

// Reduce per-pin reaction impulses and integrate the hub with them.
@compute @workgroup_size(256)
fn hub_final(@builtin(local_invocation_id) lid: vec3u) {
  var s = vec4f(0.0);
  var i = lid.x;
  loop {
    if (i >= P.numVertWG) {
      break;
    }
    s = s + RED[i];
    i = i + 256u;
  }
  let r = wg_reduce(s, lid.x);
  if (lid.x == 0u) {
    let hp = HUB[2];
    let vp = HUB[3];
    let v = vp.xyz + r.xyz / P.totalMass;
    let om = vp.w + r.w / P.hubInertia;
    HUB[0] = hp;
    HUB[1] = vec4f(v, om);
    HUB[4] = r;
  }
}

// ---------------------------------------------------------------- particle prediction / velocity

@compute @workgroup_size(256)
fn predict(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= P.numVerts) {
    return;
  }
  let xi = POS[i];
  var vi = VEL[i].xyz;
  vi.y = vi.y + P.gravityY * P.dt;
  vi = vi * (1.0 - P.damping * P.dt);
  VEL[i] = vec4f(vi, 0.0);
  PREV[i] = xi;
  POS[i] = vec4f(xi.xyz + vi * P.dt, xi.w);
}

@compute @workgroup_size(256)
fn velocity(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= P.numVerts) {
    return;
  }
  VEL[i] = vec4f((POS[i].xyz - PREV[i].xyz) * P.invDt, 0.0);
}

// ---------------------------------------------------------------- XPBD membrane edges

// One colour of distance constraints. Edges of a colour share no vertex, so each thread
// can write its two particles without races. Gauss-Seidel across colours.
@compute @workgroup_size(64)
fn xpbd_edges(@builtin(global_invocation_id) gid: vec3u) {
  if (gid.x >= RANGE.y) {
    return;
  }
  let e = RANGE.x + gid.x;
  let ij = EDGES[e];
  let pi = POS[ij.x];
  let pj = POS[ij.y];
  let d = pi.xyz - pj.xyz;
  let len = length(d);
  if (len < 1e-9) {
    return;
  }
  let n = d / len;
  let ep = EPARAM[e];
  let C = len - ep.x;
  let alphaT = ep.y * P.invDt2;
  let wsum = pi.w + pj.w;
  let lam = ELAM[e];
  let dl = (-C - alphaT * lam) / (wsum + alphaT);
  ELAM[e] = lam + dl;
  POS[ij.x] = vec4f(pi.xyz + pi.w * dl * n, pi.w);
  POS[ij.y] = vec4f(pj.xyz - pj.w * dl * n, pj.w);
}

// ---------------------------------------------------------------- shape matching (alternative)

// Per-vertex cluster (self + 12 neighbours, equal masses): centroid and rotation.
@compute @workgroup_size(64)
fn sm_cluster(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= P.numVerts) {
    return;
  }
  var cur = POS[i].xyz;
  for (var k = 0u; k < 12u; k = k + 1u) {
    cur = cur + POS[VNBR[i * 12u + k]].xyz;
  }
  let c = cur / 13.0;
  let c0 = CL0[i].xyz;
  var A = mat3x3f(vec3f(0.0), vec3f(0.0), vec3f(0.0));
  // outer product (x_j - c) * (x0_j - c0)^T for j = self and neighbours
  var j = i;
  for (var k = 0u; k < 13u; k = k + 1u) {
    if (k > 0u) {
      j = VNBR[i * 12u + k - 1u];
    }
    let a = POS[j].xyz - c;
    let b = X0[j].xyz - c0;
    A = A + mat3x3f(a * b.x, a * b.y, a * b.z);
  }
  CLC[i] = vec4f(c, 0.0);
  CLR[i] = polarRotation(A);
}

// Goal positions from all clusters containing a vertex, blended into the particles.
@compute @workgroup_size(64)
fn sm_goal(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= P.numVerts) {
    return;
  }
  let x0i = X0[i].xyz;
  var g = vec3f(0.0);
  var j = i;
  for (var k = 0u; k < 13u; k = k + 1u) {
    if (k > 0u) {
      j = VNBR[i * 12u + k - 1u];
    }
    g = g + CLC[j].xyz + CLR[j] * (x0i - CL0[j].xyz);
  }
  g = g / 13.0;
  let xi = POS[i];
  POS[i] = vec4f(xi.xyz + P.smBeta * (g - xi.xyz), xi.w);
}

// ---------------------------------------------------------------- inflation (volume) constraint

// Pass 1: per triangle volume and strain, per vertex gradient norm, reduced to partials.
@compute @workgroup_size(256)
fn vol_pass1(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_id) lid: vec3u,
             @builtin(workgroup_id) wid: vec3u) {
  let t = gid.x;
  var val = vec4f(0.0);
  if (t < P.numTris) {
    let tr = TRIS[t];
    let pa = POS[tr.x].xyz;
    let pb = POS[tr.y].xyz;
    let pc = POS[tr.z].xyz;
    val.x = dot(pa, cross(pb, pc)) / 6.0;
    let area = 0.5 * length(cross(pb - pa, pc - pa));
    val.z = abs(area / TRIREST[t] - 1.0);
  }
  if (t < P.numVerts) {
    let g = vertexGrad(t);
    val.y = POS[t].w * dot(g, g);
  }
  let s = wg_reduce(val, lid.x);
  if (lid.x == 0u) {
    RED[wid.x] = s;
  }
}

// Pass 2 (single workgroup): sums partials and solves the global lambda for C = V0 - V.
@compute @workgroup_size(256)
fn vol_final(@builtin(local_invocation_id) lid: vec3u) {
  var s = vec4f(0.0);
  var i = lid.x;
  loop {
    if (i >= P.numTriWG) {
      break;
    }
    s = s + RED[i];
    i = i + 256u;
  }
  let r = wg_reduce(s, lid.x);
  if (lid.x == 0u) {
    let V = r.x;
    let G = r.y;
    let lam = VOL[0].x;
    let C = P.volTarget - V;
    let at = P.volAlpha * P.invDt2;
    let dl = (-C - at * lam) / (G + at);
    VOL[0] = vec4f(lam + dl, dl, V, G);
    VOL[1] = vec4f(r.z, 0.0, 0.0, 0.0);
  }
}

// Pass 3: apply the volume correction. dx_i = w_i * grad C * dlambda with grad C = -grad V.
@compute @workgroup_size(256)
fn vol_apply(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= P.numVerts) {
    return;
  }
  let dl = VOL[0].y;
  let p = POS[i];
  let g = vertexGrad(i);
  POS[i] = vec4f(p.xyz - p.w * g * dl, p.w);
}

// ---------------------------------------------------------------- rim attachment (pins)

// Seat particles are pinned to the rim: target = hub + rotated seat offset. The impulse
// delivered to each particle is accumulated and later reacted onto the hub.
@compute @workgroup_size(64)
fn pins(@builtin(global_invocation_id) gid: vec3u) {
  let k = gid.x;
  if (k >= P.numAttach) {
    return;
  }
  let a = ATT[k];
  let H = HUB[2];
  let ang = a.phi + H.w;
  let tgt = vec3f(H.x + a.r * cos(ang), H.y + a.r * sin(ang), H.z + a.z);
  let p = POS[a.p];
  let corr = tgt - p.xyz;
  POS[a.p] = vec4f(tgt, p.w);
  // impulse [kg m/s] = m * displacement / dt
  PINACC[a.p] = PINACC[a.p] + vec4f(corr / (p.w * P.dt), 0.0);
}

// Reduce the reaction on the hub from every impulse recorded in PINACC: pin impulses on
// the seat particles and rim-collision impulses on any particle. Force and torque about Z.
@compute @workgroup_size(256)
fn attach_pass1(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_id) lid: vec3u,
                @builtin(workgroup_id) wid: vec3u) {
  let i = gid.x;
  var val = vec4f(0.0);
  if (i < P.numVerts) {
    let J = PINACC[i].xyz;
    if (dot(J, J) > 0.0) {
      let hubPos = HUB[2].xyz;
      let r = POS[i].xyz - hubPos;
      let Jh = -J;
      let tau = cross(r, Jh).z;
      val = vec4f(Jh, tau);
    }
  }
  let s = wg_reduce(val, lid.x);
  if (lid.x == 0u) {
    RED[wid.x] = s;
  }
}

// ---------------------------------------------------------------- ground contact + rim collision

// Unilateral ground contact (y >= contactRadius) with Coulomb friction against the
// start-of-substep position, plus a rim cylinder that particles cannot enter.
@compute @workgroup_size(256)
fn contact(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= P.numVerts) {
    return;
  }
  var p = POS[i];
  let w = p.w;
  var lam = LAMC[i];
  var flag = 0.0;
  var impN = 0.0;
  var impT = vec2f(0.0);

  let C = p.y - P.contactRadius;
  if (C < 0.0 || lam > 0.0) {
    let dl = max(-C / w, -lam);
    lam = lam + dl;
    impN = dl / P.dt; // impulse [kg m/s]
    p.y = p.y + w * dl;

    let prev = PREV[i].xyz;
    let d = vec3f(p.x - prev.x, 0.0, p.z - prev.z);
    let mag = length(d);
    let cap = P.mu * w * lam;
    if (mag > 1e-12 && cap > 0.0) {
      let s = min(1.0, cap / mag);
      let dT = -d * s;
      p.x = p.x + dT.x;
      p.z = p.z + dT.z;
      impT = vec2f(dT.x, dT.z) / (w * P.dt);
    }
    if (lam > 0.0) {
      flag = 1.0;
    }
  }

  let H = HUB[2];
  var rimImp = vec3f(0.0);
  if (abs(p.z - H.z) < P.rimHalfWidth) {
    let rel = vec2f(p.x - H.x, p.y - H.y);
    let rho = length(rel);
    let rc = P.rimRadius - 0.002;
    if (rho < rc && rho > 1e-6) {
      let q = rel * (rc / rho);
      let before = p.xyz;
      p.x = H.x + q.x;
      p.y = H.y + q.y;
      rimImp = (p.xyz - before) / (w * P.dt); // impulse [kg m/s]; reacted onto the hub later
    }
  }

  POS[i] = p;
  LAMC[i] = lam;
  PINACC[i] = PINACC[i] + vec4f(rimImp, 0.0);
  ACC[i] = ACC[i] + vec4f(impN, impT.x, impT.y, flag);
}

// ---------------------------------------------------------------- telemetry

@compute @workgroup_size(256)
fn telem_pass1(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_id) lid: vec3u,
               @builtin(workgroup_id) wid: vec3u) {
  var val = vec4f(0.0);
  if (gid.x < P.numVerts) {
    val = ACC[gid.x];
  }
  let s = wg_reduce(val, lid.x);
  if (lid.x == 0u) {
    RED[wid.x] = s;
  }
}

// Single workgroup: frame-averaged loads, volume, pressure, hub state, reaction.
// TELEM layout (vec4 each):
//   0: Fz_ground, Fx_road, Fz_lateral, contactParticles
//   1: V, pressure_gauge, strain_mean, volume_error
//   2: hub xyz, theta
//   3: hub velocity xyz, omega
//   4: frameIndex, simTime, payload weight target, tyre weight target
//   5: hub reaction impulse (xyz), torque
@compute @workgroup_size(256)
fn telem_final(@builtin(local_invocation_id) lid: vec3u) {
  var s = vec4f(0.0);
  var i = lid.x;
  loop {
    if (i >= P.numVertWG) {
      break;
    }
    s = s + RED[i];
    i = i + 256u;
  }
  let r = wg_reduce(s, lid.x);
  if (lid.x == 0u) {
    let invFrame = select(0.0, 1.0 / P.frameDt, P.frameDt > 0.0);
    let V = VOL[0].z;
    let pressure = (P.volTarget - V) / P.volAlpha;
    let g = -P.gravityY;
    TELEM[0] = vec4f(r.x * invFrame, r.y * invFrame, r.z * invFrame, r.w / max(P.frameSamples, 1.0));
    TELEM[1] = vec4f(V, pressure, VOL[1].x / f32(P.numTris), (V - P.restVolume) / P.restVolume);
    TELEM[2] = HUB[0];
    TELEM[3] = HUB[1];
    TELEM[4] = vec4f(P.frameIndex, P.simTime, (P.totalMass - 0.0) * g, P.tyreMass * g);
    TELEM[5] = HUB[4];
  }
}

// ---------------------------------------------------------------- rendering helpers

// Per-vertex normal (area weighted) and mean area strain for visualisation.
@compute @workgroup_size(256)
fn normals(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= P.numVerts) {
    return;
  }
  var n = vec3f(0.0);
  var st = 0.0;
  for (var s = 0u; s < 6u; s = s + 1u) {
    let slot = VTRI[i * 6u + s];
    let t = slot >> 2u;
    let tr = TRIS[t];
    let pa = POS[tr.x].xyz;
    let pb = POS[tr.y].xyz;
    let pc = POS[tr.z].xyz;
    let cr = cross(pb - pa, pc - pa);
    n = n + cr;
    st = st + (0.5 * length(cr)) / TRIREST[t] - 1.0;
  }
  let nn = select(vec3f(0.0, 1.0, 0.0), normalize(n), length(n) > 1e-12);
  VNRM[i] = vec4f(nn, st / 6.0);
}
