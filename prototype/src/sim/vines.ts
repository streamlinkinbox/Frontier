import { Vector3 } from 'three';
import { clamp, clamp01, damp, smoothstep } from '../core/mathx';
import { Rng } from '../core/rng';
import { SPECIES, SpeciesId, SpeciesSpec, VEHICLE } from './tuning';
import type { LatchTarget, Vehicle } from './vehicle';
import type { Track } from './track';

export type VineState =
  | 'dormant'
  | 'germinating'
  | 'extending'
  | 'mature'
  | 'latching'
  | 'latched'
  | 'creeping'
  | 'breaking'
  | 'broken';

export interface Latch {
  /** Body-local contact point. */
  anchorLocal: Vector3;
  /** Contact point on the body proxy that was grabbed. */
  proxyName: string;
  kind: 'panel' | 'wheel';
  /** Vine length at the moment of the grab — the rest length of the constraint. */
  naturalLength: number;
  /** 0 -> 1 adhesion ramp. */
  adhesion: number;
  /** Accumulated lateral peel (m). */
  peel: number;
  /** Blade contact time accumulated by the cutter. */
  cut: number;
  age: number;
}

export interface Vine {
  id: number;
  species: SpeciesId;
  /** Distance along the corridor where this vine was seeded (set by the director). */
  spawnS: number;
  /** Distance along the corridor the canopy root hangs over. */
  rootS: number;
  /** Lateral offset of the root from the corridor centreline. */
  rootLateral: number;
  spec: SpeciesSpec;
  state: VineState;
  /** Canopy anchor (fixed). */
  root: Vector3;
  /** Current free tip. */
  tip: Vector3;
  /** Where the vine is aiming. */
  target: Vector3;
  /** Accumulated grown length from the root to the tip. */
  length: number;
  latch: Latch | null;
  age: number;
  stateTime: number;
  tension: number;
  /** Smoothed tension used for audio and VFX. */
  tensionSmooth: number;
  /** Random phase for the idle sway. */
  phase: number;
  /** Seconds since the vine was created (drives germination). */
  germinateDelay: number;
  /** Last contact point in world space (for rendering / VFX). */
  contactWorld: Vector3;
  /** Closest the tip ever came to the car, in metres (tuning telemetry). */
  nearMiss: number;
  dead: boolean;
}

const TMP = new Vector3();
const TMP2 = new Vector3();

/** What the vine system needs to know about the player to aim a vine. */
export interface VineContext {
  /** Distance along the corridor the player is at. */
  playerS: number;
  /** Lateral offset of the player from the corridor centreline (m). */
  playerLateral: number;
  /** Rate of change of that lateral offset (m/s). */
  playerLateralVel: number;
  track: Track;
}

export interface CutterState {
  active: boolean;
  t: number;
  cooldown: number;
  /** Angle of the blade wedge centre (rad, relative to vehicle forward). */
  angle: number;
}

export class VineSystem {
  vines: Vine[] = [];
  private rng: Rng;
  cutter: CutterState = { active: false, t: 0, cooldown: 0, angle: 0 };

  /** Highest tension on any latch, for the HUD. */
  maxTension = 0;
  latchCount = 0;
  /** Total vines spawned this run. */
  spawnCount = 0;
  /** Set when a vine latches this frame (used for camera/audio feedback). */
  justLatched: Vine | null = null;
  justBroke: Vine | null = null;
  justSnapped: Vine | null = null;

  private nextId = 1;
  private targets: LatchTarget[] = [];

  constructor(seed = 0x51ed) {
    this.rng = new Rng(seed);
  }

  spawn(species: SpeciesId, root: Vector3, germinateDelay: number): Vine {
    const spec = SPECIES[species];
    const vine: Vine = {
      id: this.nextId++,
      species,
      spawnS: 0,
      rootS: 0,
      rootLateral: 0,
      spec,
      state: 'dormant',
      root: root.clone(),
      tip: root.clone(),
      target: root.clone(),
      length: 0,
      latch: null,
      age: 0,
      stateTime: 0,
      tension: 0,
      tensionSmooth: 0,
      phase: this.rng.float() * Math.PI * 2,
      germinateDelay,
      contactWorld: new Vector3(),
      nearMiss: Infinity,
      dead: false,
    };
    this.vines.push(vine);
    this.spawnCount++;
    return vine;
  }

  get cutterReady(): boolean {
    return this.cutter.cooldown <= 0;
  }

  triggerCutter(): boolean {
    if (!this.cutterReady) return false;
    this.cutter.active = true;
    this.cutter.t = 0;
    this.cutter.cooldown = VEHICLE.cutterCooldown;
    return true;
  }

  update(dt: number, vehicle: Vehicle, ctx: VineContext): void {
    this.cutter.cooldown = Math.max(0, this.cutter.cooldown - dt);
    if (this.cutter.active) {
      this.cutter.t += dt;
      const sweep = 0.28;
      this.cutter.angle = -1.22 + (2.44 * clamp01(this.cutter.t / sweep));
      if (this.cutter.t > sweep + 0.22) {
        this.cutter.active = false;
        this.cutter.t = 0;
      }
    }

    this.maxTension = 0;
    this.latchCount = 0;
    this.justLatched = null;
    this.justBroke = null;
    this.justSnapped = null;

    vehicle.latchTargets(this.targets);

    // ---- external modifiers driven by what is latched onto the car
    let grip = 1;
    let downforce = 1;
    let extraDrag = 0;
    let steering = 1;

    for (const vine of this.vines) {
      this.updateVine(vine, dt, vehicle, ctx);
      if (vine.latch && vine.state !== 'breaking') {
        this.latchCount++;
        this.maxTension = Math.max(this.maxTension, vine.tension);
        grip = Math.min(grip, 1 - vine.spec.tractionLoss);
        downforce = Math.min(downforce, 1 - vine.spec.downforceLoss);
        extraDrag += vine.spec.tractionLoss * 2600;
        steering = Math.min(steering, 1 - vine.spec.tractionLoss * 0.55);
      }
    }

    vehicle.gripScale = clamp(grip, 0.32, 1);
    vehicle.downforceScale = clamp(downforce, 0.6, 1);
    vehicle.extraDrag = extraDrag;
    vehicle.steeringScale = clamp(steering, 0.45, 1);

    // reap dead vines
    for (let i = this.vines.length - 1; i >= 0; i--) {
      if (this.vines[i].dead) this.vines.splice(i, 1);
    }
  }

  private updateVine(vine: Vine, dt: number, vehicle: Vehicle, ctx: VineContext): void {
    vine.age += dt;
    vine.stateTime += dt;
    const spec = vine.spec;

    switch (vine.state) {
      case 'dormant': {
        if (vine.age > vine.germinateDelay) {
          vine.state = 'germinating';
          vine.stateTime = 0;
        }
        break;
      }

      case 'germinating': {
        if (vine.stateTime > 0.55) {
          vine.state = 'extending';
          vine.stateTime = 0;
        }
        break;
      }

      case 'extending': {
        // Aim at where the player will be when they reach this station: the
        // vine hangs into the racing line ahead instead of chasing the car.
        this.aimAtInterception(vine, vehicle, ctx);

        // a vine that touches the car grabs it, whatever it was doing
        this.tryLatch(vine, vehicle);

        // The tip drops faster than it reaches, so the vine hangs before it whips.
        TMP.copy(vine.target).sub(vine.tip);
        const dy = TMP.y;
        TMP.y *= 1.7;
        const dist = TMP.length();
        if (dist > 1e-4) {
          TMP.multiplyScalar(1 / dist);
          const step = Math.min(dist, spec.extensionSpeed * dt);
          vine.tip.addScaledVector(TMP, step);
          vine.length += step;
        }
        void dy;

        // gusting sway
        vine.tip.x += Math.sin(vine.age * 1.7 + vine.phase) * 0.12 * dt;
        vine.tip.z += Math.cos(vine.age * 1.3 + vine.phase) * 0.12 * dt;

        const distToTarget = vine.tip.distanceTo(vine.target);
        if (distToTarget < 1.6 || vine.length > spec.length * 2.4) {
          vine.state = 'mature';
          vine.stateTime = 0;
        }
        break;
      }

      case 'mature': {
        // Hang and search: small involuntary sweeps, re-aiming at the player.
        this.aimAtInterception(vine, vehicle, ctx);

        TMP.copy(vine.target).sub(vine.tip);
        const dist = TMP.length();
        // a hanging vine only makes small adjustments, it does not slide
        // along the trail after the player
        if (dist > 0.01 && dist < 4.5) {
          TMP.multiplyScalar(1 / dist);
          const step = Math.min(dist, spec.extensionSpeed * 0.55 * dt);
          vine.tip.addScaledVector(TMP, step);
          // a hanging vine sways, it does not keep growing: the stem length is
          // what the latch uses as its rest length
        }
        vine.tip.x += Math.sin(vine.age * 2.1 + vine.phase) * 0.35 * dt;
        vine.tip.z += Math.cos(vine.age * 1.8 + vine.phase) * 0.35 * dt;

        this.tryLatch(vine, vehicle);
        break;
      }

      case 'latching': {
        vine.latch!.adhesion = smoothstep(0, 0.15, vine.stateTime);
        this.solveLatch(vine, dt, vehicle);
        if (vine.stateTime > 0.15) {
          vine.state = 'latched';
          vine.stateTime = 0;
        }
        break;
      }

      case 'latched':
      case 'creeping': {
        vine.latch!.adhesion = 1;
        vine.latch!.age += dt;
        this.solveLatch(vine, dt, vehicle);
        break;
      }

      case 'breaking': {
        vine.tension = damp(vine.tension, 0, 12, dt);
        if (vine.stateTime > 0.12) {
          vine.state = 'broken';
          vine.stateTime = 0;
          vine.dead = true;
          this.justSnapped = vine;
        }
        break;
      }

      case 'broken':
        vine.dead = true;
        break;
    }

    vine.tensionSmooth = damp(vine.tensionSmooth, vine.tension, 12, dt);
  }

  /**
   * Where the vine should be pointing.
   *
   * The canopy root hangs over a fixed station of the trail. The tip grows
   * toward the point the player will occupy when they get there, so a vine
   * drops into the racing line ahead of the car instead of trailing behind
   * it. Change the player's speed and the interception point moves; change
   * nothing and the vine is waiting where the player is going to be.
   */
  private aimAtInterception(vine: Vine, vehicle: Vehicle, ctx: VineContext): void {
    const gap = Math.max(0, vine.rootS - ctx.playerS);
    const speed = Math.max(8, vehicle.vel.length());
    const lead = clamp(gap / speed, 0, 4);

    // where the player will be laterally when they reach this station
    const sample = ctx.track.sampleAt(vine.rootS);
    const lateral = clamp(
      ctx.playerLateral + ctx.playerLateralVel * lead,
      -sample.width * 0.45,
      sample.width * 0.45,
    );

    // the interception point: on the trail, at car height, under the canopy
    vine.target
      .copy(sample.pos)
      .addScaledVector(sample.side, lateral)
      .addScaledVector(sample.up, 0.34);

    // never aim past the root: a vine hangs from the canopy, it does not
    // stretch across the trail
    const maxReach = vine.spec.length * 2.2;
    TMP.copy(vine.target).sub(vine.root);
    const d = TMP.length();
    if (d > maxReach) {
      vine.target.copy(vine.root).addScaledVector(TMP, maxReach / d);
    }
  }

  private tryLatch(vine: Vine, vehicle: Vehicle): void {
    const spec = vine.spec;
    for (const t of this.targets) {
      const d = vine.tip.distanceTo(t.world);
      vine.nearMiss = Math.min(vine.nearMiss, d - spec.tipRadius - t.radius);
      if (d > spec.tipRadius + t.radius) continue;

      // contact point on the surface of the proxy
      const contact = t.world.clone();
      TMP.copy(vine.tip).sub(t.world);
      if (TMP.lengthSq() > 1e-8) {
        TMP.normalize();
        contact.addScaledVector(TMP, t.radius);
      }

      const naturalLength = Math.max(0.4, vine.length);
      vine.latch = {
        anchorLocal: contact.clone().sub(vehicle.pos).applyQuaternion(vehicle.quat.clone().invert()),
        proxyName: t.name,
        kind: t.kind,
        naturalLength,
        adhesion: 0,
        peel: 0,
        cut: 0,
        age: 0,
      };
      vine.contactWorld.copy(contact);
      vine.tip.copy(contact);
      vine.state = 'latching';
      vine.stateTime = 0;
      vine.tension = 0;
      // contact is contact: the grab is announced on the frame it happens
      this.justLatched = vine;
      return;
    }
  }

  /**
   * The core of the mechanic: a spring-damper between the canopy root and the
   * contact point on the car, resolved into the same force/torque accumulator
   * the tyres use. The player feels a real pull, not a scripted slowdown.
   */
  private solveLatch(vine: Vine, dt: number, vehicle: Vehicle): void {
    const spec = vine.spec;
    const latch = vine.latch!;

    // contact point in world space, following the body
    const world = TMP.copy(latch.anchorLocal).applyQuaternion(vehicle.quat).add(vehicle.pos);
    vine.contactWorld.copy(world);

    // vector from the contact up to the canopy root
    const toRoot = TMP2.copy(vine.root).sub(world);
    const dist = toRoot.length();
    if (dist < 1e-4) return;
    toRoot.multiplyScalar(1 / dist);

    // the vine's rest length: how long it was when it grabbed
    const stretch = dist - latch.naturalLength;

    // relative velocity of the contact along the vine axis
    const rel = TMP.copy(world).sub(vehicle.pos);
    const vPoint = new Vector3()
      .copy(vehicle.vel)
      .add(new Vector3().crossVectors(vehicle.angVel, rel));
    const vAlong = vPoint.dot(toRoot);

    let tension = 0;
    if (stretch > 0) {
      tension = spec.stiffness * stretch + spec.damping * vAlong;
      tension *= latch.adhesion;
      tension = Math.max(0, tension);
    }

    vine.tension = tension;

    if (tension > 0) {
      const force = new Vector3().copy(toRoot).multiplyScalar(tension);
      vehicle.applyForceAtPoint(force, world);
    }

    // ---- creep: the vine takes up slack and wraps further back
    if (vine.state === 'latched' && vine.stateTime > 1.2) {
      vine.state = 'creeping';
      vine.stateTime = 0;
    }
    if (vine.state === 'creeping') {
      const creepRate = spec.creep;
      latch.anchorLocal.z = Math.max(-2.2, latch.anchorLocal.z - creepRate * dt);
      latch.naturalLength = Math.max(0.5, latch.naturalLength - creepRate * dt * 0.85);
    }

    // ---- peel: a hard flick slides the contact along the body and shakes it loose
    const vLocal = vPoint.clone().applyQuaternion(vehicle.quat.clone().invert());
    const lateralSpeed = Math.abs(vLocal.x);
    const peelThreshold = 1.4 + spec.grip * 2.2;
    if (lateralSpeed > peelThreshold) {
      const excess = lateralSpeed - peelThreshold;
      latch.peel += excess * dt;
      // the contact slides laterally across the body
      const slide = Math.sign(vLocal.x) * excess * dt * 0.5;
      latch.anchorLocal.x = clamp(latch.anchorLocal.x - slide, -1.25, 1.25);
    } else {
      latch.peel = Math.max(0, latch.peel - dt * 0.35);
    }

    // ---- cutter
    if (this.cutter.active) this.applyCutter(vine, vehicle);

    // ---- failure
    if (tension > spec.maxTension) {
      vine.state = 'breaking';
      vine.stateTime = 0;
      this.justBroke = vine;
    } else if (latch.peel > 1.6) {
      vine.state = 'breaking';
      vine.stateTime = 0;
      this.justBroke = vine;
    }
  }

  private applyCutter(vine: Vine, vehicle: Vehicle): void {
    const latch = vine.latch;
    if (!latch) return;
    const world = TMP.copy(latch.anchorLocal).applyQuaternion(vehicle.quat).add(vehicle.pos);
    const toContact = TMP2.copy(world).sub(vehicle.pos);
    const dist = toContact.length();
    if (dist > VEHICLE.cutterRange) return;

    const forward = vehicle.forward;
    const angle = Math.atan2(toContact.dot(vehicle.rightVec), toContact.dot(forward));
    const halfWedge = 0.62;
    if (Math.abs(angle - this.cutter.angle) > halfWedge) return;

    latch.cut += 1 / 60;
    if (latch.cut >= vine.spec.cutTime) {
      vine.state = 'breaking';
      vine.stateTime = 0;
      this.justBroke = vine;
    }
  }
}
