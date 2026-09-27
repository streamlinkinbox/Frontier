import * as THREE from 'three';

const _q = new THREE.Quaternion(), _v = new THREE.Vector3(), _r = new THREE.Vector3();
const damp = (a, b, l, dt) => a + (b - a) * (1 - Math.exp(-l * dt));

// Third-person orbit camera for a wall-walking character.
// The orbit frame's up vector is a heavily smoothed blend of world-up and the surface normal, and
// its forward axis is parallel-transported, so floor->wall->ceiling transitions never snap or roll
// abruptly. Collides with the cave (sphere-cast approximated by a ray + margin).
export class CameraRig {
  constructor(camera, dom, world) {
    this.camera = camera; this.world = world;
    this.yaw = Math.PI * 0.85; this.pitch = 0.42; this.dist = 20; this.distCur = 20;
    this.minDist = 5; this.maxDist = 70;
    this.up = new THREE.Vector3(0, 1, 0);
    this.fwd = new THREE.Vector3(0, 0, 1);
    this.target = new THREE.Vector3();
    this.lastInput = -10; this.time = 0;
    this.follow = true;
    this.cinematic = false;
    this._bind(dom);
  }

  _bind(dom) {
    let dragging = false, lx = 0, ly = 0;
    dom.addEventListener('contextmenu', (e) => e.preventDefault());
    dom.addEventListener('pointerdown', (e) => { dragging = true; lx = e.clientX; ly = e.clientY; dom.setPointerCapture(e.pointerId); });
    dom.addEventListener('pointerup', (e) => { dragging = false; dom.releasePointerCapture(e.pointerId); });
    dom.addEventListener('pointermove', (e) => {
      if (!dragging) return;
      const dx = e.clientX - lx, dy = e.clientY - ly; lx = e.clientX; ly = e.clientY;
      this.yaw -= dx * 0.005; this.pitch = THREE.MathUtils.clamp(this.pitch + dy * 0.004, -0.35, 1.45);
      this.lastInput = this.time;
    });
    dom.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.dist = THREE.MathUtils.clamp(this.dist * Math.exp(e.deltaY * 0.0012), this.minDist, this.maxDist);
    }, { passive: false });
  }

  snap(target, surfaceUp, spiderFwd) {
    this.target.copy(target);
    this.up.copy(surfaceUp).lerp(new THREE.Vector3(0, 1, 0), 0.4).normalize();
    this.fwd.copy(spiderFwd).addScaledVector(this.up, -spiderFwd.dot(this.up)).normalize();
    this.yaw = Math.PI; this.distCur = this.dist;
  }

  update(dt, target, surfaceUp, spiderFwd, moving) {
    this.time += dt;
    // smoothed orbit frame
    const desiredUp = _v.copy(surfaceUp).lerp(new THREE.Vector3(0, 1, 0), 0.4).normalize();
    const k = 1 - Math.exp(-dt * 2.2);
    _q.setFromUnitVectors(this.up, _r.copy(this.up).lerp(desiredUp, k).normalize());
    this.up.applyQuaternion(_q).normalize();
    this.fwd.applyQuaternion(_q).addScaledVector(this.up, -this.fwd.dot(this.up)).normalize();
    this.target.x = damp(this.target.x, target.x, 9, dt);
    this.target.y = damp(this.target.y, target.y, 9, dt);
    this.target.z = damp(this.target.z, target.z, 9, dt);

    const right = new THREE.Vector3().crossVectors(this.fwd, this.up).normalize();
    // lazy follow: drift behind the spider when it moves and the user isn't orbiting
    if ((this.follow && moving && this.time - this.lastInput > 1.8) || this.cinematic) {
      const sf = _v.copy(spiderFwd).addScaledVector(this.up, -spiderFwd.dot(this.up));
      if (sf.lengthSq() > 1e-4) {
        sf.normalize();
        let want = Math.atan2(sf.dot(right), sf.dot(this.fwd)) + (this.cinematic ? 0.9 * Math.sin(this.time * 0.15) + 1.2 : 0);
        let d = want - this.yaw; d = Math.atan2(Math.sin(d), Math.cos(d));
        this.yaw += d * (1 - Math.exp(-dt * (this.cinematic ? 0.4 : 1.1)));
      }
      if (this.cinematic) this.pitch = damp(this.pitch, 0.25 + 0.15 * Math.sin(this.time * 0.21), 0.5, dt);
    }
    const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
    const horiz = new THREE.Vector3().addScaledVector(this.fwd, Math.cos(this.yaw)).addScaledVector(right, Math.sin(this.yaw));
    const dir = horiz.multiplyScalar(-cp).addScaledVector(this.up, sp).normalize();
    // collision
    let want = this.cinematic ? 14 + 6 * Math.sin(this.time * 0.17) : this.dist;
    const hit = this.world.raycast(this.target, dir, want + 1.5);
    let allowed = want;
    if (hit) allowed = Math.max(Math.min(want, hit.distance - 1.5), 1.5);
    this.distCur = allowed < this.distCur ? damp(this.distCur, allowed, 25, dt) : damp(this.distCur, allowed, 2.5, dt);
    this.camera.position.copy(this.target).addScaledVector(dir, this.distCur);
    this.camera.up.copy(this.up);
    this.camera.lookAt(this.target);
  }

  // Movement basis for camera-relative control projected onto the surface the spider stands on
  moveBasis(surfaceUp) {
    const f = new THREE.Vector3();
    this.camera.getWorldDirection(f);
    const camUp = new THREE.Vector3().copy(this.camera.up);
    const pf = f.clone().addScaledVector(surfaceUp, -f.dot(surfaceUp));
    const pu = camUp.addScaledVector(surfaceUp, -camUp.dot(surfaceUp));
    const w = THREE.MathUtils.smoothstep(pf.length(), 0.2, 0.5);
    const fwd = pf.normalize().multiplyScalar(w).addScaledVector(pu.normalize(), 1 - w).normalize();
    const right = new THREE.Vector3().crossVectors(fwd, surfaceUp).normalize();
    return { fwd, right };
  }
}
