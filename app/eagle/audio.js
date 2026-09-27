// Tiny procedural sound design (WebAudio, no samples): engine, wind, wing
// beats, eagle screech, impacts.
export class Audio {
  constructor() { this.ctx = null; this.enabled = false; }
  init() {
    if (this.ctx) return;
    const C = window.AudioContext || window.webkitAudioContext; if (!C) return;
    const ctx = this.ctx = new C();
    this.master = ctx.createGain(); this.master.gain.value = 0.6; this.master.connect(ctx.destination);
    // noise buffer
    const len = ctx.sampleRate * 2, buf = ctx.createBuffer(1, len, ctx.sampleRate), d = buf.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
    this.noise = buf;
    // engine: two detuned saws + sub sine through a lowpass
    const eg = this.engineGain = ctx.createGain(); eg.gain.value = 0;
    const lp = this.engineLP = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 400; lp.Q.value = 2;
    this.osc = [];
    for (const [type, det] of [['sawtooth', 0], ['sawtooth', 7], ['square', -12]]) {
      const o = ctx.createOscillator(); o.type = type; o.frequency.value = 60; o.detune.value = det;
      const g = ctx.createGain(); g.gain.value = type === 'square' ? 0.25 : 0.5; o.connect(g); g.connect(lp); o.start(); this.osc.push(o);
    }
    lp.connect(eg); eg.connect(this.master);
    // wind: noise through bandpass
    const wn = ctx.createBufferSource(); wn.buffer = buf; wn.loop = true;
    const wf = this.windBP = ctx.createBiquadFilter(); wf.type = 'bandpass'; wf.frequency.value = 500; wf.Q.value = 0.6;
    const wg = this.windGain = ctx.createGain(); wg.gain.value = 0;
    wn.connect(wf); wf.connect(wg); wg.connect(this.master); wn.start();
    this.enabled = true;
  }
  resume() { if (this.ctx && this.ctx.state === 'suspended') this.ctx.resume(); }
  engine(rpm01, throttle, dt) {
    if (!this.enabled) return;
    const f = 40 + rpm01 * 190;
    for (const o of this.osc) o.frequency.setTargetAtTime(f, this.ctx.currentTime, 0.05);
    this.engineLP.frequency.setTargetAtTime(300 + rpm01 * 1800 + throttle * 600, this.ctx.currentTime, 0.08);
    this.engineGain.gain.setTargetAtTime(0.08 + rpm01 * 0.12 + throttle * 0.08, this.ctx.currentTime, 0.1);
  }
  wind(speed01) {
    if (!this.enabled) return;
    this.windGain.gain.setTargetAtTime(speed01 * speed01 * 0.5, this.ctx.currentTime, 0.2);
    this.windBP.frequency.setTargetAtTime(300 + speed01 * 1500, this.ctx.currentTime, 0.3);
  }
  burst({ dur = 0.3, freq = 300, q = 1, gain = 0.5, type = 'bandpass', attack = 0.01, sweep = null }) {
    if (!this.enabled) return;
    const ctx = this.ctx, t = ctx.currentTime;
    const s = ctx.createBufferSource(); s.buffer = this.noise; s.loop = true;
    const f = ctx.createBiquadFilter(); f.type = type; f.frequency.setValueAtTime(freq, t); f.Q.value = q;
    if (sweep) f.frequency.exponentialRampToValueAtTime(sweep, t + dur);
    const g = ctx.createGain(); g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(gain, t + attack); g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    s.connect(f); f.connect(g); g.connect(this.master); s.start(t); s.stop(t + dur + 0.05);
  }
  wingbeat(distance01, load) {
    const g = (1 - distance01) * (0.35 + load * 0.25);
    if (g < 0.02) return;
    this.burst({ dur: 0.45 + load * 0.2, freq: 220 - load * 60, q: 0.8, gain: g, attack: 0.12, sweep: 90 });
  }
  screech(distance01) {
    if (!this.enabled) return;
    const ctx = this.ctx, t = ctx.currentTime, g0 = (1 - distance01) * 0.5 + 0.1;
    // harsh descending cry: two saws with vibrato through a formant bandpass
    const out = ctx.createGain(); out.gain.setValueAtTime(0.0001, t); out.gain.exponentialRampToValueAtTime(g0, t + 0.06); out.gain.setValueAtTime(g0, t + 0.5); out.gain.exponentialRampToValueAtTime(0.0001, t + 0.95);
    const bp = ctx.createBiquadFilter(); bp.type = 'bandpass'; bp.frequency.setValueAtTime(2600, t); bp.frequency.exponentialRampToValueAtTime(1500, t + 0.9); bp.Q.value = 3;
    const vib = ctx.createOscillator(); vib.frequency.value = 28; const vg = ctx.createGain(); vg.gain.value = 60; vib.connect(vg);
    for (const det of [0, 9]) {
      const o = ctx.createOscillator(); o.type = 'sawtooth'; o.detune.value = det;
      o.frequency.setValueAtTime(1900, t); o.frequency.exponentialRampToValueAtTime(2400, t + 0.12); o.frequency.exponentialRampToValueAtTime(1100, t + 0.9);
      vg.connect(o.frequency); o.connect(bp); o.start(t); o.stop(t + 1);
    }
    vib.start(t); vib.stop(t + 1);
    bp.connect(out); out.connect(this.master);
  }
  impact(strength01, distance01) {
    const g = (1 - distance01) * strength01;
    if (g < 0.02) return;
    this.burst({ dur: 0.5, freq: 1200, q: 0.7, gain: g * 0.9, type: 'lowpass', attack: 0.005 });
    this.burst({ dur: 1.2, freq: 90, q: 1.5, gain: g * 0.8, type: 'lowpass', attack: 0.02 });
    // metal ring
    if (!this.enabled) return;
    const ctx = this.ctx, t = ctx.currentTime;
    for (const f of [640, 1130, 1790]) {
      const o = ctx.createOscillator(); o.type = 'triangle'; o.frequency.value = f * (0.95 + Math.random() * 0.1);
      const gg = ctx.createGain(); gg.gain.setValueAtTime(g * 0.12, t); gg.gain.exponentialRampToValueAtTime(0.0001, t + 0.9 + Math.random() * 0.5);
      o.connect(gg); gg.connect(this.master); o.start(t); o.stop(t + 1.6);
    }
  }
}
