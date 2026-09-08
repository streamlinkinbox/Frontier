// Shared WGSL/GLSL GPU state updates. No CPU particle loop or volume readback.
struct FoamParams { tick:vec4f, grid:vec4f, focus:vec4f, };
struct FoamPair { a:vec4f, b:vec4f, };
fn foamWorld(uv:vec2f) -> vec2f {return uv*96.-vec2f(48.);}
fn foamUV(xz:vec2f) -> vec2f {return (xz+vec2f(48.))/96.;}
fn foamWet(g:vec4f) -> f32 {return select(0.,1.,g.x>.015&&g.y>.02);}
fn geomAt(xz:vec2f) -> vec4f {return textureSampleLevel(foamGeometry,linearSampler,foamUV(xz),0.);}
fn motionAt(xz:vec2f) -> vec4f {return textureSampleLevel(foamMotion,linearSampler,foamUV(xz),0.);}
fn confinedVelocity(v:vec2f,g:vec4f) -> vec2f {
  let fade:f32=1.-smoothstep(.02,1.2,g.x);
  return (v-min(dot(v,g.zw),0.)*g.zw*fade)*foamWet(g);
}
fn foamMapGeometry(uv:vec2f) -> FoamPair {
  let xz:vec2f=foamWorld(uv);let p:vec3f=vec3f(xz.x,u.water.x,xz.y);
  let d:f32=map(p);let e:f32=max(.38,u.dims.w*.4);
  let n:vec2f=vec2f(map(p+vec3f(e,0.,0.))-map(p-vec3f(e,0.,0.)),map(p+vec3f(0.,0.,e))-map(p-vec3f(0.,0.,e)));
  var normal:vec2f=n/max(length(n),.0001);
  var depth:f32=0.;var light:vec3f=vec3f(0.);
  if(d>.015&&max(abs(xz.x),abs(xz.y))<45.55){
    // First solid BELOW water, not the upper terrain envelope (which may be a roof).
    var previous:f32=d;var y:f32=u.water.x;
    for(var i:i32=0;i<40;i++){
      y-=.3;let below:f32=map(vec3f(xz.x,y,xz.y));
      if(below<0.){depth=u.water.x-y-.3*clamp(-below/max(previous-below,.001),0.,1.);break;}
      previous=below;depth=min(6.,u.water.x-y);
      if(y< -9.5){break;}
    }
    let sun:vec3f=normalize(vec3f(-.67,sin(u.up.w),.46));
    var sh:f32=1.;var t:f32=.25;
    for(var i:i32=0;i<18;i++){
      let sd:f32=map(p+sun*t);sh=min(sh,10.*sd/t);t+=clamp(sd,.25,3.);
      if(sd<.01||t>40.){break;}
    }
    let access:f32=clamp(map(p+vec3f(0.,2.,0.))/2.,.15,1.);
    light=vec3f(.09,.11,.13)*access+vec3f(.68,.63,.55)*clamp(sh,0.,1.);
  }
  return FoamPair(vec4f(clamp(d,-8.,8.),clamp(depth,0.,6.),normal),vec4f(light,select(0.,1.,depth>.02&&d>.015)));
}
fn foamFlowStep(uv:vec2f) -> vec4f {
  let p:vec2f=foamWorld(uv);let g:vec4f=geomAt(p);if(foamWet(g)<.5){return vec4f(0.);}
  let dt:f32=f.tick.x;let dx:f32=96./f.grid.x;let prev:vec4f=motionAt(p);
  let l:vec4f=motionAt(p-vec2f(dx,0.));let r:vec4f=motionAt(p+vec2f(dx,0.));
  let b:vec4f=motionAt(p-vec2f(0.,dx));let t:vec4f=motionAt(p+vec2f(0.,dx));
  let drivingVelocity:vec2f=riverDirection(vec3f(p.x,0.,p.y))*u.river.x;
  var v:vec2f=motionAt(p-prev.xy*dt).xy;
  if(f.tick.w>.5){v=drivingVelocity;}
  let divergence:f32=(r.x-l.x+t.y-b.y)/(2.*dx);
  let curl:f32=(r.y-l.y-t.x+b.x)/(2.*dx);
  // A bounded 2.5D momentum/height correction. Standard only uses obstacle-
  // deflected driving flow. Ultra adds inertial transport; Cinematic concentrates
  // stronger stirring/head correction in its local whitewater focus.
  var eta:f32=0.;
  let local:f32=1.-smoothstep(f.focus.z*.7,f.focus.z,length(p-f.focus.xy));
  if(f.tick.z>1.5){
    let patchWeight:f32=select(1.,local,f.tick.z>2.5);
    let grad:vec2f=vec2f(r.z-l.z,t.z-b.z)/(2.*dx);
    v-=grad*(9.81*dt)*patchWeight;
    eta=clamp((prev.z-dt*min(g.y,1.5)*divergence)*exp(-dt*2.),-.12,.12)*patchWeight;
    let wave:f32=sin(p.x*.47+p.y*.29+f.tick.y*.2);
    v+=vec2f(.29,-.47)*wave*(u.foamEffects.x*u.river.x*.4*dt)*patchWeight;
    v=mix(v,drivingVelocity,1.-exp(-dt*.65));
  }else{v=mix(v,drivingVelocity,1.-exp(-dt*4.));}
  let raw:vec2f=v;
  v=confinedVelocity(v,g);v*=exp(-dt*.09/max(g.y,.1));
  v*=min(1.,3./max(length(v),.001));
  let energy:f32=clamp(length(raw)*.7+u.water.y*u.water.y*.8,0.,2.);
  let impact:f32=exp(-max(g.x,0.)*1.2)*abs(dot(raw,g.zw));
  let breaker:f32=exp(-max(g.x,0.)*.45)*u.water.y*u.water.y*(.5+.5*sin(p.x*1.7+p.y*.73+f.tick.y*2.));
  let source:f32=u.waterOptics.y*energy*(impact*2.5+breaker*1.8+min(abs(curl),2.)*.35+max(-divergence,0.)*.18);
  return vec4f(v,eta,clamp(source,0.,3.));
}
fn foamDensityStep(uv:vec2f) -> vec4f {
  let p:vec2f=foamWorld(uv);let g:vec4f=geomAt(p);if(foamWet(g)<.5){return vec4f(0.);}
  let dt:f32=f.tick.x;let v:vec4f=motionAt(p);
  let mid:vec2f=p-v.xy*dt*.5;var back:vec2f=p-motionAt(mid).xy*dt;
  if(foamWet(geomAt(back))<.5){back=p;}
  let old:vec4f=textureSampleLevel(foamDensity,linearSampler,foamUV(back),0.);
  let dx:f32=96./f.grid.x;
  let divergence:f32=(motionAt(p+vec2f(dx,0.)).x-motionAt(p-vec2f(dx,0.)).x+motionAt(p+vec2f(0.,dx)).y-motionAt(p-vec2f(0.,dx)).y)/(2.*dx);
  let decay:f32=exp(-dt*(.693147/u.foam.w+clamp(divergence,-.5,.5)));
  let density:f32=min(6.,old.x*decay+dt*v.w);
  if(density<.0001){return vec4f(0.);}
  let moment:f32=min(density*u.foam.w*4.,(old.y+old.x*dt)*decay);
  return vec4f(density,moment,0.,1.);
}
fn readFoamParticle(id:i32) -> FoamPair {
  let coord:vec2i=vec2i(id%i32(f.grid.z),id/i32(f.grid.z));
  return FoamPair(textureLoad(foamPositions,coord,0),textureLoad(foamVelocities,coord,0));
}
fn foamParticleStep(id:i32) -> FoamPair {
  let old:FoamPair=readFoamParticle(id);
  var p:vec3f=old.a.xyz;var age:f32=old.a.w;var v:vec3f=old.b.xyz;var kind:f32=old.b.w;
  let dt:f32=f.tick.x;
  let random:f32=hash3(vec3i(id,i32(f.tick.y*30.),i32(u.flags.z)));
  let identityRandom:f32=hash3(vec3i(id,419,i32(u.flags.z)));
  if(age<=0.||kind<.5){
    let candidate:vec2f=vec2f(hash3(vec3i(id,13,i32(f.tick.y*30.)+i32(u.flags.z))),hash3(vec3i(id,23,i32(f.tick.y*30.)+i32(u.flags.z))))*88.-vec2f(44.);
    let g:vec4f=geomAt(candidate);let m:vec4f=motionAt(candidate);
    if(foamWet(g)<.5||random>min(.8,m.w*dt*5.)){return FoamPair(vec4f(0.),vec4f(0.));}
    p=vec3f(candidate.x,u.water.x,candidate.y);p.y+=waterWaves(p).x;
    kind=1.;v=vec3f(m.x,0.,m.y);
    let local:f32=1.-smoothstep(f.focus.z*.7,f.focus.z,length(candidate-f.focus.xy));
    if(f.tick.z>2.5&&identityRandom<.46*local){
      if(identityRandom<.23*local&&u.foamEffects.y>.001){kind=2.;v.y=(.7+identityRandom*7.)*u.foamEffects.y;}
      else if(u.foamEffects.z>.001){kind=3.;p.y-=min(g.y*.65,.18+identityRandom)*u.foamEffects.z;v.y=-.2;}
    }
    age=u.foam.w*(.65+.7*identityRandom);
  }else{
    age-=dt;
    let m:vec4f=motionAt(p.xz);let g:vec4f=geomAt(p.xz);
    if(foamWet(g)<.5){return FoamPair(vec4f(0.),vec4f(0.));}
    let surface:f32=u.water.x+waterWaves(p).x;
    if(kind<1.5){
      let predicted:vec2f=p.xz+motionAt(p.xz+m.xy*dt*.5).xy*dt;
      let nextG:vec4f=geomAt(predicted);
      if(foamWet(nextG)>.5){p=vec3f(predicted.x,p.y,predicted.y);}
      p.y=u.water.x+waterWaves(p).x+.005;
      v=vec3f(m.x,0.,m.y);
    }else if(kind<2.5){
      v.y-=9.81*dt;v*=exp(-dt*.2);p+=v*dt;
      if(p.y<surface-.015){kind=select(1.,3.,identityRandom<.45&&u.foamEffects.z>.001);v.y=select(0.,-.3,kind>2.5);}
    }else{
      v.x=mix(v.x,m.x,1.-exp(-dt*3.));v.z=mix(v.z,m.y,1.-exp(-dt*3.));
      v.y=mix(v.y,.22+identityRandom*.4,1.-exp(-dt*3.));p+=v*dt;
      if(p.y>=surface-.025){kind=1.;p.y=surface+.005;age=max(age,u.foam.w*.5);}
    }
    // Swept, bounded tests: do not tunnel through newly sculpted banks or a roof.
    let middle:vec3f=(old.a.xyz+p)*.5;
    if(map(p)<.005||map(middle)<0.){
      if(kind<1.5){p=old.a.xyz;age-=dt;}
      else{return FoamPair(vec4f(0.),vec4f(0.));}
    }
  }
  if(max(abs(p.x),abs(p.z))>44.8||age<=0.){return FoamPair(vec4f(0.),vec4f(0.));}
  return FoamPair(vec4f(p,age),vec4f(v,kind));
}
fn foamCorner(id:i32) -> vec2f {
  return vec2f(select(-1.,1.,id==1||id==4||id==5),select(-1.,1.,id==2||id==3||id==5));
}
