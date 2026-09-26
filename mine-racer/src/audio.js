// ---------------------------------------------------------------------------
// All audio synthesized with WebAudio — no asset files.
// ---------------------------------------------------------------------------
export class AudioSys {
  constructor() {
    this.ctx = null;
    this.muted = false;
    this._lastDing = 0;
  }

  init() {
    if (this.ctx) { this.ctx.resume(); return; }
    const ctx = (this.ctx = new (window.AudioContext || window.webkitAudioContext)());
    this.master = ctx.createGain();
    this.master.gain.value = 0.8;
    this.master.connect(ctx.destination);

    // shared noise buffer
    const len = ctx.sampleRate;
    this.noiseBuf = ctx.createBuffer(1, len, ctx.sampleRate);
    const d = this.noiseBuf.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;

    // --- engine: saw + sub through lowpass ---
    this.engOsc = ctx.createOscillator(); this.engOsc.type = 'sawtooth';
    this.engSub = ctx.createOscillator(); this.engSub.type = 'square';
    this.engFilter = ctx.createBiquadFilter(); this.engFilter.type = 'lowpass';
    this.engGain = ctx.createGain(); this.engGain.gain.value = 0;
    this.engOsc.connect(this.engFilter); this.engSub.connect(this.engFilter);
    this.engFilter.connect(this.engGain); this.engGain.connect(this.master);
    this.engOsc.start(); this.engSub.start();
    this.engFilter.frequency.value = 300;

    // --- skid/wind noise loop ---
    this.skidSrc = ctx.createBufferSource();
    this.skidSrc.buffer = this.noiseBuf; this.skidSrc.loop = true;
    this.skidFilter = ctx.createBiquadFilter(); this.skidFilter.type = 'bandpass';
    this.skidFilter.frequency.value = 900; this.skidFilter.Q.value = 0.7;
    this.skidGain = ctx.createGain(); this.skidGain.gain.value = 0;
    this.skidSrc.connect(this.skidFilter); this.skidFilter.connect(this.skidGain);
    this.skidGain.connect(this.master);
    this.skidSrc.start();
  }

  setMuted(m) { this.muted = m; if (this.master) this.master.gain.value = m ? 0 : 0.8; }

  setEngine(speedNorm, throttle, drift) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime;
    const f = 52 + speedNorm * 150 + throttle * 8;
    this.engOsc.frequency.setTargetAtTime(f, t, 0.06);
    this.engSub.frequency.setTargetAtTime(f * 0.5, t, 0.06);
    this.engFilter.frequency.setTargetAtTime(240 + speedNorm * 800, t, 0.08);
    const g = 0.028 + speedNorm * 0.05 + Math.abs(throttle) * 0.03;
    this.engGain.gain.setTargetAtTime(g, t, 0.09);
    this.skidGain.gain.setTargetAtTime(drift * 0.06 + speedNorm * 0.012, t, 0.1);
  }

  blip(freq, dur, type = 'sine', gain = 0.2, when = 0, slideTo = null) {
    if (!this.ctx) return;
    const t0 = this.ctx.currentTime + when;
    const o = this.ctx.createOscillator(), g = this.ctx.createGain();
    o.type = type; o.frequency.setValueAtTime(freq, t0);
    if (slideTo) o.frequency.exponentialRampToValueAtTime(slideTo, t0 + dur);
    g.gain.setValueAtTime(gain, t0);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    o.connect(g); g.connect(this.master);
    o.start(t0); o.stop(t0 + dur + 0.02);
  }

  noiseBurst(dur, filterFreq, gain, type = 'lowpass') {
    if (!this.ctx) return;
    const t0 = this.ctx.currentTime;
    const s = this.ctx.createBufferSource(); s.buffer = this.noiseBuf;
    s.playbackRate.value = 0.7 + Math.random() * 0.6;
    const f = this.ctx.createBiquadFilter(); f.type = type; f.frequency.value = filterFreq;
    const g = this.ctx.createGain();
    g.gain.setValueAtTime(gain, t0);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    s.connect(f); f.connect(g); g.connect(this.master);
    s.start(t0); s.stop(t0 + dur + 0.02);
  }

  clack(dist) {
    const v = Math.min(0.5, 14 / (1 + dist * dist * 0.02)) * 0.35;
    if (v < 0.01) return;
    this.noiseBurst(0.05, 1400, v, 'bandpass');
    this.noiseBurst(0.04, 900, v * 0.8, 'bandpass');
  }

  ding() {
    const now = performance.now();
    if (now - this._lastDing < 750) return;
    this._lastDing = now;
    this.blip(740, 0.35, 'sine', 0.12);
    this.blip(1110, 0.3, 'sine', 0.07, 0.02);
  }

  crash() {
    this.noiseBurst(0.4, 500, 0.5);
    this.noiseBurst(0.25, 2400, 0.25, 'highpass');
    this.blip(70, 0.5, 'sine', 0.5, 0, 38);
    this.blip(220, 0.2, 'square', 0.12, 0, 90);
  }

  scrape() { this.noiseBurst(0.12, 1800, 0.1, 'highpass'); }

  chime(i = 0) {
    const base = [660, 720, 780, 840, 920, 990][i % 6];
    this.blip(base, 0.16, 'triangle', 0.24);
    this.blip(base * 1.5, 0.3, 'triangle', 0.2, 0.09);
  }

  fanfare() {
    [523, 659, 784, 1046].forEach((f, i) => this.blip(f, 0.4, 'triangle', 0.22, i * 0.12));
    this.blip(1318, 0.7, 'triangle', 0.2, 0.5);
  }

  countBeep(final) { this.blip(final ? 880 : 440, final ? 0.5 : 0.2, 'square', 0.18); }
}
