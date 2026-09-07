// Four non-parallel wave bands, with deep-water dispersion and a slowly varying
// envelope. xy gradients describe the same height used for ray/surface intersection.
fn waveBand(q:vec2f,d:vec2f,k:f32,a:f32,time:f32,phase:f32) -> vec3f {
  let angle:f32=dot(q,d)*k-sqrt(9.81*k)*time+phase;
  let derivative:f32=cos(angle)*a*k;
  return vec3f(sin(angle)*a,d.x*derivative,d.y*derivative);
}
fn waterWaves(p:vec3f) -> vec3f {
  let time:f32=u.eye.w*(.26+u.water.y*.5);
  let envelope:f32=.78+.22*noise(vec3f(p.x*.07,time*.06,p.z*.07));
  let amplitude:f32=(.016+.11*u.water.y*u.water.y)*envelope;
  var wave:vec3f=waveBand(p.xz,vec2f(.94,.342),.62,amplitude*.55,time,1.7);
  wave+=waveBand(p.xz,vec2f(.24,.971),1.31,amplitude*.25,time*.94,4.3);
  wave+=waveBand(p.xz,vec2f(-.73,.683),2.71,amplitude*.14,time*.81,2.1);
  wave+=waveBand(p.xz,vec2f(.83,-.558),5.49,amplitude*.06*detailVisibility(p,5.49),time*.7,5.7);
  return wave;
}
// Find the FIRST crossing of the displaced surface inside a bounded wave slab.
// A Newton solve can land on a farther, back-facing wave at grazing angles.
fn waterSurfaceHit(ro:vec3f,rd:vec3f,opaqueT:f32) -> f32 {
  let amplitude:f32=.016+.11*u.water.y*u.water.y;
  let box:vec2f=boxHit(ro,rd);
  var start:f32=max(.001,box.x);var end:f32=min(opaqueT,box.y);
  if(abs(rd.y)>.00001){
    let a:f32=(u.water.x-amplitude-ro.y)/rd.y;let b:f32=(u.water.x+amplitude-ro.y)/rd.y;
    start=max(start,min(a,b));end=min(end,max(a,b));
  }else if(abs(ro.y-u.water.x)>amplitude){return 10000.;}
  if(start>=end){return 10000.;}
  let bound:f32=abs(rd.y)+(amplitude*1.55+.005)*length(rd.xz);
  var t:f32=start;var previousT:f32=t;
  var previous:f32=(ro+rd*t).y-u.water.x-waterWaves(ro+rd*t).x;
  for(var i:i32=0;i<80;i++){
    let point:vec3f=ro+rd*t;let d:f32=point.y-u.water.x-waterWaves(point).x;
    if(abs(d)<.0008){return t;}
    if(d*previous<0.){
      var lo:f32=previousT;var hi:f32=t;
      for(var j:i32=0;j<7;j++){
        let mid:f32=(lo+hi)*.5;let q:vec3f=ro+rd*mid;let value:f32=q.y-u.water.x-waterWaves(q).x;
        if(value*previous>0.){lo=mid;}else{hi=mid;}
      }
      return (lo+hi)*.5;
    }
    previous=d;previousT=t;t+=max(.004,abs(d)/max(bound,.0001)*.85);
    if(t>end){return 10000.;}
  }
  return 10000.;
}
fn rockWetness(p:vec3f) -> f32 {
  if(u.water.z<.5){return 0.;}
  var level:f32=u.water.x;
  if(abs(p.y-level)<1.2){level+=waterWaves(p).x;}
  let seep:f32=.10+u.materialShape.z*.28+.035*noise(p*vec3f(.65,.08,.65));
  return 1.-smoothstep(level-.08,level+seep,p.y);
}

fn fbm(p:vec3f) -> f32 {return noise(p)*.58+noise(p*2.07+vec3f(3.1))*.28+noise(p*4.21+vec3f(7.7))*.14;}
fn sunDirection() -> vec3f {return normalize(vec3f(-.67,sin(u.up.w),.46));}
fn sky(rd:vec3f) -> vec3f {
  let h:f32=pow(1.-max(rd.y,0.),3.);
  var col:vec3f=mix(vec3f(.075,.115,.15),vec3f(.20,.235,.25),h);
  col+=vec3f(.35,.26,.16)*pow(max(dot(rd,sunDirection()),0.),30.)*.18;
  return col;
}
fn softShadow(p:vec3f,light:vec3f) -> f32 {
  if(u.flags.y<.5){return 1.;}
  var t:f32=.55;var shadow:f32=1.;
  for(var i:i32=0;i<34;i++) {
    let d:f32=map(p+light*t);
    shadow=min(shadow,12.*d/t);
    t+=clamp(d,.24,3.8);
    if(d<.025||t>76.){break;}
  }
  return clamp(shadow,.10,1.);
}
fn ambientOcclusion(p:vec3f,n:vec3f) -> f32 {
  var occ:f32=0.;var weight:f32=.65;
  for(var i:i32=0;i<5;i++) {
    let h:f32=.6+f32(i)*1.45;
    occ+=max(0.,h-map(p+n*h))*weight;
    weight*=.52;
  }
  return clamp(1.-occ*.32,.23,1.);
}
fn shadeRock(p:vec3f,rd:vec3f,details:bool) -> vec3f {
  var geometric:vec3f=surfaceNormal(p);
  if(dot(geometric,rd)>0.){geometric=-geometric;}
  let footprint:f32=materialFootprint(p,geometric);
  let surfaceData:vec4f=sampleMaterial(p,geometric,select(footprint*4.,footprint,details));
  var albedo:vec3f=surfaceData.rgb;var roughness:f32=surfaceData.a;
  var n:vec3f=geometric;
  if(details&&u.viewport.w<.5){n=materialNormal(p,geometric,footprint);}
  let view:vec3f=-rd;
  // Keep perturbed normals on the visible side at grazing angles.
  if(dot(n,view)<.03){n=safeNormalize(mix(n,geometric,.65));}
  let light:vec3f=sunDirection();let halfway:vec3f=safeNormalize(view+light);
  let noL:f32=max(dot(n,light),0.);let noV:f32=max(dot(n,view),.001);
  let noH:f32=max(dot(n,halfway),0.);let voH:f32=max(dot(view,halfway),0.);
  let state:vec4f=volume(p+geometric*u.dims.w*.22);
  let wet:f32=max(u.materialOptics.y,max(rockWetness(p),clamp(state.y*3.,0.,1.)));
  var f0:f32=dielectricF0(u.materialOptics.x);
  albedo*=1.-wet*(.12+u.materialShape.z*.28);
  roughness=mix(roughness,max(.12,roughness*.45),wet);
  f0=mix(f0,dielectricF0(1.333),wet);
  if(u.viewport.w>.5&&u.viewport.w<1.5){albedo=vec3f(.45,.43,.38);roughness=.85;f0=.04;n=geometric;}
  if(u.viewport.w>1.5){
    albedo=mix(vec3f(.28,.31,.31),vec3f(.12,.54,.67),clamp(state.y*9.,0.,1.));
    albedo=mix(albedo,vec3f(.9,.32,.12),clamp(max(state.w,0.)*3.,0.,.9));
    albedo=mix(albedo,vec3f(.65,.72,.31),clamp(max(-state.w,0.)*3.,0.,.8));
  }
  // Secondary (reflected/refracted) rock still needs macro shadowing. Skipping
  // it makes the bed/underside glow at the shoreline even at zero thickness.
  let shadow:f32=softShadow(p+geometric*.25,light);
  let ao:f32=ambientOcclusion(p,geometric);
  let diffuseWeight:f32=(1.-fresnelSchlick(noV,f0))*(1.-fresnelSchlick(noL,f0));
  let specular:f32=specularGGX(noV,noL,noH,voH,roughness,f0);
  let sun:vec3f=vec3f(4.6,4.25,3.7);
  var col:vec3f=(albedo*(diffuseWeight/3.14159265)+vec3f(specular))*sun*noL*shadow;
  let up:f32=clamp(n.y*.5+.5,0.,1.);
  let irradiance:vec3f=mix(vec3f(.10,.08,.06),vec3f(.27,.32,.38),up);
  col+=albedo*(1.-f0)*irradiance*ao;
  let reflected:vec3f=reflect(rd,n);
  let env:vec3f=mix(sky(reflected),sky(vec3f(0,1,0)),roughness*.65);
  let envF:f32=f0+(1.-f0)*pow(1.-noV,5.)*(1.-roughness*.75);
  col+=env*envF*ao;
  if(u.brush.w>0.&&details){
    var v:vec3f=p-u.brush.xyz;
    if(u.brushParams.z>4.5&&u.brushParams.z<5.5){v-=u.planeNormal.xyz*dot(v,u.planeNormal.xyz);}
    let dist:f32=length(v);let ring:f32=1.-smoothstep(.035,.16,abs(dist-u.brush.w));
    let tint:vec3f=select(vec3f(.45,.79,.62),vec3f(1.,.60,.25),u.brushParams.z>1.5&&u.brushParams.z<7.5);
    col=mix(col,tint,ring*.9);col=mix(col,tint,(1.-smoothstep(0.,u.brush.w,dist))*.1);
  }
  return col;
}
// Entry-only tracing: starting inside/against a bank is an immediate hit, never
// permission to march through the rock and call its far exit a deep water column.
// y: 1 = rock entry, 0 = exit from bounded volume, -1 = uncertain/budget exhausted.
fn traceWaterRay(origin:vec3f,direction:vec3f) -> vec2f {
  if(dot(direction,direction)<.000001){return vec2f(0.,-1.);}
  let bounds:vec2f=boxHit(origin,direction);
  if(bounds.y<=bounds.x){return vec2f(0.,0.);}
  var t:f32=max(0.,bounds.x);
  for(var i:i32=0;i<144;i++){
    let d:f32=map(origin+direction*t);
    if(d<=.008+min(t*.0001,.01)){return vec2f(t,1.);}
    t+=max(.006,d*.65);
    if(t>=bounds.y){return vec2f(bounds.y,0.);}
  }
  return vec2f(t,-1.);
}
fn waterRayOrigin(p:vec3f,direction:vec3f) -> vec3f {
  let bias:f32=min(.012,max(map(p),0.)*.15);
  let candidate:vec3f=p+direction*bias;
  if(map(candidate)<0.){return p;}
  return candidate;
}
fn waterTurbidity(p:vec3f) -> f32 {
  return clamp((1.-u.flags.x)*.8+volume(p).z*4.,0.,1.);
}
fn waterIllumination(p:vec3f) -> vec3f {
  let sunlight:f32=softShadow(p+vec3f(0,.03,0),sunDirection());
  let skyAccess:f32=ambientOcclusion(p,vec3f(0,1,0));
  return vec3f(.075,.085,.095)*skyAccess+vec3f(.65,.61,.54)*sunlight;
}
fn waterSegment(background:vec3f,length:f32,turbidity:f32,illumination:vec3f) -> vec3f {
  let coefficients:vec3f=mix(vec3f(3.,.75,.4),vec3f(1.8,2.6,4.2),turbidity)/max(u.waterOptics.x,.5);
  let transmission:vec3f=exp(-coefficients*max(length,0.));
  // Participating-medium contribution is LIT, not an emissive cyan edge color.
  let particles:vec3f=mix(vec3f(.035,.095,.09),vec3f(.23,.155,.07),turbidity);
  return background*transmission+particles*illumination*(vec3f(1.)-transmission);
}
fn shadeWater(p:vec3f,rd:vec3f,behind:vec3f,below:bool) -> vec3f {
  let wave:vec3f=waterWaves(p);
  let clearance:f32=max(map(p),0.);let e:f32=.38;
  let coastGradient:vec2f=vec2f(map(p+vec3f(e,0,0))-map(p-vec3f(e,0,0)),map(p+vec3f(0,0,e))-map(p-vec3f(0,0,e)))/(2.*e);
  let slope:f32=length(coastGradient);let coastNormal:vec2f=coastGradient/max(slope,.001);
  let shoreDistance:f32=clearance/max(slope,.16);
  let shoreMask:f32=exp(-shoreDistance*1.5)*smoothstep(.08,.55,slope);
  let time:f32=u.eye.w*(.65+u.water.y*.65);
  let breakup:f32=.55+.45*noise(p*vec3f(1.7,.2,1.7)+vec3f(time*.08,0,-time*.06));
  let phase:f32=shoreDistance*8.+time*3.+noise(p*.65)*.6;
  let lap:f32=cos(phase)*(.006+u.water.y*.027)*shoreMask;
  let ripples:vec2f=coastNormal*lap;
  var geometric:vec3f=normalize(vec3f(-wave.y,1.,-wave.z));
  var n:vec3f=normalize(vec3f(-wave.y-ripples.x,1.,-wave.z-ripples.y));
  if(below){n=-n;geometric=-geometric;}
  if(dot(n,-rd)<.01){n=geometric;}
  let etaI:f32=select(1.,1.333,below);let etaT:f32=select(1.333,1.,below);
  let fresnel:f32=dielectricFresnel(dot(-rd,n),etaI,etaT);
  let light:vec3f=waterIllumination(p);let turbidity:f32=waterTurbidity(p);
  let reflected:vec3f=reflect(rd,n);let reflectionOrigin:vec3f=waterRayOrigin(p,reflected);
  let reflectedHit:vec2f=traceWaterRay(reflectionOrigin,reflected);
  var reflection:vec3f=sky(reflected);
  if(reflectedHit.y>.5&&u.flags.y>.5){reflection=shadeRock(reflectionOrigin+reflected*reflectedHit.x,reflected,false);}
  else if(reflectedHit.y<0.){reflection=behind;}
  if(below){reflection=waterSegment(reflection,select(0.,max(reflectedHit.x,0.),reflectedHit.y>=0.),turbidity,light);}
  var transmission:vec3f=behind;
  let refracted:vec3f=refract(rd,n,etaI/etaT);
  if(dot(refracted,refracted)>.000001&&fresnel<.9999){
    let origin:vec3f=waterRayOrigin(p,refracted);let hit:vec2f=traceWaterRay(origin,refracted);
    var distance:f32=0.;
    if(hit.y>.5&&u.flags.y>.5){
      let bed:vec3f=origin+refracted*hit.x;
      transmission=shadeRock(bed,refracted,false);distance=hit.x+length(origin-p);
    }else if(hit.y==0.){transmission=select(behind,sky(refracted),below);distance=hit.x;}
    // On an uncertain ray we retain the known background and zero extra
    // thickness. In particular, there is no invented 8-meter fallback layer.
    if(!below){transmission=waterSegment(transmission,distance,turbidity,light);}
  }
  var col:vec3f=mix(transmission,reflection,fresnel);
  let halfway:vec3f=safeNormalize(sunDirection()-rd);
  let roughness:f32=.13+u.water.y*.05;
  let spec:f32=specularGGX(max(dot(n,-rd),.001),max(dot(n,sunDirection()),0.),max(dot(n,halfway),0.),max(dot(-rd,halfway),0.),roughness,.0204);
  if(!below){col+=vec3f(4.6,4.25,3.7)*spec*max(dot(n,sunDirection()),0.)*softShadow(p+vec3f(0,.02,0),sunDirection());}
  if(!below){
    let crest:f32=smoothstep(.4,.88,sin(phase)+breakup*.45);
    let foam:f32=clamp(crest*shoreMask*breakup*u.waterOptics.y*(.12+u.water.y*.45),0.,.45);
    col=mix(col,vec3f(.55,.56,.52)*light,foam);
  }
  return col;
}
fn renderPixel(frag:vec2f) -> vec4f {
  let uv:vec2f=(frag/u.viewport.xy)*2.-1.;
  let rd:vec3f=normalize(u.forward.xyz+u.right.xyz*uv.x*u.forward.w*u.right.w-u.up.xyz*uv.y*u.right.w);
  let ro:vec3f=u.eye.xyz;
  var col:vec3f=sky(rd);
  var t:f32=10000.;
  if(u.flags.y>.5){t=trace(ro,rd,320);}
  var groundT:f32=10000.;
  if(abs(rd.y)>.00001){
    let planeT:f32=(-2.7-ro.y)/rd.y;
    if(planeT>0.){groundT=planeT;}
  }
  if(groundT>0.&&groundT<t){
    let p:vec3f=ro+rd*groundT;
    let fog:f32=1.-exp(-groundT*.004);
    var ground:vec3f=vec3f(.075,.095,.105);
    let grid:vec2f=abs(fract(p.xz/8.-.5)-.5)*8.;
    let line:f32=1.-smoothstep(.018,.06,min(grid.x,grid.y));
    ground*=1.-line*.15*u.water.w;
    let sh:f32=softShadow(p+vec3f(0,.08,0),sunDirection());
    let tileAO:f32=1.-exp(-max(boxSdf(p-vec3f(0,-3,0),vec3f(44,5,44)),0.)*.16)*.4;
    ground*=mix(.47,1.,sh)*(1.-tileAO*.19);
    col=mix(ground,sky(rd),fog);
  }
  if(t<9999.){
    let p:vec3f=ro+rd*t;
    col=shadeRock(p,rd,true);
    col=mix(col,sky(rd),1.-exp(-t*.0007));
  }
  var wetTravel:f32=0.;
  let inFootprint:bool=max(abs(ro.x),abs(ro.z))<45.55;
  let below:bool=u.water.z>.5&&inFootprint&&ro.y<u.water.x+waterWaves(ro).x-.008&&map(ro)>0.;
  if(below){wetTravel=max(0.,min(min(t,groundT),boxHit(ro,rd).y));}
  let opaqueColor:vec3f=col;
  var cameraLight:vec3f=vec3f(0.);var cameraTurbidity:f32=0.;
  if(below&&u.viewport.w<1.5){
    cameraLight=waterIllumination(ro);cameraTurbidity=waterTurbidity(ro);
    col=waterSegment(opaqueColor,wetTravel,cameraTurbidity,cameraLight);
  }
  if(u.water.z>.5&&u.viewport.w<1.5){
    let opaqueT:f32=min(t,groundT);
    let wt:f32=waterSurfaceHit(ro,rd,opaqueT);
    let wp:vec3f=ro+rd*wt;
    let wave:vec3f=waterWaves(wp);
    let upward:vec3f=normalize(vec3f(-wave.y,1.,-wave.z));
    var correctSide:bool=dot(upward,rd)<0.;
    if(below){correctSide=dot(upward,rd)>0.;}
    if(wt>0.&&wt<opaqueT&&max(abs(wp.x),abs(wp.z))<45.55&&correctSide){
      let clearance:f32=map(wp);
      let footprint:f32=max(.004,wt*.828427/max(u.viewport.y,1.));
      // Smooth sub-pixel shoreline coverage, with opaque geometry ALWAYS in
      // front when its first hit precedes water. No hard colored cutoff ring.
      let coverage:f32=smoothstep(0.,max(.025,footprint*1.2),clearance)*smoothstep(0.,max(.025,footprint*1.2),opaqueT-wt);
      if(coverage>0.){
        var waterColor:vec3f=shadeWater(wp,rd,opaqueColor,below);
        if(below){waterColor=waterSegment(waterColor,wt,cameraTurbidity,cameraLight);}
        // Attenuate each path BEFORE coverage blending. Replacing both path
        // lengths with min(water,rock) creates an underwater edge discontinuity.
        col=mix(col,waterColor,coverage);
      }
    }
  }
  // Filmic exposure, not a photographic texture or a pre-rendered backdrop.
  col=vec3f(1.)-exp(-max(col,vec3f(0.))*u.viewport.z*1.30);
  col=vec3f(linearToSRGB(col.r),linearToSRGB(col.g),linearToSRGB(col.b));
  col*=1.-dot(uv*vec2f(.65,1.),uv*vec2f(.65,1.))*.045;
  col+=(hash3(vec3i(vec2i(frag),i32(u.eye.w*40.)))-.5)/255.;
  return vec4f(col,1.);
}
