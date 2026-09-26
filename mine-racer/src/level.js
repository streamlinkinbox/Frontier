import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import {
  CFG, GW, GH, tidx,
  tileToWorldX as WX, tileToWorldZ as WZ,
} from './config.js';
import { hash2, mulberry32 } from './util.js';
import { makeTextures } from './textures.js';
import { bfsPath } from './maze.js';

const T = CFG.TILE;

// -- helpers ----------------------------------------------------------------
function box(list, w, h, d, x, y, z, ry = 0, rx = 0, rz = 0) {
  const g = new THREE.BoxGeometry(w, h, d);
  if (rx) g.rotateX(rx);
  if (rz) g.rotateZ(rz);
  if (ry) g.rotateY(ry);
  g.translate(x, y, z);
  list.push(g);
}

function displacedRockGeometry(seedOff) {
  const g = new THREE.DodecahedronGeometry(0.62, 1);
  const p = g.attributes.position;
  for (let i = 0; i < p.count; i++) {
    const x = p.getX(i), y = p.getY(i), z = p.getZ(i);
    const h = hash2(Math.round((x + 3) * 53) + seedOff, Math.round((y + 3) * 61) + Math.round((z + 3) * 47));
    const s = 1 + (h - 0.5) * 0.55;
    p.setXYZ(i, x * s, y * s * (0.75 + h * 0.3), z * s);
  }
  g.computeVertexNormals();
  return g;
}

// ---------------------------------------------------------------------------
// Build the entire mine. Returns everything gameplay needs to reference.
// ---------------------------------------------------------------------------
export function buildLevel(scene, maze, railCellsList) {
  const { isOpen, W, H } = maze;
  const tex = makeTextures();
  const rng = mulberry32(maze.seed ^ 0x5f3759df);

  // -- materials -------------------------------------------------------------
  const M = {
    rockA:  new THREE.MeshStandardMaterial({ map: tex.rock, roughness: 0.96 }),
    rockB:  new THREE.MeshStandardMaterial({ map: tex.rock, roughness: 0.96, color: 0xbfae9c }),
    ceil:   new THREE.MeshStandardMaterial({ map: tex.rock, roughness: 1.0, color: 0x7d7268 }),
    dirt:   new THREE.MeshStandardMaterial({ map: tex.dirt, roughness: 1.0 }),
    road:   new THREE.MeshStandardMaterial({ map: tex.road, roughness: 1.0 }),
    ballast:new THREE.MeshStandardMaterial({ color: 0x20170f, roughness: 1.0 }),
    wood:   new THREE.MeshStandardMaterial({ map: tex.wood, roughness: 0.85 }),
    woodDark:new THREE.MeshStandardMaterial({ map: tex.wood, roughness: 0.9, color: 0x8a7358 }),
    rail:   new THREE.MeshStandardMaterial({ map: tex.metal, color: 0xd9dade, metalness: 0.85, roughness: 0.32 }),
    metal:  new THREE.MeshStandardMaterial({ map: tex.metal, color: 0x9aa0a8, metalness: 0.7, roughness: 0.5 }),
    lampGlass: new THREE.MeshStandardMaterial({ color: 0x1a0d05, emissive: 0xffb45e, emissiveIntensity: 2.6, roughness: 0.4 }),
    rockProp:  new THREE.MeshStandardMaterial({ color: 0x6f6255, roughness: 0.95 }),
    gold:   new THREE.MeshStandardMaterial({ color: 0xd9a441, metalness: 0.9, roughness: 0.3, emissive: 0x241203, emissiveIntensity: 1 }),
    cyanPylon: new THREE.MeshStandardMaterial({ color: 0x0a1418, emissive: 0x59e6ff, emissiveIntensity: 1.7 }),
    checker: new THREE.MeshBasicMaterial({ map: tex.checker, side: THREE.DoubleSide }),
    sign:   null, // per-sign basic material
  };

  const group = new THREE.Group();
  scene.add(group);

  const railTiles = new Set();
  for (const cells of railCellsList)
    for (let k = 0; k < cells.length; k++) {
      const [a, b] = cells[k];
      railTiles.add((2 * a + 1) + ',' + (2 * b + 1));
      if (k > 0) { const [pa, pb] = cells[k - 1]; railTiles.add((pa + a + 1) + ',' + (pb + b + 1)); }
    }

  // =========================================================================
  // FLOORS — road texture with wheel-rut grooves on straightaways
  // =========================================================================
  const floorPlain = [], floorRoadEW = [], floorRoadNS = [];
  for (let x = 0; x < GW; x++) for (let z = 0; z < GH; z++) {
    if (!isOpen(x, z)) continue;
    if (railTiles.has(x + ',' + z)) { floorPlain.push([x, z]); continue; }
    const n = isOpen(x, z - 1), s = isOpen(x, z + 1), e = isOpen(x + 1, z), w = isOpen(x - 1, z);
    if (e && w && !n && !s) floorRoadEW.push([x, z]);
    else if (n && s && !e && !w) floorRoadNS.push([x, z]);
    else floorPlain.push([x, z]);
  }
  const floorGeoms = { dirt: [], road: [] };
  const pushFloor = (list, mat, ns) => {
    for (const [x, z] of list) {
      const g = new THREE.PlaneGeometry(T, T);
      g.rotateX(-Math.PI / 2);
      if (ns) g.rotateY(Math.PI / 2);
      g.translate(WX(x), 0, WZ(z));
      floorGeoms[mat].push(g);
    }
  };
  pushFloor(floorPlain, 'dirt', false);
  pushFloor(floorRoadEW, 'road', false);
  pushFloor(floorRoadNS, 'road', true);
  for (const mat of ['dirt', 'road']) {
    if (!floorGeoms[mat].length) continue;
    const mesh = new THREE.Mesh(mergeGeometries(floorGeoms[mat]), mat === 'dirt' ? M.dirt : M.road);
    mesh.receiveShadow = true;
    group.add(mesh);
  }

  // =========================================================================
  // WALLS — only the "shell" of rock tiles that face open tunnel
  // =========================================================================
  const wallsA = [], wallsB = [];
  for (let x = 0; x < GW; x++) for (let z = 0; z < GH; z++) {
    if (isOpen(x, z)) continue;
    let facesTunnel = false;
    for (let dx = -1; dx <= 1 && !facesTunnel; dx++)
      for (let dz = -1; dz <= 1 && !facesTunnel; dz++)
        if (isOpen(x + dx, z + dz)) facesTunnel = true;
    if (!facesTunnel) continue;
    const h2 = hash2(x, z);
    const hh = CFG.WALL_H * (1.04 + h2 * 0.18);
    const list = h2 < 0.55 ? wallsA : wallsB;
    box(list, T, hh, T, WX(x), hh / 2 - 0.35, WZ(z));
  }
  for (const [list, mat] of [[wallsA, M.rockA], [wallsB, M.rockB]]) {
    const mesh = new THREE.Mesh(mergeGeometries(list), mat);
    mesh.castShadow = true; mesh.receiveShadow = true;
    group.add(mesh);
  }

  // =========================================================================
  // CEILING slabs over the tunnels
  // =========================================================================
  const ceilG = [];
  for (let x = 0; x < GW; x++) for (let z = 0; z < GH; z++) {
    if (!isOpen(x, z)) continue;
    box(ceilG, T + 0.06, 0.95, T + 0.06, WX(x), CFG.WALL_H + 0.45, WZ(z));
  }
  const ceilMesh = new THREE.Mesh(mergeGeometries(ceilG), M.ceil);
  ceilMesh.receiveShadow = true;
  group.add(ceilMesh);

  // =========================================================================
  // TIMBER MINE SUPPORTS — classic post-and-lintel sets + corner posts,
  // with hanging lanterns
  // =========================================================================
  const wood = [], lampMetal = [], lampGlass = [];
  const lampAnchors = [];       // bulb positions for the dynamic light pool
  const FH = 3.7;               // support frame height

  function addLamp(x, bulbY, z, anchorY) {
    const cordH = Math.max(0.15, anchorY - bulbY - 0.34);
    box(lampMetal, 0.07, cordH, 0.07, x, anchorY - cordH / 2, z);
    const cap = new THREE.ConeGeometry(0.18, 0.15, 8); cap.translate(x, bulbY + 0.34, z); lampMetal.push(cap);
    const top = new THREE.CylinderGeometry(0.17, 0.17, 0.05, 8); top.translate(x, bulbY + 0.26, z); lampMetal.push(top);
    const bot = new THREE.CylinderGeometry(0.16, 0.16, 0.05, 8); bot.translate(x, bulbY - 0.27, z); lampMetal.push(bot);
    for (let k = 0; k < 3; k++) {
      const a = (k / 3) * Math.PI * 2;
      box(lampMetal, 0.03, 0.52, 0.03, x + Math.cos(a) * 0.15, bulbY, z + Math.sin(a) * 0.15);
    }
    const glass = new THREE.CylinderGeometry(0.115, 0.115, 0.38, 8);
    glass.translate(x, bulbY, z);
    lampGlass.push(glass);
    lampAnchors.push(new THREE.Vector3(x, bulbY, z));
  }

  for (let x = 1; x < GW - 1; x++) for (let z = 1; z < GH - 1; z++) {
    if (!isOpen(x, z)) continue;
    const n = isOpen(x, z - 1), s = isOpen(x, z + 1), e = isOpen(x + 1, z), w = isOpen(x - 1, z);
    const openN = (n ? 1 : 0) + (s ? 1 : 0) + (e ? 1 : 0) + (w ? 1 : 0);
    const wx = WX(x), wz = WZ(z);
    const h2 = hash2(x * 3 + 1, z * 5 + 2);

    if (e && w && !n && !s && h2 < 0.52) {
      // E-W corridor: frame spanning across Z
      const off = T / 2 - 0.68;
      box(wood, 0.44, FH, 0.44, wx, FH / 2, wz - off);
      box(wood, 0.44, FH, 0.44, wx, FH / 2, wz + off);
      box(wood, 0.52, 0.48, T - 0.55, wx, FH + 0.24, wz);
      box(wood, 0.3, 0.32, 1.5, wx, FH - 0.5, wz - off + 0.75, 0, -0.72);
      box(wood, 0.3, 0.32, 1.5, wx, FH - 0.5, wz + off - 0.75, 0, 0.72);
      if (h2 < 0.34) addLamp(wx, FH - 0.62, wz, FH + 0.2);
    } else if (n && s && !e && !w && h2 < 0.52) {
      // N-S corridor: frame spanning across X
      const off = T / 2 - 0.68;
      box(wood, 0.44, FH, 0.44, wx - off, FH / 2, wz);
      box(wood, 0.44, FH, 0.44, wx + off, FH / 2, wz);
      box(wood, T - 0.55, 0.48, 0.52, wx, FH + 0.24, wz);
      box(wood, 1.5, 0.32, 0.3, wx - off + 0.75, FH - 0.5, wz, 0, 0, 0.72);
      box(wood, 1.5, 0.32, 0.3, wx + off - 0.75, FH - 0.5, wz, 0, 0, -0.72);
      if (h2 < 0.34) addLamp(wx, FH - 0.62, wz, FH + 0.2);
    } else if (openN >= 3 || (openN === 2 && !(e && w) && !(n && s))) {
      // intersection / corner: chunky corner posts against the rock
      for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
        if (hash2(x * 7 + sx, z * 11 + sz) < 0.75) {
          box(wood, 0.55, WALL_H_Minus(), 0.55,
            wx + sx * (T / 2 - 0.6), WALL_H_Minus() / 2, wz + sz * (T / 2 - 0.6));
        }
      }
      if (h2 < 0.4) addLamp(wx, CFG.WALL_H - 1.35, wz, CFG.WALL_H - 0.05);
    }
  }
  function WALL_H_Minus() { return CFG.WALL_H - 0.55; }

  const woodMesh = new THREE.Mesh(mergeGeometries(wood), M.wood);
  woodMesh.castShadow = true; woodMesh.receiveShadow = true;
  group.add(woodMesh);
  // NOTE: lantern fixtures are merged once at the very end, so portal/finish
  // lamps added later still end up in the same static meshes.

  // =========================================================================
  // RAIL LINES — recessed ballast groove, twin steel rails, wooden sleepers,
  // flashing warning beacons
  // =========================================================================
  const railLines = [];
  const railTubeG = [];
  const sleeperMatsLocal = [];
  const stripGeoms = [];

  for (let li = 0; li < railCellsList.length; li++) {
    const cells = railCellsList[li];
    const pts = cells.map(([cx, cz]) => new THREE.Vector3(WX(2 * cx + 1), 0, WZ(2 * cz + 1)));
    const curve = new THREE.CatmullRomCurve3(pts, false, 'centripetal', 0.5);
    const len = curve.getLength();
    const n = Math.max(16, Math.ceil(len / 0.5));
    const sp = curve.getSpacedPoints(n);

    // tangents + 2D normals
    const tan = [], nor = [];
    for (let i = 0; i <= n; i++) {
      const a = sp[Math.max(0, i - 1)], b = sp[Math.min(n, i + 1)];
      const t = new THREE.Vector3().subVectors(b, a); t.y = 0; t.normalize();
      tan.push(t);
      nor.push(new THREE.Vector3(-t.z, 0, t.x));
    }

    // recessed groove / ballast strip
    const half = 1.55, pos = [], uv = [];
    for (let i = 0; i < n; i++) {
      const p0 = sp[i], p1 = sp[i + 1], n0 = nor[i], n1 = nor[i + 1];
      const y = 0.022;
      const l0 = [p0.x - n0.x * half, y, p0.z - n0.z * half], r0 = [p0.x + n0.x * half, y, p0.z + n0.z * half];
      const l1 = [p1.x - n1.x * half, y, p1.z - n1.z * half], r1 = [p1.x + n1.x * half, y, p1.z + n1.z * half];
      pos.push(...l0, ...r0, ...l1, ...r0, ...r1, ...l1);
      const v0 = (i * len / n) / T, v1 = ((i + 1) * len / n) / T;
      uv.push(0, v0, 1, v0, 0, v1, 1, v0, 1, v1, 0, v1);
    }
    const strip = new THREE.BufferGeometry();
    strip.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    strip.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
    strip.computeVertexNormals();
    stripGeoms.push(strip);

    // steel rails (gauge 1.5 m) sitting proud of the groove
    const left = [], right = [];
    for (let i = 0; i <= n; i += 1) {
      left.push(new THREE.Vector3(sp[i].x - nor[i].x * 0.75, 0.1, sp[i].z - nor[i].z * 0.75));
      right.push(new THREE.Vector3(sp[i].x + nor[i].x * 0.75, 0.1, sp[i].z + nor[i].z * 0.75));
    }
    railTubeG.push(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(left), Math.ceil(n * 0.7), 0.075, 5, false));
    railTubeG.push(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(right), Math.ceil(n * 0.7), 0.075, 5, false));

    // sleepers
    const step = 0.85, count = Math.floor(len / step);
    const dummy = new THREE.Object3D();
    const arr = [];
    for (let k = 0; k < count; k++) {
      const u = (k * step + step / 2) / len;
      const p = curve.getPointAt(u), t = curve.getTangentAt(u);
      dummy.position.set(p.x, 0.058, p.z);
      dummy.rotation.set(0, Math.atan2(t.x, t.z), 0);
      dummy.updateMatrix();
      arr.push(dummy.matrix.clone());
    }
    sleeperMatsLocal.push(arr);

    // warning beacons near both ends of the line
    const beacons = [];
    for (const bu of [0.05, 0.95]) {
      const p = curve.getPointAt(bu), t = curve.getTangentAt(bu);
      const nx = -t.z, nz = t.x;
      const bx = p.x + nx * 2.6, bz = p.z + nz * 2.6;
      const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.09, 1.7, 6), M.metal);
      pole.position.set(bx, 0.85, bz);
      group.add(pole);
      const bulbMat = new THREE.MeshBasicMaterial({ color: 0x33200a });
      const bulb = new THREE.Mesh(new THREE.SphereGeometry(0.13, 10, 8), bulbMat);
      bulb.position.set(bx, 1.8, bz);
      group.add(bulb);
      beacons.push({ mat: bulbMat, mesh: bulb });
    }

    railLines.push({ curve, length: len, beacons, tiles: cells });
  }

  if (railTubeG.length) group.add(new THREE.Mesh(mergeGeometries(railTubeG), M.rail));
  if (stripGeoms.length) group.add(new THREE.Mesh(mergeGeometries(stripGeoms, false), M.ballast));
  {
    const total = sleeperMatsLocal.reduce((a, b) => a + b.length, 0);
    const sleeper = new THREE.InstancedMesh(new THREE.BoxGeometry(2.25, 0.11, 0.44), M.woodDark, Math.max(1, total));
    let ii = 0;
    for (const arr of sleeperMatsLocal) for (const m of arr) sleeper.setMatrixAt(ii++, m);
    sleeper.count = ii;
    sleeper.receiveShadow = true;
    group.add(sleeper);
  }

  // =========================================================================
  // PROPS — rubble, crates, barrels (instanced; placed against the walls)
  // =========================================================================
  const start = [0, 0], finish = [W - 1, H - 1];
  const nearPole = (cx, cz, cell) => Math.abs(cx - cell[0]) + Math.abs(cz - cell[1]) <= 1;

  const rockPlace = [], cratePlace = [], barrelPlace = [];
  for (let x = 1; x < GW - 1; x += 2) for (let z = 1; z < GH - 1; z += 2) {
    if (!isOpen(x, z)) continue;
    const cx = (x - 1) / 2, cz = (z - 1) / 2;
    if (nearPole(cx, cz, start) || nearPole(cx, cz, finish)) continue;
    if (railTiles.has(x + ',' + z)) continue;
    const sides = [[0, -1], [0, 1], [-1, 0], [1, 0]].filter(([dx, dz]) => !isOpen(x + dx, z + dz));
    if (!sides.length) continue;
    const h = hash2(x * 13 + 7, z * 17 + 3);
    if (h > 0.42) continue;
    const [dx, dz] = sides[(hash2(x, z * 3) * sides.length) | 0];
    const px = WX(x) + dx * (T / 2 - 1.35) + (rng() - 0.5) * 1.1;
    const pz = WZ(z) + dz * (T / 2 - 1.35) + (rng() - 0.5) * 1.1;
    if (h < 0.17) rockPlace.push([px, pz]);
    else if (h < 0.3) cratePlace.push([px, pz, rng() < 0.35]);
    else barrelPlace.push([px, pz]);
  }
  // scatter loose rock along corridor walls too
  for (let i = 0; i < 160; i++) {
    const x = 1 + ((rng() * (GW - 2)) | 0), z = 1 + ((rng() * (GH - 2)) | 0);
    if (!isOpen(x, z) || railTiles.has(x + ',' + z)) continue;
    const px = WX(x) + (rng() - 0.5) * (T - 2.4);
    const pz = WZ(z) + (rng() - 0.5) * (T - 2.4);
    rockPlace.push([px, pz, true]);
  }

  {
    const dummy = new THREE.Object3D();
    const rockGeo = displacedRockGeometry(3);
    const rocks = new THREE.InstancedMesh(rockGeo, M.rockProp, Math.max(1, rockPlace.length));
    let i = 0;
    for (const [px, pz, small] of rockPlace) {
      const s = (small ? 0.25 : 0.55) + rng() * 0.75;
      dummy.position.set(px, s * 0.3, pz);
      dummy.scale.setScalar(s);
      dummy.rotation.set(rng() * 0.4, rng() * Math.PI * 2, rng() * 0.4);
      dummy.updateMatrix();
      rocks.setMatrixAt(i++, dummy.matrix);
    }
    rocks.count = i;
    rocks.castShadow = true; rocks.receiveShadow = true;
    group.add(rocks);
  }
  {
    const dummy = new THREE.Object3D();
    const crates = new THREE.InstancedMesh(new THREE.BoxGeometry(1.05, 1.05, 1.05), M.wood, Math.max(1, cratePlace.length * 2));
    let i = 0;
    for (const [px, pz, tall] of cratePlace) {
      dummy.position.set(px, 0.53, pz); dummy.scale.setScalar(0.85 + rng() * 0.35);
      dummy.rotation.set(0, rng() * Math.PI, 0); dummy.updateMatrix();
      crates.setMatrixAt(i++, dummy.matrix);
      if (tall) {
        dummy.position.y = 1.55; dummy.scale.setScalar(0.65 + rng() * 0.2);
        dummy.rotation.y = rng() * Math.PI; dummy.updateMatrix();
        crates.setMatrixAt(i++, dummy.matrix);
      }
    }
    crates.count = i;
    crates.castShadow = true; crates.receiveShadow = true;
    group.add(crates);
    const barrels = new THREE.InstancedMesh(new THREE.CylinderGeometry(0.44, 0.48, 1.0, 10), M.woodDark, Math.max(1, barrelPlace.length));
    i = 0;
    for (const [px, pz] of barrelPlace) {
      dummy.position.set(px, 0.5, pz); dummy.scale.setScalar(0.9 + rng() * 0.25);
      dummy.rotation.set(0, rng() * Math.PI, 0); dummy.updateMatrix();
      barrels.setMatrixAt(i++, dummy.matrix);
    }
    barrels.count = i;
    barrels.castShadow = true; barrels.receiveShadow = true;
    group.add(barrels);
  }

  // =========================================================================
  // ENTRANCE PORTAL (start) with painted sign
  // =========================================================================
  const solution = bfsPath(maze, start, finish);
  const second = solution[1] || [1, 0];
  const dirX = second[0] - start[0], dirZ = second[1] - start[1];
  const startYaw = Math.atan2(dirX, dirZ);
  const sx = WX(1), sz = WZ(1);
  {
    const portal = [];
    const px = -dirZ, pz = dirX; // perpendicular
    box(portal, 0.7, 4.5, 0.7, sx + px * 3.1, 2.25, sz + pz * 3.1);
    box(portal, 0.7, 4.5, 0.7, sx - px * 3.1, 2.25, sz - pz * 3.1);
    const beam = new THREE.BoxGeometry(0.85, 0.75, 7.0);
    beam.rotateY(Math.atan2(px, pz));
    beam.translate(sx, 4.85, sz);
    portal.push(beam);
    const pm = new THREE.Mesh(mergeGeometries(portal), M.wood);
    pm.castShadow = true;
    group.add(pm);
    const signMat = new THREE.MeshBasicMaterial({ map: tex.sign('SHAFT 07 ⇩') });
    const sign = new THREE.Mesh(new THREE.PlaneGeometry(3.6, 1.8), signMat);
    sign.position.set(sx, 3.6, sz);
    sign.rotation.y = startYaw + Math.PI;
    group.add(sign);
    addLamp(sx + px * 2.6, 3.2, sz + pz * 2.6, 4.4);
    addLamp(sx - px * 2.6, 3.2, sz - pz * 2.6, 4.4);
  }

  // =========================================================================
  // FINISH — the gold chamber
  // =========================================================================
  const fx = WX(2 * finish[0] + 1), fz = WZ(2 * finish[1] + 1);
  {
    const dummy = new THREE.Object3D();
    const gold = new THREE.InstancedMesh(displacedRockGeometry(9), M.gold, 26);
    for (let i = 0; i < 26; i++) {
      const a = rng() * Math.PI * 2, r = i < 12 ? 1.4 + rng() * 1.4 : 2.2 + rng() * 2.1;
      const s = 0.35 + rng() * (i < 12 ? 0.5 : 0.9);
      dummy.position.set(fx + Math.cos(a) * r, s * 0.32, fz + Math.sin(a) * r);
      dummy.scale.setScalar(s);
      dummy.rotation.set(rng(), rng() * 6.28, rng());
      dummy.updateMatrix();
      gold.setMatrixAt(i, dummy.matrix);
    }
    gold.castShadow = true;
    group.add(gold);

    // checker flag posts (facing out of the tunnel you arrive from)
    const nb = [[1, 0], [-1, 0], [0, 1], [0, -1]].find(([dx, dz]) => isOpen(2 * finish[0] + 1 + dx, 2 * finish[1] + 1 + dz)) || [0, 1];
    const fdirX = -nb[0], fdirZ = -nb[1];
    const px = -fdirZ, pz = fdirX;
    for (const s of [1, -1]) {
      const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.09, 0.12, 3.6, 8), M.wood);
      pole.position.set(fx + px * 3.2 * s, 1.8, fz + pz * 3.2 * s);
      pole.castShadow = true;
      group.add(pole);
      const flag = new THREE.Mesh(new THREE.PlaneGeometry(1.35, 0.9), M.checker);
      flag.position.set(fx + px * 3.2 * s + fdirX * 0.1, 3.15, fz + pz * 3.2 * s + fdirZ * 0.1);
      flag.rotation.y = Math.atan2(fdirX, fdirZ);
      group.add(flag);
    }
    const goldLight = new THREE.PointLight(0xffc266, 70, 36, 1.8);
    goldLight.position.set(fx, 3.0, fz);
    group.add(goldLight);
    addLamp(fx + px * 3, 2.9, fz + pz * 3, CFG.WALL_H - 0.05);
  }



  // =========================================================================
  // CHECKPOINTS — cyan holo-rings + glowing pylons along the solution route
  // =========================================================================
  const checkpoints = [];
  {
    const count = CFG.CHECKPOINTS;
    for (let k = 1; k <= count; k++) {
      const ii = Math.min(solution.length - 2, Math.floor((k / (count + 1)) * solution.length));
      const [cx, cz] = solution[ii];
      const [pcx, pcz] = solution[ii - 1];
      const [ncx, ncz] = solution[ii + 1] || solution[ii];
      const dx = Math.sign(ncx - pcx), dz = Math.sign(ncz - pcz);
      const px = WX(2 * cx + 1), pz = WZ(2 * cz + 1);
      const yaw = Math.atan2(dx, dz);

      const pylons = [];
      for (const s of [1, -1]) {
        const post = new THREE.Mesh(new THREE.BoxGeometry(0.34, 2.8, 0.34), M.cyanPylon);
        post.position.set(px + (-dz) * 2.8 * s, 1.4, pz + (dx) * 2.8 * s);
        group.add(post);
        pylons.push(post);
      }
      const ringMat = new THREE.MeshBasicMaterial({
        color: 0x59e6ff, transparent: true, opacity: 0.6,
        blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide,
      });
      const ring = new THREE.Mesh(new THREE.TorusGeometry(2.6, 0.085, 8, 42), ringMat);
      ring.position.set(px, 2.55, pz);
      ring.rotation.y = yaw;
      group.add(ring);
      checkpoints.push({ pos: new THREE.Vector3(px, 0, pz), yaw, ring, ringMat, pylons, index: k - 1 });
    }
  }

  // All lantern fixtures in two static merged meshes (metal cage + glow glass)
  if (lampMetal.length) {
    group.add(new THREE.Mesh(mergeGeometries(lampMetal), M.metal));
    group.add(new THREE.Mesh(mergeGeometries(lampGlass), M.lampGlass));
  }

  return {
    group, tex, M,
    lampAnchors,
    railLines,
    railTiles,
    checkpoints,
    startPose: { x: sx, z: sz, yaw: startYaw },
    finishPos: new THREE.Vector3(fx, 0, fz),
    solution,
  };
}
