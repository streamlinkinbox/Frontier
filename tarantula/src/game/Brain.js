import * as THREE from 'three';

// Autonomous tarantula behaviour for demo / NPC use: slow deliberate wandering with long pauses
// (tarantulas are sit-and-wait predators), occasional bursts, climbing whatever it walks into,
// and periodic defensive / predatory displays.
export class Brain {
  constructor(spider) {
    this.s = spider;
    this.state = 'pause'; this.t = 1.5;
    this.turnBias = 0; this.speed = 0.6; this.run = false;
    this.ctrl = { dir: new THREE.Vector3(), speed: 0, run: false };
    this.lastPos = new THREE.Vector3(); this.stuckT = 0;
    this.time = 0;
  }

  _next() {
    const r = Math.random();
    const s = this.s;
    if (this.state === 'wander' || this.state === 'burst') {
      if (r < 0.14) { this.state = 'threat'; this.t = 2.2 + Math.random() * 1.5; s.actions.setThreat(true); return; }
      if (r < 0.24) { this.state = 'strike'; this.t = 1.8; s.actions.trigger('strike'); return; }
      if (r < 0.32) { this.state = 'flick'; this.t = 2.8; s.actions.trigger('flick'); return; }
      this.state = 'pause'; this.t = 1 + Math.random() * 3.5; return;
    }
    s.actions.setThreat(false);
    if (r < 0.15) { this.state = 'burst'; this.t = 0.8 + Math.random() * 1.2; this.run = true; this.speed = 1; }
    else { this.state = 'wander'; this.t = 3 + Math.random() * 6; this.run = false; this.speed = 0.45 + Math.random() * 0.55; }
    this.turnBias = (Math.random() - 0.5) * 1.2;
  }

  update(dt) {
    this.time += dt;
    this.t -= dt;
    if (this.t <= 0) this._next();
    const s = this.s, c = this.ctrl;
    if (this.state === 'wander' || this.state === 'burst') {
      const wobble = Math.sin(this.time * 0.37) * 0.5 + Math.sin(this.time * 0.13 + 1.3) * 0.7 + this.turnBias;
      const right = new THREE.Vector3().crossVectors(s.fwd, s.up);
      c.dir.copy(s.fwd).addScaledVector(right, wobble * 0.6).normalize();
      c.speed = this.speed; c.run = this.run;
      // stuck detection -> pick a new heading
      if (this.lastPos.distanceTo(s.pos) < 0.5 * dt * this.speed) this.stuckT += dt; else this.stuckT = 0;
      if (this.stuckT > 1.2) { this.turnBias = (Math.random() < 0.5 ? -1 : 1) * 2.5; this.stuckT = 0; }
    } else { c.speed = 0; c.run = false; }
    this.lastPos.copy(s.pos);
    return c;
  }

  stop() { this.s.actions.setThreat(false); this.state = 'pause'; this.t = 1; }
}
