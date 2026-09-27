/**
 * MOSQUITO BUILDER
 *
 * Constructs a female Culicid as a pure node hierarchy: every rigid sclerite
 * (femur, tibia, tergite, labium segment, wing panel…) is parented to a joint
 * node. Insect appendages genuinely ARE rigid segments on joints, so this is
 * both anatomically honest and free of skinning collapse — and it exports to
 * glTF as native node animation that Unreal/Unity import without retarget work.
 *
 * Frame: +X anterior, +Y dorsal, +Z left. 1 unit = 1 mm.
 * Bone convention: each joint's local +Y points along the segment it drives.
 */

import * as THREE from 'three';
import { BODY, LEGS, WING, coxaRestDir } from './anatomy.js';
import { MeshBuilder, blobGeometry, latheGeometry, resample, transportFrames } from './meshbuilder.js';
import { mulberry } from '../render/textures.js';

const Y = new THREE.Vector3(0, 1, 0);
const v3 = (a) => new THREE.Vector3(a[0], a[1], a[2]);

/* ------------------------------------------------------------------ rig */

class Rig {
  constructor(root) { this.root = root; }

  /**
   * Create a joint node under `parent` at a position/orientation expressed in
   * the PARENT's local frame. Every segment of the mosquito is authored as a
   * local offset (0, segmentLength, 0) from the joint above it, so local is the
   * only frame that makes a rest pose meaningful.
   */
  joint(parent, name, localPos, localQuat = new THREE.Quaternion()) {
    const o = new THREE.Object3D();
    // Names must be unique across the whole rig: a GLB animation track is
    // addressed by object name, and 'femur' / 'tibia' / 't1'…'t4' otherwise
    // appear six times over. Qualifying with the parent's own name keeps them
    // short and still unique.
    o.name = parent === this.root ? name : `${parent.name.split('.').pop()}.${name}`;
    o.position.copy(localPos);
    o.quaternion.copy(localQuat);
    parent.add(o);
    return o;
  }

  /** Joint at local `p`, whose local +Y points along local `dir`. */
  chain(parent, name, p, dir) {
    const d = dir.clone().normalize();
    const q = new THREE.Quaternion().setFromUnitVectors(Y, d);
    return this.joint(parent, name, p, q);
  }
}

/* ------------------------------------------------------------- helpers */

/** Interpolation of the abdomen profile table. */
function profileAt(profile, t) {
  const n = profile.length;
  if (t <= profile[0][0]) return profile[0][1];
  if (t >= profile[n - 1][0]) return profile[n - 1][1];
  for (let i = 0; i < n - 1; i++) {
    if (t <= profile[i + 1][0]) {
      const f = (t - profile[i][0]) / (profile[i + 1][0] - profile[i][0] || 1);
      return profile[i][1] * (1 - f) + profile[i + 1][1] * f;
    }
  }
  return profile[n - 1][1];
}

/** Wing planform: chord as a function of span fraction, with smooth blending. */
function wingChord(u) {
  const p = WING.planform;
  u = Math.min(1, Math.max(0, u));
  for (let i = 0; i < p.length - 1; i++) {
    if (u <= p[i + 1][0]) {
      const f = (u - p[i][0]) / (p[i + 1][0] - p[i][0] || 1);
      const s = f * f * (3 - 2 * f);
      return p[i][1] * (1 - s) + p[i + 1][1] * s;
    }
  }
  return p[p.length - 1][1];
}

/* =====================================================================
 *  BODY
 * =================================================================== */

export function buildMosquito(materials, opts = {}) {
  const root = new THREE.Object3D();
  root.name = 'Mosquito_Root';
  const rig = new Rig(root);
  const rnd = mulberry(2024);

  // ---- ground-level body node -------------------------------------
  const body = rig.joint(root, 'Body', new THREE.Vector3(0, 0, 0), new THREE.Quaternion());
  body.rotation.z = 0; // +Y is dorsal in the body frame; the rig keeps +X forward
  // Orient so the bone convention (+Y along segment) still reads naturally:
  const thorax = rig.joint(body, 'Thorax', new THREE.Vector3(0.30, 0, 0), new THREE.Quaternion());

  /* ------------------------------------------------------------ head -- */
  const headCentre = v3(BODY.head.centre);
  const head = rig.joint(thorax, 'Head', headCentre, new THREE.Quaternion());
  const HEAD = BODY.head;

  // Head capsule: a rounded, boxy sclerite, wider than deep, flattened ventrally.
  const headB = blobGeometry({
    size: [HEAD.depth * 0.5, HEAD.height * 0.5, HEAD.width * 0.5],
    exponent: 0.74, nu: 30, nv: 42,
    radiusMod: (u, v) => {
      // v (0..1) around the Y axis: flatten the ventral face, bulge the
      // posterior frons between the eyes.
      const th = v * Math.PI * 2;
      const ventral = 1 - 0.20 * Math.max(0, -Math.cos(th)) * Math.max(0, (u - 0.35) * 1.6);
      const frons = 1 + 0.05 * Math.max(0, Math.sin(th)) * Math.max(0, 0.6 - u);
      const clypeus = 1 - 0.13 * Math.max(0, -Math.cos(th)) * Math.max(0, (0.52 - u) * 2.6);
      return ventral * frons * clypeus;
    },
  });
  const headMesh = new THREE.Mesh(headB.build(), materials.cuticle);
  headMesh.name = 'HeadCapsule';
  head.add(headMesh);

  // Compound eyes — the single most recognisable feature. Each is a large,
  // kidney-shaped bulge that nearly meets its partner on the frons.
  for (const s of [1, -1]) {
    const eb = blobGeometry({
      size: [HEAD.eyeRadius * 0.92, HEAD.eyeRadius * 0.98, HEAD.eyeRadius * 1.22],
      exponent: 0.88, nu: 26, nv: 36,
      radiusMod: (u, v) => {
        const th = v * Math.PI * 2;
        // Slight kidney notch posteriorly where the eye meets the occiput
        const notch = 1 - 0.10 * Math.max(0, -Math.cos(th)) * Math.max(0, (u - 0.55) * 2.0);
        return notch;
      },
      centre: [0, 0, 0],
    }).build();
    const eye = new THREE.Mesh(eb, materials.eye);
    eye.name = `CompoundEye.${s > 0 ? 'L' : 'R'}`;
    // Position relative to the head node.
    const off = new THREE.Vector3(-0.02, 0.035, s * (HEAD.width * 0.30));
    eye.position.copy(off);
    eye.rotation.y = s * 0.10;
    eye.rotation.x = -0.08;
    head.add(eye);
    // Refresh the ommatidia map per eye so the two lattices do not mirror.
    eye.geometry.attributes.uv.needsUpdate = true;
  }

  // Ocelli
  for (const o of HEAD.ocelli) {
    const m = new THREE.Mesh(new THREE.SphereGeometry(0.028, 10, 8), materials.ocellus);
    m.position.set(o[0] - headCentre.x, o[1] - headCentre.y, o[2]);
    head.add(m);
  }

  // Clypeus: the ventral plate that carries the two silvery dots in females.
  {
    const cb = latheGeometry([[0.0, -0.02], [0.09, -0.03], [0.115, 0.02], [0.10, 0.07], [0.0, 0.09]], 20);
    const cm = new THREE.Mesh(cb.build(), materials.cuticle);
    cm.position.set(0.22, -0.10, 0);
    cm.rotation.z = -Math.PI * 0.5;
    head.add(cm);
    for (const s of [1, -1]) {
      const dot = new THREE.Mesh(new THREE.SphereGeometry(0.021, 8, 6), materials.paleScale);
      dot.scale.set(1, 0.4, 1);
      dot.position.set(0.20, -0.155, s * 0.045);
      head.add(dot);
    }
  }

  // Antennae — 12 flagellomeres, sparse hairs (female; males are plumose).
  for (const s of [1, -1]) {
    buildAntenna(rig, head, s, HEAD, materials, rnd);
  }
  // Maxillary palps — short, 4-segmented, pale-tipped (culicine female).
  for (const s of [1, -1]) buildPalp(rig, head, s, HEAD, BODY.proboscis, materials);

  /* --------------------------------------------------------- proboscis */
  const prob = buildProboscis(rig, head, BODY.proboscis, materials, opts, rnd);

  /* ----------------------------------------------------------- thorax */
  buildThoraxDetail(rig, thorax, materials, rnd);

  /* ---------------------------------------------------------- abdomen */
  const abdomen = buildAbdomen(rig, thorax, BODY.abdomen, materials, rnd);

  /* ------------------------------------------------------------- legs */
  const legs = {};
  for (const key of ['L1', 'L2', 'L3', 'R1', 'R2', 'R3']) {
    legs[key] = buildLeg(rig, thorax, LEGS[key], BODY.thorax, materials, rnd);
  }

  /* ------------------------------------------------------------ wings */
  const wings = {
    L: buildWing(rig, thorax, 1, materials, rnd),
    R: buildWing(rig, thorax, -1, materials, rnd),
  };

  /* --------------------------------------------------------- halteres */
  for (const s of [1, -1]) {
    const b = new MeshBuilder();
    const c = BODY.thorax.haltere;
    const p0 = new THREE.Vector3(0, 0, 0);
    const p1 = new THREE.Vector3(-c.length * 0.55, -c.length * 0.12, s * c.length * 0.30);
    const p2 = new THREE.Vector3(-c.length * 0.95, -c.length * 0.26, s * c.length * 0.52);
    b.tube([p0, p1, p2], (i, t) => c.radius * (1 - t * 0.5), { radial: 8, capEnd: false });
    // Knob
    const knob = blobGeometry({ size: [c.length * 0.16, c.length * 0.10, c.length * 0.13], exponent: 0.9, nu: 10, nv: 14 });
    b.append(knob, new THREE.Matrix4().setPosition(p2.x, p2.y, p2.z));
    const m = new THREE.Mesh(b.build(), materials.cuticle);
    const hn = rig.joint(thorax, `Haltere.${s > 0 ? 'L' : 'R'}`,
      v3([c.centre[0], c.centre[1], s * c.centre[2]]), new THREE.Quaternion());
    m.position.set(0, 0, 0);
    m.rotation.z = -Math.PI * 0.5;
    hn.add(m);
  }

  uniquifyNames(root);
  return {
    root, rig, body, thorax, head, prob, abdomen, legs, wings,
    nodes: { headCentre, thoraxCentre: new THREE.Vector3(0.30, 0, 0) },
  };
}

/**
 * Guarantee that every object in the rig has a unique, non-empty name.
 *
 * A glTF animation track is addressed by object name, so a rig with two nodes
 * called `femur` exports as a file whose knee animation silently drives the
 * wrong leg. Naming the joints hierarchically in `Rig.joint` gets most of the
 * way; this sweeps up the rest (shared mesh labels, unnamed sclerites).
 */
function uniquifyNames(root) {
  const used = new Set();
  const walk = (o, fallback) => {
    let base = o.name && o.name.trim() ? o.name : fallback;
    let name = base, n = 2;
    while (used.has(name)) name = `${base}.${n++}`;
    used.add(name);
    o.name = name;
    const tag = o.isMesh ? 'mesh' : 'node';
    o.children.forEach((c, i) => walk(c, `${name}.${tag}${i}`));
  };
  walk(root, 'Mosquito');
}

/* =====================================================================
 *  THORAX DETAIL
 * =================================================================== */

function buildThoraxDetail(rig, thorax, materials, rnd) {
  const T = BODY.thorax;
  const s = T.scutum;

  // Main thoracic mass: strongly arched dorsally, with the mesonotum humping
  // over the wing bases. This hump is what carries the flight muscles.
  const core = blobGeometry({
    size: [s.length * 0.5, s.height * 0.92, s.width * 0.52],
    exponent: 0.72, nu: 34, nv: 46,
    radiusMod: (u, v) => {
      const th = v * Math.PI * 2;
      const dorsal = 1 + 0.10 * Math.max(0, Math.sin(th)) * Math.max(0, (0.55 - u) * 1.7);
      const ventral = 1 - 0.14 * Math.max(0, -Math.sin(th));
      // Pronotal lobe pushing forward over the head
      const pro = 1 + 0.10 * Math.max(0, Math.cos(th)) * Math.max(0, (0.30 - u) * 2.2);
      return dorsal * ventral * pro;
    },
    centre: [0, 0, 0],
  });
  const coreMesh = new THREE.Mesh(core.build(), materials.cuticle);
  coreMesh.name = 'ThoraxCore';
  thorax.add(coreMesh);

  // Scutum — the patterned dorsal shield that carries the lyre marking (S14).
  {
    const b = new MeshBuilder();
    b.surface((u, v, out) => {
      // u: anterior→posterior over the dorsum, v: left→right
      const a = (u - 0.5) * Math.PI * 0.95;
      const w = v * Math.PI * 2;
      const rx = s.length * 0.50, ry = s.height * 0.93, rz = s.width * 0.53;
      const k = 0.74;
      const sg = (t, e) => Math.sign(t) * Math.pow(Math.abs(t), e);
      out.set(
        rx * sg(Math.sin(a), k) * sg(Math.cos(w), k),
        ry * sg(Math.cos(a), k) * 1.02,
        rz * sg(Math.sin(a), k) * sg(Math.sin(w), k));
      // Only the dorsal cap, lifted a hair above the core
      out.y += 0.004;
    }, 26, 34, { uv: (u, v) => [u * 1.0, v * 0.62] });
    const m = new THREE.Mesh(b.build(), materials.scutum);
    m.name = 'Scutum';
    thorax.add(m);
  }

  // Pleurosternal walls — the paired sclerites that carry the coxa and give
  // the thorax its boxy insect profile.
  for (const sd of [1, -1]) {
    const p = T.pleuron;
    const b = new MeshBuilder();
    b.surface((u, v, out) => {
      const x = (u - 0.5) * p.length;
      const yc = (v - 0.5) * p.height;
      const bulge = Math.sin(u * Math.PI) * 0.035;
      out.set(x, yc + 0.02, sd * (s.width * 0.50 + bulge * 0.5 + Math.sin(v * Math.PI) * 0.012));
    }, 16, 10, { uv: (u, v) => [u, v] });
    const m = new THREE.Mesh(b.build(), materials.cuticle);
    m.name = `Pleuron.${sd > 0 ? 'L' : 'R'}`;
    thorax.add(m);
  }

  // Scutellum — the small triangular lobe over the wing bases.
  {
    const b = blobGeometry({
      size: [T.scutellum.length * 0.5, T.scutellum.height, T.scutellum.width * 0.5],
      exponent: 0.6, nu: 12, nv: 18,
      centre: T.scutellum.centre.map((x) => x - 0.30),
    });
    const m = new THREE.Mesh(b.build(), materials.paleScale);
    m.name = 'Scutellum';
    thorax.add(m);
  }

  // Bristles: the "bristle hairs" that cover the dorsum of the thorax (S15).
  const bristle = new MeshBuilder();
  for (let i = 0; i < 190; i++) {
    const a = rnd() * Math.PI * 2;
    const t = rnd();
    const r = 0.5 - t * 0.25;
    const x = (rnd() - 0.5) * s.length * 0.86;
    const y = Math.cos(a * 0.5) * 0 + (0.30 - Math.abs(x) / s.length * 0.45) * 0.9;
    const dir = new THREE.Vector3((rnd() - 0.5) * 0.5, 0.85 + rnd() * 0.4, (rnd() - 0.5) * 0.9).normalize();
    const len = 0.09 + rnd() * 0.16;
    bristle.hair(new THREE.Vector3(x, y, (rnd() - 0.5) * s.width * 0.8), dir, len, 0.0055, { seg: 2, radial: 3, bend: (rnd() - 0.5) * 0.4 });
  }
  const bm = new THREE.Mesh(bristle.build(), materials.seta);
  bm.name = 'ThoracicSetae';
  thorax.add(bm);
}

/* =====================================================================
 *  ANTENNA
 * =================================================================== */

function buildAntenna(rig, head, side, HEAD, materials, rnd) {
  const A = HEAD.antenna;
  let p = new THREE.Vector3(0.16, 0.13, side * 0.115);
  let dir = new THREE.Vector3(0.72, 0.30, side * 0.62).normalize();
  const parent = head;
  const joints = [];
  const builders = [];

  // Scape
  const scapeB = new MeshBuilder();
  {
    const pts = resample([new THREE.Vector3(), dir.clone().multiplyScalar(A.scape)], 7);
    scapeB.tube(pts, (i, t) => 0.040 * (1 - t * 0.30), { radial: 8, flatten: 0.92 });
  }
  const scapeNode = rig.chain(parent, `Antenna.${side > 0 ? 'L' : 'R'}.scape`, p, dir);
  scapeNode.add(new THREE.Mesh(scapeB.build(), materials.cuticle));
  p = p.clone().addScaledVector(dir, A.scape);

  // Pedicel
  const ped = new MeshBuilder();
  {
    const pts = resample([new THREE.Vector3(), dir.clone().multiplyScalar(A.pendicel ?? A.pedicel)], 5);
    ped.tube(pts, () => 0.036, { radial: 8 });
  }
  const pedNode = rig.chain(scapeNode, 'pedicel', new THREE.Vector3(), Y);
  pedNode.add(new THREE.Mesh(ped.build(), materials.cuticle));

  // Flagellum: 12 tori of decreasing radius, gently curving forward then down.
  let cur = p.clone().addScaledVector(dir, A.pedicel);
  let cd = dir.clone();
  for (let i = 0; i < A.flagellomeres; i++) {
    const t = i / (A.flagellomeres - 1);
    const len = (A.flagellum / A.flagellomeres) * (1 - t * 0.18);
    // Antennal curvature: the flagellum arcs forward then droops.
    cd.applyAxisAngle(new THREE.Vector3(side, 0, 0), -0.055);
    cd.y -= 0.010 + t * 0.006;
    cd.normalize();
    const node = rig.chain(pedNode, `flag${i}`, cur.clone().sub(p.clone()).add(new THREE.Vector3(0, 0, 0)), Y);
    node.position.copy(new THREE.Vector3(0, i === 0 ? 0 : 0, 0));
    // The chain helper already positioned the first node; for the rest we
    // advance the local +Y origin along the parent.
    const b = new MeshBuilder();
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, len, 0)], 5);
    const r0 = 0.034 * (1 - t * 0.45);
    b.tube(pts, (k, kt) => r0 * (1 + 0.22 * Math.sin(kt * Math.PI) - kt * 0.15), { radial: 7, flatten: 0.9 });
    // Sparse hairs — female mosquitoes have short, sparse antennal setae
    for (let h = 0; h < 3; h++) {
      const ht = 0.25 + rnd() * 0.7;
      const ang = rnd() * Math.PI * 2;
      b.hair(new THREE.Vector3(Math.cos(ang) * r0 * 0.8, len * ht, Math.sin(ang) * r0 * 0.8),
        new THREE.Vector3(Math.cos(ang), 0.15, Math.sin(ang)).normalize(), 0.035 + rnd() * 0.05, 0.0022, { seg: 2, radial: 3 });
    }
    node.add(new THREE.Mesh(b.build(), materials.cuticle));
    joints.push(node);
    builders.push(b);
    // Advance
    if (i === 0) { cur = p.clone().addScaledVector(dir, A.pedicel); }
    cur.addScaledVector(cd, len);
  }
  // Rebuild the flagellar chain properly (the loop above places each segment
  // relative to the running tip, which keeps the arc smooth).
  return { joints };
}

/* =====================================================================
 *  PALP
 * =================================================================== */

function buildPalp(rig, head, side, HEAD, PR, materials) {
  const A = HEAD.palp;
  const baseDir = new THREE.Vector3(0.90, -0.18, side * 0.20).normalize();
  let p = new THREE.Vector3(0.20, -0.10, side * 0.175);
  let dir = baseDir.clone();
  const rootNode = rig.chain(head, `Palp.${side > 0 ? 'L' : 'R'}.p0`, p, dir);
  let cur = rootNode;
  for (let i = 0; i < A.segments; i++) {
    const t = i / (A.segments - 1);
    const len = A.length / A.segments * (1 - t * 0.30);
    const b = new MeshBuilder();
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, len, 0)], 5);
    const r0 = A.radius * (1 - t * 0.42);
    b.tube(pts, (k, kt) => r0 * (1 + 0.30 * Math.sin(kt * Math.PI) - kt * 0.10), { radial: 8, flatten: 0.82 });
    if (i === 0) cur.add(new THREE.Mesh(b.build(), materials.cuticle));
    else cur.add(new THREE.Mesh(b.build(), materials.cuticle));
    const nxt = i === 0 ? rootNode : rig.joint(cur, `p${i + 1}`, new THREE.Vector3(0, len, 0), new THREE.Quaternion());
    if (i === 0) { /* p1 sits at the tip of p0 */ }
    cur = nxt;
    // Palps splay outward and downward; they are held in a shallow V
    dir.applyAxisAngle(new THREE.Vector3(0, 0, side), side * 0.07);
    cur.rotateZ(0);
  }
  // Pale terminal palpomeres — S: females have small palps tipped with
  // silver/white scales.
  const tip = new THREE.Mesh(new THREE.SphereGeometry(A.radius * 0.52, 10, 8), materials.paleScale);
  tip.position.y = A.length / A.segments * 0.5;
  tip.scale.set(1, 1.4, 1);
  cur.add(tip);
  return rootNode;
}

/* =====================================================================
 *  PROBOSCIS
 * =================================================================== */

function buildProboscis(rig, head, PR, materials, opts, rnd) {
  const S = PR.sheathRadius;
  const base = new THREE.Vector3(0.24, -0.13, 0);
  const dir = new THREE.Vector3(Math.cos(PR.baseAngle), -Math.sin(PR.baseAngle), 0);

  const rootNode = rig.chain(head, 'Proboscis', base, dir);
  const nSeg = 8;
  const segLen = PR.length / nSeg;

  // --- Labium sheath ------------------------------------------------
  // The labium is a grooved, scaly lower lip. Its cross-section is flattened
  // with a shallow dorsal groove that cradles the fascicle.
  const labiumJoints = [];
  let cur = rootNode;
  let p = new THREE.Vector3();
  let d = Y.clone();
  for (let i = 0; i < nSeg; i++) {
    const t = i / (nSeg - 1);
    const node = i === 0 ? rootNode : rig.joint(cur, `labium${i}`, new THREE.Vector3(0, segLen, 0), new THREE.Quaternion());
    const b = new MeshBuilder();
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, segLen, 0)], 6);
    const r0 = S * (1.06 - t * 0.30);
    b.tube(pts, (k, kt) => r0 * (1 + 0.14 * Math.sin(kt * Math.PI) - kt * 0.06), {
      radial: 14, flatten: 0.86, capEnd: false, uStart: i / nSeg, uEnd: (i + 1) / nSeg,
    });
    // Dorsal groove: two small raised lips flanking the fascicle groove
    for (const sgn of [1, -1]) {
      b.tube(resample([
        new THREE.Vector3(0, 0, sgn * r0 * 0.42),
        new THREE.Vector3(0, segLen * 0.5, sgn * r0 * 0.34),
        new THREE.Vector3(0, segLen, sgn * r0 * 0.30),
      ], 5), () => r0 * 0.22, { radial: 5, capEnd: false });
    }
    // Setae on the labium (short, dense — it is described as a "large scaly
    // outer lower lip", S2)
    for (let h = 0; h < 9; h++) {
      const ht = 0.1 + rnd() * 0.85, ang = (rnd() * 0.9 + 0.9) * Math.PI;
      b.hair(
        new THREE.Vector3(Math.cos(ang) * r0 * 0.8, segLen * ht, Math.sin(ang) * r0 * 0.6),
        new THREE.Vector3(Math.cos(ang), 0.25, Math.sin(ang) * 0.7).normalize(),
        0.020 + rnd() * 0.035, 0.0016, { seg: 2, radial: 3, bend: 0.3 });
    }
    const m = new THREE.Mesh(b.build(), materials.labium);
    m.name = `LabiumSeg${i}`;
    node.add(m);
    labiumJoints.push(node);
    cur = node;
  }

  // --- Labella: the paired sensory lobes at the tip (S1) --------------
  const tipNode = rig.joint(labiumJoints[nSeg - 1], 'labella', new THREE.Vector3(0, segLen, 0), new THREE.Quaternion());
  {
    const b = new MeshBuilder();
    for (const sgn of [1, -1]) {
      const lobe = blobGeometry({
        size: [PR.labella.radius * 1.25, PR.labella.length * 0.5, PR.labella.radius * 0.8],
        exponent: 0.85, nu: 10, nv: 14,
        centre: [0, PR.labella.length * 0.35, sgn * PR.labella.radius * 0.9 * Math.sin(PR.labella.spread)],
      });
      b.append(lobe);
    }
    tipNode.add(new THREE.Mesh(b.build(), materials.labiumTip));
  }

  // --- Fascicle: 6 stylets (S1/S2) ---------------------------------
  // labrum, hypopharynx, mandible L/R, maxilla L/R. Only the fascicle
  // penetrates; the labium retracts over it.
  const fascRoot = rig.joint(rootNode, 'Fascicle', new THREE.Vector3(0, 0, 0), new THREE.Quaternion());
  const stylets = [];
  for (const st of PR.stylets) {
    const node = rig.joint(fascRoot, `stylet.${st.id}`, new THREE.Vector3(0, 0, 0), new THREE.Quaternion());
    const b = new MeshBuilder();
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, PR.length, 0)], 10);
    // Labrum is the fat food canal; hypopharynx is the salivary duct; the
    // mandibles and maxillae are flat saw blades (S3: labrum tip 15° included).
    const isBlade = st.id.startsWith('mand') || st.id.startsWith('max');
    const r0 = st.id === 'labrum' ? PR.fascicleRadius * 1.7
      : st.id === 'hypopharynx' ? PR.fascicleRadius * 0.9
        : PR.fascicleRadius * 0.75;
    b.tube(pts, (k, kt) => {
      if (isBlade) return r0 * (1 - kt * 0.75) * (1 + 0.30 * Math.sin(kt * 9.0));
      return r0 * (1 - kt * 0.55);
    }, { radial: isBlade ? 4 : 8, flatten: isBlade ? 0.28 : 1.0, uRepeat: 6 });
    // Serrations on the maxillary saws (S1/S3: serrated blades near the tip)
    if (st.id.startsWith('max')) {
      for (let k = 0; k < 14; k++) {
        const kt = 0.62 + k * 0.026;
        b.hair(new THREE.Vector3(0, PR.length * kt, 0), new THREE.Vector3(0, 0.3, 1), 0.016, 0.0012, { seg: 1, radial: 3 });
      }
    }
    const m = new THREE.Mesh(b.build(), materials.stylet);
    m.name = `Stylet.${st.id}`;
    // Lay the stylet out around the fascicle axis
    const ang = st.phase + st.r * Math.PI;
    const off = new THREE.Vector3(Math.cos(ang) * PR.fascicleRadius * 1.1, 0, Math.sin(ang) * PR.fascicleRadius * 1.1);
    m.position.copy(off);
    node.add(m);
    stylets.push({ id: st.id, node, mesh: m, phase: st.phase, offset: off, isBlade });
  }

  // --- Cibarial pump: the bulb at the base of the food canal ---------
  const cib = blobGeometry({ size: [0.075, 0.085, 0.075], exponent: 0.85, nu: 12, nv: 16, centre: [0, 0.06, 0] });
  const cibMesh = new THREE.Mesh(cib.build(), materials.membrane);
  cibMesh.name = 'CibarialPump';
  rootNode.add(cibMesh);

  return { root: rootNode, labiumJoints, labella: tipNode, fascRoot, stylets, segLen, dir, base };
}

/* =====================================================================
 *  ABDOMEN
 * =================================================================== */

function buildAbdomen(rig, thorax, A, materials, rnd) {
  const joints = [];
  const meshes = [];
  const start = v3(A.start);
  const n = A.segments;
  const segLen = A.length / n;
  // Rest curvature: the abdomen droops slightly, then the last tergites curl
  // up a touch (a fed female's abdomen hangs down; a resting one is gently arched).
  const droop = 0.085;

  let parent = thorax;
  let p = start.clone();
  for (let i = 0; i < n; i++) {
    const t = i / n;
    const tNext = (i + 1) / n;
    const dir = new THREE.Vector3(-1, -droop * Math.sin((0.15 + t * 0.95) * Math.PI * 0.9), 0).normalize();
    const node = i === 0
      ? rig.chain(parent, `abd0`, p, dir)
      : rig.joint(parent, `abd${i}`, new THREE.Vector3(0, segLen, 0), new THREE.Quaternion());
    if (i > 0) node.rotateZ(0);

    const r0 = profileAt(A.profile, t);
    const r1 = profileAt(A.profile, Math.min(1, tNext));
    const b = new MeshBuilder();
    // Tergite shell, slightly flattened (dorsoventrally compressed)
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, segLen * 1.06, 0)], 7);
    b.tube(pts, (k, kt) => r0 * (1 - kt * 0.30) + r1 * kt * 0.30, {
      radial: 20, flatten: 0.88, capStart: i > 0, capEnd: i === n - 1, uStart: t, uEnd: tNext,
    });
    // Scutellar suture ring at the posterior margin of each tergite
    b.tube(resample([new THREE.Vector3(0, segLen * 0.93, 0), new THREE.Vector3(0, segLen * 1.02, 0)], 3),
      () => (r0 * 0.7 + r1 * 0.3) * 1.04, { radial: 20, flatten: 0.88, capEnd: false });
    // Dorsal setae fringe
    for (let h = 0; h < 26; h++) {
      const ht = 0.05 + rnd() * 0.9, ang = (rnd() - 0.5) * 1.5;
      const rr = r0 * (1 - ht * 0.3);
      b.hair(new THREE.Vector3(Math.cos(ang) * rr * 0.7, segLen * ht, Math.sin(ang) * rr),
        new THREE.Vector3(Math.cos(ang) * 0.5, 0.3, Math.sin(ang)).normalize(),
        0.022 + rnd() * 0.035, 0.0016, { seg: 2, radial: 3, bend: 0.4 });
    }
    const m = new THREE.Mesh(b.build(), materials.abdomen);
    m.name = `Tergite${i}`;
    m.userData.t = t;
    node.add(m);
    joints.push(node); meshes.push(m);

    // Advance the chain: the next joint sits at the tip of this segment.
    const worldTip = node.localToWorld(new THREE.Vector3(0, segLen, 0));
    parent = node;
    p = worldTip;
  }

  // Cerci
  const cerciRoot = joints[n - 1];
  for (const s of [1, -1]) {
    const b = new MeshBuilder();
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, A.cerci.length * 0.5, s * A.cerci.length * 0.35), new THREE.Vector3(0, A.cerci.length, s * A.cerci.length * 0.6)], 6);
    b.tube(pts, (k, kt) => A.cerci.radius * (1 - kt * 0.7), { radial: 6 });
    const m = new THREE.Mesh(b.build(), materials.paleScale);
    m.name = 'Cerci';
    cerciRoot.add(m);
  }

  return { joints, meshes, segLen };
}

/* =====================================================================
 *  LEG
 * =================================================================== */

function buildLeg(rig, thorax, L, T, materials, rnd) {
  const coxa = T.coxa;
  const s = L.side;
  const rootPos = new THREE.Vector3(coxa[L.coxaKey][0], coxa[L.coxaKey][1], s * coxa[L.coxaKey][2]);

  const coxaDir = coxaRestDir(L);

  const coxaNode = rig.chain(thorax, `Leg_${L.key}`, rootPos, coxaDir);
  const total = L.coxa + L.trochanter + L.femur + L.tibia + L.tarsus.reduce((a, b) => a + b, 0) + L.claw;
  const uOf = (len, offset) => (offset + len) / total;
  let uCursor = 0;

  // --- Coxa ---------------------------------------------------------
  {
    const b = new MeshBuilder();
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, L.coxa, 0)], 5);
    b.tube(pts, (k, kt) => coxa.radius * (1.05 - kt * 0.12), { radial: 12, uStart: uCursor, uEnd: uOf(L.coxa, uCursor) });
    // Coxal setae
    for (let h = 0; h < 10; h++) {
      const ht = rnd(), ang = rnd() * Math.PI * 2;
      b.hair(new THREE.Vector3(Math.cos(ang) * coxa.radius, L.coxa * ht, Math.sin(ang) * coxa.radius),
        new THREE.Vector3(Math.cos(ang), 0.4, Math.sin(ang)).normalize(), 0.05, 0.0025, { seg: 2, radial: 3 });
    }
    const m = new THREE.Mesh(b.build(), materials.leg);
    m.name = 'Coxa';
    coxaNode.add(m);
  }
  // The hip joint sits at the tip of the coxa and carries the trochanter.
  const femurNode = rig.joint(coxaNode, 'hip', new THREE.Vector3(0, L.coxa, 0), new THREE.Quaternion());
  uCursor = uOf(L.coxa, uCursor);

  // --- Trochanter ---------------------------------------------------
  {
    const b = new MeshBuilder();
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, L.trochanter, 0)], 4);
    b.tube(pts, (k, kt) => L.femurRadius[0] * (0.86 - kt * 0.05), { radial: 10, uStart: uCursor, uEnd: uOf(L.trochanter, uCursor) });
    femurNode.add(new THREE.Mesh(b.build(), materials.leg));
  }
  const tibiaNode = rig.joint(femurNode, 'tibia', new THREE.Vector3(0, L.trochanter, 0), new THREE.Quaternion());
  uCursor = uOf(L.trochanter, uCursor);

  // --- Femur: long, tapered, slightly flattened ----------------------
  {
    const b = new MeshBuilder();
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, L.femur, 0)], 12);
    const r0 = L.femurRadius[0], r1 = L.femurRadius[1];
    b.tube(pts, (k, kt) => r0 + (r1 - r0) * Math.pow(kt, 0.72), {
      radial: 12, flatten: 0.86, capStart: false, uStart: uCursor, uEnd: uOf(L.femur, uCursor),
    });
    // S15: fore/mid femora carry a longitudinal pale line (handled in the
    // texture); here we add the real structural setae and the tibial spur.
    for (let h = 0; h < 46; h++) {
      const ht = 0.04 + rnd() * 0.94, ang = rnd() * Math.PI * 2;
      const rr = r0 + (r1 - r0) * Math.pow(ht, 0.72);
      b.hair(new THREE.Vector3(Math.cos(ang) * rr * 0.92, L.femur * ht, Math.sin(ang) * rr * 0.78),
        new THREE.Vector3(Math.cos(ang), 0.12, Math.sin(ang) * 0.85).normalize(),
        0.045 + rnd() * 0.075, 0.0024, { seg: 2, radial: 3, bend: 0.35 });
    }
    const m = new THREE.Mesh(b.build(), materials.leg);
    m.name = 'Femur';
    tibiaNode.add(m);
  }
  // The tibia joint sits at the distal femur and carries the tibia.
  const tarsus1Node = rig.joint(tibiaNode, 'knee', new THREE.Vector3(0, L.femur, 0), new THREE.Quaternion());
  uCursor = uOf(L.femur, uCursor);

  // --- Tibia: longer, slimmer, with the tibial spur pair -------------
  {
    const b = new MeshBuilder();
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, L.tibia, 0)], 14);
    const r0 = L.femurRadius[1] * 1.02, r1 = L.tibiaRadius[1];
    b.tube(pts, (k, kt) => r0 + (r1 - r0) * Math.pow(kt, 0.8), {
      radial: 12, flatten: 0.9, uStart: uCursor, uEnd: uOf(L.tibia, uCursor),
    });
    // Paired apical spurs — a real dipteran character
    for (const sgn of [1, -1]) {
      b.tube(resample([
        new THREE.Vector3(sgn * r0 * 0.55, L.tibia * 0.955, 0),
        new THREE.Vector3(sgn * r0 * 0.75, L.tibia * 1.06, 0),
      ], 4), (k, kt) => r0 * 0.22 * (1 - kt * 0.75), { radial: 6, capEnd: true });
    }
    for (let h = 0; h < 70; h++) {
      const ht = 0.02 + rnd() * 0.97, ang = rnd() * Math.PI * 2;
      const rr = r0 + (r1 - r0) * Math.pow(ht, 0.8);
      b.hair(new THREE.Vector3(Math.cos(ang) * rr * 0.9, L.tibia * ht, Math.sin(ang) * rr * 0.8),
        new THREE.Vector3(Math.cos(ang), 0.10, Math.sin(ang) * 0.9).normalize(),
        0.04 + rnd() * 0.08, 0.0021, { seg: 2, radial: 3, bend: 0.4 });
    }
    const m = new THREE.Mesh(b.build(), materials.leg);
    m.name = 'Tibia';
    tarsus1Node.add(m);
  }

  // --- Tarsus: 5 tarsomeres, each its own joint ----------------------
  // The first tarsomere hangs from the ankle, which is the distal tibia, so
  // it needs a joint of its own rather than sharing the tibia's.
  const tarsusJoints = [];
  let parent = rig.joint(tarsus1Node, 'ankle', new THREE.Vector3(0, L.tibia, 0), new THREE.Quaternion());
  for (let i = 0; i < L.tarsus.length; i++) {
    const len = L.tarsus[i];
    const node = i === 0 ? parent : rig.joint(parent, `t${i}`, new THREE.Vector3(0, L.tarsus[i - 1], 0), new THREE.Quaternion());
    const b = new MeshBuilder();
    const r0 = L.tibiaRadius[1] * (1 - i * 0.05), r1 = r0 * 0.88;
    const pts = resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, len, 0)], 5);
    b.tube(pts, (k, kt) => r0 * (1 - kt * 0.14), { radial: 9, flatten: 0.9, uStart: uCursor, uEnd: uOf(len, uCursor) });
    for (let h = 0; h < 12; h++) {
      const ht = 0.1 + rnd() * 0.85, ang = rnd() * Math.PI * 2;
      b.hair(new THREE.Vector3(Math.cos(ang) * r0 * 0.9, len * ht, Math.sin(ang) * r0 * 0.8),
        new THREE.Vector3(Math.cos(ang), 0.1, Math.sin(ang)).normalize(),
        0.03 + rnd() * 0.05, 0.0018, { seg: 2, radial: 3 });
    }
    const m = new THREE.Mesh(b.build(), materials.leg);
    m.name = `Tarsomere${i + 1}`;
    node.add(m);
    tarsusJoints.push(node);
    uCursor = uOf(len, uCursor);
    parent = node;
  }

  // --- Pretarsus: claws, pulvilli, empodium (S13) -------------------
  const preNode = rig.joint(parent, 'pretarsus', new THREE.Vector3(0, L.tarsus[L.tarsus.length - 1], 0), new THREE.Quaternion());
  {
    const b = new MeshBuilder();
    const baseR = L.tibiaRadius[1] * 0.72;
    // Unguitractor plate
    b.tube(resample([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, baseR * 1.3, 0)], 3),
      (k, kt) => baseR * (1 - kt * 0.3), { radial: 8 });
    // Paired tarsal claws — hooked, with the basal tooth
    for (const sgn of [1, -1]) {
      const path = resample([
        new THREE.Vector3(0, baseR * 0.6, sgn * baseR * 0.42),
        new THREE.Vector3(0, baseR * 0.4, sgn * baseR * 1.05),
        new THREE.Vector3(0, -baseR * 0.55, sgn * baseR * 1.00),
        new THREE.Vector3(0, -baseR * 1.15, sgn * baseR * 0.42),
        new THREE.Vector3(0, -baseR * 1.05, sgn * baseR * 0.05),
      ], 10);
      b.tube(path, (k, kt) => baseR * 0.30 * Math.pow(1 - kt, 0.55) + baseR * 0.04, { radial: 6 });
    }
    // Paired pulvilli: the adhesive lobes between the claws (S13)
    for (const sgn of [1, -1]) {
      const pad = blobGeometry({
        size: [baseR * 0.95, baseR * 0.42, baseR * 0.60],
        exponent: 0.7, nu: 9, nv: 12,
        centre: [0, -baseR * 0.18, sgn * baseR * 0.52],
      });
      b.append(pad);
    }
    // Empodium: the median bristle between the pulvilli
    b.hair(new THREE.Vector3(0, -baseR * 0.2, 0), new THREE.Vector3(0, -1, 0), baseR * 1.5, baseR * 0.16, { seg: 2, radial: 4 });
    // Tenent setae on the pulvilli
    for (const sgn of [1, -1]) {
      for (let k = 0; k < 14; k++) {
        const ang = (rnd() - 0.5) * 2.4;
        b.hair(
          new THREE.Vector3(Math.cos(ang) * baseR * 0.5, -baseR * 0.3, sgn * baseR * (0.25 + rnd() * 0.4)),
          new THREE.Vector3(Math.cos(ang) * 0.6, -0.6, sgn * 0.5).normalize(),
          baseR * (0.5 + rnd() * 0.4), baseR * 0.055, { seg: 2, radial: 3 });
      }
    }
    const m = new THREE.Mesh(b.build(), materials.pretarsus);
    m.name = 'Pretarsus';
    preNode.add(m);
  }

  return {
    key: L.key, spec: L,
    // root  — coxa attachment on the thorax
    // hip   — coxa/trochanter joint; the trochanter runs hip→knee
    // knee  — distal femur; the femur runs hip→knee, this node IS the knee
    // tibia — distal femur node; the tibia runs knee→ankle
    // tarsus[i] — tarsomere i+1; the ankle is tarsus[0]
    root: coxaNode, hip: femurNode, femur: femurNode,
    knee: tibiaNode, tibia: tarsus1Node,
    tarsus: tarsusJoints, pretarsus: preNode,
    total,
  };
}

/* =====================================================================
 *  WING
 * =================================================================== */

function buildWing(rig, thorax, side, materials, rnd) {
  const W = BODY.thorax.wingRoot;
  const rootPos = new THREE.Vector3(W.pos[0], W.pos[1], side * W.pos[2]);
  const strokeNode = rig.joint(thorax, `Wing_${side > 0 ? 'L' : 'R'}_stroke`, rootPos, new THREE.Quaternion());
  // The wing hinge is elevated and swept back; the base is not level with
  // the body so the stroke plane can be tilted independently of the thorax.
  const pitchNode = rig.joint(strokeNode, 'pitch', new THREE.Vector3(0, 0, 0), new THREE.Quaternion());
  pitchNode.rotateY(side * 0.28);
  pitchNode.rotateX(-0.10);

  const membrane = new MeshBuilder();
  const veins = new MeshBuilder();
  const fringe = new MeshBuilder();
  const L = WING.length;

  // --- Membrane ------------------------------------------------------
  // u: span 0→1, v: chord 0 (leading) → 1 (trailing).
  const wingPoint = (u, v) => {
    const chord = wingChord(u);
    // Leading edge runs almost straight; the trailing edge sweeps back, and
    // near the base there is the alular notch (the S16 wing-length datum).
    const le = 0.30 - 0.10 * u;
    const z = (le - v * 0.86) * chord;
    const y = -0.055 * u * u * L                    // spanwise droop
      + 0.035 * Math.sin(u * Math.PI)             // gentle dorsal camber
      - 0.02 * Math.sin(v * Math.PI) * (1 - u);
    const notch = (u < 0.10 && v > 0.72) ? 0.55 * (0.10 - u) / 0.10 : 0;
    return new THREE.Vector3(u * L, y, z * (1 - notch * 0.5));
  };

  // The wing is closed in u (leading edge → trailing edge → back), so the
  // normal is sampled across the seam rather than clamped at it.
  membrane.surface((u, v, out) => { out.copy(wingPoint(u, v)); }, 64, 14, {
    uWrap: true, uv: (u, v) => [u, v],
  });
  // Build both sides by mirroring through the thickness offset is done in the
  // material (double-sided); we add a thin solid core for silhouette.
  const wingMesh = new THREE.Mesh(membrane.build(), materials.wing);
  wingMesh.name = 'WingMembrane';
  wingMesh.material.side = THREE.DoubleSide;
  pitchNode.add(wingMesh);

  // --- Venation -------------------------------------------------------
  for (const v of WING.veins) {
    const path = [];
    const N = 14;
    for (let i = 0; i <= N; i++) {
      const t = i / N;
      const uu = v.from[0] + (v.to[0] - v.from[0]) * t;
      const vv = v.from[1] + (v.to[1] - v.from[1]) * t;
      path.push(wingPoint(uu, vv).setY(wingPoint(uu, vv).y + 0.0022));
    }
    const pts = resample(path, 12);
    const w = v.w;
    veins.tube(pts, (k, kt) => w * (1 - kt * 0.55) * (v.costal ? 1.0 : 0.85), { radial: v.costal ? 6 : 4, flatten: 0.55 });
  }
  // Costal "bristle" reinforcement along the whole leading edge
  {
    const path = [];
    for (let i = 0; i <= 40; i++) path.push(wingPoint(i / 40, 0));
    veins.tube(resample(path, 40), (k, kt) => 0.014 * (1 - Math.pow(Math.abs(kt - 0.5) * 2, 3) * 0.4), { radial: 5, flatten: 0.7 });
  }
  const veinMesh = new THREE.Mesh(veins.build(), materials.vein);
  veinMesh.name = 'WingVeins';
  pitchNode.add(veinMesh);

  // --- Fringe scales at the trailing edge ----------------------------
  for (let i = 0; i < 240; i++) {
    const u = 0.04 + rnd() * 0.95;
    const v = 0.92 + rnd() * 0.10;
    const p = wingPoint(u, Math.min(1, v));
    const dir = new THREE.Vector3(0, 0, -1).applyAxisAngle(Y, (rnd() - 0.5) * 0.7);
    fringe.hair(p.clone().setY(p.y - 0.001), dir, 0.035 + rnd() * 0.055, 0.0035, { seg: 2, radial: 3, bend: 0.5 });
  }
  const fringeMesh = new THREE.Mesh(fringe.build(), materials.seta);
  fringeMesh.name = 'WingFringe';
  pitchNode.add(fringeMesh);

  // --- Alula: the small lobe at the wing base ------------------------
  {
    const b = new MeshBuilder();
    b.surface((u, v, out) => {
      const a = u * 0.9, w = v * Math.PI * 2;
      out.set(
        Math.sin(a) * 0.30,
        Math.sin(w) * 0.012 * Math.sin(a),
        (0.03 + Math.sin(a) * 0.13) * Math.cos(w) - 0.02);
    }, 8, 12, {});
    const m = new THREE.Mesh(b.build(), materials.wing);
    m.name = 'Alula';
    m.position.set(0.02, 0, -0.03);
    pitchNode.add(m);
  }

  return { stroke: strokeNode, pitch: pitchNode, side, membrane: wingMesh };
}
