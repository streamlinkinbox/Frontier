// Hand-built low-poly models. Every custom mesh goes through triGeo() with an interior
// reference point so all face windings/normals point outward.
import * as THREE from '../vendor/three.module.min.js';
import { paint, merge, mat, triGeo, lerp, rng, TAU } from './util.js';

export const MAT = {
  flat: new THREE.MeshStandardMaterial({ vertexColors: true, flatShading: true, roughness: 0.9, metalness: 0.0 }),
  metal: new THREE.MeshStandardMaterial({ vertexColors: true, flatShading: true, roughness: 0.6, metalness: 0.35 }),
  paint: new THREE.MeshStandardMaterial({ vertexColors: true, flatShading: true, roughness: 0.42, metalness: 0.25 }),
  glow: new THREE.MeshBasicMaterial({ vertexColors: true }),
  wire: new THREE.LineBasicMaterial({ color: 0x4a443d }),
};

const mesh = (geo, material = MAT.flat, shadow = true) => {
  const m = new THREE.Mesh(geo, material);
  m.castShadow = shadow; m.receiveShadow = true;
  return m;
};

// piecewise-linear keyframes over z (keys sorted by z descending or ascending)
function pl(keys) {
  const k = [...keys].sort((a, b) => a[0] - b[0]);
  return z => {
    if (z <= k[0][0]) return k[0][1];
    for (let i = 0; i < k.length - 1; i++) if (z <= k[i + 1][0]) return lerp(k[i][1], k[i + 1][1], (z - k[i][0]) / (k[i + 1][0] - k[i][0]));
    return k[k.length - 1][1];
  };
}

// tapered prism along X (wings/fins). side=+1 builds toward +X, -1 toward -X (no mirroring
// matrices, so windings stay valid). Chord along z, thickness along y.
function prism(span, cRoot, cTip, tRoot, tTip, sweep, color, dihedral = 0, side = 1) {
  const P = (u, zc, c, t) => {
    const x = side * u, y = u * Math.tan(dihedral);
    return [[x, y + t / 2, zc + c / 2], [x, y + t / 2, zc - c / 2], [x, y - t / 2, zc - c / 2], [x, y - t / 2, zc + c / 2]];
  };
  const A = P(0, 0, cRoot, tRoot), B = P(span, -sweep, cTip, tTip);
  const tris = [];
  for (let i = 0; i < 4; i++) { const j = (i + 1) % 4; tris.push([A[i], A[j], B[j], color], [A[i], B[j], B[i], color]); }
  tris.push([A[0], A[1], A[2], color], [A[0], A[2], A[3], color], [B[0], B[1], B[2], color], [B[0], B[2], B[3], color]);
  return triGeo(tris, (x) => {
    const u = Math.min(span * 0.9, Math.max(span * 0.1, Math.abs(x)));
    return [side * u, u * Math.tan(dihedral), -sweep * u / span];
  });
}

function starGeo(r, color) {
  const tris = [];
  const pts = [];
  for (let i = 0; i < 10; i++) {
    const a = Math.PI / 2 + i * Math.PI / 5, rr = i % 2 ? r * 0.4 : r;
    pts.push([Math.cos(a) * rr, 0, -Math.sin(a) * rr]);
  }
  for (let i = 0; i < 10; i++) tris.push([[0, 0, 0], pts[i], pts[(i + 1) % 10], color]);
  return triGeo(tris, () => [0, -1, 0]);
}

function crossGeo(s, color, border) {
  const g = [];
  g.push(paint(new THREE.BoxGeometry(s, 0.02, s * 0.34), border));
  g.push(paint(new THREE.BoxGeometry(s * 0.34, 0.02, s), border));
  g.push(paint(new THREE.BoxGeometry(s * 0.8, 0.03, s * 0.2), color));
  g.push(paint(new THREE.BoxGeometry(s * 0.2, 0.03, s * 0.8), color));
  return merge(g);
}

// =====================================================================
// CAR — compact 4-door sedan (Sentra-style silhouette), lofted cross-sections
// =====================================================================
export function buildCar(paintHex = '#56603a') {
  const W = pl([[2.32, 0.66], [2.2, 0.82], [1.9, 0.87], [1.0, 0.885], [-1.6, 0.885], [-2.12, 0.85], [-2.32, 0.72]]);
  const YB = pl([[2.32, 0.38], [2.2, 0.28], [1.86, 0.28], [1.76, 0.46], [1.62, 0.62], [1.42, 0.67], [1.22, 0.62], [1.08, 0.46], [0.98, 0.28],
    [-0.94, 0.28], [-1.04, 0.46], [-1.18, 0.62], [-1.38, 0.67], [-1.58, 0.62], [-1.72, 0.46], [-1.82, 0.28], [-2.2, 0.3], [-2.32, 0.42]]);
  const BELT = pl([[2.32, 0.7], [2.2, 0.8], [1.9, 0.86], [1.05, 0.95], [-1.6, 0.99], [-2.2, 1.0], [-2.32, 0.9]]);
  const TOP = pl([[2.32, 0.75], [2.2, 0.86], [1.9, 0.92], [1.05, 1.0], [0.22, 1.40], [-0.3, 1.43], [-0.85, 1.40], [-1.62, 1.06], [-2.18, 1.04], [-2.32, 0.93]]);
  const WT = pl([[2.32, 0.54], [2.2, 0.68], [1.9, 0.75], [1.05, 0.77], [0.22, 0.61], [-0.85, 0.61], [-1.62, 0.73], [-2.18, 0.71], [-2.32, 0.58]]);
  const stations = [...new Set([2.32, 2.2, 1.9, 1.86, 1.76, 1.62, 1.42, 1.22, 1.08, 1.05, 0.98, 0.6, 0.22, -0.22, -0.38, -0.85, -0.94, -1.04, -1.18, -1.38, -1.58, -1.62, -1.72, -1.82, -2.12, -2.18, -2.2, -2.32])].sort((a, b) => b - a);

  const ring = z => {
    const w = W(z), yb = YB(z), belt = BELT(z), top = TOP(z), wt = WT(z);
    const cabin = z < 1.05 && z > -1.62;
    const edge = top - (cabin ? 0.07 : 0.035);
    const R = [
      [0, yb], [w * 0.9, yb], [w, Math.min(yb + 0.16, belt - 0.2)], [w * 1.012, (Math.min(yb + 0.16, belt - 0.2) + belt) / 2 + 0.04],
      [w * 0.985, belt], [wt, edge], [0, top],
    ];
    const pts = R.map(p => [p[0], p[1], z]);
    for (let i = 5; i >= 1; i--) pts.push([-R[i][0], R[i][1], z]);
    return pts; // 12 points
  };
  const C = { body: paintHex, glass: '#1f2a33', trim: '#1b1c1e', under: '#161616', pillar: '#141516' };
  const tris = [];
  for (let s = 0; s < stations.length - 1; s++) {
    const za = stations[s], zb = stations[s + 1], zm = (za + zb) / 2;
    const A = ring(za), B = ring(zb);
    for (let e = 0; e < 12; e++) {
      const f = (e + 1) % 12;
      let c = C.body;
      const top = e === 5 || e === 6, side = e === 4 || e === 7;
      if (e === 0 || e === 11) c = C.under;
      else if (e === 1 || e === 10) c = C.trim;
      if (zm < 1.05 && zm > 0.22) { if (top) c = C.glass; if (side) c = C.glass; }
      else if (zm < 0.22 && zm > -0.85) { if (side) c = (zm < -0.22 && zm > -0.38) ? C.pillar : C.glass; }
      else if (zm < -0.85 && zm > -1.62) { if (top) c = C.glass; }
      tris.push([A[e], A[f], B[f], c], [A[e], B[f], B[e], c]);
    }
  }
  // end caps (fan)
  const cap = (z, c) => { const R = ring(z); let cx = 0, cy = 0; R.forEach(p => { cy += p[1]; }); cy /= R.length; for (let e = 0; e < 12; e++) tris.push([[cx, cy, z], R[e], R[(e + 1) % 12], c]); };
  cap(2.32, C.body); cap(-2.32, C.body);
  const bodyGeo = triGeo(tris, (x, y, z) => [0, 0.72, Math.max(-1.95, Math.min(1.95, z))]);

  const parts = [bodyGeo];
  // grille, intake, bumpers, plates
  parts.push(paint(new THREE.BoxGeometry(0.78, 0.13, 0.05), C.trim, mat(0, 0.62, 2.315)));
  parts.push(paint(new THREE.BoxGeometry(0.98, 0.12, 0.06), C.trim, mat(0, 0.44, 2.31)));
  parts.push(paint(new THREE.BoxGeometry(0.36, 0.11, 0.02), '#e8e4d6', mat(0, 0.6, -2.33)));
  parts.push(paint(new THREE.BoxGeometry(1.2, 0.08, 0.08), C.trim, mat(0, 0.46, -2.32)));
  // mirrors
  for (const s of [1, -1]) {
    parts.push(paint(new THREE.BoxGeometry(0.06, 0.05, 0.1), C.trim, mat(s * 0.9, 1.0, 0.93)));
    parts.push(paint(new THREE.BoxGeometry(0.1, 0.11, 0.16), C.body, mat(s * 0.97, 1.03, 0.92, 0, s * 0.15, 0)));
    // door handles & door seam hint
    parts.push(paint(new THREE.BoxGeometry(0.02, 0.03, 0.16), C.trim, mat(s * 0.892, 0.9, 0.35)));
    parts.push(paint(new THREE.BoxGeometry(0.02, 0.03, 0.16), C.trim, mat(s * 0.892, 0.92, -0.6)));
  }
  // allied star on hood + roof
  parts.push(starGeo(0.3, '#ecebe3').applyMatrix4(mat(0, 0.966, 1.45, 0.095, 0, 0)));
  parts.push(starGeo(0.26, '#ecebe3').applyMatrix4(mat(0, 1.436, -0.3)));
  const body = mesh(merge(parts), MAT.paint);

  // lights (unlit so they read)
  const lp = [];
  for (const s of [1, -1]) {
    lp.push(paint(new THREE.BoxGeometry(0.34, 0.1, 0.12), '#fff6d8', mat(s * 0.5, 0.77, 2.235, -0.5, s * -0.25, 0)));
    lp.push(paint(new THREE.BoxGeometry(0.3, 0.12, 0.05), '#d42a1e', mat(s * 0.55, 0.9, -2.318)));
    lp.push(paint(new THREE.BoxGeometry(0.1, 0.06, 0.04), '#ffae2a', mat(s * 0.72, 0.86, -2.3)));
  }
  const lights = mesh(merge(lp), MAT.glow, false);

  const car = new THREE.Group();
  car.add(body, lights);
  // wheels
  const wheels = [];
  const tire = paint(new THREE.CylinderGeometry(0.31, 0.31, 0.22, 12), '#1a1a1a', mat(0, 0, 0, 0, 0, Math.PI / 2));
  const rim = paint(new THREE.CylinderGeometry(0.19, 0.19, 0.225, 8), '#8d9094', mat(0, 0, 0, 0, 0, Math.PI / 2));
  const hub = paint(new THREE.CylinderGeometry(0.06, 0.06, 0.235, 6), '#3a3b3d', mat(0, 0, 0, 0, 0, Math.PI / 2));
  const spokes = paint(new THREE.BoxGeometry(0.228, 0.3, 0.04), '#6a6d71');
  const spokes2 = paint(new THREE.BoxGeometry(0.228, 0.04, 0.3), '#6a6d71');
  const wheelGeo = merge([tire, rim, hub, spokes, spokes2]);
  for (const [x, z] of [[0.765, 1.42], [-0.765, 1.42], [0.765, -1.38], [-0.765, -1.38]]) {
    const pivot = new THREE.Group(); pivot.position.set(x, 0.31, z);
    const w = mesh(wheelGeo, MAT.flat); pivot.add(w);
    car.add(pivot);
    wheels.push({ pivot, spin: w, x, z, front: z > 0 });
  }
  car.userData.wheels = wheels;
  car.userData.flipped = bodyGeo.userData.flipped;
  return car;
}

// =====================================================================
// TANKS
// =====================================================================
function extrudeSide(pts, width, color, x = 0) {
  // shape in (z, y), extruded across X, centred on x
  const shape = new THREE.Shape(pts.map(p => new THREE.Vector2(p[0], p[1])));
  const g = new THREE.ExtrudeGeometry(shape, { depth: width, bevelEnabled: false });
  g.rotateY(-Math.PI / 2); g.translate(x + width / 2, 0, 0);
  return paint(g, color);
}
function extrudeTop(pts, h, color, y0 = 0) {
  // shape in (x, z) top view, extruded upward
  const shape = new THREE.Shape(pts.map(p => new THREE.Vector2(p[0], -p[1])));
  const g = new THREE.ExtrudeGeometry(shape, { depth: h, bevelEnabled: false });
  g.rotateX(-Math.PI / 2); g.translate(0, y0, 0);
  return paint(g, color);
}
function trackRun(x, len, h, w, color) {
  const r = h / 2, pts = [];
  for (let i = 0; i <= 6; i++) { const a = -Math.PI / 2 + i * Math.PI / 6; pts.push([len / 2 - r + Math.cos(a) * r, r + Math.sin(a) * r]); }
  for (let i = 0; i <= 6; i++) { const a = Math.PI / 2 + i * Math.PI / 6; pts.push([-len / 2 + r + Math.cos(a) * r, r + Math.sin(a) * r]); }
  return extrudeSide(pts, w, color, x);
}

export function buildTank(type, state) {
  const burnt = state === 'wreck';
  const parts = [];
  let col, dark = burnt ? '#2e2a26' : '#2b2a26';
  const cyl = (rt, rb, h, seg, c, m) => paint(new THREE.CylinderGeometry(rt, rb, h, seg), c, m);
  if (type === 'sherman') {
    col = burnt ? '#3d3a33' : '#58603a';
    parts.push(trackRun(1.05, 5.8, 1.05, 0.46, dark), trackRun(-1.05, 5.8, 1.05, 0.46, dark));
    for (const s of [1, -1]) for (let i = 0; i < 3; i++) {
      const z = -1.8 + i * 1.8;
      parts.push(paint(new THREE.BoxGeometry(0.2, 0.5, 1.2), col, mat(s * 1.33, 0.55, z)));
      parts.push(cyl(0.3, 0.3, 0.2, 8, dark, mat(s * 1.36, 0.36, z - 0.35, 0, 0, Math.PI / 2)));
      parts.push(cyl(0.3, 0.3, 0.2, 8, dark, mat(s * 1.36, 0.36, z + 0.35, 0, 0, Math.PI / 2)));
    }
    parts.push(extrudeSide([[-2.9, 0.75], [2.4, 0.75], [3.05, 1.05], [2.95, 1.35], [1.9, 2.05], [-2.6, 2.05], [-2.95, 1.6]], 2.3, col));
    parts.push(paint(new THREE.SphereGeometry(1, 9, 5), col, mat(0, 2.5, -0.2, 0, 0, 0, 1.05, 0.62, 1.2)));
    parts.push(paint(new THREE.BoxGeometry(0.9, 0.5, 0.4), col, mat(0, 2.45, 1.05)));
    parts.push(cyl(0.07, 0.08, 2.3, 8, col, mat(0, 2.45, 2.25, Math.PI / 2, 0, 0)));
    parts.push(cyl(0.34, 0.36, 0.28, 8, col, mat(0.35, 3.05, -0.5)));
    if (!burnt) for (const s of [1, -1]) parts.push(starGeo(0.42, '#e6e3d6').applyMatrix4(mat(s * 1.16, 1.45, -0.6, 0, 0, s * Math.PI / 2)));
  } else if (type === 'panzer') {
    col = burnt ? '#3f3a33' : '#a58d56';
    parts.push(trackRun(1.12, 5.9, 0.95, 0.42, dark), trackRun(-1.12, 5.9, 0.95, 0.42, dark));
    for (const s of [1, -1]) for (let i = 0; i < 8; i++) parts.push(cyl(0.28, 0.28, 0.14, 8, '#3b3a33', mat(s * 1.36, 0.36, -2.4 + i * 0.68, 0, 0, Math.PI / 2)));
    parts.push(extrudeSide([[-2.95, 0.6], [2.45, 0.6], [2.95, 1.0], [2.95, 1.45], [2.55, 1.85], [-2.95, 1.85]], 1.95, col));
    parts.push(paint(new THREE.BoxGeometry(2.9, 0.08, 5.6), col, mat(0, 1.85, -0.15))); // fenders
    for (const s of [1, -1]) parts.push(paint(new THREE.BoxGeometry(0.05, 0.8, 5.0), burnt ? '#34302b' : '#8f7a48', mat(s * 1.5, 1.35, -0.2, 0, 0, s * -0.05)));
    parts.push(extrudeTop([[0.8, 1.2], [-0.8, 1.2], [-1.1, 0.3], [-1.05, -1.4], [1.05, -1.4], [1.1, 0.3]], 0.78, col, 1.9));
    parts.push(paint(new THREE.BoxGeometry(2.7, 0.62, 0.04), burnt ? '#34302b' : '#8f7a48', mat(0, 2.3, -1.65)));
    for (const s of [1, -1]) parts.push(paint(new THREE.BoxGeometry(0.04, 0.62, 2.5), burnt ? '#34302b' : '#8f7a48', mat(s * 1.36, 2.3, -0.4)));
    parts.push(paint(new THREE.BoxGeometry(0.8, 0.5, 0.35), col, mat(0, 2.3, 1.3)));
    parts.push(cyl(0.07, 0.085, 3.6, 8, col, mat(0, 2.3, 3.15, Math.PI / 2, 0, 0)));
    parts.push(cyl(0.12, 0.12, 0.32, 8, '#3a3833', mat(0, 2.3, 4.95, Math.PI / 2, 0, 0)));
    parts.push(cyl(0.34, 0.37, 0.32, 8, col, mat(0, 2.82, -0.95)));
    if (!burnt) for (const s of [1, -1]) parts.push(crossGeo(0.6, '#15140f', '#e9e6dc').applyMatrix4(mat(s * 1.53, 1.3, 0.6, 0, 0, Math.PI / 2)));
  } else { // tiger
    col = burnt ? '#3d3833' : '#9a8350';
    parts.push(trackRun(1.3, 6.3, 1.2, 0.72, dark), trackRun(-1.3, 6.3, 1.2, 0.72, dark));
    for (const s of [1, -1]) for (let i = 0; i < 8; i++) parts.push(cyl(0.42, 0.42, 0.16, 10, '#4a463b', mat(s * (1.72 + (i % 2) * 0.06), 0.62, -2.5 + i * 0.72, 0, 0, Math.PI / 2)));
    parts.push(extrudeSide([[-3.1, 0.8], [2.7, 0.8], [3.2, 1.3], [3.2, 1.75], [2.6, 2.2], [-3.1, 2.2]], 2.3, col));
    parts.push(paint(new THREE.BoxGeometry(3.7, 0.1, 6.4), col, mat(0, 2.2, -0.1)));
    parts.push(paint(new THREE.CylinderGeometry(1.45, 1.5, 1.0, 10), col, mat(0, 2.72, -0.35, 0, 0, 0, 1, 1, 1.2)));
    parts.push(paint(new THREE.BoxGeometry(1.2, 0.8, 0.4), col, mat(0, 2.75, 1.4)));
    parts.push(cyl(0.09, 0.1, 5.0, 8, col, mat(0, 2.75, 4.0, Math.PI / 2, 0, 0)));
    parts.push(cyl(0.16, 0.16, 0.45, 8, '#3a3833', mat(0, 2.75, 6.6, Math.PI / 2, 0, 0)));
    parts.push(cyl(0.38, 0.4, 0.34, 8, col, mat(-0.55, 3.34, -0.9)));
  }
  const g = new THREE.Group();
  g.add(mesh(merge(parts), MAT.metal));
  return g;
}

// =====================================================================
// BUNKER + MG TURRET
// =====================================================================
export function buildBunker() {
  const concrete = '#9d998d', dark = '#1a1917';
  const base = [];
  base.push(paint(new THREE.CylinderGeometry(4.3, 5.1, 3.4, 8), concrete, mat(0, 0.4, 0, 0, Math.PI / 8, 0)));
  base.push(paint(new THREE.CylinderGeometry(5.5, 5.5, 0.8, 8), '#8f8b80', mat(0, 2.5, 0, 0, Math.PI / 8, 0)));
  base.push(paint(new THREE.CylinderGeometry(5.2, 5.5, 0.35, 8), '#8f8b80', mat(0, 3.07, 0, 0, Math.PI / 8, 0)));
  // embrasure slit (faces +z) with a recessed frame
  base.push(paint(new THREE.BoxGeometry(2.4, 0.7, 0.3), '#86827a', mat(0, 1.45, 4.55, -0.15, 0, 0)));
  base.push(paint(new THREE.BoxGeometry(1.9, 0.38, 0.34), dark, mat(0, 1.45, 4.6, -0.15, 0, 0)));
  // rear entrance
  base.push(paint(new THREE.BoxGeometry(1.2, 1.9, 0.4), dark, mat(0, 0.65, -4.62)));
  // turret ring
  base.push(paint(new THREE.CylinderGeometry(1.55, 1.7, 0.4, 10), '#7c786e', mat(0, 3.42, 0)));
  const bunker = new THREE.Group();
  bunker.add(mesh(merge(base)));

  // turret: yaw group -> pitch group
  const yaw = new THREE.Group(); yaw.position.y = 3.62;
  const tp = [];
  tp.push(paint(new THREE.CylinderGeometry(1.0, 1.3, 1.0, 8), '#5a5e52', mat(0, 0.5, -0.1)));
  tp.push(paint(new THREE.CylinderGeometry(0.5, 1.0, 0.35, 8), '#53574b', mat(0, 1.17, -0.1)));
  tp.push(paint(new THREE.BoxGeometry(1.9, 1.05, 0.14), '#4d5145', mat(0, 0.62, 0.95, -0.2, 0, 0))); // gun shield
  tp.push(paint(new THREE.BoxGeometry(0.4, 0.3, 0.6), '#3b3e36', mat(0.8, 0.25, -0.8))); // ammo box
  yaw.add(mesh(merge(tp), MAT.metal));
  const pitch = new THREE.Group(); pitch.position.set(0, 0.62, 0.9); yaw.add(pitch);
  const gp = [];
  for (const s of [0.24, -0.24]) {
    gp.push(paint(new THREE.CylinderGeometry(0.085, 0.085, 0.9, 8), '#262724', mat(s, 0, 0.45, Math.PI / 2, 0, 0))); // perforated jacket
    gp.push(paint(new THREE.CylinderGeometry(0.04, 0.04, 1.6, 6), '#1c1c1b', mat(s, 0, 0.9, Math.PI / 2, 0, 0)));   // barrel
    gp.push(paint(new THREE.CylinderGeometry(0.06, 0.05, 0.14, 6), '#1c1c1b', mat(s, 0, 1.7, Math.PI / 2, 0, 0)));  // muzzle
    gp.push(paint(new THREE.BoxGeometry(0.16, 0.2, 0.7), '#2b2c29', mat(s, 0.02, -0.25)));  // receiver
  }
  pitch.add(mesh(merge(gp), MAT.metal));
  // muzzle flash sprites
  const flashGeo = merge([
    paint(new THREE.ConeGeometry(0.16, 0.7, 5), '#ffd27a', mat(0, 0, 0.35, Math.PI / 2, 0, 0)),
    paint(new THREE.BoxGeometry(0.5, 0.04, 0.04), '#fff1b8'), paint(new THREE.BoxGeometry(0.04, 0.5, 0.04), '#fff1b8'),
  ]);
  const flashes = [0.24, -0.24].map(s => { const f = new THREE.Mesh(flashGeo, MAT.glow); f.position.set(s, 0, 1.82); f.visible = false; pitch.add(f); return f; });
  // alert lamp
  const lamp = new THREE.Mesh(new THREE.OctahedronGeometry(0.22, 0), new THREE.MeshBasicMaterial({ color: 0x444444 }));
  lamp.position.set(0, 1.55, -0.1); yaw.add(lamp);
  bunker.add(yaw);
  bunker.userData = { yaw, pitch, flashes, lamp };
  return bunker;
}

// =====================================================================
// PLANE — Stuka-style dive bomber, gull wings, fixed spatted gear
// =====================================================================
export function buildPlane() {
  const top = '#56604c', under = '#8fa3b0', dark = '#232320';
  const p = [];
  const fus = (rt, rb, h, z, c = top) => paint(new THREE.CylinderGeometry(rt, rb, h, 8), c, mat(0, 0, z, Math.PI / 2, 0, 0));
  p.push(fus(0.62, 0.7, 1.8, 3.2, '#4f5745'));   // engine cowl (front = +z)
  p.push(fus(0.7, 0.62, 3.4, 0.6));
  p.push(fus(0.62, 0.14, 4.6, -3.4));
  p.push(paint(new THREE.CylinderGeometry(0.34, 0.66, 0.5, 8), '#2d2f2a', mat(0, 0, 4.3, Math.PI / 2, 0, 0)));
  // canopy
  p.push(paint(new THREE.BoxGeometry(0.72, 0.55, 2.6), '#2a3a44', mat(0, 0.62, 0.7)));
  p.push(paint(new THREE.BoxGeometry(0.74, 0.08, 2.64), '#3f463a', mat(0, 0.9, 0.7)));
  // radiator
  p.push(paint(new THREE.BoxGeometry(0.7, 0.5, 1.0), '#3b4136', mat(0, -0.75, 3.1)));
  // inverted gull wings
  for (const s of [1, -1]) {
    const inner = prism(2.4, 2.5, 2.2, 0.36, 0.3, 0.1, top, -0.32, s);
    inner.applyMatrix4(mat(s * 0.5, -0.25, 0.5));
    const ix = s * (0.5 + 2.4), iy = -0.25 - 2.4 * Math.tan(0.32);
    const outer = prism(5.6, 2.2, 1.3, 0.3, 0.16, 0.5, top, 0.14, s);
    outer.applyMatrix4(mat(ix, iy, 0.45));
    p.push(inner, outer);
    p.push(crossGeo(1.1, '#161614', '#ecebe4').applyMatrix4(mat(s * 5.6, iy + 0.17 + 2.7 * Math.tan(0.14), 0.0)));
    // spatted landing gear
    p.push(paint(new THREE.BoxGeometry(0.34, 1.3, 0.7), top, mat(ix, iy - 0.75, 0.7, 0, 0, s * 0.08)));
    p.push(paint(new THREE.CylinderGeometry(0.2, 0.34, 0.9, 6), under, mat(ix, iy - 1.45, 0.7, Math.PI / 2, 0, 0)));
    p.push(paint(new THREE.CylinderGeometry(0.28, 0.28, 0.14, 8), dark, mat(ix, iy - 1.6, 0.7, 0, 0, Math.PI / 2)));
    // tailplane
    const hs = prism(2.3, 1.4, 0.8, 0.16, 0.1, 0.3, top, 0, s);
    hs.applyMatrix4(mat(s * 0.12, 0.15, -5.0));
    p.push(hs);
  }
  const fin = prism(1.9, 1.6, 0.9, 0.14, 0.1, 0.5, top, 0, 1);
  fin.applyMatrix4(mat(0, 0.35, -5.2, 0, 0, Math.PI / 2));
  p.push(fin);
  p.push(paint(new THREE.BoxGeometry(0.02, 0.5, 0.5), dark, mat(0.08, 1.4, -5.4)));
  p.push(paint(new THREE.BoxGeometry(0.02, 0.5, 0.5), dark, mat(-0.08, 1.4, -5.4)));
  // bomb under belly
  const bombMesh = mesh(buildBombGeo(), MAT.metal);
  bombMesh.position.set(0, -1.0, 0.6);
  const plane = new THREE.Group();
  plane.add(mesh(merge(p), MAT.flat));
  plane.add(bombMesh);
  // propeller
  const prop = new THREE.Group(); prop.position.set(0, 0, 4.6);
  const pb = [paint(new THREE.ConeGeometry(0.3, 0.6, 6), '#2b2c28', mat(0, 0, 0.2, Math.PI / 2, 0, 0))];
  for (let i = 0; i < 3; i++) pb.push(paint(new THREE.BoxGeometry(0.18, 1.6, 0.05), dark, mat(0, 0, 0, 0, 0, i * TAU / 3).multiply(mat(0, 0.8, 0, 0, 0.3, 0))));
  prop.add(mesh(merge(pb), MAT.flat, false));
  const disc = new THREE.Mesh(new THREE.CircleGeometry(1.7, 16), new THREE.MeshBasicMaterial({ color: 0x222222, transparent: true, opacity: 0.12, side: THREE.DoubleSide, depthWrite: false }));
  prop.add(disc);
  plane.add(prop);
  plane.userData = { prop, bombMesh };
  return plane;
}
export function buildBombGeo() {
  return merge([
    paint(new THREE.CylinderGeometry(0.2, 0.2, 0.9, 8), '#3a3d36', mat(0, 0, 0, Math.PI / 2, 0, 0)),
    paint(new THREE.ConeGeometry(0.2, 0.35, 8), '#3a3d36', mat(0, 0, 0.62, Math.PI / 2, 0, 0)),
    paint(new THREE.ConeGeometry(0.2, 0.5, 8), '#3a3d36', mat(0, 0, -0.7, -Math.PI / 2, 0, 0)),
    paint(new THREE.BoxGeometry(0.6, 0.02, 0.3), '#2c2e29', mat(0, 0, -0.85)),
    paint(new THREE.BoxGeometry(0.02, 0.6, 0.3), '#2c2e29', mat(0, 0, -0.85)),
    paint(new THREE.BoxGeometry(0.42, 0.03, 0.06), '#c9b04b', mat(0, 0, 0.35)),
  ]);
}

// =====================================================================
// OBSTACLES
// =====================================================================
// Czech hedgehog: three steel I-beams, resting on three points
export function hedgehogGeo() {
  const f = 0.15, hgt = 0.17, t = 0.028, L = 2.1;
  const s = new THREE.Shape([
    [-f / 2, -hgt / 2], [f / 2, -hgt / 2], [f / 2, -hgt / 2 + t], [t / 2, -hgt / 2 + t], [t / 2, hgt / 2 - t], [f / 2, hgt / 2 - t],
    [f / 2, hgt / 2], [-f / 2, hgt / 2], [-f / 2, hgt / 2 - t], [-t / 2, hgt / 2 - t], [-t / 2, -hgt / 2 + t], [-f / 2, -hgt / 2 + t],
  ].map(p => new THREE.Vector2(p[0], p[1])));
  const beam = () => { const g = new THREE.ExtrudeGeometry(s, { depth: L, bevelEnabled: false }); g.translate(0, 0, -L / 2); return g; };
  const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(1, 1, 1).normalize(), new THREE.Vector3(0, 1, 0));
  const R = new THREE.Matrix4().makeRotationFromQuaternion(q);
  const lift = new THREE.Matrix4().makeTranslation(0, L / 2 / Math.sqrt(3) - 0.06, 0);
  const M = m => lift.clone().multiply(R).multiply(m);
  const cols = ['#5b4d42', '#554a40', '#62544a'];
  const parts = [
    paint(beam(), cols[0], M(new THREE.Matrix4())),
    paint(beam(), cols[1], M(new THREE.Matrix4().makeRotationY(Math.PI / 2).multiply(new THREE.Matrix4().makeRotationZ(Math.PI / 2)))),
    paint(beam(), cols[2], M(new THREE.Matrix4().makeRotationX(Math.PI / 2))),
    paint(new THREE.BoxGeometry(0.26, 0.26, 0.26), '#4d4239', M(new THREE.Matrix4())),
  ];
  return merge(parts);
}

// Teller mine (T.Mi.43) with pressure cap + carry handle, and the disturbed sand ring
export function tellerGeo() {
  return merge([
    paint(new THREE.CylinderGeometry(0.62, 0.72, 0.04, 12), '#8d7c5b', mat(0, 0.01, 0)),
    paint(new THREE.CylinderGeometry(0.21, 0.23, 0.1, 12), '#4c5343', mat(0, 0.07, 0)),
    paint(new THREE.CylinderGeometry(0.2, 0.2, 0.02, 12), '#596150', mat(0, 0.125, 0)),
    paint(new THREE.CylinderGeometry(0.075, 0.08, 0.05, 8), '#3b4034', mat(0, 0.15, 0)),
    paint(new THREE.BoxGeometry(0.1, 0.04, 0.05), '#33372d', mat(0.24, 0.08, 0)),
  ]);
}
export function apMineGeo() {
  return merge([
    paint(new THREE.CylinderGeometry(0.28, 0.34, 0.03, 8), '#6c5a40', mat(0, 0.0, 0)),
    paint(new THREE.CylinderGeometry(0.012, 0.012, 0.09, 4), '#2f312c', mat(0.03, 0.05, 0)),
    paint(new THREE.CylinderGeometry(0.012, 0.012, 0.09, 4), '#2f312c', mat(-0.02, 0.05, 0.03)),
    paint(new THREE.CylinderGeometry(0.012, 0.012, 0.09, 4), '#2f312c', mat(-0.01, 0.05, -0.03)),
  ]);
}
// Mined wooden stake (Hemmbalken) leaning seaward with a Teller mine on top
export function stakeGeo() {
  const log = '#6b5639';
  const L = 3.6, lean = 0.62;
  const top = [0, Math.cos(lean) * L, -Math.sin(lean) * L];
  return merge([
    paint(new THREE.CylinderGeometry(0.11, 0.14, L, 6), log, mat(0, Math.cos(lean) * L / 2, -Math.sin(lean) * L / 2, -lean, 0, 0)),
    paint(new THREE.CylinderGeometry(0.09, 0.11, 2.0, 6), '#5f4c33', mat(0, 0.9, -1.35, 0.5, 0, 0)),
    paint(new THREE.CylinderGeometry(0.21, 0.23, 0.1, 12), '#4c5343', mat(top[0], top[1] + 0.05, top[2], -lean, 0, 0)),
    paint(new THREE.CylinderGeometry(0.075, 0.08, 0.06, 8), '#3b4034', mat(top[0], top[1] + 0.12, top[2] - 0.05, -lean, 0, 0)),
  ]);
}
export function logRampGeo() {
  const g = [];
  for (const s of [-0.6, 0.6]) g.push(paint(new THREE.CylinderGeometry(0.15, 0.17, 5.6, 6), '#6b5639', mat(s, 1.2, 0, -(Math.PI / 2 - 0.45), 0, 0)));
  g.push(paint(new THREE.CylinderGeometry(0.13, 0.15, 2.6, 6), '#5f4c33', mat(0, 1.15, -1.8, 0, 0, 0)));
  for (const s of [-0.6, 0.6]) g.push(paint(new THREE.CylinderGeometry(0.1, 0.12, 2.5, 6), '#5f4c33', mat(s, 1.15, -1.8, 0, 0, 0)));
  g.push(paint(new THREE.CylinderGeometry(0.12, 0.12, 1.5, 6), '#5f4c33', mat(0, 2.35, -2.4, 0, 0, Math.PI / 2)));
  g.push(paint(new THREE.CylinderGeometry(0.21, 0.23, 0.1, 12), '#4c5343', mat(0, 2.52, -2.4)));
  return merge(g);
}
// Belgian gate / Cointet element
export function belgianGateGeo() {
  const st = '#5d5047', b = (w, h, d, m) => paint(new THREE.BoxGeometry(w, h, d), st, m);
  const g = [];
  const W = 3.0, H = 2.5, D = 2.6;
  for (const x of [-1.5, -0.5, 0.5, 1.5]) g.push(b(0.1, H, 0.1, mat(x, H / 2, 0)));
  for (const y of [0.15, 1.25, H]) g.push(b(W, 0.1, 0.1, mat(0, y, 0)));
  for (const s of [1, -1]) {
    g.push(b(0.08, Math.hypot(1, H - 0.2), 0.08, mat(s * 1.0, H / 2, 0, 0, 0, s * Math.atan2(1, H - 0.2))));
    g.push(b(0.1, 0.1, D, mat(s * 1.5, 0.1, -D / 2)));
    g.push(b(0.09, Math.hypot(D, H), 0.09, mat(s * 1.5, H / 2, -D / 2, -Math.atan2(D, H), 0, 0)));
    g.push(b(0.07, 0.07, D * 0.7, mat(s * 0.5, 0.1, -D * 0.35)));
  }
  g.push(b(W, 0.08, 0.08, mat(0, 0.1, -D)));
  // rollers
  for (const x of [-1.5, 1.5]) g.push(paint(new THREE.CylinderGeometry(0.22, 0.22, 0.14, 8), '#3a332d', mat(x, 0.22, -0.1, 0, 0, Math.PI / 2)));
  return merge(g);
}
// Concrete dragon's tooth
export function toothGeo() {
  return merge([paint(new THREE.CylinderGeometry(0.28, 0.85, 1.5, 4), '#a19d91', mat(0, 0.55, 0, 0, Math.PI / 4, 0))]);
}
// Knife-rest barricade: log + X-legs, wrapped in wire
export function knifeRestGeo() {
  const g = [paint(new THREE.CylinderGeometry(0.1, 0.1, 4.2, 6), '#6b5639', mat(0, 1.0, 0, 0, 0, Math.PI / 2))];
  for (const x of [-1.9, 0, 1.9]) for (const s of [1, -1]) g.push(paint(new THREE.CylinderGeometry(0.07, 0.07, 2.6, 6), '#5f4c33', mat(x, 0.95, 0, s * 0.8, 0, 0)));
  return merge(g);
}
export function concreteBlockGeo() {
  return merge([
    paint(new THREE.BoxGeometry(1.3, 1.1, 2.4), '#a4a095', mat(0, 0.55, 0)),
    paint(new THREE.BoxGeometry(1.0, 0.12, 2.1), '#97938a', mat(0, 1.16, 0)),
    paint(new THREE.BoxGeometry(1.34, 0.18, 2.44), '#c4412f', mat(0, 0.8, 0)),
  ]);
}
// Sandbag: hexagonal pillow
export function sandbagGeo() {
  const g = new THREE.CylinderGeometry(0.5, 0.5, 1, 6, 2);
  const p = g.attributes.position;
  for (let i = 0; i < p.count; i++) if (Math.abs(p.getY(i)) > 0.49) { p.setX(i, p.getX(i) * 0.72); p.setZ(i, p.getZ(i) * 0.72); }
  g.computeVertexNormals();
  return paint(g, '#ffffff', mat(0, 0.12, 0, 0, 0, Math.PI / 2, 1, 1, 1).multiply(new THREE.Matrix4().makeScale(0.26, 0.62, 0.38)));
}
export function bushGeo() {
  return merge([
    paint(new THREE.IcosahedronGeometry(1, 0), '#ffffff', mat(0, 0.8, 0, 0, 0, 0, 1.3, 1.0, 1.3)),
    paint(new THREE.IcosahedronGeometry(0.8, 0), '#e8ffe8', mat(0.7, 1.3, 0.2, 0.4, 0.3, 0)),
  ]);
}
export function treeGeo() {
  return merge([
    paint(new THREE.CylinderGeometry(0.18, 0.28, 3, 5), '#5b4632', mat(0, 1.5, 0)),
    paint(new THREE.ConeGeometry(2.0, 3.4, 6), '#4f6b35', mat(0, 3.9, 0)),
    paint(new THREE.ConeGeometry(1.5, 2.8, 6), '#587640', mat(0, 5.5, 0, 0, 0.5, 0)),
    paint(new THREE.ConeGeometry(0.9, 2.0, 6), '#648448', mat(0, 6.9, 0)),
  ]);
}
export function crateGeo() {
  return merge([
    paint(new THREE.BoxGeometry(1.1, 0.75, 0.8), '#4f5a32', mat(0, 0.375, 0)),
    paint(new THREE.BoxGeometry(1.14, 0.08, 0.84), '#3e4727', mat(0, 0.7, 0)),
    paint(new THREE.BoxGeometry(0.42, 0.12, 0.02), '#f1efe6', mat(0, 0.4, 0.41)), paint(new THREE.BoxGeometry(0.12, 0.42, 0.02), '#f1efe6', mat(0, 0.4, 0.41)),
    paint(new THREE.BoxGeometry(0.02, 0.12, 0.42), '#f1efe6', mat(0.56, 0.4, 0)), paint(new THREE.BoxGeometry(0.02, 0.42, 0.12), '#f1efe6', mat(0.56, 0.4, 0)),
    paint(new THREE.BoxGeometry(0.42, 0.02, 0.12), '#f1efe6', mat(0, 0.75, 0)), paint(new THREE.BoxGeometry(0.12, 0.02, 0.42), '#f1efe6', mat(0, 0.75, 0)),
  ]);
}

// Landing craft (LCVP / Higgins boat) with the bow ramp lowered
export function buildLCVP(rampDown = true) {
  const hull = '#646b6e', dark = '#3b4043', g = [];
  g.push(paint(new THREE.BoxGeometry(3.3, 0.5, 10), dark, mat(0, 0.25, 0)));
  for (const s of [1, -1]) g.push(paint(new THREE.BoxGeometry(0.16, 2.0, 10), hull, mat(s * 1.6, 1.2, 0)));
  g.push(paint(new THREE.BoxGeometry(3.3, 2.2, 0.16), hull, mat(0, 1.3, -5)));
  g.push(paint(new THREE.BoxGeometry(1.6, 1.0, 1.6), hull, mat(0, 2.6, -4.1)));
  g.push(paint(new THREE.BoxGeometry(0.5, 0.5, 0.5), '#2a2d2f', mat(1.2, 2.5, -4.1)));
  const ramp = rampDown ? mat(0, 0.18, 6.3, Math.PI / 2 - 0.2, 0, 0) : mat(0, 1.3, 5.0, 0, 0, 0);
  g.push(paint(new THREE.BoxGeometry(3.0, 2.6, 0.18), '#5b6265', ramp));
  const grp = new THREE.Group();
  grp.add(mesh(merge(g), MAT.metal));
  return grp;
}
export function buildShip() {
  const g = [];
  g.push(paint(new THREE.BoxGeometry(12, 5, 110), '#59616a', mat(0, 2.5, 0)));
  g.push(paint(new THREE.CylinderGeometry(0.01, 6, 20, 4), '#59616a', mat(0, 2.5, 64, Math.PI / 2, Math.PI / 4, 0, 1, 1, 0.9)));
  g.push(paint(new THREE.BoxGeometry(8, 7, 18), '#666e76', mat(0, 8, 10)));
  g.push(paint(new THREE.BoxGeometry(5, 5, 8), '#666e76', mat(0, 13, 12)));
  g.push(paint(new THREE.CylinderGeometry(1.4, 1.6, 8, 8), '#50575e', mat(0, 12, -6)));
  g.push(paint(new THREE.CylinderGeometry(1.4, 1.6, 8, 8), '#50575e', mat(0, 12, -14)));
  g.push(paint(new THREE.BoxGeometry(4, 2, 5), '#4c535a', mat(0, 6, 36)));
  g.push(paint(new THREE.BoxGeometry(4, 2, 5), '#4c535a', mat(0, 6, -34)));
  g.push(paint(new THREE.CylinderGeometry(0.2, 0.3, 14, 5), '#40464c', mat(0, 20, 10)));
  const grp = new THREE.Group(); grp.add(mesh(merge(g), MAT.flat, false));
  return grp;
}

// Canvas-texture warning sign "ACHTUNG MINEN"
let signTex = null;
export function signMesh() {
  if (!signTex) {
    const c = document.createElement('canvas'); c.width = 256; c.height = 224;
    const x = c.getContext('2d');
    x.fillStyle = '#c9b98c'; x.beginPath(); x.moveTo(128, 6); x.lineTo(250, 218); x.lineTo(6, 218); x.closePath(); x.fill();
    x.lineWidth = 10; x.strokeStyle = '#6e2018'; x.stroke();
    x.fillStyle = '#211d19'; x.font = 'bold 30px Arial'; x.textAlign = 'center';
    x.fillText('ACHTUNG', 128, 150); x.fillText('MINEN!', 128, 188);
    x.beginPath(); x.arc(128, 90, 24, 0, TAU); x.fill();
    x.fillStyle = '#c9b98c'; x.beginPath(); x.arc(119, 86, 6, 0, TAU); x.arc(137, 86, 6, 0, TAU); x.fill();
    signTex = new THREE.CanvasTexture(c); signTex.colorSpace = THREE.SRGBColorSpace;
  }
  const g = new THREE.Group();
  const board = new THREE.Mesh(new THREE.PlaneGeometry(1.1, 0.96), new THREE.MeshStandardMaterial({ map: signTex, transparent: true, alphaTest: 0.5, side: THREE.DoubleSide, roughness: 1 }));
  board.position.y = 1.6; g.add(board);
  const post = mesh(paint(new THREE.BoxGeometry(0.08, 1.6, 0.08), '#5f4c33', mat(0, 0.8, -0.03)));
  g.add(post);
  return g;
}

export function flagMesh() {
  const g = new THREE.Group();
  g.add(mesh(paint(new THREE.CylinderGeometry(0.06, 0.08, 7, 6), '#d7d4c8', mat(0, 3.5, 0))));
  const flagMat = new THREE.MeshStandardMaterial({ color: 0x1f5eff, side: THREE.DoubleSide, flatShading: true, roughness: 0.8 });
  const fg = new THREE.PlaneGeometry(2.4, 1.4, 6, 1);
  const flag = new THREE.Mesh(fg, flagMat); flag.position.set(1.25, 6.2, 0); g.add(flag);
  g.userData = { flag, flagMat };
  return g;
}
