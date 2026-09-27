import * as THREE from 'three';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { ShaderPass } from 'three/examples/jsm/postprocessing/ShaderPass.js';

// Film-like finishing: subtle vignette + temporally varying fine grain (applied after tone mapping)
const FinishShader = {
  uniforms: { tDiffuse: { value: null }, uTime: { value: 0 }, uGrain: { value: 0.035 }, uVignette: { value: 0.32 }, uRes: { value: new THREE.Vector2(1, 1) } },
  vertexShader: /* glsl */`varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }`,
  fragmentShader: /* glsl */`
    uniform sampler2D tDiffuse; uniform float uTime, uGrain, uVignette; uniform vec2 uRes; varying vec2 vUv;
    float h(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * .1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
    void main(){
      vec4 c = texture2D(tDiffuse, vUv);
      vec2 q = vUv - 0.5; q.x *= uRes.x / uRes.y;
      float v = 1.0 - uVignette * smoothstep(0.35, 1.05, length(q));
      c.rgb *= v;
      float g = h(vUv * uRes + fract(uTime * 7.13) * 431.0) - 0.5;
      float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));
      c.rgb += g * uGrain * (1.0 - lum * 0.7);
      gl_FragColor = c;
    }`,
};

export class Post {
  constructor(renderer, scene, camera, { samples = 4, bloom = true } = {}) {
    this.renderer = renderer;
    const size = renderer.getDrawingBufferSize(new THREE.Vector2());
    this.rt = new THREE.WebGLRenderTarget(size.x, size.y, { type: THREE.HalfFloatType, samples });
    this.composer = new EffectComposer(renderer, this.rt);
    this.renderPass = new RenderPass(scene, camera);
    this.composer.addPass(this.renderPass);
    this.bloom = new UnrealBloomPass(new THREE.Vector2(size.x, size.y), 0.18, 0.55, 0.92);
    this.bloom.enabled = bloom;
    this.composer.addPass(this.bloom);
    this.composer.addPass(new OutputPass());
    this.finish = new ShaderPass(FinishShader);
    this.composer.addPass(this.finish);
  }
  setSize(w, h, pr) {
    this.composer.setPixelRatio(pr);
    this.composer.setSize(w, h);
    this.finish.uniforms.uRes.value.set(w * pr, h * pr);
  }
  setSamples(n) { this.rt.samples = n; }
  render(dt, t) {
    this.finish.uniforms.uTime.value = t;
    this.composer.render(dt);
  }
}
