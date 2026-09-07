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
// Rotate grain coordinates so the noise lattice does not etch axis-aligned squares.
fn grainPoint(p:vec3f) -> vec3f {return vec3f(p.x*.8+p.z*.6,p.y,-p.x*.6+p.z*.8);}
fn grainGradient(g:vec3f) -> vec3f {return vec3f(g.x*.8-g.z*.6,g.y,g.x*.6+g.z*.8);}
fn rockRelief(p:vec3f) -> vec4f {
  let q:vec3f=grainPoint(p)+vec3f(u.flags.z*.001,3.1,7.7);
  let coarse:vec4f=noiseGradient(q*vec3f(2.1,1.25,2.1));
  let broken:vec4f=noiseGradient(q*vec3f(4.3,3.7,4.3)+vec3f(11.2,4.9,2.1));
  let pores:f32=coarse.x*.72+broken.x*.28;
  let poreGradient:vec3f=grainGradient(coarse.yzw*vec3f(2.1,1.25,2.1)*.72+broken.yzw*vec3f(4.3,3.7,4.3)*.28);
  let pit:f32=clamp((pores-.12)/.55,0.,1.);
  var height:f32=coarse.x*.026+broken.x*.009-pit*pit*(3.-2.*pit)*.047;
  var gradient:vec3f=grainGradient(coarse.yzw*vec3f(2.1,1.25,2.1)*.026+broken.yzw*vec3f(4.3,3.7,4.3)*.009)-poreGradient*(6.*pit*(1.-pit)/.55)*.047;
  let beddingWeight:f32=detailVisibility(p,9.5);
  let bedding:vec4f=noiseGradient(vec3f(q.x*.63,strataCoordinate(p)*9.5,q.z*.63));
  height+=bedding.x*.009*beddingWeight;
  gradient+=vec3f(.025,1.,.017)*bedding.z*9.5*.009*beddingWeight;
  let grainWeight:f32=detailVisibility(p,14.);
  if(grainWeight>.01){
    let grain:vec4f=noiseGradient(q*14.+vec3f(1.2,9.4,4.1));
    height+=grain.x*.0045*grainWeight;
    gradient+=grainGradient(grain.yzw)*14.*.0045*grainWeight;
  }
  let microWeight:f32=detailVisibility(p,43.);
  if(microWeight>.01){
    let micro:vec4f=noiseGradient(q*43.+vec3f(8.1,2.3,4.6));
    height+=micro.x*.0014*microWeight;
    gradient+=grainGradient(micro.yzw)*43.*.0014*microWeight;
  }
  let joint:vec4f=noiseGradient(q*vec3f(.41,.13,.41)+vec3f(6.7,1.2,4.1));
  let footprint:f32=length(p-u.eye.xyz)*.828427/max(u.viewport.y,1.);
  let width:f32=max(.045,footprint*.8);
  let seamT:f32=clamp((abs(joint.x-.08)-width*.2)/(width*.8),0.,1.);
  let seam:f32=1.-seamT*seamT*(3.-2.*seamT);
  height-=seam*.016;
  gradient+=grainGradient(joint.yzw*vec3f(.41,.13,.41))*sign(joint.x-.08)*(6.*seamT*(1.-seamT)/(width*.8))*.016;
  return vec4f(height,gradient);
}

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
fn rockWetness(p:vec3f) -> f32 {
  if(u.water.z<.5){return 0.;}
  var level:f32=u.water.x;
  if(abs(p.y-level)<1.2){level+=waterWaves(p).x;}
  let seep:f32=.66+.32*noise(p*vec3f(.65,.08,.65));
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
fn rockColor(p:vec3f,n:vec3f) -> vec3f {
  let q:vec3f=grainPoint(p)+vec3f(u.flags.z*.001,3.1,7.7);
  let broad:f32=noise(p*.053+vec3f(u.flags.z*.01,0,0));
  let layer:f32=strataCoordinate(p);
  let cement:f32=strataStrength(p);
  let fine:f32=fbm(q*vec3f(1.1,1.7,1.1));
  var col:vec3f=mix(vec3f(.34,.115,.047),vec3f(.57,.315,.146),cement*.8);
  let lamina:f32=sin(layer*6.2+noise(q*.71)*1.1);
  col*=1.-(1.-smoothstep(.035,.19,abs(lamina)))*.11*detailVisibility(p,6.2);
  col*=1.+broad*.16+fine*.11;
  // Vertical desert varnish and streaks; fresher eroded faces lose that crust.
  let state:vec4f=volume(p+n*u.dims.w*.22);
  let fresh:f32=clamp(max(state.w,0.)/(u.dims.w*.8),0.,1.);
  let deposited:f32=clamp(max(-state.w,0.)/(u.dims.w*.65),0.,1.);
  let varnish:f32=smoothstep(.05,.6,noise(q*vec3f(.62,.065,.62)+vec3f(2.8)))*(1.-max(n.y,0.))*(1.-fresh);
  col*=vec3f(1.)-vec3f(.20,.23,.22)*varnish;
  col=mix(col,col*vec3f(1.13,1.09,1.03),fresh*.45);
  let sand:f32=clamp(smoothstep(.58,.92,n.y)*.32+deposited*.6,0.,.85);
  col=mix(col,vec3f(.56,.335,.167)*(1.+fine*.1),sand);
  if(u.flags.w>0.){
    let pores:f32=smoothstep(.12,.67,noise(q*vec3f(2.1,1.25,2.1))*.72+noise(q*vec3f(4.3,3.7,4.3)+vec3f(11.2,4.9,2.1))*.28);
    let width:f32=max(.045,length(p-u.eye.xyz)*.828427/max(u.viewport.y,1.)*.8);
    let joint:f32=(1.-smoothstep(width*.2,width,abs(noise(q*vec3f(.41,.13,.41)+vec3f(6.7,1.2,4.1))-.08)))*(.045/width);
    col*=1.-u.flags.w*(pores*.17+joint*.16);
    let grainWeight:f32=detailVisibility(p,14.);
    if(grainWeight>.01){
      let grain:f32=noise(q*14.+vec3f(1.2,9.4,4.1));
      col*=1.+grain*.13*u.flags.w*grainWeight;
      let quartz:f32=smoothstep(.42,.77,grain)*grainWeight*cement;
      col=mix(col,vec3f(.74,.64,.46),quartz*.28*u.flags.w);
    }
  }
  let wet:f32=max(rockWetness(p),clamp(state.y*3.,0.,.8));
  col*=1.-wet*.39;
  // A broken mineral tide mark just above the actively wetted band.
  let tideline:f32=exp(-abs(p.y-u.water.x-.55-noise(q*.7)*.08)*17.)*u.water.z;
  col=mix(col,vec3f(.53,.42,.27),tideline*.18*(1.-wet));
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
  if(u.viewport.w<.5){
    let wet:f32=rockWetness(p);
    let halfVector:vec3f=safeNormalize(light-rd);
    let spec:f32=pow(max(dot(n,halfVector),0.),mix(28.,115.,wet));
    col+=vec3f(1.1,.95,.74)*spec*mix(.012,.16,wet)*shadow;
  }
  if(u.brush.w>0.&&details){
    var v:vec3f=p-u.brush.xyz;
    if(u.brushParams.z>4.5&&u.brushParams.z<5.5){v-=u.planeNormal.xyz*dot(v,u.planeNormal.xyz);}
    let dist:f32=length(v);
    let ring:f32=1.-smoothstep(.035,.16,abs(dist-u.brush.w));
    let tint:vec3f=select(vec3f(.45,.79,.62),vec3f(1.,.60,.25),u.brushParams.z>1.5&&u.brushParams.z<7.5);
    col=mix(col,tint,ring*.9);
    col=mix(col,tint,(1.-smoothstep(0.,u.brush.w,dist))*.1);
  }
  return col;
}
fn shadeWater(p:vec3f,rd:vec3f,depthHint:f32) -> vec3f {
  let wave:vec3f=waterWaves(p);
  let d:f32=max(map(p),0.);
  let e:f32=.38;
  let coastGradient:vec2f=vec2f(map(p+vec3f(e,0,0))-map(p-vec3f(e,0,0)),map(p+vec3f(0,0,e))-map(p-vec3f(0,0,e)))/(2.*e);
  let coastSlope:f32=length(coastGradient);
  let coastNormal:vec2f=coastGradient/max(coastSlope,.001);
  let shoreDistance:f32=d/max(coastSlope,.16);
  let shoreMask:f32=exp(-shoreDistance*1.5)*smoothstep(.08,.55,coastSlope);
  let time:f32=u.eye.w*(.65+u.water.y*.65);
  let breakup:f32=.55+.45*noise(p*vec3f(1.7,.2,1.7)+vec3f(time*.08,0,-time*.06));
  let phase:f32=shoreDistance*8.+time*3.+noise(p*.65)*.6;
  let lap:f32=cos(phase)*(.006+u.water.y*.027)*shoreMask;
  let ripples:vec2f=coastNormal*lap;
  var n:vec3f=normalize(vec3f(-wave.y-ripples.x,1.,-wave.z-ripples.y));
  if(dot(n,rd)>0.){n=-n;}
  let reflected:vec3f=reflect(rd,n);
  var reflection:vec3f=sky(reflected);
  let reflectedT:f32=trace(p+n*.12,reflected,84);
  if(reflectedT<180.&&u.flags.y>.5){
    let reflectedRock:vec3f=shadeRock(p+n*.12+reflected*reflectedT,reflected,false);
    // Distance/roughness filtering reduces the old hard, stair-stepped bands.
    reflection=mix(reflection,reflectedRock,exp(-reflectedT*.013)*(.88-u.water.y*.13));
  }
  let refracted:vec3f=refract(rd,n,.7502);
  let bedT:f32=trace(p-n*.10,refracted,80);
  var opticalDepth:f32=max(.05,depthHint);
  var bedColor:vec3f=vec3f(.16,.105,.047);
  var suspended:f32=volume(p-vec3f(0,.2,0)).z;
  if(bedT<80.&&u.flags.y>.5){
    opticalDepth=max(.05,bedT);
    let bed:vec3f=p-n*.10+refracted*bedT;
    bedColor=shadeRock(bed,refracted,false);
    suspended+=volume(bed+vec3f(0,u.dims.w*.35,0)).z;
    let focus:f32=sin(bed.x*2.7+bed.z*.8+time+noise(bed*.6)*2.)+sin(bed.z*3.1-bed.x*.7-time*.83);
    let caustic:f32=pow(clamp(.5+focus*.25,0.,1.),9.)*exp(-opticalDepth*.5);
    bedColor+=vec3f(.13,.17,.13)*caustic;
  }
  let turbidity:f32=clamp((1.-u.flags.x)*.7+suspended*5.,0.,1.);
  let absorption:vec3f=exp(-mix(vec3f(.42,.115,.075),vec3f(1.35,.8,.4),turbidity)*min(opticalDepth,16.));
  let waterColor:vec3f=mix(vec3f(.012,.088,.082),vec3f(.19,.125,.052),turbidity);
  let transmission:vec3f=bedColor*absorption+waterColor*(vec3f(1.)-absorption);
  let fresnel:f32=.0204+.9796*pow(1.-clamp(dot(-rd,n),0.,1.),5.);
  var col:vec3f=mix(transmission,reflection,clamp(fresnel,.025,.96));
  let halfway:vec3f=safeNormalize(sunDirection()-rd);
  let highlight:f32=pow(max(dot(n,halfway),0.),180.)*.45+pow(max(dot(n,halfway),0.),38.)*.025;
  col+=vec3f(1.45,1.2,.85)*highlight*softShadow(p+n*.22,sunDirection());
  let crest:f32=smoothstep(.32,.86,sin(phase)+breakup*.5);
  let contact:f32=exp(-shoreDistance*8.)*(.3+.7*breakup);
  let foam:f32=clamp((crest*shoreMask*.30+contact*.22)*breakup*(.25+u.water.y*.85),0.,.65);
  col=mix(col,vec3f(.52,.53,.44),foam);
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
  if(u.water.z>.5&&u.viewport.w<1.5&&abs(rd.y)>.00001){
    var wt:f32=(u.water.x-ro.y)/rd.y;
    if(wt>0.&&max(abs((ro+rd*wt).x),abs((ro+rd*wt).z))<46.){
      // Intersect the displaced surface, rather than shading a flat sheet.
      for(var step:i32=0;step<3;step++){
        let point:vec3f=ro+rd*wt;let wave:vec3f=waterWaves(point);
        let derivative:f32=rd.y-dot(wave.yz,rd.xz);
        if(abs(derivative)>.04){wt-=clamp((point.y-u.water.x-wave.x)/derivative,-1.,1.);}
      }
    }
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
