struct Uniforms {
  eye: vec4f,
  forward: vec4f,
  right: vec4f,
  up: vec4f,
  viewport: vec4f,
  brush: vec4f,
  water: vec4f,
  flags: vec4f,
  dims: vec4f,
  erosion: vec4f,
  geology: vec4f,
  sculpt: vec4f,
  brushParams: vec4f,
  pick: vec4f,
  planeOrigin: vec4f,
  planeNormal: vec4f,
  strokeTangent: vec4f,
  strokePrevious: vec4f,
  processes: vec4f,
  material: vec4f,
  materialShape: vec4f,
  materialOptics: vec4f,
  baseColor: vec4f,
  waterOptics: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var field: texture_3d<f32>;
@group(0) @binding(2) var linearSampler: sampler;
@group(0) @binding(3) var outputField: texture_storage_3d<rgba16float, write>;
@group(0) @binding(4) var outputFlux: texture_storage_3d<rgba16float, write>;
@group(0) @binding(5) var fluxField: texture_3d<f32>;
@group(0) @binding(6) var<storage, read_write> pickResult: array<vec4f>;
const WORLD_MIN: vec3f = vec3f(-48., -10., -48.);
const WORLD_MAX: vec3f = vec3f(48., 38., 48.);
const WORLD_SIZE: vec3f = vec3f(96., 48., 96.);
fn hash3(p: vec3i) -> f32 {
  var a: u32 = (u32(p.x)*73856093u) ^ (u32(p.y)*19349663u) ^ (u32(p.z)*83492791u);
  a = (a ^ (a >> 13u)) * 1274126177u;
  return f32(a ^ (a >> 16u)) / 4294967295.;
}
fn noise(p: vec3f) -> f32 {
  let i: vec3i = vec3i(floor(p));
  let q: vec3f = fract(p); let f: vec3f = q*q*(3.-2.*q);
  return mix(mix(mix(hash3(i),hash3(i+vec3i(1,0,0)),f.x),mix(hash3(i+vec3i(0,1,0)),hash3(i+vec3i(1,1,0)),f.x),f.y),
    mix(mix(hash3(i+vec3i(0,0,1)),hash3(i+vec3i(1,0,1)),f.x),mix(hash3(i+vec3i(0,1,1)),hash3(i+vec3i(1,1,1)),f.x),f.y),f.z)*2.-1.;
}
// Shared visual/mechanical lithology. Pale, cemented bands resist detachment.
fn strataCoordinate(p:vec3f) -> f32 {
  return p.y+p.x*.025+p.z*.017+noise(p*vec3f(.047,.018,.047))*.85;
}
fn strataStrength(p:vec3f) -> f32 {
  return .2+.75*smoothstep(-.5,.65,sin(strataCoordinate(p)*1.05+noise(p*vec3f(.16,.06,.16))*.24));
}
fn erodibility(p:vec3f) -> f32 {return mix(1.,1.-strataStrength(p)*.82,u.geology.y);}
fn solidFraction(d:f32) -> f32 {return clamp(.5-d/(2.*u.dims.w),0.,1.);}
fn smin(a: f32,b: f32,k: f32) -> f32 {let h: f32=clamp(.5+.5*(b-a)/k,0.,1.);return mix(b,a,h)-k*h*(1.-h);}
fn boxSdf(p:vec3f,b:vec3f) -> f32 {let q:vec3f=abs(p)-b;return length(max(q,vec3f(0)))+min(max(q.x,max(q.y,q.z)),0.);}
fn ellipsoid(p:vec3f,r:vec3f) -> f32 {let k0:f32=length(p/r);let k1:f32=length(p/(r*r));return select(k0*(k0-1.)/max(k1,.00001),-min(r.x,min(r.y,r.z)),k0<.00001);}
fn loadAt(p:vec3i) -> vec4f {return textureLoad(field,clamp(p,vec3i(0),vec3i(u.dims.xyz)-1),0);}
fn volume(p:vec3f) -> vec4f {return textureSampleLevel(field,linearSampler,(p-WORLD_MIN)/WORLD_SIZE,0.);}
fn map(p:vec3f) -> f32 {return max(volume(p).x,boxSdf(p-vec3f(0,14,0),vec3f(48,24,48)));}
fn safeNormalize(v:vec3f) -> vec3f {
  let squared:f32=dot(v,v);
  return select(vec3f(0,1,0),v*inverseSqrt(max(squared,.00000001)),squared>.00000001);
}
fn surfaceNormal(p:vec3f) -> vec3f {
  // Blend across voxel faces before applying the fine material bump. This
  // avoids faceted/dotted lighting along the lower-resolution fallback's seams.
  let e:f32=max(.3,u.dims.w*.6);
  return safeNormalize(vec3f(map(p+vec3f(e,0,0))-map(p-vec3f(e,0,0)),map(p+vec3f(0,e,0))-map(p-vec3f(0,e,0)),map(p+vec3f(0,0,e))-map(p-vec3f(0,0,e))));
}
fn boxHit(ro:vec3f,rd:vec3f) -> vec2f {
  let inv:vec3f=1./(rd+vec3f(.000001));
  let a:vec3f=(WORLD_MIN-ro)*inv;let b:vec3f=(WORLD_MAX-ro)*inv;
  let near:vec3f=min(a,b);let far:vec3f=max(a,b);
  return vec2f(max(0.,max(near.x,max(near.y,near.z))),min(far.x,min(far.y,far.z)));
}
fn trace(ro:vec3f,rd:vec3f,maxSteps:i32) -> f32 {
  let interval:vec2f=boxHit(ro,rd);if(interval.x>interval.y){return 10000.;}
  var t:f32=interval.x;
  var previousT:f32=t;var previousD:f32=map(ro+rd*t);
  for(var i:i32=0;i<maxSteps;i++) {
    let d:f32=map(ro+rd*t);
    let epsilon:f32=.018+min(t*.0003,.045);
    if(abs(d)<epsilon){return t;}
    // A warped/evolved level set is not an exact distance everywhere. Refine
    // the FIRST sign crossing rather than jumping through a wall to its back.
    if(d*previousD<0.){
      var lo:f32=previousT;var hi:f32=t;
      for(var j:i32=0;j<7;j++){
        let mid:f32=(lo+hi)*.5;let md:f32=map(ro+rd*mid);
        if(md*previousD>0.){lo=mid;}else{hi=mid;}
      }
      return (lo+hi)*.5;
    }
    previousT=t;previousD=d;
    t+=max(.015,abs(d)*.65);
    if(t>interval.y){break;}
  }
  return 10000.;
}
