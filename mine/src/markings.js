// Procedural road markings, derived from each tunnel's lane layout (lanes.js):
//  - edge lines at the (tapering) road edge
//  - centre line: dashed when 1 + 1 lanes, double solid when a side has 2 lanes
//  - dashed lane dividers on 2-lane sides; they end where the lane starts to close
//  - merge arrows in the ending lane (in that lane's travel direction) + LANE ENDS signs
//  - gaps where the paint would lie inside a pothole
// Everything is one decal mesh (+ a few sign meshes), rebuilt with the mine.
import * as THREE from 'three';
import { dentAt } from './potholes.js';
import { LANE_W } from './lanes.js';

const LIFT = 0.014;        // above the road surface
const STEP = 0.5;          // sampling along the tunnel
const DASH = 3.0, GAP = 6.0;

export function buildMarkings(mine, P) {
  const group = new THREE.Group();
  group.name = 'RoadMarkings';
  const pos = [], col = [];
  const WHITE = [0.86, 0.84, 0.78], YELLOW = [0.95, 0.72, 0.18];
  const tmp = new THREE.Vector3();

  const surf = (fr, road, s, n, out) => {
    const d = road.holes.length ? dentAt(road.holes, s, n) : { dz: 0 };
    out.copy(fr.p).addScaledVector(fr.N, n).addScaledVector(fr.B, LIFT + Math.max(d.dz, 0));
    return d.dz;
  };
  const quad = (a, b, c, d, color) => {
    for (const v of [a, b, c, a, c, d]) { pos.push(v.x, v.y, v.z); col.push(...color); }
  };

  // a strip following nFn(s) (null = no paint here) between s-positions
  const strip = (road, frames, nFn, width, color, dashed) => {
    const A = new THREE.Vector3(), B = new THREE.Vector3(), C = new THREE.Vector3(), D = new THREE.Vector3();
    for (let i = 0; i < frames.length - 1; i++) {
      const f0 = frames[i], f1 = frames[i + 1];
      const n0 = nFn(f0.s, f0.L), n1 = nFn(f1.s, f1.L);
      if (n0 == null || n1 == null) continue;
      if (dashed) {
        const u = ((f0.s - road.s0) % (DASH + GAP) + DASH + GAP) % (DASH + GAP);
        if (u > DASH) continue;
      }
      const w = width / 2;
      const z0 = surf(f0.f, road, f0.s, n0 + w, A), z1 = surf(f0.f, road, f0.s, n0 - w, B);
      const z2 = surf(f1.f, road, f1.s, n1 - w, C), z3 = surf(f1.f, road, f1.s, n1 + w, D);
      if (Math.min(z0, z1, z2, z3) < -0.008) continue; // paint gap over a pothole
      quad(A, B, C, D, color);
    }
  };

  // merge arrow: straight shaft then a bend toward the road centre, head at the end
  const arrow = (road, s, n, dir, inward, color) => {
    // local 2D (along, across); along is in the travel direction, across positive = towards centre
    const pts = [
      [[0, -0.12], [2.2, -0.12], [2.2, 0.12], [0, 0.12]],                 // shaft
      [[2.2, -0.12], [3.4, 0.45], [3.4, 0.75], [2.2, 0.12]],              // bend
      [[3.2, 0.2], [4.4, 1.2], [3.1, 1.25], [3.1, 1.25]],                 // head (degenerate quad = triangle)
    ];
    for (const q of pts) {
      const v = q.map(([al, ac]) => {
        const ss = s + dir * al, nn = n + inward * ac;
        const fr = mine.frame(road.edge, ss);
        const o = new THREE.Vector3();
        surf(fr, road, ss, nn, o);
        return o;
      });
      // keep the winding facing up
      const up = new THREE.Vector3().subVectors(v[2], v[0]).cross(new THREE.Vector3().subVectors(v[3], v[1]));
      if (up.y < 0) quad(v[0], v[3], v[2], v[1], color); else quad(v[0], v[1], v[2], v[3], color);
    }
  };

  const signs = [];
  for (const road of mine.roads) {
    const len = road.s1 - road.s0;
    if (len < 4) continue;
    const frames = [];
    for (let s = road.s0 + 0.6; s <= road.s1 - 0.6; s += STEP) frames.push({ s, f: mine.frame(road.edge, s), L: road.layout(s) });
    const full = (hw) => hw > 2 * LANE_W + 0.45; // side currently has 2 complete lanes
    // edge lines
    strip(road, frames, (s, L) => L.hwL - 0.5, 0.12, WHITE, false);
    strip(road, frames, (s, L) => -(L.hwR - 0.5), 0.12, WHITE, false);
    // centre: dashed single if 1+1, double solid otherwise (no overtaking across on wide roads)
    strip(road, frames, (s, L) => (full(L.hwL) || full(L.hwR) ? null : 0), 0.12, YELLOW, true);
    strip(road, frames, (s, L) => (full(L.hwL) || full(L.hwR) ? 0.13 : null), 0.1, YELLOW, false);
    strip(road, frames, (s, L) => (full(L.hwL) || full(L.hwR) ? -0.13 : null), 0.1, YELLOW, false);
    // lane dividers
    strip(road, frames, (s, L) => (full(L.hwL) ? L.hwL - 0.5 - LANE_W : null), 0.12, WHITE, true);
    strip(road, frames, (s, L) => (full(L.hwR) ? -(L.hwR - 0.5 - LANE_W) : null), 0.12, WHITE, true);

    // lane drops: right side travels +s, left side travels -s
    for (const t of road.layout.tapers || []) {
      const [l0, r0] = t.from, [l1, r1] = t.to;
      const drops = [];
      if (r0 === 2 && r1 === 1) drops.push({ side: -1, dir: 1, sStart: t.c - t.L / 2 });
      if (l1 === 2 && l0 === 1) drops.push({ side: 1, dir: -1, sStart: t.c + t.L / 2 });
      for (const d of drops) {
        // run-up inside this tunnel before the lane starts closing (tunnels are often short)
        const avail = d.dir > 0 ? d.sStart - road.s0 : road.s1 - d.sStart;
        const spacing = Math.min(15, (avail - 7) / 3);
        if (spacing >= 3.5) {
          for (let k = 1; k <= 3; k++) {
            const s = d.sStart - d.dir * (k * spacing + 1);
            const L = road.layout(s);
            const hw = d.side > 0 ? L.hwL : L.hwR;
            if (!full(hw)) continue;
            const nOuter = d.side * (hw - 0.5 - LANE_W / 2);
            arrow(road, s, nOuter - d.side * 0.3, d.dir, -d.side, WHITE);
          }
        }
        // sign: as early as possible (near the entry mouth, max 60 m ahead of the taper)
        const back = Math.min(60, avail - 3);
        if (back >= 3) signs.push({ road, s: d.sStart - d.dir * back, side: d.side, dir: d.dir });
      }
    }
  }

  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  geo.computeVertexNormals();
  const mat = new THREE.MeshStandardMaterial({
    vertexColors: true, roughness: 0.7, metalness: 0,
    emissive: 0x2a2820, emissiveIntensity: 0.6, // retro-reflective paint
    polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2,
  });
  const mesh = new THREE.Mesh(geo, mat);
  mesh.name = 'markings';
  mesh.receiveShadow = true;
  group.add(mesh);

  // LANE ENDS signs on the wall of the ending side, facing the approaching traffic
  if (signs.length) {
    const tex = laneEndsTexture();
    const face = new THREE.MeshStandardMaterial({ map: tex, color: tex ? 0xffffff : 0xffb020, emissive: 0x332200, emissiveIntensity: tex ? 0.5 : 0.2, roughness: 0.6 });
    const back = new THREE.MeshStandardMaterial({ color: 0x333333, roughness: 0.6, metalness: 0.6 });
    const pole = new THREE.MeshStandardMaterial({ color: 0x555555, roughness: 0.5, metalness: 0.7 });
    for (const sg of signs) {
      const f = mine.frame(sg.road.edge, sg.s);
      const L = sg.road.layout(sg.s);
      const hw = sg.side > 0 ? L.hwL : L.hwR;
      const g = new THREE.Group();
      const base = f.p.clone().addScaledVector(f.N, sg.side * (hw - 0.15));
      g.position.copy(base);
      // local +z faces the driver coming along dir
      const toward = f.T.clone().multiplyScalar(-sg.dir);
      g.lookAt(base.clone().add(toward.setY(0).normalize()));
      const post = new THREE.Mesh(new THREE.CylinderGeometry(0.04, 0.05, 1.9, 8), pole);
      post.position.y = 1.05; g.add(post);
      const plate = new THREE.Mesh(new THREE.PlaneGeometry(1.1, 1.1), face);
      plate.position.set(0, 2.25, 0.03); plate.rotation.z = Math.PI / 4; g.add(plate);
      const bk = new THREE.Mesh(new THREE.BoxGeometry(1.14, 1.14, 0.03), back);
      bk.position.set(0, 2.25, 0); bk.rotation.z = Math.PI / 4; g.add(bk);
      g.userData.sign = 'lane-ends';
      group.add(g);
    }
  }
  group.userData.stats = { tris: pos.length / 9, signs: signs.length };
  return group;
}

let _tex = null;
function laneEndsTexture() {
  if (typeof document === 'undefined') return null;
  if (_tex) return _tex;
  const c = document.createElement('canvas');
  c.width = c.height = 256;
  const g = c.getContext('2d');
  g.fillStyle = '#f2a900'; g.fillRect(0, 0, 256, 256);
  g.strokeStyle = '#111'; g.lineWidth = 12; g.strokeRect(10, 10, 236, 236);
  // the plate is rotated +45 deg (diamond) -> draw the content rotated back
  g.translate(128, 128); g.rotate(-Math.PI / 4);
  g.fillStyle = '#111';
  g.font = 'bold 34px sans-serif'; g.textAlign = 'center';
  g.fillText('LANE', 0, -34); g.fillText('ENDS', 0, 2);
  // two lanes merging into one
  g.lineWidth = 9; g.strokeStyle = '#111'; g.lineCap = 'round';
  g.beginPath(); g.moveTo(-26, 62); g.lineTo(-26, 30); g.lineTo(-6, 12); g.stroke();
  g.beginPath(); g.moveTo(10, 62); g.lineTo(10, 14); g.stroke();
  _tex = new THREE.CanvasTexture(c);
  _tex.colorSpace = THREE.SRGBColorSpace;
  _tex.anisotropy = 4;
  return _tex;
}
