// WCSPH and experimental DFSPH. Neighbor queries, constraints and integration execute on the GPU.
struct Particle { p: vec4<f32>, v: vec4<f32> }
struct Params {
  domain: vec4<f32>, fluid: vec4<f32>, body: vec4<f32>, motion: vec4<f32>,
  poke: vec4<f32>, grid: vec4<u32>, settings: vec4<f32>, rock: vec4<f32>, damping:vec4<f32>
}
@group(0) @binding(0) var<uniform> u: Params;
@group(0) @binding(1) var<storage,read> src: array<Particle>;
@group(0) @binding(2) var<storage,read_write> dst: array<Particle>;
@group(0) @binding(3) var<storage,read_write> heads: array<atomic<u32>>;
@group(0) @binding(4) var<storage,read_write> links: array<u32>;
@group(0) @binding(5) var<storage,read_write> density: array<vec2<f32>>;
@group(0) @binding(6) var<storage,read_write> counters: array<atomic<u32>>;
@group(0) @binding(7) var<storage,read_write> neighbors:array<u32>;
const NIL: u32 = 0xffffffffu;
const PI: f32 = 3.14159265359;
fn cell(p:vec3<f32>)->vec3<i32>{
  let q=vec3<i32>(floor((p+vec3<f32>(u.domain.x,0.,u.domain.y))/u.domain.w));
  return clamp(q,vec3<i32>(0),vec3<i32>(u.grid.xyz)-1);
}
fn key(c:vec3<i32>)->u32{return u32(c.x)+u.grid.x*(u32(c.y)+u.grid.y*u32(c.z));}
fn valid(c:vec3<i32>)->bool{return all(c>=vec3<i32>(0))&&all(c<vec3<i32>(u.grid.xyz));}
fn support(distance:f32)->f32 {let q=clamp(1.-distance/u.domain.w,0.,1.);return .5*u.fluid.y*q*q*q;}
@compute @workgroup_size(128)
fn clear(@builtin(global_invocation_id) id:vec3<u32>){
  if(id.x<u.grid.x*u.grid.y*u.grid.z){atomicStore(&heads[id.x],NIL);}
}
@compute @workgroup_size(128)
fn build(@builtin(global_invocation_id) id:vec3<u32>){
  if(id.x>=u.grid.w){return;}
  links[id.x]=atomicExchange(&heads[key(cell(src[id.x].p.xyz))],id.x);
}
@compute @workgroup_size(128)
fn densities(@builtin(global_invocation_id) id:vec3<u32>){
  let i=id.x;if(i>=u.grid.w){return;}
  let p=src[i].p.xyz;let c=cell(p);let h=u.domain.w;let h2=h*h;
  let poly=315./(64.*PI*pow(h,9.));var rho=0.;var accepted=0u;
  // In-cell distances to neighboring cell faces, reused across the 27-cell stencil.
  let local=p-(vec3<f32>(c)*h-vec3<f32>(u.domain.x,0.,u.domain.y));
  let lower=local*local;let upper=(vec3<f32>(h)-local)*(vec3<f32>(h)-local);
  let gapX=vec3<f32>(lower.x,0.,upper.x);
  let gapY=vec3<f32>(lower.y,0.,upper.y);
  let gapZ=vec3<f32>(lower.z,0.,upper.z);
  for(var z=-1;z<=1;z++){for(var y=-1;y<=1;y++){for(var x=-1;x<=1;x++){
    let other=c+vec3<i32>(x,y,z);if(!valid(other)){continue;}
    // Conservative margin retains boundary interactions despite f32 rounding.
    if(gapX[u32(x+1)]+gapY[u32(y+1)]+gapZ[u32(z+1)]>h2*1.0001){continue;}
    var j=atomicLoad(&heads[key(other)]);var visits=0u;
    loop {
      if(j==NIL){break;}if(visits>=256u){atomicAdd(&counters[0],1u);break;}visits++;
      let d=p-src[j].p.xyz;let q=max(0.,h2-dot(d,d));rho+=u.fluid.x*poly*q*q*q;
      if(q>0.&&j!=i){if(accepted<96u){neighbors[(accepted+1u)*u.grid.w+i]=j;}accepted++;}
      j=links[j];
    }
  }}}
  neighbors[i]=accepted;
  // Approximate solid boundary kernel support. Contacts themselves are projected below.
  rho+=support(p.y)+support(u.domain.z-p.y);
  rho+=support(u.domain.x-abs(p.x))+support(u.domain.y-abs(p.z));
  rho+=support(length(p-u.rock.xyz)-u.rock.w);
  rho+=support(length(p-u.body.xyz)-u.body.w);
  rho=max(rho,u.fluid.y*.15);
  density[i]=vec2<f32>(rho,u.fluid.z*max(0.,rho-u.fluid.y));
}
fn sphereContact(p0:vec3<f32>,v0:vec3<f32>,sphere:vec4<f32>,wallVelocity:vec3<f32>)->Particle{
  var p=p0;var v=v0;let delta=p-sphere.xyz;let distance=length(delta);let radius=sphere.w+u.settings.x;
  if(distance<radius){
    let normal=select(vec3<f32>(0.,1.,0.),delta/max(distance,.00001),distance>.00001);
    p=sphere.xyz+normal*(radius+.0001);
    if(p.y<u.settings.x){
      p.y=u.settings.x;let horizontal=p.xz-sphere.xz;
      let r=sqrt(max(0.,radius*radius-(p.y-sphere.y)*(p.y-sphere.y)));
      let direction=select(vec2<f32>(1.,0.),horizontal/max(length(horizontal),.00001),length(horizontal)>.00001);
      let projected=sphere.xz+direction*(r+.0001);p.x=projected.x;p.z=projected.y;
    }
    var relative=v-wallVelocity;let vn=dot(relative,normal);
    if(vn<0.){relative-=(1.+u.settings.w)*vn*normal;}
    let tangent=relative-dot(relative,normal)*normal;
    v=relative-tangent*.035+wallVelocity;atomicAdd(&counters[2],1u);
  }
  return Particle(vec4<f32>(p,1.),vec4<f32>(v,0.));
}
struct PairTerms {force:vec3<f32>,smoothing:vec3<f32>}
fn pairTerms(j:u32,p:vec3<f32>,v:vec3<f32>,rho:f32,pressure:f32,h:f32,spiky:f32,poly:f32)->PairTerms{
  var result=PairTerms(vec3<f32>(0.),vec3<f32>(0.));
  let delta=p-src[j].p.xyz;let r=length(delta);
  if(r<h&&r>.00001){
          let q=h-r;let rj=density[j].x;let pj=density[j].y;
          let kernel=h*h-r*r;result.smoothing+=2.*u.fluid.x/(rho+rj)*poly*kernel*kernel*kernel*(src[j].v.xyz-v);
          result.force+=u.fluid.x*(pressure/(rho*rho)+pj/(rj*rj))*spiky*q*q*delta/r;
          result.force+=u.fluid.w*u.fluid.x/rj*spiky*q*(src[j].v.xyz-v);
          result.force-=u.settings.z*(q/h)*(q/h)*delta/r;
  }return result;
}
@compute @workgroup_size(128)
fn integrate(@builtin(global_invocation_id) id:vec3<u32>){
  let i=id.x;if(i>=u.grid.w){return;}
  var p=src[i].p.xyz;var v=src[i].v.xyz;let c=cell(p);let h=u.domain.w;
  let rho=density[i].x;let pressure=density[i].y;
  let spiky=45./(PI*pow(h,6.));var acceleration=vec3<f32>(0.,-u.settings.y,0.);
  var smoothing=vec3<f32>(0.);let poly=315./(64.*PI*pow(h,9.));
  let cached=neighbors[i];
  if(cached<=96u){
    for(var k=0u;k<cached;k++){
      let terms=pairTerms(neighbors[(k+1u)*u.grid.w+i],p,v,rho,pressure,h,spiky,poly);
      acceleration+=terms.force;smoothing+=terms.smoothing;
    }
  }else{
    // Never drop interactions when the cache overflows: use the exact original traversal.
    for(var z=-1;z<=1;z++){for(var y=-1;y<=1;y++){for(var x=-1;x<=1;x++){
      let other=c+vec3<i32>(x,y,z);if(!valid(other)){continue;}
      var j=atomicLoad(&heads[key(other)]);var visits=0u;
      loop{if(j==NIL||visits>=256u){break;}visits++;
        if(i!=j){let terms=pairTerms(j,p,v,rho,pressure,h,spiky,poly);acceleration+=terms.force;smoothing+=terms.smoothing;}
        j=links[j];
      }
    }}}
  }
  let jet=p-u.poke.xyz;let weight=exp(-dot(jet,jet)/.18);
  acceleration+=u.poke.w*weight*(vec3<f32>(0.,1.,0.)+vec3<f32>(jet.x,0.,jet.z)*1.4);
  v+=acceleration*u.motion.w+smoothing*(u.damping.x*u.motion.w/.002);
  if(u.damping.w>.5){
    // Add the already-computed divergence correction to explicit nonpressure forces.
    v+=dst[i].v.xyz-src[i].v.xyz;
    dst[i]=Particle(src[i].p,vec4<f32>(v,0.));
  }else{advance(i,v);}
}
fn advance(i:u32,v0:vec3<f32>){
  var p=src[i].p.xyz;var v=v0;let rho=density[i].x;
  let speed=length(v);if(speed>12.){v*=12./speed;atomicAdd(&counters[1],1u);}
  p+=v*u.motion.w;
  // Kinematic, moving solid body: contact impulses are relative to its velocity.
  var hit=sphereContact(p,v,u.rock,vec3<f32>(0.));p=hit.p.xyz;v=hit.v.xyz;
  for(var contact=0;contact<12;contact++){
    let bodyDelta=p-u.body.xyz;let headDelta=p-u.body.xyz-vec3<f32>(0.,.34,.17);
    let bodyRadius=u.body.w+u.settings.x;let headRadius=.24+u.settings.x;
    if(dot(bodyDelta,bodyDelta)>=bodyRadius*bodyRadius&&dot(headDelta,headDelta)>=headRadius*headRadius){break;}
    hit=sphereContact(p,v,u.body,u.motion.xyz);p=hit.p.xyz;v=hit.v.xyz;
    hit=sphereContact(p,v,vec4<f32>(u.body.xyz+vec3<f32>(0.,.34,.17),.24),u.motion.xyz);p=hit.p.xyz;v=hit.v.xyz;
  }
  let lo=vec3<f32>(-u.domain.x+u.settings.x,u.settings.x,-u.domain.y+u.settings.x);
  let hi=vec3<f32>(u.domain.x-u.settings.x,u.domain.z-u.settings.x,u.domain.y-u.settings.x);
  for(var axis=0;axis<3;axis++){
    if(p[axis]<lo[axis]){p[axis]=lo[axis];v[axis]=abs(v[axis])*u.settings.w;atomicAdd(&counters[2],1u);}
    if(p[axis]>hi[axis]){p[axis]=hi[axis];v[axis]=-abs(v[axis])*u.settings.w;atomicAdd(&counters[2],1u);}
  }
  // Floor projection can lift a previously outside particle into the submerged rock.
  hit=sphereContact(p,v,u.rock,vec3<f32>(0.));p=hit.p.xyz;v=hit.v.xyz;
  let badP=any((bitcast<vec3<u32>>(p)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
  let badV=any((bitcast<vec3<u32>>(v)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
  if(badP||badV||any(abs(p)>2.*vec3<f32>(u.domain.x,u.domain.z,u.domain.y)+vec3<f32>(1.))){
    p=vec3<f32>(0.,1.5,0.);v=vec3<f32>(0.);atomicAdd(&counters[3],1u);
  }
  dst[i]=Particle(vec4<f32>(p,rho/u.fluid.y),vec4<f32>(v,0.));
}

// Experimental DFSPH: separate velocity-divergence and predicted-density projections.
// Normalized poly6 gradients match the density kernel, including the derivatives
// of the approximate solid-support terms. Jacobi multipliers are read only after
// a dispatch boundary; correction reads no neighboring writable velocities.
@group(0) @binding(8) var<storage,read_write> df:array<vec4<f32>>;
fn dfGradient(i:u32,j:u32)->vec3<f32>{
  let delta=src[i].p.xyz-src[j].p.xyz;let h=u.domain.w;
  let q=max(0.,h*h-dot(delta,delta));
  return -6.*u.fluid.x/u.fluid.y*(315./(64.*PI*pow(h,9.)))*q*q*delta;
}
fn supportGradient(distance:f32,normal:vec3<f32>)->vec3<f32>{
  if(distance<=0.||distance>=u.domain.w){return vec3<f32>(0.);}
  let q=1.-distance/u.domain.w;return -1.5/u.domain.w*q*q*normal;
}
fn dfBodyGradient(p:vec3<f32>)->vec3<f32>{
  let delta=p-u.body.xyz;let r=length(delta);
  return supportGradient(r-u.body.w,delta/max(r,.00001));
}
fn dfBoundaryGradient(p:vec3<f32>)->vec3<f32>{
  var g=supportGradient(p.y,vec3<f32>(0.,1.,0.))+supportGradient(u.domain.z-p.y,vec3<f32>(0.,-1.,0.));
  g+=supportGradient(u.domain.x-abs(p.x),vec3<f32>(-sign(p.x),0.,0.));
  g+=supportGradient(u.domain.y-abs(p.z),vec3<f32>(0.,0.,-sign(p.z)));
  let delta=p-u.rock.xyz;let r=length(delta);
  return g+supportGradient(r-u.rock.w,delta/max(r,.00001))+dfBodyGradient(p);
}
@compute @workgroup_size(128)
fn dfFactors(@builtin(global_invocation_id) id:vec3<u32>){
  let i=id.x;if(i>=u.grid.w){return;}
  var gradient=dfBoundaryGradient(src[i].p.xyz);var diagonal=0.;
  let cached=neighbors[i];
  if(cached<=96u){for(var k=0u;k<cached;k++){
    let j=neighbors[(k+1u)*u.grid.w+i];
    let g=dfGradient(i,j);gradient+=g;diagonal+=dot(g,g);
  }}else{
    let c=cell(src[i].p.xyz);
    for(var z=-1;z<=1;z++){for(var y=-1;y<=1;y++){for(var x=-1;x<=1;x++){
      let other=c+vec3<i32>(x,y,z);if(!valid(other)){continue;}
      var j=atomicLoad(&heads[key(other)]);var visits=0u;
      loop{if(j==NIL||visits>=256u){break;}visits++;
        if(j!=i){let g=dfGradient(i,j);gradient+=g;diagonal+=dot(g,g);}j=links[j];
      }
    }}}
  }

  diagonal+=dot(gradient,gradient);
  df[i]=vec4<f32>(select(0.,1./max(diagonal,.000001),diagonal>.000001),0.,0.,0.);
  dst[i]=src[i];
}
fn dfRate(i:u32)->f32{
  let p=src[i].p.xyz;let v=dst[i].v.xyz;
  var rate=dot(dfBoundaryGradient(p),v)-dot(dfBodyGradient(p),u.motion.xyz);
  let cached=neighbors[i];
  if(cached<=96u){for(var k=0u;k<cached;k++){
    let j=neighbors[(k+1u)*u.grid.w+i];
    rate+=dot(dfGradient(i,j),v-dst[j].v.xyz);
  }}else{
    let c=cell(src[i].p.xyz);
    for(var z=-1;z<=1;z++){for(var y=-1;y<=1;y++){for(var x=-1;x<=1;x++){
      let other=c+vec3<i32>(x,y,z);if(!valid(other)){continue;}
      var j=atomicLoad(&heads[key(other)]);var visits=0u;
      loop{if(j==NIL||visits>=256u){break;}visits++;
        if(j!=i){rate+=dot(dfGradient(i,j),v-dst[j].v.xyz);}j=links[j];
      }
    }}}
  }

  return rate;
}
@compute @workgroup_size(128)
fn dfDivergence(@builtin(global_invocation_id) id:vec3<u32>){
  let i=id.x;if(i>=u.grid.w){return;}
  // Free-surface deficiency makes a divergence constraint unreliable there.
  let rate=select(0.,max(0.,dfRate(i)),neighbors[i]>=20u);
  df[i].y=.5*rate*df[i].x;
}
@compute @workgroup_size(128)
fn dfDensity(@builtin(global_invocation_id) id:vec3<u32>){
  let i=id.x;if(i>=u.grid.w){return;}
  let error=max(0.,(density[i].x/u.fluid.y-1.)/u.motion.w+dfRate(i));
  df[i].y=.5*error*df[i].x;
}
@compute @workgroup_size(128)
fn dfCorrect(@builtin(global_invocation_id) id:vec3<u32>){
  let i=id.x;if(i>=u.grid.w){return;}
  var correction=df[i].y*dfBoundaryGradient(src[i].p.xyz);
  let cached=neighbors[i];
  if(cached<=96u){for(var k=0u;k<cached;k++){
    let j=neighbors[(k+1u)*u.grid.w+i];
    correction+=(df[i].y+df[j].y)*dfGradient(i,j);
  }}else{
    let c=cell(src[i].p.xyz);
    for(var z=-1;z<=1;z++){for(var y=-1;y<=1;y++){for(var x=-1;x<=1;x++){
      let other=c+vec3<i32>(x,y,z);if(!valid(other)){continue;}
      var j=atomicLoad(&heads[key(other)]);var visits=0u;
      loop{if(j==NIL||visits>=256u){break;}visits++;
        if(j!=i){correction+=(df[i].y+df[j].y)*dfGradient(i,j);}j=links[j];
      }
    }}}
  }

  dst[i].v=vec4<f32>(dst[i].v.xyz-correction,0.);
}
@compute @workgroup_size(128)
fn dfDiagnostics(@builtin(global_invocation_id) id:vec3<u32>){
  let i=id.x;if(i>=u.grid.w){return;}
  let rate=dfRate(i);
  df[i].z=max(0.,density[i].x/u.fluid.y-1.+u.motion.w*rate);
  df[i].w=max(0.,rate)*u.motion.w;
}
@compute @workgroup_size(128)
fn dfAdvance(@builtin(global_invocation_id) id:vec3<u32>){
  let i=id.x;if(i>=u.grid.w){return;}advance(i,dst[i].v.xyz);
}
