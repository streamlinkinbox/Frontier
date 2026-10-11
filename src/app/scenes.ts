/**
 * Orbit camera + scene presets, shared by the browser app and the headless
 * render check so a verified frame means the same thing in both.
 */

import { clamp, m4lookAt, m4mul, m4perspective, type Mat4, type V3 } from '../core/math';
import { makeDoc, makeEdge, makeNode, makeArea, type RoadClass, type RoadDoc } from '../core/model';

export class OrbitCamera {
  target: V3 = [0, 0, 0];
  distance = 150;
  /** radians, 0 = looking north (-Z is up on screen) */
  yaw = 0.6;
  pitch = 0.62;
  fov = (38 * Math.PI) / 180;
  near = 0.4;
  far = 3000;

  eye(): V3 {
    const cp = Math.cos(this.pitch);
    return [
      this.target[0] + Math.sin(this.yaw) * cp * this.distance,
      this.target[1] + Math.sin(this.pitch) * this.distance,
      this.target[2] + Math.cos(this.yaw) * cp * this.distance,
    ];
  }

  viewProj(aspect: number): Mat4 {
    const view = m4lookAt(this.eye(), this.target, [0, 1, 0]);
    const proj = m4perspective(this.fov, aspect, this.near, this.far);
    return m4mul(proj, view);
  }

  orbit(dx: number, dy: number): void {
    this.yaw -= dx * 0.006;
    this.pitch = clamp(this.pitch + dy * 0.005, 0.03, 1.52);
  }

  pan(dx: number, dy: number): void {
    const e = this.eye();
    const forward: V3 = [this.target[0] - e[0], this.target[1] - e[1], this.target[2] - e[2]];
    const fl = Math.hypot(forward[0], forward[1], forward[2]) || 1;
    const f: V3 = [forward[0] / fl, forward[1] / fl, forward[2] / fl];
    const right: V3 = [f[2], 0, -f[0]];
    const rl = Math.hypot(right[0], right[2]) || 1;
    const r: V3 = [right[0] / rl, 0, right[2] / rl];
    const flat: V3 = [-r[2] * f[1], r[0] * f[2] - r[2] * f[0], r[0] * f[1]];
    const v: V3 = [r[2] * f[0] - r[0] * f[2], r[2] * f[1] - r[1] * f[2], r[1] * f[0] - r[0] * f[1]];
    void flat;
    void v;
    const speed = this.distance * 0.0016;
    this.target = [
      this.target[0] - r[0] * dx * speed - f[0] * dy * speed,
      this.target[1],
      this.target[2] - r[2] * dx * speed - f[2] * dy * speed,
    ];
  }

  zoom(delta: number): void {
    this.distance = clamp(this.distance * Math.exp(delta * 0.0016), 8, 2200);
  }
}

export interface ScenePreset {
  id: string;
  name: string;
  build(): RoadDoc;
}

function gridDoc(): RoadDoc {
  const doc = makeDoc('Grid district');
  const n = 4;
  const step = 96;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      doc.nodes.push(makeNode((i - (n - 1) / 2) * step, (j - (n - 1) / 2) * step));
    }
  }
  const at = (i: number, j: number) => doc.nodes[j * n + i];
  for (let j = 0; j < n; j++) {
    for (let i = 0; i + 1 < n; i++) {
      doc.edges.push(makeEdge(at(i, j).id, at(i + 1, j).id, j === 1 ? 'avenue' : 'street'));
    }
  }
  for (let i = 0; i < n; i++) {
    for (let j = 0; j + 1 < n; j++) {
      doc.edges.push(makeEdge(at(i, j).id, at(i, j + 1).id, i === 2 ? 'boulevard' : 'street'));
    }
  }
  return doc;
}

function crossroadsDoc(): RoadDoc {
  const doc = makeDoc('Boulevard crossing');
  const hub = makeNode(0, 0);
  doc.nodes.push(hub);
  const arms: Array<[number, RoadClass]> = [
    [0, 'boulevard'],
    [90, 'arterial'],
    [180, 'boulevard'],
    [270, 'arterial'],
  ];
  for (const [deg, cls] of arms) {
    const a = (deg / 180) * Math.PI;
    const n = makeNode(Math.cos(a) * 150, Math.sin(a) * 150);
    doc.nodes.push(n);
    doc.edges.push(makeEdge(hub.id, n.id, cls));
  }
  return doc;
}

function yDoc(): RoadDoc {
  const doc = makeDoc('Y junction');
  const hub = makeNode(0, 0);
  doc.nodes.push(hub);
  const arms: Array<[number, number, RoadClass]> = [
    [-24, 130, 'avenue'],
    [196, 130, 'avenue'],
    [86, 110, 'street'],
  ];
  for (const [deg, len, cls] of arms) {
    const a = (deg / 180) * Math.PI;
    const n = makeNode(Math.cos(a) * len, Math.sin(a) * len);
    doc.nodes.push(n);
    doc.edges.push(makeEdge(hub.id, n.id, cls));
  }
  return doc;
}

function interchangeDoc(): RoadDoc {
  const doc = makeDoc('Interchange');
  const hub = makeNode(0, 0);
  doc.nodes.push(hub);
  const arms: Array<[number, number, RoadClass]> = [
    [0, 260, 'motorway'],
    [180, 260, 'motorway'],
    [34, 180, 'ramp'],
    [214, 180, 'ramp'],
  ];
  for (const [deg, len, cls] of arms) {
    const a = (deg / 180) * Math.PI;
    const n = makeNode(Math.cos(a) * len, Math.sin(a) * len);
    doc.nodes.push(n);
    doc.edges.push(makeEdge(hub.id, n.id, cls));
  }
  return doc;
}

function neighbourhoodDoc(): RoadDoc {
  const doc = makeDoc('Residential loop');
  const n = 7;
  const r = 120;
  const ring: string[] = [];
  for (let i = 0; i < n; i++) {
    const a = (i / n) * Math.PI * 2;
    const node = makeNode(Math.cos(a) * r, Math.sin(a) * r);
    doc.nodes.push(node);
    ring.push(node.id);
  }
  for (let i = 0; i < n; i++) doc.edges.push(makeEdge(ring[i], ring[(i + 1) % n], 'local'));
  // two cross streets through the middle
  const midA = makeNode(0, 0);
  const midB = makeNode(0, 0);
  doc.nodes.push(midA, midB);
  doc.edges.push(makeEdge(ring[0], midA.id, 'street'));
  doc.edges.push(makeEdge(midA.id, ring[3], 'street'));
  doc.edges.push(makeEdge(ring[5], midB.id, 'lane'));
  doc.edges.push(makeEdge(midB.id, ring[2], 'lane'));
  // cul-de-sac branch (gets a turning head)
  const cul = makeNode(0, -r * 0.45);
  doc.nodes.push(cul);
  doc.edges.push(makeEdge(midA.id, cul.id, 'lane'));
  // a small paved square
  doc.areas.push(
    makeArea('plaza', [
      [-26 + r * 0.2, 30],
      [26 + r * 0.2, 30],
      [26 + r * 0.2, 74],
      [-26 + r * 0.2, 74],
    ]),
  );
  return doc;
}

export const SCENES: ScenePreset[] = [
  { id: 'grid', name: 'Grid district', build: gridDoc },
  { id: 'cross', name: 'Boulevard crossing', build: crossroadsDoc },
  { id: 'y', name: 'Y junction', build: yDoc },
  { id: 'interchange', name: 'Interchange', build: interchangeDoc },
  { id: 'hood', name: 'Residential loop', build: neighbourhoodDoc },
];
