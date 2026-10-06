//============================================================================================================================================
//                                                              VIEWPORT.JS
//============================================================================================================================================
// WebGL presentation layer: Z-up scene, studio lighting, the generated network, and the editable overlay (corridor
// ribbons, control-point handles, junction markers). Orbiting, panning and zooming are hand-rolled so the editor has
// no runtime dependency beyond three.js itself.

import * as THREE from 'three';
import { TranslateGizmo } from './Gizmo.js?v=9';
import { pavingTexture, signTexture, surfaceTexture } from './Textures.js?v=9';

// Paint-like groups: hidden together by the markings toggle, and polygon-offset so they never z-fight the road.
export const MARKING_GROUPS = new Set(['markings', 'markingsYellow', 'laneTint']);

export const MATERIAL_STYLES = {
  road: { color: 0x32363d, roughness: 0.95, metalness: 0.0 },
  curb: { color: 0x9ea3aa, roughness: 0.82, metalness: 0.0 },
  pavement: { color: 0x6b7077, roughness: 0.92, metalness: 0.0 },
  driveway: { color: 0x7d8189, roughness: 0.9, metalness: 0.0 },
  drainGrate: { color: 0x23262b, roughness: 0.45, metalness: 0.75 },
  drainCover: { color: 0x5a5e66, roughness: 0.45, metalness: 0.7 },
  drainPipe: { color: 0x6c5f4e, roughness: 0.85, metalness: 0.1 },
  markings: { color: 0xe6e2d6, roughness: 0.7, metalness: 0.0, emissive: 0x15140f },
  markingsYellow: { color: 0xd8b545, roughness: 0.72, metalness: 0.0, emissive: 0x201803 },
  laneTint: { color: 0x3f6b4a, roughness: 0.95, metalness: 0.0 },
  deck: { color: 0x8b8e93, roughness: 0.88, metalness: 0.0 },
  structure: { color: 0x59616e, roughness: 0.55, metalness: 0.55 },
  piers: { color: 0x7c7f85, roughness: 0.9, metalness: 0.0 },
  railing: { color: 0xa7adb5, roughness: 0.6, metalness: 0.35 },
  barrier: { color: 0x9a9da1, roughness: 0.93, metalness: 0.0 },
  cables: { color: 0xc6cad0, roughness: 0.4, metalness: 0.7 },
  earth: { color: 0x6b6450, roughness: 1.0, metalness: 0.0 },
  roadbed: { color: 0x8a8d92, roughness: 0.92, metalness: 0.0 },
  signFace: { color: 0xffffff, roughness: 0.55, metalness: 0.05 },
  signPost: { color: 0x9aa0a8, roughness: 0.45, metalness: 0.6 },
};

// `pavement#brick@1.00` → `pavement`. Group names carry their paving variant so each pattern gets its own texture.
const groupBase = (name) => name.split('#')[0];
// Flat-shaded fallback colours for unsealed carriageways, used when textures are switched off.
const LANE_TINT_COLORS = { cycle: 0x3a6b46, bus: 0x7a3a34 };
const SURFACE_COLORS = { gravel: 0x8b8475, dirt: 0x6f6149, track: 0x6b5f4a, concrete: 0x8c8c8a, setts: 0x6a6a6e, chipseal: 0x55565a };

const groupVariant = (name) => {
  const tail = name.split('#')[1];
  if (!tail) return null;
  const [pattern, scale] = tail.split('@');
  return { pattern, scale: parseFloat(scale) || 1 };
};

export class Viewport {
  constructor(canvas) {
    this.canvas = canvas;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.05;

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x242831);
    this.scene.fog = new THREE.Fog(0x242831, 240, 900);

    this.camera = new THREE.PerspectiveCamera(45, 1, 0.5, 4000);
    this.camera.up.set(0, 0, 1);

    this.target = new THREE.Vector3(0, 0, 0);
    this.spherical = { radius: 160, theta: -Math.PI * 0.35, phi: Math.PI * 0.32 };

    this._buildLights();
    this._buildGround();

    this.networkGroup = new THREE.Group();
    this.scene.add(this.networkGroup);
    this.overlayGroup = new THREE.Group();
    this.scene.add(this.overlayGroup);
    // Selection highlights (the stretch of road between two junctions, or the junction itself) and street-name
    // labels live in their own groups: they are rebuilt on selection, not on every network solve.
    this.highlightGroup = new THREE.Group();
    this.scene.add(this.highlightGroup);
    this.labelGroup = new THREE.Group();
    this.scene.add(this.labelGroup);

    this.gizmo = new TranslateGizmo(this.scene);
    this.raycaster = new THREE.Raycaster();
    this.raycaster.params.Line.threshold = 1.2;
    this.pointer = new THREE.Vector2();

    this.meshes = new Map();
    this.handleMeshes = [];
    this.corridorLines = [];
    this.junctionMeshes = [];
    this.displayMode = 'shaded';
    this.showGround = true;
    this.showMarkings = true;
    this.showDrainage = false; // the buried pipe run; the castings at the surface are always shown
    this.showLabels = true;
    this.textured = true;
    this.flySpeed = 34; // m/s, adjusted with the wheel while the right button is held
    this._groups = null;

    this._applyCamera();
    this.resize();
  }

  _buildLights() {
    this.hemi = new THREE.HemisphereLight(0x8fa2bd, 0x33363c, 0.75);
    this.scene.add(this.hemi);

    this.sun = new THREE.DirectionalLight(0xffeedd, 2.1);
    this.sun.castShadow = true;
    this.sun.shadow.mapSize.set(2048, 2048);
    const d = 160;
    this.sun.shadow.camera.left = -d;
    this.sun.shadow.camera.right = d;
    this.sun.shadow.camera.top = d;
    this.sun.shadow.camera.bottom = -d;
    this.sun.shadow.camera.near = 1;
    this.sun.shadow.camera.far = 700;
    this.sun.shadow.bias = -0.0008;
    this.scene.add(this.sun);
    this.scene.add(this.sun.target);
    this.setSunAzimuth(-35);

    this.fill = new THREE.DirectionalLight(0x9bb3d4, 0.45);
    this.fill.position.set(-80, 120, 60);
    this.scene.add(this.fill);
  }

  setSunAzimuth(deg) {
    this.sunAzimuth = deg;
    const a = (deg * Math.PI) / 180;
    this.sun.position.set(Math.cos(a) * 150, Math.sin(a) * 150, 185);
  }

  setShadows(on) {
    this.sun.castShadow = on;
    this.renderer.shadowMap.needsUpdate = true;
  }

  _buildGround() {
    this.groundGroup = new THREE.Group();
    const plane = new THREE.Mesh(
      new THREE.PlaneGeometry(2400, 2400),
      new THREE.MeshStandardMaterial({ color: 0x2c313a, roughness: 1.0, metalness: 0 }),
    );
    plane.position.z = -0.02;
    plane.receiveShadow = true;
    this.ground = plane;
    this.groundGroup.add(plane);

    const grid = new THREE.GridHelper(1200, 120, 0x3c4250, 0x31353e);
    grid.rotation.x = Math.PI / 2;
    grid.position.z = 0.002;
    grid.material.transparent = true;
    grid.material.opacity = 0.55;
    this.grid = grid;
    this.groundGroup.add(grid);
    this.scene.add(this.groundGroup);
  }

  setGroundVisible(v) {
    this.showGround = v;
    this.groundGroup.visible = v;
  }

  // ── network meshes ───────────────────────────────────────────────────────────────────────────────────────────────

  setNetwork(groups) {
    this._groups = groups;
    for (const [name, mesh] of this.meshes) {
      this.networkGroup.remove(mesh);
      mesh.geometry.dispose();
      mesh.material.dispose();
      void name;
    }
    this.meshes.clear();

    for (const [name, spec] of Object.entries(groups)) {
      if (!spec || spec.triangleCount === 0) continue;
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute('position', new THREE.Float32BufferAttribute(spec.positions, 3));
      if (spec.normals.length === spec.positions.length) {
        geometry.setAttribute('normal', new THREE.Float32BufferAttribute(spec.normals, 3));
      }
      geometry.setAttribute('uv', new THREE.Float32BufferAttribute(spec.uvs, 2));
      geometry.setIndex(spec.indices);
      if (spec.normals.length !== spec.positions.length) geometry.computeVertexNormals();
      geometry.computeBoundingSphere();

      const base = groupBase(name);
      const style = MATERIAL_STYLES[base] || MATERIAL_STYLES.road;
      const surfaceTint = base === 'road'
        ? SURFACE_COLORS[groupVariant(name)?.pattern]
        : base === 'laneTint' ? LANE_TINT_COLORS[groupVariant(name)?.pattern] : null;
      const material = new THREE.MeshStandardMaterial({
        ...style,
        ...(surfaceTint ? { color: surfaceTint } : {}),
        side: THREE.DoubleSide,
        wireframe: this.displayMode === 'wireframe',
        flatShading: this.displayMode === 'surfaces',
      });
      this._applyTexture(material, name, base);
      if (MARKING_GROUPS.has(base)) {
        material.polygonOffset = true;
        material.polygonOffsetFactor = -2;
        material.polygonOffsetUnits = -2;
      }
      const mesh = new THREE.Mesh(geometry, material);
      mesh.castShadow = !MARKING_GROUPS.has(base);
      mesh.receiveShadow = true;
      mesh.visible = base === 'drainPipe' ? this.showDrainage : MARKING_GROUPS.has(base) ? this.showMarkings : true;
      mesh.name = name;
      this.networkGroup.add(mesh);
      this.meshes.set(name, mesh);
    }
  }

  // Surfaces are drawn with runtime-generated canvas textures (see Textures.js) — no files, no fetches.
  _applyTexture(material, name, base) {
    if (!this.textured) return;
    let tex = null;
    if (base === 'pavement') {
      const variant = groupVariant(name) || { pattern: 'concrete', scale: 1 };
      tex = pavingTexture(THREE, variant.pattern, variant.scale);
    } else if (base === 'road') {
      // `road#gravel` → the matching running surface; bare `road` stays asphalt.
      const variant = groupVariant(name);
      tex = surfaceTexture(THREE, variant ? `road:${variant.pattern}` : 'road');
    } else if (base === 'signFace') {
      tex = signTexture(THREE);
    } else {
      tex = surfaceTexture(THREE, base);
    }
    if (!tex) return;
    material.map = tex.map;
    if (tex.normalMap) {
      material.normalMap = tex.normalMap;
      material.normalScale = new THREE.Vector2(0.8, 0.8);
    }
    material.needsUpdate = true;
  }

  setTextured(on) {
    this.textured = on;
    if (this._groups) this.setNetwork(this._groups);
  }

  setDisplayMode(mode) {
    this.displayMode = mode;
    for (const mesh of this.meshes.values()) {
      mesh.material.wireframe = mode === 'wireframe';
      mesh.material.flatShading = mode === 'surfaces';
      mesh.material.needsUpdate = true;
    }
  }

  // The carrier pipe is a metre under the road, so it is only worth drawing when it is being inspected.
  setDrainageVisible(v) {
    this.showDrainage = v;
    for (const [name, mesh] of this.meshes) {
      if (groupBase(name) === 'drainPipe') mesh.visible = v;
    }
  }

  setMarkingsVisible(v) {
    this.showMarkings = v;
    for (const [name, mesh] of this.meshes) {
      if (MARKING_GROUPS.has(groupBase(name))) mesh.visible = v;
    }
  }

  // ── editable overlay ─────────────────────────────────────────────────────────────────────────────────────────────

  setOverlay(corridors, graph, selection) {
    for (const child of [...this.overlayGroup.children]) {
      this.overlayGroup.remove(child);
      child.geometry?.dispose?.();
      child.material?.dispose?.();
    }
    this.handleMeshes = [];
    this.junctionMeshes = [];

    const pointKeys = new Set((selection?.points || []).map((p) => `${p.corridorId}:${p.index}`));
    const junctionIds = new Set((selection?.junctions || []).map((j) => j.id));
    if (selection?.junctionId) junctionIds.add(selection.junctionId);

    for (const corridor of corridors) {
      const selected = selection?.corridorId === corridor.id || (selection?.corridorIds || []).includes(corridor.id);
      const colour = selected ? 0xd6a665 : corridor.family === 'bridge' ? 0x7fa7c9 : 0x79808c;

      if (corridor.samples && corridor.samples.length > 1) {
        const positions = [];
        for (const p of corridor.samples) positions.push(p.x, p.y, p.z + 0.08);
        const geom = new THREE.BufferGeometry();
        geom.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
        const line = new THREE.Line(geom, new THREE.LineBasicMaterial({ color: colour, transparent: true, opacity: selected ? 0.95 : 0.5, depthTest: false }));
        line.renderOrder = 10;
        line.userData.corridorId = corridor.id;
        this.overlayGroup.add(line);
      }

      corridor.points.forEach((p, index) => {
        const isSelectedPoint = pointKeys.has(`${corridor.id}:${index}`);
        const size = isSelectedPoint ? 0.95 : 0.7;
        const handle = new THREE.Mesh(
          new THREE.SphereGeometry(size, 14, 10),
          new THREE.MeshBasicMaterial({
            color: isSelectedPoint ? 0xf1c994 : selected ? 0xd6a665 : 0x5b626d,
            depthTest: false,
            transparent: true,
            opacity: 0.95,
          }),
        );
        handle.position.set(p.x, p.y, p.z);
        handle.renderOrder = 20;
        handle.userData = { corridorId: corridor.id, pointIndex: index, handle: true };
        this.overlayGroup.add(handle);
        if (!corridor.draft) this.handleMeshes.push(handle);

        if (p.z > 0.05) {
          const stem = new THREE.Line(
            new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(p.x, p.y, 0), new THREE.Vector3(p.x, p.y, p.z)]),
            new THREE.LineBasicMaterial({ color: 0x4e5663, transparent: true, opacity: 0.7, depthTest: false }),
          );
          stem.renderOrder = 9;
          this.overlayGroup.add(stem);
        }
      });
    }

    if (graph) {
      for (const node of graph.nodes.values()) {
        if (node.degree < 2) continue;
        const active = junctionIds.has(node.id);
        const ring = new THREE.Mesh(
          new THREE.RingGeometry(node.cornerRadius - 0.25, node.cornerRadius, 48),
          new THREE.MeshBasicMaterial({ color: active ? 0xf1c994 : node.degree >= 3 ? 0xd6a665 : 0x5d6775, transparent: true, opacity: active ? 0.75 : 0.35, side: THREE.DoubleSide, depthTest: false }),
        );
        ring.position.set(node.co.x, node.co.y, node.co.z + 0.06);
        ring.renderOrder = 8;
        this.overlayGroup.add(ring);

        // A junction is grabbable as a unit: this disc is what the pointer picks.
        const hub = new THREE.Mesh(
          new THREE.CylinderGeometry(active ? 1.5 : 1.25, active ? 1.5 : 1.25, 0.5, 20),
          new THREE.MeshBasicMaterial({ color: active ? 0xf1c994 : 0xb98f58, transparent: true, opacity: 0.9, depthTest: false }),
        );
        hub.rotation.x = Math.PI / 2;
        hub.position.set(node.co.x, node.co.y, node.co.z + 0.25);
        hub.renderOrder = 21;
        hub.userData = { junctionId: node.id, junction: true, co: { ...node.co }, degree: node.degree };
        this.overlayGroup.add(hub);
        this.junctionMeshes.push(hub);
      }
    }
  }

  // ── selection highlight ──────────────────────────────────────────────────────────────────────────────────────
  // `strips` are ribbons of left/right edge points (one stretch of road between junctions); `discs` are junction
  // aprons. Both are drawn as unlit translucent accent sheets just above the surface they belong to.

  setHighlight({ strips = [], discs = [] } = {}) {
    for (const child of [...this.highlightGroup.children]) {
      this.highlightGroup.remove(child);
      child.geometry?.dispose?.();
      child.material?.dispose?.();
    }

    const sheet = (color, opacity) =>
      new THREE.MeshBasicMaterial({ color, transparent: true, opacity, side: THREE.DoubleSide, depthWrite: false });

    for (const strip of strips) {
      if (!strip.left || strip.left.length < 2) continue;
      const positions = [];
      const indices = [];
      for (let i = 0; i < strip.left.length; i++) {
        const l = strip.left[i];
        const r = strip.right[i];
        positions.push(l.x, l.y, l.z + 0.06, r.x, r.y, r.z + 0.06);
        if (i > 0) {
          const a = (i - 1) * 2;
          indices.push(a, a + 1, a + 3, a, a + 3, a + 2);
        }
      }
      const geom = new THREE.BufferGeometry();
      geom.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
      geom.setIndex(indices);
      const mesh = new THREE.Mesh(geom, sheet(0xd6a665, 0.3));
      mesh.renderOrder = 6;
      this.highlightGroup.add(mesh);

      // bright edge lines so the extent of the stretch is unmistakable
      for (const side of ['left', 'right']) {
        const pts = strip[side].map((p) => new THREE.Vector3(p.x, p.y, p.z + 0.09));
        const line = new THREE.Line(
          new THREE.BufferGeometry().setFromPoints(pts),
          new THREE.LineBasicMaterial({ color: 0xf1c994, transparent: true, opacity: 0.9, depthTest: false }),
        );
        line.renderOrder = 12;
        this.highlightGroup.add(line);
      }
    }

    for (const disc of discs) {
      const mesh = new THREE.Mesh(new THREE.CircleGeometry(disc.radius, 40), sheet(0xf1c994, 0.26));
      mesh.position.set(disc.co.x, disc.co.y, disc.co.z + 0.05);
      mesh.renderOrder = 6;
      this.highlightGroup.add(mesh);
      const ring = new THREE.Mesh(
        new THREE.RingGeometry(disc.radius - 0.4, disc.radius, 48),
        new THREE.MeshBasicMaterial({ color: 0xf1c994, transparent: true, opacity: 0.95, side: THREE.DoubleSide, depthTest: false }),
      );
      ring.position.set(disc.co.x, disc.co.y, disc.co.z + 0.1);
      ring.renderOrder = 12;
      this.highlightGroup.add(ring);
    }
  }

  // ── street-name labels ───────────────────────────────────────────────────────────────────────────────────────
  // Names are drawn to a canvas and shown as camera-facing sprites, so a street is identifiable in the viewport
  // without hunting through the outliner. Scale is in world metres and clamped by distance in `render()`.

  setLabels(labels = []) {
    for (const child of [...this.labelGroup.children]) {
      this.labelGroup.remove(child);
      child.material?.map?.dispose?.();
      child.material?.dispose?.();
    }
    for (const label of labels) {
      const sprite = this._makeLabel(label);
      if (sprite) this.labelGroup.add(sprite);
    }
    this.labelGroup.visible = this.showLabels;
  }

  setLabelsVisible(v) {
    this.showLabels = v;
    this.labelGroup.visible = v;
  }

  _makeLabel({ text, position, accent = false, kind = 'corridor' }) {
    if (typeof document === 'undefined' || !THREE.Sprite || !THREE.SpriteMaterial) return null;
    const canvas = document.createElement('canvas');
    const ctx = canvas.getContext('2d');
    if (!ctx) return null;
    const pad = 18;
    const font = '600 40px "DM Sans", system-ui, sans-serif';
    ctx.font = font;
    const width = Math.ceil((ctx.measureText?.(text)?.width ?? text.length * 20) + pad * 2);
    canvas.width = Math.max(64, width);
    canvas.height = 76;
    ctx.font = font;
    ctx.textBaseline = 'middle';
    ctx.fillStyle = accent ? 'rgba(54,42,26,0.92)' : 'rgba(23,25,29,0.82)';
    ctx.fillRect(0, 10, canvas.width, 56);
    ctx.fillStyle = accent ? '#f1c994' : '#d5d7dc';
    ctx.fillRect(0, 10, 4, 56);
    ctx.fillText(text, pad, 39);

    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture, transparent: true, depthTest: false, sizeAttenuation: false }));
    const aspect = canvas.width / canvas.height;
    const h = kind === 'junction' ? 0.028 : 0.034;
    sprite.scale.set(h * aspect, h, 1);
    sprite.position.set(position.x, position.y, position.z + (kind === 'junction' ? 3.2 : 2.2));
    sprite.renderOrder = 30;
    sprite.userData = { label: true, kind };
    return sprite;
  }

  // ── rectangle (marquee) picking ──────────────────────────────────────────────────────────────────────────────
  // Returns the userData of every handle / junction hub whose centre projects inside the given NDC rectangle.

  _insideRect(object, rect) {
    const v = object.position.clone().project(this.camera);
    if (v.z > 1) return false;
    return v.x >= rect.x0 && v.x <= rect.x1 && v.y >= rect.y0 && v.y <= rect.y1;
  }

  handlesInRect(rect) {
    return this.handleMeshes.filter((m) => this._insideRect(m, rect)).map((m) => m.userData);
  }

  junctionsInRect(rect) {
    return this.junctionMeshes.filter((m) => this._insideRect(m, rect)).map((m) => m.userData);
  }

  setOverlayVisible(v) {
    this.overlayGroup.visible = v;
    this.gizmo.enabled = v;
    if (!v) this.gizmo.detach();
  }

  // ── camera ───────────────────────────────────────────────────────────────────────────────────────────────────────

  _applyCamera() {
    const { radius, theta, phi } = this.spherical;
    const sinPhi = Math.sin(phi);
    this.camera.position.set(
      this.target.x + radius * sinPhi * Math.cos(theta),
      this.target.y + radius * sinPhi * Math.sin(theta),
      this.target.z + radius * Math.cos(phi),
    );
    this.camera.lookAt(this.target);
    this.sun.target.position.copy(this.target);
    this.sun.target.updateMatrixWorld();
  }

  orbit(dx, dy) {
    this.spherical.theta -= dx * 0.006;
    this.spherical.phi = Math.max(0.04, Math.min(Math.PI * 0.495, this.spherical.phi - dy * 0.006));
    this._applyCamera();
  }

  pan(dx, dy) {
    const scale = this.spherical.radius * 0.0016;
    const right = new THREE.Vector3().subVectors(this.camera.position, this.target).cross(this.camera.up).normalize();
    const up = new THREE.Vector3().crossVectors(right, new THREE.Vector3().subVectors(this.camera.position, this.target)).normalize();
    this.target.addScaledVector(right, -dx * scale);
    this.target.addScaledVector(up, -dy * scale);
    this._applyCamera();
  }

  zoom(delta) {
    this.spherical.radius = Math.max(6, Math.min(1400, this.spherical.radius * (1 + delta * 0.0014)));
    this._applyCamera();
  }

  // ── Unreal-style flight ──────────────────────────────────────────────────────────────────────────────────────
  // Hold the right mouse button to mouse-look; WASD flies, Q/E drop and rise, Shift sprints, the wheel trims speed.
  // Look pivots about the camera *position* (the orbit target is pushed ahead of it) so the two models coexist: let
  // go of the right button and the usual orbit still turns around whatever you flew up to.

  look(dx, dy) {
    const pos = this.camera.position.clone();
    this.spherical.theta -= dx * 0.0042;
    this.spherical.phi = Math.max(0.02, Math.min(Math.PI - 0.02, this.spherical.phi - dy * 0.0042));
    const { radius, theta, phi } = this.spherical;
    const sinPhi = Math.sin(phi);
    this.target.set(
      pos.x - radius * sinPhi * Math.cos(theta),
      pos.y - radius * sinPhi * Math.sin(theta),
      pos.z - radius * Math.cos(phi),
    );
    this._applyCamera();
  }

  // input: { forward, strafe, rise } each in −1…1, plus a speed multiplier.
  fly(input, dt) {
    const { forward = 0, strafe = 0, rise = 0, boost = 1 } = input;
    if (!forward && !strafe && !rise) return false;
    const dir = new THREE.Vector3().subVectors(this.target, this.camera.position).normalize();
    const right = new THREE.Vector3().crossVectors(dir, new THREE.Vector3(0, 0, 1)).normalize();
    const step = this.flySpeed * boost * Math.min(dt, 0.1);
    const move = new THREE.Vector3()
      .addScaledVector(dir, forward * step)
      .addScaledVector(right, strafe * step)
      .addScaledVector(new THREE.Vector3(0, 0, 1), rise * step);
    this.target.add(move);
    this._applyCamera();
    return true;
  }

  adjustFlySpeed(delta) {
    this.flySpeed = Math.max(2, Math.min(400, this.flySpeed * (1 - delta * 0.0012)));
    return this.flySpeed;
  }

  frame(bounds) {
    if (!bounds) return;
    const cx = (bounds.min.x + bounds.max.x) * 0.5;
    const cy = (bounds.min.y + bounds.max.y) * 0.5;
    const cz = (bounds.min.z + bounds.max.z) * 0.5;
    const size = Math.max(bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, 20);
    this.target.set(cx, cy, cz);
    this.spherical.radius = size * 1.5 + 30;
    // Fog has to follow the scene: a city block and a 700 m showcase cannot share one depth cue, and a fixed
    // range would dissolve half the network into the background the moment you framed all of it.
    if (this.scene.fog) {
      this.scene.fog.near = Math.max(120, this.spherical.radius * 0.75);
      this.scene.fog.far = Math.max(600, this.spherical.radius * 3.2);
    }
    this._applyCamera();
  }

  setView(name) {
    if (name === 'top') {
      this.spherical.theta = -Math.PI / 2;
      this.spherical.phi = 0.05;
    } else if (name === 'front') {
      this.spherical.theta = -Math.PI / 2;
      this.spherical.phi = Math.PI * 0.46;
    } else if (name === 'iso') {
      this.spherical.theta = -Math.PI * 0.35;
      this.spherical.phi = Math.PI * 0.32;
    }
    this._applyCamera();
  }

  // ── picking ──────────────────────────────────────────────────────────────────────────────────────────────────────

  updatePointer(event) {
    const rect = this.canvas.getBoundingClientRect();
    this.pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    this.pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
    this.raycaster.setFromCamera(this.pointer, this.camera);
    return this.raycaster;
  }

  pickJunction() {
    const hits = this.raycaster.intersectObjects(this.junctionMeshes, false);
    return hits.length ? hits[0].object.userData : null;
  }

  pickHandle() {
    const hits = this.raycaster.intersectObjects(this.handleMeshes, false);
    return hits.length ? hits[0].object.userData : null;
  }

  pickGroundPoint(height = 0) {
    const plane = new THREE.Plane(new THREE.Vector3(0, 0, 1), -height);
    const hit = new THREE.Vector3();
    return this.raycaster.ray.intersectPlane(plane, hit) ? { x: hit.x, y: hit.y, z: height } : null;
  }

  pickSurface() {
    const meshes = [...this.meshes.values()];
    const hits = this.raycaster.intersectObjects(meshes, false);
    if (hits.length) return { x: hits[0].point.x, y: hits[0].point.y, z: hits[0].point.z };
    return this.pickGroundPoint(0);
  }

  // ── loop ─────────────────────────────────────────────────────────────────────────────────────────────────────────

  resize() {
    const w = this.canvas.clientWidth || 1;
    const h = this.canvas.clientHeight || 1;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  render() {
    this.gizmo.updateScale(this.camera);
    this.renderer.render(this.scene, this.camera);
  }
}

export { THREE };
