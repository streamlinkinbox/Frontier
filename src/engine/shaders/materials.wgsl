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
// Research/design notes: public/research/materials-and-water.md. Color is LINEAR RGB.
// Millimeter grains are averaged when unresolved, not enlarged into white blobs.
fn detailVisibility(p:vec3f,frequency:f32) -> f32 {
  let width:f32=length(p-u.eye.xyz)*.828427/max(u.viewport.y,1.);
  return 1.-smoothstep(.15,.75,width*frequency);
}
fn materialFootprint(p:vec3f,n:vec3f) -> f32 {
  let view:vec3f=safeNormalize(u.eye.xyz-p);
  return max(.00001,length(p-u.eye.xyz)*.828427/max(u.viewport.y,1.)/max(abs(dot(n,view)),.25));
}
fn featureWeight(footprint:f32,size:f32) -> f32 {return 1.-smoothstep(.15,.75,footprint/max(size,.00001));}
fn grainPoint(p:vec3f) -> vec3f {return vec3f(p.x*.8+p.z*.6,p.y,-p.x*.6+p.z*.8);}
fn grainGradient(g:vec3f) -> vec3f {return vec3f(g.x*.8-g.z*.6,g.y,g.x*.6+g.z*.8);}
// Centimeter-scale rock structure above the existing millimeter grain model.
// Matched CPU reference: surfaceDetail.ts. No color/noise animation or SDF edit.
fn layeredRock(p:vec3f,footprint:f32) -> vec4f {
  if(u.rockDetail.x==0.&&(u.rockLayers.y==0.||u.material.x>1.5)){return vec4f(0.);}
  let warp:vec4f=noiseGradient(p*.115+vec3f(u.flags.z*.002,7.9,3.1));
  let strength:f32=.35+.4*u.rockLayers.z;let c:vec3f=vec3f(.7,.17,.45)*strength;
  let q:vec3f=grainPoint(p)+warp.x*c;
  var height:f32=0.;var gradient:vec3f=vec3f(0.);var weight:f32=.5;var frequency:f32=1.;var total:f32=0.;
  for(var i:i32=0;i<5;i++){
    if(i>=i32(u.rockDetail.z)){break;}
    total+=weight;
    let f:f32=frequency/u.rockDetail.y;let w:f32=weight*featureWeight(footprint,u.rockDetail.y/frequency);
    if(w>0.){
      let point:vec3f=q*vec3f(f,f*1.3,f)+vec3f(f32(i)*4.31,-f32(i)*7.17,f32(i)*11.63);
      let value:vec4f=noiseGradient(point);let absolute:f32=sqrt(value.x*value.x+.015);
      let shaped:f32=value.x*(1.-u.rockDetail.w)+(.55-absolute)*1.5*u.rockDetail.w;
      let derivative:f32=(1.-u.rockDetail.w)-1.5*u.rockDetail.w*value.x/absolute;
      height+=shaped*w;gradient+=value.yzw*vec3f(f,f*1.3,f)*derivative*w;
    }
    frequency*=2.07;weight*=.5;
  }
  let amplitude:f32=u.rockDetail.x/max(total,.5);
  height*=amplitude;gradient*=amplitude;
  var g:vec3f=grainGradient(gradient)+warp.yzw*.115*dot(gradient,c);
  if(u.material.x<1.5&&u.rockLayers.y>0.){
    let k:f32=6.2831853/u.rockLayers.x;let w:f32=featureWeight(footprint,u.rockLayers.x*.35);
    let band:vec4f=noiseGradient(p*vec3f(.65,.12,.65)+vec3f(13.1,u.flags.z*.001,19.3));
    let level:f32=p.y+p.x*.025+p.z*.017+warp.x*u.rockLayers.z*.8;
    let gp:vec3f=(vec3f(.025,1.,.017)+warp.yzw*.115*u.rockLayers.z*.8)*k+band.yzw*vec3f(.65,.12,.65)*u.rockLayers.z*.7;
    let phase:f32=level*k+band.x*u.rockLayers.z*.7;let second:f32=phase*2.07+band.x*.4;
    let wave:f32=sin(phase)+.25*sin(second);let t:f32=clamp((wave+.25)/1.1,0.,1.);
    let a:f32=u.rockLayers.y*w;let derivativeScale:f32=6.*t*(1.-t)/1.1*a;
    let d:f32=cos(phase)+.25*2.07*cos(second);let extra:f32=.25*cos(second)*.4;
    height+=(t*t*(3.-2.*t)-.5)*a;
    g+=(gp*d+band.yzw*vec3f(.65,.12,.65)*extra)*derivativeScale;
  }
  return vec4f(height,g);
}
fn materialMacroColor(p:vec3f,n:vec3f,displacement:f32) -> vec3f {
  let q:vec3f=p/u.materialShape.x;
  let broad:f32=noise(q*vec3f(.31,.17,.31)+vec3f(u.flags.z*.003,0,0));
  let fine:f32=noise(q*vec3f(1.7,1.3,1.7)+vec3f(0,4.7,0));
  let layer:f32=sin(strataCoordinate(q)*1.05);
  let stain:f32=smoothstep(.05,.7,noise(q*vec3f(.67,.18,.67)+vec3f(3.1,0,0)));
  let weather:f32=u.materialShape.w;
  var factor:f32=1.;var oxide:vec3f=vec3f(1.);var bedding:vec3f=vec3f(1.);
  if(u.material.x<.5){
    factor=1.+broad*.12;
    let bed:f32=smoothstep(-.7,.7,layer);
    bedding=mix(vec3f(1.),mix(vec3f(.64,.46,.27),vec3f(1.18,1.22,1.28),bed),u.materialShape.y);
    oxide=vec3f(1.+stain*weather*.08,1.-stain*weather*.16,1.-stain*weather*.3);
  }else if(u.material.x<1.5){
    factor=1.+broad*.12+fine*.035+layer*u.materialShape.y*.13;
    oxide=vec3f(1.,1.-stain*weather*.045,1.-stain*weather*.09);
  }else if(u.material.x<2.5){
    factor=1.+broad*.09+fine*.025;
    oxide=vec3f(1.+stain*weather*.1,1.-stain*weather*.04,1.-stain*weather*.09);
  }else{
    factor=1.+broad*.1+fine*.045;
    oxide=vec3f(1.+stain*weather*.4,1.+stain*weather*.1,1.-stain*weather*.07);
  }
  let fresh:f32=clamp(max(displacement,0.)/.8,0.,1.);
  let deposited:f32=clamp(max(-displacement,0.)/.65,0.,1.)*smoothstep(.2,.8,n.y);
  return clamp(mix(u.baseColor.rgb*factor*bedding*mix(oxide,vec3f(1.),fresh),u.baseColor.rgb*1.08,deposited*.4),vec3f(.008),vec3f(.85));
}
// Jittered cellular grains for interlocking minerals, not a second color tint.
fn crystalCell(q:vec3f) -> vec4f {
  let base:vec3i=vec3i(floor(q-.5));var distance:f32=100.;var result:vec4f=vec4f(0.);
  for(var z:i32=0;z<2;z++){for(var y:i32=0;y<2;y++){for(var x:i32=0;x<2;x++){
    let cell:vec3i=base+vec3i(x,y,z);let random:f32=hash3(cell);
    let point:vec3f=vec3f(cell)+.2+fract(vec3f(random*17.31,random*37.97,random*67.13))*.6;
    let offset:vec3f=q-point;let squared:f32=dot(offset,offset);
    if(squared<distance){distance=squared;result=vec4f(offset,hash3(cell+vec3i(7,31,13)));}
  }}}
  return result;
}
fn materialPoreSize() -> f32 {
  if(u.material.x<.5){return max(u.material.z*4.,.006*u.materialShape.x);}
  if(u.material.x<1.5){return .012*u.materialShape.x;}
  if(u.material.x<2.5){return max(u.material.z*2.,.004*u.materialShape.x);}
  return .014*u.materialShape.x;
}
// rgb = unlit material albedo; a = perceptual roughness (not opacity).
fn sampleMaterial(p:vec3f,n:vec3f,footprint:f32) -> vec4f {
  let state:vec4f=volume(p+n*u.dims.w*.2);
  var color:vec3f=materialMacroColor(p,n,state.w);
  var roughness:f32=u.material.y;
  let grainWeight:f32=featureWeight(footprint,u.material.z);
  let q:vec3f=grainPoint(p)+vec3f(u.flags.z*.001,3.1,7.7);
  if(grainWeight>.01){
    if(u.material.x>1.5&&u.material.x<2.5){
      let crystal:vec4f=crystalCell(q/u.material.z);
      var mineral:vec3f=vec3f(1.1,.98,.91);var polish:f32=.025;
      if(crystal.w<.15){mineral=vec3f(.14,.15,.16);polish=-.2;}
      else if(crystal.w<.48){mineral=vec3f(1.27,1.27,1.27);polish=-.1;}
      color*=mix(vec3f(1.),mineral,grainWeight);
      roughness+=polish*grainWeight;
    }else{
      let grain:f32=noise(q/u.material.z);
      let contrast:f32=select(.10,.045,u.material.x>2.5);
      color*=1.+grain*contrast*grainWeight;
      roughness+=grain*.045*grainWeight;
    }
  }
  let poreSize:f32=materialPoreSize();let poreWeight:f32=featureWeight(footprint,poreSize);
  if(poreWeight>.01&&u.materialShape.z>0.){
    let pored:f32=noise(q/poreSize)*.5+.5;
    let threshold:f32=mix(.92,.25,u.materialShape.z);
    let pit:f32=smoothstep(threshold,min(.99,threshold+.18),pored);
    color*=1.-pit*poreWeight*select(.19,.32,u.material.x>2.5);
    roughness+=pit*.065*poreWeight;
  }
  // Unresolved microgeometry belongs in the lobe, not in oversized freckles.
  roughness=sqrt(roughness*roughness+(1.-grainWeight)*.008+(1.-poreWeight)*u.materialShape.z*.025);
  return vec4f(clamp(color,vec3f(.008),vec3f(.85)),clamp(roughness,.12,1.));
}
fn materialNormal(p:vec3f,n:vec3f,footprint:f32,structure:vec4f) -> vec3f {
  if(u.flags.w<=0.){return n;}
  let q:vec3f=grainPoint(p)+vec3f(u.flags.z*.001,3.1,7.7);
  let poreSize:f32=materialPoreSize();let poreWeight:f32=featureWeight(footprint,poreSize);
  var gradient:vec3f=structure.yzw;
  if(poreWeight>.01&&u.materialShape.z>0.){
    let pore:vec4f=noiseGradient(q/poreSize);
    let threshold:f32=mix(.92,.25,u.materialShape.z);
    let t:f32=clamp((pore.x*.5+.5-threshold)/.18,0.,1.);
    gradient-=grainGradient(pore.yzw)*(.5/poreSize)*(6.*t*(1.-t)/.18)*u.material.w*poreWeight;
  }
  let grainWeight:f32=featureWeight(footprint,u.material.z);
  if(grainWeight>.01){
    if(u.material.x>1.5&&u.material.x<2.5){
      let cell:vec4f=crystalCell(q/u.material.z);
      let facet:vec3f=vec3f(sin(cell.w*31.),cos(cell.w*47.),sin(cell.w*73.));
      gradient+=facet*min(.24,u.material.w/max(u.material.z,.00001))*grainWeight;
    }else{
      let grain:vec4f=noiseGradient(q/u.material.z);
      gradient+=grainGradient(grain.yzw)/u.material.z*min(u.material.w*.15,u.material.z*.12)*grainWeight;
    }
  }
  let mesoSize:f32=.045*u.materialShape.x;let mesoWeight:f32=featureWeight(footprint,mesoSize);
  if(mesoWeight>.01){
    let meso:vec4f=noiseGradient(q/mesoSize);
    gradient+=grainGradient(meso.yzw)/mesoSize*u.material.w*.25*mesoWeight;
  }
  gradient-=n*dot(n,gradient);
  gradient*=u.flags.w/max(1.,length(gradient)/1.2);
  return safeNormalize(n-gradient);
}
fn dielectricF0(ior:f32) -> f32 {let f:f32=(ior-1.)/(ior+1.);return f*f;}
fn fresnelSchlick(c:f32,f0:f32) -> f32 {return f0+(1.-f0)*pow(1.-clamp(c,0.,1.),5.);}
// GGX / correlated Smith visibility. Roughness is squared only here.
fn specularGGX(noV:f32,noL:f32,noH:f32,voH:f32,roughness:f32,f0:f32) -> f32 {
  let alpha:f32=roughness*roughness;let a2:f32=alpha*alpha;
  let denom:f32=noH*noH*(a2-1.)+1.;let distribution:f32=a2/(3.14159265*denom*denom);
  let visibility:f32=.5/max(.000001,noL*sqrt(noV*noV*(1.-a2)+a2)+noV*sqrt(noL*noL*(1.-a2)+a2));
  return distribution*visibility*fresnelSchlick(voH,f0);
}
fn dielectricFresnel(cosine:f32,etaI:f32,etaT:f32) -> f32 {
  let c:f32=clamp(abs(cosine),0.,1.);let eta:f32=etaI/etaT;
  let sin2:f32=eta*eta*(1.-c*c);if(sin2>=1.){return 1.;}
  let ct:f32=sqrt(max(0.,1.-sin2));
  let rs:f32=(etaI*c-etaT*ct)/max(.00000001,etaI*c+etaT*ct);
  let rp:f32=(etaT*c-etaI*ct)/max(.00000001,etaT*c+etaI*ct);
  return (rs*rs+rp*rp)*.5;
}
fn linearToSRGB(c:f32) -> f32 {if(c<=.0031308){return c*12.92;}return 1.055*pow(c,1./2.4)-.055;}
