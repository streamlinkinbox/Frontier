/* ============================================================
   FRONTIER · ROAD EDITOR — three.js viewport
   Bucket → material, prop → InstancedMesh, Unreal-style camera,
   lit / unlit / material-ID / wireframe view modes, day–night sun,
   exact CPU picking, x-ray for the metro, minimap data, traffic sim.
   ============================================================ */

import * as THREE from '../vendor/three.module.js';
import { buildMaterials, buildGlyphs, buildEnvironment, buildRadial } from './textures.js';
import { PROP_SPECS, PROP_MATERIALS } from './props.js';
import { buildAll, buildRoad, framesOf, prepareRoad, profileOf, BuildCtx, KERB_SECTION } from './mesh.js';
import { levelAt, solveSection, CLASS_COLOR, PROFILES } from './profiles.js';
import { RNG, clamp, lerp, dist, strSeed, TAU } from './lib.js';

/* ------------------------------- layer bookkeeping ------------------------------- */
export const LAYERS = [
  { key: 'surfaces', label: 'Pavement & carriageway' },
  { key: 'kerb', label: 'Kerbs & stone edging' },
  { key: 'paint', label: 'Road markings' },
  { key: 'furniture', label: 'Street furniture' },
  { key: 'lighting', label: 'Lighting' },
  { key: 'greenery', label: 'Trees & landscaping' },
  { key: 'safety', label: 'Safety & barriers' },
  { key: 'transit', label: 'Transit & metro' },
  { key: 'rail', label: 'Railway' },
  { key: 'signing', label: 'Signing & works' },
  { key: 'water', label: 'Water & quay' },
  { key: 'massing', label: 'Context massing' },
  { key: 'underground', label: 'Underground (x-ray)' },
  { key: 'traffic', label: 'Traffic' },
];
const BUCKET_LAYER = {
  'pav:asphalt': 'surfaces', 'pav:concrete': 'surfaces', 'pav:paver': 'surfaces', 'pav:brick': 'surfaces',
  'pav:cobble': 'surfaces', 'pav:gravel': 'surfaces', 'pav:tile': 'surfaces', 'pav:timber': 'surfaces',
  'pav:soil': 'surfaces', 'pav:rubber': 'surfaces', 'pav:grass': 'greenery', 'pav:granite': 'kerb',
  'pav:metal': 'safety', 'pav:water': 'water', 'pav:paint': 'paint', '__massing': 'massing', '__underground': 'underground'
};
const CAT_LAYER = {
  Delineation: 'furniture', Lighting: 'lighting', Greenery: 'greenery', Street: 'furniture',
  Transit: 'transit', 'Traffic calming': 'safety', Works: 'safety', Safety: 'safety', Signing: 'signing',
  Rail: 'rail', Rural: 'rail', Pedestrian: 'furniture', Traffic: 'traffic', Massing: 'massing', Structures: 'safety'
};

const PAVE = {
  'pav:asphalt': { rough: .94, metal: 0, tex: 'asphalt', env: .3, tilt: .95 },
  'pav:concrete': { rough: .84, metal: 0, tex: 'concrete', env: .26, tilt: .8 },
  'pav:paver': { rough: .78, metal: 0, tex: 'paver', env: .24, tilt: 1.1 },
  'pav:brick': { rough: .8, metal: 0, tex: 'brick', env: .2, tilt: 1.05 },
  'pav:cobble': { rough: .6, metal: 0, tex: 'cobble', env: .55, tilt: 1.3 },
  'pav:granite': { rough: .68, metal: 0, tex: 'granite', env: .3, tilt: .8 },
  'pav:gravel': { rough: .97, metal: 0, tex: 'gravel', env: .1, tilt: 1.4 },
  'pav:grass': { rough: .95, metal: 0, tex: 'grass', env: .08, tilt: 1.1 },
  'pav:tile': { rough: .28, metal: 0, tex: 'tile', env: .85, tilt: .6 },
  'pav:timber': { rough: .8, metal: 0, tex: 'timber', env: .2, tilt: .9 },
  'pav:metal': { rough: .4, metal: .92, tex: 'metal', env: 1, tilt: .7 },
  'pav:water': { rough: .07, metal: .18, tex: 'water', env: 1.2, tilt: .5, transparent: true, opacity: .9 },
  'pav:rubber': { rough: .93, metal: 0, tex: 'asphalt', env: .16, tilt: 1.2 },
  'pav:soil': { rough: .97, metal: 0, tex: 'grass', env: .06, tilt: 1.2 },
  'pav:paint': { rough: .46, metal: 0, tex: 'paint', env: .5, tilt: .35 },
};

export class RoadScene {
  constructor(host, handlers = {}) {
    this.host = host; this.h = handlers;
    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance', preserveDrawingBuffer: true });
    this.renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 2));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.05;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.domElement.style.cssText = 'display:block;width:100%;height:100%;outline:none';
    this.renderer.domElement.tabIndex = 0;
    host.appendChild(this.renderer.domElement);

    this.scene = new THREE.Scene();
    this.scene.fog = new THREE.FogExp2(0x121a24, .0011);
    this.camera = new THREE.PerspectiveCamera(55, 16 / 9, .4, 4600);
    this.cam = { pos: new THREE.Vector3(260, 96, 340), yaw: -2.5, pitch: -.19, speed: 18 };
    this.orbit = { target: new THREE.Vector3(), dist: 260, theta: .8, phi: 1.02 };
    this.mode = 'fly';

    this.groups = { static: new THREE.Group(), decal: new THREE.Group(), props: new THREE.Group(), under: new THREE.Group(), overlay: new THREE.Group(), ghost: new THREE.Group(), agents: new THREE.Group() };
    for (const [k, g] of Object.entries(this.groups)) { g.name = k; this.scene.add(g); }

    this.view = 'lit';                       // lit | unlit | id | wire
    this.flags = { shadows: true, ground: true, grid: true, xray: false, marks: true, props: true, traffic: true, massing: true, detail: 1 };
    this.layers = Object.create(null); for (const L of LAYERS) this.layers[L.key] = true;
    this.selected = null; this._hoverId = null;
    this.drawn = []; this.instanced = [];
    this.traffic = []; this.gates = [];
    this._clock = new THREE.Clock();
    this.keys = Object.create(null);
    this.pointer = { down: 0, dragBtn: -1, lastX: 0, lastY: 0, moved: 0, nx: 0, ny: 0 };
    this._bind();
  }

  /* ------------------------------- boot ------------------------------- */
  init() {
    this.textures = buildMaterials();
    this.glyphs = buildGlyphs();
    this._materials();
    this._lights();
    this._ground();
    this.setSun({ azimuth: 128, elevation: 38, exposure: 1.05 });
    this._resize();
    if (typeof ResizeObserver !== 'undefined') new ResizeObserver(() => this._resize()).observe(this.host);
    else addEventListener('resize', () => this._resize());
    this.renderer.setAnimationLoop(() => this._frame());
    return this;
  }
  _materials() {
    const M = this.M = {};
    for (const [key, d] of Object.entries(PAVE)) {
      const t = this.textures[d.tex];
      const lit = new THREE.MeshStandardMaterial({
        color: 0xffffff, vertexColors: true, roughness: d.rough, metalness: d.metal,
        map: t?.map ?? null, normalMap: t?.normalMap ?? null, roughnessMap: t?.roughnessMap ?? null, envMapIntensity: d.env,
        transparent: !!d.transparent, opacity: d.opacity ?? 1
      });
      if (t?.normalMap) lit.normalScale = new THREE.Vector2(d.tilt ?? 1, d.tilt ?? 1);
      if (key === 'pav:paint') { lit.polygonOffset = true; lit.polygonOffsetFactor = -3; lit.polygonOffsetUnits = -4; }
      M[key] = {
        lit,
        unlit: new THREE.MeshBasicMaterial({ color: 0xd8d8d8, vertexColors: false }),
        id: new THREE.MeshBasicMaterial({ color: 0xffffff, vertexColors: true })
      };
    }
    for (const name of Object.keys(this.glyphs)) {
      const lit = new THREE.MeshStandardMaterial({ color: 0xf4f4ef, roughness: .5, metalness: 0, map: null, alphaMap: this.glyphs[name], alphaTest: .3, transparent: true, depthWrite: false });
      lit.polygonOffset = true; lit.polygonOffsetFactor = -5; lit.polygonOffsetUnits = -6;
      M['glyph:' + name] = { lit, unlit: lit, id: new THREE.MeshBasicMaterial({ map: this.glyphs[name], transparent: true, alphaTest: .25, depthWrite: false }) };
    }
    for (const [mkey, spec] of Object.entries(PROP_MATERIALS)) {
      const t = spec.tex ? this.textures[spec.tex] : null;
      const glassy = spec.opacity !== undefined && spec.opacity < 1;
      const lit = new THREE.MeshStandardMaterial({
        color: new THREE.Color(...(spec.tint || [.6, .6, .6])), roughness: spec.rough ?? .7, metalness: spec.metal ?? 0,
        map: t?.map ?? null, normalMap: t?.normalMap ?? null, roughnessMap: t?.roughnessMap ?? null, envMapIntensity: spec.metal ? 1 : .45,
        transparent: !!glassy, opacity: glassy ? spec.opacity : 1,
        emissive: spec.emissive ? new THREE.Color(...spec.emissive) : new THREE.Color(0, 0, 0), emissiveIntensity: .6,
        side: spec.kind === 'foliage' || spec.kind === 'fence' ? THREE.DoubleSide : THREE.FrontSide
      });
      if (spec.kind === 'fence') { lit.alphaMap = this.textures.mesh?.map; lit.alphaTest = .32; }
      if (spec.kind === 'water') { lit.transparent = true; lit.opacity = .82; }
      M['prop:' + mkey] = { lit, unlit: new THREE.MeshBasicMaterial({ color: new THREE.Color(...(spec.tint || [.6, .6, .6])) }), id: null };
    }
  }
  _lights() {
    this.sun = new THREE.DirectionalLight(0xffe8cd, 2.6);
    this.sun.castShadow = true;
    Object.assign(this.sun.shadow, { bias: -.0006, normalBias: .07 });
    this.sun.shadow.mapSize.set(2048, 2048);
    const c = this.sun.shadow.camera;
    c.near = 2; c.far = 1600; c.left = -300; c.right = 300; c.top = 300; c.bottom = -300;
    this.scene.add(this.sun, this.sun.target);
    this.hemi = new THREE.HemisphereLight(0x9fbcd9, 0x39382f, .8); this.scene.add(this.hemi);
    this.amb = new THREE.AmbientLight(0x24303f, .3); this.scene.add(this.amb);
  }
  _ground() {
    const t = this.textures.grass;
    const map = t?.map.clone(), nrm = t?.normalMap.clone();
    for (const m of [map, nrm]) if (m) { m.repeat.set(300, 300); m.needsUpdate = true; }
    this.ground = new THREE.Mesh(new THREE.PlaneGeometry(4600, 4600), new THREE.MeshStandardMaterial({
      color: 0x555b49, roughness: .97, metalness: 0, map, normalMap: nrm, envMapIntensity: .18
    }));
    this.ground.rotation.x = -Math.PI / 2; this.ground.position.y = -.09; this.ground.receiveShadow = true;
    this.scene.add(this.ground);
    this.grid = new THREE.GridHelper(2400, 240, 0x30425f, 0x1d2534);
    this.grid.material.transparent = true; this.grid.material.opacity = .42; this.grid.position.y = .006;
    this.gridBig = new THREE.GridHelper(2400, 24, 0x4a6699, 0x2b3a55);
    this.gridBig.material.transparent = true; this.gridBig.material.opacity = .72; this.gridBig.position.y = .01;
    this.scene.add(this.grid, this.gridBig);
  }
  setSun({ azimuth = 128, elevation = 38, exposure = 1.05 } = {}) {
    this.sunParams = { azimuth, elevation, exposure };
    const el = clamp(elevation, -12, 88) * Math.PI / 180;
    const a = azimuth * Math.PI / 180;
    const day = clamp(Math.sin(el), 0, 1);
    this.day = day; this.night = 1 - day;
    this.sun.position.set(Math.cos(a) * Math.cos(el) * 700, Math.max(40, Math.sin(el) * 700), Math.sin(a) * Math.cos(el) * 700);
    this.sun.intensity = .1 + day * 3.2;
    this.sun.color.setHSL(.1 - (1 - day) * .045, .62 - (1 - day) * .3, .48 + day * .4);
    this.hemi.intensity = .18 + day * 1.0;
    this.amb.intensity = .1 + (1 - day) * .34;
    this.renderer.toneMappingExposure = exposure * (1 + (1 - day) * .22);
    this.envTex?.dispose?.();
    this.envTex = buildEnvironment({ azimuth, elevation: el * 180 / Math.PI, intensity: .6 + day }, day);
    this.scene.environment = this.envTex;
    const top = mixc([.032, .042, .07], [.14, .32, .64], day);
    const hz = mixc([.055, .06, .085], [.82, .68, .52], Math.pow(day, .65));
    if (!this.sky) {
      this.sky = new THREE.Mesh(new THREE.SphereGeometry(3600, 36, 22), new THREE.ShaderMaterial({
        side: THREE.BackSide, depthWrite: false, fog: false,
        uniforms: { top: { value: new THREE.Color() }, hz: { value: new THREE.Color() }, sunDir: { value: new THREE.Vector3() }, day: { value: 1 } },
        vertexShader: 'varying vec3 vP; void main(){ vP=normalize(position); gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.); }',
        fragmentShader: `varying vec3 vP; uniform vec3 top,hz,sunDir; uniform float day;
          void main(){
            float y=vP.y;
            vec3 c = y>0.0 ? mix(hz, top, pow(y,.55)) : mix(hz, vec3(.02,.022,.03), pow(min(-y*3.,1.),.6));
            float sd = max(dot(vP, normalize(sunDir)),0.);
            c += vec3(1.,.86,.66)*pow(sd,340.)*4.5*day + vec3(1.,.8,.6)*pow(sd,8.)*.18*day;
            float grid = 0.;
            float star = step(.99975, fract(sin(dot(floor(vP.xz*500.), vec2(12.9898,78.233)))*43758.5453)) * (1.-day) * max(y,0.);
            gl_FragColor = vec4(c + star*vec3(.9,.94,1.), 1.);
          }`
      }));
      this.scene.add(this.sky);
    }
    const u = this.sky.material.uniforms;
    u.top.value.setRGB(...top); u.hz.value.setRGB(...hz); u.day.value = day;
    u.sunDir.value.copy(this.sun.position).normalize();
    this.scene.fog.color.setRGB(...mixc([.05, .06, .08], [.6, .64, .68], day));
    this.scene.fog.density = .00075 + (1 - day) * .0008;
    for (const e of this.emissiveMats || []) if (!e.blink && e.m) e.m.emissiveIntensity = .18 + this.night * (e.strong ? 3.4 : 2.2);
    this.h.onSun?.(this.sunParams, day);
    return day;
  }

  /* ------------------------------- build ------------------------------- */
  setNetwork(network, opts = {}) {
    this.network = network;
    for (const key of ['static', 'decal', 'props', 'under', 'agents']) this._clear(this.groups[key]);
    this.drawn = []; this.instanced = []; this.gates = []; this.emissiveMats = [];
    const { ctx, stats } = buildAll(network, { ...opts, detail: this.flags.detail, groundY: () => 0 });
    this.ctx = ctx;
    this._emitBuckets(ctx.b, this.groups.static, false);
    this._emitBuckets(ctx.underground, this.groups.under, true);
    this._emitBuckets(ctx.decal, this.groups.decal, false, true);
    this._emitProps(ctx);
    this._emitMassing(network);
    this._pickData(network);
    this._buildTraffic(network);
    this.applyView();
    this.stats = stats;
    this.h.onBuilt?.(network, ctx, stats);
    return stats;
  }
  _geoFrom(g) {
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(g.pos, 3));
    geo.setAttribute('normal', new THREE.Float32BufferAttribute(g.nrm, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(g.uv, 2));
    geo.setAttribute('color', new THREE.Float32BufferAttribute(g.col, 3));
    geo.setIndex(g.idx);
    geo.computeBoundingSphere();
    return geo;
  }
  _emitBuckets(builder, group, underground, decal = false) {
    for (const [key, g] of builder.groups) {
      if (!g.n) continue;
      const set = this.M[key] || this.M['pav:concrete'];
      const mats = { lit: set.lit.clone(), unlit: set.unlit.clone(), id: (set.id || set.lit).clone() };
      if (underground) for (const m of Object.values(mats)) { m.transparent = true; m.opacity = .95; }
      const mesh = new THREE.Mesh(this._geoFrom(g), mats.lit);
      mesh.receiveShadow = true;
      mesh.castShadow = !decal && !underground && key !== 'pav:asphalt' && key !== 'pav:grass';
      mesh.userData = { bucket: key, underground, decal, layer: underground ? '__underground' : (BUCKET_LAYER[key] || 'surfaces') };
      group.add(mesh);
      this.drawn.push({ mesh, mats });
    }
  }
  _protoGeo(proto) {
    this._geoCache = this._geoCache || Object.create(null);
    if (this._geoCache[proto]) return this._geoCache[proto];
    const spec = PROP_SPECS[proto];
    if (!spec) return null;
    const byMat = new Map();
    for (const part of spec.parts) {
      let geo;
      if (part.g === 'box') geo = new THREE.BoxGeometry(part.a[0], part.a[1], part.a[2]);
      else if (part.g === 'cyl') geo = new THREE.CylinderGeometry(part.a[0], part.a[1], part.a[2], part.a[3] || 8);
      else if (part.g === 'sph') geo = new THREE.SphereGeometry(part.a[0], part.a[1] || 12, part.a[2] || 8);
      else if (part.g === 'cone') geo = new THREE.ConeGeometry(part.a[0], part.a[1], part.a[2] || 12);
      else continue;
      const m = new THREE.Matrix4().compose(
        new THREE.Vector3(...(part.at || [0, 0, 0])),
        new THREE.Quaternion().setFromEuler(new THREE.Euler(part.rot?.[0] || 0, part.rot?.[1] || 0, part.rot?.[2] || 0)),
        new THREE.Vector3(...(part.s || [1, 1, 1])));
      geo.applyMatrix4(m);
      const ng = geo.toNonIndexed(); geo.dispose();
      const acc = byMat.get(part.m) || { pos: [], nrm: [], uv: [], idx: [], n: 0 };
      const p = ng.getAttribute('position'), nn = ng.getAttribute('normal'), uu = ng.getAttribute('uv');
      for (let i = 0; i < p.count; i++) {
        acc.pos.push(p.getX(i), p.getY(i), p.getZ(i));
        acc.nrm.push(nn.getX(i), nn.getY(i), nn.getZ(i));
        acc.uv.push(uu.getX(i), uu.getY(i));
        acc.idx.push(acc.n + i);
      }
      acc.n += p.count;
      byMat.set(part.m, acc);
      ng.dispose();
    }
    const out = [];
    for (const [mkey, g] of byMat) {
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.Float32BufferAttribute(g.pos, 3));
      geo.setAttribute('normal', new THREE.Float32BufferAttribute(g.nrm, 3));
      geo.setAttribute('uv', new THREE.Float32BufferAttribute(g.uv, 2));
      geo.setIndex(g.idx);
      geo.computeBoundingSphere();
      out.push({ mkey, geo });
    }
    this._geoCache[proto] = out;
    return out;
  }
  _emitProps(ctx) {
    const dummy = new THREE.Object3D();
    for (const [proto, list] of ctx.props) {
      if (!list.length) continue;
      const parts = this._protoGeo(proto);
      if (!parts) continue;
      const spec = PROP_SPECS[proto];
      /* blinking beacons are split into two buckets so crossings do not pulse in lockstep */
      const buckets = list.some(t => t.blink !== undefined) ? [[0, 1].map(p => list.filter(t => (t.blink | 0) % 2 === p))] : [list];
      buckets.forEach((sub, phase) => {
        if (!sub.length) return;
        for (const { mkey, geo } of parts) {
          const set = this.M['prop:' + mkey];
          if (!set) continue;
          const mats = { lit: set.lit.clone(), unlit: set.unlit.clone(), id: (set.id || set.lit).clone() };
          const im = new THREE.InstancedMesh(geo, mats.lit, sub.length);
          for (let i = 0; i < sub.length; i++) {
            const t = sub[i];
            dummy.position.set(t.x, t.y, t.z); dummy.rotation.set(0, t.rotY, 0); dummy.scale.set(t.sx || 1, t.sy || 1, t.sz || 1);
            dummy.updateMatrix(); im.setMatrixAt(i, dummy.matrix);
            if (t.color) im.setColorAt(i, new THREE.Color(t.color));
          }
          im.instanceMatrix.needsUpdate = true;
          if (im.instanceColor) im.instanceColor.needsUpdate = true;
          im.castShadow = !!spec.shadow; im.receiveShadow = true;
          im.userData = { proto, part: mkey, layer: CAT_LAYER[spec.cat] || 'furniture', blink: buckets.length > 1 ? phase : -1 };
          this.groups.props.add(im);
          this.instanced.push(im);
          this.drawn.push({ mesh: im, mats });
          const ms = PROP_MATERIALS[mkey];
          if (ms?.emissive || /lamp|signal|beacon|head/.test(mkey)) this.emissiveMats.push({ m: mats.lit, blink: im.userData.blink, strong: !!ms?.emissive });
        }
      });
    }
    /* animated arms: one InstancedMesh per (prototype, material) with all pivots inside */
    const anim = ctx.animated || [];
    if (anim.length) {
      const byProto = new Map();
      for (const a of anim) (byProto.get(a.proto) || byProto.set(a.proto, []).get(a.proto)).push(a);
      for (const [proto, items] of byProto) {
        for (const { mkey, geo } of (this._protoGeo(proto) || [])) {
          const set = this.M['prop:' + mkey];
          if (!set) continue;
          const mats = { lit: set.lit.clone(), unlit: set.unlit.clone(), id: (set.id || set.lit).clone() };
          const im = new THREE.InstancedMesh(geo, mats.lit, items.length);
          im.userData.items = items; im.castShadow = true; im.receiveShadow = true;
          im.userData.layer = CAT_LAYER[(PROP_SPECS[proto] || {}).cat] || 'safety';
          im.frustumCulled = false;
          this.groups.props.add(im);
          this.gates.push(im);
          this.drawn.push({ mesh: im, mats });
        }
      }
    }
    this._gateT = 0;
    const glowTex = buildRadial();
    this.glowMat = new THREE.SpriteMaterial({ map: glowTex, color: 0xffd9a2, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false });
    this.glows = [];
    for (let i = 0; i < 36; i++) { const sp = new THREE.Sprite(this.glowMat); this.scene.add(sp); this.glows.push(sp); }
    this.lampPoints = ['lamp-cobra', 'lamp-modern', 'lamp-heritage', 'lamp-twin', 'lamp-globe']
      .reduce((acc, k) => acc.concat(ctx.props.get(k) || []), []);
  }
  _emitMassing(network) {
    const list = network.massing;
    if (!list?.length) return;
    const geo = new THREE.BoxGeometry(1, 1, 1); geo.translate(0, .5, 0);
    const map = this.textures.facade?.map.clone();
    if (map) { map.needsUpdate = true; }
    const lit = new THREE.MeshStandardMaterial({ color: 0xbdbdbd, roughness: .72, metalness: .04, map: map ?? null, normalMap: this.textures.facade?.normalMap ?? null, envMapIntensity: .5 });
    const im = new THREE.InstancedMesh(geo, lit, list.length);
    const dummy = new THREE.Object3D(), rng = new RNG(11);
    for (let i = 0; i < list.length; i++) {
      const b = list[i];
      dummy.position.set(b.x, b.y, b.z); dummy.rotation.set(0, b.rot, 0); dummy.scale.set(b.sx, b.sy, b.sz);
      dummy.updateMatrix(); im.setMatrixAt(i, dummy.matrix);
      const t = rng.next();
      im.setColorAt(i, new THREE.Color().setHSL(.08 + t * .06, .06 + t * .12, .2 + t * .34));
      const uvScale = [Math.max(4, b.sx / 8), Math.max(2, b.sy / 4)];
      void uvScale;
    }
    im.instanceMatrix.needsUpdate = true; if (im.instanceColor) im.instanceColor.needsUpdate = true;
    im.castShadow = true; im.receiveShadow = true;
    im.userData = { bucket: '__massing', layer: 'massing' };
    this.groups.static.add(im);
    this.drawn.push({ mesh: im, mats: { lit, unlit: new THREE.MeshBasicMaterial({ color: 0x9a9a9a }), id: new THREE.MeshBasicMaterial({ color: 0x8e93a8 }) } });
  }
  _clear(group) {
    for (let i = group.children.length - 1; i >= 0; i--) {
      const c = group.children[i];
      c.geometry?.dispose?.();
      group.remove(c);
    }
  }

  /* ------------------------------- view modes ------------------------------- */
  setView(v) { this.view = v; this.applyView(); }
  applyView() {
    const v = this.view;
    for (const d of this.drawn) {
      if (v === 'wire') {
        if (!d.mats.wire) d.mats.wire = new THREE.MeshBasicMaterial({ color: 0xa8bfe0, vertexColors: true, wireframe: true, transparent: true, opacity: .7 });
        d.mesh.material = d.mats.wire;
      } else {
        const mat = v === 'lit' ? d.mats.lit : v === 'unlit' ? d.mats.unlit : v === 'id' ? d.mats.id : d.mats.lit;
        d.mesh.material = mat;
        if (v === 'lit' && mat.wireframe) mat.wireframe = false;
      }
    }
    this.ground.visible = this.flags.ground !== false && v !== 'wire';
    this.ground.material.transparent = !!this.flags.xray;
    this.ground.material.opacity = this.flags.xray ? .14 : 1;
    this.ground.material.depthWrite = !this.flags.xray;
    this.grid.visible = this.flags.grid !== false;
    this.gridBig.visible = this.flags.grid !== false;
    this.groups.under.visible = !!this.flags.xray;
    this.groups.props.visible = this.flags.props !== false;
    this.groups.decal.visible = this.flags.marks !== false;
    this.groups.agents.visible = this.flags.traffic !== false;
    for (const im of this.instanced) {
      const layer = im.userData.layer;
      im.visible = this.layers[layer] !== false;
    }
    for (const d of this.drawn) {
      const layer = d.mesh.userData.layer;
      if (!layer) continue;
      d.mesh.visible = this.layers[layer] !== false && !(layer === '__underground' && !this.flags.xray);
    }
    for (const m of [this.grid, this.gridBig]) if (m) m.material.opacity = this.flags.ground ? .3 : .55;
    this.renderer.shadowMap.enabled = !!this.flags.shadows && v === 'lit';
    this.renderer.shadowMap.needsUpdate = true;
  }
  setLayer(key, on) { this.layers[key] = on; this.applyView(); }

  /* ------------------------------- picking ------------------------------- */
  _pickData(network) {
    const tri = [], meta = [];
    for (const road of network.roads || []) {
      prepareRoad(road);
      const f = framesOf(road);
      if (f.length < 2) continue;
      const s = road._section, p = road._profile;
      const half = Math.max(Math.abs(s.start), Math.abs(s.end));
      const base = tri.length / 9;
      for (let i = 0; i < f.length - 1; i++) {
        const A = f[i], B = f[i + 1];
        const yA0 = A.y + (road.deckY || 0) + levelAt(s, p, -half), yA1 = A.y + (road.deckY || 0) + levelAt(s, p, half);
        const yB0 = B.y + (road.deckY || 0) + levelAt(s, p, -half), yB1 = B.y + (road.deckY || 0) + levelAt(s, p, half);
        const a = [A.x + A.nx * -half, yA0, A.z + A.nz * -half], b = [A.x + A.nx * half, yA1, A.z + A.nz * half];
        const c = [B.x + B.nx * half, yB1, B.z + B.nz * half], d = [B.x + B.nx * -half, yB0, B.z + B.nz * -half];
        tri.push(...a, ...b, ...c, ...a, ...c, ...d);
        meta.push({ roadId: road.id, s: A.s, s2: B.s }, { roadId: road.id, s: A.s, s2: B.s });
      }
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(tri, 3));
    this.pickGeo = geo;
    this.pickMeta = meta;
    this.pickNodes = (network.nodes || []).map(n => ({ n, r: 13 + (n.radius || 0) * .6 }));
  }
  pick(nx, ny) {
    const { o, d } = this._ray(nx, ny);
    let best = null;
    const pos = this.pickGeo?.getAttribute('position');
    if (pos) {
      const p = pos.array;
      for (let i = 0; i < p.length; i += 9) {
        const t = rayTri(o, d, p, i);
        if (t < 0) continue;
        if (!best || t < best.t) best = { t, tri: i / 9 };
      }
    }
    for (const { n, r } of this.pickNodes || []) {
      const t = rayDisc(o, d, n, r);
      if (t >= 0 && (!best || t < best.t)) best = { t, node: n };
    }
    if (!best) return null;
    if (best.node) return { kind: 'node', node: best.node, id: best.node.id, name: best.node.name };
    const m = this.pickMeta[best.tri];
    const road = this.network.roads.find(r => r.id === m.roadId);
    return { kind: 'road', road, id: road.id, name: road.name, s: (m.s + m.s2) / 2, point: [o.x + d.x * best.t, o.y + d.y * best.t, o.z + d.z * best.t] };
  }
  _ray(nx, ny) {
    const v = new THREE.Vector3(nx, ny, .5).unproject(this.camera);
    const o = this.camera.position.clone();
    const d = v.sub(o).normalize();
    return { o: { x: o.x, y: o.y, z: o.z }, d: { x: d.x, y: d.y, z: d.z } };
  }

  /* ------------------------------- overlays ------------------------------- */
  setSelection(id) { this.selected = id; this._overlay(); }
  setHover(id) { if (id !== this._hoverId) { this._hoverId = id; this._overlay(); } }
  _ribbon(frames, offA, offB, yFn, color, opacity) {
    const pos = [], idx = []; let n = 0;
    for (let i = 0; i < frames.length; i++) {
      const F = frames[i];
      for (const off of [offA, offB]) { pos.push(F.x + F.nx * off, yFn(F, off), F.z + F.nz * off); }
    }
    for (let i = 0; i < frames.length - 1; i++) { const a = i * 2; idx.push(a, a + 1, a + 3, a, a + 3, a + 2); }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    geo.setIndex(idx); geo.computeBoundingSphere();
    return new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color, transparent: true, opacity, side: THREE.DoubleSide, depthWrite: false, depthTest: true }));
  }
  _overlay() {
    this._clear(this.groups.overlay);
    this.groups.overlay.children.forEach(c => c.material?.dispose?.());
    const draw = (id, color, opacity, withFill) => {
      const road = this.network?.roads.find(r => r.id === id);
      if (!road) return;
      prepareRoad(road);
      const f = framesOf(road), s = road._section;
      const yFn = (F, off) => F.y + (road.deckY || 0) + levelAt(s, road._profile, off) + .1;
      if (withFill) this.groups.overlay.add(this._ribbon(f, s.start - .25, s.end + .25, yFn, color, .13));
      this.groups.overlay.add(this._ribbon(f, s.start - .12, s.start + .28, yFn, color, opacity));
      this.groups.overlay.add(this._ribbon(f, s.end - .28, s.end + .12, yFn, color, opacity));
      const pts = road.points.map(p => new THREE.Vector3(p.x, (p.y || 0) + (road.deckY || 0) + .55, p.z));
      if (pts.length > 1) {
        const lg = new THREE.BufferGeometry().setFromPoints(pts);
        const ln = new THREE.Line(lg, new THREE.LineDashedMaterial({ color, dashSize: 3, gapSize: 2, transparent: true, opacity: .55 }));
        ln.computeLineDistances();
        this.groups.overlay.add(ln);
      }
      const hg = new THREE.BufferGeometry().setFromPoints(pts);
      this.groups.overlay.add(new THREE.Points(hg, new THREE.PointsMaterial({ color, size: .55, sizeAttenuation: true })));
      this.handlePoints = pts; this.handleRoad = road;
    };
    this.handlePoints = null; this.handleRoad = null;
    if (this._hoverId && this._hoverId !== this.selected) draw(this._hoverId, 0xbfd6ff, .35, false);
    if (this.selected) draw(this.selected, 0x6ea8ff, .95, true);
  }
  setGhost(points, profileRef) {
    this._clear(this.groups.ghost);
    this.groups.ghost.children.forEach(c => c.material?.dispose?.());
    if (!points || points.length < 2) return;
    const road = { id: '__ghost', profile: profileRef, points: points.map(p => ({ ...p, y: p.y || 0 })), smooth: 2, crossings: [], bumps: [], driveways: [], noEndCrossings: true };
    prepareRoad(road);
    const ctx = new BuildCtx({ detail: 1 });
    buildRoad(road, ctx, { detail: 1 });
    const mats = new THREE.MeshBasicMaterial({ color: 0x74aaff, transparent: true, opacity: .5, depthWrite: false, side: THREE.DoubleSide });
    for (const [, g] of ctx.b.groups) {
      if (!g.n) continue;
      this.groups.ghost.add(new THREE.Mesh(this._geoFrom(g), mats));
    }
    for (const [, g] of ctx.decal.groups) {
      if (!g.n) continue;
      this.groups.ghost.add(new THREE.Mesh(this._geoFrom(g), new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: .6, depthWrite: false })));
    }
  }

  /* ------------------------------- camera ------------------------------- */
  _bind() {
    const el = this.renderer.domElement;
    el.addEventListener('contextmenu', e => e.preventDefault());
    el.addEventListener('pointerdown', e => {
      el.focus?.();
      try { el.setPointerCapture(e.pointerId); } catch (err) { }
      this.pointer.down = 1; this.pointer.dragBtn = e.button;
      this.pointer.lastX = e.clientX; this.pointer.lastY = e.clientY; this.pointer.moved = 0;
      this.h.onPointerDown?.(e, this);
    });
    el.addEventListener('pointermove', e => {
      const r = el.getBoundingClientRect();
      this.pointer.nx = ((e.clientX - r.left) / r.width) * 2 - 1;
      this.pointer.ny = -((e.clientY - r.top) / r.height) * 2 + 1;
      const dx = e.clientX - this.pointer.lastX, dy = e.clientY - this.pointer.lastY;
      this.pointer.lastX = e.clientX; this.pointer.lastY = e.clientY;
      this.pointer.moved += Math.abs(dx) + Math.abs(dy);
      if (this.pointer.dragBtn === 2 || this.pointer.dragBtn === 1) {
        if (this.mode === 'orbit') {
          this.orbit.theta -= dx * .0055;
          this.orbit.phi = clamp(this.orbit.phi - dy * .0055, .05, 1.53);
        } else {
          const sens = .0021 * (this.h.getSensitivity ? this.h.getSensitivity() : 1);
          this.cam.yaw -= dx * sens;
          this.cam.pitch = clamp(this.cam.pitch - dy * sens, -1.53, 1.53);
        }
        if (this.pointer.dragBtn === 1 && this.mode !== 'orbit') {
          const f = new THREE.Vector3(Math.sin(this.cam.yaw), 0, Math.cos(this.cam.yaw));
          const rgt = new THREE.Vector3(f.z, 0, -f.x);
          this.cam.pos.addScaledVector(rgt, -dx * this.cam.speed * .016).addScaledVector(f, dy * this.cam.speed * .016);
        }
      } else if (!this.pointer.down) this.h.onHover?.(this.pick(this.pointer.nx, this.pointer.ny), this);
    });
    const release = e => {
      if (this.pointer.down && this.pointer.moved < 7) {
        const hit = this.pick(this.pointer.nx, this.pointer.ny);
        if (e.button === 2) this.h.onRightClick?.(hit, this, e);
        else if (e.button === 0) this.h.onPick?.(hit, this, e);
      }
      this.pointer.down = 0; this.pointer.dragBtn = -1;
      this.h.onPointerUp?.(e, this);
    };
    el.addEventListener('pointerup', release);
    el.addEventListener('pointercancel', () => { this.pointer.down = 0; this.pointer.dragBtn = -1; });
    el.addEventListener('wheel', e => {
      e.preventDefault();
      const dir = Math.sign(e.deltaY) || 1;
      if (this.mode === 'orbit') this.orbit.dist = clamp(this.orbit.dist * (1 + dir * .1), 4, 3000);
      else this.cam.speed = clamp(this.cam.speed * (1 - dir * .14), .4, 400);
      this.h.onSpeed?.(this.mode === 'orbit' ? this.orbit.dist : this.cam.speed);
    }, { passive: false });
    const isField = e => /INPUT|TEXTAREA|SELECT/.test(e.target?.tagName) || e.target?.isContentEditable;
    addEventListener('keydown', e => {
      if (isField(e)) return;
      this.keys[e.code] = 1;
      if (e.code === 'KeyF') this.frameSelection();
      if (e.code === 'Home') this.frameAll();
      const r = this.h.onKey?.(e, this);
      if (r !== false && ['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'Space', 'KeyF'].includes(e.code)) e.preventDefault();
    });
    addEventListener('keyup', e => { this.keys[e.code] = 0; });
    addEventListener('blur', () => { this.keys = Object.create(null); this.pointer.down = 0; this.pointer.dragBtn = -1; });
  }
  setMode(m) {
    this.mode = m;
    if (m === 'orbit') {
      const f = fwdVec(this.cam);
      this.orbit.target.copy(this.cam.pos).addScaledVector(f, 70);
      const d = this.cam.pos.distanceTo(this.orbit.target);
      this.orbit.dist = d;
      this.orbit.theta = Math.atan2(this.cam.pos.x - this.orbit.target.x, this.cam.pos.z - this.orbit.target.z);
      this.orbit.phi = Math.acos(clamp((this.cam.pos.y - this.orbit.target.y) / d, -1, 1));
    }
    this.h.onMode?.(m);
  }
  frameSelection() {
    const road = this.network?.roads.find(r => r.id === this.selected);
    if (!road) return this.frameAll();
    const box = new THREE.Box3();
    for (const p of framesOf(road)) box.expandByPoint(new THREE.Vector3(p.x, p.y, p.z));
    const c = box.getCenter(new THREE.Vector3()), sz = box.getSize(new THREE.Vector3());
    const d = Math.max(46, Math.max(sz.x, sz.z) * .85);
    this.cam.pos.set(c.x - d * .5, c.y + d * .42, c.z - d * .5);
    this.cam.yaw = Math.atan2(c.x - this.cam.pos.x, c.z - this.cam.pos.z);
    this.cam.pitch = -Math.atan2(this.cam.pos.y - c.y, Math.hypot(c.x - this.cam.pos.x, c.z - this.cam.pos.z));
    this.orbit.target.copy(c); this.orbit.dist = d * 1.4;
  }
  frameAll() {
    let mx = 120;
    for (const road of this.network?.roads || []) for (const p of road.points) mx = Math.max(mx, Math.hypot(p.x, p.z));
    const r = mx * 1.16;
    this.cam.pos.set(r * .66, r * .6, r * .66);
    this.cam.yaw = Math.atan2(-this.cam.pos.x, -this.cam.pos.z);
    this.cam.pitch = -.62;
    this.orbit.target.set(0, 0, 0); this.orbit.dist = r * 1.05;
  }
  focusOn(x, z, y = 0, d = 90, h = 46) {
    this.cam.pos.set(x - d * .55, y + h, z - d * .55);
    this.cam.yaw = Math.atan2(x - this.cam.pos.x, z - this.cam.pos.z);
    this.cam.pitch = -Math.atan2(this.cam.pos.y - (y + 2), Math.hypot(x - this.cam.pos.x, z - this.cam.pos.z));
    this.orbit.target.set(x, y, z); this.orbit.dist = d * 1.3;
  }
  /* place the camera on the road — "drive" preview */
  driveOn(roadId, s = 0, lane = 'auto') {
    const road = this.network?.roads.find(r => r.id === roadId) || this.network?.roads[0];
    if (!road) return null;
    prepareRoad(road);
    const f = framesOf(road); if (!f.length) return null;
    const L = f[f.length - 1].s;
    const st = clamp(s, 0, L);
    const fr = frameAtLocal(road, st);
    const off = lane === 'auto' ? autoLane(road) : lane;
    const y = fr.y + (road.deckY || 0) + levelAt(road._section, road._profile, off) + 1.42;
    this.cam.pos.set(fr.x + fr.nx * off, y, fr.z + fr.nz * off);
    this.cam.pitch = -.02;
    this.drive = { road, s: st, off, speed: (profileOf(road.profile).speed || 50) / 3.6 };
    if (this.mode !== 'drive') this.prevMode = this.mode;
    this.mode = 'drive';
    return this.drive;
  }
  stopDrive() { this.drive = null; this.mode = this.prevMode || this.h.getPrevMode?.() || 'fly'; }

  /* ------------------------------- traffic ------------------------------- */
  _buildTraffic(network) {
    this.traffic = [];
    const rng = new RNG(strSeed('traffic-' + (network.params?.seed ?? 1)));
    for (const r of network.roads || []) {
      if (['subway', 'rail', 'tram', 'path', 'pedestrian', 'plaza'].includes(r.cls)) continue;
      prepareRoad(r);
      const f = framesOf(r); if (!f.length) continue;
      const L = f[f.length - 1].s;
      if (L < 26) continue;
      const lanes = r._section.elements.filter(e => (e.k === 'lane' || e.k === 'bus' || e.k === 'cycle') && e.dirNum);
      if (!lanes.length) continue;
      const density = r.cls === 'arterial' ? .013 : r.cls === 'collector' ? .007 : .0045;
      const n = clamp(Math.round(L * density), 0, 10);
      for (let i = 0; i < n; i++) {
        const lane = lanes[rng.int(0, lanes.length - 1)];
        const proto = lane.k === 'bus' ? 'bus' : lane.k === 'cycle' ? 'cyclist' : rng.chance(.13) ? 'van' : 'car';
        this.traffic.push({
          road: r, s: rng.range(0, L), L, off: (lane.x0 + lane.x1) / 2, dir: lane.dirNum,
          proto, speed: (profileOf(r.profile).speed || 40) / 3.6 * rng.range(.4, .85),
          color: proto === 'car' ? new THREE.Color().setHSL(rng.next(), .32, .5).getHex() : null
        });
      }
      for (let i = 0; i < clamp(Math.round(L * .004), 0, 3); i++) {
        const side = rng.chance(.5) ? 1 : -1;
        const el = r._section.elements[side > 0 ? r._section.elements.length - 1 : 0];
        if (!el) continue;
        this.traffic.push({ road: r, s: rng.range(0, L), L, off: el.cx, dir: rng.sign(), proto: 'pedestrian', speed: rng.range(1.1, 1.7), foot: true });
      }
    }
    this._agentMeshes();
  }
  _agentMeshes() {
    this.agentMesh = Object.create(null);
    const byProto = Object.create(null);
    for (const t of this.traffic) byProto[t.proto] = (byProto[t.proto] || 0) + 1;
    for (const proto of Object.keys(byProto)) {
      const parts = this._protoGeo(proto); if (!parts) continue;
      this.agentMesh[proto] = parts.map(({ mkey, geo }) => {
        const mat = (this.M['prop:' + mkey]?.lit || new THREE.MeshStandardMaterial()).clone();
        const im = new THREE.InstancedMesh(geo, mat, byProto[proto]);
        im.castShadow = true; im.frustumCulled = false;
        im.userData = { layer: CAT_LAYER[PROP_SPECS[proto].cat] || 'traffic' };
        this.groups.agents.add(im);
        this.drawn.push({ mesh: im, mats: { lit: mat, unlit: mat, id: mat } });
        return im;
      });
    }
  }
  _updateTraffic(dt) {
    if (!this.agentMesh) return;
    if (this.flags.traffic === false) return;
    const dummy = new THREE.Object3D();
    const count = Object.create(null);
    for (const t of this.traffic) {
      t.s += dt * t.speed * t.dir;
      if (t.s > t.L) t.s -= t.L; else if (t.s < 0) t.s += t.L;
      const f = frameAtLocal(t.road, t.s); if (!f) continue;
      const road = t.road;
      const y = f.y + (road.deckY || 0) + levelAt(road._section, road._profile, t.off) + .04;
      const ang = Math.atan2(f.tx * t.dir, f.tz * t.dir);
      const meshes = this.agentMesh[t.proto]; if (!meshes) continue;
      const i = count[t.proto] = (count[t.proto] || 0) + 1;
      const k = i - 1;
      for (const im of meshes) {
        dummy.position.set(f.x + f.nx * t.off, y, f.z + f.nz * t.off);
        dummy.rotation.set(0, ang, 0);
        dummy.scale.set(1, 1, 1);
        dummy.updateMatrix();
        im.setMatrixAt(k, dummy.matrix);
        if (t.color && im === meshes[0]) im.setColorAt(k, _col.setHex(t.color));
        im.count = i;
      }
    }
    for (const proto of Object.keys(this.agentMesh)) for (const im of this.agentMesh[proto]) {
      im.instanceMatrix.needsUpdate = true;
      if (im.instanceColor) im.instanceColor.needsUpdate = true;
    }
  }

  /* ------------------------------- frame ------------------------------- */
  _frame() {
    const dt = Math.min(.06, this._clock.getDelta());
    this._move(dt);
    this._updateTraffic(dt);
    this._gates(dt);
    this._shadow();
    this._glow();
    this._waterT(dt);
    if (this.h.pickDuringDrive) this.h.pickDuringDrive();
    this.renderer.render(this.scene, this.camera);
    const info = this.renderer.info;
    this._fps = (this._fps || 60) * .92 + (dt > 0 ? 1 / dt : 60) * .08;
    const now = performance.now();
    if (now - (this._lastHud || 0) > 200) {
      this._lastHud = now;
      this.h.onFrame?.({ fps: this._fps, calls: info.render.calls, tris: info.render.triangles, mem: info.memory.geometries, pos: this.camera.position, speed: this.mode === 'drive' ? (this.drive?.speed || 0) * 3.6 : this.cam.speed, mode: this.mode, day: this.day });
    }
  }
  _move(dt) {
    if (this.mode === 'drive' && this.drive) {
      const road = this.drive.road;
      const f = framesOf(road);
      const L = f[f.length - 1].s;
      this.drive.s += dt * this.drive.speed;
      if (this.drive.s > L - 8) this.drive.s = 8;
      const fr = frameAtLocal(road, this.drive.s);
      if (fr) {
        const y = fr.y + (road.deckY || 0) + levelAt(road._section, road._profile, this.drive.off) + 1.5;
        this.cam.pos.lerp(new THREE.Vector3(fr.x + fr.nx * this.drive.off, y, fr.z + fr.nz * this.drive.off), 1 - Math.pow(.0006, dt));
        const look = frameAtLocal(road, this.drive.s + 16);
        if (look) {
          this.cam.yaw = lerpAngle(this.cam.yaw, Math.atan2(look.x - fr.x, look.z - fr.z), 1 - Math.pow(.002, dt));
          this.cam.pitch = -.03;
        }
      }
      this.camera.position.copy(this.cam.pos);
      this.camera.quaternion.setFromEuler(new THREE.Euler(this.cam.pitch, this.cam.yaw + Math.PI, 0, 'YXZ'));
      return;
    }
    if (this.mode === 'orbit') {
      const t = this.orbit.target;
      const sp = Math.sin(this.orbit.phi), cp = Math.cos(this.orbit.phi);
      let ox = 0, oz = 0;
      const k = this.keys;
      const pan = (this.cam.speed || 18) * dt * 1.6;
      if (k.KeyW || k.ArrowUp) oz -= pan; if (k.KeyS || k.ArrowDown) oz += pan;
      if (k.KeyA || k.ArrowLeft) ox -= pan; if (k.KeyD || k.ArrowRight) ox += pan;
      if (ox || oz) {
        const c = Math.cos(this.orbit.theta), s2 = Math.sin(this.orbit.theta);
        t.x += ox * c - oz * s2; t.z += ox * s2 + oz * c;
      }
      if (k.KeyE) t.y += pan; if (k.KeyQ) t.y -= pan;
      this.camera.position.set(t.x + Math.sin(this.orbit.theta) * this.orbit.dist * sp, t.y + this.orbit.dist * cp, t.z + Math.cos(this.orbit.theta) * this.orbit.dist * sp);
      this.camera.lookAt(t);
      this.cam.pos.copy(this.camera.position);
      return;
    }
    const k = this.keys;
    const fwd = (k.KeyW || k.ArrowUp ? 1 : 0) - (k.KeyS || k.ArrowDown ? 1 : 0);
    const side = (k.KeyD || k.ArrowRight ? 1 : 0) - (k.KeyA || k.ArrowLeft ? 1 : 0);
    const up = (k.KeyE || k.Space ? 1 : 0) - (k.KeyQ || k.ControlLeft || k.ControlRight ? 1 : 0);
    const sprint = k.ShiftLeft || k.ShiftRight ? 5.5 : 1;
    const slow = k.AltLeft || k.AltRight ? .18 : 1;
    const v = (this.cam.speed || 18) * sprint * slow;
    if (fwd || side || up) {
      const c = Math.cos(this.cam.pitch);
      const fx = Math.sin(this.cam.yaw) * (this.mode === 'walk' ? 1 : c), fz = Math.cos(this.cam.yaw) * (this.mode === 'walk' ? 1 : c);
      const rx = -fz, rz = fx;
      this.cam.pos.x += (fx * fwd + rx * side) * v * dt;
      this.cam.pos.z += (fz * fwd + rz * side) * v * dt;
      if (this.mode === 'fly') this.cam.pos.y += (Math.sin(this.cam.pitch) * fwd * v + up * v * .85) * dt;
      else this.cam.pos.y += up * v * .5 * dt;
    }
    if (this.mode === 'walk') {
      const gy = this.groundYAt ? this.groundYAt(this.cam.pos.x, this.cam.pos.z) : 0;
      this.cam.pos.y = lerp(this.cam.pos.y, gy + 1.72, 1 - Math.pow(.00005, dt));
    } else if (this.cam.pos.y < .4) this.cam.pos.y = .4;
    this.camera.position.copy(this.cam.pos);
    this.camera.quaternion.setFromEuler(new THREE.Euler(this.cam.pitch, this.cam.yaw + Math.PI, 0, 'YXZ'));
    const fovWant = (k.ShiftLeft && this.mode === 'fly') ? 74 : (this.h.getFov?.() ?? 55);
    if (Math.abs(this.camera.fov - fovWant) > .01) { this.camera.fov = lerp(this.camera.fov, fovWant, 1 - Math.pow(.002, dt)); this.camera.updateProjectionMatrix(); }
  }
  _gates(dt) {
    if (!this.gates.length) return;
    this._gateT = (this._gateT || 0) + dt;
    const dummy = this._dummy || (this._dummy = new THREE.Object3D());
    let anyClosing = 0;
    for (const g of this.gates) {
      const items = g.userData.items || [];
      for (let i = 0; i < items.length; i++) {
        const a = items[i];
        const t = (this._gateT / 16 + (a.phase || 0)) % 1;
        const closing = t > .3 && t < .72;
        if (closing) anyClosing = 1;
        let k = 0;
        if (closing) {
          const open = (t - .3) / .11, close = (t - .61) / .11;
          k = t < .61 ? clamp(open, 0, 1) : clamp(1 - close, 0, 1);
        }
        dummy.position.set(a.pivot.x, a.pivot.y, a.pivot.z);
        dummy.rotation.set(-k * Math.PI / 2, a.base, 0, 'YXZ');
        dummy.scale.set(1, 1, 1); dummy.updateMatrix();
        g.setMatrixAt(i, dummy.matrix);
      }
      g.count = items.length;
      g.instanceMatrix.needsUpdate = true;
    }
    for (const e of this.emissiveMats || []) {
      if (e.blink < 0) continue;
      const t = (this._gateT / 16 + e.blink * .5) % 1;
      const on = t > .3 && t < .72 && (this._gateT % .9) < (t > .61 ? .62 : .5);
      e.m.emissiveIntensity = (on ? 5 : .25) + this.night * .8;
      anyClosing && (e.m.emissiveIntensity += 1);
    }
  }
  _glow() {
    if (!this.glows) return;
    const want = clamp(this.night * .95, 0, 1);
    this.glowMat.opacity = lerp(this.glowMat.opacity, want, .06);
    if (this.glowMat.opacity < .015) { for (const g of this.glows) g.visible = false; return; }
    const cam = this.camera.position;
    const near = this.lampPoints.map(p => ({ p, d: (p.x - cam.x) ** 2 + (p.z - cam.z) ** 2 })).sort((a, b) => a.d - b.d).slice(0, this.glows.length);
    for (let i = 0; i < this.glows.length; i++) {
      const g = this.glows[i];
      if (!near[i] || near[i].d > 130000) { g.visible = false; continue; }
      g.visible = true;
      g.position.set(near[i].p.x, near[i].p.y + 7.6, near[i].p.z);
      const s = clamp(Math.sqrt(near[i].d) * .16, 4, 20);
      g.scale.set(s, s, 1);
    }
  }
  _shadow() {
    const p = this.camera.position;
    this.sun.target.position.set(p.x, 0, p.z);
    this.sun.position.set(this.sun.position.x + 0, this.sun.position.y, this.sun.position.z + 0);
    const dir = this.sun.position.clone().sub(this.sun.target.position).normalize();
    this.sun.position.copy(this.sun.target.position).addScaledVector(dir, 640);
    const spread = clamp(160 + this.cam.speed * 4, 200, 1100);
    const c = this.sun.shadow.camera;
    if (Math.abs(c.right - spread / 2) > spread * .08) {
      c.left = -spread / 2; c.right = spread / 2; c.top = spread / 2; c.bottom = -spread / 2;
      c.updateProjectionMatrix();
    }
    this.sun.target.updateMatrixWorld();
  }
  _waterT(dt) {
    this._wt = (this._wt || 0) + dt;
    const t = this.M['pav:water']?.lit;
    if (t?.map) { t.map.offset.set(this._wt * .008, this._wt * .004); t.normalMap?.offset.set(-this._wt * .01, this._wt * .006); }
    const g = this.ground?.material;
    if (g?.map && this.flags.ground) { }
  }
  screenshot() { this.renderer.render(this.scene, this.camera); return this.renderer.domElement.toDataURL('image/png'); }
  _resize() {
    const w = this.host.clientWidth || 1280, h = this.host.clientHeight || 720;
    if (!w || !h) return;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h; this.camera.updateProjectionMatrix();
  }
  dispose() {
    this.renderer.setAnimationLoop(null);
    for (const g of Object.values(this.groups)) this._clear(g);
    this.renderer.dispose();
  }
}

/* ------------------------------- helpers ------------------------------- */
const _col = new THREE.Color();
const mixc = (a, b, t) => a.map((v, i) => v + (b[i] - v) * clamp(t, 0, 1));
const lerpAngle = (a, b, t) => { let d = ((b - a + Math.PI) % TAU + TAU) % TAU - Math.PI; return a + d * t; };
function fwdVec(cam) { const c = Math.cos(cam.pitch); return new THREE.Vector3(Math.sin(cam.yaw) * c, Math.sin(cam.pitch), Math.cos(cam.yaw) * c).normalize(); }
function frameAtLocal(road, s) {
  const f = road._frames || framesOf(road);
  if (!f.length) return null;
  const L = f[f.length - 1].s;
  const t = clamp(s, 0, L);
  let i = 0;
  while (i < f.length - 2 && f[i + 1].s < t) i++;
  const a = f[i], b = f[i + 1] || a;
  const k = a.s === b.s ? 0 : (t - a.s) / (b.s - a.s);
  const tx = lerp(a.tx, b.tx, k), tz = lerp(a.tz, b.tz, k);
  return { x: lerp(a.x, b.x, k), z: lerp(a.z, b.z, k), y: lerp(a.y, b.y, k), tx, tz, nx: -tz, nz: tx };
}
function autoLane(road) {
  const lanes = (road._section?.elements || []).filter(e => e.k === 'lane');
  return lanes.length ? (lanes[Math.floor(lanes.length / 2)].x0 + lanes[Math.floor(lanes.length / 2)].x1) / 2 : 0;
}
/** Möller–Trumbore against the packed triangle array */
function rayTri(o, d, p, i) {
  const ax = p[i], ay = p[i + 1], az = p[i + 2];
  const bx = p[i + 3], by = p[i + 4], bz = p[i + 5];
  const cx = p[i + 6], cy = p[i + 7], cz = p[i + 8];
  const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
  const e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
  const px = d.y * e2z - d.z * e2y, py = d.z * e2x - d.x * e2z, pz = d.x * e2y - d.y * e2x;
  const det = e1x * px + e1y * py + e1z * pz;
  if (Math.abs(det) < 1e-10) return -1;
  const inv = 1 / det;
  const tx = o.x - ax, ty = o.y - ay, tz = o.z - az;
  const u = (tx * px + ty * py + tz * pz) * inv;
  if (u < -.002 || u > 1.002) return -1;
  const qx = ty * e1z - tz * e1y, qy = tz * e1x - tx * e1z, qz = tx * e1y - ty * e1x;
  const v = (d.x * qx + d.y * qy + d.z * qz) * inv;
  if (v < -.002 || u + v > 1.002) return -1;
  const t = (e2x * qx + e2y * qy + e2z * qz) * inv;
  return t > .3 ? t : -1;
}
function rayDisc(o, d, c, r) {
  if (d.y > -1e-4) return -1;
  const t = (c.y - o.y) / d.y;
  if (t < 0) return -1;
  const hx = o.x + d.x * t - c.x, hz = o.z + d.z * t - c.z;
  return hx * hx + hz * hz < r * r ? t : -1;
}
export { frameAtLocal, autoLane };
