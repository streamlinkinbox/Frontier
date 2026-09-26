// Tiny synthesized sound engine (WebAudio, no assets)
export class Audio {
  constructor() { this.ctx = null; this.muted = false; this.lastMG = 0; }
  init() {
    if (this.ctx) { if (this.ctx.state === 'suspended') this.ctx.resume(); return; }
    const AC = window.AudioContext || window.webkitAudioContext; if (!AC) return;
    const c = this.ctx = new AC();
    this.master = c.createGain(); this.master.gain.value = 0.55; this.master.connect(c.destination);
    // noise buffer
    const len = c.sampleRate * 2, buf = c.createBuffer(1, len, c.sampleRate), d = buf.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
    this.noise = buf;
    // engine
    this.eng = c.createOscillator(); this.eng.type = 'sawtooth'; this.eng.frequency.value = 40;
    this.eng2 = c.createOscillator(); this.eng2.type = 'square'; this.eng2.frequency.value = 20;
    this.engF = c.createBiquadFilter(); this.engF.type = 'lowpass'; this.engF.frequency.value = 500;
    this.engG = c.createGain(); this.engG.gain.value = 0;
    const g2 = c.createGain(); g2.gain.value = 0.35;
    this.eng.connect(this.engF); this.eng2.connect(g2); g2.connect(this.engF); this.engF.connect(this.engG); this.engG.connect(this.master);
    this.eng.start(); this.eng2.start();
    // surf ambience
    const surf = c.createBufferSource(); surf.buffer = buf; surf.loop = true;
    const sf = c.createBiquadFilter(); sf.type = 'lowpass'; sf.frequency.value = 600;
    this.surfG = c.createGain(); this.surfG.gain.value = 0.05;
    surf.connect(sf); sf.connect(this.surfG); this.surfG.connect(this.master); surf.start();
    // plane drone
    this.drone = c.createOscillator(); this.drone.type = 'sawtooth'; this.drone.frequency.value = 85;
    const df = c.createBiquadFilter(); df.type = 'lowpass'; df.frequency.value = 380;
    this.droneG = c.createGain(); this.droneG.gain.value = 0;
    this.drone.connect(df); df.connect(this.droneG); this.droneG.connect(this.master); this.drone.start();
  }
  setMuted(m) { this.muted = m; if (this.master) this.master.gain.value = m ? 0 : 0.55; }
  engine(speed, throttle, on) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime, rpm = 30 + Math.abs(speed) * 2.6 + throttle * 12;
    this.eng.frequency.setTargetAtTime(rpm, t, 0.08); this.eng2.frequency.setTargetAtTime(rpm / 2, t, 0.08);
    this.engF.frequency.setTargetAtTime(300 + throttle * 700 + Math.abs(speed) * 12, t, 0.1);
    this.engG.gain.setTargetAtTime(on ? 0.085 + throttle * 0.05 : 0, t, 0.1);
  }
  planes(nearest) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime, g = nearest < 600 ? Math.min(0.16, 30 / (nearest + 40)) : 0;
    this.droneG.gain.setTargetAtTime(g, t, 0.2);
    this.drone.frequency.setTargetAtTime(80 + Math.sin(t * 3) * 3, t, 0.1);
  }
  surf(v) { if (this.ctx) this.surfG.gain.setTargetAtTime(v, this.ctx.currentTime, 0.5); }
  _noise(dur, freq, q, gain, type = 'bandpass', decay = dur) {
    const c = this.ctx, t = c.currentTime, s = c.createBufferSource(); s.buffer = this.noise;
    s.playbackRate.value = 0.8 + Math.random() * 0.4;
    const f = c.createBiquadFilter(); f.type = type; f.frequency.value = freq; f.Q.value = q;
    const g = c.createGain(); g.gain.setValueAtTime(gain, t); g.gain.exponentialRampToValueAtTime(0.0001, t + decay);
    s.connect(f); f.connect(g); g.connect(this.master); s.start(t, Math.random()); s.stop(t + dur + 0.05);
    return { f, t };
  }
  mg(dist) {
    if (!this.ctx || dist > 320) return;
    const now = this.ctx.currentTime; if (now - this.lastMG < 0.03) return; this.lastMG = now;
    const v = Math.min(0.5, 14 / (dist + 10));
    this._noise(0.09, 900 + Math.random() * 400, 1.2, v, 'bandpass', 0.08);
    this._noise(0.05, 180, 0.8, v * 0.8, 'lowpass', 0.05);
  }
  crack() { if (this.ctx) this._noise(0.04, 3500, 2, 0.12, 'highpass', 0.03); }
  ping() {
    if (!this.ctx) return;
    const c = this.ctx, t = c.currentTime, o = c.createOscillator(), g = c.createGain();
    o.type = 'square'; o.frequency.setValueAtTime(1900 + Math.random() * 600, t); o.frequency.exponentialRampToValueAtTime(700, t + 0.06);
    g.gain.setValueAtTime(0.09, t); g.gain.exponentialRampToValueAtTime(0.0001, t + 0.07);
    o.connect(g); g.connect(this.master); o.start(t); o.stop(t + 0.08);
    this._noise(0.05, 4000, 3, 0.1, 'bandpass', 0.05);
  }
  boom(dist, big = 1) {
    if (!this.ctx) return;
    const v = Math.min(1, 60 / (dist + 30)) * big;
    const c = this.ctx, t = c.currentTime + Math.min(0.6, dist / 340);
    const s = c.createBufferSource(); s.buffer = this.noise;
    const f = c.createBiquadFilter(); f.type = 'lowpass'; f.frequency.setValueAtTime(1600, t); f.frequency.exponentialRampToValueAtTime(90, t + 1.4);
    const g = c.createGain(); g.gain.setValueAtTime(0.0001, c.currentTime); g.gain.setValueAtTime(v * 0.9, t); g.gain.exponentialRampToValueAtTime(0.0001, t + 1.8);
    s.connect(f); f.connect(g); g.connect(this.master); s.start(t); s.stop(t + 2);
    const o = c.createOscillator(), og = c.createGain(); o.type = 'sine';
    o.frequency.setValueAtTime(70, t); o.frequency.exponentialRampToValueAtTime(28, t + 0.6);
    og.gain.setValueAtTime(0.0001, c.currentTime); og.gain.setValueAtTime(v * 0.8, t); og.gain.exponentialRampToValueAtTime(0.0001, t + 0.8);
    o.connect(og); og.connect(this.master); o.start(t); o.stop(t + 0.9);
  }
  whistle(dur, dist) {
    if (!this.ctx || dist > 250) return;
    const c = this.ctx, t = c.currentTime, o = c.createOscillator(), g = c.createGain();
    o.type = 'sine'; o.frequency.setValueAtTime(1500, t); o.frequency.exponentialRampToValueAtTime(420, t + dur);
    const v = Math.min(0.07, 6 / (dist + 30));
    g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(v, t + dur * 0.6); g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    o.connect(g); g.connect(this.master); o.start(t); o.stop(t + dur + 0.05);
  }
  wire() { if (this.ctx) this._noise(0.12, 2600, 4, 0.05, 'bandpass', 0.1); }
  pickup() {
    if (!this.ctx) return;
    const c = this.ctx, t = c.currentTime;
    [520, 780, 1040].forEach((f, i) => { const o = c.createOscillator(), g = c.createGain(); o.type = 'triangle'; o.frequency.value = f; g.gain.setValueAtTime(0.0001, t + i * 0.07); g.gain.exponentialRampToValueAtTime(0.12, t + i * 0.07 + 0.01); g.gain.exponentialRampToValueAtTime(0.0001, t + i * 0.07 + 0.25); o.connect(g); g.connect(this.master); o.start(t + i * 0.07); o.stop(t + i * 0.07 + 0.3); });
  }
}
