// Large-area ocean: Gerstner waves on a camera-following near tile + far
// horizon tile (snapped to the vertex grid, so the sea is effectively
// endless — practical limit is the far tile, default ±9 km, set by Far size).
// The 2.5D heightfield texture is blended in only inside its world rect.
import * as THREE from 'three';

export const WAVES = [ // dirX, dirZ, amplitude, wavelength, speed
  [1, .3, .34, 31, 1.1], [.7, -1, .22, 17, 1.35], [-.4, 1, .12, 9, 1.8], [1, 1, .06, 4.7, 2.4]];
export function gerstnerHeight(x, z, t, ampScale = 1) {
  let y = 0; for (const [dx, dz, a, len, sp] of WAVES) {
    const k = Math.PI * 2 / len, l = Math.hypot(dx, dz);
    y += a * ampScale * Math.sin((dx / l * x + dz / l * z) * k + t * sp * .9); // matches vertex shader exactly
  } return y;
}
export function makeOceanMaterial(hf) {
  const uniforms = {
    uTime: { value: 0 }, uAmp: { value: 1 }, uChop: { value: .55 }, uFoam: { value: 1 },
    uDeep: { value: new THREE.Color(0x06394d) }, uShallow: { value: new THREE.Color(0x2e8aa8) },
    uSun: { value: new THREE.Vector3(.5, .8, .3).normalize() },
    uHfTex: { value: hf.tex }, uHfCenter: { value: hf.center }, uHfSize: { value: hf.worldSize },
    uCam: { value: new THREE.Vector3() },
  };
  const waves = WAVES.map(([dx, dz, a, len, sp]) => {
    const l = Math.hypot(dx, dz), k = (Math.PI * 2 / len).toFixed(4);
    return `{ vec2 d=vec2(${(dx / l).toFixed(3)},${(dz / l).toFixed(3)}); float k=${k};
      float f=k*dot(d,p)+t*${(sp * .9).toFixed(2)}; float a=${a.toFixed(3)}*uAmp;
      y+=a*sin(f); xz+=d*(uChop*a*cos(f)/k*.12); float c=a*k*cos(f); grad+=d*c; }`;
  }).join('\n');
  const mat = new THREE.ShaderMaterial({
    uniforms, transparent: false,
    vertexShader: `
      uniform float uTime,uAmp,uChop,uHfSize; uniform sampler2D uHfTex; uniform vec2 uHfCenter;
      varying vec3 vW; varying vec3 vN; varying float vFoam; varying float vCrest;
      void main(){ vec3 pos=position; vec4 wp=modelMatrix*vec4(pos,1.); vec2 p=wp.xz; float t=uTime;
        float y=0.; vec2 xz=vec2(0.); vec2 grad=vec2(0.); ${waves}
        wp.xz+=xz; wp.y+=y; vCrest=y;
        vec2 huv=(p-uHfCenter)/uHfSize+.5;
        if(all(greaterThan(huv,vec2(0.)))&&all(lessThan(huv,vec2(1.)))){
          vec4 hf=texture2D(uHfTex,huv); wp.y+=(hf.r*255.-128.)/42.; vFoam=hf.g; }
        else vFoam=0.;
        vN=normalize(vec3(-grad.x,1.,-grad.y)); vW=wp.xyz;
        gl_Position=projectionMatrix*viewMatrix*wp; }`,
    fragmentShader: `
      uniform vec3 uDeep,uShallow,uSun,uCam; uniform float uFoam,uTime;
      varying vec3 vW; varying vec3 vN; varying float vFoam; varying float vCrest;
      void main(){ vec3 V=normalize(uCam-vW); vec3 N=normalize(vN+vec3(sin(vW.x*2.1+uTime)*.03,0.,cos(vW.z*1.7-uTime)*.03));
        float fres=pow(1.-max(dot(N,V),0.),3.);
        vec3 col=mix(uDeep,uShallow,clamp(vCrest*.9+.35,0.,1.));
        col=mix(col,vec3(.75,.88,.95),fres*.65);
        vec3 H=normalize(V+uSun); col+=vec3(1.)*pow(max(dot(N,H),0.),180.)*1.2; // sun glints
        float foam=clamp(vFoam*1.4+max(0.,vCrest-.62)*1.6*uFoam,0.,1.);
        foam*= .75+.25*sin(vW.x*7.+vW.z*6.+uTime*2.);
        col=mix(col,vec3(.96,.99,1.),clamp(foam,0.,1.)*.9);
        float dist=length(uCam-vW); col=mix(col,vec3(.62,.76,.84),smoothstep(300.,4500.,dist)); // horizon haze
        gl_FragColor=vec4(col,1.); }`,
  });
  return { mat, uniforms };
}
export function makeOcean(scene, hf) {
  const { mat, uniforms } = makeOceanMaterial(hf);
  const near = new THREE.Mesh(new THREE.PlaneGeometry(260, 260, 200, 200).rotateX(-Math.PI / 2), mat);
  const far = new THREE.Mesh(new THREE.PlaneGeometry(18000, 18000, 48, 48).rotateX(-Math.PI / 2), mat);
  far.position.y = -.35; near.renderOrder = 1; scene.add(near, far);
  return { near, far, uniforms,
    follow(cam) { // snap to near-tile vertex spacing => no swimming, endless sea
      const s = 260 / 200; near.position.x = Math.round(cam.x / s) * s; near.position.z = Math.round(cam.z / s) * s;
      far.position.x = cam.x; far.position.z = cam.z; } };
}
