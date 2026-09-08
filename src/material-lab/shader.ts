import field from "./stone.glsl?raw";
export const stoneVertex = `#version 300 es
void main(){float x=float((gl_VertexID<<1)&2);float y=float(gl_VertexID&2);gl_Position=vec4(x*2.-1.,y*2.-1.,0.,1.);}`;
export const stoneFragment = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
uniform vec4 uEye,uRight,uUp,uForward,uShape,uMeso,uFracture,uMicro,uMaterial,uLight,uDisplay,uBounds,uProbe;
uniform sampler2D uPalette;
out vec4 fragColor;
${field}
vec3 sunDirection(){return normalize(vec3(cos(uLight.x)*cos(uLight.y),sin(uLight.y),sin(uLight.x)*cos(uLight.y)));}
vec3 stoneGradient(vec3 p,bool detailed,float epsilon){
  vec2 e=vec2(epsilon,0.);
  return vec3(detailStone(p+e.xyy,detailed)-detailStone(p-e.xyy,detailed),detailStone(p+e.yxy,detailed)-detailStone(p-e.yxy,detailed),detailStone(p+e.yyx,detailed)-detailStone(p-e.yyx,detailed))/(2.*epsilon);
}
float conservativeStep(vec3 p,bool detailed){
  float base=baseStone(p);
  if(!detailed)return base/uBounds.y;
  if(base>uBounds.x+.001)return (base-uBounds.x)/uBounds.y;
  return detailStone(p,true)/uBounds.z;
}
vec3 traceStone(vec3 ro,vec3 rd,bool detailed,float limit){
  float b=dot(ro,rd),disc=b*b-dot(ro,ro)+.48*.48;
  if(disc<0.)return vec3(-1.,0.,0.);
  float begin=max(0.,-b-sqrt(disc)),end=min(limit,-b+sqrt(disc));
  if(begin>end)return vec3(-1.,0.,0.);
  float t=begin,lastT=t,lastD=1.,eps=uBounds.w;
  for(int i=0;i<440;i++){
    if(i>=int(uLight.w))return vec3(-1.,float(i),1.);
    vec3 p=ro+rd*t;float base=baseStone(p);
    float d=(detailed&&base>uBounds.x+.001)?base-uBounds.x:detailStone(p,detailed);
    if(abs(d)<eps)return vec3(t,float(i),0.);
    if(d<0. && lastD>0.) {
      float lo=lastT,hi=t;
      for(int j=0;j<9;j++){float mid=(lo+hi)*.5;if(detailStone(ro+rd*mid,detailed)>0.)lo=mid;else hi=mid;}
      return vec3((lo+hi)*.5,float(i),0.);
    }
    lastT=t;lastD=d;
    float slope=detailed&&base<=uBounds.x+.001?uBounds.z:uBounds.y;
    t+=max(eps*.28,abs(d)*.85/slope);
    if(t>end)return vec3(-1.,float(i),0.);
  }
  return vec3(-1.,440.,1.);
}
float shadowStone(vec3 p,bool detailed){
  vec3 light=sunDirection();float t=.001,shadow=1.;
  for(int i=0;i<48;i++){
    vec3 q=p+light*t;
    float d=conservativeStep(q,detailed);
    if(d<.000035)return 0.;
    shadow=min(shadow,9.*d/t);
    t+=clamp(d*.85,.0002,.055);
    if(t>.9||length(q)>.5)break;
  }
  return clamp(shadow,0.,1.);
}
float localOcclusion(vec3 p,vec3 n,float gradient,bool detailed){
  float total=0.,weight=.55;
  for(int i=0;i<5;i++){
    float h=.002+float(i*i)*.0035;
    float d=detailStone(p+n*h,detailed)/max(.4,gradient);
    total+=max(0.,h-d)/h*weight;weight*=.55;
  }
  return clamp(1.-total*.85,.22,1.);
}
vec3 paletteColor(float v){
  float t=clamp((v-.5)*uMaterial.y+.5+uMaterial.z,0.,1.);
  vec3 color=texture(uPalette,vec2((t*255.+.5)/256.,.5)).rgb;
  float y=dot(color,vec3(.2126,.7152,.0722));
  return clamp(mix(vec3(y),color,uMaterial.w),0.,1.);
}
float fresnel(float cosine){return .04+.96*pow(1.-clamp(cosine,0.,1.),5.);}
float specular(vec3 n,vec3 v,vec3 l,float rough){
  vec3 h=normalize(v+l);float nv=max(dot(n,v),.001),nl=max(dot(n,l),0.),nh=max(dot(n,h),0.);
  float a=rough*rough,a2=a*a,d=nh*nh*(a2-1.)+1.;
  float distribution=a2/(3.14159265*d*d);
  float visibility=.5/max(.001,nl*sqrt(nv*nv*(1.-a2)+a2)+nv*sqrt(nl*nl*(1.-a2)+a2));
  return distribution*visibility*fresnel(dot(v,h));
}
vec3 shadeStone(vec3 p,vec3 rd,bool detailed){
  float e=clamp(uEye.w*.22,.000025,.0007);
  vec3 grad=stoneGradient(p,detailed,e);float gradient=length(grad);vec3 n=grad/max(gradient,.0001);
  if(dot(n,-rd)<0.)n=-n;
  float base=baseStone(p),relief=clamp(base/max(.002,uBounds.x+uFracture.x*.5),-1.,1.);
  // Color is selected from geometric height/relief, not a second pigment noise.
  float mask=.55+(p.y/.48)*.12+relief*.17;
  vec3 albedo=paletteColor(mask);
  int view=int(uDisplay.z);
  if(view==2)return albedo;
  if(view==3)return n*.5+.5;
  if(view==4)return mix(vec3(.18,.47,.66),vec3(.89,.56,.28),relief*.5+.5);
  float ao=localOcclusion(p,n,gradient,detailed);
  vec3 v=-rd,l=sunDirection();float nl=max(dot(n,l),0.);
  float rough=clamp(uMaterial.x+max(-relief,0.)*.08,.3,1.);
  if(view==1)albedo=vec3(.34,.36,.35);
  float shadow=shadowStone(p+n*max(e*3.,.0004),detailed);
  vec3 sun=vec3(3.6,3.25,2.85);
  vec3 color=(albedo*(1.-fresnel(dot(n,v)))/3.14159265+vec3(specular(n,v,l,rough)))*sun*nl*shadow;
  vec3 sky=mix(vec3(.13,.115,.095),vec3(.34,.40,.46),clamp(n.y*.5+.5,0.,1.));
  color+=albedo*sky*ao;
  vec3 fill=normalize(vec3(-.65,.35,.3));color+=albedo*max(dot(n,fill),0.)*vec3(.14,.17,.19)*ao;
  vec3 reflected=reflect(rd,n);float env=fresnel(dot(n,v))*(1.-rough*.7);
  color+=mix(vec3(.06,.05,.04),vec3(.28,.34,.42),clamp(reflected.y*.5+.5,0.,1.))*env*ao;
  return color;
}
vec3 displayColor(vec3 c){
  c=vec3(1.)-exp(-max(c,vec3(0.))*uLight.z*1.35);
  return mix(c*12.92,1.055*pow(c,vec3(1./2.4))-.055,step(vec3(.0031308),c));
}
vec3 linearDisplay(vec3 c){return mix(c*12.92,1.055*pow(max(c,vec3(0.)),vec3(1./2.4))-.055,step(vec3(.0031308),c));}
void main(){
  if(uProbe.w>.5){
    float d=detailStone(uProbe.xyz,uShape.w>.5);float v=floor(clamp((d+1.)*.5,0.,1.)*16777215.+.5);
    fragColor=vec4(floor(v/65536.),mod(floor(v/256.),256.),mod(v,256.),255.)/255.;return;
  }
  vec2 uv=gl_FragCoord.xy/uDisplay.xy*2.-1.;
  vec3 ro=uEye.xyz,rd=normalize(uForward.xyz+uRight.xyz*uv.x*uRight.w*uForward.w+uUp.xyz*uv.y*uRight.w);
  bool detailed=uShape.w>.5 && (uDisplay.w<0. || gl_FragCoord.x/uDisplay.x>=uDisplay.w);
  float floorT=rd.y<-.00001?(-.237-ro.y)/rd.y:1000.;if(floorT<0.)floorT=1000.;
  vec3 hit=traceStone(ro,rd,detailed,floorT);
  int view=int(uDisplay.z);
  if(view==5){fragColor=vec4(vec3(hit.x>0.?1.:0.),1.);return;}
  if(view==6){fragColor=vec4(hit.z>.5?vec3(1.,.16,.08):mix(vec3(.04,.10,.13),vec3(.3,.9,.6),hit.y/uLight.w),1.);return;}
  vec3 color=mix(vec3(.047,.065,.070),vec3(.09,.115,.12),clamp(uv.y*.5+.5,0.,1.));
  if(floorT<10.){
    vec3 p=ro+rd*floorT;
    float sh=length(p.xz)<.8?shadowStone(p+vec3(0,.0004,0),detailed):1.;
    float contact=1.-.25*exp(-dot(p.xz,p.xz)*10.);
    vec3 floorColor=vec3(.16,.174,.17)*mix(.28,1.,sh)*contact;
    color=mix(floorColor,color,1.-exp(-floorT*.10));
  }
  if(hit.x>0.) {
    color=shadeStone(ro+rd*hit.x,rd,detailed);
    if(view==2){fragColor=vec4(linearDisplay(color),1.);return;}
    if(view==3||view==4){fragColor=vec4(color,1.);return;}
  }
  color=displayColor(color);color*=1.-.09*dot(uv*vec2(.55,.8),uv*vec2(.55,.8));
  fragColor=vec4(color,1.);
}`;
