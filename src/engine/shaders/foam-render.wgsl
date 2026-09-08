// Evolving GPU state supplies coverage; the small atlas only supplies appearance.
var<private> foamHitData:vec4f=vec4f(10000.,10000.,0.,0.);
var<private> foamTransData:vec4f=vec4f(0.);
var<private> foamWaterTransmission:vec3f=vec3f(0.);
var<private> foamWaterWeight:f32=0.;
fn foamSurface(p:vec3f,n:vec3f) -> vec4f {
  let uv:vec2f=(p.xz+vec2f(48.))/96.;
  let footprint:f32=materialFootprint(p,n);
  let densityLod:f32=max(0.,log2(max(1.,footprint*f32(textureDimensions(foamDensity,0).x)/96.)));
  let state:vec4f=textureSampleLevel(foamDensity,linearSampler,uv,densityLod);
  let q:f32=max(0.,state.x);if(q<.0001){return vec4f(0.);}
  let flow:vec2f=textureSampleLevel(foamMotion,linearSampler,uv,0.).xy;
  let lod:f32=clamp(log2(max(1.,footprint*128./u.foam.z)),0.,7.);
  let phase:f32=fract(u.foamEffects.w/8.);let other:f32=fract(phase+.5);
  let a:vec2f=fract((p.xz-flow*phase*8.)/u.foam.z);
  let b:vec2f=fract((p.xz-flow*other*8.)/u.foam.z+vec2f(.37,.61));
  let detail:vec4f=mix(textureSampleLevel(foamAtlas,linearSampler,a,lod),textureSampleLevel(foamAtlas,linearSampler,b,lod),abs(phase*2.-1.));
  let age:f32=state.y/max(q,.001);
  let coverage:f32=(1.-exp(-q*1.8))*mix(.6,1.,detail.r);
  let light:vec3f=textureSampleLevel(foamLighting,linearSampler,uv,0.).rgb;
  let bubbly:vec3f=normalize(n+vec3f(detail.g*2.-1.,0.,detail.b*2.-1.)*.32);
  let sun:vec3f=normalize(vec3f(-.67,sin(u.up.w),.46));
  let color:vec3f=mix(vec3f(.79,.81,.78),vec3f(.63,.68,.65),clamp(age/u.foam.w,0.,1.));
  return vec4f(color*light*(.8+.25*max(dot(bubbly,sun),0.)),clamp(coverage,0.,.98));
}
fn foamInspect(p:vec3f) -> vec3f {
  let uv:vec2f=(p.xz+vec2f(48.))/96.;
  let state:vec4f=textureSampleLevel(foamDensity,linearSampler,uv,0.);
  let flow:vec4f=textureSampleLevel(foamMotion,linearSampler,uv,0.);
  let geom:vec4f=textureSampleLevel(foamGeometry,linearSampler,uv,0.);
  if(u.foam.y<1.5){return vec3f(1.-exp(-max(state.x,0.)*1.8));}
  if(u.foam.y<2.5){return mix(vec3f(.06,.15,.25),vec3f(1.,.65,.18),clamp(state.y/max(state.x,.001)/u.foam.w,0.,1.))*smoothstep(0.,.03,state.x);}
  if(u.foam.y<3.5){return vec3f(clamp(flow.xy/5.+vec2f(.5),vec2f(0.),vec2f(1.)),.3);}
  if(u.foam.y<4.5){return mix(vec3f(.02,.05,.08),vec3f(1.,.57,.15),clamp(flow.w,0.,1.));}
  return mix(vec3f(.8,.16,.1),vec3f(.12,.7,.68),smoothstep(0.,.15,geom.x))*clamp(geom.y*.25+.3,.3,1.);
}
