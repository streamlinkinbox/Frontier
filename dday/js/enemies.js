// MG turrets (burst fire, lead targeting, LOS), bullets with tracers, dive bombers dropping bombs
import * as THREE from '../vendor/three.module.min.js';
import { clamp, lerp, rng, TAU } from './util.js';
import { buildPlane, buildBombGeo, MAT } from './models.js';

const R = rng(4242);
const BULLET_SPEED = 240;

export class Enemies {
  constructor(scene, T, world, fx, audio) {
    this.scene = scene; this.T = T; this.W = world; this.fx = fx; this.audio = audio;
    // bullets: short tracer streaks (discrete rounds, not beams)
    this.maxB = 400;
    const tg = new THREE.BoxGeometry(0.07, 0.07, 1.1); tg.translate(0, 0, -0.55);
    this.bMesh = new THREE.InstancedMesh(tg, new THREE.MeshBasicMaterial({ color: 0xffc766 }), this.maxB);
    this.bMesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage); this.bMesh.frustumCulled = false; this.bMesh.count = 0;
    scene.add(this.bMesh);
    this.bullets = [];
    this.planes = []; this.bombs = [];
    this.bombGeo = buildBombGeo();
    this.planeT = 22; this.m = new THREE.Matrix4(); this.q = new THREE.Quaternion(); this.v = new THREE.Vector3();
    this.spotted = 0;
  }
  reset() {
    for (const p of this.planes) this.scene.remove(p.obj);
    for (const b of this.bombs) this.scene.remove(b.mesh);
    this.planes = []; this.bombs = []; this.bullets = []; this.planeT = 20;
  }

  update(dt, car, game) {
    const cp = car.pos;
    this.spotted = 0;
    // ---------------- turrets ----------------
    for (const t of this.W.turrets) {
      const dx = cp.x - t.pos.x, dz = cp.z - t.pos.z, dist = Math.hypot(dx, dz);
      const inRange = dist < t.range && game.live && car.hp > 0;
      let canSee = false;
      if (inRange) {
        t.losT = (t.losT || 0) - dt;
        if (t.losT <= 0) { t.losT = 0.25; t.los = this.T.los(t.pos.x, t.pos.y + 0.3, t.pos.z, cp.x, cp.y + 1, cp.z); }
        canSee = t.los;
      }
      let targetYaw, targetPitch;
      if (canSee) {
        t.alert = Math.min(1, t.alert + dt * 1.5);
        this.spotted++;
        const tof = dist / BULLET_SPEED;
        const ax = cp.x + car.vel.x * tof * 0.95, az = cp.z + car.vel.z * tof * 0.95, ay = cp.y + 0.8 + car.vel.y * tof;
        const lx = ax - t.pos.x, lz = az - t.pos.z, ly = ay - t.pos.y;
        targetYaw = Math.atan2(lx, lz);
        const hd = Math.hypot(lx, lz);
        targetPitch = -Math.atan2(ly + 0.5 * 9.8 * tof * tof * 0, hd);
      } else {
        t.alert = Math.max(0, t.alert - dt * 0.4);
        t.scan += dt * 0.5;
        targetYaw = t.baseYaw + Math.sin(t.scan) * 1.1;
        targetPitch = 0.05;
      }
      // slew limits (fast cars can outrun the traverse up close)
      const worldYaw = t.baseYaw + t.yaw;
      let dy = Math.atan2(Math.sin(targetYaw - worldYaw), Math.cos(targetYaw - worldYaw));
      const slew = canSee ? 1.25 : 0.6;
      t.yaw += clamp(dy, -slew * dt, slew * dt);
      t.pitch += clamp(targetPitch - t.pitch, -0.8 * dt, 0.8 * dt);
      t.pitch = clamp(t.pitch, -0.35, 0.6);
      t.yawObj.rotation.y = t.yaw; t.pitchObj.rotation.x = t.pitch;
      t.lamp.material.color.setHex(canSee ? (Math.sin(game.time * 20) > 0 ? 0xff3322 : 0x551111) : t.alert > 0 ? 0xffaa22 : 0x3a3a3a);
      // firing
      t.cool -= dt;
      for (const f of t.flashes) f.visible = false;
      if (canSee && Math.abs(dy) < 0.09 && t.alert > 0.55) {
        if (t.burst <= 0 && t.cool <= 0) { t.burst = 9 + Math.floor(R() * 6); t.shotT = 0; }
      }
      if (t.burst > 0) {
        t.shotT -= dt;
        if (t.shotT <= 0) {
          t.shotT = 1 / 13; t.burst--; t.barrel ^= 1;
          if (t.burst <= 0) t.cool = R.range(1.1, 1.9);
          this.fire(t, dist);
        }
      }
    }
    // ---------------- bullets ----------------
    let n = 0;
    for (let i = this.bullets.length - 1; i >= 0; i--) {
      const b = this.bullets[i];
      const ox = b.x, oy = b.y, oz = b.z;
      b.x += b.vx * dt; b.y += b.vy * dt; b.z += b.vz * dt; b.life -= dt;
      // hit car? segment vs car capsule approximation
      const hit = segSphere(ox, oy, oz, b.x, b.y, b.z, cp.x, cp.y + 0.8, cp.z, 1.35);
      if (hit && car.hp > 0) {
        car.damage(game.bulletDmg, 'mg');
        this.fx.spark(cp.x + (R() - 0.5), cp.y + 0.9 + R() * 0.4, cp.z + (R() - 0.5), 5);
        this.audio.ping();
        this.bullets.splice(i, 1); continue;
      }
      // near-miss crack
      if (!b.cracked && Math.hypot(b.x - cp.x, b.z - cp.z) < 6) { b.cracked = true; this.audio.crack(); }
      const gh = this.T.heightAt(b.x, b.z);
      if (b.y < gh || b.life <= 0) {
        if (b.y < gh + 0.5) {
          const wl = game.water.level;
          if (gh < wl) this.fx.splash(b.x, wl, b.z, 3);
          else this.fx.dust(b.x, gh + 0.1, b.z, b.z < 330 ? new THREE.Color('#d6c294') : new THREE.Color('#7b6a4f'), 3, 0.45, 2.2);
        }
        this.bullets.splice(i, 1); continue;
      }
      if (n < this.maxB && b.tracer) {
        this.v.set(b.x, b.y, b.z);
        this.q.setFromUnitVectors(new THREE.Vector3(0, 0, 1), new THREE.Vector3(b.vx, b.vy, b.vz).normalize());
        this.m.compose(this.v, this.q, new THREE.Vector3(1, 1, 1));
        this.bMesh.setMatrixAt(n++, this.m);
      }
    }
    this.bMesh.count = n; this.bMesh.instanceMatrix.needsUpdate = true;

    // ---------------- planes ----------------
    if (game.live) {
      this.planeT -= dt;
      if (this.planeT <= 0 && cp.z > 60) { this.spawnPlane(car); this.planeT = R.range(game.planeGap[0], game.planeGap[1]); }
    }
    let nearest = 1e9;
    for (let i = this.planes.length - 1; i >= 0; i--) {
      const p = this.planes[i];
      p.t += dt;
      // gentle steering toward the car's future position until release
      if (!p.dropped) {
        const T = Math.sqrt(2 * Math.max(5, p.pos.y - cp.y) / 9.8);
        const tx = cp.x + car.vel.x * (T + p.eta()), tz = cp.z + car.vel.z * (T + p.eta());
        const want = Math.atan2(tx - p.pos.x, tz - p.pos.z);
        const d = Math.atan2(Math.sin(want - p.heading), Math.cos(want - p.heading));
        p.heading += clamp(d, -0.35 * dt, 0.35 * dt);
        p.bank = lerp(p.bank, clamp(d * 2.5, -0.7, 0.7), 1 - Math.exp(-3 * dt));
        // release when the bomb's ballistic impact lands on the predicted car position
        const ix = p.pos.x + Math.sin(p.heading) * p.speed * T, iz = p.pos.z + Math.cos(p.heading) * p.speed * T;
        const cx = cp.x + car.vel.x * T, cz = cp.z + car.vel.z * T;
        const miss = Math.hypot(ix - cx, iz - cz);
        const along = (cx - p.pos.x) * Math.sin(p.heading) + (cz - p.pos.z) * Math.cos(p.heading);
        if (along > 0 && miss < 16 && along < p.speed * T + 10) { p.dropped = true; p.dropQueue = 3; p.dropT = 0; }
      } else {
        p.bank = lerp(p.bank, 0, 1 - Math.exp(-2 * dt));
        p.climb = Math.min(p.climb + dt * 6, 14);
      }
      if (p.dropQueue > 0) {
        p.dropT -= dt;
        if (p.dropT <= 0) { p.dropT = 0.2; p.dropQueue--; this.dropBomb(p, cp); if (p.dropQueue === 0) p.obj.userData.bombMesh.visible = false; }
      }
      p.pos.x += Math.sin(p.heading) * p.speed * dt; p.pos.z += Math.cos(p.heading) * p.speed * dt;
      p.pos.y += p.climb * dt;
      // keep altitude over terrain
      const gh = this.T.heightAt(p.pos.x, p.pos.z);
      if (!p.dropped && p.pos.y < gh + 45) p.pos.y += (gh + 45 - p.pos.y) * dt;
      p.obj.position.copy(p.pos);
      p.obj.rotation.set(-p.climb * 0.02, p.heading, -p.bank, 'YXZ');
      p.obj.userData.prop.rotation.z += dt * 60;
      const d = p.pos.distanceTo(cp); nearest = Math.min(nearest, d);
      if (p.t > 40 || (p.dropped && d > 700)) { this.scene.remove(p.obj); this.planes.splice(i, 1); }
    }
    this.audio.planes(nearest);
    this.nearestPlane = nearest;

    // ---------------- bombs ----------------
    for (let i = this.bombs.length - 1; i >= 0; i--) {
      const b = this.bombs[i];
      b.vel.y -= 9.8 * dt;
      b.pos.addScaledVector(b.vel, dt);
      b.mesh.position.copy(b.pos);
      b.mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), b.vel.clone().normalize());
      const gh = this.T.heightAt(b.pos.x, b.pos.z);
      const wl = game.water.level;
      if (b.pos.y <= Math.max(gh, wl)) {
        this.scene.remove(b.mesh); this.bombs.splice(i, 1);
        game.explode(b.pos.x, Math.max(gh, wl), b.pos.z, { radius: 11, dmg: 62, size: 1.5, lift: 11, water: wl > gh + 0.5, crater: 3.6, cause: 'bomb' });
      }
    }
  }

  fire(t, dist) {
    t.pitchObj.updateWorldMatrix(true, false);
    const m = t.pitchObj.matrixWorld;
    const muzzle = new THREE.Vector3(t.barrel ? -0.24 : 0.24, 0, 1.8).applyMatrix4(m);
    const dir = new THREE.Vector3(0, 0, 1).transformDirection(m);
    const spread = 0.011 + (dist > 120 ? 0.004 : 0);
    dir.x += (R() - 0.5) * spread * 2; dir.y += (R() - 0.5) * spread * 2; dir.z += (R() - 0.5) * spread * 2; dir.normalize();
    this.bullets.push({ x: muzzle.x, y: muzzle.y, z: muzzle.z, vx: dir.x * BULLET_SPEED, vy: dir.y * BULLET_SPEED, vz: dir.z * BULLET_SPEED, life: 1.3, tracer: true });
    const f = t.flashes[t.barrel]; f.visible = true; f.rotation.z = R() * TAU; f.scale.setScalar(0.8 + R() * 0.6);
    this.audio.mg(dist);
  }

  spawnPlane(car) {
    const cp = car.pos;
    const obj = buildPlane();
    // come in from ahead (inland) or from the flanks
    const a = R.range(-0.9, 0.9);
    const dist = 650;
    const start = new THREE.Vector3(cp.x + Math.sin(a) * dist, 0, cp.z + Math.cos(a) * dist);
    start.x = clamp(start.x, -420, 420);
    start.y = this.T.heightAt(clamp(start.x, -240, 240), clamp(start.z, -300, 1700)) + 55 + R() * 15;
    const heading = Math.atan2(cp.x - start.x, cp.z - start.z);
    const p = { obj, pos: start, heading, speed: 62, bank: 0, climb: 0, t: 0, dropped: false, dropQueue: 0 };
    p.eta = () => 0;
    obj.position.copy(start); this.scene.add(obj);
    this.planes.push(p);
    this.onPlane && this.onPlane(p);
  }
  dropBomb(p, cp) {
    const mesh = new THREE.Mesh(this.bombGeo, MAT.metal); mesh.castShadow = true;
    const pos = p.pos.clone().add(new THREE.Vector3(0, -1.2, 0));
    const vel = new THREE.Vector3(Math.sin(p.heading) * p.speed, -1, Math.cos(p.heading) * p.speed);
    mesh.position.copy(pos); this.scene.add(mesh);
    this.bombs.push({ mesh, pos, vel });
    const T = Math.sqrt(2 * Math.max(5, pos.y - cp.y) / 9.8);
    this.audio.whistle(T, pos.distanceTo(cp));
  }
}

function segSphere(ax, ay, az, bx, by, bz, cx, cy, cz, r) {
  const dx = bx - ax, dy = by - ay, dz = bz - az, L2 = dx * dx + dy * dy + dz * dz || 1e-9;
  let t = ((cx - ax) * dx + (cy - ay) * dy + (cz - az) * dz) / L2; t = clamp(t, 0, 1);
  const x = ax + dx * t - cx, y = ay + dy * t - cy, z = az + dz * t - cz;
  return x * x + y * y + z * z < r * r;
}
