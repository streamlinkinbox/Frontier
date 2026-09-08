var ge=Object.defineProperty;var xe=(t,e,r)=>e in t?ge(t,e,{enumerable:!0,configurable:!0,writable:!0,value:r}):t[e]=r;var u=(t,e,r)=>xe(t,typeof e!="symbol"?e+"":e,r);import{c as ve,F as Q,p as be,q as k,x as ye,t as a,G as Se,I as ie,H as we,v as re,u as Ee,U as je,B as ke,L as Ce,M as Te,N as Me,w as Re,J as Ne,R as ze,z as Ae,S as Be,C as X,O as De,K as Fe,X as Le,Q as _e}from"./satmap-BX0uR9-t.js";/**
 * @license lucide-react v0.468.0 - ISC
 *
 * This source code is licensed under the ISC license.
 * See the LICENSE file in the root directory of this source tree.
 */const Ue=ve("Focus",[["circle",{cx:"12",cy:"12",r:"3",key:"1v7zrd"}],["path",{d:"M3 7V5a2 2 0 0 1 2-2h2",key:"aa7l1z"}],["path",{d:"M17 3h2a2 2 0 0 1 2 2v2",key:"4qcy5o"}],["path",{d:"M21 17v2a2 2 0 0 1-2 2h-2",key:"6vwrx8"}],["path",{d:"M7 21H5a2 2 0 0 1-2-2v-2",key:"ioqczr"}]]),ee=["lit","clay","albedo","normals","relief","silhouette","steps"],B={seed:214,form:.65,facets:.55,chips:7,bedding:.65,layerBreakup:.65,spacing:24,tilt:14,crackDepth:6,crackWidth:1,crackSpacing:110,crackBranching:.7,crackChipping:.65,porosity:.28,poreSize:13,poreIrregularity:.75,grain:.45,grainSize:3,detail:!0,palette:"quarry-sandstone",contrast:1.3,bias:-.05,saturation:.85,roughness:.79,exposure:1.05,sunAzimuth:-42,sunElevation:42,view:"lit",quality:"balanced",compare:!1,split:.5,turntable:!1},K=[{id:"sandstone",name:"Desert sandstone",category:"SEDIMENTARY",description:"Broken beds, fine grit, dry fissures.",color:"#c7a079",values:{...B}},{id:"basalt",name:"Vesicular basalt",category:"VOLCANIC",description:"Rough fractured faces and open vesicles.",color:"#566263",values:{...B,palette:"volcanic-obsidian",facets:.8,chips:8,bedding:0,porosity:.9,poreSize:20,poreIrregularity:.9,grain:.32,grainSize:2.6,crackDepth:8,crackSpacing:85,roughness:.86,bias:.12}},{id:"granite",name:"Broken granite",category:"IGNEOUS",description:"Angular breakup and coarse geometric grain.",color:"#aab4b9",values:{...B,palette:"quarry-granite",facets:.75,chips:5,bedding:0,porosity:.08,poreSize:10,grain:.85,grainSize:4,crackDepth:6,crackSpacing:135,roughness:.7}},{id:"slate",name:"Layered slate",category:"METAMORPHIC",description:"Broken foliation, torn ledges and branching splits.",color:"#7a8496",values:{...B,palette:"quarry-slate",facets:.9,chips:3,bedding:2.8,spacing:16,layerBreakup:.9,tilt:36,porosity:.04,grain:.16,grainSize:1.4,crackDepth:7,crackWidth:.85,crackChipping:.8,crackSpacing:75,roughness:.7}},{id:"limestone",name:"Weathered limestone",category:"SEDIMENTARY",description:"Soft broken relief and small solution pits.",color:"#d2d4bd",values:{...B,palette:"quarry-limestone",facets:.4,chips:3.5,bedding:.9,spacing:32,porosity:.65,poreSize:16,grain:.3,grainSize:2,crackDepth:3,roughness:.84}},{id:"riverstone",name:"River-worn stone",category:"WEATHERED",description:"A restrained, smoother comparison specimen.",color:"#8caaa3",values:{...B,palette:"beach-granite",facets:.15,form:.32,chips:1.2,bedding:0,porosity:.04,poreSize:9,grain:.12,grainSize:1,crackDepth:0,roughness:.52}}],Pe={seed:[0,99999],form:[0,1],facets:[0,1],chips:[0,12],bedding:[0,6],layerBreakup:[0,1],spacing:[8,60],tilt:[-70,70],crackDepth:[0,12],crackWidth:[.2,2],crackSpacing:[45,180],crackBranching:[0,1],crackChipping:[0,1],porosity:[0,1],poreSize:[6,30],poreIrregularity:[0,1],grain:[0,1.5],grainSize:[.6,6],contrast:[.5,2.5],bias:[-.4,.4],saturation:[0,1.5],roughness:[.3,1],exposure:[.5,1.8],sunAzimuth:[-180,180],sunElevation:[12,80],split:[.05,.95]};function ue(t){if(!t||typeof t!="object"||Array.isArray(t))throw new Error("This is not a stone material recipe.");const e=t;if(e.version!==1&&e.version!==2||e.kind!=="frontier-sdf-stone"||!e.settings||typeof e.settings!="object")throw new Error("Unsupported stone recipe format.");const r=e.settings,i={...B};for(const[o,l]of Object.entries(Pe)){const s=r[o];if(!(e.version===1&&s===void 0&&["layerBreakup","crackBranching","crackChipping","poreIrregularity"].includes(o))){if(typeof s!="number"||!Number.isFinite(s)||s<l[0]||s>l[1])throw new Error(`Invalid ${o} value.`);i[o]=s}}if(!Number.isInteger(i.seed))throw new Error("Seed must be a whole number.");for(const o of["detail","compare","turntable"]){if(typeof r[o]!="boolean")throw new Error(`Invalid ${o} option.`);i[o]=r[o]}if(typeof r.palette!="string"||!Q(r.palette))throw new Error("Unknown SatMap in recipe.");if(i.palette=r.palette,!ee.includes(r.view))throw new Error("Invalid inspection view.");if(i.view=r.view,!["draft","balanced","closeup"].includes(r.quality))throw new Error("Invalid preview quality.");return i.quality=r.quality,i}const ne=t=>JSON.stringify({kind:"frontier-sdf-stone",version:2,settings:t},null,2),z=(t,e,r)=>Math.max(e,Math.min(r,t));function D(t,e){const r=z((t/Math.max(e,1e-7)-.22)/.58,0,1);return 1-r*r*(3-2*r)}function Ie(t,e){return t.detail?(t.chips*D(e,.018)+t.bedding*1.3*D(e,t.spacing*5e-4)+t.grain*(D(e,t.grainSize*.001)+.35*D(e,t.grainSize*5e-4)))*.001:0}function he(t,e){const r=1+t.form*5.2*.14100000000000001;if(!t.detail)return{base:r,substrate:r,fracture:r,pore:r};const i=t.chips*.001*D(e,.018)*5.2*(.6*22+.28*47+.12*93),o=1e3/t.spacing+5.2*(7*.35+17*.09),l=1e3/(t.spacing*4.2),s=t.bedding*.001*D(e,t.spacing*5e-4)*((.7+.92*t.layerBreakup)/.12*o+t.layerBreakup*11.95*l+t.layerBreakup*.18*(o/.22+29.15*l)),c=t.grain*.001*5.2*(D(e,t.grainSize*.001)*1e3/t.grainSize+.35*D(e,t.grainSize*5e-4)*2e3/t.grainSize),h=t.crackWidth*.001*D(e,t.crackWidth*.006),g=h*1.15,j=Math.max(1e-5,t.crackDepth*7e-4),d=r+i+s+c,v=1.6+g*t.crackChipping*2750+d*(.8*g/j+g*t.crackChipping*7.6/Math.max(j*.8,h*6,1e-5));return{base:r,substrate:d*1.1,fracture:Math.max(r,v)*1.1,pore:1+t.poreIrregularity*1.3}}function qe(t,e){const r=he(t,e);return Math.max(r.substrate,r.fracture,r.pore)}function pe(t,e,r,i){let o=(Math.imul(t,73856093)^Math.imul(e,19349663)^Math.imul(r,83492791)^Math.imul(i,2654435761))>>>0;return o=Math.imul(o^o>>>13,1274126177)>>>0,((o^o>>>16)>>>0)/4294967295}function se(t,e){const r=t.map(Math.floor),i=t.map((s,c)=>s-r[c]),o=i.map(s=>s*s*(3-2*s));let l=0;for(let s=0;s<2;s++)for(let c=0;c<2;c++)for(let h=0;h<2;h++)l+=pe(r[0]+h,r[1]+c,r[2]+s,e)*(h?o[0]:1-o[0])*(c?o[1]:1-o[1])*(s?o[2]:1-o[2]);return l*2-1}const E=(t,e)=>[t[0]*e,t[1]*e,t[2]*e],U=(t,e,r,i)=>[t[0]+e,t[1]+r,t[2]+i],Oe=(t,e,r)=>{const i=z(.5+.5*(t-e)/r,0,1);return e*(1-i)+t*i+r*i*(1-i)};function P(t,e){let r=(Math.hypot(t[0]/.3,t[1]/.245,t[2]/.27)-1)*.245;const i=.004+.025*(1-e.facets),o=.286-e.facets*.082;for(const l of[[.78,.2,.59],[-.8,.46,.39],[.1,.82,-.56],[-.2,-.68,-.7]]){const s=Math.hypot(...l);r=Oe(r,(t[0]*l[0]+t[1]*l[1]+t[2]*l[2])/s-o,i)}return r+=e.form*(.016*se(E(t,4),e.seed)+.007*se(U(E(t,11),13,7,3),e.seed)),Math.max(r,-t[1]-.235)}const J=56,L=(t,e)=>[t[0]+e[0],t[1]+e[1],t[2]+e[2]],$=(t,e)=>[t[0]-e[0],t[1]-e[1],t[2]-e[2]],oe=(t,e)=>t[0]*e[0]+t[1]*e[1]+t[2]*e[2],A=t=>E(t,1/Math.max(1e-12,Math.hypot(...t))),Y=(t,e)=>[t[1]*e[2]-t[2]*e[1],t[2]*e[0]-t[0]*e[2],t[0]*e[1]-t[1]*e[0]],H=t=>t.map(Math.fround);function V(t,e){const r=A(t);let i=0,o=.46;for(let l=0;l<24;l++){const s=(i+o)*.5;P(E(r,s),e)>0?o=s:i=s}return H(E(r,(i+o)*.5))}function Z(t,e){return A([P(U(t,3e-4,0,0),e)-P(U(t,-3e-4,0,0),e),P(U(t,0,3e-4,0),e)-P(U(t,0,-3e-4,0),e),P(U(t,0,0,3e-4),e)-P(U(t,0,0,-3e-4),e)])}const G=new Map;function Ge(t){const e=[t.seed,t.form,t.facets,t.crackSpacing,t.crackBranching].join(":"),r=G.get(e);if(r)return r;const i=[],o=(d,v)=>pe(d,v,381,t.seed+701),l=(d,v,R,w,N,b)=>{if(i.length>=J)return;const y=A($(v,d));let C=A(L(Z(d,t),Z(v,t)));C=A($(C,E(y,oe(C,y)))),i.push({a:H(d),b:H(v),normal:H(C),widthA:Math.fround(R),widthB:Math.fround(w),depth:Math.fround(.7+o(i.length,33)*.35),parent:N,branch:b})},s=z(Math.round(450/t.crackSpacing),3,4);for(let d=0;d<s;d++){const v=.62+d*2.399963+(o(d,1)-.5)*.6,R=A([Math.sin(v),.14+o(d,2)*.58,Math.cos(v)]),w=A(Y([0,1,0],R)),N=Y(R,w),b=(o(d,3)-.5)*2.3,y=L(E(w,Math.cos(b)),E(N,Math.sin(b))),C=Y(R,y),I=(.53+o(d,4)*.26)*z(110/t.crackSpacing,.65,1.35),S=[],p=Array.from({length:11},(m,T)=>(.035+.965*Math.pow(Math.max(0,Math.sin(T/10*Math.PI)),.7))*(.78+o(d,7)*.35)),q=o(d,8)*6.2831853;for(let m=0;m<11;m++){const T=(m-5)/5,F=Math.sin(m*.47+q)*.065+(o(d*13+m,9)-.5)*.045;S.push(V(L(L(R,E(y,T*I)),E(C,F)),t))}for(let m=0;m<10;m++)l(S[m],S[m+1],p[m],p[m+1],d,!1);if(t.crackBranching>0)for(let m=0;m<2;m++){const T=m*4+3,F=S[T],O=Z(F,t);let _=A($(S[T+1],S[T]));_=A($(_,E(O,oe(_,O))));const n=Y(O,_),x=(.52+o(d*3+m,19)*.5)*(m===0?1:-1),M=L(E(_,Math.cos(x)),E(n,Math.sin(x))),te=.22+o(d*3+m,21)*.18,ae=V(L(A(F),E(M,te*.6)),t),fe=V(L(L(A(F),E(M,te)),E(n,(o(d,24)-.5)*.11)),t),W=p[T]*t.crackBranching*(.38+o(d*3+m,27)*.2);l(F,ae,W,W*.55,d,!0),l(ae,fe,W*.55,W*.035,d,!0)}}const c=new Float32Array(J*4),h=new Float32Array(c.length),g=new Float32Array(c.length);i.forEach((d,v)=>{c.set([...d.a,d.widthA],v*4),h.set([...d.b,d.widthB],v*4),g.set([...d.normal,d.depth],v*4)});const j={key:e,segments:i,a:c,b:h,normals:g};return G.size>=12&&G.delete(G.keys().next().value),G.set(e,j),j}const We=`// Analytic 3D material field. The only texture in this lab is the color CLUT.
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
// @include structure
float detailStone(vec3 p,bool enabled) {
  float base=baseStone(p);if(!enabled){stoneStepBound=base/uBounds.y;return base;}
  vec3 q=vec3(p.x*.8+p.z*.6,p.y,-p.x*.6+p.z*.8);
  float d=base;
  d+=uMeso.x*stoneWeight(.018)*(.6*noiseStone(q*22.)+.28*noiseStone(q*47.+vec3(11,3,5))+.12*abs(noiseStone(q*93.+vec3(3,19,7))));
  if(uMeso.y>0.) d+=beddingStone(p);
  if(uMicro.y>0.) {
    float f=1./uMicro.z;
    float w1=stoneWeight(uMicro.z),w2=stoneWeight(uMicro.z*.5);
    if(w1>0.)d+=uMicro.y*w1*noiseStone(q*f+vec3(4,7,1));
    if(w2>0.)d+=uMicro.y*.35*w2*noiseStone(q*f*2.+vec3(17,3,13));
  }
  stoneStepBound=d/uFieldBounds.x;
  if(uFracture.x>0. && d<maxCrackMouth()) {
    float cut=fractureStone(p,d);
    d=max(d,-cut);stoneStepBound=max(stoneStepBound,-cut/uFieldBounds.y);
  }
  if(uFracture.w>0. && d<uMicro.x*.32) {
    float cut=poresStone(p);
    d=max(d,-cut);stoneStepBound=max(stoneStepBound,-cut/uFieldBounds.z);
  }
  stoneStepBound=max(stoneStepBound,-p.y-.235);
  return max(d,-p.y-.235);
}
`,Xe=`// Irregular stratigraphy: ordered jittered boundaries, not sine-wave ribs.
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
`,Ke=`#version 300 es
void main(){float x=float((gl_VertexID<<1)&2);float y=float(gl_VertexID&2);gl_Position=vec4(x*2.-1.,y*2.-1.,0.,1.);}`,$e=`#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
uniform vec4 uEye,uRight,uUp,uForward,uShape,uMeso,uFracture,uMicro,uMaterial,uLight,uDisplay,uBounds,uProbe;
uniform vec4 uStructure,uFieldBounds;
#define STONE_CRACK_SEGMENTS ${J}
uniform vec4 uCrackA[STONE_CRACK_SEGMENTS],uCrackB[STONE_CRACK_SEGMENTS],uCrackN[STONE_CRACK_SEGMENTS];
uniform int uCrackCount;
uniform sampler2D uPalette;
float stoneStepBound=0.;
out vec4 fragColor;
${We.replace("// @include structure",Xe)}
vec3 sunDirection(){return normalize(vec3(cos(uLight.x)*cos(uLight.y),sin(uLight.y),sin(uLight.x)*cos(uLight.y)));}
vec3 stoneGradient(vec3 p,bool detailed,float epsilon){
  vec2 e=vec2(epsilon,0.);
  return vec3(detailStone(p+e.xyy,detailed)-detailStone(p-e.xyy,detailed),detailStone(p+e.yxy,detailed)-detailStone(p-e.yxy,detailed),detailStone(p+e.yyx,detailed)-detailStone(p-e.yyx,detailed))/(2.*epsilon);
}
float conservativeStep(vec3 p,bool detailed){
  float base=baseStone(p);
  if(!detailed)return base/uBounds.y;
  if(base>uBounds.x+.001)return (base-uBounds.x)/uBounds.y;
  detailStone(p,true);return stoneStepBound;
}
vec3 traceStone(vec3 ro,vec3 rd,bool detailed,float limit){
  float b=dot(ro,rd),disc=b*b-dot(ro,ro)+.48*.48;
  if(disc<0.)return vec3(-1.,0.,0.);
  float begin=max(0.,-b-sqrt(disc)),end=min(limit,-b+sqrt(disc));
  if(begin>end)return vec3(-1.,0.,0.);
  float t=begin,lastT=t,lastD=1.,eps=uBounds.w;
  float previousSafe=0.,attemptedStep=0.;bool relaxed=false;
  for(int i=0;i<600;i++){
    if(i>=int(uLight.w))return vec3(-1.,float(i),1.);
    vec3 p=ro+rd*t;float base=baseStone(p);
    float d=(detailed&&base>uBounds.x+.001)?base-uBounds.x:detailStone(p,detailed);
    float advance=detailed&&base<=uBounds.x+.001?stoneStepBound:d/uBounds.y;
    // Verified over-relaxation: accept a longer step only if its two empty
    // distance bounds overlap. Otherwise backtrack before accepting any hit.
    if(relaxed && previousSafe+max(advance,0.)<attemptedStep*.9999){
      t=lastT+previousSafe;relaxed=false;continue;
    }
    if(abs(d)<eps)return vec3(t,float(i),0.);
    if(d<0. && lastD>0.) {
      float lo=lastT,hi=t;
      for(int j=0;j<9;j++){float mid=(lo+hi)*.5;if(detailStone(ro+rd*mid,detailed)>0.)lo=mid;else hi=mid;}
      return vec3((lo+hi)*.5,float(i),0.);
    }
    if(t>=end)return vec3(-1.,float(i),0.);
    lastT=t;lastD=d;
    previousSafe=max(eps*.28,abs(advance)*.85);
    attemptedStep=min(previousSafe*1.55,end-t);relaxed=true;t+=attemptedStep;
  }
  return vec3(-1.,600.,1.);
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
}`,le=t=>{const e=Math.hypot(...t)||1;return t.map(r=>r/e)},ce=(t,e)=>[t[1]*e[2]-t[2]*e[1],t[2]*e[0]-t[0]*e[2],t[0]*e[1]-t[1]*e[0]];class Ye{constructor(e,r,i){u(this,"gl");u(this,"program");u(this,"palette");u(this,"target");u(this,"framebuffer");u(this,"locations",new Map);u(this,"observer");u(this,"abort",new AbortController);u(this,"frame",0);u(this,"fence",null);u(this,"submitted",0);u(this,"dirty",!0);u(this,"disposed",!1);u(this,"lost",!1);u(this,"paletteKey","");u(this,"crackKey","");u(this,"fractureSegments",0);u(this,"width",0);u(this,"height",0);u(this,"yaw",.65);u(this,"pitch",.34);u(this,"distance",1.1);u(this,"down",null);u(this,"smooth",!1);u(this,"time",0);u(this,"draws",0);u(this,"footprint",0);u(this,"tick",e=>{if(this.frame=0,this.disposed||this.lost||document.hidden)return;const r=this.gl;if(this.fence){const i=r.clientWaitSync(this.fence,0,0);if(i===r.TIMEOUT_EXPIRED){if(e-this.submitted>3e4){this.report("The SDF draw timed out. Reload and use Draft quality.");return}this.frame=requestAnimationFrame(this.tick);return}if(r.deleteSync(this.fence),this.fence=null,i===r.WAIT_FAILED){this.report("Graphics synchronization failed. Reload the material editor.");return}this.report()}if(this.settings.turntable&&(this.time&&(this.yaw+=Math.min((e-this.time)/1e3,.05)*.18),this.dirty=!0),this.time=e,this.dirty)try{this.draw(this.settings,!0),this.dirty=!1,this.fence=r.fenceSync(r.SYNC_GPU_COMMANDS_COMPLETE,0),r.flush(),this.submitted=e}catch(i){this.report(i instanceof Error?i.message:String(i));return}(this.fence||this.settings.turntable)&&(this.frame=requestAnimationFrame(this.tick))});this.canvas=e,this.settings=r,this.status=i;const o=e.getContext("webgl2",{alpha:!1,antialias:!1,preserveDrawingBuffer:!0,powerPreference:"high-performance"});if(!o)throw new Error("WebGL2 is unavailable. Enable browser graphics acceleration to use the SDF material editor.");this.gl=o,this.initializeGPU();const l=this.abort.signal;e.addEventListener("webglcontextlost",c=>{c.preventDefault(),this.lost=!0,this.report("Graphics context lost. Reload the material editor to reconnect.")},{signal:l}),e.addEventListener("webglcontextrestored",()=>{try{this.lost=!1,this.initializeGPU(),this.request()}catch(c){this.report(String(c))}},{signal:l}),e.addEventListener("pointerdown",c=>{c.button>2||(e.focus(),e.setPointerCapture(c.pointerId),this.down={x:c.clientX,y:c.clientY,id:c.pointerId})},{signal:l}),e.addEventListener("pointermove",c=>{!this.down||c.pointerId!==this.down.id||(this.yaw-=(c.clientX-this.down.x)*.006,this.pitch=z(this.pitch+(c.clientY-this.down.y)*.005,.06,1.32),this.down={x:c.clientX,y:c.clientY,id:c.pointerId},this.request())},{signal:l});const s=()=>{this.down=null};e.addEventListener("pointerup",s,{signal:l}),e.addEventListener("pointercancel",s,{signal:l}),e.addEventListener("lostpointercapture",s,{signal:l}),e.addEventListener("contextmenu",c=>c.preventDefault(),{signal:l}),e.addEventListener("wheel",c=>{c.preventDefault(),this.distance=z(this.distance*Math.exp(c.deltaY*.0011),.46,3),this.request()},{signal:l,passive:!1}),e.addEventListener("keydown",c=>{c.key.toLowerCase()==="f"&&(c.preventDefault(),this.resetCamera()),c.key.toLowerCase()==="c"&&(this.smooth=!0,this.request())},{signal:l}),window.addEventListener("keyup",c=>{c.key.toLowerCase()==="c"&&(this.smooth=!1,this.request())},{signal:l}),window.addEventListener("blur",()=>{this.smooth=!1,this.down=null,this.request()},{signal:l}),document.addEventListener("visibilitychange",()=>{this.time=0,document.hidden||this.request()},{signal:l}),this.observer=new ResizeObserver(()=>this.request()),this.observer.observe(e),this.request()}initializeGPU(){const e=this.gl,r=(l,s)=>{const c=e.createShader(l);if(e.shaderSource(c,s),e.compileShader(c),!e.getShaderParameter(c,e.COMPILE_STATUS)){const h=e.getShaderInfoLog(c);throw e.deleteShader(c),new Error(h||"Stone shader compilation failed.")}return c},i=r(e.VERTEX_SHADER,Ke);let o;try{o=r(e.FRAGMENT_SHADER,$e)}catch(l){throw e.deleteShader(i),l}if(this.program=e.createProgram(),e.attachShader(this.program,i),e.attachShader(this.program,o),e.linkProgram(this.program),e.deleteShader(i),e.deleteShader(o),!e.getProgramParameter(this.program,e.LINK_STATUS))throw new Error(e.getProgramInfoLog(this.program)||"Stone shader linking failed.");this.locations.clear();for(const l of["uEye","uRight","uUp","uForward","uShape","uMeso","uFracture","uMicro","uMaterial","uLight","uDisplay","uBounds","uProbe","uStructure","uFieldBounds","uCrackA[0]","uCrackB[0]","uCrackN[0]","uCrackCount"])this.locations.set(l,e.getUniformLocation(this.program,l));this.palette=e.createTexture(),e.activeTexture(e.TEXTURE0),e.bindTexture(e.TEXTURE_2D,this.palette),e.texParameteri(e.TEXTURE_2D,e.TEXTURE_MIN_FILTER,e.LINEAR),e.texParameteri(e.TEXTURE_2D,e.TEXTURE_MAG_FILTER,e.LINEAR),e.texParameteri(e.TEXTURE_2D,e.TEXTURE_WRAP_S,e.CLAMP_TO_EDGE),e.texParameteri(e.TEXTURE_2D,e.TEXTURE_WRAP_T,e.CLAMP_TO_EDGE),e.texImage2D(e.TEXTURE_2D,0,e.SRGB8_ALPHA8,256,1,0,e.RGBA,e.UNSIGNED_BYTE,null),e.useProgram(this.program),e.uniform1i(e.getUniformLocation(this.program,"uPalette"),0),this.target=e.createTexture(),this.framebuffer=e.createFramebuffer(),this.width=this.height=0,this.paletteKey="",this.crackKey="",this.fence=null,this.dirty=!0}update(e){this.settings=e,this.request()}zoomBy(e){this.distance=z(this.distance*e,.46,3),this.request()}resetCamera(){this.yaw=.65,this.pitch=.34,this.distance=1.1,this.request()}setCamera(e,r,i){this.yaw=e,this.pitch=z(r,.06,1.32),this.distance=z(i,.46,3),this.request()}request(){this.dirty=!0,!this.frame&&!this.disposed&&!this.lost&&(this.frame=requestAnimationFrame(this.tick))}report(e=""){this.status({ready:!e,width:this.width,height:this.height,error:e,footprint:this.footprint,draws:this.draws})}dimensions(e){const r=Math.max(1,this.canvas.clientWidth),i=Math.max(1,this.canvas.clientHeight),o={draft:320,balanced:600,closeup:900}[e.quality],l=Math.min(devicePixelRatio||1,2,o/Math.sqrt(r*i));return[Math.max(1,Math.round(r*l)),Math.max(1,Math.round(i*l))]}bindTarget(e,r){const i=this.gl;if(this.width!==e||this.height!==r){if(i.activeTexture(i.TEXTURE1),i.bindTexture(i.TEXTURE_2D,this.target),i.texParameteri(i.TEXTURE_2D,i.TEXTURE_MIN_FILTER,i.LINEAR),i.texParameteri(i.TEXTURE_2D,i.TEXTURE_MAG_FILTER,i.LINEAR),i.texParameteri(i.TEXTURE_2D,i.TEXTURE_WRAP_S,i.CLAMP_TO_EDGE),i.texParameteri(i.TEXTURE_2D,i.TEXTURE_WRAP_T,i.CLAMP_TO_EDGE),i.texImage2D(i.TEXTURE_2D,0,i.RGBA8,e,r,0,i.RGBA,i.UNSIGNED_BYTE,null),i.bindFramebuffer(i.FRAMEBUFFER,this.framebuffer),i.framebufferTexture2D(i.FRAMEBUFFER,i.COLOR_ATTACHMENT0,i.TEXTURE_2D,this.target,0),i.checkFramebufferStatus(i.FRAMEBUFFER)!==i.FRAMEBUFFER_COMPLETE)throw new Error("Could not allocate the material preview.");this.width=e,this.height=r}else i.bindFramebuffer(i.FRAMEBUFFER,this.framebuffer);i.viewport(0,0,e,r)}uniforms(e,r,i,o){const l=this.gl,s=(C,I)=>l.uniform4fv(this.locations.get(C),I),c=[Math.sin(this.yaw)*Math.cos(this.pitch)*this.distance,Math.sin(this.pitch)*this.distance,Math.cos(this.yaw)*Math.cos(this.pitch)*this.distance],h=le(c.map(C=>-C)),g=le(ce(h,[0,1,0])),j=ce(g,h),d=.36;this.footprint=2*Math.max(.04,this.distance-.29)*d/i;const v=1+e.form*5.2*(.016*4+.007*11),R=(o??e.detail)&&!this.smooth,w={...e,detail:R};s("uEye",[...c,this.footprint]),s("uRight",[...g,d]),s("uUp",[...j,0]),s("uForward",[...h,r/i]),s("uShape",[e.seed,e.form,e.facets,R?1:0]),s("uMeso",[e.chips*.001,e.bedding*.001,e.spacing*.001,e.tilt*Math.PI/180]),s("uFracture",[e.crackDepth*.001,e.crackWidth*.001,e.crackSpacing*.001,e.porosity]),s("uMicro",[e.poreSize*.001,e.grain*.001,e.grainSize*.001,0]),s("uStructure",[e.layerBreakup,e.crackBranching,e.crackChipping,e.poreIrregularity]);const N=he(w,this.footprint);s("uFieldBounds",[N.substrate,N.fracture,N.pore,0]);const b=e.crackDepth>0?Ge(e):null,y=(b==null?void 0:b.key)??"off";y!==this.crackKey&&(this.crackKey=y,this.fractureSegments=(b==null?void 0:b.segments.length)??0,l.uniform1i(this.locations.get("uCrackCount"),this.fractureSegments),b&&(l.uniform4fv(this.locations.get("uCrackA[0]"),b.a),l.uniform4fv(this.locations.get("uCrackB[0]"),b.b),l.uniform4fv(this.locations.get("uCrackN[0]"),b.normals))),s("uMaterial",[e.roughness,e.contrast,e.bias,e.saturation]),s("uLight",[e.sunAzimuth*Math.PI/180,e.sunElevation*Math.PI/180,e.exposure,{draft:300,balanced:420,closeup:600}[e.quality]]),s("uDisplay",[r,i,ee.indexOf(e.view),e.compare?e.split:-1]),s("uBounds",[Ie(w,this.footprint),v,qe(w,this.footprint),z(this.footprint*.1,2e-5,15e-5)]),s("uProbe",[0,0,0,0])}draw(e,r){if(this.disposed||this.lost||this.gl.isContextLost())throw new Error("The graphics context is unavailable.");const i=this.gl,[o,l]=this.dimensions(e);this.bindTarget(o,l),i.useProgram(this.program),i.disable(i.BLEND),i.disable(i.DEPTH_TEST),i.disable(i.CULL_FACE),i.activeTexture(i.TEXTURE0),i.bindTexture(i.TEXTURE_2D,this.palette);const s=Q(e.palette);s.id!==this.paletteKey&&(i.texSubImage2D(i.TEXTURE_2D,0,0,0,256,1,i.RGBA,i.UNSIGNED_BYTE,be(s.palette)),this.paletteKey=s.id),this.uniforms(e,o,l),i.drawArrays(i.TRIANGLES,0,3),r&&(this.canvas.width!==o&&(this.canvas.width=o),this.canvas.height!==l&&(this.canvas.height=l),i.bindFramebuffer(i.READ_FRAMEBUFFER,this.framebuffer),i.bindFramebuffer(i.DRAW_FRAMEBUFFER,null),i.blitFramebuffer(0,0,o,l,0,0,o,l,i.COLOR_BUFFER_BIT,i.NEAREST)),this.draws++}async capture(e=this.settings.view,r=this.settings.detail){this.draw({...this.settings,view:e,detail:r},!1);const i=this.gl,o=new Uint8Array(this.width*this.height*4);i.bindFramebuffer(i.READ_FRAMEBUFFER,this.framebuffer),i.readPixels(0,0,this.width,this.height,i.RGBA,i.UNSIGNED_BYTE,o);const l=new Uint8ClampedArray(o.length),s=this.width*4;for(let g=0;g<this.height;g++)l.set(o.subarray(g*s,(g+1)*s),(this.height-1-g)*s);const c=document.createElement("canvas");c.width=this.width,c.height=this.height,c.getContext("2d").putImageData(new ImageData(l,this.width,this.height),0,0);const h=await new Promise(g=>c.toBlob(g,"image/png"));if(this.request(),!h)throw new Error("Could not encode the preview PNG.");return h}probe(e,r=!0){const i=this.gl,[o,l]=this.dimensions(this.settings);this.bindTarget(o,l),i.useProgram(this.program),i.activeTexture(i.TEXTURE0),i.bindTexture(i.TEXTURE_2D,this.palette),this.uniforms(this.settings,o,l,r),i.viewport(0,0,1,1);const s=new Uint8Array(4),c=[];for(const h of e)i.uniform4fv(this.locations.get("uProbe"),[...h,1]),i.drawArrays(i.TRIANGLES,0,3),i.readPixels(0,0,1,1,i.RGBA,i.UNSIGNED_BYTE,s),c.push((s[0]*65536+s[1]*256+s[2])/16777215*2-1);return this.request(),c}getDiagnostics(){return{backend:"WebGL2",width:this.width,height:this.height,footprint:this.footprint,draws:this.draws,materialTextures:1,fractureSegments:this.fractureSegments,structure:"irregular sheets / connected finite fractures / organic vesicles",texturePurpose:"256×1 color CLUT only",detailSource:"analytic 3D signed field",glError:this.gl.getError(),contextLost:this.gl.isContextLost()}}dispose(){this.disposed=!0,cancelAnimationFrame(this.frame),this.abort.abort(),this.observer.disconnect(),this.fence&&this.gl.deleteSync(this.fence),this.gl.deleteTexture(this.palette),this.gl.deleteTexture(this.target),this.gl.deleteFramebuffer(this.framebuffer),this.gl.deleteProgram(this.program)}}const me="frontier.sdf-material-lab.v1",He={lit:"Surface",clay:"Clay",albedo:"Albedo",normals:"Normals",relief:"SDF relief",silhouette:"Silhouette",steps:"March steps"};function de(t,e){const r=URL.createObjectURL(t),i=document.createElement("a");i.href=r,i.download=e,i.click(),setTimeout(()=>URL.revokeObjectURL(r),1e3)}function Ve(){let t={...B};try{const r=localStorage.getItem(me);r&&(t=ue(JSON.parse(r)))}catch{}return new URLSearchParams(location.search).get("quality")==="draft"&&(t.quality="draft"),t}function f({label:t,value:e,min:r,max:i,step:o=1,unit:l="",scale:s=1,onChange:c,disabled:h=!1}){return a.jsxs("label",{className:`lab-slider ${h?"is-disabled":""}`,children:[a.jsxs("span",{className:"lab-slider-label",children:[t,a.jsxs("span",{className:"lab-number",children:[a.jsx("input",{"aria-label":`${t} numeric value`,type:"number",min:r*s,max:i*s,step:o*s,value:Number((e*s).toFixed(3)),disabled:h,onChange:g=>{const j=g.target.valueAsNumber;Number.isFinite(j)&&c(z(j/s,r,i))}}),a.jsx("small",{children:l})]})]}),a.jsx("input",{"aria-label":t,type:"range",min:r,max:i,step:o,value:e,disabled:h,onChange:g=>c(Number(g.target.value))})]})}function Ze(){const[t,e]=k.useState(Ve),[r,i]=k.useState(()=>{var n;return((n=K.find(x=>["seed","form","facets","chips","bedding","layerBreakup","spacing","tilt","crackDepth","crackWidth","crackSpacing","crackBranching","crackChipping","porosity","poreSize","poreIrregularity","grain","grainSize","palette"].every(M=>x.values[M]===t[M])))==null?void 0:n.id)||"custom"}),[o,l]=k.useState(!1),[s,c]=k.useState({ready:!1,width:0,height:0,error:"",footprint:0,draws:0}),[h,g]=k.useState(""),[j,d]=k.useState(""),[v,R]=k.useState(!1),[w,N]=k.useState(null),b=k.useRef(null),y=k.useRef(null),C=k.useRef(t),I=k.useRef(null);C.current=t,k.useEffect(()=>{try{const n=new Ye(b.current,C.current,c);return y.current=n,()=>{n.dispose(),y.current=null}}catch(n){c(x=>({...x,error:n instanceof Error?n.message:String(n)}))}},[]),k.useEffect(()=>{var n;return(n=y.current)==null?void 0:n.update(t)},[t]),k.useEffect(()=>{if(!j)return;const n=setTimeout(()=>d(""),6e3);return()=>clearTimeout(n)},[j]);const S=(n,x)=>{e(M=>({...M,[n]:x})),l(!0)},p=n=>x=>S(n,x),q=K.find(n=>n.id===r),m=Q(t.palette),T=ye(h),F=async()=>{if(y.current){R(!0);try{de(await y.current.capture(),`frontier-stone-${t.seed}-${t.view}.png`)}catch(n){d(String(n))}finally{R(!1)}}},O=()=>{try{localStorage.setItem(me,ne(t)),d("Recipe saved in this browser. The terrain project is untouched."),l(!1)}catch{d("Browser storage is unavailable. Use Export recipe instead.")}},_=async n=>{if(n)try{if(n.size>64*1024)throw new Error("Stone recipes must be smaller than 64 KB.");const x=ue(JSON.parse(await n.text()));e(x),i("custom"),l(!1),d("Stone recipe loaded. No image detail maps involved.")}catch(x){d(x instanceof Error?x.message:String(x))}};return a.jsxs("div",{className:"material-lab",children:[a.jsxs("header",{className:"lab-header",children:[a.jsxs("a",{className:"lab-brand",href:"./index.html",children:[a.jsx(Se,{size:23}),a.jsx("strong",{children:"FRONTIER"})]}),a.jsx("span",{className:"lab-header-divider"}),a.jsxs("div",{className:"lab-page-title",children:[a.jsx("span",{children:"Material editor"}),a.jsxs("small",{children:["SDF SURFACE LAB ",a.jsx("i",{})," EXPERIMENTAL"]})]}),a.jsxs("div",{className:"lab-header-actions",children:[a.jsxs("a",{className:"lab-terrain-link",href:"./index.html",children:["Terrain studio ",a.jsx(ie,{size:13})]}),a.jsx("button",{onClick:O,className:"lab-icon-button","aria-label":"Save stone recipe",title:"Save recipe locally",children:a.jsx(we,{size:17})}),a.jsxs("button",{className:"lab-primary",onClick:()=>void F(),disabled:!s.ready||!!s.error||v,children:[a.jsx(re,{size:14}),v?"Capturing…":"Export PNG"]})]})]}),a.jsxs("div",{className:"lab-mobile-nav",children:[a.jsx("button",{className:w==="specimen"?"selected":"",onClick:()=>N(w==="specimen"?null:"specimen"),children:"Specimens"}),a.jsx("button",{className:w===null?"selected":"",onClick:()=>N(null),children:"Viewport"}),a.jsx("button",{className:w==="controls"?"selected":"",onClick:()=>N(w==="controls"?null:"controls"),children:"Surface controls"})]}),a.jsxs("main",{className:"lab-workspace",children:[a.jsxs("aside",{className:`lab-presets ${w==="specimen"?"mobile-open":""}`,"aria-label":"Stone specimens",children:[a.jsxs("div",{className:"lab-panel-title",children:[a.jsx("span",{children:"Specimens"}),a.jsx("small",{children:"01—06"})]}),a.jsxs("p",{className:"lab-panel-note",children:["One organic form.",a.jsx("br",{}),"A different surface story."]}),a.jsx("div",{className:"lab-specimen-list",children:K.map((n,x)=>a.jsxs("button",{className:r===n.id?"selected":"","aria-pressed":r===n.id,onClick:()=>{e(M=>({...n.values,quality:M.quality,view:M.view,compare:M.compare,split:M.split,turntable:M.turntable})),i(n.id),l(!1),N(null)},children:[a.jsxs("div",{className:"lab-specimen-graphic",style:{"--stone-color":n.color},children:[a.jsx("span",{}),a.jsxs("small",{children:["0",x+1]}),r===n.id&&a.jsx(Ee,{size:12})]}),a.jsxs("span",{className:"lab-specimen-text",children:[a.jsx("strong",{children:n.name}),a.jsx("small",{children:n.category})]})]},n.id))}),a.jsxs("div",{className:"lab-recipe-actions",children:[a.jsxs("button",{onClick:()=>{de(new Blob([ne(t)],{type:"application/json"}),`frontier-stone-${t.seed}.json`)},children:[a.jsx(re,{size:13})," Export recipe"]}),a.jsxs("button",{onClick:()=>{var n;return(n=I.current)==null?void 0:n.click()},children:[a.jsx(je,{size:13})," Import recipe"]}),a.jsx("input",{ref:I,hidden:!0,type:"file",accept:".json,application/json","aria-label":"Import stone recipe",onChange:n=>{var x;_((x=n.target.files)==null?void 0:x[0]),n.target.value=""}})]}),a.jsxs("div",{className:"lab-side-footnote",children:[a.jsx(ke,{size:16}),a.jsxs("p",{children:["Not a normal-map preview.",a.jsx("br",{}),"The ray hits the detailed",a.jsx("br",{}),"signed surface itself."]})]})]}),a.jsxs("section",{className:"lab-stage","aria-label":"SDF stone preview",children:[a.jsxs("div",{className:"lab-stage-heading",children:[a.jsxs("div",{children:[a.jsxs("small",{children:["SPECIMEN /"," ",r==="custom"?"CUSTOM":String(K.findIndex(n=>n.id===r)+1).padStart(2,"0")]}),a.jsxs("h1",{children:[(q==null?void 0:q.name)??"Custom stone",a.jsx("span",{children:o?"Modified":""})]})]}),a.jsxs("span",{className:"lab-renderer-tag",children:[a.jsx("i",{}),"WEBGL2"]})]}),a.jsx("div",{className:"lab-view-tabs",role:"group","aria-label":"Stone inspection views",children:ee.map(n=>a.jsx("button",{"aria-pressed":t.view===n,onClick:()=>S("view",n),children:He[n]},n))}),a.jsxs("div",{className:"lab-canvas-wrap",children:[a.jsx("canvas",{ref:b,className:"lab-canvas",tabIndex:0,"aria-label":"Interactive SDF stone. Drag to orbit, scroll to zoom, F to frame, hold C for the smooth form."}),a.jsxs("div",{className:"lab-canvas-tag",children:[a.jsx(Ce,{size:12}),a.jsx("span",{children:"SIGNED SURFACE DETAIL"}),a.jsx("small",{children:"SatMap color only · no image relief"})]}),!s.ready&&!s.error&&a.jsx("div",{className:"lab-loading",children:"Tracing the stone surface…"}),s.error&&a.jsxs("div",{className:"lab-error",role:"alert",children:[a.jsx("strong",{children:"Graphics needs attention"}),a.jsx("p",{children:s.error}),a.jsx("button",{onClick:()=>location.reload(),children:"Reload material editor"})]}),t.compare&&a.jsxs(a.Fragment,{children:[a.jsx("div",{className:"lab-split-line",style:{left:`${t.split*100}%`}}),a.jsx("span",{className:"lab-split-label left",children:"SMOOTH FORM"}),a.jsx("span",{className:"lab-split-label right",children:"SDF DETAIL"})]}),a.jsxs("div",{className:"lab-view-tools",children:[a.jsx("button",{"aria-label":"Zoom into stone",onClick:()=>{var n;return(n=y.current)==null?void 0:n.zoomBy(.8)},children:a.jsx(Te,{size:15})}),a.jsx("button",{"aria-label":"Zoom out from stone",onClick:()=>{var n;return(n=y.current)==null?void 0:n.zoomBy(1.25)},children:a.jsx(Me,{size:15})}),a.jsx("button",{className:t.turntable?"selected":"","aria-pressed":t.turntable,"aria-label":"Rotate stone automatically",onClick:()=>S("turntable",!t.turntable),children:a.jsx(Re,{size:14})}),a.jsx("button",{"aria-label":"Frame stone",onClick:()=>{var n;return(n=y.current)==null?void 0:n.resetCamera()},children:a.jsx(Ue,{size:16})})]}),a.jsxs("div",{className:"lab-orbit-help",children:["DRAG Orbit ",a.jsx("i",{})," SCROLL Zoom ",a.jsx("i",{})," HOLD C Smooth form"]})]}),a.jsxs("div",{className:"lab-stage-bottom",children:[a.jsxs("label",{className:"lab-check",children:[a.jsx("input",{type:"checkbox",checked:t.compare,onChange:n=>S("compare",n.target.checked)}),a.jsx("span",{children:"Compare smooth / detailed"})]}),t.compare&&a.jsx("input",{className:"lab-split-range","aria-label":"Comparison split",type:"range",min:.05,max:.95,step:.01,value:t.split,onChange:n=>S("split",Number(n.target.value))}),a.jsxs("span",{className:"lab-resolution",children:[s.width," × ",s.height," ",a.jsx("i",{})," ",s.footprint?(s.footprint*1e3).toFixed(2):"—"," mm footprint"]})]}),a.jsxs("div",{className:"lab-proof-note",children:[a.jsx(Ne,{size:14}),a.jsx("span",{children:t.view==="silhouette"?"White is the actual ray-hit silhouette. Turn detail off to compare the boundary.":t.view==="relief"?"Blue = inward carving. Warm = outward relief. Derived from the signed field at the hit point.":t.view==="steps"?"Mint shows raymarch work. Red marks an exhausted step budget—not a valid surface hit.":"Inspect in Clay or Silhouette to separate geometric detail from the SatMap colors."})]})]}),a.jsxs("aside",{className:`lab-controls ${w==="controls"?"mobile-open":""}`,"aria-label":"SDF surface controls",children:[a.jsxs("div",{className:"lab-panel-title",children:[a.jsx("span",{children:"Surface stack"}),a.jsx("button",{className:"lab-icon-button","aria-label":"Reset stone material",onClick:()=>{e(n=>({...B,quality:n.quality})),i("sandstone"),l(!1)},children:a.jsx(ze,{size:14})})]}),a.jsxs("div",{className:"lab-color-card",children:[a.jsx("small",{children:"BASE COLOR / SATMAP"}),a.jsx("strong",{children:m.name}),a.jsx("div",{className:"lab-palette-ramp",style:{background:Ae(m.palette)}}),a.jsx("input",{className:"lab-palette-search","aria-label":"Search stone palettes",placeholder:`Search ${Be.length} palettes…`,value:h,onChange:n=>g(n.target.value)}),a.jsxs("select",{"aria-label":"Stone SatMap palette",value:T.some(n=>n.id===t.palette)?t.palette:"",onChange:n=>{n.target.value&&S("palette",n.target.value)},children:[!T.some(n=>n.id===t.palette)&&a.jsx("option",{value:"",children:T.length?"Choose a matching palette":"No matching palettes"}),T.map(n=>a.jsxs("option",{value:n.id,children:[n.category," · ",n.name]},n.id))]}),a.jsxs("span",{className:"lab-palette-origin",children:[m.origin==="satellite"?"Satellite-derived CLUT":m.origin==="fantasy"?"Fantasy · authored CLUT":"Terrain-inspired · authored CLUT"," ","· COLOR ONLY"]})]}),a.jsxs("details",{className:"lab-control-section",open:!0,children:[a.jsxs("summary",{children:[a.jsx("span",{children:"01"})," Organic form ",a.jsx(X,{size:12})]}),a.jsx(f,{label:"Form irregularity",value:t.form,min:0,max:1,step:.01,scale:100,unit:"%",onChange:p("form")}),a.jsx(f,{label:"Angular facets",value:t.facets,min:0,max:1,step:.01,scale:100,unit:"%",onChange:p("facets")}),a.jsxs("div",{className:"lab-seed",children:[a.jsxs("label",{children:["Seed",a.jsx("input",{type:"number","aria-label":"Stone seed",min:0,max:99999,value:t.seed,onChange:n=>{Number.isFinite(n.target.valueAsNumber)&&S("seed",Math.round(z(n.target.valueAsNumber,0,99999)))}})]}),a.jsx("button",{"aria-label":"New stone seed",onClick:()=>S("seed",crypto.getRandomValues(new Uint32Array(1))[0]%1e5),children:a.jsx(De,{size:14})})]})]}),a.jsxs("div",{className:"lab-detail-toggle",children:[a.jsxs("label",{className:"lab-check",children:[a.jsx("input",{type:"checkbox",checked:t.detail,onChange:n=>S("detail",n.target.checked)}),a.jsx("span",{children:"SDF surface detail"})]}),a.jsx("small",{children:"CHANGES THE ZERO SURFACE"})]}),a.jsxs("details",{className:"lab-control-section",open:!0,children:[a.jsxs("summary",{children:[a.jsx("span",{children:"02"})," Chips & cleavage ",a.jsx(X,{size:12})]}),a.jsx(f,{label:"Chipped relief",value:t.chips,min:0,max:12,step:.25,unit:"mm",onChange:p("chips"),disabled:!t.detail}),a.jsx(f,{label:"Crack depth",value:t.crackDepth,min:0,max:12,step:.25,unit:"mm",onChange:p("crackDepth"),disabled:!t.detail}),a.jsx(f,{label:"Crack aperture",value:t.crackWidth,min:.2,max:2,step:.05,unit:"mm",onChange:p("crackWidth"),disabled:!t.detail}),a.jsx(f,{label:"Crack branching",value:t.crackBranching,min:0,max:1,step:.01,scale:100,unit:"%",onChange:p("crackBranching"),disabled:!t.detail}),a.jsx(f,{label:"Fracture edge chipping",value:t.crackChipping,min:0,max:1,step:.01,scale:100,unit:"%",onChange:p("crackChipping"),disabled:!t.detail}),a.jsx(f,{label:"Crack spacing",value:t.crackSpacing,min:45,max:180,step:5,unit:"mm",onChange:p("crackSpacing"),disabled:!t.detail})]}),a.jsxs("details",{className:"lab-control-section",children:[a.jsxs("summary",{children:[a.jsx("span",{children:"03"})," Bedding ",a.jsx(X,{size:12})]}),a.jsx(f,{label:"Bedding relief",value:t.bedding,min:0,max:6,step:.1,unit:"mm",onChange:p("bedding"),disabled:!t.detail}),a.jsx(f,{label:"Layer spacing",value:t.spacing,min:8,max:60,step:1,unit:"mm",onChange:p("spacing"),disabled:!t.detail}),a.jsx(f,{label:"Sheet breakup",value:t.layerBreakup,min:0,max:1,step:.01,scale:100,unit:"%",onChange:p("layerBreakup"),disabled:!t.detail}),a.jsx("p",{children:"Uneven sheet thickness, chipped ledges and partial delamination—not wrapping sine-wave ribs."}),a.jsx(f,{label:"Bedding tilt",value:t.tilt,min:-70,max:70,step:1,unit:"°",onChange:p("tilt"),disabled:!t.detail})]}),a.jsxs("details",{className:"lab-control-section",open:!0,children:[a.jsxs("summary",{children:[a.jsx("span",{children:"04"})," Pores & grain ",a.jsx(X,{size:12})]}),a.jsx(f,{label:"Pore coverage",value:t.porosity,min:0,max:1,step:.01,scale:100,unit:"%",onChange:p("porosity"),disabled:!t.detail}),a.jsx(f,{label:"Pore spacing",value:t.poreSize,min:6,max:30,step:1,unit:"mm",onChange:p("poreSize"),disabled:!t.detail}),a.jsx(f,{label:"Pore irregularity",value:t.poreIrregularity,min:0,max:1,step:.01,scale:100,unit:"%",onChange:p("poreIrregularity"),disabled:!t.detail}),a.jsx("p",{children:"Rotated, elongated and joined vesicles with uneven walls. Zero returns to round pores."}),a.jsx(f,{label:"Grain relief",value:t.grain,min:0,max:1.5,step:.05,unit:"mm",onChange:p("grain"),disabled:!t.detail}),a.jsx(f,{label:"Grain size",value:t.grainSize,min:.6,max:6,step:.1,unit:"mm",onChange:p("grainSize"),disabled:!t.detail}),a.jsx("p",{children:"Zoom in to resolve finer grain. Sub-pixel bands fade out in the field, not into oversized noise."})]}),a.jsxs("details",{className:"lab-control-section",children:[a.jsxs("summary",{children:[a.jsx("span",{children:"05"})," Light & response ",a.jsx(Fe,{size:12})]}),a.jsx(f,{label:"Roughness",value:t.roughness,min:.3,max:1,step:.01,scale:100,unit:"%",onChange:p("roughness")}),a.jsx(f,{label:"Palette contrast",value:t.contrast,min:.5,max:2.5,step:.05,onChange:p("contrast")}),a.jsx(f,{label:"Palette bias",value:t.bias,min:-.4,max:.4,step:.01,onChange:p("bias")}),a.jsx(f,{label:"Saturation",value:t.saturation,min:0,max:1.5,step:.01,scale:100,unit:"%",onChange:p("saturation")}),a.jsx(f,{label:"Light azimuth",value:t.sunAzimuth,min:-180,max:180,step:1,unit:"°",onChange:p("sunAzimuth")}),a.jsx(f,{label:"Light elevation",value:t.sunElevation,min:12,max:80,step:1,unit:"°",onChange:p("sunElevation")}),a.jsx(f,{label:"Exposure",value:t.exposure,min:.5,max:1.8,step:.05,onChange:p("exposure")})]}),a.jsxs("label",{className:"lab-quality",children:["Preview budget",a.jsxs("select",{"aria-label":"Stone preview quality",value:t.quality,onChange:n=>S("quality",n.target.value),children:[a.jsx("option",{value:"draft",children:"Draft · lower GPU cost"}),a.jsx("option",{value:"balanced",children:"Balanced"}),a.jsx("option",{value:"closeup",children:"Close-up · more detail"})]})]}),a.jsxs("a",{className:"lab-docs",href:"./research/sdf-stone-materials.md",target:"_blank",rel:"noreferrer",children:["Method, limits & verification ",a.jsx(ie,{size:12})]})]})]}),a.jsxs("footer",{className:"lab-footer",children:[a.jsxs("span",{children:[a.jsx("i",{})," SDF GEOMETRY · NOT A SCANNED MATERIAL"]}),a.jsx("span",{children:"Separate experiment · terrain project unchanged"})]}),j&&a.jsxs("div",{className:"lab-toast",role:"status",children:[j,a.jsx("button",{"aria-label":"Dismiss message",onClick:()=>d(""),children:a.jsx(Le,{size:14})})]})]})}_e.createRoot(document.getElementById("material-root")).render(a.jsx(Ze,{}));
