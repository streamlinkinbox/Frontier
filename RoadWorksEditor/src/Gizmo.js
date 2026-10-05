//============================================================================================================================================
//                                                                GIZMO.JS
//============================================================================================================================================
// A small, deterministic translate gizmo. The reference tool's gizmo drifted because it re-projected the pointer to a
// new plane every frame; this one records the grab offset once on pointer-down and then solves a single ray/plane (or
// ray/line) intersection per move, so the handle stays exactly under the cursor and the drag is frame-rate independent.
//
// Handles: X / Y / Z axes and a free XY plane pad. Z-up world.

import * as THREE from 'three';

const AXIS_COLORS = { x: 0xd2615c, y: 0x8fb469, z: 0x6f93c7, xy: 0xd6a665 };

export class TranslateGizmo {
  constructor(scene) {
    this.group = new THREE.Group();
    this.group.renderOrder = 999;
    this.group.visible = false;
    scene.add(this.group);
    this.handles = [];
    this.active = null;
    this.enabled = true;
    this.snap = 0;
    this.onChange = null;
    this.onCommit = null;

    this._buildAxis('x', new THREE.Vector3(1, 0, 0));
    this._buildAxis('y', new THREE.Vector3(0, 1, 0));
    this._buildAxis('z', new THREE.Vector3(0, 0, 1));
    this._buildPlanePad();
  }

  _material(color) {
    return new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.95, toneMapped: false });
  }

  _buildAxis(axis, dir) {
    const mat = this._material(AXIS_COLORS[axis]);
    const shaft = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.035, 1.0, 8), mat);
    const head = new THREE.Mesh(new THREE.ConeGeometry(0.1, 0.26, 10), mat);
    const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir);
    shaft.position.copy(dir.clone().multiplyScalar(0.5));
    shaft.quaternion.copy(q);
    head.position.copy(dir.clone().multiplyScalar(1.1));
    head.quaternion.copy(q);
    for (const m of [shaft, head]) {
      m.userData.gizmoAxis = axis;
      m.renderOrder = 999;
      this.group.add(m);
      this.handles.push(m);
    }
  }

  _buildPlanePad() {
    const mat = this._material(AXIS_COLORS.xy);
    mat.opacity = 0.42;
    mat.side = THREE.DoubleSide;
    const pad = new THREE.Mesh(new THREE.PlaneGeometry(0.34, 0.34), mat);
    pad.position.set(0.26, 0.26, 0);
    pad.userData.gizmoAxis = 'xy';
    pad.renderOrder = 999;
    this.group.add(pad);
    this.handles.push(pad);
  }

  attach(position) {
    this.group.position.set(position.x, position.y, position.z);
    this.group.visible = this.enabled;
  }

  detach() {
    this.group.visible = false;
    this.active = null;
  }

  updateScale(camera) {
    const d = this.group.position.distanceTo(camera.position);
    const s = Math.max(0.6, d * 0.085);
    this.group.scale.setScalar(s);
  }

  hitTest(raycaster) {
    if (!this.group.visible) return null;
    const hits = raycaster.intersectObjects(this.handles, false);
    return hits.length ? hits[0].object.userData.gizmoAxis : null;
  }

  // Begin a drag. `origin` is the world position of the thing being moved.
  begin(axis, raycaster, origin) {
    this.active = { axis, origin: origin.clone(), start: null };
    const p = this._solve(raycaster, axis, origin);
    this.active.start = p ? p.clone() : origin.clone();
    return true;
  }

  update(raycaster) {
    if (!this.active) return null;
    const p = this._solve(raycaster, this.active.axis, this.active.origin);
    if (!p) return null;
    const delta = p.clone().sub(this.active.start);
    if (this.active.axis === 'x') delta.set(delta.x, 0, 0);
    else if (this.active.axis === 'y') delta.set(0, delta.y, 0);
    else if (this.active.axis === 'z') delta.set(0, 0, delta.z);
    else delta.set(delta.x, delta.y, 0);

    const next = this.active.origin.clone().add(delta);
    if (this.snap > 0) {
      next.x = Math.round(next.x / this.snap) * this.snap;
      next.y = Math.round(next.y / this.snap) * this.snap;
      if (this.active.axis === 'z') next.z = Math.round(next.z / this.snap) * this.snap;
    }
    this.group.position.copy(next);
    return next;
  }

  end() {
    this.active = null;
  }

  // Ray → plane for the XY pad and the X/Y axes, ray → line for the Z axis.
  _solve(raycaster, axis, origin) {
    const ray = raycaster.ray;
    if (axis === 'z') {
      // closest point on the vertical line through origin
      const lineDir = new THREE.Vector3(0, 0, 1);
      const w0 = origin.clone().sub(ray.origin);
      const a = lineDir.dot(lineDir);
      const b = lineDir.dot(ray.direction);
      const c = ray.direction.dot(ray.direction);
      const d = lineDir.dot(w0);
      const e = ray.direction.dot(w0);
      const denom = a * c - b * b;
      if (Math.abs(denom) < 1e-6) return null;
      const t = (b * e - c * d) / denom;
      return origin.clone().add(lineDir.multiplyScalar(-t));
    }

    let normal;
    if (axis === 'xy') normal = new THREE.Vector3(0, 0, 1);
    else {
      // Pick whichever of the two containing planes faces the camera most directly.
      const toCam = ray.direction.clone().negate();
      if (axis === 'x') normal = Math.abs(toCam.z) > Math.abs(toCam.y) ? new THREE.Vector3(0, 0, 1) : new THREE.Vector3(0, 1, 0);
      else normal = Math.abs(toCam.z) > Math.abs(toCam.x) ? new THREE.Vector3(0, 0, 1) : new THREE.Vector3(1, 0, 0);
    }
    const plane = new THREE.Plane().setFromNormalAndCoplanarPoint(normal, origin);
    const hit = new THREE.Vector3();
    return ray.intersectPlane(plane, hit) ? hit : null;
  }
}
