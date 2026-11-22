import { clamp, clamp01, lerp } from '../core/mathx';

/**
 * Everything you hear is synthesised at runtime — no audio assets needed.
 *
 * The mix follows the design rule in docs/03: three always-on layers (canopy
 * bed, insect life, sub) plus transient events, with a low-pass duck when a
 * vine has hold of you so the world sounds muffled while you fight it.
 */
export class AudioEngine {
  private ctx: AudioContext | null = null;
  private master!: GainNode;
  private duckFilter!: BiquadFilterNode;
  private engineGain!: GainNode;
  private engineOscs: OscillatorNode[] = [];
  private engineFilter!: BiquadFilterNode;
  private canopyGain!: GainNode;
  private tyreGain!: GainNode;
  private noiseBuffer!: AudioBuffer;
  private started = false;
  private muted = false;

  async start(): Promise<void> {
    if (this.started) return;
    const Ctor = window.AudioContext ?? (window as any).webkitAudioContext;
    if (!Ctor) return;
    this.ctx = new Ctor();
    if (this.ctx.state === 'suspended') await this.ctx.resume();
    const ctx = this.ctx;

    this.master = ctx.createGain();
    this.master.gain.value = 0.55;
    this.master.connect(ctx.destination);

    // global duck filter (vine hold)
    this.duckFilter = ctx.createBiquadFilter();
    this.duckFilter.type = 'lowpass';
    this.duckFilter.frequency.value = 20000;
    this.duckFilter.connect(this.master);

    // --- noise buffer shared by the beds
    const len = ctx.sampleRate * 2;
    this.noiseBuffer = ctx.createBuffer(1, len, ctx.sampleRate);
    const data = this.noiseBuffer.getChannelData(0);
    for (let i = 0; i < len; i++) data[i] = Math.random() * 2 - 1;

    // --- engine: three harmonics + induction noise
    this.engineGain = ctx.createGain();
    this.engineGain.gain.value = 0.0;
    this.engineFilter = ctx.createBiquadFilter();
    this.engineFilter.type = 'lowpass';
    this.engineFilter.frequency.value = 900;
    this.engineFilter.Q.value = 1.2;
    this.engineGain.connect(this.engineFilter);
    this.engineFilter.connect(this.duckFilter);

    const harmonics = [1, 2, 3.02];
    for (const h of harmonics) {
      const osc = ctx.createOscillator();
      osc.type = h === 1 ? 'sawtooth' : 'square';
      osc.frequency.value = 60;
      const g = ctx.createGain();
      g.gain.value = h === 1 ? 0.5 : 0.22 / h;
      osc.connect(g);
      g.connect(this.engineGain);
      osc.start();
      this.engineOscs.push(osc);
    }
    // induction / turbo hiss
    const hiss = ctx.createBufferSource();
    hiss.buffer = this.noiseBuffer;
    hiss.loop = true;
    const hissFilter = ctx.createBiquadFilter();
    hissFilter.type = 'bandpass';
    hissFilter.frequency.value = 2400;
    hissFilter.Q.value = 0.9;
    const hissGain = ctx.createGain();
    hissGain.gain.value = 0;
    hiss.connect(hissFilter);
    hissFilter.connect(hissGain);
    hissGain.connect(this.duckFilter);
    hiss.start();

    // --- canopy bed
    const canopy = ctx.createBufferSource();
    canopy.buffer = this.noiseBuffer;
    canopy.loop = true;
    const canopyFilter = ctx.createBiquadFilter();
    canopyFilter.type = 'bandpass';
    canopyFilter.frequency.value = 900;
    canopyFilter.Q.value = 0.6;
    this.canopyGain = ctx.createGain();
    this.canopyGain.gain.value = 0.05;
    canopy.connect(canopyFilter);
    canopyFilter.connect(this.canopyGain);
    this.canopyGain.connect(this.duckFilter);
    canopy.start();

    // --- tyre / surface noise
    const tyre = ctx.createBufferSource();
    tyre.buffer = this.noiseBuffer;
    tyre.loop = true;
    const tyreFilter = ctx.createBiquadFilter();
    tyreFilter.type = 'lowpass';
    tyreFilter.frequency.value = 500;
    this.tyreGain = ctx.createGain();
    this.tyreGain.gain.value = 0;
    tyre.connect(tyreFilter);
    tyreFilter.connect(this.tyreGain);
    this.tyreGain.connect(this.duckFilter);
    tyre.start();

    this.started = true;
  }

  setMuted(m: boolean): void {
    this.muted = m;
    if (this.master) this.master.gain.value = m ? 0 : 0.55;
  }

  get isMuted(): boolean {
    return this.muted;
  }

  update(rpm: number, load: number, speed: number, roughness: number, canopyDensity: number, duck: number): void {
    if (!this.started || !this.ctx) return;
    const t = this.ctx.currentTime;
    const base = clamp(rpm * 0.5, 30, 400);
    for (let i = 0; i < this.engineOscs.length; i++) {
      this.engineOscs[i].frequency.setTargetAtTime(base * [1, 2, 3.02][i], t, 0.04);
    }
    this.engineGain.gain.setTargetAtTime(0.05 + load * 0.16, t, 0.08);
    this.engineFilter.frequency.setTargetAtTime(500 + load * 2200 + rpm * 0.12, t, 0.08);
    this.canopyGain.gain.setTargetAtTime(0.035 + canopyDensity * 0.09, t, 0.4);
    this.tyreGain.gain.setTargetAtTime(clamp01(speed / 40) * roughness * 0.16, t, 0.1);
    this.duckFilter.frequency.setTargetAtTime(lerp(20000, 900, clamp01(duck)), t, 0.12);
  }

  // ------------------------------------------------------------------ events

  private env(
    type: OscillatorType,
    freq: number,
    freqEnd: number,
    duration: number,
    gain: number,
    filterType: BiquadFilterType = 'bandpass',
  ): void {
    if (!this.started || !this.ctx) return;
    const ctx = this.ctx;
    const t = ctx.currentTime;
    const osc = ctx.createOscillator();
    osc.type = type;
    osc.frequency.setValueAtTime(freq, t);
    osc.frequency.exponentialRampToValueAtTime(Math.max(20, freqEnd), t + duration);
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, t);
    g.gain.exponentialRampToValueAtTime(gain, t + 0.008);
    g.gain.exponentialRampToValueAtTime(0.0001, t + duration);
    const f = ctx.createBiquadFilter();
    f.type = filterType;
    f.frequency.value = Math.max(200, freq * 1.4);
    osc.connect(f);
    f.connect(g);
    g.connect(this.duckFilter);
    osc.start(t);
    osc.stop(t + duration + 0.05);
  }

  private noiseBurst(duration: number, gain: number, freq: number, q = 1.2): void {
    if (!this.started || !this.ctx) return;
    const ctx = this.ctx;
    const t = ctx.currentTime;
    const src = ctx.createBufferSource();
    src.buffer = this.noiseBuffer;
    const f = ctx.createBiquadFilter();
    f.type = 'bandpass';
    f.frequency.value = freq;
    f.Q.value = q;
    const g = ctx.createGain();
    g.gain.setValueAtTime(gain, t);
    g.gain.exponentialRampToValueAtTime(0.0001, t + duration);
    src.connect(f);
    f.connect(g);
    g.connect(this.duckFilter);
    src.start(t);
    src.stop(t + duration + 0.02);
  }

  latch(): void {
    this.env('triangle', 420, 90, 0.35, 0.5);
    this.noiseBurst(0.18, 0.25, 1800, 1.6);
  }

  creak(intensity: number): void {
    this.env('sawtooth', 90 + intensity * 40, 150 + intensity * 90, 0.5, 0.06 + intensity * 0.1, 'bandpass');
  }

  snap(): void {
    this.env('square', 900, 120, 0.12, 0.42);
    this.noiseBurst(0.3, 0.3, 900, 0.8);
  }

  woodyCrack(): void {
    this.env('sawtooth', 180, 55, 0.6, 0.45, 'lowpass');
    this.noiseBurst(0.4, 0.28, 420, 0.7);
  }

  wetRelease(): void {
    this.noiseBurst(0.22, 0.3, 2600, 0.6);
  }

  cutter(): void {
    this.env('square', 2400, 1400, 0.1, 0.16);
    this.noiseBurst(0.45, 0.18, 3200, 0.5);
  }

  impact(strength: number): void {
    this.env('sine', 140, 40, 0.35, 0.3 * strength, 'lowpass');
    this.noiseBurst(0.28, 0.22 * strength, 700, 0.8);
  }
}
