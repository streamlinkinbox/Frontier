// Real track geometry laid into the track beds of the mine mesh:
//   * rails  - swept flat-bottom rail profile (foot / web / head), all quads,
//              along every cart lane AND every junction route (switch yard look)
//   * sleepers + tie plates - instanced, in the tunnel track beds
//   * flangeways - thin dark strips beside the rail heads on the junction plates
//
// Rails follow the drivable surface exactly (downward ray casts against the
// collision mesh), so they bank, climb and dip with the road. Rail tops sit
// 1.5 cm above the road: cars can drive across them, carts ride on them.
import * as THREE from 'three';

const RAIL_H = 0.115;   // rail height (foot to head)
const PROUD = 0.015;    // rail top above the road surface
const SLEEPER = { h: 0.1, w: 0.2, spacing: 0.7 };

// half rail profile, x = lateral (from rail centre), y = up from rail base.
// Mirrored to a closed-bottom outline, walked left -> over the head -> right.
const HALF = [
  [0.062, 0.0], [0.062, 0.012], [0.012, 0.03], [0.012, 0.078], [0.031, 0.088], [0.031, 0.108], [0.024, 0.115],
];
const PROFILE = [...HALF.slice().reverse().map(([x, y]) => [-x, y]), ...HALF.map(([x, y]) => [x, y])];
// PROFILE runs from left foot tip over the head to the right foot tip (x: -0.062 -> +0.062)

const DOWN = new THREE.Vector3(0, -1, 0);

function resample(pts, step) {
  const out = [pts[0].clone()];
  let carry = 0;
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1], b = pts[i];
    const L = a.distanceTo(b);
    let d = step - carry;
    while (d <= L) { out.push(a.clone().lerp(b, d / L)); d += step; }
    carry = L - (d - step);
  }
  if (out[out.length - 1].distanceTo(pts[pts.length - 1]) > step * 0.3) out.push(pts[pts.length - 1].clone());
  else out[out.length - 1].copy(pts[pts.length - 1]);
  return out;
}

// frames along a path, snapped to the drivable surface
function surfaceFrames(world, pts) {
  const n = pts.length;
  const P = [], U = [];
  const o = new THREE.Vector3();
  for (const p of pts) {
    const hit = world.raycast(o.set(p.x, p.y + 1.2, p.z), DOWN, 3);
    if (hit && hit.normal.y > 0.5) { P.push(hit.point); U.push(hit.normal); }
    else { P.push(p.clone()); U.push(new THREE.Vector3(0, 1, 0)); }
  }
  // smooth normals (faceted collision quads) and heights a little
  const frames = [];
  for (let i = 0; i < n; i++) {
    const up = new THREE.Vector3();
    for (let k = -3; k <= 3; k++) up.add(U[Math.min(n - 1, Math.max(0, i + k))]);
    const T = P[Math.min(n - 1, i + 1)].clone().sub(P[Math.max(0, i - 1)]).normalize();
    up.addScaledVector(T, -up.dot(T)).normalize();
    const L = new THREE.Vector3().crossVectors(up, T).normalize(); // left
    frames.push({ p: P[i], T, U: up, L });
  }
  return frames;
}

class Builder {
  constructor() { this.pos = []; this.nrm = []; this.col = []; this.idx = []; }
  get n() { return this.pos.length / 3; }
  v(p, nm, c) { this.pos.push(p.x, p.y, p.z); this.nrm.push(nm.x, nm.y, nm.z); this.col.push(c[0], c[1], c[2]); return this.n - 1; }
  quad(a, b, c, d) { this.idx.push(a, b, c, a, c, d); }
  geometry() {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.nrm, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(this.col, 3));
    g.setIndex(this.pos.length / 3 > 65535 ? new THREE.Uint32BufferAttribute(this.idx, 1) : new THREE.Uint16BufferAttribute(this.idx, 1));
    g.computeBoundingSphere();
    return g;
  }
}

const RUST = [0.2, 0.11, 0.065], POLISHED = [0.62, 0.6, 0.57], DARK = [0.012, 0.011, 0.01];

// per-vertex profile normals (average of the adjacent segment normals) + colour class
const PNORM = PROFILE.map((_, i) => {
  const seg = (a, b) => { const nx = -(b[1] - a[1]), ny = b[0] - a[0]; const l = Math.hypot(nx, ny); return [nx / l, ny / l]; };
  const n0 = i > 0 ? seg(PROFILE[i - 1], PROFILE[i]) : null, n1 = i < PROFILE.length - 1 ? seg(PROFILE[i], PROFILE[i + 1]) : null;
  const nx = (n0 ? n0[0] : 0) + (n1 ? n1[0] : 0), ny = (n0 ? n0[1] : 0) + (n1 ? n1[1] : 0);
  const l = Math.hypot(nx, ny); return [nx / l, ny / l];
});

// sweep one rail along frames at lateral offset `lat` (along L). One shared
// vertex per profile point per ring -> quads between consecutive rings.
function sweepRail(B, frames, lat) {
  const np = PROFILE.length;
  const rings = [];
  const p = new THREE.Vector3(), nm = new THREE.Vector3();
  for (const f of frames) {
    const base = f.p.clone().addScaledVector(f.L, lat).addScaledVector(f.U, PROUD - RAIL_H);
    const ring = [];
    for (let s = 0; s < np; s++) {
      const [x, y] = PROFILE[s], [nx, ny] = PNORM[s];
      // profile +x maps to -L (the right-hand side when looking down the track)
      nm.copy(f.L).multiplyScalar(-nx).addScaledVector(f.U, ny);
      // polished: running surface + gauge (inner) face of the head
      const polished = y > 0.1 || (y > 0.085 && lat * x > 0);
      ring.push(B.v(p.copy(base).addScaledVector(f.L, -x).addScaledVector(f.U, y), nm, polished ? POLISHED : RUST));
    }
    rings.push(ring);
  }
  for (let i = 0; i + 1 < rings.length; i++) {
    const A = rings[i], C = rings[i + 1];
    for (let s = 0; s + 1 < np; s++) B.quad(A[s], A[s + 1], C[s + 1], C[s]);
  }
}

function flangeway(B, frames, lat, inward) {
  // dark 5 cm strip on the gauge side of the rail head, just above the plate floor
  const up = new THREE.Vector3();
  const rows = [];
  const p = new THREE.Vector3();
  for (const f of frames) {
    up.copy(f.U);
    const o = f.p.clone().addScaledVector(f.U, 0.004);
    const a = B.v(p.copy(o).addScaledVector(f.L, lat + inward * 0.035), up, DARK);
    const b = B.v(p.copy(o).addScaledVector(f.L, lat + inward * 0.085), up, DARK);
    rows.push(inward > 0 ? [b, a] : [a, b]);
  }
  for (let i = 0; i + 1 < rows.length; i++) B.quad(rows[i][0], rows[i][1], rows[i + 1][1], rows[i + 1][0]);
}

export function buildTracks(mine, P, world, traffic) {
  const group = new THREE.Group();
  group.name = 'MineTracks';
  const g2 = P.gauge / 2;
  const rails = new Builder();
  const plates = new Builder();
  const sleeperMats = [];
  const m4 = new THREE.Matrix4(), off = new THREE.Matrix4();
  let railLen = 0;

  // 1) tunnel lanes: rails + sleepers in the bed
  for (const lane of mine.lanes) {
    const pts = resample(lane.pts, 1.2); // tunnels are gentle (crest R >= 65 m)
    const fr = surfaceFrames(world, pts);
    sweepRail(rails, fr, g2);
    sweepRail(rails, fr, -g2);
    railLen += 2 * (pts.length - 1) * 1.2;
    // sleepers (the lane's dir=-1 twin covers the other track, so no duplicates)
    const sp = resample(lane.pts, SLEEPER.spacing);
    const sf = surfaceFrames(world, sp);
    for (let i = 1; i < sf.length - 1; i++) {
      const f = sf[i];
      m4.makeBasis(f.L, f.U, f.T).setPosition(f.p);
      off.makeTranslation(0, PROUD - RAIL_H - SLEEPER.h / 2, 0);
      sleeperMats.push(m4.clone().multiply(off));
    }
  }
  // 2) junction routes (every allowed turn, as in a switch yard): rails + flangeways
  let routes = 0;
  if (traffic && traffic.transitionCache) {
    for (const [, outs] of traffic.transitionCache) {
      for (const [, tp] of outs) {
        const pts = resample(tp, 0.6);
        const fr = surfaceFrames(world, pts);
        sweepRail(rails, fr, g2);
        sweepRail(rails, fr, -g2);
        flangeway(plates, fr, g2, -1);
        flangeway(plates, fr, -g2, 1);
        railLen += 2 * (pts.length - 1) * 0.6;
        routes++;
      }
    }
  }

  const railMat = new THREE.MeshStandardMaterial({ vertexColors: true, metalness: 0.85, roughness: 0.42 });
  const railMesh = new THREE.Mesh(rails.geometry(), railMat);
  railMesh.name = 'Rails';
  railMesh.receiveShadow = true;
  group.add(railMesh);

  const fwMat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 1, metalness: 0, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 });
  const fwMesh = new THREE.Mesh(plates.geometry(), fwMat);
  fwMesh.name = 'Flangeways';
  group.add(fwMesh);

  // sleepers + tie plates (instanced)
  const wood = new THREE.MeshStandardMaterial({ color: 0x3a2515, roughness: 0.95 });
  const steel = new THREE.MeshStandardMaterial({ color: 0x2a2826, roughness: 0.6, metalness: 0.75 });
  const sleeperLen = P.gauge + 0.62;
  const sl = new THREE.InstancedMesh(new THREE.BoxGeometry(sleeperLen, SLEEPER.h, SLEEPER.w), wood, sleeperMats.length);
  const tp = new THREE.InstancedMesh(new THREE.BoxGeometry(0.2, 0.014, 0.17), steel, sleeperMats.length * 2);
  const plateOff = [new THREE.Matrix4().makeTranslation(g2, SLEEPER.h / 2 + 0.007, 0), new THREE.Matrix4().makeTranslation(-g2, SLEEPER.h / 2 + 0.007, 0)];
  sleeperMats.forEach((m, i) => {
    sl.setMatrixAt(i, m);
    tp.setMatrixAt(i * 2, m4.multiplyMatrices(m, plateOff[0]));
    tp.setMatrixAt(i * 2 + 1, m4.multiplyMatrices(m, plateOff[1]));
  });
  sl.name = 'Sleepers'; tp.name = 'TiePlates';
  [sl, tp].forEach((im) => { im.receiveShadow = true; im.instanceMatrix.needsUpdate = true; group.add(im); });

  group.userData.stats = { railKm: railLen / 1000, railVerts: rails.n, sleepers: sleeperMats.length, routes };
  return group;
}
