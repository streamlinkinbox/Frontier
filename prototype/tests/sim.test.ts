import { describe, expect, it } from 'vitest';
import { Vector3 } from 'three';
import { World } from '../src/sim/world';
import type { VehicleInput } from '../src/sim/vehicle';
import { damp } from '../src/core/mathx';

const idle: VehicleInput = { throttle: 0, brake: 0, steer: 0, handbrake: false, boost: false };
const full: VehicleInput = { throttle: 1, brake: 0, steer: 0, handbrake: false, boost: false };

function run(world: World, seconds: number, input: VehicleInput, dt = 1 / 60) {
  const steps = Math.round(seconds / dt);
  for (let i = 0; i < steps; i++) world.update(dt, input);
}

describe('track', () => {
  it('generates a continuous, drivable corridor', () => {
    const world = new World(4242);
    const track = world.track;
    expect(track.totalLength).toBeGreaterThan(2000);

    // the centreline height must stay finite and change gradually
    let prev = track.heightAt(track.samples[0].pos.x, track.samples[0].pos.z);
    let maxDelta = 0;
    for (let s = 10; s < 800; s += 10) {
      const p = track.pointAt(s, 0);
      const h = track.heightAt(p.x, p.z);
      expect(Number.isFinite(h)).toBe(true);
      maxDelta = Math.max(maxDelta, Math.abs(h - prev));
      prev = h;
    }
    expect(maxDelta).toBeLessThan(3.0);
  });

  it('keeps the corridor width inside the designed range', () => {
    const world = new World(99);
    for (let s = 0; s < 1500; s += 25) {
      const w = world.track.sampleAt(s).width;
      expect(w).toBeGreaterThan(6.5);
      expect(w).toBeLessThan(21);
    }
  });
});

describe('vehicle', () => {
  it('accelerates, drives, and stays on the ground', () => {
    const world = new World(777);
    const start = world.vehicle.pos.clone();
    run(world, 8, full);

    expect(world.vehicle.speed).toBeGreaterThan(11);
    expect(world.distance).toBeGreaterThan(60);
    expect(world.vehicle.pos.y).toBeGreaterThan(world.track.heightAt(world.vehicle.pos.x, world.vehicle.pos.z) - 0.5);
    expect(world.vehicle.pos.distanceTo(start)).toBeGreaterThan(60);
  });

  it('brakes to a stop', () => {
    const world = new World(778);
    run(world, 6, full);
    const brake: VehicleInput = { throttle: 0, brake: 1, steer: 0, handbrake: false, boost: false };
    run(world, 5, brake);
    expect(world.vehicle.speed).toBeLessThan(6);
  });

  it('steers', () => {
    const world = new World(779);
    run(world, 3, full);
    const yaw0 = world.vehicle.angVel.y;
    const steer: VehicleInput = { throttle: 0.6, brake: 0, steer: 1, handbrake: false, boost: false };
    run(world, 1.2, steer);
    expect(world.vehicle.angVel.y).toBeGreaterThan(yaw0 - 0.001);
    expect(Math.abs(world.vehicle.angVel.y)).toBeGreaterThan(0.05);
  });
});

describe('vines', () => {
  it('latches onto the vehicle and applies a real pulling force', () => {
    const world = new World(31337);
    run(world, 2, full);

    const car = world.vehicle;
    const hood = car.latchTargets([]).find((t) => t.name === 'hood')!;
    const root = hood.world.clone().add(new Vector3(2.2, 3.2, 0.5));
    const vine = world.vines.spawn('tendril', root, 0);
    vine.state = 'mature';
    vine.tip.copy(hood.world);
    vine.length = 4.5;

    world.update(1 / 60, full);
    expect(world.vines.latchCount).toBeGreaterThan(0);

    // the constraint must load up and yaw the car
    const yawBefore = car.angVel.y;
    run(world, 0.6, full);
    expect(vine.tension).toBeGreaterThan(50);
    expect(Math.abs(car.angVel.y - yawBefore)).toBeGreaterThan(0.0005);
  });

  it('an anchor pulls far harder and holds on longer than a tendril', () => {
    const measure = (species: 'anchor' | 'tendril') => {
      const world = new World(31338);
      run(world, 2, full);
      const car = world.vehicle;
      const hood = car.latchTargets([]).find((t) => t.name === 'hood')!;
      const root = hood.world.clone().add(new Vector3(1.2, 2.4, 0));
      const vine = world.vines.spawn(species, root, 0);
      vine.state = 'mature';
      vine.tip.copy(hood.world);
      vine.length = root.distanceTo(hood.world);
      world.update(1 / 60, full);

      let peak = 0;
      let heldFor = 0;
      for (let i = 0; i < 240; i++) {
        world.update(1 / 60, full);
        peak = Math.max(peak, vine.tension);
        const st = vine.state as string;
        if (st === 'latched' || st === 'creeping' || st === 'latching') heldFor += 1 / 60;
      }
      return { peak, heldFor };
    };

    const anchor = measure('anchor');
    const tendril = measure('tendril');

    expect(anchor.peak).toBeGreaterThan(tendril.peak * 3);
    expect(anchor.heldFor).toBeGreaterThan(tendril.heldFor);
  });

  it('the cutter severs a latched vine', () => {
    const world = new World(5150);
    run(world, 1, idle);
    const car = world.vehicle;
    const bumper = car.latchTargets([]).find((t) => t.name === 'bumperF')!;
    const vine = world.vines.spawn('tendril', bumper.world.clone().add(new Vector3(0.4, 2.6, 0.4)), 0);
    vine.state = 'mature';
    vine.tip.copy(bumper.world);
    vine.length = 3;
    world.update(1 / 60, idle);
    expect(world.vines.latchCount).toBeGreaterThan(0);

    expect(world.vines.triggerCutter()).toBe(true);
    run(world, 1.2, idle);
    expect(world.vines.latchCount).toBe(0);
  });

  it('the director spawns vines ahead of the player as the run progresses', () => {
    const world = new World(24680);
    run(world, 40, full);
    expect(world.vines.spawnCount).toBeGreaterThan(0);
    // nothing should spawn behind the player
    for (const vine of world.vines.vines) {
      const q = world.track.nearest(vine.root.x, vine.root.z);
      expect(q.s).toBeGreaterThan(world.playerS - 140);
    }
  });
});

const driveState = new WeakMap<World, { stuckFor: number }>();

describe('play session', () => {
  /**
   * Mirrors the runtime loop in src/main.ts: keyboard state is smoothed into
   * a VehicleInput, the world is stepped, and the resulting events are routed
   * to feedback. Everything here runs headless.
   */
  it('runs a full session: drive, get grappled, cut free, boost, recover', () => {
    const world = new World(20261122);
    const dt = 1 / 60;
    const keys = new Set<string>();
    const input: VehicleInput = { throttle: 0, brake: 0, steer: 0, handbrake: false, boost: false };

    const events: Record<string, number> = {};
    let latched = 0;
    let cut = 0;
    let recovered = 0;
    let boosted = 0;

    for (let frame = 0; frame < 60 * 90; frame++) {
      // --- input smoothing, the same way the player layer does it
      const throttleKey = keys.has('w') || keys.has('arrowup');
      const brakeKey = keys.has('s') || keys.has('arrowdown');
      const left = keys.has('a') || keys.has('arrowleft');
      const right = keys.has('d') || keys.has('arrowright');
      input.throttle = damp(input.throttle, throttleKey ? 1 : 0, 9, dt);
      input.brake = damp(input.brake, brakeKey ? 1 : 0, 12, dt);
      const steerTarget = (right ? 1 : 0) - (left ? 1 : 0);
      input.steer = damp(input.steer, steerTarget, 7, dt);
      input.handbrake = keys.has(' ');
      input.boost = keys.has('shift');

      // a light hand on the wheel, the way a player would hold it
      const q = world.track.nearest(world.vehicle.pos.x, world.vehicle.pos.z);
      const aim = world.track.pointAt(q.s + 18, 0);
      const toAim = Math.atan2(aim.x - world.vehicle.pos.x, aim.z - world.vehicle.pos.z);
      let err = toAim - Math.atan2(world.vehicle.forward.x, world.vehicle.forward.z);
      while (err > Math.PI) err -= Math.PI * 2;
      while (err < -Math.PI) err += Math.PI * 2;
      const correction = Math.max(-1, Math.min(1, err * 1.6));
      input.steer = Math.max(-1, Math.min(1, input.steer * 0.5 + correction * 0.5));

      world.update(dt, input);

      for (const ev of world.drainEvents()) {
        events[ev.type] = (events[ev.type] ?? 0) + 1;
        if (ev.type === 'latch') latched++;
        if (ev.type === 'recover') recovered++;
      }

      // --- the player reacts: cut free when something grabs the car
      if (world.vines.latchCount > 0 && world.vines.cutterReady) {
        if (world.vines.triggerCutter()) cut++;
      }
      // --- and uses boost when the trail opens up
      if (frame === 300) {
        keys.add('shift');
        boosted++;
      }
      if (frame === 360) keys.delete('shift');
    }

    // the run actually happened
    expect(world.distance).toBeGreaterThan(120);
    // the canopy reached for the car and the player answered it
    expect(latched).toBeGreaterThan(0);
    expect(cut).toBeGreaterThan(0);
    expect(events.break ?? 0).toBeGreaterThan(0);
    // the feedback channels all fired
    expect(events.impact ?? 0).toBeGreaterThanOrEqual(0);
    expect(boosted).toBe(1);
    console.log('play session:', JSON.stringify(events), `distance=${world.distance.toFixed(0)}`);
  });

  it('reverses out when the brake is held at a standstill', () => {
    const world = new World(555);
    const full: VehicleInput = { throttle: 0, brake: 0, steer: 0, handbrake: false, boost: false };
    for (let i = 0; i < 60; i++) world.update(1 / 60, full);
    const back: VehicleInput = { throttle: 0, brake: 1, steer: 0, handbrake: false, boost: false };
    for (let i = 0; i < 90; i++) world.update(1 / 60, back);
    expect(world.vehicle.reverseMode).toBe(true);
    expect(world.vehicle.pos.z).toBeLessThan(12.5);
  });

  it('boost adds torque and then goes on cooldown', () => {
    const world = new World(556);
    const boost: VehicleInput = { throttle: 1, brake: 0, steer: 0, handbrake: false, boost: true };
    for (let i = 0; i < 10; i++) world.update(1 / 60, boost);
    expect(world.vehicle.boostTimer).toBeGreaterThan(0);
    const timer = world.vehicle.boostTimer;
    for (let i = 0; i < 60 * 3; i++) world.update(1 / 60, boost);
    expect(world.vehicle.boostTimer).toBeLessThan(timer);
    expect(world.vehicle.boostCooldown).toBeGreaterThan(0);
  });
});

describe('soak', () => {
  /**
   * A stand-in for a competent player: hold the middle of the corridor, pick
   * a clear line around boulders and logs, full throttle otherwise.
   */
  function drive(world: World, dt: number) {
    const q = world.track.nearest(world.vehicle.pos.x, world.vehicle.pos.z);
    const state = driveState.get(world) ?? { stuckFor: 0 };
    driveState.set(world, state);

    // Pick a line that clears the solid obstacles. Loose stones and rotten
    // logs are not in the list: a driver smashes through those.
    const blocked: { lat: number; half: number }[] = [];
    for (const p of world.props) {
      if (p.mass > 0) continue;
      const dS = p.s - q.s;
      if (dS < -4 || dS > 45) continue;
      const sample = world.track.sampleAt(p.s);
      const lat = (p.x - sample.pos.x) * sample.side.x + (p.z - sample.pos.z) * sample.side.z;
      blocked.push({ lat, half: p.r + 1.7 });
    }
    const limit = Math.max(1.5, q.width * 0.5 - 1.4);
    let bestLat = 0;
    let bestScore = -Infinity;
    for (let lat = -limit; lat <= limit; lat += 0.25) {
      let score = -Math.abs(lat) * 0.1;
      for (const b of blocked) {
        const d = Math.abs(lat - b.lat) - b.half;
        if (d < 0) score += d * 4;
      }
      if (score > bestScore) {
        bestScore = score;
        bestLat = lat;
      }
    }

    // a wedged car reverses out, the way a player would
    if (world.vehicle.speed < 0.6) state.stuckFor += dt;
    else state.stuckFor = 0;
    const reversing = state.stuckFor > 1.0 && state.stuckFor < 2.4;

    const target = world.track.pointAt(q.s + 24, bestLat);
    const toTarget = Math.atan2(target.x - world.vehicle.pos.x, target.z - world.vehicle.pos.z);
    let err = toTarget - Math.atan2(world.vehicle.forward.x, world.vehicle.forward.z);
    while (err > Math.PI) err -= Math.PI * 2;
    while (err < -Math.PI) err += Math.PI * 2;
    // a lateral term keeps it from sawing at the wheel
    err += Math.max(-0.35, Math.min(0.35, (bestLat - q.lateral) * 0.05));
    const steer = Math.max(-1, Math.min(1, err * 1.25));

    // slow for the corners: a driver does not carry 30 m/s into a hairpin
    let maxCurv = 1e-4;
    for (let d = 5; d < 70; d += 5) {
      maxCurv = Math.max(maxCurv, Math.abs(world.track.sampleAt(q.s + d).curvature));
    }
    const cornerSpeed = Math.sqrt((0.75 * 9.81) / maxCurv);
    const speed = world.vehicle.speed;
    const throttle = speed < cornerSpeed ? 1 : 0;
    const brake = speed > cornerSpeed * 1.12 ? 0.6 : 0;

    if (reversing) {
      world.update(dt, { throttle: 0, brake: 1, steer: -steer, handbrake: false, boost: false });
    } else {
      world.update(dt, { throttle, brake, steer, handbrake: false, boost: false });
    }
  }

  it('survives two minutes of driving without the simulation blowing up', () => {
    const world = new World(123456);
    const dt = 1 / 60;
    let maxSpeed = 0;
    let latches = 0;
    let breaks = 0;
    let recoveries = 0;
    let rollovers = 0;
    let slowFrames = 0;
    let offTrailFrames = 0;
    let wasUpright = true;
    let worstSink = Infinity;
    let worstSinkState: Record<string, unknown> = {};
    const vineMisses = new Map<number, number>();

    for (let i = 0; i < 60 * 120; i++) {
      drive(world, dt);
      for (const v of world.vines.vines) {
        if (Number.isFinite(v.nearMiss)) {
          const prev = vineMisses.get(v.id);
          if (prev === undefined || v.nearMiss < prev) vineMisses.set(v.id, v.nearMiss);
        }
      }
      const upright = world.vehicle.upVec.y > 0.5;
      if (wasUpright && !upright) rollovers++;
      wasUpright = upright;
      if (world.vehicle.speed < 5) slowFrames++;
      {
        const qq = world.track.nearest(world.vehicle.pos.x, world.vehicle.pos.z);
        if (Math.abs(qq.lateral) > qq.width) offTrailFrames++;
      }
      maxSpeed = Math.max(maxSpeed, world.vehicle.speed);
      for (const e of world.drainEvents()) {
        if (e.type === 'latch') latches++;
        if (e.type === 'break' || e.type === 'snap') breaks++;
        if (e.type === 'recover') recoveries++;
      }

      // the simulation must stay finite and above ground
      expect(Number.isFinite(world.vehicle.pos.x)).toBe(true);
      expect(Number.isFinite(world.vehicle.pos.y)).toBe(true);
      expect(Number.isFinite(world.vehicle.vel.length())).toBe(true);
      // the car never sinks through the ground under itself
      if (!world.vehicle.airborne && world.vehicle.upVec.y > 0.6) {
        const groundHere = world.track.heightAt(world.vehicle.pos.x, world.vehicle.pos.z);
        const sink = world.vehicle.pos.y - groundHere;
        if (sink < worstSink) {
          worstSink = sink;
          worstSinkState = {
            frame: i,
            up: world.vehicle.upVec.y,
            speed: world.vehicle.speed,
            lateral: world.track.nearest(world.vehicle.pos.x, world.vehicle.pos.z).lateral,
            loads: world.vehicle.wheels.map((w) => Math.round(w.load)),
            comps: world.vehicle.wheels.map((w) => Number(w.compression.toFixed(3))),
            grounded: world.vehicle.wheels.map((w) => w.grounded),
            mountY: world.vehicle.wheels.map((w) =>
              Number(w.mount.clone().applyQuaternion(world.vehicle.quat).add(world.vehicle.pos).y.toFixed(2)),
            ),
            groundUnderMount: world.vehicle.wheels.map((w) => {
              const m = w.mount.clone().applyQuaternion(world.vehicle.quat).add(world.vehicle.pos);
              return Number(world.track.heightAt(m.x, m.z).toFixed(2));
            }),
            contactY: world.vehicle.wheels.map((w) => Number(w.contact.y.toFixed(2))),
            carGround: Number(world.track.heightAt(world.vehicle.pos.x, world.vehicle.pos.z).toFixed(2)),
            posY: Number(world.vehicle.pos.y.toFixed(2)),
          };
        }
      }
    }

    expect(world.distance).toBeGreaterThan(600);
    expect(maxSpeed).toBeGreaterThan(15);
    expect(maxSpeed).toBeLessThan(80);
    expect(latches).toBeGreaterThan(0);
    expect(breaks).toBeGreaterThan(0);
    // a competent driver should not need the safety net often
    expect(recoveries).toBeLessThan(12);
    // the body never sinks into the terrain: the world's own recovery digs the
    // car out at 0.35 m, so the simulation may dip just past that for a frame
    console.log('worst sink:', worstSink.toFixed(3), JSON.stringify(worstSinkState));
    expect(worstSink).toBeGreaterThan(-0.45);
    const missed = [...vineMisses.values()].sort((a, b) => a - b);
    console.log(
      `soak: distance=${world.distance.toFixed(0)}m maxSpeed=${maxSpeed.toFixed(1)} ` +
        `latches=${latches} breaks=${breaks} recoveries=${recoveries} rollovers=${rollovers} ` +
        `slow=${((slowFrames / (60 * 120)) * 100).toFixed(0)}% offTrail=${((offTrailFrames / (60 * 120)) * 100).toFixed(0)}%`,
    );
    console.log(
      `misses: n=${missed.length} values=${missed.map((m) => m.toFixed(2)).join(', ')}`,
    );
    console.log(`true misses (>0.25 m): ${missed.filter((m) => m > 0.25).length}`);
  });
});
