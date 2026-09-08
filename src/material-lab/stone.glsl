// Analytic 3D material field. The only texture in this lab is the color CLUT.
float hashStone(ivec3 p, uint seed) {
  uint a=(uint(p.x)*73856093u)^(uint(p.y)*19349663u)^(uint(p.z)*83492791u)^(seed*2654435761u);
  a=(a^(a>>13u))*1274126177u;
  return float(a^(a>>16u))/4294967295.;
}
float noiseStone(vec3 p) {
  ivec3 i=ivec3(floor(p));vec3 f=fract(p);vec3 w=f*f*(3.-2.*f);uint seed=uint(uShape.x);
  return mix(mix(mix(hashStone(i,seed),hashStone(i+ivec3(1,0,0),seed),w.x),mix(hashStone(i+ivec3(0,1,0),seed),hashStone(i+ivec3(1,1,0),seed),w.x),w.y),
    mix(mix(hashStone(i+ivec3(0,0,1),seed),hashStone(i+ivec3(1,0,1),seed),w.x),mix(hashStone(i+ivec3(0,1,1),seed),hashStone(i+ivec3(1,1,1),seed),w.x),w.y),w.z)*2.-1.;
}
float stoneWeight(float size) {return 1.-smoothstep(.22,.8,uEye.w/max(size,.0000001));}
float smoothMaxStone(float a,float b,float k) {float h=clamp(.5+.5*(a-b)/k,0.,1.);return mix(b,a,h)+k*h*(1.-h);}
float baseStone(vec3 p) {
  float d=(length(p/vec3(.30,.245,.27))-1.)*.245;
  float k=.004+.025*(1.-uShape.z),cut=.286-uShape.z*.082;
  d=smoothMaxStone(d,dot(p,normalize(vec3(.78,.2,.59)))-cut,k);
  d=smoothMaxStone(d,dot(p,normalize(vec3(-.8,.46,.39)))-cut,k);
  d=smoothMaxStone(d,dot(p,normalize(vec3(.1,.82,-.56)))-cut,k);
  d=smoothMaxStone(d,dot(p,normalize(vec3(-.2,-.68,-.7)))-cut,k);
  d+=uShape.y*(.016*noiseStone(p*4.)+.007*noiseStone(p*11.+vec3(13,7,3)));
  return max(d,-p.y-.235);
}
float detailStone(vec3 p,bool enabled) {
  float base=baseStone(p);if(!enabled)return base;
  vec3 q=vec3(p.x*.8+p.z*.6,p.y,-p.x*.6+p.z*.8);
  float d=base;
  d+=uMeso.x*stoneWeight(.018)*(.6*noiseStone(q*22.)+.28*noiseStone(q*47.+vec3(11,3,5))+.12*abs(noiseStone(q*93.+vec3(3,19,7))));
  if(uMeso.y>0.) {
    float level=(p.y*cos(uMeso.w)+p.x*sin(uMeso.w))/uMeso.z+noiseStone(p*8.)*.32+noiseStone(p*21.+vec3(3,7,1))*.075;
    float phase=level*6.2831853;
    float mask=.12+.88*smoothstep(-.3,.5,noiseStone(p*13.+vec3(11,2,7)));
    d+=uMeso.y*stoneWeight(uMeso.z*.5)*mask*(.55*sin(phase)+.18*sin(phase*2.));
  }
  if(uMicro.y>0.) {
    float f=1./uMicro.z;
    float w1=stoneWeight(uMicro.z),w2=stoneWeight(uMicro.z*.5);
    if(w1>0.)d+=uMicro.y*w1*noiseStone(q*f+vec3(4,7,1));
    if(w2>0.)d+=uMicro.y*.35*w2*noiseStone(q*f*2.+vec3(17,3,13));
  }
  if(uFracture.x>0. && d<uFracture.y) {
    float width=uFracture.y*stoneWeight(uFracture.y*4.);
    float a=(dot(p,vec3(.8,0.,.6))+noiseStone(p*13.)*.003)/uFracture.z;
    float b=(dot(p,vec3(-.28,.92,.27))+noiseStone(p*11.+vec3(3,8,5))*.003)/(uFracture.z*1.37);
    float line=min(abs(fract(a+.5)-.5)*uFracture.z,abs(fract(b+.5)-.5)*uFracture.z*1.37);
    float gap=noiseStone(p*15.+vec3(7,3,11))*.003-.0008;
    float slot=max(max(line-width,gap),-base-uFracture.x);
    d=max(d,-slot);
  }
  float size=uMicro.x,rScale=stoneWeight(size*.3);
  if(uFracture.w>0. && rScale>0. && d<size*.21) {
    ivec3 cell=ivec3(floor(p/size));float pores=10.;uint seed=uint(uShape.x);
    // Eight neighboring lattice vertices, bounded jitter; actual spherical cuts.
    for(int z=0;z<2;z++)for(int y=0;y<2;y++)for(int x=0;x<2;x++) {
      ivec3 id=cell+ivec3(x,y,z);
      if(hashStone(id,seed+31u)>uFracture.w)continue;
      float random=hashStone(id,seed+53u);
      vec3 jitter=vec3(hashStone(id,seed+71u),hashStone(id,seed+97u),hashStone(id,seed+113u))-.5;
      vec3 center=(vec3(id)+jitter*.12)*size;
      pores=min(pores,length(p-center)-size*(.10+.11*random)*rScale);
    }
    d=max(d,-pores);
  }
  return max(d,-p.y-.235);
}
