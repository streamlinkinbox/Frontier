import { Euler, Quaternion, Vector3 } from 'three';
import { Rng } from '../core/rng';
import { clamp } from '../core/mathx';
import { Track } from './track';
import { Vehicle, VehicleInput } from './vehicle';
import { VineSystem, VineContext } from './vines';
import { Director } from './director';
import { VEHICLE, biomeName } from './tuning';

export type PropKind = 'rock' | 'tree' | 'log';

export interface Prop {
  x: number;
  z: number;
  /** Collision radius: boulder/rock radius, or the capsule radius of a log. */
  r: number;
  h: number;
  kind: PropKind;
  s: number;
  rot: number;
  scale: number;
  /** Capsule axis (unit vector in XZ) — logs are capsules, not spheres. */
  ax: number;
  az: number;
  /** Half length of the capsule (0 for spherical props). */
  halfLen: number;
  /** Static props have zero mass; loose stones and rotten logs can be shoved. */
  mass: number;
  vx: number;
  vz: number;
  /** Seconds before this prop can be hit again (stops a shoved log jittering). */
  cooldown: number;
}

export interface GameEvent {
  type: 'latch' | 'break' | 'snap' | 'cut' | 'impact' | 'recover';
  pos: Vector3;
  strength: number;
  species?: string;
}

const FIXED_DT = 1 / 240;
const MAX_SUBSTEPS = 8;

export class World {
  readonly track: Track;
  readonly vehicle: Vehicle;
  readonly vines: VineSystem;
  readonly director: Director;

  props: Prop[] = [];
  events: GameEvent[] = [];

  time = 0;
  distance = 0;
  playerS = 0;
  biome = 'CANOPY EDGE';
  /** Set when the car has been stuck upside down for too long. */
  private upsideDownTime = 0;
  /** Seconds spent wedged against an obstacle with the throttle open. */
  private stuckTime = 0;
  /** Seconds spent outside the corridor. */
  private offTrailTime = 0;
  private rng: Rng;
  private propBuckets = new Map<number, Prop[]>();
  /** Lateral offset of the player from the corridor centreline (m). */
  private playerLateral = 0;
  /** Rate of change of the player's lateral offset (m/s). */
  private playerLateralVel = 0;
  private vineCtx: VineContext = {
    playerS: 0,
    playerLateral: 0,
    playerLateralVel: 0,
    track: null as unknown as Track,
  };
  private propsGeneratedTo = 0;
  private accumulator = 0;

  constructor(seed = 20261122) {
    this.track = new Track(seed);
    this.vehicle = new Vehicle();
    this.vines = new VineSystem(seed ^ 0x2b1d);
    this.director = new Director(seed);
    this.rng = new Rng(seed ^ 0x1234567);

    this.vineCtx.track = this.track;
    this.resetVehicleAt(12);
    this.generateProps();
  }

  // ------------------------------------------------------------------- props

  private generateProps(): void {
    const track = this.track;
    while (this.propsGeneratedTo < track.totalLength - 80) {
      const s = this.propsGeneratedTo;
      const sample = track.sampleAt(s);
      const r = this.rng;

      // treeline: trees just beyond the drivable band
      if (r.float() < 0.72) {
        const side = r.float() < 0.5 ? -1 : 1;
        const lateral = side * (sample.width + r.range(0.8, 11));
        const p = track.pointAt(s, lateral);
        this.addProp({
          x: p.x,
          z: p.z,
          r: r.range(0.32, 0.55),
          h: r.range(7, 15),
          kind: 'tree',
          s,
          rot: r.range(0, Math.PI * 2),
          scale: r.range(0.85, 1.4),
          ax: 0,
          az: 0,
          halfLen: 0,
          mass: 0,
          vx: 0,
          vz: 0,
          cooldown: 0,
        });
      }

      // loose stones on the trail: small enough to shove aside, big enough to feel
      if (r.float() < 0.34) {
        const lateral = r.range(-sample.width * 0.8, sample.width * 0.8);
        const p = track.pointAt(s, lateral);
        const rad = r.range(0.42, 1.45);
        // under a metre across these are loose; above that they are boulders
        const loose = rad < 0.9;
        this.addProp({
          x: p.x,
          z: p.z,
          r: rad,
          h: r.range(0.6, 1.9),
          kind: 'rock',
          s,
          rot: r.range(0, Math.PI * 2),
          scale: r.range(0.8, 1.5),
          ax: 0,
          az: 0,
          halfLen: 0,
          mass: loose ? 38 + rad * rad * rad * 55 : 0,
          vx: 0,
          vz: 0,
          cooldown: 0,
        });
      }

      // fallen logs: rotten enough to be shoved aside, low enough to ride over
      if (r.float() < 0.12) {
        const lateral = r.range(-sample.width * 0.7, sample.width * 0.7);
        const p = track.pointAt(s, lateral);
        const ang = r.range(0, Math.PI * 2);
        const half = r.range(1.5, 2.9);
        this.addProp({
          x: p.x,
          z: p.z,
          r: 0.34,
          h: 0.55,
          kind: 'log',
          s,
          rot: ang,
          scale: 1,
          ax: Math.sin(ang),
          az: Math.cos(ang),
          halfLen: half,
          mass: 34 + half * 22,
          vx: 0,
          vz: 0,
          cooldown: 0,
        });
      }

      this.propsGeneratedTo += r.range(7, 16);
    }

    // features authored by the track generator (each is dressed exactly once)
    for (const f of track.features) {
      if (f.dressed) continue;
      if (f.s0 > this.propsGeneratedTo + 60) break;
      f.dressed = true;
      if (f.type === 'boulders') {
        const count = this.rng.int(3, 6);
        for (let i = 0; i < count; i++) {
          const s = f.s0 + this.rng.range(-4, 4);
          const lateral = f.lateral + this.rng.range(-2.5, 2.5);
          const p = this.track.pointAt(s, lateral);
          this.addProp({
            x: p.x,
            z: p.z,
            r: this.rng.range(0.55, 1.35),
            h: this.rng.range(0.5, 1.7),
            kind: 'rock',
            s,
            rot: this.rng.range(0, Math.PI * 2),
            scale: this.rng.range(0.8, 1.4),
            ax: 0,
            az: 0,
            halfLen: 0,
            mass: 0, // a boulder field is a wall of stone: you pick a line through it
            vx: 0,
            vz: 0,
            cooldown: 0,
          });
        }
      } else if (f.type === 'choke') {
        // two boulders with a 3.2 m gap between them: a precision gate, not a wall
        for (const side of [-1, 1]) {
          const p = this.track.pointAt(f.s0, side * 2.4);
          this.addProp({
            x: p.x,
            z: p.z,
            r: 0.8,
            h: 1.8,
            kind: 'rock',
            s: f.s0,
            rot: this.rng.range(0, Math.PI * 2),
            scale: 1.3,
            ax: 0,
            az: 0,
            halfLen: 0,
            mass: 0,
            vx: 0,
            vz: 0,
            cooldown: 0,
          });
        }
      } else if (f.type === 'log') {
        const p = this.track.pointAt(f.s0, f.lateral);
        const ang = 0.2;
        this.addProp({
          x: p.x,
          z: p.z,
          r: 0.36,
          h: 0.6,
          kind: 'log',
          s: f.s0,
          rot: ang,
          scale: 1.1,
          ax: Math.sin(ang),
          az: Math.cos(ang),
          halfLen: 2.6,
          mass: 86,
          vx: 0,
          vz: 0,
          cooldown: 0,
        });
      }
    }
  }

  /**
   * Loose stones and shoved logs roll to a stop. They are pure set dressing
   * with a little momentum, so they never become the thing that stops a run.
   */
  private stepLooseProps(dt: number): void {
    const bucket = Math.floor(this.playerS / 20);
    for (let b = bucket - 1; b <= bucket + 1; b++) {
      const list = this.propBuckets.get(b);
      if (!list) continue;
      for (const prop of list) {
        if (prop.cooldown > 0) prop.cooldown = Math.max(0, prop.cooldown - dt);
        if (prop.mass <= 0) continue;
        const sp = Math.hypot(prop.vx, prop.vz);
        if (sp < 0.02) {
          prop.vx = 0;
          prop.vz = 0;
          continue;
        }
        prop.x += prop.vx * dt;
        prop.z += prop.vz * dt;
        const dec = Math.min(sp, 7 * dt);
        prop.vx -= (prop.vx / sp) * dec;
        prop.vz -= (prop.vz / sp) * dec;
      }
    }
  }

  private addProp(prop: Prop): void {
    this.props.push(prop);
    const bucket = Math.floor(prop.s / 20);
    const list = this.propBuckets.get(bucket);
    if (list) list.push(prop);
    else this.propBuckets.set(bucket, [prop]);
  }

  // ------------------------------------------------------------------- update

  update(dt: number, input: VehicleInput): void {
    this.accumulator += Math.min(dt, 0.25);
    let steps = 0;
    while (this.accumulator >= FIXED_DT && steps < MAX_SUBSTEPS) {
      this.fixedStep(FIXED_DT, input);
      this.accumulator -= FIXED_DT;
      steps++;
    }
    if (steps === MAX_SUBSTEPS) this.accumulator = 0;
  }

  private fixedStep(dt: number, input: VehicleInput): void {
    this.time += dt;
    this.track.ensureLength(this.playerS + 1200);
    this.generateProps();
    this.stepLooseProps(dt);

    this.vehicle.step(dt, input, this.track);

    // surface the vine system's per-step signals as gameplay events
    const vines = this.vines;
    if (vines.justLatched) {
      this.events.push({
        type: 'latch',
        pos: vines.justLatched.contactWorld.clone(),
        strength: vines.justLatched.species === 'anchor' ? 1 : 0.4,
        species: vines.justLatched.species,
      });
    }
    if (vines.justSnapped) {
      this.events.push({
        type: 'snap',
        pos: vines.justSnapped.contactWorld.clone(),
        strength: 1,
        species: vines.justSnapped.species,
      });
    }
    if (vines.justBroke) {
      this.events.push({
        type: 'break',
        pos: vines.justBroke.contactWorld.clone(),
        strength: 0.9,
        species: vines.justBroke.species,
      });
    }

    const q = this.track.nearest(this.vehicle.pos.x, this.vehicle.pos.z);
    this.playerS = q.s;
    this.distance = this.playerS;
    this.biome = biomeName(this.distance);

    // where the player is on the corridor and how fast that is changing:
    // the vine system uses this to work out where to hang its traps
    const side = this.track.sampleAt(q.s).side;
    this.playerLateral = q.lateral;
    this.playerLateralVel = this.vehicle.vel.x * side.x + this.vehicle.vel.z * side.z;

    this.vineCtx.playerS = this.playerS;
    this.vineCtx.playerLateral = this.playerLateral;
    this.vineCtx.playerLateralVel = this.playerLateralVel;
    this.vines.update(dt, this.vehicle, this.vineCtx);

    this.director.update(this.playerS, this.track, this.vines, this.vehicle.speed);
    this.resolvePropCollisions(dt);

    // recovery: if the car ends up on its roof, put it back after a moment
    if (this.vehicle.upVec.y < 0.1) {
      this.upsideDownTime += dt;
      if (this.upsideDownTime > 2.5) this.recoverVehicle('rolled');
    } else {
      this.upsideDownTime = 0;
    }

    // safety net: nothing may end up below the world
    if (this.vehicle.pos.y < this.track.heightAt(this.vehicle.pos.x, this.vehicle.pos.z) - 2.5) {
      this.recoverVehicle('stuck');
    }

    // The run lives inside the corridor: if the car ends up in the undergrowth
    // it is pulled back onto the trail, the way a course marshal would.
    const lateral = q.lateral;
    if (Math.abs(lateral) > q.width + 1.5) {
      this.offTrailTime += dt;
      if (this.offTrailTime > 1.0) this.recoverVehicle('stuck');
    } else {
      this.offTrailTime = 0;
    }

    // and if the chassis has buried itself in the ground, it is dug out
    const groundHere = this.track.heightFromQuery(this.vehicle.pos.x, this.vehicle.pos.z, q);
    if (this.vehicle.pos.y < groundHere - 0.35) this.recoverVehicle('stuck');

    // recovery: wedged against stone with the throttle open. The run is not
    // lost, the car is simply picked up and put back on the trail.
    if (!this.vehicle.airborne && this.vehicle.speed < 1.3 && input.throttle > 0.4) {
      this.stuckTime += dt;
      if (this.stuckTime > 4.5) this.recoverVehicle('stuck');
    } else {
      this.stuckTime = 0;
    }
  }

  /**
   * Closest point on a prop to a wheel contact, in XZ.
   *
   * Rocks are spheres; logs are capsules (a fallen trunk is a 5 m long tube, not
   * a 4 m boulder). Getting this right is what keeps the trail passable: a log
   * lying across the trail is something you shove aside or ride over, whereas a
   * boulder field is a wall you have to pick a line through.
   */
  private closestPointOnProp(prop: Prop, x: number, z: number, out: { x: number; z: number }): void {
    if (prop.halfLen > 0) {
      const dx = x - prop.x;
      const dz = z - prop.z;
      let t = dx * prop.ax + dz * prop.az;
      if (t > prop.halfLen) t = prop.halfLen;
      else if (t < -prop.halfLen) t = -prop.halfLen;
      out.x = prop.x + prop.ax * t;
      out.z = prop.z + prop.az * t;
    } else {
      out.x = prop.x;
      out.z = prop.z;
    }
  }

  private resolvePropCollisions(dt: number): void {
    const bucket = Math.floor(this.playerS / 20);
    const candidates: Prop[] = [];
    for (let b = bucket - 1; b <= bucket + 1; b++) {
      const list = this.propBuckets.get(b);
      if (list) candidates.push(...list);
    }
    if (candidates.length === 0) return;

    const closest = { x: 0, z: 0 };
    let impactSpeed = 0;
    let impactPos: Vector3 | null = null;

    for (const w of this.vehicle.wheels) {
      for (const prop of candidates) {
        if (prop.mass > 0 && prop.cooldown > 0) continue;
        this.closestPointOnProp(prop, w.contact.x, w.contact.z, closest);
        const dx = w.contact.x - closest.x;
        const dz = w.contact.z - closest.z;
        const d = Math.hypot(dx, dz);
        const minD = prop.r + 0.22;
        if (d > minD || d < 1e-4) continue;

        const nx = dx / d;
        const nz = dz / d;
        const pen = minD - d;
        // push the car clear of the obstacle
        this.vehicle.pos.x += nx * pen;
        this.vehicle.pos.z += nz * pen;

        const vel = this.vehicle.vel;
        const vn = vel.x * nx + vel.z * nz;
        if (vn >= 0) continue;

        impactSpeed = Math.max(impactSpeed, -vn);
        if (!impactPos) impactPos = w.contact.clone();

        if (prop.mass > 0) {
          // Loose stone or a rotten log: momentum is shared, the obstacle is
          // shoved aside and the car keeps most of its speed.
          const restitution = 0.18;
          const invMass = 1 / VEHICLE.mass + 1 / prop.mass;
          const j = (-(1 + restitution) * vn) / invMass;
          vel.x += (j * nx) / VEHICLE.mass;
          vel.z += (j * nz) / VEHICLE.mass;
          prop.vx -= (j * nx) / prop.mass;
          prop.vz -= (j * nz) / prop.mass;
          prop.cooldown = 0.25;
        } else {
          // Solid stone: the inward velocity goes, the slide along it stays.
          vel.x -= nx * vn;
          vel.z -= nz * vn;
          // scrubbing friction, bounded so a scrape costs speed but never pins
          const tx = -nz;
          const tz = nx;
          const vt = vel.x * tx + vel.z * tz;
          const scrub = Math.min(Math.abs(vt), 5.5 * dt);
          vel.x -= tx * Math.sign(vt) * scrub;
          vel.z -= tz * Math.sign(vt) * scrub;
        }
      }
    }

    if (impactSpeed > 3) {
      this.events.push({
        type: 'impact',
        pos: impactPos ? impactPos.clone() : this.vehicle.pos.clone(),
        strength: clamp(impactSpeed / 18, 0.2, 1),
      });
      // A scrape against a tree or boulder shears anything latched on.
      if (impactSpeed > 7) {
        for (const vine of this.vines.vines) {
          if (vine.latch && vine.state !== 'breaking') {
            vine.state = 'breaking';
            vine.stateTime = 0;
            this.events.push({
              type: 'break',
              pos: vine.contactWorld.clone(),
              strength: 0.8,
              species: vine.species,
            });
          }
        }
      }
    }
  }

  /**
   * The clearest lateral offset on the trail at a given distance: the line a
   * driver would pick to get past whatever is in the way.
   */
  bestLateralAt(s: number, width: number): number {
    const limit = Math.max(1.5, width * 0.5 - 1.8);
    const blocked: { lat: number; r: number }[] = [];
    for (const p of this.props) {
      if (Math.abs(p.s - s) > 14) continue;
      const sample = this.track.sampleAt(p.s);
      const lat = (p.x - sample.pos.x) * sample.side.x + (p.z - sample.pos.z) * sample.side.z;
      blocked.push({ lat, r: p.r + p.halfLen + 1.2 });
    }
    let best = 0;
    let bestScore = -Infinity;
    for (let lat = -limit; lat <= limit; lat += 0.5) {
      let score = -Math.abs(lat) * 0.05;
      for (const b of blocked) {
        const d = Math.abs(lat - b.lat) - b.r;
        if (d < 0) score += d * 2;
      }
      if (score > bestScore) {
        bestScore = score;
        best = lat;
      }
    }
    return best;
  }

  /**
   * Puts the car back on the corridor. Anything that was holding it is let
   * go: the run continues, and the player keeps their momentum.
   */
  recoverVehicle(reason: 'manual' | 'rolled' | 'stuck' = 'manual'): void {
    const q = this.track.nearest(this.vehicle.pos.x, this.vehicle.pos.z);
    const lateral =
      reason === 'stuck'
        ? this.bestLateralAt(q.s, q.width)
        : clamp(q.lateral, -q.width * 0.6, q.width * 0.6);
    this.resetVehicleAt(q.s, lateral);
    this.upsideDownTime = 0;
    this.stuckTime = 0;
    this.offTrailTime = 0;

    // release whatever was holding the car
    for (const vine of this.vines.vines) {
      if (!vine.latch) continue;
      vine.latch = null;
      vine.state = 'broken';
      vine.dead = true;
      vine.tension = 0;
      this.events.push({
        type: 'break',
        pos: vine.contactWorld.clone(),
        strength: 0.6,
        species: vine.species,
      });
    }

    if (reason !== 'manual') {
      this.events.push({
        type: 'recover',
        pos: this.vehicle.pos.clone(),
        strength: 1,
      });
    }
  }

  /**
   * Places the car on the corridor, settled onto the terrain: the ride height,
   * pitch and roll are solved from the ground under each corner so the run
   * never starts with a wheel in the air.
   */
  resetVehicleAt(s: number, lateral = 0): void {
    const p = this.track.pointAt(s, lateral);
    const sample = this.track.sampleAt(s);
    const heading = Math.atan2(sample.fwd.x, sample.fwd.z);

    const fwdV = new Vector3(sample.fwd.x, 0, sample.fwd.z).normalize();
    const rightV = new Vector3(fwdV.z, 0, -fwdV.x);
    const halfF = VEHICLE.trackF * 0.5;
    const halfR = VEHICLE.trackR * 0.5;
    const wbF = VEHICLE.wheelbase * 0.5;
    const wbR = VEHICLE.wheelbase * 0.5;

    const groundAt = (x: number, z: number) => {
      const wx = p.x + rightV.x * x + fwdV.x * z;
      const wz = p.z + rightV.z * x + fwdV.z * z;
      return this.track.heightAt(wx, wz);
    };

    const hFL = groundAt(-halfF, wbF);
    const hFR = groundAt(halfF, wbF);
    const hRL = groundAt(-halfR, -wbR);
    const hRR = groundAt(halfR, -wbR);
    const frontAvg = (hFL + hFR) * 0.5;
    const rearAvg = (hRL + hRR) * 0.5;
    const leftAvg = (hFL + hRL) * 0.5;
    const rightAvg = (hFR + hRR) * 0.5;
    const avg = (frontAvg + rearAvg) * 0.5;

    const pitch = -Math.atan2(frontAvg - rearAvg, VEHICLE.wheelbase);
    const roll = Math.atan2(rightAvg - leftAvg, VEHICLE.trackF);
    const quat = new Quaternion().setFromEuler(new Euler(pitch, heading, roll, 'YXZ'));

    this.vehicle.reset(new Vector3(p.x, avg + VEHICLE.cogHeight, p.z), heading);
    this.vehicle.quat.copy(quat);
    this.vehicle.vel.set(0, 0, 0);
    this.vehicle.angVel.set(0, 0, 0);
  }

  drainEvents(): GameEvent[] {
    const out = this.events;
    this.events = [];
    return out;
  }
}
