import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DISTRICTS, sampleDistrict, locate, motionOffset } from './route.js';

const routes = DISTRICTS.map(sampleDistrict);

test('District I is a 30+ second drive; all districts are substantial', () => {
  assert.ok(routes[0].length > 470, `district I length ${routes[0].length.toFixed(0)} m`);
  assert.ok(routes[0].length / 16 > 29, 'at 16 m/s moving average');
  for (const r of routes) assert.ok(r.length > 240, r.name);
});

test('the path is continuous: walking the centreline never loses the ground', () => {
  for (const r of routes) {
    let hint = 0, prev = null;
    for (let s = 0; s < r.length; s += 1.5) {
      // position on the centreline at route-distance s (interpolate samples)
      const i = r.samples.findIndex((q, j) => j > 0 && q.s >= s);
      const a = r.samples[i - 1], b = r.samples[i], f = (s - a.s) / (b.s - a.s || 1);
      const x = a.p[0] + (b.p[0] - a.p[0]) * f, z = a.p[2] + (b.p[2] - a.p[2]) * f;
      const hit = locate(r.samples, x, z, hint, 0);
      hint = hit.index;
      assert.ok(Math.abs(hit.lateral) < 0.6, `${r.name} @${s.toFixed(0)}m lateral ${hit.lateral.toFixed(2)}`);
      assert.ok(Number.isFinite(hit.height));
      if (prev) assert.ok(Math.abs(hit.height - prev) < 0.45, `${r.name} @${s.toFixed(0)}m step ${(hit.height - prev).toFixed(2)} m`);
      prev = hit.height;
      assert.ok(Math.abs(hit.s - s) < 3.5, 'route distance progresses monotonically');
    }
  }
});

test('drift motion is gentle and shared (neighbouring pieces move together)', () => {
  for (let s = 0; s < 500; s += 10) {
    assert.ok(Math.abs(motionOffset(s, 5)) < 0.28);
    assert.ok(Math.abs(motionOffset(s + 2.5, 5) - motionOffset(s, 5)) < 0.02, 'no tearing between neighbours');
  }
});

test('every district start is on the path (spawn sanity)', () => {
  for (const r of routes) {
    const hit = locate(r.samples, r.samples[0].p[0], r.samples[0].p[2], 0, 0);
    assert.ok(Math.abs(hit.lateral) < 0.1 && Math.abs(hit.s) < 0.1);
  }
});

test('a simulated drive (pure-pursuit at 16 m/s) reaches every portal on the deck', () => {
  for (const r of routes) {
    let hint = 2; const a0 = r.samples[2];
    let x = a0.p[0], z = a0.p[2], yaw = a0.yaw, s = 0, maxLat = 0, time = 0;
    const dt = 1/30, speed = 16;
    while (s < r.length - 6 && time < 120) {
      const target = r.samples[Math.min(r.samples.length - 1, hint + 10)];
      let want = Math.atan2(target.p[2] - z, target.p[0] - x);
      let turn = want - yaw; while (turn > Math.PI) turn -= 2*Math.PI; while (turn < -Math.PI) turn += 2*Math.PI;
      yaw += Math.max(-1.3*dt, Math.min(1.3*dt, turn));
      x += Math.cos(yaw) * speed * dt; z += Math.sin(yaw) * speed * dt;
      let hit = locate(r.samples, x, z, hint, time);
      if (Math.abs(hit.lateral) > r.width/2 - 0.85) {   // the stone kerb, same clamp as the game
        const cl = Math.sign(hit.lateral) * (r.width/2 - 0.85);
        x = hit.px + hit.nx * cl; z = hit.pz + hit.nz * cl;
        hit = locate(r.samples, x, z, hint, time);
      }
      hint = hit.index; s = hit.s;
      maxLat = Math.max(maxLat, Math.abs(hit.lateral)); time += dt;
    }
    assert.ok(s >= r.length - 6, `${r.name}: only reached ${s.toFixed(0)}/${r.length.toFixed(0)} m`);
    assert.ok(maxLat < r.width/2, `${r.name}: left deck, lateral ${maxLat.toFixed(1)} m`);
  }
});
