// Procedural sky dome (Preetham-flavoured gradient + sun disc + haze) and an
// environment map baked from it for PBR reflections/ambient.
import * as THREE from '../vendor/three.module.js';

export function makeSky(sunDir) {
  const uniforms = {
    uSun: { value: sunDir.clone().normalize() },
    uTurbidity: { value: 3.2 },
    uZenith: { value: new THREE.Color(0.22, 0.42, 0.85) },
    uHorizon: { value: new THREE.Color(0.86, 0.9, 0.98) },
    uHaze: { value: new THREE.Color(0.98, 0.86, 0.7) },
    uGround: { value: new THREE.Color(0.32, 0.3, 0.28) },
  };
  const mat = new THREE.ShaderMaterial({
    uniforms, side: THREE.BackSide, depthWrite: false,
    vertexShader: `varying vec3 vDir; void main(){ vDir = normalize(position); vec4 p = projectionMatrix * modelViewMatrix * vec4(position,1.0); gl_Position = p.xyww; }`,
    fragmentShader: `
      uniform vec3 uSun, uZenith, uHorizon, uHaze, uGround; uniform float uTurbidity; varying vec3 vDir;
      void main(){
        vec3 d = normalize(vDir);
        float y = d.y;
        float t = pow(max(y, 0.0), 0.42);
        vec3 col = mix(uHorizon, uZenith, t);
        float sd = max(dot(d, uSun), 0.0);
        // forward-scattering haze around the sun
        col += uHaze * (pow(sd, 6.0) * 0.35 + pow(sd, 48.0) * 0.6) * (1.0 - t * 0.5);
        // sun disc
        col += vec3(1.0, 0.95, 0.85) * smoothstep(0.9993, 0.9997, sd) * 6.0;
        // below horizon
        col = mix(col, uGround, smoothstep(0.0, -0.08, y));
        gl_FragColor = vec4(col, 1.0);
      }`,
  });
  const mesh = new THREE.Mesh(new THREE.SphereGeometry(4000, 48, 24), mat);
  mesh.frustumCulled = false; mesh.renderOrder = -10;
  return { mesh, uniforms };
}

export function bakeEnvironment(renderer, sunDir) {
  const pmrem = new THREE.PMREMGenerator(renderer);
  const scene = new THREE.Scene();
  const { mesh } = makeSky(sunDir);
  mesh.geometry = new THREE.SphereGeometry(50, 32, 16);
  scene.add(mesh);
  // a dim ground plane so the lower hemisphere is not pure sky
  const ground = new THREE.Mesh(new THREE.CircleGeometry(60, 24), new THREE.MeshBasicMaterial({ color: 0x5a4c3c }));
  ground.rotation.x = -Math.PI / 2; ground.position.y = -3; scene.add(ground);
  const rt = pmrem.fromScene(scene, 0.04);
  pmrem.dispose();
  return rt.texture;
}
