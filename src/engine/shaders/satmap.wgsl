// Satellite-derived CLUT + actual terrain data. NO legacy noise/strata albedo.
// CPU reference/export: satmaps/satmap.ts. Satellite detail is an artistic
// luminance signal, not measured geometry. Mirror/triplanar projection handles
// steep cliffs, arches and overhangs without a stretched top-down photograph.
fn satMirror(uv:vec2f) -> vec2f {return vec2f(1.)-abs(fract(uv*.5)*2.-1.);}
fn satPhotoRead(coordinates:vec2f,lod:f32) -> f32 {
  let uv:vec2f=satMirror(coordinates);
  return textureSampleLevel(satDetail,linearSampler,uv,lod).r;
}
fn satPhoto(p:vec3f,n:vec3f,footprint:f32) -> f32 {
  let q:vec3f=p/u.satWeather.w;
  let w:vec3f=pow(abs(n),vec3f(4.));let total:f32=max(.000001,w.x+w.y+w.z);
  let lod:f32=clamp(log2(max(1.,footprint*64./u.satWeather.w)),0.,6.);
  return (satPhotoRead(q.zy+vec2f(.17,.31),lod)*w.x+satPhotoRead(q.xz,lod)*w.y+satPhotoRead(q.xy+vec2f(.43,.71),lod)*w.z)/total;
}
fn satCurvature(p:vec3f) -> f32 {
  let e:f32=max(u.dims.w*1.4,.8);
  let a:f32=map(p+vec3f(e,0,0));let b:f32=map(p-vec3f(e,0,0));
  let c:f32=map(p+vec3f(0,e,0));let d:f32=map(p-vec3f(0,e,0));
  let f:f32=map(p+vec3f(0,0,e));let g:f32=map(p-vec3f(0,0,e));
  let lap:f32=a+b+c+d+f+g-6.*map(p);
  let gradient:vec3f=vec3f(a-b,c-d,f-g)/(2.*e);
  return clamp(lap/(e*max(length(gradient),.15))*1.2,-1.,1.);
}
fn satElevation(p:vec3f) -> f32 {return clamp((p.y-u.satSurface.y)/max(1.,u.satSurface.z-u.satSurface.y),0.,1.);}
fn satFlow(p:vec3f,n:vec3f) -> f32 {
  let uv:vec2f=(p.xz+vec2f(48.))/96.;
  let data:vec4f=textureSampleLevel(satTerrain,linearSampler,uv,0.);
  let height:f32=-10.+(data.g*256.+data.b)/257.*48.;
  let top:f32=1.-smoothstep(u.dims.w*1.5,u.dims.w*4.,abs(p.y-height));
  return data.r*data.a*top*smoothstep(-.05,.65,n.y);
}
fn satSediment(state:vec4f,n:vec3f) -> f32 {
  // w is signed accumulated surface displacement. Negative = deposition;
  // suspended sediment (z) is NOT painted as deposited ground material.
  return clamp(-state.w/.65,0.,1.)*smoothstep(.05,.75,n.y);
}
fn satTextureMask(p:vec3f,n:vec3f,ao:f32,photo:f32,curvature:f32,state:vec4f) -> f32 {
  let slope:f32=1.-clamp(n.y,0.,1.);
  let total:f32=u.satShape.x+u.satShape.y+u.satShape.z+u.satShape.w+u.satWeather.z;
  var value:f32=.5;
  if(total>.00001){value=(satElevation(p)*u.satShape.x+(1.-slope)*u.satShape.y+(curvature*.5+.5)*u.satShape.z+ao*u.satShape.w+photo*u.satWeather.z)/total;}
  let runoff:f32=max(satFlow(p,n),clamp(state.y*3.,0.,1.));
  value=mix(value,.1+photo*.16,runoff*u.satWeather.x);
  value=mix(value,.82+photo*.16,satSediment(state,n)*u.satWeather.y);
  return clamp(value,0.,1.);
}
fn satColorAt(value:f32) -> vec3f {
  var t:f32=pow(clamp((value-.5)*u.satColor.y+.5,0.,1.),exp2(-u.satColor.x*2.));
  if(u.satColor.w>.5){t=1.-t;}
  t=mix(u.satmap.z,u.satmap.w,t);
  // Texel-center mapping, sRGB GPU texture decodes BEFORE interpolation.
  let uv:vec2f=vec2f((t*255.+.5)/256.,.5);
  let color:vec3f=textureSampleLevel(satPalette,linearSampler,uv,0.).rgb;
  let luma:f32=dot(color,vec3f(.2126,.7152,.0722));
  return clamp(mix(vec3f(luma),color,u.satColor.z),vec3f(0.),vec3f(1.));
}
fn sampleSatMaterial(p:vec3f,n:vec3f,footprint:f32,ao:f32) -> vec4f {
  let state:vec4f=volume(p+n*u.dims.w*.22);
  let photo:f32=satPhoto(p,n,footprint);
  var curvature:f32=0.;if(u.satShape.z>0.){curvature=satCurvature(p);}
  let mask:f32=satTextureMask(p,n,ao,photo,curvature,state);
  let roughness:f32=clamp(u.material.y+(.5-photo)*.13+satSediment(state,n)*.08,.12,1.);
  return vec4f(satColorAt(mask),roughness);
}
fn satNormal(p:vec3f,n:vec3f,footprint:f32) -> vec3f {
  if(u.satSurface.x<=0.||u.flags.w<=0.){return n;}
  let e:f32=max(u.satWeather.w/128.,footprint*.5);
  let dx:f32=satPhoto(p+vec3f(e,0,0),n,footprint)-satPhoto(p-vec3f(e,0,0),n,footprint);
  let dy:f32=satPhoto(p+vec3f(0,e,0),n,footprint)-satPhoto(p-vec3f(0,e,0),n,footprint);
  let dz:f32=satPhoto(p+vec3f(0,0,e),n,footprint)-satPhoto(p-vec3f(0,0,e),n,footprint);
  var gradient:vec3f=vec3f(dx,dy,dz)/(2.*e)*u.satSurface.x*u.flags.w;
  gradient-=n*dot(n,gradient);gradient/=max(1.,length(gradient)/.7);
  return safeNormalize(n-gradient);
}
