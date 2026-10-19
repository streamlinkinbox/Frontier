import { buildBridgeNetworks } from "./bridge-structures.js";
import { buildTransportRail, buildBridge } from "./transport-world.js";
import { mapBounds, CITY_ID } from "./sample-city.js";
import { tramJunctionLinks } from "./tram-network.js";
import { modern } from "./landscape-layout.js";
import { buildLandscapeRoad, buildSampleTram } from "./landscape-world.js";
import * as THREE from "three";
import { isWaterChannel } from "./drainage.js";

import { FlyCamera } from "./fly-camera.js";
import {
  generateStreetscape,
  tactilePrototype,
  auditStreetscape,
  rampCenters,
  gutterWidth,
  streetSettings,
} from "./streetscape.js";
import { streetMaterial, surfaceTexture } from "./street-materials.js";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { mergeGeometries } from "three/addons/utils/BufferGeometryUtils.js";
import { generateRoadMesh, roadY } from "./geometry.js";
import {
  terrainHeight,
  terrainSurface,
  riverX,
  roadWidth,
  dist,
} from "./model.js";
const COLORS = {
  crossingDeck: 0x92968e,
  signalRed: 0xcb443c,
  bridgeSteel: 0x487c80,
  parking: 0x6c7170,
  parkCar: 0xa66b55,
  tramBody: 0x3c8378,
  tramGlass: 0x293837,
  cycle: 0xad6257,
  planting: 0x76965d,
  trackgrass: 0x86a56c,
  refuge: 0xc3c5c1,
  asphalt: 0x4d5555,
  dirt: 0xad9671,
  curb: 0xd4d4bf,
  walk: 0xc6c6b4,
  white: 0xe4e4cf,
  yellow: 0xd8c181,
  bike: 0x598e7d,
  rail: 0x596465,
  railbed: 0x9e9b8a,
  tree: 0x567a58,
  tree2: 0x6a865a,
  trunk: 0x7b7661,
  building: 0xd8d7c6,
  roof: 0xbcbeb0,
  concrete: 0xc9cabb,
};
export class World {
  constructor(
    container,
    { onSelect, onGround, onHover, onChange, onNodeMove } = {},
  ) {
    this.container = container;
    this.callbacks = { onSelect, onGround, onHover, onChange, onNodeMove };
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(
      document.documentElement.dataset.theme === "dark" ? "#1c1c1c" : "#e7eae4",
    );
    this.root = new THREE.Group();
    this.scene.add(this.root);
    this.layers = {};
    this.mode = "3d";
    this.tool = "select";
    this.selected = null;
    this.options = {
      roads: true,
      buildings: true,
      vegetation: true,
      water: true,
      rail: true,
      markings: true,
      wireframe: false,
    };
    this.renderer = new THREE.WebGLRenderer({
      antialias: true,
      alpha: false,
      logarithmicDepthBuffer: true,
    });
    this.renderer.setPixelRatio(Math.min(Math.max(devicePixelRatio, 1.5), 2));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    container.appendChild(this.renderer.domElement);
    this.renderer.domElement.setAttribute(
      "aria-label",
      "Interactive 3D city and road editor",
    );
    this.camera = new THREE.OrthographicCamera(-300, 300, 220, -220, 0.1, 2400);
    this.camera.position.set(330, 440, 430);
    this.camera.lookAt(0, 0, 0);
    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.target.set(0, 0, 0);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.12;
    this.controls.maxPolarAngle = Math.PI * 0.47;
    this.controls.minZoom = 0.45;
    this.controls.maxZoom = 8;
    this.controls.mouseButtons = {
      LEFT: null,
      MIDDLE: THREE.MOUSE.PAN,
      RIGHT: THREE.MOUSE.ROTATE,
    };
    this.controls.addEventListener("change", () => {
      this.needsRender = true;
      onChange?.();
    });
    this.scene.add(new THREE.HemisphereLight(0xf8fbef, 0xa3a79a, 2.5));
    const sun = new THREE.DirectionalLight(0xfff4de, 3.1);
    sun.position.set(-140, 300, -100);
    sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048);
    Object.assign(sun.shadow.camera, {
      left: -350,
      right: 350,
      top: 300,
      bottom: -300,
      near: 1,
      far: 700,
    });
    sun.shadow.bias = -0.0005;
    sun.shadow.normalBias = 0.6;
    this.sun = sun;
    this.scene.add(sun, sun.target);
    this.ray = new THREE.Raycaster();
    this.pointer = new THREE.Vector2();
    this.groundPlane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
    this.down = null;
    container.addEventListener("pointerdown", (e) => {
      if (e.target !== this.renderer.domElement || e.button !== 0) return;
      this.down = { x: e.clientX, y: e.clientY };
      const pt = this.point(e),
        r = this.result?.roads.find((r) => r.e.id === this.selected);
      if (this.tool === "select" && pt && r) {
        const rect = this.renderer.domElement.getBoundingClientRect();
        const n = [r.a, r.b].find((n) => {
          const screen = this.project(
            n.x,
            terrainHeight(n.x, n.z, this.p.brushes) + 0.8,
            n.z,
          );
          return (
            Math.hypot(
              screen.x - (e.clientX - rect.left),
              screen.y - (e.clientY - rect.top),
            ) < 10
          );
        });
        if (n) {
          this.dragNode = n;
          this.renderer.domElement.setPointerCapture(e.pointerId);
          this.controls.enabled = false;
        }
      }
    });
    container.addEventListener("pointermove", (e) => {
      if (e.target !== this.renderer.domElement) return;
      const pt = this.point(e);
      if (pt) {
        onHover?.(pt);
        if (this.dragNode) {
          const connected = this.result.roads.filter(
            (r) => r.e.a === this.dragNode.id || r.e.b === this.dragNode.id,
          );
          this.preview(
            connected.flatMap((r) => [
              r.e.a === this.dragNode.id ? r.b : r.a,
              pt,
            ]),
          );
        }
      }
    });
    container.addEventListener("pointerup", (e) => {
      if (e.target !== this.renderer.domElement || e.button !== 0 || !this.down)
        return;
      if (this.dragNode) {
        const n = this.dragNode,
          pt = this.point(e),
          moved =
            Math.hypot(e.clientX - this.down.x, e.clientY - this.down.y) > 5;
        this.dragNode = null;
        this.controls.enabled = !this.fly.enabled;
        this.preview([]);
        if (this.renderer.domElement.hasPointerCapture(e.pointerId))
          this.renderer.domElement.releasePointerCapture(e.pointerId);
        if (moved && pt) {
          onNodeMove?.(n.id, pt);
          return;
        }
      }
      if (Math.hypot(e.clientX - this.down.x, e.clientY - this.down.y) > 5)
        return;
      const pt = this.point(e);
      if (!pt) return;
      if (this.tool === "select" || this.tool === "delete") {
        let nearest = null,
          best = 14 / this.camera.zoom;
        for (const r of this.result.roads) {
          const t = Math.max(
            0,
            Math.min(
              1,
              ((pt.x - r.a.x) * r.dx + (pt.z - r.a.z) * r.dz) / r.length,
            ),
          );
          const d = Math.hypot(
            pt.x - r.a.x - t * r.length * r.dx,
            pt.z - r.a.z - t * r.length * r.dz,
          );
          if (d < r.w / 2 + best && d < best) {
            best = d;
            nearest = r.e.id;
          }
        }
        onSelect?.(nearest);
      } else if (this.tool !== "pan") onGround?.(pt, e);
    });
    this.renderer.domElement.tabIndex = 0;
    this.planCamera = this.camera;
    this.flyCamera = new THREE.PerspectiveCamera(48, 1, 0.04, 2400);
    this.flyCamera.position.copy(this.camera.position).multiplyScalar(0.8);
    this.flyCamera.quaternion.copy(this.camera.quaternion);
    this.navigation = "fly";
    this.fly = new FlyCamera(this);
    this.lastFrame = performance.now();
    this.setNavigation("fly");
    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(container);
    this.resize();
    this.animate();
  }
  point(e) {
    const b = this.renderer.domElement.getBoundingClientRect();
    this.pointer.set(
      ((e.clientX - b.left) / b.width) * 2 - 1,
      (-(e.clientY - b.top) / b.height) * 2 + 1,
    );
    this.ray.setFromCamera(this.pointer, this.camera);
    if (this.terrain) {
      const hit = this.ray.intersectObject(this.terrain);
      if (hit.length)
        return { x: hit[0].point.x, y: hit[0].point.y, z: hit[0].point.z };
    }
    const hit = this.ray.ray.intersectPlane(
      this.groundPlane,
      new THREE.Vector3(),
    );
    return hit ? { x: hit.x, y: hit.y, z: hit.z } : null;
  }
  resize() {
    const w = this.container.clientWidth,
      h = this.container.clientHeight;
    if (!w || !h) return;
    this.renderer.setSize(w, h);
    this.needsRender = true;
    const span = this.mode === "split" ? 420 : 460;
    if (this.camera.isPerspectiveCamera) this.camera.aspect = w / h;
    this.camera.left = (-span * w) / h / 2;
    this.camera.right = (span * w) / h / 2;
    this.camera.top = span / 2;
    this.camera.bottom = -span / 2;
    this.camera.updateProjectionMatrix();
  }
  setNavigation(value) {
    this.fly?.stop();
    this.navigation = value;
    this.camera =
      this.mode === "2d" || value === "orbit"
        ? this.planCamera
        : this.flyCamera;
    this.controls.object = this.camera;
    this.controls.enabled = this.camera === this.planCamera;
    this.fly.enabled = this.mode !== "2d" && value === "fly";
    if (this.controls.enabled) this.controls.update();
    this.camera.updateMatrixWorld(true);
    this.resize();
    this.needsRender = true;
    this.callbacks.onChange?.();
  }
  setMode(mode) {
    this.mode = mode;
    this.fly.stop();
    if (mode === "2d") {
      this.inspectingId = null;
      if (this.highlight) this.highlight.visible = true;
      this.camera = this.planCamera;
      this.controls.object = this.camera;
      this.controls.enabled = true;
      this.fly.enabled = false;
      this.controls.enableRotate = false;
      this.camera.position.set(
        this.controls.target.x,
        650,
        this.controls.target.z + 0.001,
      );
      this.controls.update();
    } else {
      this.controls.enableRotate = true;
      if (this.navigation === "orbit") {
        this.planCamera.position
          .copy(this.controls.target)
          .add(new THREE.Vector3(330, 440, 430));
      }
      this.setNavigation(this.navigation);
    }
    this.camera.updateMatrixWorld(true);
    this.resize();
    this.needsRender = true;
    this.callbacks.onChange?.();
  }
  setTool(tool) {
    this.tool = tool;
    this.controls.mouseButtons.LEFT = tool === "pan" ? THREE.MOUSE.PAN : null;
    this.container.style.cursor =
      tool === "pan" ? "grab" : tool === "select" ? "default" : "crosshair";
  }
  zoom(value) {
    if (this.camera.isPerspectiveCamera) {
      this.camera.position.addScaledVector(
        this.camera.getWorldDirection(new THREE.Vector3()),
        (value - 1) * this.fly.speed * 3,
      );
      this.camera.updateMatrixWorld(true);
      this.needsRender = true;
      return;
    }
    this.camera.zoom = Math.max(0.05, Math.min(8, this.camera.zoom * value));
    this.camera.updateProjectionMatrix();
    this.needsRender = true;
  }
  frameArea(points, padding = 45) {
    if (!points.length) return;
    const xs = points.map((n) => n.x),
      zs = points.map((n) => n.z),
      cx = (Math.min(...xs) + Math.max(...xs)) / 2,
      cz = (Math.min(...zs) + Math.max(...zs)) / 2;
    const width = Math.max(...xs) - Math.min(...xs) + padding,
      height = Math.max(...zs) - Math.min(...zs) + padding,
      aspect =
        this.container.clientWidth / Math.max(1, this.container.clientHeight),
      span = Math.max(width / aspect, height, 40) * 1.2;
    this.inspectingId = null;
    this.controls.target.set(cx, 0, cz);
    if (this.camera.isPerspectiveCamera) {
      this.camera.position.set(cx + span * 0.35, span * 0.95, cz + span * 0.8);
      this.camera.lookAt(cx, 0, cz);
      this.camera.far = Math.max(2400, span * 6);
    } else {
      this.camera.position.set(cx, 650, cz + 0.001);
      this.camera.zoom =
        Math.min(
          (this.camera.right - this.camera.left) / width,
          (this.camera.top - this.camera.bottom) / height,
        ) * 0.85;
      this.controls.update();
    }
    this.camera.updateProjectionMatrix();
    this.camera.updateMatrixWorld(true);
    this.needsRender = true;
  }
  frameCity() {
    const roads = this.result.roads.filter(
      (r) =>
        r.e.district === CITY_ID &&
        r.e.sampleKey !== "connector" &&
        !r.e.transportSample,
    );
    this.frameArea(
      roads.flatMap((r) => [r.a, r.b]),
      65,
    );
  }
  home() {
    if (!this.p) return;
    this.frameArea(this.p.nodes, 90);
  }
  frameRoundabout(group) {
    const roads = this.result.roads.filter((r) => r.e.roundabout === group);
    if (!roads.length) return;
    const nodes = roads.flatMap((r) => [r.a, r.b]),
      xs = nodes.map((n) => n.x),
      zs = nodes.map((n) => n.z),
      cx = (Math.min(...xs) + Math.max(...xs)) / 2,
      cz = (Math.min(...zs) + Math.max(...zs)) / 2;
    const span =
        Math.max(
          Math.max(...xs) - Math.min(...xs),
          Math.max(...zs) - Math.min(...zs),
        ) + 18,
      target = new THREE.Vector3(cx, 0, cz);
    if (this.camera.isPerspectiveCamera) {
      this.camera.position
        .copy(target)
        .add(new THREE.Vector3(span * 0.85, span * 0.95, span * 0.85));
      this.camera.lookAt(target);
    } else {
      this.camera.position.add(target.clone().sub(this.controls.target));
      this.controls.target.copy(target);
      this.camera.zoom = Math.min(
        5,
        Math.min(
          this.camera.right - this.camera.left,
          this.camera.top - this.camera.bottom,
        ) / span,
      );
      this.controls.update();
    }
    this.camera.updateProjectionMatrix();
    this.camera.updateMatrixWorld(true);
    this.needsRender = true;
  }
  focus(id) {
    this.inspectingId = null;
    if (this.highlight) this.highlight.visible = true;
    const r = this.result.roads.find((r) => r.e.id === id);
    if (!r) return;
    const target = new THREE.Vector3(
        (r.a.x + r.b.x) / 2,
        0,
        (r.a.z + r.b.z) / 2,
      ),
      delta = target.clone().sub(this.controls.target);
    if (this.camera.isPerspectiveCamera) {
      this.camera.position.copy(target).add(new THREE.Vector3(32, 35, 42));
      this.camera.lookAt(target);
      this.camera.updateMatrixWorld(true);
      this.needsRender = true;
      return;
    }
    this.camera.position.add(delta);
    this.controls.target.copy(target);
    this.camera.zoom = 2.2;
    this.camera.updateProjectionMatrix();
    this.controls.update();
    this.camera.updateMatrixWorld(true);
    this.needsRender = true;
  }
  animate = () => {
    this.frame = requestAnimationFrame(this.animate);
    const now = performance.now(),
      dt = (now - this.lastFrame) / 1000;
    this.lastFrame = now;
    if (this.fly?.enabled) this.fly.update(dt);
    else if (this.controls.enabled) this.controls.update();
    if (
      this.needsRender &&
      this.container.clientWidth &&
      this.container.clientHeight
    ) {
      this.updateDetailLOD();
      this.renderer.render(this.scene, this.camera);
      this.needsRender = false;
      if (this.labelsCallback) this.labelsCallback(this);
    }
  };
  inspectCurb(id, drainView = false) {
    const r = this.result.roads.find((r) => r.e.id === id);
    if (!r || !r.e.sidewalk) return;
    this.mode = "3d";
    this.setNavigation("fly");
    this.fly.setSpeed(3);
    const ramp = this.streetscape?.ramps.find(
      (a) => a.road === id && a.side === 1,
    );
    const drain = this.streetscape?.drains.find(
      (d) => d.road === id && d.side === 1,
    );
    const s =
        drainView && drain
          ? (drain.start + drain.end) / 2
          : (ramp?.s ?? ((r.from + r.to) * r.length) / 2),
      base = roadY(this.p, r.e, s / r.length);
    const channelDetail = drainView && isWaterChannel(r.e),
      gutter = this.streetscape?.gutterWidth(r.e) || 0;
    const offset = r.w / 2 + (channelDetail ? gutter * 0.5 : gutter + 0.6);
    const target = new THREE.Vector3(
      r.a.x + r.dx * s + r.nx * offset,
      base + 0.08,
      r.a.z + r.dz * s + r.nz * offset,
    );
    this.camera.position
      .copy(target)
      .add(
        new THREE.Vector3(
          r.dx * (channelDetail ? 3.2 : 5) - r.nx * 2.3,
          channelDetail ? 1.8 : 2.5,
          r.dz * (channelDetail ? 3.2 : 5) - r.nz * 2.3,
        ),
      );
    this.camera.lookAt(target);
    this.camera.updateMatrixWorld(true);
    this.inspectingId = id;
    if (this.highlight) this.highlight.visible = false;
    this.needsRender = true;
  }
  project(x, y, z) {
    const v = new THREE.Vector3(x, y, z).project(this.camera);
    return {
      x: ((v.x + 1) / 2) * this.container.clientWidth,
      y: ((1 - v.y) / 2) * this.container.clientHeight,
      visible: v.z < 1,
    };
  }
  disposeGroup(group) {
    group.traverse((o) => {
      o.geometry?.dispose();
      if (o.isInstancedMesh) o.dispose();
      if (o.material) {
        (Array.isArray(o.material) ? o.material : [o.material]).forEach((m) =>
          m.dispose(),
        );
      }
    });
    group.clear();
  }
  rebuild(p, prepared = null) {
    this.needsRender = true;
    this.p = p;
    this.bounds = mapBounds(p);
    const bounds = this.bounds,
      span = Math.max(bounds.width, bounds.height);
    this.sun.target.position.set(bounds.x, 0, bounds.z);
    this.sun.position.set(
      bounds.x - 140,
      Math.max(300, span * 0.65),
      bounds.z - 100,
    );
    Object.assign(this.sun.shadow.camera, {
      left: -span * 0.7,
      right: span * 0.7,
      top: span * 0.7,
      bottom: -span * 0.7,
      far: Math.max(700, span * 2),
    });
    this.sun.shadow.camera.updateProjectionMatrix();
    this.disposeGroup(this.root);
    this.layers = {};
    this.result = prepared?.result ?? generateRoadMesh(p);
    this.preparedStreetscape = prepared?.streetscape;
    this.preparedStreetAudit = prepared?.streetAudit;
    this.batches = new Map();
    this.materials = {};
    for (const [k, v] of Object.entries(COLORS))
      this.materials[k] = new THREE.MeshStandardMaterial({
        color: v,
        roughness: 0.92,
      });
    const addLayer = (name) => {
      const g = new THREE.Group();
      this.root.add(g);
      this.layers[name] = g;
      return g;
    };
    [
      "land",
      "roads",
      "markings",
      "buildings",
      "vegetation",
      "water",
      "rail",
      "details",
    ].forEach(addLayer);
    this.buildTerrain();
    this.buildRoads();
    this.buildLandscape();
    this.buildRail();
    for (const [key, list] of this.batches) {
      const [layer, mat] = key.split(":");
      if (!list.length) continue;
      const geo = mergeGeometries(list, false);
      const object = new THREE.Mesh(geo, this.materials[mat]);
      object.castShadow = layer !== "markings" && layer !== "water";
      object.receiveShadow = true;
      this.layers[layer].add(object);
      list.forEach((g) => g.dispose());
    }
    this.setOptions(this.options);
    this.select(this.selected);
    return this.result;
  }
  batch(geo, mat, layer = "details") {
    const key = `${layer}:${mat}`;
    if (!this.batches.has(key)) this.batches.set(key, []);
    if (geo.index) {
      const flat = geo.toNonIndexed();
      geo.dispose();
      this.batches.get(key).push(flat);
    } else this.batches.get(key).push(geo);
  }
  box(x, y, z, sx, sy, sz, mat, layer = "details", angle = 0) {
    const g = new THREE.BoxGeometry(sx, sy, sz);
    g.rotateY(angle);
    g.translate(x, y, z);
    this.batch(g, mat, layer);
  }
  strip(a, b, width, height, mat, layer = "details") {
    const l = Math.hypot(b[0] - a[0], b[2] - a[2]);
    if (l < 0.01) return;
    const g = new THREE.BoxGeometry(width, height, l);
    const angle = Math.atan2(b[0] - a[0], b[2] - a[2]);
    g.rotateX(-Math.atan2(b[1] - a[1], l));
    g.rotateY(angle);
    g.translate(
      (a[0] + b[0]) / 2,
      (a[1] + b[1]) / 2 + height / 2,
      (a[2] + b[2]) / 2,
    );
    this.batch(g, mat, layer);
  }
  buildTerrain() {
    const p = this.p,
      bounds = this.bounds,
      geo = new THREE.PlaneGeometry(
        bounds.width,
        bounds.height,
        Math.min(400, Math.ceil(bounds.width / 4)),
        Math.min(240, Math.ceil(bounds.height / 4)),
      );
    geo.rotateX(-Math.PI / 2);
    geo.translate(bounds.x, 0, bounds.z);
    const pos = geo.attributes.position,
      colors = [];
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i),
        z = pos.getZ(i),
        h = terrainSurface(x, z, p.brushes);
      pos.setY(i, h);
      const noise = Math.sin(x * 0.035) * Math.cos(z * 0.03) * 0.006;
      const c = new THREE.Color(
        h < -0.5 ? "#a6b49c" : x < -165 ? "#9eae80" : "#a9b995",
      );
      c.offsetHSL(0, noise, noise);
      colors.push(c.r, c.g, c.b);
    }
    geo.setAttribute("color", new THREE.Float32BufferAttribute(colors, 3));
    geo.computeVertexNormals();
    this.terrain = new THREE.Mesh(
      geo,
      new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 1 }),
    );
    this.terrain.receiveShadow = true;
    this.layers.land.add(this.terrain);
    this.box(
      bounds.x,
      -5,
      bounds.z,
      bounds.width,
      6,
      bounds.height,
      "concrete",
      "land",
    );
    const waterMat = new THREE.MeshStandardMaterial({
      color: 0x81b1ad,
      roughness: 0.3,
      metalness: 0.12,
    });
    const pts = [];
    for (let z = bounds.minZ; z <= bounds.maxZ; z += 3) {
      const x = riverX(z),
        w = 11.4;
      pts.push(x - w, -0.65, z, x + w, -0.65, z);
    }
    const waterGeo = new THREE.BufferGeometry();
    waterGeo.setAttribute("position", new THREE.Float32BufferAttribute(pts, 3));
    const inds = [];
    for (let i = 0; i < pts.length / 3 - 2; i += 2)
      inds.push(i, i + 2, i + 1, i + 1, i + 2, i + 3);
    waterGeo.setIndex(inds);
    waterGeo.computeVertexNormals();
    this.layers.water.add(new THREE.Mesh(waterGeo, waterMat));
    const lake = new THREE.Mesh(new THREE.CircleGeometry(1, 64), waterMat);
    lake.rotation.x = -Math.PI / 2;
    lake.scale.set(25, 33, 1);
    lake.position.set(-151, -0.65, 72);
    this.layers.water.add(lake);
    // Short terrain-following crop strips, clipped away from roads and islands.
    // Long 46 m strips used to bridge terrain undulations and cross new junctions.
    const rings = new Map();
    for (const r of this.result.roads)
      if (r.e.roundaboutRole === "ring") {
        if (!rings.has(r.e.roundabout)) rings.set(r.e.roundabout, []);
        rings.get(r.e.roundabout).push(r.a, r.b);
      }
    const islands = [...rings.values()].map((nodes) => {
      const xs = nodes.map((n) => n.x),
        zs = nodes.map((n) => n.z);
      return {
        x: (Math.min(...xs) + Math.max(...xs)) / 2,
        z: (Math.min(...zs) + Math.max(...zs)) / 2,
        r:
          Math.max(
            Math.max(...xs) - Math.min(...xs),
            Math.max(...zs) - Math.min(...zs),
          ) / 2,
      };
    });
    const clear = (x, z) =>
      !islands.some((c) => Math.hypot(x - c.x, z - c.z) < c.r + 2) &&
      !this.result.roads.some((r) => {
        const t = Math.max(
          0,
          Math.min(r.length, (x - r.a.x) * r.dx + (z - r.a.z) * r.dz),
        );
        return (
          Math.hypot(x - r.a.x - t * r.dx, z - r.a.z - t * r.dz) <
          r.w / 2 + r.e.sidewalk + 3
        );
      });
    for (let field = 0; field < 4; field++) {
      const cx = -247 + (field % 2) * 43,
        cz = field < 2 ? 60 : 139;
      for (let row = -6; row <= 6; row++) {
        const x = cx + row * 2.2;
        for (let z = cz - 23; z < cz + 23; z += 2) {
          if (!clear(x, z + 1)) continue;
          this.strip(
            [x, terrainHeight(x, z, p.brushes) + 0.04, z],
            [x, terrainHeight(x, z + 2, p.brushes) + 0.04, z + 2],
            1.35,
            0.025,
            field % 2 ? "roof" : "tree2",
            "land",
          );
        }
      }
    }
  }
  buildRoads() {
    const { roads, junctions, mesh } = this.result,
      p = this.p;
    const roadGeo = mesh.geometry(null, "asphalt");
    this.materials.asphalt.map = surfaceTexture("asphalt");
    this.materials.asphalt.bumpMap = this.materials.asphalt.map;
    this.materials.asphalt.bumpScale = 0.009;
    const road = new THREE.Mesh(roadGeo, this.materials.asphalt);
    road.receiveShadow = true;
    this.layers.roads.add(road);
    for (const kind of new Set(mesh.surfaceKinds)) {
      if (kind === "asphalt") continue;
      const object = new THREE.Mesh(
        mesh.geometry(null, kind),
        this.materials[kind],
      );
      object.receiveShadow = true;
      object.castShadow = true;
      this.layers.roads.add(object);
    }
    for (const r of roads) {
      const { e, a, b, nx, nz, dx, dz, w, from, to, length, sections } = r;
      if (modern(e)) {
        buildLandscapeRoad(this, r);
        buildSampleTram(this, r);
        continue;
      }
      if (e.preset === "trail") {
        const m = new THREE.Mesh(mesh.geometry(e.id), this.materials.dirt);
        m.position.y = 0.02;
        this.layers.roads.add(m);
      }
      const at = (t, o = 0, y = 0) => [
        a.x + (b.x - a.x) * t + nx * o,
        roadY(p, e, t) + y,
        a.z + (b.z - a.z) * t + nz * o,
      ];
      for (let i = 1; i < sections.length; i++) {
        const t1 = from + ((to - from) * (i - 1)) / (sections.length - 1),
          t2 = from + ((to - from) * i) / (sections.length - 1);
        for (const side of [-1, 1]) {
          if (e.bike) {
            this.strip(
              at(t1, side * (w / 2 - 0.9), 0.015),
              at(t2, side * (w / 2 - 0.9), 0.015),
              1.45,
              0.015,
              "bike",
              "markings",
            );
            this.strip(
              at(t1, side * (w / 2 - 1.75), 0.04),
              at(t2, side * (w / 2 - 1.75), 0.04),
              0.12,
              0.015,
              "white",
              "markings",
            );
          }
        }
      }
      if (e.structure === "bridge" && !e.bridgeNetwork)
        buildBridge(this, r, at);
      if (e.roundaboutRole === "ring") {
        if (e.markings) this.arrow(at((from + to) / 2, 0, 0.055), dx, dz);
        continue;
      }
      if (
        e.roundaboutRole === "entry" &&
        e.markings &&
        (to - from) * length > 8
      ) {
        // Give-way marks on the inbound lane; the selected circulation sets handedness.
        const side = e.circulation === "clockwise" ? -1 : 1,
          s = from * length + 4,
          lane = (side * w) / 4;
        for (let j = 0; j < 3; j++) {
          const off = lane + (j - 1) * 0.65,
            a = at((s + 0.65) / length, off - 0.21, 0.055),
            b = at((s + 0.65) / length, off + 0.21, 0.055),
            c = at((s - 0.1) / length, off, 0.055);
          this.strip(a, b, 0.09, 0.01, "white", "markings");
          this.strip(b, c, 0.09, 0.01, "white", "markings");
          this.strip(c, a, 0.09, 0.01, "white", "markings");
        }
        if (s + 11 < to * length) {
          this.arrow(at((s + 8) / length, lane, 0.055), -dx, -dz);
          this.arrow(at((s + 8) / length, -lane, 0.055), dx, dz);
        }
        continue;
      }
      if (!e.markings || e.preset === "trail" || e.preset === "alley") continue;
      const edgeMat = e.preset === "country" ? "yellow" : "white";
      for (let lane = 1; lane < e.lanes; lane++) {
        const off = (lane - e.lanes / 2) * e.laneWidth;
        for (let s = from * length + 1; s < to * length - 1; s += 6) {
          const t = s / length,
            t2 = Math.min(to, (s + 2.6) / length);
          this.strip(
            at(t, off, 0.04),
            at(t2, off, 0.04),
            0.2,
            0.018,
            lane === e.lanes / 2 ? edgeMat : "white",
            "markings",
          );
        }
      }
      for (const side of [-1, 1]) {
        for (let i = 1; i < sections.length; i++) {
          const t1 = from + ((to - from) * (i - 1)) / (sections.length - 1),
            t2 = from + ((to - from) * i) / (sections.length - 1);
          this.strip(
            at(t1, side * (w / 2 - 0.25), 0.04),
            at(t2, side * (w / 2 - 0.25), 0.04),
            0.16,
            0.015,
            "white",
            "markings",
          );
        }
      }
      if (e.sidewalk > 0 && (to - from) * length > 7) {
        for (const [t, dir] of [
          [from + 3 / length, 1],
          [to - 3 / length, -1],
        ]) {
          if (t <= from || t >= to) continue;
          const nodeId = dir === 1 ? e.a : e.b,
            j = junctions.find((j) => j.id === nodeId);
          if (
            !j ||
            j.degree < 3 ||
            !rampCenters(r, junctions).some(
              (c) => Math.abs(c - t * length) < 1e-5,
            )
          )
            continue;
          for (let off = -w / 2 + 0.65; off < w / 2 - 0.4; off += 1.15) {
            const v = at(t, off, 0.05);
            this.box(
              v[0],
              v[1],
              v[2],
              0.55,
              0.018,
              2.5,
              "white",
              "markings",
              Math.atan2(dx, dz),
            );
          }
          const stop = t + (dir * 2.7) / length,
            v = at(stop, (-dir * w) / 4, 0.05);
          this.box(
            v[0],
            v[1],
            v[2],
            w / 2 - 0.5,
            0.018,
            0.32,
            "white",
            "markings",
            Math.atan2(dx, dz),
          );
        }
      }
      // Lane direction arrows, oriented with traffic, well back from the crossing.
      if (
        (to - from) * length > 18 &&
        e.lanes > 1 &&
        e.preset !== "country" &&
        (e.networkFlow !== "forward" || e.networkArrow)
      ) {
        for (const side of [-1, 1]) {
          const t = (from + to) / 2,
            v = at(t, side * e.laneWidth * 0.5, 0.07),
            dir = e.networkFlow === "forward" ? 1 : -side;
          this.arrow(v, dx * dir, dz * dir);
          if (e.bike) {
            const v2 = at(
              from + (to - from) * 0.62,
              side * (w / 2 - 0.9),
              0.05,
            );
            this.bicycle(v2, dx * dir, dz * dir);
          }
        }
      }
    }
    this.tramLinks = tramJunctionLinks(this.result);
    for (const link of this.tramLinks)
      for (const points of link.paths)
        for (let i = 1; i < points.length; i++)
          this.strip(points[i - 1], points[i], 0.075, 0.012, "rail", "rail");
    buildBridgeNetworks(this);
    this.buildStreetscape();
  }
  buildStreetscape() {
    this.streetscape =
      this.preparedStreetscape ?? generateStreetscape(this.p, this.result);
    this.preparedStreetscape = null;
    this.pavementCheck =
      this.preparedStreetAudit ?? auditStreetscape(this.streetscape);
    this.preparedStreetAudit = null;
    this.tactileBatches = [];
    for (const name of new Set(this.streetscape.mesh.groups)) {
      const geometry = this.streetscape.mesh.geometry(name),
        material = streetMaterial(name),
        object = new THREE.Mesh(geometry, material);
      object.receiveShadow = true;
      object.castShadow = true;
      this.layers.roads.add(object);
    }
    const prototype = tactilePrototype().geometry(),
      up = new THREE.Vector3(0, 1, 0),
      matrix = new THREE.Matrix4();
    for (const [color, list] of Object.entries(this.streetscape.instances)) {
      if (!list.length) continue;
      const material = streetMaterial(`tactile_${color}`),
        mesh = new THREE.InstancedMesh(
          prototype.clone(),
          material,
          list.length,
        );
      mesh.receiveShadow = true;
      mesh.count = 0;
      mesh.frustumCulled = false;
      this.tactileBatches.push({ mesh, list });
      this.layers.roads.add(mesh);
    }
    prototype.dispose();
  }
  updateDetailLOD() {
    if (this.highlight && this.camera.isPerspectiveCamera)
      for (const object of this.highlight.children.slice(1)) {
        const d = object.position.distanceTo(this.camera.position),
          scale = Math.min(
            1,
            Math.max(
              0.012,
              (2 * Math.tan((this.camera.fov * Math.PI) / 360) * d * 6) /
                Math.max(1, this.container.clientHeight) /
                1.8,
            ),
          );
        object.scale.setScalar(scale);
      }
    else if (this.highlight)
      for (const object of this.highlight.children.slice(1))
        object.scale.setScalar(1);

    const matrix = new THREE.Matrix4(),
      up = new THREE.Vector3(0, 1, 0),
      normal = new THREE.Vector3(),
      q = new THREE.Quaternion(),
      pos = new THREE.Vector3(),
      scale = new THREE.Vector3(1, 1, 1);
    for (const { mesh, list } of this.tactileBatches || []) {
      let count = 0;
      if (this.camera.isPerspectiveCamera)
        for (const item of list) {
          pos.set(...item.position);
          if (pos.distanceToSquared(this.camera.position) > 20 * 20) continue;
          normal.set(...item.normal).normalize();
          q.setFromUnitVectors(up, normal);
          matrix.compose(pos, q, scale);
          mesh.setMatrixAt(count++, matrix);
        }
      mesh.count = count;
      mesh.visible = count > 0;
      if (count) mesh.instanceMatrix.needsUpdate = true;
    }
  }
  arrow(v, dx, dz) {
    const nx = -dz,
      nz = dx;
    const local = (x, z) => [
      v[0] + nx * x + dx * z,
      v[1],
      v[2] + nz * x + dz * z,
    ];
    this.strip(local(0, -1.7), local(0, 1), 0.23, 0.015, "white", "markings");
    this.strip(
      local(-0.7, 0.3),
      local(0, 1.2),
      0.23,
      0.015,
      "white",
      "markings",
    );
    this.strip(
      local(0.7, 0.3),
      local(0, 1.2),
      0.23,
      0.015,
      "white",
      "markings",
    );
  }
  bicycle(v, dx, dz) {
    const nx = -dz,
      nz = dx;
    const pt = (x, z) => [v[0] + nx * x + dx * z, v[1], v[2] + nz * x + dz * z];
    for (const z of [-0.7, 0.7]) {
      const g = new THREE.TorusGeometry(0.32, 0.055, 4, 12);
      g.rotateX(-Math.PI / 2);
      g.translate(...pt(0, z));
      this.batch(g, "white", "markings");
    }
    this.strip(pt(0, -0.6), pt(0.4, 0), 0.09, 0.01, "white", "markings");
    this.strip(pt(0.4, 0), pt(0, 0.65), 0.09, 0.01, "white", "markings");
    this.strip(pt(0, -0.6), pt(-0.2, 0.2), 0.09, 0.01, "white", "markings");
    this.strip(pt(-0.2, 0.2), pt(0.4, 0), 0.09, 0.01, "white", "markings");
  }
  buildLandscape() {
    const p = this.p;
    let seed = 43;
    const random = () => {
      seed = (seed * 1664525 + 1013904223) >>> 0;
      return seed / 4294967296;
    };
    const roadDistance = (x, z) => {
      let d = 1e6;
      for (const r of this.result.roads) {
        const t = Math.max(
          0,
          Math.min(1, ((x - r.a.x) * r.dx + (z - r.a.z) * r.dz) / r.length),
        );
        d = Math.min(
          d,
          Math.hypot(
            x - r.a.x - t * r.length * r.dx,
            z - r.a.z - t * r.length * r.dz,
          ) -
            r.w / 2 -
            r.e.sidewalk,
        );
      }
      return d;
    };
    const occupied = [];
    for (const r of this.result.roads) {
      if (r.e.structure === "bridge") continue;
      if (
        ![
          "local",
          "avenue",
          "alley",
          "garden",
          "cycle",
          "tram",
          "access",
          "service",
        ].includes(r.e.preset)
      )
        continue;
      for (let s = 17; s < r.length - 12; s += 16 + random() * 6) {
        for (const side of [-1, 1]) {
          if (random() < 0.18) continue;
          const t = s / r.length,
            offset = side * (r.w / 2 + r.e.sidewalk + 8 + random() * 4),
            x = r.a.x + r.dx * s + r.nx * offset,
            z = r.a.z + r.dz * s + r.nz * offset;
          if (
            Math.abs(x - riverX(z)) < 25 ||
            roadDistance(x, z) < (r.e.bridgeNetwork ? 9 : 6.5) ||
            p.bridgeNetworks?.some(
              (n) =>
                n.id === "roundabout" &&
                Math.hypot(x - n.origin.x, z - n.origin.z) < 85,
            ) ||
            occupied.some((a) => Math.hypot(a.x - x, a.z - z) < 13)
          )
            continue;
          occupied.push({ x, z });
          const h = 3.8 + random() * 6 + (x > 70 && z < 10 ? random() * 13 : 0),
            y = terrainHeight(x, z, p.brushes),
            sx = 7 + random() * 4,
            sz = 7 + random() * 6,
            angle = Math.atan2(r.dx, r.dz);
          this.box(x, y + h / 2, z, sx, h, sz, "building", "buildings", angle);
          this.box(
            x,
            y + h + 0.18,
            z,
            sx + 0.4,
            0.45,
            sz + 0.4,
            "roof",
            "buildings",
            angle,
          );
          if (h > 10) {
            this.box(
              x + 1,
              y + h + 0.8,
              z - 1,
              2,
              1.2,
              2.5,
              "concrete",
              "buildings",
              angle,
            );
          } else {
            this.box(
              x,
              y + h + 0.8,
              z,
              sx * 0.7,
              1,
              sz * 0.7,
              "roof",
              "buildings",
              angle,
            );
          }
          // Small planted plot beside each building.
          if (random() > 0.3)
            this.tree(x + r.nx * 7, y, z + r.nz * 7, 2 + random());
        }
      }
    }
    for (let i = 0; i < 680; i++) {
      const x = -280 + random() * 560,
        z = -225 + random() * 450;
      if (
        Math.abs(x - riverX(z)) < 17 ||
        terrainSurface(x, z, p.brushes) < 0.1 ||
        roadDistance(x, z) < 5 ||
        occupied.some((a) => Math.hypot(a.x - x, a.z - z) < 9)
      )
        continue;
      if (x > -55 && x < 170 && z > -135 && z < 130 && random() < 0.6) continue;
      if (x < -220 && z > 20 && z < 175) continue;
      this.tree(x, terrainHeight(x, z, p.brushes), z, 2.1 + random() * 2.1);
    }
    if (p.sampleCityVersion === 1) {
      for (let i = 0; i < 220; i++) {
        const x = p.cityOrigin.x + random() * 700,
          z = p.cityOrigin.z - 285 + random() * 550;
        if (
          roadDistance(x, z) < 8 ||
          occupied.some((a) => Math.hypot(a.x - x, a.z - z) < 11) ||
          (p.corridors ?? []).some((c) =>
            c.points.some((n) => Math.hypot(n.x - x, n.z - z) < 12),
          )
        )
          continue;
        this.tree(x, terrainHeight(x, z, p.brushes), z, 1.8 + random() * 1.4);
      }
    }
    // Street lamps with a distinct arm and head.
    for (const r of this.result.roads) {
      if (r.e.sidewalk <= 0) continue;
      for (let s = r.from * r.length + 8; s < r.to * r.length - 8; s += 30) {
        const x =
            r.a.x +
            r.dx * s +
            r.nx * (r.w / 2 + gutterWidth(r.e) + r.e.sidewalk - 0.15),
          z =
            r.a.z +
            r.dz * s +
            r.nz * (r.w / 2 + gutterWidth(r.e) + r.e.sidewalk - 0.15),
          y = roadY(p, r.e, s / r.length) + streetSettings(r.e).curbHeight;
        this.box(x, y + 2.8, z, 0.14, 5.5, 0.14, "rail");
        this.box(
          x - r.nx * 0.6,
          y + 5.5,
          z - r.nz * 0.6,
          1.3,
          0.15,
          0.28,
          "rail",
          "details",
          -Math.atan2(r.nz, r.nx),
        );
      }
    }
  }
  tree(x, y, z, size) {
    const trunk = new THREE.CylinderGeometry(0.18, 0.28, size * 1.8, 5);
    trunk.translate(x, y + size * 0.9, z);
    this.batch(trunk, "trunk", "vegetation");
    const crown = new THREE.IcosahedronGeometry(size, 1);
    crown.scale(0.9, 1.2, 0.9);
    crown.translate(x, y + size * 1.9, z);
    this.batch(crown, Math.round(x + z) % 3 ? "tree" : "tree2", "vegetation");
  }
  buildRail() {
    buildTransportRail(this);
  }
  setOptions(options) {
    this.needsRender = true;
    Object.assign(this.options, options);
    for (const key of [
      "roads",
      "buildings",
      "vegetation",
      "water",
      "rail",
      "markings",
    ])
      if (this.layers[key]) this.layers[key].visible = this.options[key];
    if (this.wire) {
      this.root.remove(this.wire);
      this.wire.geometry.dispose();
      this.wire.material.dispose();
      this.wire = null;
    }
    if (this.options.wireframe && this.result) {
      const positions = [];
      for (const mesh of [this.result.mesh, this.streetscape?.mesh].filter(
        Boolean,
      )) {
        const seen = new Set();
        for (const f of mesh.faces)
          for (let i = 0; i < 4; i++) {
            const a = f[i],
              b = f[(i + 1) % 4],
              key = [a, b].sort((a, b) => a - b).join(":");
            if (seen.has(key)) continue;
            seen.add(key);
            for (const index of [a, b]) {
              const v = mesh.vertices[index];
              positions.push(v[0], v[1] + 0.005, v[2]);
            }
          }
      }
      const g = new THREE.BufferGeometry();
      g.setAttribute(
        "position",
        new THREE.Float32BufferAttribute(positions, 3),
      );
      this.wire = new THREE.LineSegments(
        g,
        new THREE.LineBasicMaterial({
          color: 0xb6dccb,
          transparent: true,
          opacity: 0.9,
        }),
      );
      this.root.add(this.wire);
    }
  }
  select(id) {
    this.needsRender = true;
    this.selected = id;
    if (this.highlight) {
      this.root.remove(this.highlight);
      this.disposeGroup(this.highlight);
    }
    this.highlight = new THREE.Group();
    if (id !== this.inspectingId) this.inspectingId = null;
    this.highlight.visible = id !== this.inspectingId;
    this.root.add(this.highlight);
    if (!id || !this.result) return;
    const r = this.result.roads.find((r) => r.e.id === id);
    if (!r) return;
    // Outline only: a translucent orange fill hid the actual asphalt/concrete.
    const edges = new Map();
    this.result.mesh.faces.forEach((f, i) => {
      if (this.result.mesh.groups[i] !== id) return;
      for (let k = 0; k < 4; k++) {
        const pair = [f[k], f[(k + 1) % 4]].sort((a, b) => a - b),
          key = pair.join(":");
        const entry = edges.get(key);
        if (entry) entry.count++;
        else edges.set(key, { pair, count: 1 });
      }
    });
    const positions = [];
    for (const { pair, count } of edges.values())
      if (count === 1)
        for (const v of pair) positions.push(...this.result.mesh.vertices[v]);
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute(
      "position",
      new THREE.Float32BufferAttribute(positions, 3),
    );
    const mesh = new THREE.LineSegments(
      geometry,
      new THREE.LineBasicMaterial({ color: 0xefad79, depthWrite: false }),
    );
    mesh.position.y = 0.035;
    this.highlight.add(mesh);
    for (const n of [r.a, r.b]) {
      const geo = new THREE.SphereGeometry(1.15, 16, 12),
        mat = new THREE.MeshBasicMaterial({ color: 0xffffff });
      const dot = new THREE.Mesh(geo, mat);
      dot.position.set(n.x, terrainHeight(n.x, n.z, this.p.brushes) + 0.8, n.z);
      this.highlight.add(dot);
      const ring = new THREE.Mesh(
        new THREE.TorusGeometry(1.8, 0.33, 6, 24),
        new THREE.MeshBasicMaterial({ color: 0xe78d58 }),
      );
      ring.rotation.x = -Math.PI / 2;
      ring.position.copy(dot.position);
      this.highlight.add(ring);
    }
  }
  preview(points, handles = true) {
    this.needsRender = true;
    if (this.previewGroup) {
      this.scene.remove(this.previewGroup);
      this.disposeGroup(this.previewGroup);
    }
    this.previewGroup = new THREE.Group();
    this.scene.add(this.previewGroup);
    if (!points.length) return;
    const verts = points.map(
      (p) =>
        new THREE.Vector3(
          p.x,
          terrainHeight(p.x, p.z, this.p?.brushes || []) + 1,
          p.z,
        ),
    );
    const g = new THREE.BufferGeometry().setFromPoints(verts);
    const line = new THREE.Line(
      g,
      new THREE.LineBasicMaterial({ color: 0xeb925e }),
    );
    this.previewGroup.add(line);
    for (const v of handles ? verts : []) {
      const m = new THREE.Mesh(
        new THREE.SphereGeometry(1.3, 12, 8),
        new THREE.MeshBasicMaterial({ color: 0xf4b488 }),
      );
      m.position.copy(v);
      this.previewGroup.add(m);
    }
  }
  destroy() {
    cancelAnimationFrame(this.frame);
    this.resizeObserver.disconnect();
    this.fly?.destroy();
    this.controls.dispose();
    this.disposeGroup(this.root);
    this.renderer.dispose();
    this.renderer.domElement.remove();
  }
}
