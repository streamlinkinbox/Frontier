// Three.js world: renderer, sky/sun, ground, chunked road meshes, instanced props, overlays, picking.
import * as THREE from 'three';
import { Sky } from 'three/examples/jsm/objects/Sky.js';
import { buildTextureSet } from './textures.js';
import { buildMaterials, buildPropMaterials } from './materials.js';
import { propLibrary, SHADOW_PROPS } from './propsLib.js';
import { CameraController } from './camera.js';
import { getProfile, layout } from '../road/profiles.js';

const CHUNK = 512;
const PCHUNK = 400;
const PICK_MATS = new Set(['asphalt', 'asphaltOld', 'concrete', 'sidewalk', 'pavers', 'cobble', 'gravel', 'dirt', 'busRed', 'bikeGreen', 'ballast', 'crossPanel']);
const MARK_MATS = new Set(['markWhite', 'markYellow', 'decal']);
const DISTRICT_COLORS = { downtown: [0.30, 0.30, 0.29], midtown: [0.31, 0.32, 0.29], oldtown: [0.36, 0.33, 0.28], residential: [0.27, 0.34, 0.20], industrial: [0.34, 0.33, 0.29], park: [0.22, 0.36, 0.16], suburb: [0.28, 0.37, 0.20], rural: [0.33, 0.38, 0.19] };
export const CATEGORY_COLORS = { highway: '#f59e0b', arterial: '#4fd8e0', urban: '#e6e6e6', residential: '#a3a3a3', rural: '#a3c76b', active: '#22c55e' };

export class World {
  constructor(canvas) {
    this.canvas = canvas;
    const r = this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, logarithmicDepthBuffer: true, powerPreference: 'high-performance', preserveDrawingBuffer: false });
    r.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
    r.outputColorSpace = THREE.SRGBColorSpace;
    r.toneMapping = THREE.ACESFilmicToneMapping;
    r.toneMappingExposure = 0.62;
    r.info.autoReset = false;
    r.shadowMap.enabled = true;
    r.shadowMap.type = THREE.PCFSoftShadowMap;

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(60, 1, 0.3, 30000);
    this.camera.position.set(-120, 90, 160);
    this.controls = new CameraController(this.camera, canvas);

    this.T = buildTextureSet();
    for (const k in this.T) { const t = this.T[k]; const set = x => x && x.isTexture && (x.anisotropy = Math.min(16, r.capabilities.getMaxAnisotropy())); if (t?.map) { set(t.map); set(t.normal); } else set(t); }
    this.M = buildMaterials(this.T);
    this.PM = buildPropMaterials(this.T);
    this.props = propLibrary();

    this.setupSky();
    this.roads = new THREE.Group(); this.scene.add(this.roads);
    this.overlay = new THREE.Group(); this.overlay.renderOrder = 10; this.scene.add(this.overlay);
    this.hoverGroup = new THREE.Group(); this.scene.add(this.hoverGroup);
    this.previewGroup = new THREE.Group(); this.scene.add(this.previewGroup);
    this.topoGroup = new THREE.Group(); this.topoGroup.visible = false; this.scene.add(this.topoGroup);

    this.chunks = new Map();     // key -> {elements:Set, group, roadGroup, markGroup, propGroup, center}
    this.elemChunk = new Map();  // element id -> key
    this.propChunks = new Map(); // key -> {key, elements:Set, group, center}
    this.elemProps = new Map();  // element id -> Set(prop chunk keys)
    this.settings = { markings: true, props: true, trees: true, shadows: true, propDist: 620, markDist: 1600, view: 'lit', hour: 15.5 };
    this.stats = { tris: 0, calls: 0, fps: 0 };
    this.clock = new THREE.Clock();
    this.frameCbs = new Set();
    this.raycaster = new THREE.Raycaster();
    this._ft = 0; this._fc = 0;
    this.resize();
    this.ro = new ResizeObserver(() => this.resize()); this.ro.observe(canvas.parentElement);
    this.running = true;
    this.loop = this.loop.bind(this);
    requestAnimationFrame(this.loop);
  }

  // ------------------------------------------------------------------ environment
  setupSky() {
    const sky = this.sky = new Sky();
    sky.scale.setScalar(20000);
    sky.material.depthTest = false; sky.material.depthWrite = false; sky.renderOrder = -10; sky.frustumCulled = false;
    const u = sky.material.uniforms;
    u.turbidity.value = 4; u.rayleigh.value = 1.4; u.mieCoefficient.value = 0.004; u.mieDirectionalG.value = 0.85;
    this.scene.add(sky);
    this.sun = new THREE.DirectionalLight(0xfff2df, 3.2);
    this.sun.castShadow = true;
    const sc = this.sun.shadow;
    sc.mapSize.set(2048, 2048); sc.camera.near = 1; sc.camera.far = 1600;
    sc.camera.left = -260; sc.camera.right = 260; sc.camera.top = 260; sc.camera.bottom = -260;
    sc.camera.updateProjectionMatrix();
    sc.bias = -0.0003; sc.normalBias = 0.25;
    this.scene.add(this.sun, this.sun.target);
    this.hemi = new THREE.HemisphereLight(0xcfe3ff, 0x5a5448, 0.9);
    this.scene.add(this.hemi);
    this.scene.fog = new THREE.Fog(0xbfd0dc, 900, 9000);
    this.pmrem = new THREE.PMREMGenerator(this.renderer);
    this.setHour(this.settings?.hour ?? 15.5);
  }
  setHour(h) {
    if (this.settings) this.settings.hour = h;
    const day = Math.sin(((h - 6) / 12) * Math.PI);           // 1 at noon, <0 at night
    const elev = Math.max(-0.12, day) * 62;                     // degrees
    const az = ((h - 6) / 12) * 180 - 90 + 35;
    const phi = THREE.MathUtils.degToRad(90 - elev), theta = THREE.MathUtils.degToRad(az);
    this.sunDir = new THREE.Vector3().setFromSphericalCoords(1, phi, theta);
    this.sky.material.uniforms.sunPosition.value.copy(this.sunDir);
    const k = THREE.MathUtils.clamp(day * 3, 0, 1), night = 1 - THREE.MathUtils.clamp((day + 0.1) * 4, 0, 1);
    // Strong key light vs. dim fill keeps cast shadows readable on dark asphalt.
    this.sun.intensity = 5.2 * k;
    this.sun.color.setHSL(0.09, 0.6, 0.55 + 0.35 * k);
    this.hemi.intensity = 0.12 + 0.3 * k;
    this.renderer.toneMappingExposure = 0.62 + night * 0.5;
    const fog = new THREE.Color().setHSL(0.58, 0.3, 0.08 + 0.6 * THREE.MathUtils.clamp(day + 0.15, 0, 1) ** 0.6);
    this.scene.fog.color.copy(fog);
    this.PM.glow.emissiveIntensity = 0.3 + night * 3.0;
    // environment from sky
    if (this._env) this._env.dispose();
    const tmp = new THREE.Scene(); const s2 = new Sky(); s2.scale.setScalar(1000); s2.material.uniforms.sunPosition.value.copy(this.sunDir);
    for (const n of ['turbidity', 'rayleigh', 'mieCoefficient', 'mieDirectionalG']) s2.material.uniforms[n].value = this.sky.material.uniforms[n].value;
    tmp.add(s2);
    this._env = this.pmrem.fromScene(tmp, 0, 1, 3000);
    this.scene.environment = this._env.texture;
    this.scene.environmentIntensity = 0.2 + 0.22 * k;
    s2.geometry.dispose(); s2.material.dispose();
  }

  buildGround(net) {
    if (this.ground) { this.scene.remove(this.ground); this.ground.geometry.dispose(); this.ground.material.alphaMap?.dispose(); this.ground.material.dispose(); }
    const R = net.districtRaster, ext = R ? R.ext : (net.meta.size || 4096) * 0.8;
    const res = R ? R.res : 64;
    const geo = new THREE.PlaneGeometry(ext * 2, ext * 2, res - 1, res - 1);
    geo.rotateX(-Math.PI / 2);
    const col = new Float32Array(res * res * 3);
    for (let j = 0; j < res; j++) for (let i = 0; i < res; i++) {
      const t = R ? R.types[R.data[j * res + i] - 1] : 'rural';
      const c = DISTRICT_COLORS[t] || DISTRICT_COLORS.rural;
      const n = 0.94 + 0.12 * Math.sin(i * 0.37 + j * 0.21) * Math.cos(i * 0.13 - j * 0.29);
      const k = (j * res + i) * 3; col[k] = c[0] * n; col[k + 1] = c[1] * n; col[k + 2] = c[2] * n;
    }
    geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
    const detail = this.T.groundDetail.map.clone(); detail.needsUpdate = true;
    detail.repeat.set(ext * 2 / 9, ext * 2 / 9);
    const mat = new THREE.MeshStandardMaterial({ vertexColors: true, map: detail, roughness: 1, metalness: 0 });
    // river hole
    if (net.river) {
      const S = 2048, c = document.createElement('canvas'); c.width = c.height = S;
      const g = c.getContext('2d'); g.fillStyle = '#fff'; g.fillRect(0, 0, S, S);
      g.strokeStyle = '#000'; g.lineJoin = 'round'; g.lineCap = 'butt'; g.lineWidth = (net.river.width + 4) / (ext * 2) * S;
      g.beginPath();
      net.river.pts.forEach(([x, z], i) => { const px = (x + ext) / (ext * 2) * S, py = (z + ext) / (ext * 2) * S; i ? g.lineTo(px, py) : g.moveTo(px, py); });
      g.stroke();
      const t = new THREE.CanvasTexture(c);
      mat.alphaMap = t; mat.alphaTest = 0.5;
    }
    const m = this.ground = new THREE.Mesh(geo, mat);
    m.position.y = -0.06; m.receiveShadow = true;
    this.scene.add(m);
  }

  // ------------------------------------------------------------------ chunks
  chunkKey(out) {
    if (out.min[0] === Infinity) return '0,0';
    const cx = (out.min[0] + out.max[0]) / 2, cz = (out.min[2] + out.max[2]) / 2;
    return Math.floor(cx / CHUNK) + ',' + Math.floor(cz / CHUNK);
  }
  setNetwork(net, builder) {
    this.net = net; this.builder = builder;
    for (const ch of this.chunks.values()) this.disposeChunk(ch);
    for (const pc of this.propChunks.values()) this.disposePropChunk(pc);
    this.chunks.clear(); this.elemChunk.clear(); this.propChunks.clear(); this.elemProps.clear();
    for (const [id, out] of builder.outs) { this.assign(id, out); this.assignProps(id, out); }
    for (const ch of this.chunks.values()) this.buildChunk(ch);
    for (const pc of this.propChunks.values()) this.buildPropChunk(pc);
    this.buildGround(net);
    if (this.settings.view === 'topology') this.buildTopology();
  }
  assign(id, out) {
    const key = this.chunkKey(out);
    let ch = this.chunks.get(key);
    if (!ch) { const [i, j] = key.split(',').map(Number); ch = { key, elements: new Set(), group: null, center: new THREE.Vector3((i + 0.5) * CHUNK, 0, (j + 0.5) * CHUNK) }; this.chunks.set(key, ch); }
    ch.elements.add(id); this.elemChunk.set(id, key);
    return ch;
  }
  propKey(x, z) { return Math.floor(x / PCHUNK) + ',' + Math.floor(z / PCHUNK); }
  assignProps(id, out) {
    const keys = new Set();
    for (const t in out.props) { const l = out.props[t]; for (let i = 0; i < l.length; i += 8) keys.add(this.propKey(l[i], l[i + 2])); }
    for (const key of keys) {
      let pc = this.propChunks.get(key);
      if (!pc) { const [i, j] = key.split(',').map(Number); pc = { key, elements: new Set(), group: null, center: new THREE.Vector3((i + 0.5) * PCHUNK, 0, (j + 0.5) * PCHUNK) }; this.propChunks.set(key, pc); }
      pc.elements.add(id);
    }
    this.elemProps.set(id, keys);
    return keys;
  }
  // Rebuild only chunks touched by updated/removed elements
  applyUpdate({ updated = [], removed = [] }) {
    const dirty = new Set(), dirtyP = new Set();
    for (const id of [...updated, ...removed]) {
      const old = this.elemChunk.get(id);
      if (old) { this.chunks.get(old)?.elements.delete(id); this.elemChunk.delete(id); dirty.add(old); }
      const oldP = this.elemProps.get(id);
      if (oldP) { for (const k of oldP) { this.propChunks.get(k)?.elements.delete(id); dirtyP.add(k); } this.elemProps.delete(id); }
    }
    for (const id of updated) { const out = this.builder.outs.get(id); if (out) { dirty.add(this.assign(id, out).key); for (const k of this.assignProps(id, out)) dirtyP.add(k); } }
    for (const k of dirty) { const ch = this.chunks.get(k); if (ch) this.buildChunk(ch); }
    for (const k of dirtyP) { const pc = this.propChunks.get(k); if (pc) this.buildPropChunk(pc); }
    if (this.settings.view === 'topology') this.buildTopology();
  }
  disposePropChunk(pc) {
    if (!pc.group) return;
    this.roads.remove(pc.group);
    pc.group.traverse(o => { if (o.isInstancedMesh) o.dispose(); });
    pc.group = null;
  }
  disposeChunk(ch) {
    if (!ch.group) return;
    this.roads.remove(ch.group);
    ch.group.traverse(o => { if (o.isMesh && o.userData.ownGeo) o.geometry.dispose(); if (o.isInstancedMesh) o.dispose(); });
    ch.group = null;
  }
  buildChunk(ch) {
    this.disposeChunk(ch);
    const group = new THREE.Group(), roadG = new THREE.Group(), markG = new THREE.Group();
    group.add(roadG, markG);
    const byMat = new Map();
    const bmin = [Infinity, Infinity, Infinity], bmax = [-Infinity, -Infinity, -Infinity];
    for (const id of ch.elements) {
      const out = this.builder.outs.get(id); if (!out) continue;
      for (const mat in out.mesh) { const pc = out.mesh[mat]; if (!pc.i.length) continue; if (!byMat.has(mat)) byMat.set(mat, []); byMat.get(mat).push(pc); }
      for (let k = 0; k < 3; k++) { if (out.min[k] < bmin[k]) bmin[k] = out.min[k]; if (out.max[k] > bmax[k]) bmax[k] = out.max[k]; }
    }
    for (const [mat, pcs] of byMat) {
      let nv = 0, ni = 0; for (const pc of pcs) { nv += pc.p.length / 3; ni += pc.i.length; }
      const pos = new Float32Array(nv * 3), uv = new Float32Array(nv * 2), idx = nv > 65535 ? new Uint32Array(ni) : new Uint16Array(ni);
      let vo = 0, io = 0;
      for (const pc of pcs) { pos.set(pc.p, vo * 3); uv.set(pc.uv, vo * 2); for (let k = 0; k < pc.i.length; k++) idx[io + k] = pc.i[k] + vo; vo += pc.p.length / 3; io += pc.i.length; }
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
      geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
      geo.setIndex(new THREE.BufferAttribute(idx, 1));
      geo.computeVertexNormals();
      geo.computeBoundingSphere();
      const mesh = new THREE.Mesh(geo, this.M[mat] || this.M.__fallback);
      mesh.userData = { ownGeo: true, mat, chunk: ch.key };
      mesh.receiveShadow = true;
      mesh.castShadow = !MARK_MATS.has(mat) && mat !== 'water';
      if (mat === 'water') mesh.renderOrder = 2;
      (MARK_MATS.has(mat) ? markG : roadG).add(mesh);
    }
    ch.group = group; ch.roadG = roadG; ch.markG = markG; ch.bmin = bmin; ch.bmax = bmax;
    this.roads.add(group);
    this.applyChunkVisibility(ch, true);
  }
  buildPropChunk(pc) {
    this.disposePropChunk(pc);
    const group = new THREE.Group(), propG = new THREE.Group(), treeG = new THREE.Group();
    group.add(propG, treeG);
    const byProp = new Map(), key = pc.key;
    for (const id of pc.elements) {
      const out = this.builder.outs.get(id); if (!out) continue;
      for (const t in out.props) {
        const l = out.props[t], sel = [];
        for (let i = 0; i < l.length; i += 8) if (this.propKey(l[i], l[i + 2]) === key) for (let k = 0; k < 8; k++) sel.push(l[i + k]);
        if (sel.length) { if (!byProp.has(t)) byProp.set(t, []); byProp.get(t).push(sel); }
      }
    }
    const tmpM = new THREE.Matrix4(), q = new THREE.Quaternion(), e = new THREE.Euler(0, 0, 0, 'YZX'), p = new THREE.Vector3(), s = new THREE.Vector3();
    for (const [type, lists] of byProp) {
      const parts = this.props[type] || this.props.__fallback;
      let n = 0; for (const l of lists) n += l.length / 8;
      const mats = new Array(n); let k = 0;
      for (const l of lists) for (let i = 0; i < l.length; i += 8) {
        p.set(l[i], l[i + 1], l[i + 2]); e.set(0, l[i + 3], l[i + 4]); q.setFromEuler(e); s.set(l[i + 5], l[i + 6], l[i + 7]);
        mats[k++] = tmpM.clone().compose(p, q, s);
      }
      const isTree = type === 'treeA' || type === 'treeB';
      for (const part of parts) {
        const im = new THREE.InstancedMesh(part.geo, this.PM[part.mat] || this.PM.vcol, n);
        for (let i = 0; i < n; i++) im.setMatrixAt(i, mats[i]);
        im.instanceMatrix.needsUpdate = true;
        im.computeBoundingSphere();
        im.castShadow = SHADOW_PROPS.has(type) && part.mat !== 'glass';
        im.receiveShadow = false;
        im.userData = { prop: type };
        (isTree ? treeG : propG).add(im);
      }
    }
    pc.group = group; pc.propG = propG; pc.treeG = treeG; pc.prop = true;
    this.roads.add(group);
    this.applyChunkVisibility(pc, true);
  }

  applyChunkVisibility(ch, force = false) {
    if (!ch.group) return;
    const c = this.camera.position;
    let dx, dz;
    if (ch.prop) { dx = Math.max(0, Math.abs(c.x - ch.center.x) - PCHUNK / 2); dz = Math.max(0, Math.abs(c.z - ch.center.z) - PCHUNK / 2); }
    else { dx = Math.max(0, ch.bmin[0] - c.x, c.x - ch.bmax[0]); dz = Math.max(0, ch.bmin[2] - c.z, c.z - ch.bmax[2]); }
    const d = Math.hypot(dx, dz, Math.max(0, c.y - 50) * 0.5);
    const S = this.settings, under = S.view === 'underground';
    if (ch.prop) {
      const pv = S.props && d < S.propDist && !under, tv = S.trees && d < S.propDist * 1.5 && !under;
      if (force || ch.propG.visible !== pv) ch.propG.visible = pv;
      if (force || ch.treeG.visible !== tv) ch.treeG.visible = tv;
    } else {
      const mv = S.markings && d < S.markDist && !under;
      if (force || ch.markG.visible !== mv) ch.markG.visible = mv;
    }
  }
  allChunks() { return [...this.chunks.values(), ...this.propChunks.values()]; }

  // ------------------------------------------------------------------ view modes
  setView(mode) {
    this.settings.view = mode;
    const under = mode === 'underground';
    const clay = mode === 'topology';
    if (this.ground) this.ground.visible = !under;
    for (const [k, m] of Object.entries(this.M)) {
      if (k === '__fallback') continue;
      const keep = ['tunnel', 'platform', 'platformEdge'].includes(k);
      if (!m.userData.base) m.userData.base = { transparent: m.transparent, opacity: m.opacity, depthWrite: m.depthWrite, map: m.map, color: m.color.clone() };
      const b = m.userData.base;
      if (under && !keep) { m.transparent = true; m.opacity = 0.12; m.depthWrite = false; }
      else { m.transparent = b.transparent; m.opacity = b.opacity; m.depthWrite = b.depthWrite; }
      if (clay && !MARK_MATS.has(k)) { m.map = null; m.color.set(0x8a8c90); } else { m.map = b.map; m.color.copy(b.color); }
      m.needsUpdate = true;
    }
    this.topoGroup.visible = clay;
    if (clay) this.buildTopology();
    for (const ch of this.allChunks()) this.applyChunkVisibility(ch, true);
  }
  buildTopology() {
    const g = this.topoGroup; g.clear();
    if (!this.net) return;
    const byCol = new Map();
    for (const e of this.net.edges.values()) {
      const c = this.net.centerline(e), prof = getProfile(e), col = CATEGORY_COLORS[prof.category] || '#fff';
      if (!byCol.has(col)) byCol.set(col, []);
      const arr = byCol.get(col);
      for (let i = 0; i < c.X.length - 1; i++) arr.push(c.X[i], c.Y[i] + 0.6, c.Z[i], c.X[i + 1], c.Y[i + 1] + 0.6, c.Z[i + 1]);
    }
    for (const [col, arr] of byCol) {
      const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.Float32BufferAttribute(arr, 3));
      g.add(new THREE.LineSegments(geo, new THREE.LineBasicMaterial({ color: col, depthTest: false, transparent: true, opacity: 0.95 })));
    }
    const KC = { junction: '#4fd8e0', roundabout: '#f59e0b', culdesac: '#a78bfa', continuation: '#555', flush: '#ef4444', flat: '#ef4444', none: '#888' };
    const pts = new Map();
    for (const n of this.net.nodes.values()) {
      const J = this.builder?.junctions.get(n.id); const k = J ? J.kind : 'none';
      if (!pts.has(k)) pts.set(k, []); pts.get(k).push(n.x, n.y + 0.8, n.z);
    }
    for (const [k, arr] of pts) {
      const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.Float32BufferAttribute(arr, 3));
      g.add(new THREE.Points(geo, new THREE.PointsMaterial({ color: KC[k] || '#fff', size: k === 'continuation' ? 3 : 6, sizeAttenuation: false, depthTest: false })));
    }
    g.traverse(o => { o.renderOrder = 20; });
  }
  setSetting(k, v) {
    this.settings[k] = v;
    if (k === 'shadows') { this.sun.castShadow = v; }
    if (k === 'hour') this.setHour(v);
    for (const ch of this.allChunks()) this.applyChunkVisibility(ch, true);
  }

  // ------------------------------------------------------------------ overlays
  clearGroup(g) { g.traverse(o => { if (o.geometry) o.geometry.dispose(); if (o.material && o.material !== this._ovMat && o.material !== this._ovLine) o.material.dispose?.(); }); g.clear(); }
  edgeBand(edge, color, opacity, lift = 0.25) {
    const c = this.net.centerline(edge), half = layout(getProfile(edge)).total / 2;
    const pos = [], idx = [];
    const n = c.X.length;
    for (let i = 0; i < n; i++) {
      const f = c.frameAt(c.S[i]);
      pos.push(f.x - f.nx * half, f.y + lift, f.z - f.nz * half, f.x + f.nx * half, f.y + lift, f.z + f.nz * half);
      if (i < n - 1) { const a = i * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
    }
    const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3)); geo.setIndex(idx);
    const mesh = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color, transparent: true, opacity, depthWrite: false, side: THREE.DoubleSide, polygonOffset: true, polygonOffsetFactor: -8, polygonOffsetUnits: -8 }));
    mesh.renderOrder = 15;
    // outline
    const lp = [];
    for (let i = 0; i < n; i++) { const f = c.frameAt(c.S[i]); lp.push(f.x - f.nx * half, f.y + lift + 0.05, f.z - f.nz * half); }
    for (let i = n - 1; i >= 0; i--) { const f = c.frameAt(c.S[i]); lp.push(f.x + f.nx * half, f.y + lift + 0.05, f.z + f.nz * half); }
    lp.push(lp[0], lp[1], lp[2]);
    const lg = new THREE.BufferGeometry(); lg.setAttribute('position', new THREE.Float32BufferAttribute(lp, 3));
    const line = new THREE.Line(lg, new THREE.LineBasicMaterial({ color, depthTest: false, transparent: true }));
    line.renderOrder = 16;
    return [mesh, line];
  }
  handle(x, y, z, r, color, ring = false) {
    const geo = ring ? new THREE.RingGeometry(r * 0.7, r, 28) : new THREE.CircleGeometry(r, 24);
    geo.rotateX(-Math.PI / 2);
    const m = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.95, side: THREE.DoubleSide }));
    m.position.set(x, y + 0.4, z); m.renderOrder = 18;
    return m;
  }
  // selection: {type:'edge'|'node'|'street'|'rail'|'subway', id, ids?}
  setSelection(sel, accent = 0x4fd8e0) {
    this.clearGroup(this.overlay);
    if (!sel || !this.net) return;
    const g = this.overlay;
    if (sel.type === 'edge' || sel.type === 'street') {
      const ids = sel.type === 'street' ? sel.ids : [sel.id];
      for (const id of ids) {
        const e = this.net.edges.get(id); if (!e) continue;
        g.add(...this.edgeBand(e, accent, sel.type === 'street' ? 0.16 : 0.22));
        if (sel.type === 'edge') {
          for (const nid of [e.a, e.b]) { const n = this.net.nodes.get(nid); g.add(this.handle(n.x, n.y, n.z, 2.4, 0xffffff, true)); }
          e.pts.forEach((p, i) => { const h = this.handle(p.x, p.y ?? this.net.centerline(e).frameAt(this.net.centerline(e).nearest(p.x, p.z).s).y, p.z, 1.5, accent); h.userData = { ctrl: i, edge: e.id }; g.add(h); });
        }
      }
    } else if (sel.type === 'node') {
      const n = this.net.nodes.get(sel.id); if (!n) return;
      for (const e of this.net.nodeEdges(n.id)) g.add(...this.edgeBand(e, accent, 0.1));
      g.add(this.handle(n.x, n.y, n.z, 3.2, accent, true));
      g.add(this.handle(n.x, n.y, n.z, 1.2, 0xffffff));
    } else if (sel.type === 'rail' || sel.type === 'subway') {
      const L = (sel.type === 'rail' ? this.net.rails : this.net.subways).get(sel.id); if (!L) return;
      const y = sel.type === 'rail' ? 1.5 : -L.depth + 1;
      const lp = []; for (const p of L.pts) lp.push(p.x, (p.y ?? 0) + y, p.z);
      const lg = new THREE.BufferGeometry(); lg.setAttribute('position', new THREE.Float32BufferAttribute(lp, 3));
      const line = new THREE.Line(lg, new THREE.LineBasicMaterial({ color: accent, depthTest: false })); line.renderOrder = 16; g.add(line);
    }
  }
  setHover(h) {
    this.clearGroup(this.hoverGroup);
    if (!h || !this.net) return;
    if (h.type === 'edge') { const e = this.net.edges.get(h.id); if (e) this.hoverGroup.add(...this.edgeBand(e, 0xffffff, 0.08, 0.2)); }
    if (h.type === 'node') { const n = this.net.nodes.get(h.id); if (n) this.hoverGroup.add(this.handle(n.x, n.y, n.z, 2.8, 0xffffff, true)); }
    if (h.type === 'point') this.hoverGroup.add(this.handle(h.x, h.y, h.z, h.r || 1.6, h.color ?? 0xffffff, true));
  }
  // Drawing preview: polyline points + ghost band
  setPreview(points, width, color = 0x4fd8e0, ok = true) {
    this.clearGroup(this.previewGroup);
    if (!points || points.length < 1) return;
    const g = this.previewGroup;
    for (const p of points) g.add(this.handle(p.x, p.y || 0, p.z, 1.4, ok ? 0xffffff : 0xef4444));
    if (points.length < 2) return;
    const lp = []; for (const p of points) lp.push(p.x, (p.y || 0) + 0.5, p.z);
    const lg = new THREE.BufferGeometry(); lg.setAttribute('position', new THREE.Float32BufferAttribute(lp, 3));
    const line = new THREE.Line(lg, new THREE.LineDashedMaterial({ color: 0xffffff, dashSize: 3, gapSize: 2, depthTest: false }));
    line.computeLineDistances(); line.renderOrder = 19; g.add(line);
    if (points.curve) {
      const c = points.curve, half = width / 2, pos = [], idx = [];
      const n = c.X.length;
      for (let i = 0; i < n; i++) { const f = c.frameAt(c.S[i]); pos.push(f.x - f.nx * half, f.y + 0.3, f.z - f.nz * half, f.x + f.nx * half, f.y + 0.3, f.z + f.nz * half); if (i < n - 1) { const a = i * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); } }
      const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3)); geo.setIndex(idx);
      const m = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color: ok ? color : 0xef4444, transparent: true, opacity: 0.3, depthWrite: false, side: THREE.DoubleSide, depthTest: false }));
      m.renderOrder = 17; g.add(m);
    }
  }

  // ------------------------------------------------------------------ picking
  ray(clientX, clientY) {
    const r = this.canvas.getBoundingClientRect();
    const v = new THREE.Vector2(((clientX - r.left) / r.width) * 2 - 1, -((clientY - r.top) / r.height) * 2 + 1);
    this.raycaster.setFromCamera(v, this.camera);
    return this.raycaster.ray;
  }
  planePoint(ray, y = 0) {
    if (Math.abs(ray.direction.y) < 1e-6) return null;
    const t = (y - ray.origin.y) / ray.direction.y;
    if (t < 0) return null;
    return ray.at(t, new THREE.Vector3());
  }
  // Raycast against road meshes (surfaces only) — returns world point or null
  pickSurface(clientX, clientY) {
    this.ray(clientX, clientY);
    const targets = [];
    for (const ch of this.chunks.values()) if (ch.roadG) for (const m of ch.roadG.children) if (PICK_MATS.has(m.userData.mat)) targets.push(m);
    if (this.ground?.visible) targets.push(this.ground);
    this.raycaster.far = 8000;
    const hit = this.raycaster.intersectObjects(targets, false)[0];
    return hit ? hit.point : null;
  }

  focus(x, y, z, dist = 120) { this.controls.focus(new THREE.Vector3(x, y, z), dist); }

  // ------------------------------------------------------------------ loop
  resize() {
    const p = this.canvas.parentElement; if (!p) return;
    const w = p.clientWidth, h = p.clientHeight; if (!w || !h) return;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h; this.camera.updateProjectionMatrix();
  }
  loop() {
    if (!this.running) return;
    requestAnimationFrame(this.loop);
    const dt = Math.min(this.clock.getDelta(), 0.1);
    this.controls.update(dt);
    const cam = this.camera.position;
    // shadows follow camera focus (snap to texel grid to avoid shimmering)
    const fw = new THREE.Vector3(); this.camera.getWorldDirection(fw);
    const reach = THREE.MathUtils.clamp(cam.y * 1.2, 40, 180);
    const tx = cam.x + fw.x * reach, tz = cam.z + fw.z * reach;
    const texel = 520 / 2048, sx = Math.round(tx / texel) * texel, sz = Math.round(tz / texel) * texel;
    this.sun.target.position.set(sx, 0, sz);
    this.sun.position.set(sx + this.sunDir.x * 700, this.sunDir.y * 700 + 1, sz + this.sunDir.z * 700);
    this.sky.position.copy(cam);
    this.M.water.normalMap && this.M.water.normalMap.offset.set(this.clock.elapsedTime * 0.004, this.clock.elapsedTime * 0.006);
    this._vis = (this._vis || 0) + 1;
    if (this._vis % 8 === 0) for (const ch of this.allChunks()) this.applyChunkVisibility(ch);
    for (const cb of this.frameCbs) cb(dt);
    this.renderer.info.reset();
    this.renderer.render(this.scene, this.camera);
    this._ft += dt; this._fc++;
    if (this._ft > 0.5) { this.stats.fps = Math.round(this._fc / this._ft); this._ft = 0; this._fc = 0; this.stats.tris = this.renderer.info.render.triangles; this.stats.calls = this.renderer.info.render.calls; }
  }
  dispose() { this.running = false; this.ro.disconnect(); this.controls.dispose(); this.renderer.dispose(); }
}
