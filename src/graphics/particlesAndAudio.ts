import * as THREE from 'three';
import { clamp, lerp } from '../math/noise';
import { ProceduralTextureLibrary } from './proceduralTextures';
import { VehicleRigidBody } from '../physics/vehiclePhysics';

interface ParticleSlot {
  active: boolean;
  age: number;
  life: number;
  x: number;
  y: number;
  z: number;
  vx: number;
  vy: number;
  vz: number;
  gravity: number;
  drag: number;
  sizeStart: number;
  sizeEnd: number;
  r: number;
  g: number;
  b: number;
  alphaPeak: number;
}

const MAX_PARTICLES = 560;

export class OffroadParticleSystem {
  public readonly points: THREE.Points;

  private slots: ParticleSlot[] = [];
  private cursor = 0;
  private posAttr: THREE.BufferAttribute;
  private colAttr: THREE.BufferAttribute;
  private sizeAttr: THREE.BufferAttribute;

  // Ambient floating canopy pollen motes around camera
  public readonly pollenPoints: THREE.Points;
  private pollenGeo: THREE.BufferGeometry;

  constructor(textures: ProceduralTextureLibrary) {
    for (let i = 0; i < MAX_PARTICLES; i++) {
      this.slots.push({
        active: false,
        age: 0,
        life: 1,
        x: 0,
        y: -1000,
        z: 0,
        vx: 0,
        vy: 0,
        vz: 0,
        gravity: 0,
        drag: 0.98,
        sizeStart: 0.5,
        sizeEnd: 1.5,
        r: 1,
        g: 1,
        b: 1,
        alphaPeak: 0.5,
      });
    }

    const pos = new Float32Array(MAX_PARTICLES * 3);
    const col = new Float32Array(MAX_PARTICLES * 4);
    const sizes = new Float32Array(MAX_PARTICLES);

    const geo = new THREE.BufferGeometry();
    this.posAttr = new THREE.BufferAttribute(pos, 3);
    this.posAttr.setUsage(THREE.DynamicDrawUsage);
    this.colAttr = new THREE.BufferAttribute(col, 4);
    this.colAttr.setUsage(THREE.DynamicDrawUsage);
    this.sizeAttr = new THREE.BufferAttribute(sizes, 1);
    this.sizeAttr.setUsage(THREE.DynamicDrawUsage);

    geo.setAttribute('position', this.posAttr);
    geo.setAttribute('color', this.colAttr);
    geo.setAttribute('aSize', this.sizeAttr);
    geo.boundingSphere = new THREE.Sphere(new THREE.Vector3(), 10000);

    const mat = new THREE.ShaderMaterial({
      uniforms: {
        uTex: { value: textures.particleSoft },
        uPixelRatio: { value: Math.min(window.devicePixelRatio || 1, 2) },
      },
      vertexShader: `
        attribute float aSize;
        attribute vec4 color;
        varying vec4 vColor;
        uniform float uPixelRatio;
        void main() {
          vColor = color;
          vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
          gl_PointSize = clamp(aSize * uPixelRatio * (290.0 / max(1.0, -mvPosition.z)), 1.0, 96.0);
          gl_Position = projectionMatrix * mvPosition;
        }
      `,
      fragmentShader: `
        uniform sampler2D uTex;
        varying vec4 vColor;
        void main() {
          vec4 texel = texture2D(uTex, gl_PointCoord);
          float alpha = texel.a * vColor.a;
          if (alpha < 0.015) discard;
          gl_FragColor = vec4(vColor.rgb * texel.rgb, alpha);
        }
      `,
      transparent: true,
      depthWrite: false,
    });

    this.points = new THREE.Points(geo, mat);
    this.points.frustumCulled = false;

    // Subtle sunlit rainforest spore / pollen motes
    const moteCount = 140;
    const motePos = new Float32Array(moteCount * 3);
    for (let i = 0; i < moteCount; i++) {
      motePos[i * 3] = (Math.random() - 0.5) * 44;
      motePos[i * 3 + 1] = Math.random() * 14;
      motePos[i * 3 + 2] = (Math.random() - 0.5) * 65;
    }
    this.pollenGeo = new THREE.BufferGeometry();
    this.pollenGeo.setAttribute('position', new THREE.BufferAttribute(motePos, 3));
    const pollenMat = new THREE.PointsMaterial({
      map: textures.particleSoft,
      color: 0xf7ebb2,
      size: 0.16,
      transparent: true,
      opacity: 0.45,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
    });
    this.pollenPoints = new THREE.Points(this.pollenGeo, pollenMat);
    this.pollenPoints.frustumCulled = false;
  }

  private emit(opts: Omit<ParticleSlot, 'active' | 'age'>): void {
    const p = this.slots[this.cursor];
    this.cursor = (this.cursor + 1) % MAX_PARTICLES;
    p.active = true;
    p.age = 0;
    p.life = opts.life;
    p.x = opts.x;
    p.y = opts.y;
    p.z = opts.z;
    p.vx = opts.vx;
    p.vy = opts.vy;
    p.vz = opts.vz;
    p.gravity = opts.gravity;
    p.drag = opts.drag;
    p.sizeStart = opts.sizeStart;
    p.sizeEnd = opts.sizeEnd;
    p.r = opts.r;
    p.g = opts.g;
    p.b = opts.b;
    p.alphaPeak = opts.alphaPeak;
  }

  public emitVineSnapBurst(worldPos: THREE.Vector3, isRockShear: boolean): void {
    const count = 34;
    for (let i = 0; i < count; i++) {
      const theta = Math.random() * Math.PI * 2;
      const elev = (Math.random() - 0.3) * Math.PI;
      const spd = 2.5 + Math.random() * 8.5;
      const isSap = i % 3 === 0;

      this.emit({
        life: 0.45 + Math.random() * 0.55,
        x: worldPos.x + (Math.random() - 0.5) * 0.4,
        y: worldPos.y + (Math.random() - 0.5) * 0.4,
        z: worldPos.z + (Math.random() - 0.5) * 0.4,
        vx: Math.cos(theta) * Math.cos(elev) * spd,
        vy: Math.sin(elev) * spd + 2.4,
        vz: Math.sin(theta) * Math.cos(elev) * spd,
        gravity: -11.5,
        drag: 0.96,
        sizeStart: isSap ? 0.24 : 0.34,
        sizeEnd: isSap ? 0.08 : 0.62,
        r: isRockShear && !isSap ? 0.95 : isSap ? 0.85 : 0.42,
        g: isRockShear && !isSap ? 0.78 : isSap ? 0.92 : 0.68,
        b: isSap ? 0.28 : 0.22,
        alphaPeak: 0.88,
      });
    }
  }

  public emitImpactDust(worldPos: THREE.Vector3, intensity: number): void {
    const count = Math.floor(8 + intensity * 16);
    for (let i = 0; i < count; i++) {
      const a = Math.random() * Math.PI * 2;
      const spd = (1.5 + Math.random() * 5.0) * intensity;
      this.emit({
        life: 0.55 + Math.random() * 0.5,
        x: worldPos.x,
        y: worldPos.y + 0.2,
        z: worldPos.z,
        vx: Math.cos(a) * spd,
        vy: 1.2 + Math.random() * 2.8,
        vz: Math.sin(a) * spd,
        gravity: -3.5,
        drag: 0.94,
        sizeStart: 0.5,
        sizeEnd: 2.1,
        r: 0.68,
        g: 0.56,
        b: 0.42,
        alphaPeak: 0.48,
      });
    }
  }

  public emitTimberSlamShockwave(worldPos: THREE.Vector3): void {
    const count = 48;
    for (let i = 0; i < count; i++) {
      const a = (i / count) * Math.PI * 2 + (Math.random() - 0.5) * 0.2;
      const spd = 4.0 + Math.random() * 9.5;
      const isLeaf = i % 4 === 0;
      this.emit({
        life: 0.75 + Math.random() * 0.7,
        x: worldPos.x + (Math.random() - 0.5) * 3.0,
        y: worldPos.y + 0.3,
        z: worldPos.z + (Math.random() - 0.5) * 3.0,
        vx: Math.cos(a) * spd,
        vy: 2.2 + Math.random() * 4.5,
        vz: Math.sin(a) * spd,
        gravity: isLeaf ? -4.5 : -7.5,
        drag: 0.93,
        sizeStart: isLeaf ? 0.35 : 0.85,
        sizeEnd: isLeaf ? 0.5 : 3.4,
        r: isLeaf ? 0.42 : 0.64,
        g: isLeaf ? 0.68 : 0.52,
        b: isLeaf ? 0.26 : 0.38,
        alphaPeak: 0.68,
      });
    }
  }

  private _backDir = new THREE.Vector3();

  public update(
    dt: number,
    vehicle: VehicleRigidBody,
    elapsedTime: number,
    focusPos?: THREE.Vector3
  ): void {
    // 1. Emit authentic off-road tire roostertails & dust plumes based on real wheel speed & slip!
    this._backDir.set(0, 0, 1).applyQuaternion(vehicle.quaternion);
    const speed = Math.abs(vehicle.forwardSpeedMps);

    for (const w of vehicle.wheels) {
      if (!w.inContact) continue;
      const wheelTangential = Math.abs(w.angularVel * w.radius);
      const activity = speed * 0.045 + w.combinedSlip * 0.95;

      // Sparkling White-Foam Water Splash Rooster-Tails when splashing through puddles!
      if (w.waterDepth > 0.025 && speed > 2.5) {
        const splashCount = speed > 14 ? 3 : 2;
        const sideSign = w.isLeft ? -1 : 1;
        for (let s = 0; s < splashCount; s++) {
          const sprayUp = clamp(speed * 0.22 + w.waterDepth * 9.0, 2.2, 6.4);
          this.emit({
            life: 0.42 + Math.random() * 0.32,
            x: w.contactPointWorld.x + (Math.random() - 0.5) * 0.35,
            y: w.contactPointWorld.y + 0.16,
            z: w.contactPointWorld.z + (Math.random() - 0.5) * 0.35,
            vx: this._backDir.x * speed * 0.45 + sideSign * (1.8 + Math.random() * 2.8),
            vy: sprayUp + Math.random() * 1.8,
            vz: this._backDir.z * speed * 0.45 + (Math.random() - 0.5) * 2.0,
            gravity: -13.5,
            drag: 0.96,
            sizeStart: 0.38,
            sizeEnd: 1.45,
            r: 0.84,
            g: 0.94,
            b: 0.96,
            alphaPeak: 0.76,
          });
        }
        continue;
      }

      if (w.surface === 'HOLLOW_LOG') continue;

      if (activity > 0.28 && Math.random() < clamp(activity * 0.75, 0, 0.92)) {
        const isMud = w.surface === 'WET_MUD' || w.mudiness > 0.35;
        const isVerge = w.surface === 'LOOSE_VERGE';

        // Roostertail spray kicked backward & upward by spinning mud-terrain lugs!
        const kickSpeed = clamp(wheelTangential * 0.36, 1.5, 9.5);
        this.emit({
          life: isMud ? 0.48 + Math.random() * 0.25 : 0.7 + Math.random() * 0.55,
          x: w.contactPointWorld.x + (Math.random() - 0.5) * 0.25,
          y: w.contactPointWorld.y + 0.14,
          z: w.contactPointWorld.z + (Math.random() - 0.5) * 0.25,
          vx: this._backDir.x * kickSpeed + (Math.random() - 0.5) * 2.2 + vehicle.linearVelocity.x * 0.35,
          vy: (isMud ? 2.4 : 1.1) + Math.random() * 1.9,
          vz: this._backDir.z * kickSpeed + (Math.random() - 0.5) * 2.2 + vehicle.linearVelocity.z * 0.35,
          gravity: isMud ? -12.5 : -1.2,
          drag: isMud ? 0.97 : 0.93,
          sizeStart: isMud ? 0.28 : 0.45,
          sizeEnd: isMud ? 0.52 : 2.35 + w.combinedSlip * 0.9,
          r: isMud ? 0.24 : isVerge ? 0.42 : 0.67,
          g: isMud ? 0.17 : isVerge ? 0.48 : 0.52,
          b: isMud ? 0.11 : isVerge ? 0.28 : 0.36,
          alphaPeak: isMud ? 0.72 : clamp(0.18 + w.combinedSlip * 0.26, 0.16, 0.46),
        });
      }
    }

    // 2. Step all active particles and pack into GPU buffers
    const posArr = this.posAttr.array as Float32Array;
    const colArr = this.colAttr.array as Float32Array;
    const sizeArr = this.sizeAttr.array as Float32Array;

    for (let i = 0; i < MAX_PARTICLES; i++) {
      const p = this.slots[i];
      const i3 = i * 3;
      const i4 = i * 4;

      if (!p.active) {
        posArr[i3 + 1] = -1000;
        colArr[i4 + 3] = 0;
        sizeArr[i] = 0;
        continue;
      }

      p.age += dt;
      if (p.age >= p.life) {
        p.active = false;
        posArr[i3 + 1] = -1000;
        colArr[i4 + 3] = 0;
        sizeArr[i] = 0;
        continue;
      }

      const dragStep = Math.pow(p.drag, dt * 60);
      p.vx *= dragStep;
      p.vy = p.vy * dragStep + p.gravity * dt;
      p.vz *= dragStep;

      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.z += p.vz * dt;

      const t = p.age / p.life;
      const alpha = Math.sin(Math.pow(t, 0.65) * Math.PI) * p.alphaPeak;

      posArr[i3] = p.x;
      posArr[i3 + 1] = p.y;
      posArr[i3 + 2] = p.z;

      colArr[i4] = p.r;
      colArr[i4 + 1] = p.g;
      colArr[i4 + 2] = p.b;
      colArr[i4 + 3] = alpha;

      sizeArr[i] = lerp(p.sizeStart, p.sizeEnd, t);
    }

    this.posAttr.needsUpdate = true;
    this.colAttr.needsUpdate = true;
    this.sizeAttr.needsUpdate = true;

    // Recenter ambient sunlit canopy pollen motes around active observer camera or vehicle
    const anchor = focusPos ?? vehicle.position;
    this.pollenPoints.position.set(
      anchor.x,
      anchor.y,
      Math.floor(anchor.z / 30) * 30
    );
    this.pollenPoints.rotation.y = Math.sin(elapsedTime * 0.12) * 0.08;
  }
}

/**
 * Procedural WebAudio Rally Engine, Gravel Tire Scrub & Living Vine Strain/Snap Synthesizer.
 */
export class RallyVineAudioEngine {
  private ctx: AudioContext | null = null;
  private masterGain: GainNode | null = null;

  // Engine synth nodes
  private oscPrimary: OscillatorNode | null = null;
  private oscHarmonic: OscillatorNode | null = null;
  private engineFilter: BiquadFilterNode | null = null;
  private engineGain: GainNode | null = null;

  // Gravel/dirt tire scrub nodes
  private scrubFilter: BiquadFilterNode | null = null;
  private scrubGain: GainNode | null = null;

  // Living vine fiber tension creak nodes
  private vineStrainOsc: OscillatorNode | null = null;
  private vineStrainGain: GainNode | null = null;

  public muted = false;
  public initialized = false;

  public unlock(): void {
    if (this.initialized) {
      if (this.ctx && this.ctx.state === 'suspended') {
        this.ctx.resume();
      }
      return;
    }

    const AudioCtx = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    if (!AudioCtx) return;

    try {
      const ctx = new AudioCtx();
      this.ctx = ctx;

      const master = ctx.createGain();
      master.gain.value = this.muted ? 0 : 0.34;
      master.connect(ctx.destination);
      this.masterGain = master;

      // 1. Rally V6 / Boxer Dual-Oscillator Waveshaped Exhaust Note
      const shaper = ctx.createWaveShaper();
      const curve = new Float32Array(256);
      for (let i = 0; i < 256; i++) {
        const x = (i / 128) - 1;
        curve[i] = Math.tanh(x * 2.4);
      }
      shaper.curve = curve;

      const engFilter = ctx.createBiquadFilter();
      engFilter.type = 'lowpass';
      engFilter.frequency.value = 260;
      engFilter.Q.value = 2.2;

      const engGain = ctx.createGain();
      engGain.gain.value = 0.18;

      const osc1 = ctx.createOscillator();
      osc1.type = 'sawtooth';
      osc1.frequency.value = 55;

      const osc2 = ctx.createOscillator();
      osc2.type = 'triangle';
      osc2.frequency.value = 82.5;

      osc1.connect(shaper);
      osc2.connect(shaper);
      shaper.connect(engFilter);
      engFilter.connect(engGain);
      engGain.connect(master);

      osc1.start();
      osc2.start();

      this.oscPrimary = osc1;
      this.oscHarmonic = osc2;
      this.engineFilter = engFilter;
      this.engineGain = engGain;

      // 2. Filtered Noise Buffer for Off-Road Gravel/Dirt Tire Scrub
      const bufSize = ctx.sampleRate * 2;
      const noiseBuf = ctx.createBuffer(1, bufSize, ctx.sampleRate);
      const data = noiseBuf.getChannelData(0);
      let b0 = 0, b1 = 0, b2 = 0;
      for (let i = 0; i < bufSize; i++) {
        const white = Math.random() * 2 - 1;
        b0 = 0.997 * b0 + white * 0.029;
        b1 = 0.985 * b1 + white * 0.032;
        b2 = 0.950 * b2 + white * 0.048;
        data[i] = (b0 + b1 + b2 + white * 0.18) * 0.5;
      }

      const noiseSrc = ctx.createBufferSource();
      noiseSrc.buffer = noiseBuf;
      noiseSrc.loop = true;

      const scrubFilter = ctx.createBiquadFilter();
      scrubFilter.type = 'bandpass';
      scrubFilter.frequency.value = 680;
      scrubFilter.Q.value = 0.9;

      const scrubGain = ctx.createGain();
      scrubGain.gain.value = 0.0;

      noiseSrc.connect(scrubFilter);
      scrubFilter.connect(scrubGain);
      scrubGain.connect(master);
      noiseSrc.start();

      this.scrubFilter = scrubFilter;
      this.scrubGain = scrubGain;

      // 3. Woody Vine Fiber Tension Groan / Creak
      const vOsc = ctx.createOscillator();
      vOsc.type = 'sawtooth';
      vOsc.frequency.value = 95;
      const vFilter = ctx.createBiquadFilter();
      vFilter.type = 'bandpass';
      vFilter.frequency.value = 340;
      vFilter.Q.value = 4.5;
      const vGain = ctx.createGain();
      vGain.gain.value = 0;

      vOsc.connect(vFilter);
      vFilter.connect(vGain);
      vGain.connect(master);
      vOsc.start();

      this.vineStrainOsc = vOsc;
      this.vineStrainGain = vGain;

      this.initialized = true;
    } catch {
      // Ignore if sandboxed audio unavailable
    }
  }

  public toggleMute(): boolean {
    this.muted = !this.muted;
    if (this.masterGain && this.ctx) {
      this.masterGain.gain.setTargetAtTime(this.muted ? 0 : 0.34, this.ctx.currentTime, 0.04);
    }
    return this.muted;
  }

  public updateContinuous(vehicle: VehicleRigidBody, maxVineStrain01: number): void {
    if (!this.initialized || !this.ctx || !this.oscPrimary || !this.oscHarmonic) return;
    const now = this.ctx.currentTime;

    // 6-cylinder firing frequency: 3 pulses per revolution = (RPM / 60) * 3
    const baseFreq = clamp((vehicle.rpm / 60) * 2.2, 36, 285);
    this.oscPrimary.frequency.setTargetAtTime(baseFreq, now, 0.03);
    this.oscHarmonic.frequency.setTargetAtTime(baseFreq * 1.498, now, 0.03);

    const loadBoost = vehicle.isBoosting ? 1.45 : 1.0;
    this.engineFilter!.frequency.setTargetAtTime(
      (190 + vehicle.throttleLoad * 620 + (vehicle.rpm / 7400) * 480) * loadBoost,
      now,
      0.04
    );
    this.engineGain!.gain.setTargetAtTime(
      0.13 + vehicle.throttleLoad * 0.14 + (vehicle.isBoosting ? 0.08 : 0),
      now,
      0.04
    );

    // Gravel/Mud Tire Scrub + Creek Water Splash Hiss!
    let slipSum = 0;
    for (const w of vehicle.wheels) {
      if (w.inContact) slipSum += w.combinedSlip;
    }
    const speedFactor = clamp(vehicle.speedKmh / 45, 0, 1);
    const inWater = vehicle.maxWheelWaterDepth > 0.025;
    const scrubLevel = clamp(
      (slipSum * 0.12 + speedFactor * (inWater ? 0.28 : 0.09)) * speedFactor,
      0,
      0.42
    );
    this.scrubGain!.gain.setTargetAtTime(scrubLevel, now, 0.05);
    this.scrubFilter!.frequency.setTargetAtTime(
      inWater ? 1150 + speedFactor * 600 : 480 + slipSum * 240,
      now,
      0.05
    );

    // Vine Tension Strain Creak
    const strainGain = maxVineStrain01 > 0.1 ? clamp(maxVineStrain01 * 0.18, 0, 0.22) : 0;
    this.vineStrainGain!.gain.setTargetAtTime(strainGain, now, 0.04);
    this.vineStrainOsc!.frequency.setTargetAtTime(75 + maxVineStrain01 * 165, now, 0.04);
  }

  public playTimberCrackSound(): void {
    if (!this.initialized || !this.ctx || !this.masterGain || this.muted) return;
    const now = this.ctx.currentTime;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'sawtooth';
    osc.frequency.setValueAtTime(195, now);
    osc.frequency.exponentialRampToValueAtTime(42, now + 0.35);
    gain.gain.setValueAtTime(0.42, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.38);
    osc.connect(gain);
    gain.connect(this.masterGain);
    osc.start(now);
    osc.stop(now + 0.4);
  }

  public playTimberSlamSound(): void {
    if (!this.initialized || !this.ctx || !this.masterGain || this.muted) return;
    const now = this.ctx.currentTime;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(110, now);
    osc.frequency.exponentialRampToValueAtTime(28, now + 0.45);
    gain.gain.setValueAtTime(0.55, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.48);
    osc.connect(gain);
    gain.connect(this.masterGain);
    osc.start(now);
    osc.stop(now + 0.5);
  }

  public playVineLatchSound(): void {
    if (!this.initialized || !this.ctx || !this.masterGain || this.muted) return;
    const now = this.ctx.currentTime;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'triangle';
    osc.frequency.setValueAtTime(290, now);
    osc.frequency.exponentialRampToValueAtTime(78, now + 0.16);
    gain.gain.setValueAtTime(0.38, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.18);
    osc.connect(gain);
    gain.connect(this.masterGain);
    osc.start(now);
    osc.stop(now + 0.19);
  }

  public playVineSnapSound(isShear: boolean): void {
    if (!this.initialized || !this.ctx || !this.masterGain || this.muted) return;
    const now = this.ctx.currentTime;

    // Crisp fibrous whip-crack impulse
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = isShear ? 'sawtooth' : 'square';
    osc.frequency.setValueAtTime(isShear ? 720 : 520, now);
    osc.frequency.exponentialRampToValueAtTime(65, now + 0.22);

    gain.gain.setValueAtTime(0.45, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.24);

    osc.connect(gain);
    gain.connect(this.masterGain);
    osc.start(now);
    osc.stop(now + 0.25);
  }

  public playPortalWarpSound(): void {
    if (!this.initialized || !this.ctx || !this.masterGain || this.muted) return;
    const now = this.ctx.currentTime;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(180, now);
    osc.frequency.exponentialRampToValueAtTime(860, now + 0.28);
    osc.frequency.exponentialRampToValueAtTime(220, now + 0.62);
    gain.gain.setValueAtTime(0.48, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.65);
    osc.connect(gain);
    gain.connect(this.masterGain);
    osc.start(now);
    osc.stop(now + 0.68);
  }

  public playFuelPickupSound(): void {
    if (!this.initialized || !this.ctx || !this.masterGain || this.muted) return;
    const now = this.ctx.currentTime;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'triangle';
    osc.frequency.setValueAtTime(420, now);
    osc.frequency.exponentialRampToValueAtTime(760, now + 0.16);
    gain.gain.setValueAtTime(0.32, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.19);
    osc.connect(gain);
    gain.connect(this.masterGain);
    osc.start(now);
    osc.stop(now + 0.21);
  }

  public playThunderStrikeSound(intensity = 1.0): void {
    if (!this.initialized || !this.ctx || !this.masterGain || this.muted) return;
    const now = this.ctx.currentTime;

    // 1. Sharp high-voltage electric arc crack
    const crackOsc = this.ctx.createOscillator();
    const crackGain = this.ctx.createGain();
    crackOsc.type = 'sawtooth';
    crackOsc.frequency.setValueAtTime(880, now);
    crackOsc.frequency.exponentialRampToValueAtTime(64, now + 0.18);
    crackGain.gain.setValueAtTime(0.28 * intensity, now);
    crackGain.gain.exponentialRampToValueAtTime(0.001, now + 0.2);
    crackOsc.connect(crackGain);
    crackGain.connect(this.masterGain);
    crackOsc.start(now);
    crackOsc.stop(now + 0.22);

    // 2. Deep rolling sub-bass thunder boom
    const boomOsc = this.ctx.createOscillator();
    const boomGain = this.ctx.createGain();
    boomOsc.type = 'triangle';
    boomOsc.frequency.setValueAtTime(92, now + 0.04);
    boomOsc.frequency.exponentialRampToValueAtTime(28, now + 1.15);
    boomGain.gain.setValueAtTime(0.001, now);
    boomGain.gain.linearRampToValueAtTime(0.46 * intensity, now + 0.08);
    boomGain.gain.exponentialRampToValueAtTime(0.001, now + 1.25);
    boomOsc.connect(boomGain);
    boomGain.connect(this.masterGain);
    boomOsc.start(now);
    boomOsc.stop(now + 1.28);
  }
}
