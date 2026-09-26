import * as THREE from 'three';
import { CFG, GW, GH, tidx, tileToWorldX as WX, tileToWorldZ as WZ, worldToTileX, worldToTileZ } from './config.js';
import { clamp, lerp, damp } from './util.js';

const HALF = CFG.TILE / 2;

// ---------------------------------------------------------------------------
// Box car — simple but with a bit of detail: hood, cab + glass, fenders,
// bumpers, spare tyre, headlights, tail lights, roll bar, exhaust.
// ---------------------------------------------------------------------------
export class Car {
  constructor(scene) {
    this.group = new THREE.Group();       // world transform (pos + yaw)
    this.body = new THREE.Group();        // visual: tilt / bob applied here
    this.group.add(this.body);
    scene.add(this.group);

    const paint = new THREE.MeshStandardMaterial({ color: 0xe8632a, metalness: 0.25, roughness: 0.42 });
    const paintDark = new THREE.MeshStandardMaterial({ color: 0xb84a1e, metalness: 0.3, roughness: 0.5 });
    const darkTrim = new THREE.MeshStandardMaterial({ color: 0x1c1d20, metalness: 0.4, roughness: 0.6 });
    const glass = new THREE.MeshStandardMaterial({ color: 0x0e141b, metalness: 0.9, roughness: 0.12 });
    const tyre = new THREE.MeshStandardMaterial({ color: 0x141311, roughness: 0.92 });
    const hub = new THREE.MeshStandardMaterial({ color: 0x8d9299, metalness: 0.85, roughness: 0.35 });
    const lensF = new THREE.MeshStandardMaterial({ color: 0x3a3320, emissive: 0xffedb8, emissiveIntensity: 2.2 });
    this.tailMat = new THREE.MeshStandardMaterial({ color: 0x2a0505, emissive: 0xd41f14, emissiveIntensity: 0.8 });

    const add = (geo, mat, x, y, z, parent = this.body, cast = true) => {
      const m = new THREE.Mesh(geo, mat);
      m.position.set(x, y, z);
      m.castShadow = cast;
      parent.add(m);
      return m;
    };

    // main tub, hood, cab, glass band, roof
    add(new THREE.BoxGeometry(1.9, 0.62, 3.7), paint, 0, 0.66, 0);
    add(new THREE.BoxGeometry(1.72, 0.38, 1.05), paintDark, 0, 1.06, 1.28);
    add(new THREE.BoxGeometry(1.66, 0.68, 1.5), paint, 0, 1.3, -0.52);
    add(new THREE.BoxGeometry(1.7, 0.4, 1.32), glass, 0, 1.36, -0.52, this.body, false);
    add(new THREE.BoxGeometry(1.68, 0.1, 1.46), paintDark, 0, 1.7, -0.52);
    // grill + bumpers + fenders
    add(new THREE.BoxGeometry(1.5, 0.3, 0.07), darkTrim, 0, 0.78, 1.865);
    add(new THREE.BoxGeometry(2.02, 0.22, 0.3), darkTrim, 0, 0.52, 1.95);
    add(new THREE.BoxGeometry(2.02, 0.22, 0.3), darkTrim, 0, 0.52, -1.95);
    for (const sx of [-1, 1]) for (const sz of [-1, 1])
      add(new THREE.BoxGeometry(0.36, 0.14, 1.06), paintDark, sx * 0.99, 1.02, sz * 1.3);
    // roll bar + exhaust + spare tyre on the back deck
    for (const sx of [-1, 1]) add(new THREE.BoxGeometry(0.09, 0.72, 0.09), darkTrim, sx * 0.7, 1.62, -1.18);
    add(new THREE.BoxGeometry(1.49, 0.09, 0.09), darkTrim, 0, 1.95, -1.18);
    add(new THREE.CylinderGeometry(0.075, 0.095, 0.5, 8).rotateX(Math.PI / 2), darkTrim, 0.62, 0.4, -2.0);
    add(new THREE.CylinderGeometry(0.46, 0.46, 0.26, 12).rotateZ(Math.PI / 2), tyre, 0, 1.18, -1.7);
    add(new THREE.CylinderGeometry(0.2, 0.2, 0.28, 8).rotateZ(Math.PI / 2), hub, 0, 1.18, -1.7, this.body, false);
    // lights (visual)
    for (const sx of [-1, 1]) add(new THREE.BoxGeometry(0.24, 0.15, 0.06), lensF, sx * 0.6, 0.92, 1.87, this.body, false);
    for (const sx of [-1, 1]) add(new THREE.BoxGeometry(0.24, 0.14, 0.06), this.tailMat, sx * 0.62, 0.86, -1.87, this.body, false);

    // wheels
    this.wheels = [];
    for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
      const wg = new THREE.Group();
      wg.position.set(sx * 0.99, 0.44, sz * 1.3);
      const t = new THREE.Mesh(new THREE.CylinderGeometry(0.44, 0.44, 0.32, 14).rotateZ(Math.PI / 2), tyre);
      t.castShadow = true;
      const h = new THREE.Mesh(new THREE.CylinderGeometry(0.21, 0.21, 0.34, 8).rotateZ(Math.PI / 2), hub);
      wg.add(t, h);
      this.body.add(wg);
      this.wheels.push({ g: wg, front: sz > 0 });
    }

    // headlight spotlight (the only shadow caster among lights)
    this.head = new THREE.SpotLight(0xffe9c4, 640, 58, 0.52, 0.5, 1.35);
    this.head.position.set(0, 1.15, 1.5);
    this.head.castShadow = true;
    this.head.shadow.mapSize.set(1024, 1024);
    this.head.shadow.camera.near = 0.8;
    this.head.shadow.camera.far = 46;
    this.head.shadow.bias = -0.003;
    this.headTarget = new THREE.Object3D();
    this.headTarget.position.set(0, 0.35, 18);
    this.group.add(this.head, this.headTarget);
    this.head.target = this.headTarget;

    // small warm fill so the car stays readable between lanterns
    this.fill = new THREE.PointLight(0xffe2c0, 7, 13, 1.8);
    this.fill.position.set(0, 2.6, 0);
    this.group.add(this.fill);

    // state
    this.pos = new THREE.Vector3();
    this.vel = new THREE.Vector3();
    this.yaw = 0;
    this.steer = 0;
    this.speed = 0;
    this.wallBump = 0;   // impulse magnitude this frame (for audio/shake)

    this.reset(0, 0, 0);
  }

  reset(x, z, yaw) {
    this.pos.set(x, 0, z);
    this.vel.set(0, 0, 0);
    this.yaw = yaw;
    this.speed = 0;
    this.steer = 0;
    this.group.position.copy(this.pos);
    this.group.rotation.y = yaw;
  }

  get forward() { return new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw)); }

  // input: { steer -1..1, throttle -1..1, handbrake bool }
  update(dt, input, maze, obstacles) {
    const C = CFG.CAR;
    this.wallBump = 0;
    const spd = this.speed;

    // --- steering ---
    const maxSteer = 0.62 / (1 + Math.abs(spd) * 0.052);
    this.steer = lerp(this.steer, input.steer * maxSteer, damp(11, dt));
    if (Math.abs(spd) > 0.15) {
      this.yaw += this.steer * (spd / 2.25) * dt;
    }

    // --- throttle / brake ---
    if (input.throttle > 0) {
      this.speed = spd < 0
        ? spd + C.BRAKE * 0.7 * dt
        : spd + C.ACCEL * (1 - spd / C.MAX_SPEED) * dt;
    } else if (input.throttle < 0) {
      this.speed = spd > 0.4
        ? spd - C.BRAKE * dt
        : Math.max(spd - C.REVERSE_ACCEL * dt, -C.MAX_REVERSE);
    } else {
      this.speed = spd - Math.sign(spd) * Math.min(Math.abs(spd), C.ROLL_DRAG * dt * 3.2);
    }
    if (input.handbrake) this.speed -= this.speed * 1.9 * dt;

    // --- grip / drift: velocity chases the heading vector ---
    const fwd = this.forward;
    const targetVel = fwd.clone().multiplyScalar(this.speed);
    const drifting = input.handbrake || (Math.abs(this.steer) > 0.24 && Math.abs(this.speed) > 8);
    const grip = input.handbrake ? 2.1 : drifting ? 4.2 : 8.5;
    this.vel.lerp(targetVel, damp(grip, dt));
    this.driftAmount = clamp(this.vel.clone().sub(targetVel).length() / 8, 0, 1);

    this.pos.addScaledVector(this.vel, dt);

    // --- collide with the rock (two circles: front & rear) ---
    this.collideWithWalls(maze, fwd, 0.95, 1.02);
    this.collideWithWalls(maze, fwd, -0.95, 1.02);

    // --- soft obstacles (parked carts etc.) ---
    for (const ob of obstacles) {
      const dx = this.pos.x - ob.x, dz = this.pos.z - ob.z;
      const rr = ob.r + 1.15;
      const d2 = dx * dx + dz * dz;
      if (d2 < rr * rr && d2 > 1e-6) {
        const d = Math.sqrt(d2), nx = dx / d, nz = dz / d;
        this.pos.x += nx * (rr - d); this.pos.z += nz * (rr - d);
        const vn = this.vel.x * nx + this.vel.z * nz;
        if (vn < 0) { this.vel.x -= nx * vn * 1.2; this.vel.z -= nz * vn * 1.2; }
      }
    }

    // --- apply transform + juicy secondary motion ---
    this.group.position.copy(this.pos);
    this.group.rotation.y = this.yaw;
    const sN = clamp(this.speed / C.MAX_SPEED, -1, 1);
    this.body.rotation.z = -this.steer * Math.abs(sN) * 1.35;
    this.body.rotation.x = clamp((this._lastSpeed ?? this.speed) - this.speed, -1, 1) * 0.09;
    this.body.position.y = Math.sin(performance.now() * 0.011) * 0.022 * Math.abs(sN) + this.driftAmount * 0.02;
    this._lastSpeed = this.speed;
    for (const w of this.wheels) {
      w.g.rotation.x += (this.speed / 0.44) * dt;
      if (w.front) w.g.rotation.y = this.steer * 1.9;
    }
    this.tailMat.emissiveIntensity = input.throttle < 0 ? 3.2 : 0.8;
  }

  collideWithWalls(maze, fwd, along, r) {
    const cx = this.pos.x + fwd.x * along;
    const cz = this.pos.z + fwd.z * along;
    const tx = worldToTileX(cx), tz = worldToTileZ(cz);
    for (let ix = tx - 1; ix <= tx + 1; ix++) for (let iz = tz - 1; iz <= tz + 1; iz++) {
      if (maze.isOpen(ix, iz)) continue;
      if (ix < 0 || iz < 0 || ix >= GW || iz >= GH) continue;
      const bx = WX(ix), bz = WZ(iz);
      const qx = clamp(cx, bx - HALF, bx + HALF);
      const qz = clamp(cz, bz - HALF, bz + HALF);
      let dx = cx - qx, dz = cz - qz;
      const d2 = dx * dx + dz * dz;
      if (d2 >= r * r) continue;
      let d = Math.sqrt(d2), nx, nz;
      if (d < 1e-5) { nx = 0; nz = cz > bz ? 1 : -1; d = 0; }
      else { nx = dx / d; nz = dz / d; }
      const push = (r - d) * 0.5;
      this.pos.x += nx * push; this.pos.z += nz * push;
      const vn = this.vel.x * nx + this.vel.z * nz;
      if (vn < 0) {
        this.vel.x -= nx * vn * 1.45;
        this.vel.z -= nz * vn * 1.45;
        this.wallBump = Math.max(this.wallBump, -vn);
        this.speed *= 1 - clamp(-vn * 0.022, 0, 0.5);
      }
    }
  }
}
