import * as THREE from 'three';

// =====================================================================
// *Tyrannosaurus rex* — measured 1:1 skeletal reconstruction.
// Units are meters. Forward = +Z, up = +Y.
//
// Measured reference table (Sue FMNH PR2081 / Hartman USNM 555000 ~11.6 m,
// scaled to a ~10 m adult here):
//   skull length ............ 1.46 | femur ................. 1.32
//   hip height .............. 3.02 | tibia ................. 1.16
//   dorsal series ........... ~1.9 | metatarsus III ........ 0.68
//   tail .................... ~4.7 | pedal formula ......... 3-4-5 + claws
//   cervicals 10 / dorsals 13 / sacrals 5 / caudals 40+
// =====================================================================

export const LEG = { L1: 1.32, L2: 1.16, LMT: 0.68 };   // femur, tibia, metatarsus
export const TAIL_LENS = [0.72, 0.68, 0.64, 0.6, 0.56, 0.52, 0.47, 0.42];
export const PELVIS_Y = 3.02;
export const HIP_LOCAL = { x: 0.44, y: -0.05, z: 0 };

export function buildRex() {
  const J = {};
  const tailSegs = [];
  const labelSpots = [];
  const hydroDefs = [];

  const mats = {
    bone:     new THREE.MeshStandardMaterial({ color: 0xdcc9a1, roughness: 0.6, metalness: 0.05 }),
    boneDark: new THREE.MeshStandardMaterial({ color: 0xb09468, roughness: 0.7, metalness: 0.05 }),
    tooth:    new THREE.MeshStandardMaterial({ color: 0xf3ecd9, roughness: 0.3 }),
    metal:    new THREE.MeshStandardMaterial({ color: 0x46536b, roughness: 0.35, metalness: 0.9 }),
    darkMetal:new THREE.MeshStandardMaterial({ color: 0x1c2330, roughness: 0.5, metalness: 0.85 }),
    chrome:   new THREE.MeshStandardMaterial({ color: 0xc7d2e4, roughness: 0.15, metalness: 1.0 }),
    socket:   new THREE.MeshStandardMaterial({ color: 0x07090d, roughness: 0.9 }),
    eye:      new THREE.MeshStandardMaterial({ color: 0x2a1500, emissive: 0xffaa22, emissiveIntensity: 0.9 }),
    core:     new THREE.MeshStandardMaterial({ color: 0x062a33, emissive: 0x28e6ff, emissiveIntensity: 0.8 }),
    sleeve:   new THREE.MeshStandardMaterial({ color: 0x3d434c, roughness: 0.42, metalness: 0.85 }),
    collar:   new THREE.MeshStandardMaterial({ color: 0x22262c, roughness: 0.35, metalness: 0.9 }),
    led:      new THREE.MeshStandardMaterial({ color: 0x1a0a00, emissive: 0xffb454, emissiveIntensity: 1.6 }),
  };

  const root = new THREE.Group();
  root.name = 'trex';

  // ---------- helpers ----------
  const Jt = (name, parent, x, y, z) => {
    const g = new THREE.Group();
    g.name = name;
    g.position.set(x, y, z);
    parent.add(g);
    J[name] = g;
    return g;
  };
  const M = (geo, mat, parent, x = 0, y = 0, z = 0) => {
    const m = new THREE.Mesh(geo, mat);
    m.position.set(x, y, z);
    m.castShadow = true;
    parent.add(m);
    return m;
  };
  const ball = (r, mat, parent, x, y, z, sx = 1, sy = 1, sz = 1) => {
    const m = M(new THREE.SphereGeometry(r, 14, 12), mat, parent, x, y, z);
    m.scale.set(sx, sy, sz);
    return m;
  };
  const box = (w, h, d, mat, parent, x, y, z) => M(new THREE.BoxGeometry(w, h, d), mat, parent, x, y, z);
  const cyl = (rt, rb, h, mat, parent, x, y, z, seg = 12) => M(new THREE.CylinderGeometry(rt, rb, h, seg), mat, parent, x, y, z);
  const cone = (r, h, mat, parent, x, y, z, seg = 10) => M(new THREE.ConeGeometry(r, h, seg), mat, parent, x, y, z);
  const _up = new THREE.Vector3(0, 1, 0);
  const strut = (parent, ax, ay, az, bx, by, bz, r, mat) => {
    const a = new THREE.Vector3(ax, ay, az), b = new THREE.Vector3(bx, by, bz);
    const dir = b.clone().sub(a);
    const len = dir.length();
    const m = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.75, r * 0.9, len, 10), mat);
    m.position.copy(a).addScaledVector(dir, 0.5);
    m.quaternion.setFromUnitVectors(_up, dir.normalize());
    m.castShadow = true;
    parent.add(m);
    return m;
  };
  const tag = (obj, bone, desc) => {
    obj.traverse((o) => { if (o.isMesh) o.userData.info = { bone, desc }; });
  };
  const grp = (parent, x = 0, y = 0, z = 0) => {
    const g = new THREE.Group();
    g.position.set(x, y, z);
    parent.add(g);
    return g;
  };
  const label = (obj, at, text) => labelSpots.push({ obj, at, text });
  const hydro = (aObj, a, bObj, b, r, group) =>
    hydroDefs.push({ a: { obj: aObj, at: a }, b: { obj: bObj, at: b }, r, group });

  // Flat bone plate from a lateral-profile polygon.
  // pts: [[forward(+Z), up(+Y)], ...]. True silhouette edges — gaps between
  // plates form the real fenestrae (antorbital, orbit, temporal).
  const plate = (pts, thickness, mat, parent, x, y, z) => {
    const sh = new THREE.Shape();
    sh.moveTo(pts[0][0], pts[0][1]);
    for (let i = 1; i < pts.length; i++) sh.lineTo(pts[i][0], pts[i][1]);
    const geo = new THREE.ExtrudeGeometry(sh, {
      depth: thickness, bevelEnabled: true, bevelThickness: 0.008, bevelSize: 0.008, bevelSegments: 1,
    });
    geo.rotateY(-Math.PI / 2); // shapeX -> world +Z, extrude -> world X
    geo.translate(thickness / 2, 0, 0);
    const m = new THREE.Mesh(geo, mat);
    m.position.set(x, y, z);
    m.castShadow = true;
    parent.add(m);
    return m;
  };

  // Spool-shaped vertebral centrum (lathe-turned), axis along Z.
  const centrum = (r, len, mat, parent, x, y, z) => {
    const prof = [[0.8, -0.5], [1.0, -0.4], [0.97, -0.28], [0.84, -0.1], [0.82, 0],
      [0.84, 0.1], [0.97, 0.28], [1.0, 0.4], [0.8, 0.5]];
    const pts = prof.map(([k, t]) => new THREE.Vector2(Math.max(0.001, r * k), len * t));
    const geo = new THREE.LatheGeometry(pts, 14);
    geo.rotateX(Math.PI / 2);
    return M(geo, mat, parent, x, y, z);
  };

  // Curved rib as a swept tube: head -> out -> down -> in.
  const rib = (parent, sx, z, size, mat) => {
    const s = size;
    const curve = new THREE.CatmullRomCurve3([
      new THREE.Vector3(sx * 0.28 * s, 0.32 * s, z),
      new THREE.Vector3(sx * 0.78 * s, 0.12 * s, z + 0.04),
      new THREE.Vector3(sx * 0.82 * s, -0.38 * s, z + 0.1),
      new THREE.Vector3(sx * 0.5 * s, -0.78 * s, z + 0.16),
      new THREE.Vector3(sx * 0.24 * s, -0.95 * s, z + 0.2),
    ]);
    const m = new THREE.Mesh(new THREE.TubeGeometry(curve, 16, 0.032 * s, 8), mat);
    m.castShadow = true;
    parent.add(m);
    ball(0.05 * s, mat, parent, sx * 0.28 * s, 0.32 * s, z); // rib head
    return m;
  };

  // Tooth row: upper (hanging) or lower (standing), varying heights.
  const toothRow = (parent, z0, z1, n, x, yBase, hMax, mat, up) => {
    for (let i = 0; i < n; i++) {
      const f = i / (n - 1);
      const h = hMax * (0.55 + 0.45 * Math.sin(Math.PI * Math.min(1, 0.15 + 0.85 * f)));
      const z = z0 + (z1 - z0) * f;
      const t = cone(0.03 * (0.7 + h / hMax * 0.5), h, mat, parent, x, up ? yBase + h / 2 : yBase - h / 2, z);
      if (!up) t.rotation.x = Math.PI;
      t.rotation.z = 0.06; // slight lean
    }
  };

  // ================= PELVIS =================
  const pelvis = Jt('pelvis', root, 0, PELVIS_Y, 0);

  { // sacrum: 5 fused centra + fused spine + sacral ribs
    const g = grp(pelvis, 0, 0.15, -0.35);
    for (let i = 0; i < 5; i++) {
      const z = -0.32 + i * 0.16;
      const c = cyl(0.17, 0.17, 0.14, mats.bone, g, 0, 0, z, 14);
      c.rotation.x = Math.PI / 2;
      box(0.09, 0.3, 0.1, mats.boneDark, g, 0, 0.28, z);
      if (i < 4) {
        strut(g, -0.15, 0.02, z + 0.08, -0.44, 0.1, z + 0.08, 0.05, mats.boneDark);
        strut(g, 0.15, 0.02, z + 0.08, 0.44, 0.1, z + 0.08, 0.05, mats.boneDark);
      }
    }
    tag(g, 'Sacrum', 'Five fused vertebrae forming a rigid hip block, bolted to the ilia by sacral ribs.');
  }
  for (const s of [1, -1]) {
    { // ilium: long blade with peduncles (extruded profile)
      const g = grp(pelvis, s * 0.42, 0.32, -0.25);
      plate([
        [0.95, 0.1], [0.9, 0.3], [0.4, 0.38], [-0.3, 0.4], [-0.85, 0.3], [-0.95, 0.05],
        [-0.8, -0.12], [-0.35, -0.18], [-0.3, -0.34], [-0.12, -0.34], [-0.08, -0.16],
        [0.25, -0.16], [0.3, -0.34], [0.48, -0.34], [0.52, -0.14], [0.85, -0.1],
      ], 0.1, mats.bone, g, 0, 0, 0);
      tag(g, 'Ilium', 'Upper hip blade (~1.4 m) — anchored the colossal tail-driven leg muscles.');
    }
    ball(0.16, mats.socket, pelvis, s * 0.44, -0.05, -0.2); // acetabulum
    { // pubis + boot
      const g = grp(pelvis);
      strut(g, s * 0.4, -0.12, 0.05, s * 0.3, -0.9, 0.72, 0.07, mats.bone);
      const boot = box(0.2, 0.16, 0.52, mats.boneDark, g, s * 0.3, -0.93, 0.8);
      boot.rotation.x = 0.12;
      tag(g, 'Pubis', 'Forward-pointing hip bone ending in the expanded pubic boot.');
    }
    { // ischium + foot
      const g = grp(pelvis);
      strut(g, s * 0.4, -0.12, -0.4, s * 0.32, -0.82, -0.72, 0.075, mats.bone);
      box(0.2, 0.13, 0.34, mats.boneDark, g, s * 0.32, -0.84, -0.76);
      tag(g, 'Ischium', 'Rear hip bone with an expanded foot for tail-muscle attachment.');
    }
  }
  label(pelvis, [0.62, 0.55, -0.25], 'Ilium');
  label(pelvis, [0, 0.6, -0.35], 'Sacrum (5 fused)');

  // ================= SPINE / TORSO =================
  const spine = Jt('spine', pelvis, 0, 0.45, 1.15);

  { // dorsal vertebrae: 8 centra, tall neural spines, transverse processes
    const g = grp(spine);
    for (let i = 0; i < 8; i++) {
      const z = 0.0 + i * 0.135;
      centrum(0.19, 0.12, mats.bone, g, 0, 0.25, z);
      box(0.07, 0.4, 0.1, mats.boneDark, g, 0, 0.58, z);            // neural spine
      strut(g, -0.12, 0.32, z, -0.36, 0.38, z, 0.035, mats.boneDark);
      strut(g, 0.12, 0.32, z, 0.36, 0.38, z, 0.035, mats.boneDark);
      ball(0.045, mats.boneDark, g, 0, 0.42, z - 0.09);             // prezygapophysis
      ball(0.045, mats.boneDark, g, 0, 0.42, z + 0.09);             // postzygapophysis
    }
    tag(g, 'Dorsal vertebrae', 'Thirteen back vertebrae with tall, rough neural spines anchoring powerful back tendons.');
  }
  { // dorsal ribs: curved swept tubes + 2 floating pairs
    const g = grp(spine, 0, -0.05, 0);
    const sizes = [0.72, 0.86, 0.95, 1.0, 0.98, 0.9, 0.78, 0.62, 0.45];
    for (let i = 0; i < sizes.length; i++) {
      const z = -0.05 + i * 0.12;
      for (const s of [1, -1]) rib(g, s, z, sizes[i], mats.bone);
    }
    tag(g, 'Dorsal ribs', 'The barrel chest — curved ribs of a wide, deep torso housing huge lungs and air sacs.');
    label(spine, [0.9, -0.2, 0.3], 'Ribs');
  }
  { // gastralia
    const g = grp(spine);
    for (let i = 0; i < 6; i++) {
      const z = 0.05 + i * 0.13, y = -0.88 + i * 0.025;
      strut(g, -0.32, y + 0.07, z, 0, y - 0.03, z + 0.05, 0.02, mats.boneDark);
      strut(g, 0.32, y + 0.07, z, 0, y - 0.03, z + 0.05, 0.02, mats.boneDark);
    }
    tag(g, 'Gastralia', 'Eighteen to nineteen pairs of segmented belly ribs armoring the abdomen.');
    label(spine, [0, -1.05, 0.4], 'Gastralia');
  }
  { // furcula
    const g = grp(spine, 0, -0.5, 1.0);
    strut(g, -0.14, 0.1, 0, 0, -0.08, 0.04, 0.025, mats.boneDark);
    strut(g, 0.14, 0.1, 0, 0, -0.08, 0.04, 0.025, mats.boneDark);
    tag(g, 'Furcula', 'Wishbone — known in tyrannosaurids such as B-rex (MOR 1125).');
  }
  for (const s of [1, -1]) {
    { // scapula (long blade) + coracoid + glenoid
      const g = grp(spine);
      const sc = plate([
        [0.14, 0.5], [-0.1, 0.48], [-0.16, 0.0], [-0.13, -0.42], [0.02, -0.5], [0.12, -0.4], [0.1, 0.0],
      ], 0.06, mats.bone, g, s * 0.56, 0.5, 0.3);
      sc.rotation.x = -0.35; sc.rotation.z = s * -0.1;
      const co = cyl(0.17, 0.17, 0.07, mats.boneDark, g, s * 0.6, -0.02, 0.55);
      co.rotation.z = Math.PI / 2;
      ball(0.06, mats.socket, g, s * 0.62, 0.06, 0.55); // glenoid cavity
      tag(g, 'Scapula + coracoid', 'Shoulder girdle of a forelimb barely one meter long — yet strongly muscled.');
    }
  }
  label(spine, [0, 0.95, 0.4], 'Dorsals (13)');
  label(spine, [-0.7, 0.85, 0.25], 'Scapula');

  // hydraulic power core + accumulators
  {
    const g = grp(spine, 0, -0.35, 0.3);
    M(new THREE.TorusGeometry(0.2, 0.05, 10, 24), mats.metal, g, 0, 0, 0);
    M(new THREE.OctahedronGeometry(0.13), mats.core, g, 0, 0, 0.02);
    const coreLight = new THREE.PointLight(0x33d5ff, 1.0, 8, 1);
    coreLight.position.set(0, -0.2, 0.6);
    g.add(coreLight);
    for (const s of [1, -1]) {
      cyl(0.07, 0.07, 0.4, mats.chrome, g, s * 0.45, -0.1, -0.2);
      box(0.1, 0.12, 0.1, mats.darkMetal, g, s * 0.45, -0.32, -0.2);
    }
    tag(g, 'Hydraulic power core', 'Unit hardware: pressurized reservoir driving every piston on the skeleton.');
  }

  // ================= ARMS =================
  for (const s of [1, -1]) {
    const nm = s === 1 ? 'L' : 'R';
    const sh = Jt('sh' + nm, spine, s * 0.62, 0.05, 0.55);
    {
      const g = grp(sh);
      ball(0.065, mats.boneDark, g, 0, 0, 0);
      strut(g, 0, 0, 0, 0, -0.32, 0.02, 0.05, mats.bone);
      box(0.05, 0.13, 0.07, mats.boneDark, g, 0, -0.11, 0.07); // deltopectoral crest
      ball(0.05, mats.boneDark, g, 0.03, -0.32, 0.02);
      ball(0.05, mats.boneDark, g, -0.03, -0.32, 0.02);
      tag(g, 'Humerus', 'Short but robust upper arm with a large muscle crest.');
    }
    const el = Jt('el' + nm, sh, 0, -0.32, 0.02);
    {
      const g = grp(el);
      strut(g, -0.028, 0, 0, -0.028, -0.24, 0.01, 0.028, mats.bone);   // radius
      strut(g, 0.028, 0, -0.01, 0.034, -0.24, 0.0, 0.034, mats.boneDark); // ulna + olecranon
      ball(0.035, mats.boneDark, g, 0.028, 0.03, -0.02);
      ball(0.04, mats.boneDark, g, 0, -0.25, 0);
      for (const f of [-0.035, 0.035]) { // digits I–II: 2 phalanges + claw
        strut(g, f, -0.26, 0.01, f, -0.3, 0.045, 0.018, mats.bone);
        strut(g, f, -0.3, 0.045, f, -0.32, 0.075, 0.015, mats.bone);
        const cl = cone(0.018, 0.08, mats.tooth, g, f, -0.325, 0.11);
        cl.rotation.x = Math.PI / 2 + 0.35;
      }
      tag(g, 'Manus (digits I–II)', 'Two-fingered hand tipped with claws — all that remains of the theropod hand.');
    }
    hydro(spine, [s * 0.58, 0.3, 0.35], sh, [0, -0.2, 0.0], 0.026, 'arm');
    hydro(sh, [0, -0.08, 0.05], el, [0, -0.14, 0.02], 0.02, 'arm');
  }
  label(J.shL, [0.12, -0.2, 0], 'Humerus');

  // ================= NECK (10 cervicals, S-curve) =================
  const neck1 = Jt('neck1', spine, 0, 0.35, 1.05);
  {
    const g = grp(neck1);
    const cents = [[0.05, 0.08], [0.14, 0.24], [0.23, 0.39], [0.3, 0.52], [0.35, 0.62]];
    for (const [y, z] of cents) {
      centrum(0.155, 0.13, mats.bone, g, 0, y, z);
      ball(0.09, mats.boneDark, g, 0, y, z + 0.07, 1, 1, 0.7);
      box(0.06, 0.15, 0.09, mats.boneDark, g, 0, y + 0.2, z);
      ball(0.04, mats.boneDark, g, -0.1, y + 0.1, z + 0.08);
      ball(0.04, mats.boneDark, g, 0.1, y + 0.1, z + 0.08);
      // overlapping cervical ribs sweeping back-down
      const ribCurve = (sx) => {
        const c = new THREE.CatmullRomCurve3([
          new THREE.Vector3(sx * 0.12, y - 0.04, z),
          new THREE.Vector3(sx * 0.16, y - 0.12, z - 0.18),
          new THREE.Vector3(sx * 0.14, y - 0.16, z - 0.36),
        ]);
        const m = new THREE.Mesh(new THREE.TubeGeometry(c, 8, 0.02, 6), mats.boneDark);
        m.castShadow = true;
        g.add(m);
      };
      ribCurve(1); ribCurve(-1);
    }
    tag(g, 'Cervical vertebrae', 'Ten neck vertebrae in an S-curve — short, deep and muscular to carry the massive head.');
  }
  label(neck1, [0, 0.5, 0.35], 'Cervicals (10)');
  for (const s of [1, -1]) {
    hydro(spine, [s * 0.4, 0.55, 0.95], neck1, [s * 0.14, 0.3, 0.3], 0.04, 'neck');
  }
  const neck2 = Jt('neck2', neck1, 0, 0.38, 0.66);
  {
    const g = grp(neck2);
    const cents = [[0.03, 0.05], [0.1, 0.19], [0.16, 0.32]];
    for (const [y, z] of cents) {
      centrum(0.145, 0.12, mats.bone, g, 0, y, z);
      ball(0.085, mats.boneDark, g, 0, y, z + 0.065, 1, 1, 0.7);
      box(0.055, 0.14, 0.08, mats.boneDark, g, 0, y + 0.19, z);
    }
    const at = M(new THREE.TorusGeometry(0.1, 0.038, 8, 16), mats.boneDark, g, 0, 0.2, 0.44); // atlas ring
    box(0.09, 0.09, 0.12, mats.bone, g, 0, 0.2, 0.38);   // axis (short)
    cone(0.035, 0.08, mats.boneDark, g, 0, 0.2, 0.47).rotation.x = Math.PI / 2; // dens
    box(0.05, 0.17, 0.07, mats.boneDark, g, 0, 0.3, 0.37); // axis spine
    tag(g, 'Atlas + axis', 'First two neck vertebrae; the axis was exceptionally short, locking the head steady.');
  }

  // ================= SKULL (1.46 m, true fenestrae) =================
  // Local frame: +Z toward snout, tooth row y≈-0.30, roof y≈+0.30.
  const head = Jt('head', neck2, 0, 0.24, 0.52);

  { // braincase + occiput + nuchal crest + foramen magnum
    const g = grp(head);
    box(0.3, 0.36, 0.26, mats.bone, g, 0, 0.06, -0.3);
    box(0.3, 0.4, 0.06, mats.boneDark, g, 0, 0.04, -0.44);       // occipital plate
    box(0.34, 0.08, 0.08, mats.boneDark, g, 0, 0.28, -0.42);     // nuchal crest
    const fm = cyl(0.05, 0.05, 0.03, mats.socket, g, 0, 0.02, -0.475); // foramen magnum
    fm.rotation.x = Math.PI / 2;
    ball(0.065, mats.boneDark, g, 0, -0.1, -0.46);               // occipital condyle
    ball(0.05, mats.boneDark, g, 0.12, -0.08, -0.44);            // basal tubera
    ball(0.05, mats.boneDark, g, -0.12, -0.08, -0.44);
    tag(g, 'Braincase', 'Fused braincase with the foramen magnum and ball-like occipital condyle; huge olfactory bulbs inside.');
  }
  { // skull roof: frontals + parietals + supratemporal fossae
    const g = grp(head);
    box(0.26, 0.07, 0.55, mats.bone, g, 0, 0.27, -0.08);
    for (const s of [1, -1]) {
      const f = cyl(0.07, 0.07, 0.02, mats.socket, g, s * 0.07, 0.305, -0.28, 12); // supratemporal fenestra
      f.scale.z = 1.4;
    }
    tag(g, 'Skull roof', 'Thick frontals/parietals pierced by the supratemporal fenestrae — anchor for tons of jaw muscle.');
  }
  { // fused nasals: armored snout roof with midline ridge
    const g = grp(head);
    const n = box(0.15, 0.07, 0.85, mats.bone, g, 0, 0.2, 0.52);
    n.rotation.x = -0.03;
    box(0.05, 0.05, 0.7, mats.boneDark, g, 0, 0.25, 0.5);        // midline ridge
    for (let i = 0; i < 4; i++) ball(0.02, mats.boneDark, g, (i % 2 ? 0.05 : -0.05), 0.26, 0.3 + i * 0.15);
    tag(g, 'Nasals', 'Fused, rugose nasals armored the top of the snout.');
  }
  { // premaxilla: snout tip + 4 D-teeth + nasal process
    const g = grp(head);
    box(0.2, 0.28, 0.16, mats.bone, g, 0, -0.14, 1.03);
    box(0.07, 0.3, 0.1, mats.bone, g, 0, 0.08, 0.98);            // nasal process
    for (const s of [1, -1]) for (let i = 0; i < 2; i++) {
      const t = cone(0.026, 0.1, mats.tooth, g, s * (0.045 + i * 0.05), -0.32, 1.07 - i * 0.035);
      t.rotation.x = Math.PI;
    }
    tag(g, 'Premaxilla', 'Snout tip holding four small D-shaped teeth per side.');
  }
  { // palate + dark mouth cavity (visible when jaw opens)
    const g = grp(head);
    box(0.1, 0.04, 0.9, mats.boneDark, g, 0, -0.28, 0.45);       // vomer/palatine bar
    box(0.24, 0.2, 0.8, mats.socket, g, 0, -0.16, 0.3);          // mouth interior shadow
    tag(g, 'Palate', 'Midline palatal bones roofing the mouth cavity.');
  }
  for (const s of [1, -1]) {
    { // MAXILLA: tooth bar + ascending process; top edge forms the
      // antorbital fenestra floor. 12 teeth, largest mid-row.
      const g = grp(head);
      const px = plate([
        [0.14, -0.33], [0.98, -0.31], [1.0, -0.14], [0.92, -0.02], [0.88, 0.1],
        [0.8, 0.1], [0.76, -0.02], [0.5, -0.04], [0.24, -0.02], [0.14, -0.1],
      ], 0.075, mats.bone, g, s * 0.115, 0, 0);
      px.rotation.y = 0;
      toothRow(g, 0.2, 0.94, 12, s * 0.115, -0.31, 0.15, mats.tooth, false);
      tag(g, 'Maxilla', 'Main upper tooth bone (~12 serrated teeth/side) below the great antorbital fenestra.');
    }
    { // LACRIMAL: vertical bar + display boss; its front edge frames the
      // antorbital fenestra, its rear edge the orbit.
      const g = grp(head);
      plate([
        [0.3, -0.06], [0.34, 0.1], [0.3, 0.24], [0.16, 0.26], [0.12, 0.1], [0.14, -0.08],
      ], 0.07, mats.bone, g, s * 0.125, 0, 0);
      ball(0.055, mats.boneDark, g, s * 0.125, 0.27, 0.22, 1, 0.7, 1.3); // brow boss
      tag(g, 'Lacrimal', 'Bone over the eye with a horn-like display boss, between fenestra and orbit.');
    }
    { // JUGAL: cheek bar + two ascending processes; rims the orbit below.
      const g = grp(head);
      plate([
        [0.18, -0.24], [0.2, -0.1], [0.14, -0.06], [0.1, -0.12], [-0.14, -0.12],
        [-0.18, -0.02], [-0.26, -0.02], [-0.26, -0.14], [-0.28, -0.24],
      ], 0.065, mats.bone, g, s * 0.13, 0, 0);
      tag(g, 'Jugal', 'Cheek bone forming the lower rim of the orbit.');
    }
    { // POSTORBITAL: T-bar behind the orbit with brow process.
      const g = grp(head);
      plate([
        [-0.16, -0.04], [-0.14, 0.14], [-0.2, 0.26], [-0.32, 0.26], [-0.34, 0.1], [-0.3, -0.06],
      ], 0.07, mats.bone, g, s * 0.14, 0, 0);
      tag(g, 'Postorbital', 'Bone behind the eye; its brow processes gave T. rex forward binocular vision.');
    }
    { // SQUAMOSAL + QUADRATOJUGAL: rear corner; gap ahead = lateral temporal fenestra
      const g = grp(head);
      plate([
        [-0.3, -0.18], [-0.28, 0.0], [-0.32, 0.14], [-0.42, 0.14], [-0.44, -0.1], [-0.4, -0.2],
      ], 0.065, mats.boneDark, g, s * 0.145, 0, 0);
      tag(g, 'Squamosal', 'Rear skull corner framing the lateral temporal fenestra (jaw-muscle opening).');
    }
    { // QUADRATE: hinge strut down to the jaw joint
      const g = grp(head);
      strut(g, s * 0.12, 0.04, -0.32, s * 0.12, -0.32, -0.3, 0.045, mats.boneDark);
      ball(0.06, mats.boneDark, g, s * 0.12, -0.33, -0.3);
      tag(g, 'Quadrate', 'Vertical hinge bone; the lower jaw swung from its lower condyles.');
    }
    { // orbit content: eyeball + lids
      const g = grp(head);
      ball(0.095, mats.socket, g, s * 0.1, 0.06, -0.02);
      ball(0.035, mats.eye, g, s * 0.15, 0.06, 0.0);
      const lid = M(new THREE.TorusGeometry(0.08, 0.02, 8, 18), mats.boneDark, g, s * 0.15, 0.06, 0.0);
      lid.rotation.y = Math.PI / 2;
      tag(g, 'Orbit', 'Forward-facing eye sockets — rare in giant theropods — gave overlapping depth vision.');
    }
  }
  label(head, [0, 0.48, 0.35], 'Cranium');
  label(head, [-0.32, 0.12, -0.02], 'Orbit');
  const headLight = new THREE.PointLight(0xffaa33, 0.4, 5, 1);
  headLight.position.set(0, 0.1, 0.7);
  head.add(headLight);

  // ---- lower jaw (hinge at quadrate) ----
  const jaw = Jt('jaw', head, 0, -0.34, -0.3);
  for (const s of [1, -1]) {
    { // DENTARY: deep tooth bar, 14 teeth
      const g = grp(jaw);
      plate([
        [-0.02, -0.26], [1.28, -0.3], [1.36, -0.22], [1.36, -0.12], [1.3, -0.1],
        [0.9, -0.13], [0.4, -0.14], [0.0, -0.12],
      ], 0.06, mats.bone, g, s * 0.095, 0, 0);
      toothRow(g, 0.12, 1.26, 14, s * 0.095, -0.13, 0.1, mats.tooth, true);
      tag(g, 'Dentary', 'Main lower-jaw bone carrying ~14 serrated teeth per side.');
    }
    { // SURANGULAR: upper rear + coronoid rise; jaw-muscle anchor
      const g = grp(jaw);
      plate([
        [-0.06, -0.12], [0.42, -0.12], [0.4, -0.02], [0.3, 0.04], [0.1, 0.05], [-0.04, 0.0],
      ], 0.055, mats.boneDark, g, s * 0.1, 0, 0);
      tag(g, 'Surangular', 'Upper rear jaw bone; prime anchor for the jaw-closing muscles.');
    }
    { // ANGULAR: lower rear; gap above = external mandibular fenestra
      const g = grp(jaw);
      plate([
        [-0.04, -0.28], [0.36, -0.28], [0.34, -0.2], [0.0, -0.19],
      ], 0.05, mats.boneDark, g, s * 0.1, 0, 0);
      tag(g, 'Angular', 'Lower rear jaw bone below the external mandibular fenestra.');
    }
    ball(0.055, mats.boneDark, jaw, s * 0.1, -0.04, -0.04).geometry; // articular (tagged below)
    hydro(head, [s * 0.17, 0.28, -0.32], jaw, [s * 0.12, 0.06, 0.28], 0.038, 'jaw');
  }
  {
    const g = grp(jaw);
    box(0.17, 0.18, 0.14, mats.bone, g, 0, -0.2, 1.32); // chin symphysis
    tag(g, 'Dentary', 'Fused chin symphysis — the bite-force nexus of the lower jaws.');
  }
  label(jaw, [0, -0.42, 0.9], 'Dentary');

  // ================= HINDLIMBS (IK-driven) =================
  for (const s of [1, -1]) {
    const nm = s === 1 ? 'L' : 'R';
    const hip = Jt('hip' + nm, pelvis, s * HIP_LOCAL.x, HIP_LOCAL.y, HIP_LOCAL.z);
    { // FEMUR 1.32: head + neck + bowed shaft + trochanter + condyles
      const g = grp(hip);
      const headBall = ball(0.13, mats.boneDark, g, s * -0.06, 0.06, 0); // femoral head (medial)
      headBall.scale.set(1, 1.2, 1);
      strut(g, s * -0.04, 0.0, 0, 0, -0.2, 0, 0.1, mats.bone);           // neck
      strut(g, 0, -0.15, 0, 0, -LEG.L1 + 0.05, 0.015, 0.105, mats.bone); // shaft
      box(0.07, 0.22, 0.09, mats.boneDark, g, 0, -0.52, -0.12);          // 4th trochanter
      ball(0.115, mats.boneDark, g, 0.085, -LEG.L1, 0);                  // lateral condyle
      ball(0.115, mats.boneDark, g, -0.085, -LEG.L1, 0);                 // medial condyle
      box(0.1, 0.08, 0.1, mats.boneDark, g, 0, -LEG.L1 + 0.08, 0.1);     // intercondylar bridge
      tag(g, 'Femur', 'Thigh bone, 1.32 m; its fourth trochanter anchored tail-powered leg muscles.');
    }
    const knee = Jt('knee' + nm, hip, 0, -LEG.L1, 0);
    { // TIBIA 1.16: cnemial crest + shaft + malleolus; FIBULA splint
      const g = grp(knee);
      ball(0.11, mats.boneDark, g, 0, -0.02, 0);
      strut(g, 0, 0, 0, 0, -LEG.L2, 0, 0.085, mats.bone);
      const cn = box(0.06, 0.34, 0.16, mats.boneDark, g, 0, -0.22, 0.12); // cnemial crest
      cn.rotation.x = 0.15;
      ball(0.095, mats.boneDark, g, 0, -LEG.L2, 0);                       // medial malleolus
      strut(g, s * 0.11, -0.04, -0.01, s * 0.1, -1.02, 0, 0.028, mats.boneDark); // fibula
      ball(0.045, mats.boneDark, g, s * 0.11, -0.04, -0.01);
      tag(g, 'Tibia + fibula', 'Shin (1.16 m) with a tall cnemial crest; slender fibular splint alongside.');
    }
    { // ASTRAGALUS: ankle cap + ascending process bracing the shin
      const g = grp(knee, 0, -LEG.L2, 0);
      const cap = cyl(0.105, 0.105, 0.13, mats.boneDark, g, 0, 0.0, 0.01);
      cap.rotation.x = Math.PI / 2;
      box(0.12, 0.26, 0.035, mats.boneDark, g, 0, 0.16, 0.09);            // ascending process
      ball(0.05, mats.boneDark, g, s * 0.1, -0.02, -0.03);                // calcaneum nub
      tag(g, 'Astragalus', 'Ankle bone whose ascending process braced the shin — the mesotarsal hinge.');
    }
    const ank = Jt('ank' + nm, knee, 0, -LEG.L2, 0);
    { // METATARSALS II–IV (0.68): arctometatarsus — III longest, pinched top
      const g = grp(ank);
      strut(g, -0.07, 0, 0, -0.078, -LEG.LMT, 0.01, 0.045, mats.bone);         // MT-II
      strut(g, 0, -0.03, 0.035, 0, -LEG.LMT - 0.035, 0.03, 0.045, mats.bone);  // MT-III
      strut(g, 0.07, 0, 0, 0.078, -LEG.LMT, 0.01, 0.045, mats.bone);           // MT-IV
      for (const [mx, my, mz] of [[-0.078, -LEG.LMT, 0.01], [0, -LEG.LMT - 0.035, 0.03], [0.078, -LEG.LMT, 0.01]]) {
        ball(0.055, mats.boneDark, g, mx - 0.025, my, mz);
        ball(0.055, mats.boneDark, g, mx + 0.025, my, mz);                     // distal condyles
      }
      tag(g, 'Metatarsals II–IV', 'Arctometatarsalian foot: MT-III pinched between II and IV — stiff and spring-like.');
    }
    const mtp = Jt('mtp' + nm, ank, 0, -LEG.LMT, 0.01);
    { // PEDAL DIGITS II(3)–III(4)–IV(5) + unguals; hallux vestige
      const g = grp(mtp);
      const buildToe = (tx, lens, clawLen) => {
        const tg = grp(g, tx, -0.04, 0);
        tg.rotation.x = 0.08;
        let z = 0, y = 0;
        lens.forEach((L, i) => {
          const r = 0.042 - i * 0.006;
          strut(tg, 0, y, z, 0, y - 0.012, z + L, r, mats.bone);
          ball(r * 1.15, mats.boneDark, tg, 0, y - 0.012, z + L);
          y -= 0.012; z += L;
        });
        const c1 = cone(0.04, clawLen * 0.6, mats.tooth, tg, 0, y - 0.02, z + clawLen * 0.25);
        c1.rotation.x = Math.PI / 2 + 0.3;
        const c2 = cone(0.028, clawLen * 0.5, mats.tooth, tg, 0, y - 0.055, z + clawLen * 0.55);
        c2.rotation.x = Math.PI / 2 + 0.7;
      };
      buildToe(-0.1, [0.13, 0.1, 0.08], 0.2);          // digit II
      buildToe(0, [0.14, 0.11, 0.09, 0.07], 0.22);     // digit III (4 phalanges)
      buildToe(0.1, [0.12, 0.09, 0.07, 0.06, 0.05], 0.19); // digit IV (5 phalanges)
      const hg = grp(g, s * -0.13, -0.02, -0.06);      // hallux (digit I)
      strut(hg, 0, 0, 0, 0, -0.03, 0.07, 0.02, mats.boneDark);
      const hc = cone(0.025, 0.08, mats.tooth, hg, 0, -0.045, 0.12);
      hc.rotation.x = Math.PI / 2 + 0.35;
      tag(g, 'Pedal digits II–IV', 'Three weight-bearing toes with the true 3-4-5 phalangeal formula, plus a reduced dewclaw.');
    }
    // leg hydraulics
    hydro(pelvis, [s * 0.35, 0.5, -0.85], hip, [0, -0.7, -0.17], 0.055, 'leg' + nm);
    hydro(hip, [0, -0.28, 0.15], knee, [0, -0.3, 0.15], 0.045, 'leg' + nm);
    hydro(knee, [0, -0.25, -0.14], ank, [0, -0.18, -0.11], 0.038, 'leg' + nm);
    hydro(ank, [0, -0.4, 0.1], mtp, [0, 0.05, 0.16], 0.026, 'leg' + nm);
    hydro(knee, [0, -0.55, 0.12], ank, [0, -0.08, 0.1], 0.028, 'leg' + nm);
  }
  label(J.hipL, [0.18, -0.65, 0], 'Femur 1.32 m');
  label(J.kneeL, [0.15, -0.55, 0], 'Tibia 1.16 m');
  label(J.ankL, [0.16, -0.35, 0], 'Metatarsus');
  label(J.mtpL, [0, 0.05, 0.7], 'Pes 3-4-5');

  // ================= TAIL (physics-driven, deep-based) =================
  const tailRoot = Jt('tailRoot', pelvis, 0, 0.15, -0.95);
  {
    const g = grp(tailRoot);
    const c = cyl(0.2, 0.2, 0.3, mats.bone, g, 0, 0, -0.15, 14);
    c.rotation.x = Math.PI / 2;
    box(0.08, 0.5, 0.14, mats.boneDark, g, 0, 0.4, -0.15);
    tag(g, 'Caudal vertebrae', 'Over forty tail vertebrae (Sue: 47) — the deep, heavy tail balanced the head.');
  }
  for (let i = 0; i < TAIL_LENS.length; i++) {
    const len = TAIL_LENS[i];
    const t = i / (TAIL_LENS.length - 1);
    const r = 0.185 * (1 - t) + 0.04 * t;
    const seg = new THREE.Group();
    seg.name = 'tailSeg' + i;
    root.add(seg);
    tailSegs.push(seg);
    const g = grp(seg);
    const nC = 3; // centra per segment
    for (let c = 0; c < nC; c++) {
      const z = len * ((c + 0.5) / nC);
      centrum(r, len / nC * 0.92, mats.bone, g, 0, 0, z);
      const h = (0.52 * (1 - t) + 0.05) * (1 - c * 0.08);
      box(0.05, h, len * 0.16, mats.boneDark, g, 0, r + h / 2 - 0.02, z); // neural spine
      ball(0.03, mats.boneDark, g, 0, r * 0.6, z - len / nC * 0.4);
      if (i < 4 && c === 1) { // transverse processes, proximal half
        const tl = 0.34 * (1 - t * 0.75);
        strut(g, 0, 0.05, z, -tl, 0.09, z, 0.026, mats.boneDark);
        strut(g, 0, 0.05, z, tl, 0.09, z, 0.026, mats.boneDark);
      }
    }
    tag(g, 'Caudal vertebrae', i < 2
      ? 'Deep proximal caudals with tall spines — anchors for the tail muscles driving each stride.'
      : 'Mid and distal caudals stiffened by long prezygapophyses, forming a dynamic counterbalance.');
    if (i < 5) {
      const cg = grp(seg);
      for (let c = 0; c < 2; c++) {
        const z = len * (0.3 + c * 0.4);
        const ch = (0.42 * (1 - t) + 0.03);
        strut(cg, -0.025, -r * 0.7, z, -0.02, -r * 0.7 - ch, z + 0.02, 0.02, mats.boneDark);
        strut(cg, 0.025, -r * 0.7, z, 0.02, -r * 0.7 - ch, z + 0.02, 0.02, mats.boneDark);
      }
      tag(cg, 'Chevrons', 'Y-shaped haemal arches beneath the tail shielding vessels and adding leverage.');
    }
    if (i === TAIL_LENS.length - 1) ball(0.032, mats.led, seg, 0, 0, len + 0.02);
  }
  label(tailSegs[2], [0, 0.6, 0.3], 'Caudals (40+)');
  label(tailSegs[1], [0, -0.6, 0.3], 'Chevrons');
  for (const s of [1, -1]) {
    hydro(pelvis, [s * 0.3, 0.35, -0.8], tailSegs[1], [s * 0.13, 0.18, 0.32], 0.033, 'tail');
  }

  const refs = { coreLight: null, headLight, footL: J.mtpL, footR: J.mtpR, lens: [] };
  root.traverse((o) => {
    if (o.isMesh && o.material === mats.eye) refs.lens.push(o);
    if (o.isPointLight && o.color.getHex() === 0x33d5ff) refs.coreLight = o;
  });

  return { root, J, tailSegs, labelSpots, hydroDefs, mats, refs };
}
