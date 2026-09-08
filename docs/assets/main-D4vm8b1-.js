var K=Object.defineProperty;var Z=(i,t,r)=>t in i?K(i,t,{enumerable:!0,configurable:!0,writable:!0,value:r}):i[t]=r;var d=(i,t,r)=>Z(i,typeof t!="symbol"?t+"":t,r);import{c as J,F as L,p as Q,q as g,x as ee,t as e,G as te,I,H as ae,v as B,u as ie,U as se,B as re,L as ne,M as oe,N as le,w as ce,J as de,R as he,z as ue,S as pe,C as k,O as me,K as fe,X as ge,Q as xe}from"./satmap-BX0uR9-t.js";/**
 * @license lucide-react v0.468.0 - ISC
 *
 * This source code is licensed under the ISC license.
 * See the LICENSE file in the root directory of this source tree.
 */const ve=J("Focus",[["circle",{cx:"12",cy:"12",r:"3",key:"1v7zrd"}],["path",{d:"M3 7V5a2 2 0 0 1 2-2h2",key:"aa7l1z"}],["path",{d:"M17 3h2a2 2 0 0 1 2 2v2",key:"4qcy5o"}],["path",{d:"M21 17v2a2 2 0 0 1-2 2h-2",key:"6vwrx8"}],["path",{d:"M7 21H5a2 2 0 0 1-2-2v-2",key:"ioqczr"}]]),U=["lit","clay","albedo","normals","relief","silhouette","steps"],E={seed:214,form:.65,facets:.55,chips:7,bedding:.65,spacing:24,tilt:14,crackDepth:6,crackWidth:1,crackSpacing:110,porosity:.28,poreSize:13,grain:.45,grainSize:3,detail:!0,palette:"quarry-sandstone",contrast:1.3,bias:-.05,saturation:.85,roughness:.79,exposure:1.05,sunAzimuth:-42,sunElevation:42,view:"lit",quality:"balanced",compare:!1,split:.5,turntable:!1},A=[{id:"sandstone",name:"Desert sandstone",category:"SEDIMENTARY",description:"Broken beds, fine grit, dry fissures.",color:"#c7a079",values:{...E}},{id:"basalt",name:"Vesicular basalt",category:"VOLCANIC",description:"Rough fractured faces and open vesicles.",color:"#566263",values:{...E,palette:"volcanic-obsidian",facets:.8,chips:8,bedding:0,porosity:.9,poreSize:20,grain:.32,grainSize:2.6,crackDepth:8,crackSpacing:85,roughness:.86,bias:.12}},{id:"granite",name:"Broken granite",category:"IGNEOUS",description:"Angular breakup and coarse geometric grain.",color:"#aab4b9",values:{...E,palette:"quarry-granite",facets:.75,chips:5,bedding:0,porosity:.08,poreSize:10,grain:.85,grainSize:4,crackDepth:6,crackSpacing:135,roughness:.7}},{id:"slate",name:"Layered slate",category:"METAMORPHIC",description:"Tilted cleavage, thin ledges, sharp splits.",color:"#7a8496",values:{...E,palette:"quarry-slate",facets:.9,chips:3,bedding:3.5,spacing:14,tilt:36,porosity:.04,grain:.16,grainSize:1.4,crackDepth:7,crackWidth:.55,crackSpacing:75,roughness:.7}},{id:"limestone",name:"Weathered limestone",category:"SEDIMENTARY",description:"Soft broken relief and small solution pits.",color:"#d2d4bd",values:{...E,palette:"quarry-limestone",facets:.4,chips:3.5,bedding:.9,spacing:32,porosity:.65,poreSize:16,grain:.3,grainSize:2,crackDepth:3,roughness:.84}},{id:"riverstone",name:"River-worn stone",category:"WEATHERED",description:"A restrained, smoother comparison specimen.",color:"#8caaa3",values:{...E,palette:"beach-granite",facets:.15,form:.32,chips:1.2,bedding:0,porosity:.04,poreSize:9,grain:.12,grainSize:1,crackDepth:0,roughness:.52}}],be={seed:[0,99999],form:[0,1],facets:[0,1],chips:[0,12],bedding:[0,6],spacing:[8,60],tilt:[-70,70],crackDepth:[0,12],crackWidth:[.2,2],crackSpacing:[45,180],porosity:[0,1],poreSize:[6,30],grain:[0,1.5],grainSize:[.6,6],contrast:[.5,2.5],bias:[-.4,.4],saturation:[0,1.5],roughness:[.3,1],exposure:[.5,1.8],sunAzimuth:[-180,180],sunElevation:[12,80],split:[.05,.95]};function W(i){if(!i||typeof i!="object"||Array.isArray(i))throw new Error("This is not a stone material recipe.");const t=i;if(t.version!==1||t.kind!=="frontier-sdf-stone"||!t.settings||typeof t.settings!="object")throw new Error("Unsupported stone recipe format.");const r=t.settings,a={...E};for(const[c,o]of Object.entries(be)){const n=r[c];if(typeof n!="number"||!Number.isFinite(n)||n<o[0]||n>o[1])throw new Error(`Invalid ${c} value.`);a[c]=n}if(!Number.isInteger(a.seed))throw new Error("Seed must be a whole number.");for(const c of["detail","compare","turntable"]){if(typeof r[c]!="boolean")throw new Error(`Invalid ${c} option.`);a[c]=r[c]}if(typeof r.palette!="string"||!L(r.palette))throw new Error("Unknown SatMap in recipe.");if(a.palette=r.palette,!U.includes(r.view))throw new Error("Invalid inspection view.");if(a.view=r.view,!["draft","balanced","closeup"].includes(r.quality))throw new Error("Invalid preview quality.");return a.quality=r.quality,a}const q=i=>JSON.stringify({kind:"frontier-sdf-stone",version:1,settings:i},null,2),w=(i,t,r)=>Math.max(t,Math.min(r,i));function R(i,t){const r=w((i/Math.max(t,1e-7)-.22)/.58,0,1);return 1-r*r*(3-2*r)}function ye(i,t){return i.detail?(i.chips*R(t,.018)+i.bedding*.73*R(t,i.spacing*5e-4)+i.grain*(R(t,i.grainSize*.001)+.35*R(t,i.grainSize*5e-4)))*.001:0}function Se(i,t){const r=1+i.form*5.2*.14100000000000001;if(!i.detail)return r;const a=i.chips*.001*R(t,.018)*5.2*(.6*22+.28*47+.12*93),c=i.bedding*.001*R(t,i.spacing*5e-4)*(.91*6.2831853*(1e3/i.spacing+5.2*(8*.32+21*.075))+.73*.88*1.875*5.2*13),o=i.grain*.001*5.2*(R(t,i.grainSize*.001)*1e3/i.grainSize+.35*R(t,i.grainSize*5e-4)*2e3/i.grainSize);return Math.max(1.5,r+a+c+o)*1.1}const Ee=`// Analytic 3D material field. The only texture in this lab is the color CLUT.
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
`,we=`#version 300 es
void main(){float x=float((gl_VertexID<<1)&2);float y=float(gl_VertexID&2);gl_Position=vec4(x*2.-1.,y*2.-1.,0.,1.);}`,je=`#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
uniform vec4 uEye,uRight,uUp,uForward,uShape,uMeso,uFracture,uMicro,uMaterial,uLight,uDisplay,uBounds,uProbe;
uniform sampler2D uPalette;
out vec4 fragColor;
${Ee}
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
}`,O=i=>{const t=Math.hypot(...i)||1;return i.map(r=>r/t)},G=(i,t)=>[i[1]*t[2]-i[2]*t[1],i[2]*t[0]-i[0]*t[2],i[0]*t[1]-i[1]*t[0]];class Te{constructor(t,r,a){d(this,"gl");d(this,"program");d(this,"palette");d(this,"target");d(this,"framebuffer");d(this,"locations",new Map);d(this,"observer");d(this,"abort",new AbortController);d(this,"frame",0);d(this,"fence",null);d(this,"submitted",0);d(this,"dirty",!0);d(this,"disposed",!1);d(this,"lost",!1);d(this,"paletteKey","");d(this,"width",0);d(this,"height",0);d(this,"yaw",.65);d(this,"pitch",.34);d(this,"distance",1.1);d(this,"down",null);d(this,"smooth",!1);d(this,"time",0);d(this,"draws",0);d(this,"footprint",0);d(this,"tick",t=>{if(this.frame=0,this.disposed||this.lost||document.hidden)return;const r=this.gl;if(this.fence){const a=r.clientWaitSync(this.fence,0,0);if(a===r.TIMEOUT_EXPIRED){if(t-this.submitted>3e4){this.report("The SDF draw timed out. Reload and use Draft quality.");return}this.frame=requestAnimationFrame(this.tick);return}if(r.deleteSync(this.fence),this.fence=null,a===r.WAIT_FAILED){this.report("Graphics synchronization failed. Reload the material editor.");return}this.report()}if(this.settings.turntable&&(this.time&&(this.yaw+=Math.min((t-this.time)/1e3,.05)*.18),this.dirty=!0),this.time=t,this.dirty)try{this.draw(this.settings,!0),this.dirty=!1,this.fence=r.fenceSync(r.SYNC_GPU_COMMANDS_COMPLETE,0),r.flush(),this.submitted=t}catch(a){this.report(a instanceof Error?a.message:String(a));return}(this.fence||this.settings.turntable)&&(this.frame=requestAnimationFrame(this.tick))});this.canvas=t,this.settings=r,this.status=a;const c=t.getContext("webgl2",{alpha:!1,antialias:!1,preserveDrawingBuffer:!0,powerPreference:"high-performance"});if(!c)throw new Error("WebGL2 is unavailable. Enable browser graphics acceleration to use the SDF material editor.");this.gl=c,this.initializeGPU();const o=this.abort.signal;t.addEventListener("webglcontextlost",l=>{l.preventDefault(),this.lost=!0,this.report("Graphics context lost. Reload the material editor to reconnect.")},{signal:o}),t.addEventListener("webglcontextrestored",()=>{try{this.lost=!1,this.initializeGPU(),this.request()}catch(l){this.report(String(l))}},{signal:o}),t.addEventListener("pointerdown",l=>{l.button>2||(t.focus(),t.setPointerCapture(l.pointerId),this.down={x:l.clientX,y:l.clientY,id:l.pointerId})},{signal:o}),t.addEventListener("pointermove",l=>{!this.down||l.pointerId!==this.down.id||(this.yaw-=(l.clientX-this.down.x)*.006,this.pitch=w(this.pitch+(l.clientY-this.down.y)*.005,.06,1.32),this.down={x:l.clientX,y:l.clientY,id:l.pointerId},this.request())},{signal:o});const n=()=>{this.down=null};t.addEventListener("pointerup",n,{signal:o}),t.addEventListener("pointercancel",n,{signal:o}),t.addEventListener("lostpointercapture",n,{signal:o}),t.addEventListener("contextmenu",l=>l.preventDefault(),{signal:o}),t.addEventListener("wheel",l=>{l.preventDefault(),this.distance=w(this.distance*Math.exp(l.deltaY*.0011),.46,3),this.request()},{signal:o,passive:!1}),t.addEventListener("keydown",l=>{l.key.toLowerCase()==="f"&&(l.preventDefault(),this.resetCamera()),l.key.toLowerCase()==="c"&&(this.smooth=!0,this.request())},{signal:o}),window.addEventListener("keyup",l=>{l.key.toLowerCase()==="c"&&(this.smooth=!1,this.request())},{signal:o}),window.addEventListener("blur",()=>{this.smooth=!1,this.down=null,this.request()},{signal:o}),document.addEventListener("visibilitychange",()=>{this.time=0,document.hidden||this.request()},{signal:o}),this.observer=new ResizeObserver(()=>this.request()),this.observer.observe(t),this.request()}initializeGPU(){const t=this.gl,r=(o,n)=>{const l=t.createShader(o);if(t.shaderSource(l,n),t.compileShader(l),!t.getShaderParameter(l,t.COMPILE_STATUS)){const m=t.getShaderInfoLog(l);throw t.deleteShader(l),new Error(m||"Stone shader compilation failed.")}return l},a=r(t.VERTEX_SHADER,we);let c;try{c=r(t.FRAGMENT_SHADER,je)}catch(o){throw t.deleteShader(a),o}if(this.program=t.createProgram(),t.attachShader(this.program,a),t.attachShader(this.program,c),t.linkProgram(this.program),t.deleteShader(a),t.deleteShader(c),!t.getProgramParameter(this.program,t.LINK_STATUS))throw new Error(t.getProgramInfoLog(this.program)||"Stone shader linking failed.");this.locations.clear();for(const o of["uEye","uRight","uUp","uForward","uShape","uMeso","uFracture","uMicro","uMaterial","uLight","uDisplay","uBounds","uProbe"])this.locations.set(o,t.getUniformLocation(this.program,o));this.palette=t.createTexture(),t.activeTexture(t.TEXTURE0),t.bindTexture(t.TEXTURE_2D,this.palette),t.texParameteri(t.TEXTURE_2D,t.TEXTURE_MIN_FILTER,t.LINEAR),t.texParameteri(t.TEXTURE_2D,t.TEXTURE_MAG_FILTER,t.LINEAR),t.texParameteri(t.TEXTURE_2D,t.TEXTURE_WRAP_S,t.CLAMP_TO_EDGE),t.texParameteri(t.TEXTURE_2D,t.TEXTURE_WRAP_T,t.CLAMP_TO_EDGE),t.texImage2D(t.TEXTURE_2D,0,t.SRGB8_ALPHA8,256,1,0,t.RGBA,t.UNSIGNED_BYTE,null),t.useProgram(this.program),t.uniform1i(t.getUniformLocation(this.program,"uPalette"),0),this.target=t.createTexture(),this.framebuffer=t.createFramebuffer(),this.width=this.height=0,this.paletteKey="",this.fence=null,this.dirty=!0}update(t){this.settings=t,this.request()}zoomBy(t){this.distance=w(this.distance*t,.46,3),this.request()}resetCamera(){this.yaw=.65,this.pitch=.34,this.distance=1.1,this.request()}setCamera(t,r,a){this.yaw=t,this.pitch=w(r,.06,1.32),this.distance=w(a,.46,3),this.request()}request(){this.dirty=!0,!this.frame&&!this.disposed&&!this.lost&&(this.frame=requestAnimationFrame(this.tick))}report(t=""){this.status({ready:!t,width:this.width,height:this.height,error:t,footprint:this.footprint,draws:this.draws})}dimensions(t){const r=Math.max(1,this.canvas.clientWidth),a=Math.max(1,this.canvas.clientHeight),c={draft:320,balanced:600,closeup:900}[t.quality],o=Math.min(devicePixelRatio||1,2,c/Math.sqrt(r*a));return[Math.max(1,Math.round(r*o)),Math.max(1,Math.round(a*o))]}bindTarget(t,r){const a=this.gl;if(this.width!==t||this.height!==r){if(a.activeTexture(a.TEXTURE1),a.bindTexture(a.TEXTURE_2D,this.target),a.texParameteri(a.TEXTURE_2D,a.TEXTURE_MIN_FILTER,a.LINEAR),a.texParameteri(a.TEXTURE_2D,a.TEXTURE_MAG_FILTER,a.LINEAR),a.texParameteri(a.TEXTURE_2D,a.TEXTURE_WRAP_S,a.CLAMP_TO_EDGE),a.texParameteri(a.TEXTURE_2D,a.TEXTURE_WRAP_T,a.CLAMP_TO_EDGE),a.texImage2D(a.TEXTURE_2D,0,a.RGBA8,t,r,0,a.RGBA,a.UNSIGNED_BYTE,null),a.bindFramebuffer(a.FRAMEBUFFER,this.framebuffer),a.framebufferTexture2D(a.FRAMEBUFFER,a.COLOR_ATTACHMENT0,a.TEXTURE_2D,this.target,0),a.checkFramebufferStatus(a.FRAMEBUFFER)!==a.FRAMEBUFFER_COMPLETE)throw new Error("Could not allocate the material preview.");this.width=t,this.height=r}else a.bindFramebuffer(a.FRAMEBUFFER,this.framebuffer);a.viewport(0,0,t,r)}uniforms(t,r,a,c){const o=this.gl,n=(j,z)=>o.uniform4fv(this.locations.get(j),z),l=[Math.sin(this.yaw)*Math.cos(this.pitch)*this.distance,Math.sin(this.pitch)*this.distance,Math.cos(this.yaw)*Math.cos(this.pitch)*this.distance],m=O(l.map(j=>-j)),f=O(G(m,[0,1,0])),S=G(f,m),x=.36;this.footprint=2*Math.max(.04,this.distance-.29)*x/a;const C=1+t.form*5.2*(.016*4+.007*11),N=(c??t.detail)&&!this.smooth,v={...t,detail:N};n("uEye",[...l,this.footprint]),n("uRight",[...f,x]),n("uUp",[...S,0]),n("uForward",[...m,r/a]),n("uShape",[t.seed,t.form,t.facets,N?1:0]),n("uMeso",[t.chips*.001,t.bedding*.001,t.spacing*.001,t.tilt*Math.PI/180]),n("uFracture",[t.crackDepth*.001,t.crackWidth*.001,t.crackSpacing*.001,t.porosity]),n("uMicro",[t.poreSize*.001,t.grain*.001,t.grainSize*.001,0]),n("uMaterial",[t.roughness,t.contrast,t.bias,t.saturation]),n("uLight",[t.sunAzimuth*Math.PI/180,t.sunElevation*Math.PI/180,t.exposure,{draft:240,balanced:340,closeup:440}[t.quality]]),n("uDisplay",[r,a,U.indexOf(t.view),t.compare?t.split:-1]),n("uBounds",[ye(v,this.footprint),C,Se(v,this.footprint),w(this.footprint*.1,2e-5,15e-5)]),n("uProbe",[0,0,0,0])}draw(t,r){if(this.disposed||this.lost||this.gl.isContextLost())throw new Error("The graphics context is unavailable.");const a=this.gl,[c,o]=this.dimensions(t);this.bindTarget(c,o),a.useProgram(this.program),a.disable(a.BLEND),a.disable(a.DEPTH_TEST),a.disable(a.CULL_FACE),a.activeTexture(a.TEXTURE0),a.bindTexture(a.TEXTURE_2D,this.palette);const n=L(t.palette);n.id!==this.paletteKey&&(a.texSubImage2D(a.TEXTURE_2D,0,0,0,256,1,a.RGBA,a.UNSIGNED_BYTE,Q(n.palette)),this.paletteKey=n.id),this.uniforms(t,c,o),a.drawArrays(a.TRIANGLES,0,3),r&&(this.canvas.width!==c&&(this.canvas.width=c),this.canvas.height!==o&&(this.canvas.height=o),a.bindFramebuffer(a.READ_FRAMEBUFFER,this.framebuffer),a.bindFramebuffer(a.DRAW_FRAMEBUFFER,null),a.blitFramebuffer(0,0,c,o,0,0,c,o,a.COLOR_BUFFER_BIT,a.NEAREST)),this.draws++}async capture(t=this.settings.view,r=this.settings.detail){this.draw({...this.settings,view:t,detail:r},!1);const a=this.gl,c=new Uint8Array(this.width*this.height*4);a.bindFramebuffer(a.READ_FRAMEBUFFER,this.framebuffer),a.readPixels(0,0,this.width,this.height,a.RGBA,a.UNSIGNED_BYTE,c);const o=new Uint8ClampedArray(c.length),n=this.width*4;for(let f=0;f<this.height;f++)o.set(c.subarray(f*n,(f+1)*n),(this.height-1-f)*n);const l=document.createElement("canvas");l.width=this.width,l.height=this.height,l.getContext("2d").putImageData(new ImageData(o,this.width,this.height),0,0);const m=await new Promise(f=>l.toBlob(f,"image/png"));if(this.request(),!m)throw new Error("Could not encode the preview PNG.");return m}probe(t,r=!0){const a=this.gl,[c,o]=this.dimensions(this.settings);this.bindTarget(c,o),a.useProgram(this.program),a.activeTexture(a.TEXTURE0),a.bindTexture(a.TEXTURE_2D,this.palette),this.uniforms(this.settings,c,o,r),a.viewport(0,0,1,1);const n=new Uint8Array(4),l=[];for(const m of t)a.uniform4fv(this.locations.get("uProbe"),[...m,1]),a.drawArrays(a.TRIANGLES,0,3),a.readPixels(0,0,1,1,a.RGBA,a.UNSIGNED_BYTE,n),l.push((n[0]*65536+n[1]*256+n[2])/16777215*2-1);return this.request(),l}getDiagnostics(){return{backend:"WebGL2",width:this.width,height:this.height,footprint:this.footprint,draws:this.draws,materialTextures:1,texturePurpose:"256×1 color CLUT only",detailSource:"analytic 3D signed field",glError:this.gl.getError(),contextLost:this.gl.isContextLost()}}dispose(){this.disposed=!0,cancelAnimationFrame(this.frame),this.abort.abort(),this.observer.disconnect(),this.fence&&this.gl.deleteSync(this.fence),this.gl.deleteTexture(this.palette),this.gl.deleteTexture(this.target),this.gl.deleteFramebuffer(this.framebuffer),this.gl.deleteProgram(this.program)}}const Y="frontier.sdf-material-lab.v1",Re={lit:"Surface",clay:"Clay",albedo:"Albedo",normals:"Normals",relief:"SDF relief",silhouette:"Silhouette",steps:"March steps"};function X(i,t){const r=URL.createObjectURL(i),a=document.createElement("a");a.href=r,a.download=t,a.click(),setTimeout(()=>URL.revokeObjectURL(r),1e3)}function Ne(){let i={...E};try{const r=localStorage.getItem(Y);r&&(i=W(JSON.parse(r)))}catch{}return new URLSearchParams(location.search).get("quality")==="draft"&&(i.quality="draft"),i}function u({label:i,value:t,min:r,max:a,step:c=1,unit:o="",scale:n=1,onChange:l,disabled:m=!1}){return e.jsxs("label",{className:`lab-slider ${m?"is-disabled":""}`,children:[e.jsxs("span",{className:"lab-slider-label",children:[i,e.jsxs("span",{className:"lab-number",children:[e.jsx("input",{"aria-label":`${i} numeric value`,type:"number",min:r*n,max:a*n,step:c*n,value:Number((t*n).toFixed(3)),disabled:m,onChange:f=>{const S=f.target.valueAsNumber;Number.isFinite(S)&&l(w(S/n,r,a))}}),e.jsx("small",{children:o})]})]}),e.jsx("input",{"aria-label":i,type:"range",min:r,max:a,step:c,value:t,disabled:m,onChange:f=>l(Number(f.target.value))})]})}function Ce(){const[i,t]=g.useState(Ne),[r,a]=g.useState(()=>{var s;return((s=A.find(p=>["seed","form","facets","chips","bedding","spacing","tilt","crackDepth","crackWidth","crackSpacing","porosity","poreSize","grain","grainSize","palette"].every(y=>p.values[y]===i[y])))==null?void 0:s.id)||"custom"}),[c,o]=g.useState(!1),[n,l]=g.useState({ready:!1,width:0,height:0,error:"",footprint:0,draws:0}),[m,f]=g.useState(""),[S,x]=g.useState(""),[C,N]=g.useState(!1),[v,j]=g.useState(null),z=g.useRef(null),T=g.useRef(null),_=g.useRef(i),P=g.useRef(null);_.current=i,g.useEffect(()=>{try{const s=new Te(z.current,_.current,l);return T.current=s,()=>{s.dispose(),T.current=null}}catch(s){l(p=>({...p,error:s instanceof Error?s.message:String(s)}))}},[]),g.useEffect(()=>{var s;return(s=T.current)==null?void 0:s.update(i)},[i]),g.useEffect(()=>{if(!S)return;const s=setTimeout(()=>x(""),6e3);return()=>clearTimeout(s)},[S]);const b=(s,p)=>{t(y=>({...y,[s]:p})),o(!0)},h=s=>p=>b(s,p),F=A.find(s=>s.id===r),M=L(i.palette),D=ee(m),$=async()=>{if(T.current){N(!0);try{X(await T.current.capture(),`frontier-stone-${i.seed}-${i.view}.png`)}catch(s){x(String(s))}finally{N(!1)}}},H=()=>{try{localStorage.setItem(Y,q(i)),x("Recipe saved in this browser. The terrain project is untouched."),o(!1)}catch{x("Browser storage is unavailable. Use Export recipe instead.")}},V=async s=>{if(s)try{if(s.size>64*1024)throw new Error("Stone recipes must be smaller than 64 KB.");const p=W(JSON.parse(await s.text()));t(p),a("custom"),o(!1),x("Stone recipe loaded. No image detail maps involved.")}catch(p){x(p instanceof Error?p.message:String(p))}};return e.jsxs("div",{className:"material-lab",children:[e.jsxs("header",{className:"lab-header",children:[e.jsxs("a",{className:"lab-brand",href:"./index.html",children:[e.jsx(te,{size:23}),e.jsx("strong",{children:"FRONTIER"})]}),e.jsx("span",{className:"lab-header-divider"}),e.jsxs("div",{className:"lab-page-title",children:[e.jsx("span",{children:"Material editor"}),e.jsxs("small",{children:["SDF SURFACE LAB ",e.jsx("i",{})," EXPERIMENTAL"]})]}),e.jsxs("div",{className:"lab-header-actions",children:[e.jsxs("a",{className:"lab-terrain-link",href:"./index.html",children:["Terrain studio ",e.jsx(I,{size:13})]}),e.jsx("button",{onClick:H,className:"lab-icon-button","aria-label":"Save stone recipe",title:"Save recipe locally",children:e.jsx(ae,{size:17})}),e.jsxs("button",{className:"lab-primary",onClick:()=>void $(),disabled:!n.ready||!!n.error||C,children:[e.jsx(B,{size:14}),C?"Capturing…":"Export PNG"]})]})]}),e.jsxs("div",{className:"lab-mobile-nav",children:[e.jsx("button",{className:v==="specimen"?"selected":"",onClick:()=>j(v==="specimen"?null:"specimen"),children:"Specimens"}),e.jsx("button",{className:v===null?"selected":"",onClick:()=>j(null),children:"Viewport"}),e.jsx("button",{className:v==="controls"?"selected":"",onClick:()=>j(v==="controls"?null:"controls"),children:"Surface controls"})]}),e.jsxs("main",{className:"lab-workspace",children:[e.jsxs("aside",{className:`lab-presets ${v==="specimen"?"mobile-open":""}`,"aria-label":"Stone specimens",children:[e.jsxs("div",{className:"lab-panel-title",children:[e.jsx("span",{children:"Specimens"}),e.jsx("small",{children:"01—06"})]}),e.jsxs("p",{className:"lab-panel-note",children:["One organic form.",e.jsx("br",{}),"A different surface story."]}),e.jsx("div",{className:"lab-specimen-list",children:A.map((s,p)=>e.jsxs("button",{className:r===s.id?"selected":"","aria-pressed":r===s.id,onClick:()=>{t(y=>({...s.values,quality:y.quality,view:y.view,compare:y.compare,split:y.split,turntable:y.turntable})),a(s.id),o(!1),j(null)},children:[e.jsxs("div",{className:"lab-specimen-graphic",style:{"--stone-color":s.color},children:[e.jsx("span",{}),e.jsxs("small",{children:["0",p+1]}),r===s.id&&e.jsx(ie,{size:12})]}),e.jsxs("span",{className:"lab-specimen-text",children:[e.jsx("strong",{children:s.name}),e.jsx("small",{children:s.category})]})]},s.id))}),e.jsxs("div",{className:"lab-recipe-actions",children:[e.jsxs("button",{onClick:()=>{X(new Blob([q(i)],{type:"application/json"}),`frontier-stone-${i.seed}.json`)},children:[e.jsx(B,{size:13})," Export recipe"]}),e.jsxs("button",{onClick:()=>{var s;return(s=P.current)==null?void 0:s.click()},children:[e.jsx(se,{size:13})," Import recipe"]}),e.jsx("input",{ref:P,hidden:!0,type:"file",accept:".json,application/json","aria-label":"Import stone recipe",onChange:s=>{var p;V((p=s.target.files)==null?void 0:p[0]),s.target.value=""}})]}),e.jsxs("div",{className:"lab-side-footnote",children:[e.jsx(re,{size:16}),e.jsxs("p",{children:["Not a normal-map preview.",e.jsx("br",{}),"The ray hits the detailed",e.jsx("br",{}),"signed surface itself."]})]})]}),e.jsxs("section",{className:"lab-stage","aria-label":"SDF stone preview",children:[e.jsxs("div",{className:"lab-stage-heading",children:[e.jsxs("div",{children:[e.jsxs("small",{children:["SPECIMEN /"," ",r==="custom"?"CUSTOM":String(A.findIndex(s=>s.id===r)+1).padStart(2,"0")]}),e.jsxs("h1",{children:[(F==null?void 0:F.name)??"Custom stone",e.jsx("span",{children:c?"Modified":""})]})]}),e.jsxs("span",{className:"lab-renderer-tag",children:[e.jsx("i",{}),"WEBGL2"]})]}),e.jsx("div",{className:"lab-view-tabs",role:"group","aria-label":"Stone inspection views",children:U.map(s=>e.jsx("button",{"aria-pressed":i.view===s,onClick:()=>b("view",s),children:Re[s]},s))}),e.jsxs("div",{className:"lab-canvas-wrap",children:[e.jsx("canvas",{ref:z,className:"lab-canvas",tabIndex:0,"aria-label":"Interactive SDF stone. Drag to orbit, scroll to zoom, F to frame, hold C for the smooth form."}),e.jsxs("div",{className:"lab-canvas-tag",children:[e.jsx(ne,{size:12}),e.jsx("span",{children:"SIGNED SURFACE DETAIL"}),e.jsx("small",{children:"SatMap color only · no image relief"})]}),!n.ready&&!n.error&&e.jsx("div",{className:"lab-loading",children:"Tracing the stone surface…"}),n.error&&e.jsxs("div",{className:"lab-error",role:"alert",children:[e.jsx("strong",{children:"Graphics needs attention"}),e.jsx("p",{children:n.error}),e.jsx("button",{onClick:()=>location.reload(),children:"Reload material editor"})]}),i.compare&&e.jsxs(e.Fragment,{children:[e.jsx("div",{className:"lab-split-line",style:{left:`${i.split*100}%`}}),e.jsx("span",{className:"lab-split-label left",children:"SMOOTH FORM"}),e.jsx("span",{className:"lab-split-label right",children:"SDF DETAIL"})]}),e.jsxs("div",{className:"lab-view-tools",children:[e.jsx("button",{"aria-label":"Zoom into stone",onClick:()=>{var s;return(s=T.current)==null?void 0:s.zoomBy(.8)},children:e.jsx(oe,{size:15})}),e.jsx("button",{"aria-label":"Zoom out from stone",onClick:()=>{var s;return(s=T.current)==null?void 0:s.zoomBy(1.25)},children:e.jsx(le,{size:15})}),e.jsx("button",{className:i.turntable?"selected":"","aria-pressed":i.turntable,"aria-label":"Rotate stone automatically",onClick:()=>b("turntable",!i.turntable),children:e.jsx(ce,{size:14})}),e.jsx("button",{"aria-label":"Frame stone",onClick:()=>{var s;return(s=T.current)==null?void 0:s.resetCamera()},children:e.jsx(ve,{size:16})})]}),e.jsxs("div",{className:"lab-orbit-help",children:["DRAG Orbit ",e.jsx("i",{})," SCROLL Zoom ",e.jsx("i",{})," HOLD C Smooth form"]})]}),e.jsxs("div",{className:"lab-stage-bottom",children:[e.jsxs("label",{className:"lab-check",children:[e.jsx("input",{type:"checkbox",checked:i.compare,onChange:s=>b("compare",s.target.checked)}),e.jsx("span",{children:"Compare smooth / detailed"})]}),i.compare&&e.jsx("input",{className:"lab-split-range","aria-label":"Comparison split",type:"range",min:.05,max:.95,step:.01,value:i.split,onChange:s=>b("split",Number(s.target.value))}),e.jsxs("span",{className:"lab-resolution",children:[n.width," × ",n.height," ",e.jsx("i",{})," ",n.footprint?(n.footprint*1e3).toFixed(2):"—"," mm footprint"]})]}),e.jsxs("div",{className:"lab-proof-note",children:[e.jsx(de,{size:14}),e.jsx("span",{children:i.view==="silhouette"?"White is the actual ray-hit silhouette. Turn detail off to compare the boundary.":i.view==="relief"?"Blue = inward carving. Warm = outward relief. Derived from the signed field at the hit point.":i.view==="steps"?"Mint shows raymarch work. Red marks an exhausted step budget—not a valid surface hit.":"Inspect in Clay or Silhouette to separate geometric detail from the SatMap colors."})]})]}),e.jsxs("aside",{className:`lab-controls ${v==="controls"?"mobile-open":""}`,"aria-label":"SDF surface controls",children:[e.jsxs("div",{className:"lab-panel-title",children:[e.jsx("span",{children:"Surface stack"}),e.jsx("button",{className:"lab-icon-button","aria-label":"Reset stone material",onClick:()=>{t(s=>({...E,quality:s.quality})),a("sandstone"),o(!1)},children:e.jsx(he,{size:14})})]}),e.jsxs("div",{className:"lab-color-card",children:[e.jsx("small",{children:"BASE COLOR / SATMAP"}),e.jsx("strong",{children:M.name}),e.jsx("div",{className:"lab-palette-ramp",style:{background:ue(M.palette)}}),e.jsx("input",{className:"lab-palette-search","aria-label":"Search stone palettes",placeholder:`Search ${pe.length} palettes…`,value:m,onChange:s=>f(s.target.value)}),e.jsxs("select",{"aria-label":"Stone SatMap palette",value:D.some(s=>s.id===i.palette)?i.palette:"",onChange:s=>{s.target.value&&b("palette",s.target.value)},children:[!D.some(s=>s.id===i.palette)&&e.jsx("option",{value:"",children:D.length?"Choose a matching palette":"No matching palettes"}),D.map(s=>e.jsxs("option",{value:s.id,children:[s.category," · ",s.name]},s.id))]}),e.jsxs("span",{className:"lab-palette-origin",children:[M.origin==="satellite"?"Satellite-derived CLUT":M.origin==="fantasy"?"Fantasy · authored CLUT":"Terrain-inspired · authored CLUT"," ","· COLOR ONLY"]})]}),e.jsxs("details",{className:"lab-control-section",open:!0,children:[e.jsxs("summary",{children:[e.jsx("span",{children:"01"})," Organic form ",e.jsx(k,{size:12})]}),e.jsx(u,{label:"Form irregularity",value:i.form,min:0,max:1,step:.01,scale:100,unit:"%",onChange:h("form")}),e.jsx(u,{label:"Angular facets",value:i.facets,min:0,max:1,step:.01,scale:100,unit:"%",onChange:h("facets")}),e.jsxs("div",{className:"lab-seed",children:[e.jsxs("label",{children:["Seed",e.jsx("input",{type:"number","aria-label":"Stone seed",min:0,max:99999,value:i.seed,onChange:s=>{Number.isFinite(s.target.valueAsNumber)&&b("seed",Math.round(w(s.target.valueAsNumber,0,99999)))}})]}),e.jsx("button",{"aria-label":"New stone seed",onClick:()=>b("seed",crypto.getRandomValues(new Uint32Array(1))[0]%1e5),children:e.jsx(me,{size:14})})]})]}),e.jsxs("div",{className:"lab-detail-toggle",children:[e.jsxs("label",{className:"lab-check",children:[e.jsx("input",{type:"checkbox",checked:i.detail,onChange:s=>b("detail",s.target.checked)}),e.jsx("span",{children:"SDF surface detail"})]}),e.jsx("small",{children:"CHANGES THE ZERO SURFACE"})]}),e.jsxs("details",{className:"lab-control-section",open:!0,children:[e.jsxs("summary",{children:[e.jsx("span",{children:"02"})," Chips & cleavage ",e.jsx(k,{size:12})]}),e.jsx(u,{label:"Chipped relief",value:i.chips,min:0,max:12,step:.25,unit:"mm",onChange:h("chips"),disabled:!i.detail}),e.jsx(u,{label:"Crack depth",value:i.crackDepth,min:0,max:12,step:.25,unit:"mm",onChange:h("crackDepth"),disabled:!i.detail}),e.jsx(u,{label:"Crack aperture",value:i.crackWidth,min:.2,max:2,step:.05,unit:"mm",onChange:h("crackWidth"),disabled:!i.detail}),e.jsx(u,{label:"Crack spacing",value:i.crackSpacing,min:45,max:180,step:5,unit:"mm",onChange:h("crackSpacing"),disabled:!i.detail})]}),e.jsxs("details",{className:"lab-control-section",children:[e.jsxs("summary",{children:[e.jsx("span",{children:"03"})," Bedding ",e.jsx(k,{size:12})]}),e.jsx(u,{label:"Bedding relief",value:i.bedding,min:0,max:6,step:.1,unit:"mm",onChange:h("bedding"),disabled:!i.detail}),e.jsx(u,{label:"Layer spacing",value:i.spacing,min:8,max:60,step:1,unit:"mm",onChange:h("spacing"),disabled:!i.detail}),e.jsx(u,{label:"Bedding tilt",value:i.tilt,min:-70,max:70,step:1,unit:"°",onChange:h("tilt"),disabled:!i.detail})]}),e.jsxs("details",{className:"lab-control-section",open:!0,children:[e.jsxs("summary",{children:[e.jsx("span",{children:"04"})," Pores & grain ",e.jsx(k,{size:12})]}),e.jsx(u,{label:"Pore coverage",value:i.porosity,min:0,max:1,step:.01,scale:100,unit:"%",onChange:h("porosity"),disabled:!i.detail}),e.jsx(u,{label:"Pore spacing",value:i.poreSize,min:6,max:30,step:1,unit:"mm",onChange:h("poreSize"),disabled:!i.detail}),e.jsx(u,{label:"Grain relief",value:i.grain,min:0,max:1.5,step:.05,unit:"mm",onChange:h("grain"),disabled:!i.detail}),e.jsx(u,{label:"Grain size",value:i.grainSize,min:.6,max:6,step:.1,unit:"mm",onChange:h("grainSize"),disabled:!i.detail}),e.jsx("p",{children:"Zoom in to resolve finer grain. Sub-pixel bands fade out in the field, not into oversized noise."})]}),e.jsxs("details",{className:"lab-control-section",children:[e.jsxs("summary",{children:[e.jsx("span",{children:"05"})," Light & response ",e.jsx(fe,{size:12})]}),e.jsx(u,{label:"Roughness",value:i.roughness,min:.3,max:1,step:.01,scale:100,unit:"%",onChange:h("roughness")}),e.jsx(u,{label:"Palette contrast",value:i.contrast,min:.5,max:2.5,step:.05,onChange:h("contrast")}),e.jsx(u,{label:"Palette bias",value:i.bias,min:-.4,max:.4,step:.01,onChange:h("bias")}),e.jsx(u,{label:"Saturation",value:i.saturation,min:0,max:1.5,step:.01,scale:100,unit:"%",onChange:h("saturation")}),e.jsx(u,{label:"Light azimuth",value:i.sunAzimuth,min:-180,max:180,step:1,unit:"°",onChange:h("sunAzimuth")}),e.jsx(u,{label:"Light elevation",value:i.sunElevation,min:12,max:80,step:1,unit:"°",onChange:h("sunElevation")}),e.jsx(u,{label:"Exposure",value:i.exposure,min:.5,max:1.8,step:.05,onChange:h("exposure")})]}),e.jsxs("label",{className:"lab-quality",children:["Preview budget",e.jsxs("select",{"aria-label":"Stone preview quality",value:i.quality,onChange:s=>b("quality",s.target.value),children:[e.jsx("option",{value:"draft",children:"Draft · lower GPU cost"}),e.jsx("option",{value:"balanced",children:"Balanced"}),e.jsx("option",{value:"closeup",children:"Close-up · more detail"})]})]}),e.jsxs("a",{className:"lab-docs",href:"./research/sdf-stone-materials.md",target:"_blank",rel:"noreferrer",children:["Method, limits & verification ",e.jsx(I,{size:12})]})]})]}),e.jsxs("footer",{className:"lab-footer",children:[e.jsxs("span",{children:[e.jsx("i",{})," SDF GEOMETRY · NOT A SCANNED MATERIAL"]}),e.jsx("span",{children:"Separate experiment · terrain project unchanged"})]}),S&&e.jsxs("div",{className:"lab-toast",role:"status",children:[S,e.jsx("button",{"aria-label":"Dismiss message",onClick:()=>x(""),children:e.jsx(ge,{size:14})})]})]})}xe.createRoot(document.getElementById("material-root")).render(e.jsx(Ce,{}));
