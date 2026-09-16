import * as THREE from 'three';
import { FOAM_VS, FOAM_FS } from './glsl.js';

export const FOAM_SIZE = 256;
export const FOAM_SPAN = 1300; // world meters covered (matches the ±650 near grid)

// ---------------------------------------------------------------------------
// FoamSim: a real foam simulation on a ping-pong buffer, not sprite decor.
// Each frame the swell/sea spectrum is re-evaluated per texel to inject foam
// from the same fold + breaker physics the ocean surface uses; the buffer
// then advects with the surface flow, diffuses slightly and decays — so foam
// streaks persist, drift and fade instead of popping in place.
// R = persistent coverage (slow decay), G = fresh churn (fast decay).
// ---------------------------------------------------------------------------
export class FoamSim {
  constructor(renderer, shared) {
    this.renderer = renderer;
    const linearOK = !!renderer.extensions.get('OES_texture_half_float_linear');
    const filt = linearOK ? THREE.LinearFilter : THREE.NearestFilter;
    const opts = {
      type: THREE.HalfFloatType,
      format: THREE.RGBAFormat,
      minFilter: filt,
      magFilter: filt,
      depthBuffer: false,
      stencilBuffer: false,
      wrapS: THREE.ClampToEdgeWrapping,
      wrapT: THREE.ClampToEdgeWrapping,
    };
    this.rtA = new THREE.WebGLRenderTarget(FOAM_SIZE, FOAM_SIZE, opts);
    this.rtB = new THREE.WebGLRenderTarget(FOAM_SIZE, FOAM_SIZE, opts);
    this.read = this.rtA;
    this.write = this.rtB;

    this.uniforms = {
      uPrev: { value: this.read.texture },
      uDt: { value: 0.016 },
      uFlow: { value: new THREE.Vector2(0.6, 0) },
      // shared by reference with the ocean: identical physics inputs
      uTime: shared.uTime,
      uSpecA: shared.uSpecA,
      uSpecB: shared.uSpecB,
      uCascadeAmp: shared.uCascadeAmp,
      uSurfOn: shared.uSurfOn,
      uShoalGain: shared.uShoalGain,
      uBreakAmp: shared.uBreakAmp,
      uPeelSpeed: shared.uPeelSpeed,
      uPeelWidth: shared.uPeelWidth,
      uPeelOffset: shared.uPeelOffset,
      uFoldGain: shared.uFoldGain,
      uShoreX: shared.uShoreX,
      uShoreAngle: shared.uShoreAngle,
      uBeachSlope: shared.uBeachSlope,
      uReefX: shared.uReefX,
      uReefAngle: shared.uReefAngle,
      uReefDepth: shared.uReefDepth,
      uReefWidth: shared.uReefWidth,
      uBarEnable: shared.uBarEnable,
    };
    this.material = new THREE.ShaderMaterial({
      uniforms: this.uniforms,
      vertexShader: FOAM_VS,
      fragmentShader: FOAM_FS,
      depthTest: false,
      depthWrite: false,
    });
    this.scene = new THREE.Scene();
    this.scene.add(new THREE.Mesh(new THREE.PlaneGeometry(2, 2), this.material));
    this.camera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);

    const prevTarget = renderer.getRenderTarget();
    const prevClear = renderer.getClearColor(new THREE.Color());
    const prevAlpha = renderer.getClearAlpha();
    renderer.setClearColor(0x000000, 1);
    renderer.setRenderTarget(this.rtA);
    renderer.clear();
    renderer.setRenderTarget(this.rtB);
    renderer.clear();
    renderer.setRenderTarget(prevTarget);
    renderer.setClearColor(prevClear, prevAlpha);
  }

  update(dt, flow) {
    if (dt <= 0) return; // paused: foam stays perfectly frozen with the sea
    this.uniforms.uDt.value = Math.min(dt, 0.05);
    this.uniforms.uFlow.value.copy(flow);
    this.uniforms.uPrev.value = this.read.texture;
    const prevTarget = this.renderer.getRenderTarget();
    this.renderer.setRenderTarget(this.write);
    this.renderer.render(this.scene, this.camera);
    this.renderer.setRenderTarget(prevTarget);
    const t = this.read;
    this.read = this.write;
    this.write = t;
  }
}
