// Value noise with an analytic derivative. One eight-corner evaluation supplies
// both relief and normals, instead of six extra noise samples per bump layer.
fn noiseGradient(p:vec3f) -> vec4f {
  let i:vec3i=vec3i(floor(p));let f:vec3f=fract(p);
  let w:vec3f=f*f*(3.-2.*f);let dw:vec3f=6.*f*(1.-f);
  let a:f32=hash3(i);let b:f32=hash3(i+vec3i(1,0,0));
  let c:f32=hash3(i+vec3i(0,1,0));let d:f32=hash3(i+vec3i(1,1,0));
  let e:f32=hash3(i+vec3i(0,0,1));let f1:f32=hash3(i+vec3i(1,0,1));
  let g:f32=hash3(i+vec3i(0,1,1));let h:f32=hash3(i+vec3i(1,1,1));
  let k1:f32=b-a;let k2:f32=c-a;let k3:f32=e-a;
  let k4:f32=a-b-c+d;let k5:f32=a-c-e+g;let k6:f32=a-b-e+f1;
  let k7:f32=-a+b+c-d+e-f1-g+h;
  let value:f32=a+k1*w.x+k2*w.y+k3*w.z+k4*w.x*w.y+k5*w.y*w.z+k6*w.z*w.x+k7*w.x*w.y*w.z;
  let gradient:vec3f=dw*vec3f(k1+k4*w.y+k6*w.z+k7*w.y*w.z,k2+k4*w.x+k5*w.z+k7*w.x*w.z,k3+k5*w.y+k6*w.x+k7*w.x*w.y);
  return vec4f(value*2.-1.,gradient*2.);
}
fn detailVisibility(p:vec3f,frequency:f32) -> f32 {
  let pixelWidth:f32=length(p-u.eye.xyz)*.828427/max(u.viewport.y,1.);
  return 1.-smoothstep(.25,.8,pixelWidth*frequency);
}
fn rockRelief(p:vec3f) -> vec4f {
  let base:vec4f=noiseGradient(p*1.8+vec3f(u.flags.z*.001,3.1,7.7));
  let bedding:vec4f=noiseGradient(p*vec3f(.85,15.,.85)+vec3f(2.3));
  let beddingWeight:f32=detailVisibility(p,15.);
  var height:f32=base.x*.045+bedding.x*.009*beddingWeight;
  var gradient:vec3f=base.yzw*1.8*.045+bedding.yzw*vec3f(.85,15.,.85)*.009*beddingWeight;
  let pitT:f32=clamp((base.x-.25)/.4,0.,1.);
  height-=pitT*pitT*(3.-2.*pitT)*.045;
  gradient-=base.yzw*1.8*(6.*pitT*(1.-pitT)/.4)*.045;
  let grainWeight:f32=detailVisibility(p,11.5);
  if(grainWeight>.01){
    let grain:vec4f=noiseGradient(p*11.5+vec3f(3.9,7.2,1.4));
    height+=grain.x*.009*grainWeight;
    gradient+=grain.yzw*11.5*.009*grainWeight;
  }
  let microWeight:f32=detailVisibility(p,37.);
  if(microWeight>.01){
    let micro:vec4f=noiseGradient(p*37.+vec3f(8.1,2.3,4.6));
    height+=micro.x*.0024*microWeight;
    gradient+=micro.yzw*37.*.0024*microWeight;
  }
  let joints:vec4f=noiseGradient(p*vec3f(.43,.11,.43)+vec3f(6.7,1.2,4.1));
  let seam:f32=exp(-abs(joints.x-.08)*40.)*detailVisibility(p,8.);
  height-=seam*.045;
  gradient+=joints.yzw*vec3f(.43,.11,.43)*sign(joints.x-.08)*40.*seam*.045;
  return vec4f(height,gradient);
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
fn rockColor(p:vec3f,n:vec3f) -> vec3f {
  let broad:f32=noise(p*.053+vec3f(u.flags.z*.01,0,0));
  let layer:f32=p.y+noise(p*vec3f(.035,.02,.035))*1.7+sin(p.z*.022)*1.6;
  let bands:f32=sin(layer*.92)*.5+.5;
  let fine:f32=fbm(p*vec3f(1.4,2.2,1.4));
  var col:vec3f=mix(vec3f(.38,.113,.043),vec3f(.53,.24,.091),bands*.65+.15);
  let pale:f32=smoothstep(.65,.92,sin(layer*.46+.9));
  col=mix(col,vec3f(.61,.355,.17),pale*.4);
  let seam:f32=1.-smoothstep(.025,.16,abs(sin(layer*2.8+noise(p*.42)*.3)));
  col*=1.-seam*.14;
  col*=1.+broad*.20+fine*.13;
  let fracture:f32=smoothstep(.48,.73,abs(noise(p*vec3f(.37,.015,.37)+vec3f(3.4))));
  col*=1.-fracture*.27*(1.-max(n.y,0.));
  col=mix(col,vec3f(.50,.255,.112)*(1.+fine*.07),smoothstep(.64,.94,n.y)*.42);
  if(u.flags.w>0.){
    let pores:f32=smoothstep(.25,.65,noise(p*1.8+vec3f(u.flags.z*.001,3.1,7.7)));
    let joint:f32=exp(-abs(noise(p*vec3f(.43,.11,.43)+vec3f(6.7,1.2,4.1))-.08)*40.)*detailVisibility(p,8.);
    col*=1.-u.flags.w*(pores*.16+joint*.27);
    let grainWeight:f32=detailVisibility(p,11.5);
    if(grainWeight>.01){
      let grain:f32=noise(p*11.5+vec3f(3.9,7.2,1.4));
      col*=1.+grain*.16*u.flags.w*grainWeight;
      let minerals:f32=smoothstep(.5,.82,grain)*grainWeight;
      col=mix(col,vec3f(.71,.61,.43),minerals*.25*u.flags.w);
    }
    let chips:f32=smoothstep(.38,.78,noise(p*vec3f(2.4,7.3,2.4)+vec3f(6.1)));
    col=mix(col,col*vec3f(.79,.82,.83),chips*.26*u.flags.w);
  }
  let wet:f32=(1.-smoothstep(u.water.x-.15,u.water.x+.85,p.y))*u.water.z;
  col*=1.-wet*.32;
  return col;
}
fn shadeRock(p:vec3f,rd:vec3f,details:bool) -> vec3f {
  var n:vec3f=surfaceNormal(p);
  if(dot(n,rd)>0.){n=-n;}
  let geometric:vec3f=n;
  if(details&&u.viewport.w<.5&&u.flags.w>0.){
    let relief:vec4f=rockRelief(p);
    let tangentGradient:vec3f=relief.yzw-n*dot(n,relief.yzw);
    n=safeNormalize(n-tangentGradient*u.flags.w);
  }
  let light:vec3f=sunDirection();
  let diffuse:f32=max(dot(n,light),0.);
  var shadow:f32=1.;var ao:f32=1.;
  if(details){shadow=softShadow(p+geometric*.27,light);ao=ambientOcclusion(p,geometric);}
  var albedo:vec3f=rockColor(p,geometric);
  if(u.viewport.w>.5&&u.viewport.w<1.5){albedo=vec3f(.53,.51,.45);}
  if(u.viewport.w>1.5){
    let state:vec4f=volume(p+geometric*.45);
    albedo=mix(vec3f(.28,.31,.31),vec3f(.12,.54,.67),clamp(state.y*9.,0.,1.));
    albedo=mix(albedo,vec3f(.9,.32,.12),clamp(max(state.w,0.)*3.,0.,.9));
    albedo=mix(albedo,vec3f(.65,.72,.31),clamp(max(-state.w,0.)*3.,0.,.8));
  }
  let ambient:vec3f=vec3f(.27,.34,.42)*(.50+.50*max(n.y,0.))*ao;
  let bounce:vec3f=vec3f(.20,.11,.06)*(1.-max(n.y,0.))*(.4+.6*ao);
  var col:vec3f=albedo*(ambient+bounce+vec3f(1.45,1.24,1.02)*diffuse*shadow);
  let rim:f32=pow(1.-clamp(dot(-rd,n),0.,1.),4.);
  col+=vec3f(.08,.064,.044)*rim*diffuse*shadow;
  if(u.brush.w>0.&&details){
    let dist:f32=length(p-u.brush.xyz);
    let ring:f32=1.-smoothstep(.035,.16,abs(dist-u.brush.w));
    let tint:vec3f=select(vec3f(.45,.79,.62),vec3f(1.,.60,.25),u.brushParams.z>1.5);
    col=mix(col,tint,ring*.9);
    col=mix(col,tint,(1.-smoothstep(0.,u.brush.w,dist))*.1);
  }
  return col;
}
fn shadeWater(p:vec3f,rd:vec3f,depth:f32) -> vec3f {
  let time:f32=u.eye.w*(.25+u.water.y*1.4);
  let a:f32=p.x*1.65+p.z*1.12+time*1.5;
  let b:f32=p.x*-.74+p.z*2.3-time*.85;
  let wave:f32=.013+u.water.y*.045;
  let n:vec3f=normalize(vec3f((sin(a)+sin(b)*.5)*wave,1.,(cos(a*.87)+cos(b)*.65)*wave));
  let refl:vec3f=reflect(rd,n);
  var reflection:vec3f=sky(refl);
  let reflectedT:f32=trace(p+n*.18,refl,60);
  if(reflectedT<180.&&u.flags.y>.5){reflection=shadeRock(p+n*.18+refl*reflectedT,refl,false)*.85;}
  let fresnel:f32=.045+.955*pow(1.-clamp(dot(-rd,n),0.,1.),5.);
  let absorption:vec3f=exp(-vec3f(.65,.24,.19)*max(depth,.05));
  let bed:vec3f=vec3f(.16,.22,.15)*absorption;
  let body:vec3f=vec3f(.018,.115,.12)*(1.-absorption);
  var col:vec3f=mix(bed+body,reflection,clamp(fresnel,.06,.85));
  let halfVector:vec3f=normalize(sunDirection()-rd);
  let spec:f32=pow(max(dot(n,halfVector),0.),320.);
  col+=vec3f(1.2,.95,.62)*spec*.6*softShadow(p+n*.3,sunDirection());
  let shore:f32=exp(-depth*3.)*(.5+.5*noise(p*2.2+vec3f(time*.2)));
  col+=vec3f(.26,.32,.28)*shore*.26;
  return col;
}
fn renderPixel(frag:vec2f) -> vec4f {
  let uv:vec2f=(frag/u.viewport.xy)*2.-1.;
  let rd:vec3f=normalize(u.forward.xyz+u.right.xyz*uv.x*u.forward.w*u.right.w-u.up.xyz*uv.y*u.right.w);
  let ro:vec3f=u.eye.xyz;
  var col:vec3f=sky(rd);
  var t:f32=10000.;
  if(u.flags.y>.5){t=trace(ro,rd,220);}
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
  if(u.water.z>.5&&u.viewport.w<1.5&&abs(rd.y)>.00001){
    let wt:f32=(u.water.x-ro.y)/rd.y;
    let wp:vec3f=ro+rd*wt;
    if(wt>0.&&wt<t&&wt<groundT&&max(abs(wp.x),abs(wp.z))<45.55&&map(wp)>.025){
      let depth:f32=min(8.,max(.02,(t-wt)*abs(rd.y)));
      col=shadeWater(wp,rd,depth);
      col=mix(col,sky(rd),1.-exp(-wt*.0007));
    }
  }
  // Filmic exposure, not a photographic texture or a pre-rendered backdrop.
  col=vec3f(1.)-exp(-max(col,vec3f(0.))*u.viewport.z*1.30);
  col=pow(col,vec3f(1./2.2));
  col*=1.-dot(uv*vec2f(.65,1.),uv*vec2f(.65,1.))*.045;
  col+=(hash3(vec3i(vec2i(frag),i32(u.eye.w*40.)))-.5)/255.;
  return vec4f(col,1.);
}
