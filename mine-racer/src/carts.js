import * as THREE from 'three';
import { CFG } from './config.js';
import { clamp } from './util.js';

// ---------------------------------------------------------------------------
// Mine carts: rail traffic shuttling back and forth on each rail line.
// Rolling carts are LETHAL to touch; parked carts are soft obstacles.
// ---------------------------------------------------------------------------
export class CartSystem {
  constructor(scene, railLines) {
    this.carts = [];
    this.time = 0;

    const bodyMat = new THREE.MeshStandardMaterial({ color: 0x596068, metalness: 0.65, roughness: 0.5 });
    const darkMat = new THREE.MeshStandardMaterial({ color: 0x2c2f33, metalness: 0.6, roughness: 0.6 });
    const rockMat = new THREE.MeshStandardMaterial({ color: 0x7a6c5a, roughness: 0.95 });
    const hubMat = new THREE.MeshStandardMaterial({ color: 0xa8adb4, metalness: 0.85, roughness: 0.35 });
    const lampMat = new THREE.MeshStandardMaterial({ color: 0x201205, emissive: 0xffc477, emissiveIntensity: 2.5 });

    railLines.forEach((line, li) => {
      const nCarts = line.length > 150 ? 2 : 1;
      for (let ci = 0; ci < nCarts; ci++) {
        const g = new THREE.Group();

        const chassis = new THREE.Mesh(new THREE.BoxGeometry(1.5, 0.28, 2.3), darkMat);
        chassis.position.y = 0.52; chassis.castShadow = true; g.add(chassis);
        // hopper walls, flared outwards like real ore carts
        const sideL = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.85, 2.25), bodyMat);
        sideL.position.set(-0.72, 1.02, 0); sideL.rotation.z = -0.13; g.add(sideL);
        const sideR = sideL.clone(); sideR.position.x = 0.72; sideR.rotation.z = 0.13; g.add(sideR);
        const endF = new THREE.Mesh(new THREE.BoxGeometry(1.42, 0.85, 0.08), bodyMat);
        endF.position.set(0, 1.02, 1.12); endF.rotation.x = 0.13; g.add(endF);
        const endB = endF.clone(); endB.position.z = -1.12; endB.rotation.x = -0.13; g.add(endB);
        sideL.castShadow = endF.castShadow = true;
        // ore load
        for (let k = 0; k < 4; k++) {
          const r = new THREE.Mesh(new THREE.DodecahedronGeometry(0.3 + Math.random() * 0.16, 0), rockMat);
          r.position.set((Math.random() - 0.5) * 0.7, 1.35, (Math.random() - 0.5) * 1.3);
          r.rotation.set(Math.random() * 3, Math.random() * 3, Math.random() * 3);
          g.add(r);
        }
        // wheels (ride on the 1.5 m gauge)
        const wheels = [];
        for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
          const w = new THREE.Mesh(new THREE.CylinderGeometry(0.35, 0.35, 0.16, 12).rotateZ(Math.PI / 2), darkMat);
          w.position.set(sx * 0.78, 0.36, sz * 0.78);
          const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.13, 0.13, 0.18, 8).rotateZ(Math.PI / 2), hubMat);
          hub.position.copy(w.position);
          g.add(w, hub);
          wheels.push(w);
        }
        // tiny headlamp each end (direction changes)
        for (const sz of [-1, 1]) {
          const l = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.14, 0.06), lampMat);
          l.position.set(0, 1.22, sz * 1.18);
          g.add(l);
        }

        scene.add(g);
        const atStart = ci % 2 === 0;
        this.carts.push({
          g, wheels, line, li,
          u: atStart ? 0.02 : 0.98,
          dir: atStart ? 1 : -1,
          state: 'idle',
          wait: 2.5 + Math.random() * 7 + li * 3 + ci * 5,
          speed: 0,
          clackAcc: 0,
        });
      }
    });
    this._tmp = new THREE.Vector3();
  }

  // soft-obstacle circles for parked carts
  obstacles() {
    const out = [];
    for (const c of this.carts) {
      if (c.state !== 'idle') continue;
      const p = c.g.position;
      out.push({ x: p.x, z: p.z, r: 1.5 });
    }
    return out;
  }

  anyRolling(li) { return this.carts.some((c) => c.li === li && c.state === 'run'); }

  update(dt, playerPos, audio) {
    this.time += dt;
    let nearest = Infinity;
    let hit = null;

    for (const c of this.carts) {
      const { curve, length } = c.line;

      if (c.state === 'idle') {
        c.wait -= dt;
        if (c.wait <= 0) { c.state = 'run'; c.speed = 0; }
      } else {
        // accelerate, cruise, ease into the far end
        const target = c.dir > 0 ? 1 : 0;
        const remain = Math.abs(target - c.u) * length;
        const cruise = CFG.CART_SPEED;
        const desire = remain < 7 ? Math.max(1.4, cruise * (remain / 7)) : cruise;
        c.speed += clamp(desire - c.speed, -6 * dt * c.speed - 2 * dt, 5 * dt);
        c.speed = clamp(c.speed + (desire > c.speed ? 4 : -9) * dt * 0.5, 0.8, cruise);
        c.u += (c.dir * c.speed * dt) / length;

        // sleeper clack audio
        c.clackAcc += c.speed * dt;
        if (c.clackAcc > 0.85) {
          c.clackAcc = 0;
          const dd = this._tmp.copy(c.g.position).sub(playerPos).length();
          if (dd < 55 && audio) audio.clack(dd);
        }

        if ((c.dir > 0 && c.u >= target) || (c.dir < 0 && c.u <= target)) {
          c.u = target; c.state = 'idle'; c.dir *= -1;
          c.wait = 4 + Math.random() * 9;
          c.speed = 0;
        }
      }

      // place on track
      const p = curve.getPointAt(clamp(c.u, 0, 1));
      const t = curve.getTangentAt(clamp(c.u, 0, 1));
      c.g.position.set(p.x, 0.12, p.z);
      c.g.rotation.y = Math.atan2(t.x, t.z) + (c.dir < 0 ? Math.PI : 0);
      if (c.state === 'run') for (const w of c.wheels) w.rotation.x += (c.speed / 0.35) * dt;

      const d = this._tmp.copy(c.g.position).sub(playerPos).length();
      if (c.state === 'run') {
        nearest = Math.min(nearest, d);
        if (d < CFG.CART_HIT_RADIUS) hit = c;
      }
    }

    // warning beacons: flash red while anything rolls on that line
    const flashOn = Math.floor(this.time * 5) % 2 === 0;
    const seen = new Set();
    for (const c of this.carts) {
      if (seen.has(c.li)) continue;
      seen.add(c.li);
      const rolling = this.anyRolling(c.li);
      for (const b of c.line.beacons) {
        if (rolling) {
          b.mat.color.setHex(flashOn ? 0xff2a18 : 0x380a05);
          const s = flashOn ? 1.35 : 0.9;
          b.mesh.scale.setScalar(s);
        } else {
          b.mat.color.setHex(0x33200a);
          b.mesh.scale.setScalar(1);
        }
      }
    }

    return { nearest, hit };
  }
}
