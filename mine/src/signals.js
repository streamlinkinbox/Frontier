// Junction control for T (3-arm) and + (4-arm) junctions, chosen at random per
// junction (seeded):
//   'lights' - traffic lights at every arm mouth; trains obey them, the player
//              gets a time penalty for running a red
//   'sign'   - level-crossing warning sign (crossbuck + amber flashers that
//              blink while a train is approaching / crossing); nobody stops
//   'none'   - nothing. Look both ways.
// The phase logic is node-safe (used by tests); visuals are only built when a
// scene is passed.
import * as THREE from 'three';
import { mulberry32 } from './noise.js';

const GREEN = 8, AMBER = 2, ALLRED = 2;
const UP = new THREE.Vector3(0, 1, 0);

export class JunctionControl {
  constructor(scene = null) {
    this.scene = scene;
    this.group = new THREE.Group();
    this.group.name = 'JunctionControl';
    if (scene) scene.add(this.group);
    this.hubs = new Map(); // hub id -> control
    this.mats = null;
  }

  // mode: 'random' | 'lights' | 'signs' | 'none'
  rebuild(mine, P, mode = 'random', seed = 1) {
    this.clear();
    const rnd = mulberry32(seed * 7919 + 13);
    for (const h of mine.hubs) {
      const K = h.arms.length;
      const r = rnd(); // always consume -> stable choices across modes
      if (K !== 3 && K !== 4) continue;
      let type = mode === 'lights' ? 'lights' : mode === 'signs' ? 'sign' : mode === 'none' ? 'none'
        : r < 0.4 ? 'lights' : r < 0.7 ? 'sign' : 'none';
      if (type === 'none') { this.hubs.set(h.id, { type, hub: h }); continue; }
      const c = { type, hub: h, arms: h.arms.map((a) => ({ ...a })), t: rnd() * 20, blink: 0, active: false };
      if (type === 'lights') {
        // phase groups: opposite arms share a green
        const idx = h.arms.map((_, i) => i);
        let groups;
        if (K === 4) groups = [[0, 2], [1, 3]];
        else {
          let best = null;
          for (let i = 0; i < 3; i++) for (let j = i + 1; j < 3; j++) {
            const d = h.arms[i].T.dot(h.arms[j].T);
            if (!best || d < best.d) best = { d, i, j };
          }
          groups = [[best.i, best.j], idx.filter((k) => k !== best.i && k !== best.j)];
        }
        c.groups = groups;
        c.cycle = groups.length * (GREEN + AMBER + ALLRED);
        c.arms.forEach((a, i) => { a.group = groups.findIndex((g) => g.includes(i)); });
      }
      this.hubs.set(h.id, c);
      if (this.scene) this.buildVisuals(c, P);
    }
    this.carPrev = new Map();
  }

  clear() {
    this.group.children.slice().forEach((o) => {
      this.group.remove(o);
      o.traverse((m) => { if (m.geometry) m.geometry.dispose(); });
    });
    this.hubs.clear();
  }

  counts() {
    const n = { lights: 0, sign: 0, none: 0 };
    for (const c of this.hubs.values()) n[c.type]++;
    return n;
  }

  // phase state for an arm of a lights junction: 'green' | 'amber' | 'red', and seconds left in it
  armState(c, armIndex) {
    const slot = GREEN + AMBER + ALLRED;
    const t = ((c.t % c.cycle) + c.cycle) % c.cycle;
    const g = Math.floor(t / slot), u = t - g * slot;
    if (c.arms[armIndex].group !== g) {
      // red; time until this arm's group turns green
      const toGreen = ((c.arms[armIndex].group - g + c.groups.length) % c.groups.length) * slot - u;
      return { state: 'red', left: toGreen };
    }
    if (u < GREEN) return { state: 'green', left: GREEN - u };
    if (u < GREEN + AMBER) return { state: 'amber', left: GREEN + AMBER - u };
    return { state: 'red', left: slot - u };
  }

  armIndexForEdge(c, edge, hubId) {
    // an edge can touch the same hub twice only for loops (not generated); match by edge
    return c.arms.findIndex((a) => a.edge === edge);
  }

  // Train interface: may a train coming from `edge` enter hub `hubId` now,
  // needing `needTime` seconds to clear it completely?
  canEnter(hubId, edge, needTime) {
    const c = this.hubs.get(hubId);
    if (!c || c.type !== 'lights') return true;
    const i = this.armIndexForEdge(c, edge, hubId);
    if (i < 0) return true;
    const s = this.armState(c, i);
    // amber + all-red exist to clear the junction, so they count as clearing time
    return s.state === 'green' && s.left + AMBER + ALLRED - 0.5 >= needTime;
  }
  isSignalled(hubId) { const c = this.hubs.get(hubId); return !!c && c.type === 'lights'; }

  update(dt, traffic) {
    for (const [id, c] of this.hubs) {
      if (c.type === 'none') continue;
      c.t += dt;
      if (c.type === 'sign') {
        // flashers: a train holds or is queueing for this junction
        const q = traffic && traffic.queues && traffic.queues.get(id);
        c.active = !!(traffic && traffic.locks && traffic.locks.get(id)) || !!(q && q.length);
        c.blink += dt;
      }
      if (this.scene) this.updateVisuals(c);
    }
  }

  // Player: returns 'red' once when the car enters a lights junction against a red
  checkCar(pos, roadHalfWidth) {
    let event = null;
    for (const [id, c] of this.hubs) {
      if (c.type !== 'lights') continue;
      if (c.hub.center.distanceToSquared(pos) > 45 * 45) continue;
      c.arms.forEach((a, i) => {
        const rel = pos.clone().sub(a.mouth);
        const d = rel.dot(a.T); // > 0: still in the tunnel
        const side = new THREE.Vector3().crossVectors(UP, a.T);
        const lat = rel.dot(side);
        const key = id * 16 + i;
        const prev = this.carPrev.get(key);
        this.carPrev.set(key, d);
        if (prev === undefined || Math.abs(lat) > roadHalfWidth + 0.5 || Math.abs(rel.y) > 4) return;
        if (prev > 0.2 && d <= 0.2 && this.armState(c, i).state === 'red') event = 'red';
      });
    }
    return event;
  }

  // ------------------------------------------------------------ visuals
  materials() {
    if (this.mats) return this.mats;
    const lamp = (hex, on) => new THREE.MeshStandardMaterial({ color: on ? 0xffffff : 0x111111, emissive: hex, emissiveIntensity: on ? 9 : 0.06, roughness: 0.3 });
    this.mats = {
      body: new THREE.MeshStandardMaterial({ color: 0x1c1c1c, roughness: 0.6, metalness: 0.5 }),
      pole: new THREE.MeshStandardMaterial({ color: 0x8a8a84, roughness: 0.5, metalness: 0.7 }),
      stripe: new THREE.MeshStandardMaterial({ color: 0xd9a300, roughness: 0.6 }),
      white: new THREE.MeshStandardMaterial({ color: 0xe8e4da, roughness: 0.7 }),
      red: new THREE.MeshStandardMaterial({ color: 0xb01010, roughness: 0.6 }),
      stopLine: new THREE.MeshStandardMaterial({ color: 0x8a8272, roughness: 0.8, polygonOffset: true, polygonOffsetFactor: -3, polygonOffsetUnits: -3 }),
      R: [lamp(0xff1a0a, false), lamp(0xff1a0a, true)],
      A: [lamp(0xffa000, false), lamp(0xffa000, true)],
      G: [lamp(0x18ff5a, false), lamp(0x18ff5a, true)],
      warnSign: signTexture(),
    };
    return this.mats;
  }

  buildVisuals(c, P) {
    const M = this.materials();
    const hw = P.roadHalfWidth;
    c.arms.forEach((a) => {
      // frame at the mouth, facing traffic that enters the junction (-T)
      const right = new THREE.Vector3().crossVectors(UP, a.T).normalize(); // right-hand side for inbound traffic
      const g = new THREE.Group();
      const base = a.mouth.clone().addScaledVector(a.T, 1.4).addScaledVector(right, hw - 0.4);
      g.position.copy(base);
      g.lookAt(base.clone().add(a.T)); // local +z faces the approaching driver (who comes out of the tunnel)
      // snap to the floor
      g.position.y = a.mouth.y + 0.12;
      const box = (w, h, d, m, x, y, z) => { const b = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), m); b.position.set(x, y, z); g.add(b); return b; };
      const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.07, 2.6, 8), M.pole);
      pole.position.y = 1.3; g.add(pole);
      for (let i = 0; i < 4; i++) box(0.16, 0.12, 0.16, i % 2 ? M.body : M.stripe, 0, 0.1 + i * 0.12, 0); // hazard base
      if (c.type === 'lights') {
        box(0.38, 1.05, 0.26, M.body, 0, 2.25, 0.05);
        box(0.44, 1.12, 0.03, M.body, 0, 2.25, -0.1); // backplate
        a.lamps = ['R', 'A', 'G'].map((k, i) => {
          const l = new THREE.Mesh(new THREE.CylinderGeometry(0.105, 0.105, 0.05, 14), M[k][0]);
          l.rotation.x = Math.PI / 2;
          l.position.set(0, 2.57 - i * 0.32, 0.19);
          g.add(l);
          const visor = new THREE.Mesh(new THREE.CylinderGeometry(0.14, 0.14, 0.14, 12, 1, true, -Math.PI / 2, Math.PI), M.body);
          visor.rotation.x = Math.PI / 2; visor.rotation.y = Math.PI;
          visor.position.set(0, 2.6 - i * 0.32, 0.26);
          g.add(visor);
          return { mesh: l, key: k };
        });
        // stop line across the inbound half of the road
        const sl = new THREE.Mesh(new THREE.PlaneGeometry(hw - 0.3, 0.35), M.stopLine);
        sl.rotation.x = -Math.PI / 2;
        const slp = a.mouth.clone().addScaledVector(a.T, 3.2).addScaledVector(right, (hw - 0.3) / 2);
        slp.y += 0.01;
        sl.position.copy(slp);
        sl.rotation.z = Math.atan2(a.T.x, a.T.z);
        this.group.add(sl);
      } else {
        // crossbuck + warning plate + twin amber flashers
        for (const s of [1, -1]) {
          const b = box(1.25, 0.2, 0.03, M.white, 0, 2.45, 0.08);
          b.rotation.z = s * 0.62;
          const r2 = box(1.1, 0.07, 0.035, M.red, 0, 2.45, 0.09);
          r2.rotation.z = s * 0.62;
        }
        const plate = new THREE.Mesh(new THREE.PlaneGeometry(0.62, 0.62), new THREE.MeshStandardMaterial({ map: M.warnSign, roughness: 0.6, emissive: 0x332200, emissiveMap: M.warnSign, emissiveIntensity: 0.4 }));
        plate.position.set(0, 1.55, 0.08); plate.rotation.z = Math.PI / 4; g.add(plate);
        box(0.9, 0.08, 0.08, M.body, 0, 1.95, 0.05);
        a.flash = [-0.36, 0.36].map((x) => {
          const l = new THREE.Mesh(new THREE.CylinderGeometry(0.1, 0.1, 0.05, 14), M.A[0]);
          l.rotation.x = Math.PI / 2; l.position.set(x, 1.95, 0.12); g.add(l);
          return l;
        });
      }
      this.group.add(g);
    });
  }

  updateVisuals(c) {
    const M = this.mats;
    if (c.type === 'lights') {
      c.arms.forEach((a, i) => {
        const s = this.armState(c, i).state;
        const on = { R: s === 'red', A: s === 'amber', G: s === 'green' };
        for (const l of a.lamps) { const m = M[l.key][on[l.key] ? 1 : 0]; if (l.mesh.material !== m) l.mesh.material = m; }
      });
    } else if (c.type === 'sign') {
      const phase = Math.floor(c.blink * 2.5) % 2;
      c.arms.forEach((a) => a.flash.forEach((l, k) => { const m = M.A[c.active && phase === k ? 1 : 0]; if (l.material !== m) l.material = m; }));
    }
  }
}

function signTexture() {
  if (typeof document === 'undefined') return null;
  const cv = document.createElement('canvas'); cv.width = cv.height = 256;
  const g = cv.getContext('2d');
  g.fillStyle = '#f2b400'; g.fillRect(0, 0, 256, 256);
  g.strokeStyle = '#111'; g.lineWidth = 14; g.strokeRect(10, 10, 236, 236);
  // counter-rotate the content (the plate is mounted as a diamond)
  g.translate(128, 128); g.rotate(Math.PI / 4); // canvas y points down -> opposite sign to the plate rotation
  g.fillStyle = '#111';
  // little mine cart icon
  g.fillRect(-52, -30, 104, 44);
  g.beginPath(); g.moveTo(-62, -34); g.lineTo(62, -34); g.lineTo(52, -26); g.lineTo(-52, -26); g.fill();
  g.beginPath(); g.arc(-30, 24, 13, 0, Math.PI * 2); g.arc(30, 24, 13, 0, Math.PI * 2); g.fill();
  g.fillRect(-70, 38, 140, 7);
  g.font = 'bold 30px sans-serif'; g.textAlign = 'center'; g.fillText('TRAINS', 0, 80);
  const t = new THREE.CanvasTexture(cv);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}
