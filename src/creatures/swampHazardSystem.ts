import * as THREE from 'three';
import { clamp, lerp } from '../math/noise';
import { CreatureSpawnNode } from '../world/trailCorridor';
import { VehicleRigidBody } from '../physics/vehiclePhysics';

export type CreatureKind = 'SIPHON_MOSQUITO' | 'ROBO_TICK';

interface ActiveCreature {
  id: number;
  chunkIndex: number;
  kind: CreatureKind;
  state: 'PATROL' | 'INTERCEPT' | 'LATCHED' | 'BLASTED';
  position: THREE.Vector3;
  homePos: THREE.Vector3;
  velocity: THREE.Vector3;
  attachLocal: THREE.Vector3;
  latchedTime: number;
  blastTimer: number;
  phase: number;
  group: THREE.Group;
  wingL?: THREE.Mesh;
  wingR?: THREE.Mesh;
  abdomenMesh?: THREE.Mesh;
}

const MAX_CREATURES = 36;

export class BiomeCreatureSystem {
  public readonly group = new THREE.Group();

  public onCreatureLatched?: (kind: CreatureKind) => void;
  public onCreaturesPurged?: (count: number) => void;

  private creatures: ActiveCreature[] = [];
  private nextId = 1;

  private chitinMat: THREE.MeshStandardMaterial;
  private fuelSacMat: THREE.MeshStandardMaterial;
  private wingMat: THREE.MeshBasicMaterial;
  private roboMetalMat: THREE.MeshStandardMaterial;
  private roboEyeMat: THREE.MeshStandardMaterial;

  constructor() {
    this.chitinMat = new THREE.MeshStandardMaterial({
      color: 0x22291e,
      roughness: 0.45,
      metalness: 0.25,
    });
    this.fuelSacMat = new THREE.MeshStandardMaterial({
      color: 0xd9381e,
      emissive: 0xa81b05,
      emissiveIntensity: 1.4,
      roughness: 0.3,
    });
    this.wingMat = new THREE.MeshBasicMaterial({
      color: 0xcbe8e0,
      transparent: true,
      opacity: 0.48,
      side: THREE.DoubleSide,
      depthWrite: false,
    });
    this.roboMetalMat = new THREE.MeshStandardMaterial({
      color: 0x323942,
      roughness: 0.3,
      metalness: 0.88,
    });
    this.roboEyeMat = new THREE.MeshStandardMaterial({
      color: 0xff2a1a,
      emissive: 0xff1e00,
      emissiveIntensity: 2.5,
      roughness: 0.2,
    });
  }

  public spawnForChunk(spawns: CreatureSpawnNode[]): void {
    for (const s of spawns) {
      if (this.creatures.length >= MAX_CREATURES) {
        const idx = this.creatures.findIndex((c) => c.state !== 'LATCHED');
        if (idx >= 0) {
          this.group.remove(this.creatures[idx].group);
          this.creatures.splice(idx, 1);
        } else {
          break;
        }
      }

      const built =
        s.kind === 'SIPHON_MOSQUITO' ? this.buildMosquitoMesh() : this.buildRoboTickMesh();
      built.group.position.copy(s.position);
      this.group.add(built.group);

      this.creatures.push({
        id: this.nextId++,
        chunkIndex: s.chunkIndex,
        kind: s.kind,
        state: 'PATROL',
        position: s.position.clone(),
        homePos: s.position.clone(),
        velocity: new THREE.Vector3(),
        attachLocal: new THREE.Vector3(
          (Math.random() - 0.5) * 1.1,
          s.kind === 'SIPHON_MOSQUITO' ? 0.85 : 0.48,
          (Math.random() - 0.5) * 2.2
        ),
        latchedTime: 0,
        blastTimer: 0,
        phase: Math.random() * Math.PI * 2,
        group: built.group,
        wingL: built.wingL,
        wingR: built.wingR,
        abdomenMesh: built.abdomenMesh,
      });
    }
  }

  public removeChunkCreatures(chunkIndex: number): void {
    for (let i = this.creatures.length - 1; i >= 0; i--) {
      const c = this.creatures[i];
      if (c.chunkIndex === chunkIndex && c.state !== 'LATCHED') {
        this.group.remove(c.group);
        this.creatures.splice(i, 1);
      }
    }
  }

  public clearAll(): void {
    for (const c of this.creatures) {
      this.group.remove(c.group);
    }
    this.creatures.length = 0;
  }

  /**
   * Blasts all latched Siphon Mosquitos & Robotic Ticks off the vehicle (called on [E] Hydraulic Shockwave)!
   */
  public purgeLatchedFromVehicle(vehicle: VehicleRigidBody): number {
    let count = 0;
    for (const c of this.creatures) {
      if (c.state === 'LATCHED') {
        c.state = 'BLASTED';
        c.blastTimer = 0;
        const outDir = c.position.clone().sub(vehicle.position).normalize();
        outDir.y = 0.85;
        c.velocity.copy(outDir.multiplyScalar(18.0)).add(vehicle.linearVelocity);
        count++;
      }
    }
    if (count > 0 && this.onCreaturesPurged) {
      this.onCreaturesPurged(count);
    }
    return count;
  }

  private buildMosquitoMesh(): {
    group: THREE.Group;
    wingL: THREE.Mesh;
    wingR: THREE.Mesh;
    abdomenMesh: THREE.Mesh;
  } {
    const g = new THREE.Group();

    const thorax = new THREE.Mesh(new THREE.SphereGeometry(0.28, 8, 8), this.chitinMat);
    thorax.scale.set(0.85, 0.85, 1.35);
    g.add(thorax);

    // Glowing Crimson Fuel-Siphon Abdomen (swells as it drains fuel!)
    const abdomen = new THREE.Mesh(new THREE.SphereGeometry(0.34, 9, 8), this.fuelSacMat);
    abdomen.scale.set(0.75, 0.75, 1.65);
    abdomen.position.set(0, -0.06, 0.58);
    g.add(abdomen);

    // Long Siphon Proboscis Needle
    const needleGeo = new THREE.ConeGeometry(0.045, 0.85, 6);
    needleGeo.rotateX(-Math.PI * 0.5);
    const needle = new THREE.Mesh(needleGeo, this.chitinMat);
    needle.position.set(0, -0.1, -0.58);
    g.add(needle);

    // Rapid-buzzing Translucent Wings
    const wingGeo = new THREE.PlaneGeometry(0.95, 0.34);
    wingGeo.translate(0.47, 0, 0);
    const wingL = new THREE.Mesh(wingGeo, this.wingMat);
    wingL.position.set(0.1, 0.2, 0);
    const wingR = new THREE.Mesh(wingGeo, this.wingMat);
    wingR.scale.x = -1;
    wingR.position.set(-0.1, 0.2, 0);
    g.add(wingL, wingR);

    g.scale.setScalar(1.25);
    return { group: g, wingL, wingR, abdomenMesh: abdomen };
  }

  private buildRoboTickMesh(): {
    group: THREE.Group;
    wingL?: THREE.Mesh;
    wingR?: THREE.Mesh;
    abdomenMesh?: THREE.Mesh;
  } {
    const g = new THREE.Group();

    // Heavy Armored Gunmetal Hexapod Carapace
    const shell = new THREE.Mesh(
      new THREE.SphereGeometry(0.48, 10, 8, 0, Math.PI * 2, 0, Math.PI * 0.62),
      this.roboMetalMat
    );
    shell.scale.set(1.1, 0.68, 1.35);
    shell.castShadow = true;
    g.add(shell);

    // Glowing Red Optic Scanner Eye + Dorsal Electromagnetic Clamp Coil
    const eye = new THREE.Mesh(new THREE.SphereGeometry(0.14, 8, 8), this.roboEyeMat);
    eye.position.set(0, 0.16, -0.52);
    g.add(eye);

    const coil = new THREE.Mesh(new THREE.CylinderGeometry(0.22, 0.26, 0.18, 8), this.roboEyeMat);
    coil.position.set(0, 0.32, 0.08);
    g.add(coil);

    // 6 Articulated Mechanical Clamp Legs
    const legGeo = new THREE.BoxGeometry(0.55, 0.08, 0.09);
    for (const side of [-1, 1]) {
      for (const zOff of [-0.32, 0.0, 0.34]) {
        const leg = new THREE.Mesh(legGeo, this.roboMetalMat);
        leg.position.set(side * 0.52, 0.02, zOff);
        leg.rotation.z = side * -0.45;
        g.add(leg);
      }
    }

    g.scale.setScalar(1.15);
    return { group: g };
  }

  private _tmpWorld = new THREE.Vector3();

  public update(
    dt: number,
    elapsedTime: number,
    vehicle: VehicleRigidBody,
    allowVehicleAttack = true
  ): void {
    let latchedMosquitos = 0;
    let latchedTicks = 0;

    for (let i = this.creatures.length - 1; i >= 0; i--) {
      const c = this.creatures[i];

      if (c.wingL && c.wingR) {
        const flap = Math.sin(elapsedTime * 62.0 + c.phase) * 0.68;
        c.wingL.rotation.z = flap;
        c.wingR.rotation.z = -flap;
      }

      if (!allowVehicleAttack && (c.state === 'INTERCEPT' || c.state === 'LATCHED')) {
        c.state = 'PATROL';
      }

      const distToCar = c.position.distanceTo(vehicle.position);

      if (c.state === 'PATROL') {
        if (c.kind === 'SIPHON_MOSQUITO') {
          c.position.x = c.homePos.x + Math.sin(elapsedTime * 2.2 + c.phase) * 4.5;
          c.position.y = c.homePos.y + Math.sin(elapsedTime * 3.5 + c.phase) * 0.9;
          c.position.z = c.homePos.z + Math.cos(elapsedTime * 1.9 + c.phase) * 4.5;
          if (allowVehicleAttack && distToCar < 36.0 && vehicle.position.z > c.position.z - 8) {
            c.state = 'INTERCEPT';
          }
        } else {
          // Robotic Tick waits & scans until vehicle passes within leap range!
          c.position.x = c.homePos.x + Math.sin(elapsedTime * 1.5 + c.phase) * 1.2;
          c.group.rotation.y = elapsedTime * 1.8 + c.phase;
          if (allowVehicleAttack && distToCar < 22.0 && vehicle.position.z > c.position.z - 5) {
            c.state = 'INTERCEPT';
            c.velocity.set(0, 6.5, 0);
          }
        }
        c.group.position.copy(c.position);
      } else if (c.state === 'INTERCEPT') {
        vehicle.localToWorld(c.attachLocal, this._tmpWorld);
        const toTarget = this._tmpWorld.clone().sub(c.position);
        const d = toTarget.length();

        if (d < 1.85 && latchedMosquitos + latchedTicks < 3) {
          c.state = 'LATCHED';
          c.latchedTime = 0;
          if (this.onCreatureLatched) this.onCreatureLatched(c.kind);
        } else if (distToCar > 52.0 || vehicle.position.z < c.position.z - 22.0) {
          c.state = 'PATROL';
        } else {
          const chaseSpeed = c.kind === 'SIPHON_MOSQUITO' ? 36.0 : 29.0;
          toTarget.normalize().multiplyScalar(chaseSpeed);
          c.velocity.lerp(toTarget, clamp(8.5 * dt, 0, 1));
          c.position.addScaledVector(c.velocity, dt);
          c.group.position.copy(c.position);
          c.group.lookAt(this._tmpWorld);
        }
      } else if (c.state === 'LATCHED') {
        c.latchedTime += dt;
        vehicle.localToWorld(c.attachLocal, c.position);
        c.group.position.copy(c.position);
        c.group.quaternion.copy(vehicle.quaternion);

        if (c.kind === 'SIPHON_MOSQUITO') {
          latchedMosquitos++;
          // Swell crimson abdomen visually as it sucks fuel!
          if (c.abdomenMesh) {
            const bloat = 1.0 + clamp(c.latchedTime * 0.18, 0, 0.75) + Math.sin(elapsedTime * 10) * 0.08;
            c.abdomenMesh.scale.set(0.75 * bloat, 0.75 * bloat, 1.65 * bloat);
          }
          // Overdrive afterburner heat or violent wrenching shakes mosquitos off after 2.2s!
          if ((vehicle.isBoosting && c.latchedTime > 1.2) || c.latchedTime > 8.5) {
            c.state = 'BLASTED';
            c.velocity.set((Math.random() - 0.5) * 14, 9.0, 8.0).add(vehicle.linearVelocity);
          }
        } else {
          latchedTicks++;
          // Robotic Tick clings stubbornly until blasted off by [E] Hydraulic Shockwave or 11s overload!
          if (c.latchedTime > 11.0) {
            c.state = 'BLASTED';
            c.velocity.set((Math.random() - 0.5) * 12, 8.0, 6.0).add(vehicle.linearVelocity);
          }
        }
      } else if (c.state === 'BLASTED') {
        c.blastTimer += dt;
        c.velocity.y -= 16.0 * dt;
        c.position.addScaledVector(c.velocity, dt);
        c.group.position.copy(c.position);
        c.group.rotation.x += 9.0 * dt;
        c.group.rotation.z += 7.0 * dt;
        if (c.blastTimer > 2.2) {
          this.group.remove(c.group);
          this.creatures.splice(i, 1);
        }
      }
    }

    vehicle.latchedMosquitoCount = latchedMosquitos;
    vehicle.latchedTickCount = latchedTicks;

    // Drain vehicle fuel when Siphon Mosquitos are latched!
    if (latchedMosquitos > 0) {
      vehicle.fuel = Math.max(0, vehicle.fuel - latchedMosquitos * 5.4 * dt);
    }
  }
}
