// Irregular stratigraphy: ordered jittered boundaries, not sine-wave ribs.
float bedBoundary(int i) {return float(i)+(hashStone(ivec3(i,613,73),uint(uShape.x))-.5)*.48;}
vec2 bedPlate(int i,vec2 uv) {
  float f=1./(uMeso.z*4.2);
  float low=noiseStone(vec3(uv*f,float(i)*2.713+5.));
  float high=noiseStone(vec3(uv*f*2.13+vec2(9,4),float(i)*1.379+11.));
  float spall=clamp((high+.05)/.38,0.,1.);
  float h=(hashStone(ivec3(i,619,97),uint(uShape.x))-.5)*.7+uStructure.x*(.28*low+.36*spall);
  return vec2(h,spall);
}
float beddingStone(vec3 p) {
  float c=cos(uMeso.w),s=sin(uMeso.w);
  float level=(p.y*c+p.x*s)/uMeso.z+noiseStone(p*7.)*.35+noiseStone(p*17.+vec3(3,7,1))*.09;
  vec2 uv=vec2(p.x*c-p.y*s,p.z);
  int layer=int(floor(level));
  if(level<bedBoundary(layer))layer--;else if(level>=bedBoundary(layer+1))layer++;
  float bottom=level-bedBoundary(layer),top=bedBoundary(layer+1)-level;
  vec2 sheet=bedPlate(layer,uv);
  float lowerWidth=.06+hashStone(ivec3(layer,631,101),uint(uShape.x))*.07;
  float upperWidth=.06+hashStone(ivec3(layer+1,631,101),uint(uShape.x))*.07;
  if(bottom<lowerWidth)sheet=mix(bedPlate(layer-1,uv),sheet,clamp((bottom+lowerWidth)/(2.*lowerWidth),0.,1.));
  else if(top<upperWidth)sheet=mix(sheet,bedPlate(layer+1,uv),clamp((-top+upperWidth)/(2.*upperWidth),0.,1.));
  float torn=max(0.,1.-min(bottom,top)/.22)*sheet.y*uStructure.x*.18;
  return uMeso.y*stoneWeight(uMeso.z*.5)*(sheet.x+torn);
}
float maxCrackMouth() {return uFracture.y*stoneWeight(uFracture.y*6.)*(1.+3.8*uStructure.z)*1.2;}
// Finite, surface-following parent/fork segments. All descriptors are uniforms,
// generated once per topology change, never a height/normal texture.
float fractureStone(vec3 p,float substrate) {
  float width=uFracture.y*stoneWeight(uFracture.y*6.);
  if(width<=0.)return 10.;
  float noise=0.;bool noiseReady=false;
  float rockDepth=max(0.,-substrate),mouth=maxCrackMouth(),cut=10.;
  for(int i=0;i<STONE_CRACK_SEGMENTS;i++) {
    if(i>=uCrackCount)break;
    vec4 a=uCrackA[i],b=uCrackB[i],normal=uCrackN[i];
    vec3 delta=b.xyz-a.xyz;float depth=uFracture.x*normal.w;
    float support=depth+uBounds.x+.010;
    // Cheap tight-box rejection BEFORE length/normalization/noise work.
    vec3 bound=abs(p-(a.xyz+b.xyz)*.5)-abs(delta)*.5;
    if(max(bound.x,max(bound.y,bound.z))>support+mouth*1.1)continue;
    float len=length(delta);vec3 dir=delta/len;
    if(!noiseReady){noise=.55*noiseStone(p*80.+vec3(11,7,3))+.20*noiseStone(p*170.+vec3(3,13,7));noiseReady=true;}
    float along=dot(p-a.xyz,dir),t=clamp(along/len,0.,1.);
    vec3 q=p-mix(a.xyz,b.xyz,t);
    float taper=mix(a.w,b.w,t),nominal=width*taper;
    float penetration=clamp(1.-rockDepth/max(depth,.00001),0.,1.);
    float shoulder=clamp(1.-rockDepth/max(depth*.8,width*6.),0.,1.);
    float halfWidth=nominal*(.2+.8*penetration+uStructure.z*(.6+3.2*clamp((.6-abs(noise+.15))*2.,0.,1.))*shoulder*shoulder);
    float across=dot(q,cross(normal.xyz,dir))+noise*nominal*uStructure.z*.45;
    float beyond=along-clamp(along,0.,len);
    float slit=length(vec2(across,beyond))-halfWidth;
    cut=min(cut,max(max(slit,-substrate-depth),abs(dot(q,normal.xyz))-support));
  }
  return cut;
}
float smoothMinStone(float a,float b,float k) {float h=clamp(.5+.5*(b-a)/max(k,.0000001),0.,1.);return mix(b,a,h)-k*h*(1.-h);}
float organicPore(vec3 local,float r,vec3 axes,vec2 angles) {
  float irregularity=uStructure.w;
  if(irregularity<=0.)return length(local)-r;
  float ct=cos(angles.x),st=sin(angles.x),cp=cos(angles.y),sp=sin(angles.y);
  vec3 a=vec3(local.x*ct-local.y*st,local.x*st+local.y*ct,local.z);
  vec3 q=vec3(a.x*cp+a.z*sp,a.y,-a.x*sp+a.z*cp);
  vec3 shape=mix(vec3(1.),axes,irregularity),v=q/shape;
  float minAxis=min(shape.x,min(shape.y,shape.z)),l2=length(v),l4=pow(dot(v*v,v*v),.25);
  float mainShape=(mix(l2,l4,irregularity*.22)-r)*minAxis;
  vec3 lobe=(q-r*irregularity*vec3(.45,.18,-.12))/shape;
  float secondary=(length(lobe)-r*.68)*minAxis;
  float wall=r*irregularity*.055*stoneWeight(r*.9)*sin(v.x/r*6.+.3)*sin(v.y/r*5.+.2)*sin(v.z/r*4.+1.1);
  return smoothMinStone(mainShape,secondary,r*.18*irregularity)+wall;
}
float poresStone(vec3 p) {
  float size=uMicro.x,visible=stoneWeight(size*.3);
  if(visible<=0.)return 10.;
  ivec3 cell=ivec3(floor(p/size));float pores=10.;uint seed=uint(uShape.x);
  for(int z=0;z<2;z++)for(int y=0;y<2;y++)for(int x=0;x<2;x++) {
    ivec3 id=cell+ivec3(x,y,z);
    if(hashStone(id,seed+31u)>uFracture.w)continue;
    float r=size*(.10+.11*hashStone(id,seed+53u))*visible;
    vec3 jitter=vec3(hashStone(id,seed+71u),hashStone(id,seed+97u),hashStone(id,seed+113u))-.5;
    vec3 local=p-(vec3(id)+jitter*.12)*size;
    if(length(local)>r*1.6+.002){pores=min(pores,length(local)-r*1.35);continue;}
    vec3 axes=vec3(.45+.5*hashStone(id,seed+173u),.58+.38*hashStone(id,seed+191u),1.);
    vec2 angles=vec2(hashStone(id,seed+211u),hashStone(id,seed+229u))*6.2831853;
    pores=min(pores,organicPore(local,r,axes,angles));
  }
  return pores;
}
