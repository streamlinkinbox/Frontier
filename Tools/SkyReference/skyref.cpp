// Direct transcription of the reference celestial fragment shader's SKY SCOPE:
// atmosphere() 20x8, dawnGlow() incl. the white line, sun disc + aureole,
// planet ground shade, analytic media (height fog off / aerial perspective
// on, as the panel uploads), the gated star field incl. Milky Way, autoEV,
// ACES, vignette, gamma, grain. No moons/clouds/volumes/plane/flare/lights.
// Purpose: the panel-exact sky truth for Project Zero's parity gate.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

struct V3 { float x,y,z;
  V3 operator+(const V3&o)const{return{x+o.x,y+o.y,z+o.z};}
  V3 operator-(const V3&o)const{return{x-o.x,y-o.y,z-o.z};}
  V3 operator*(float s)const{return{x*s,y*s,z*s};}
  V3 operator*(const V3&o)const{return{x*o.x,y*o.y,z*o.z};}
  friend V3 operator*(float s,const V3&v){return{v.x*s,v.y*s,v.z*s};}
};
static float dot(const V3&a,const V3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static float len(const V3&a){return std::sqrt(dot(a,a));}
static V3 norm(const V3&a){float l=len(a);return a*(1.0f/std::max(l,1e-9f));}
static float clampf(float v,float a,float b){return v<a?a:(v>b?b:v);}
static float sstep(float e0,float e1,float x){float t=clampf((x-e0)/(e1-e0),0,1);return t*t*(3-2*t);}
static V3 mixv(const V3&a,const V3&b,float t){return a+(b-a)*t;}
static float mixf(float a,float b,float t){return a+(b-a)*t;}
static V3 expv(const V3&a){return{(float)std::exp(a.x),(float)std::exp(a.y),(float)std::exp(a.z)};}

static const float PI=3.14159265f, D2R=3.14159265f/180.0f;

// ---- uniforms (HTML defaults) ----
static float uSunTime=6.4f;
static V3 uSunDir; static V3 uSunColor; static float uSunIntensity=22, uSunAng, uSunSoft=0.25f, uSunDiscBoost=12;
static float uRayleigh=1,uMie=1,uMieG=0.78f,uOzone=1.2f,uPlanetR=6371*1000.f,uAtmoH=100*1000.f,uHr=8000,uHm=1200;
static V3 uSkyTint={1,1,1}; static V3 uGroundColor={0.16862746f,0.16078432f,0.14117648f};
static float uSkyBright=1,uGroundBright=1,uHLInt=1,uDawnInt=1,uHLAuto=1;
static float uEV=0.4f,uBloom=1,uVignette=0.28f,uGrain=0.1f;
static float uCamHeight=2;
static float uTime=0.5f;
static float uFogOn=0,uFogDensity=.011f,uFogHeight=42,uFogScatter=.7f;
static V3 uFogColor={0.5607843f,0.6431373f,0.73333335f};
static float uAFOn=1,uAFDensity=7e-5f,uAFHeight=1200,uAFStart=0,uAFMie=.35f,uAFG=.7f,uAFSky=1;
static V3 uAFTint={1,1,1};
static V3 uSkyAmb;
static float uStarsOn=1,uStarDensity=1,uStarBright=1,uStarSize=1,uStarGlow=.8f,uStarTwinkle=.5f;
static float uStarColor=1,uStarLimit=1.5f,uMilky=1,uStarRot=40*3.14159265f/180.0f,uMilkyTilt=-62*3.14159265f/180.0f;
static float uStarLayers=3,uStarAA=1,uStarCeil=8*2.6e-3f*1*3.2f*1.6f;
static V3 uCamFwd,uCamRight,uCamUp; static float uTanHalf;

static V3 sunDirAt(double t,double latDeg,double azOff){
  double lat=latDeg*3.14159265358979/180.0, HA=(t-12)/24*2*3.14159265358979;
  double sE=std::cos(lat)*std::cos(HA); double el=std::asin(std::max(-1.0,std::min(1.0,sE)));
  double az=std::atan2(std::sin(HA),std::cos(HA)*std::sin(lat))+3.14159265358979+azOff*3.14159265358979/180.0;
  return {(float)(std::sin(az)*std::cos(el)),(float)std::sin(el),(float)(-std::cos(az)*std::cos(el))};
}
static V3 kelvinRGB(float k){ k/=100; float r=k<=66?255:329.7f*std::pow(k-60,-0.133f),
  g=k<=66?99.47f*std::log(k)-161.1f:288.1f*std::pow(k-60,-0.0755f),
  b=k>=66?255:(k<=19?0:138.5f*std::log(k-10)-305); return {clampf(r,0,255)/255,clampf(g,0,255)/255,clampf(b,0,255)/255}; }

static void rsi(const V3&ro,const V3&rd,float r,float&o0,float&o1){
  float b=dot(ro,rd),c=dot(ro,ro)-r*r,d=b*b-c;
  if(d<0){o0=o1=-1;return;} d=std::sqrt(d); o0=-b-d; o1=-b+d;
}
static void atmosphere(const V3&ro,const V3&rd,V3&sky,V3&trans,float&ground){
  sky={0,0,0}; trans={1,1,1}; ground=0;
  float Ra=uPlanetR+uAtmoH, n,f; rsi(ro,rd,Ra,n,f); if(f<0) return;
  float t0=std::max(n,0.f),t1=f,gn,gf; rsi(ro,rd,uPlanetR,gn,gf);
  if(gn>0){t1=gn;ground=1;}
  const int N=20,NL=8; float L=t1-t0;
  V3 betaR={5.8e-6f*uRayleigh,13.5e-6f*uRayleigh,33.1e-6f*uRayleigh};
  V3 betaM={21e-6f*uMie,21e-6f*uMie,21e-6f*uMie};
  V3 betaO={0.65e-6f*uOzone,1.881e-6f*uOzone,0.085e-6f*uOzone};
  V3 sR={0,0,0},sM={0,0,0}; float odR=0,odM=0;
  float mu=dot(rd,uSunDir),g=uMieG;
  float pR=3/(16*PI)*(1+mu*mu);
  float pM=3/(8*PI)*((1-g*g)*(1+mu*mu))/((2+g*g)*std::pow(1+g*g-2*g*mu,1.5f));
  for(int i=0;i<N;i++){
    float s0=(float)i/N,s1=(float)(i+1)/N; s0*=s0;s1*=s1;
    float ta=t0+L*s0,tb=t0+L*s1,seg=tb-ta,tm=.5f*(ta+tb);
    V3 p=ro+rd*tm; float h=len(p)-uPlanetR;
    float hr=(float)std::exp(-h/uHr)*seg, hm=(float)std::exp(-h/uHm)*seg;
    odR+=hr;odM+=hm;
    float ln,lf; rsi(p,uSunDir,Ra,ln,lf); float lenL=lf,olR=0,olM=0; bool ok=true;
    for(int j=0;j<NL;j++){ float q0=(float)j/NL,q1=(float)(j+1)/NL; q0*=q0;q1*=q1;
      float segL=lenL*(q1-q0); V3 q=p+uSunDir*(lenL*.5f*(q0+q1)); float hq=len(q)-uPlanetR;
      if(hq<0){ok=false;break;} olR+=(float)std::exp(-hq/uHr)*segL; olM+=(float)std::exp(-hq/uHm)*segL; }
    if(ok){ V3 att=expv((betaR*(odR+olR)+betaM*(1.1f*(odM+olM))+betaO*(odR+olR))*-1);
      sR=sR+att*hr; sM=sM+att*hm; }
  }
  trans=expv((betaR*odR+betaM*(1.1f*odM)+betaO*odR)*-1);
  sky=(sR*betaR*pR+sM*betaM*pM)*uSunIntensity*uSunColor;
}
static V3 dawnGlow(const V3&dir,float elevDeg,float facing){
  float alt=(float)(std::asin(clampf(dir.y,-1,1))/D2R); if(alt<-2) return {0,0,0};
  float altp=std::max(alt,0.f);
  float dAz=(float)std::acos(clampf(facing*2-1,-1,1));
  float az=(float)std::exp(-std::pow(dAz/.95f,2)), azW=(float)std::exp(-std::pow(dAz/1.8f,2));
  float tw=sstep(-16,-5,elevDeg)*(1-sstep(.5f,6,elevDeg));
  float depth=clampf(-elevDeg/10,0,1);
  V3 c0={1,.88f,.62f},c1={1,.62f,.28f},c2={.95f,.42f,.30f},c3={.62f,.36f,.48f},c4={.25f,.30f,.58f};
  float u=(float)std::log2(1+altp*2);
  V3 c=mixv(c0,c1,sstep(0,1.6f,u)); c=mixv(c,c2,sstep(1.6f,2.9f,u));
  c=mixv(c,c3,sstep(2.9f,4,u)); c=mixv(c,c4,sstep(4,5.2f,u));
  c=mixv(c,mixv(c2,c4,.6f),depth*.6f);
  float H=1.9f+(3.8f-1.9f)*depth;
  float env=(float)std::exp(-altp/H)*(1-depth*.35f);
  float rim=(float)std::exp(-altp/.45f)*(1-depth);
  V3 glow=(c*env*.30f+c0*rim*.25f)*(az*.85f+azW*.15f)*tw;
  float wlWin=1+(sstep(-5.5f,-2.5f,elevDeg)*(1-sstep(-.6f,.3f,elevDeg))-1)*uHLAuto;
  float lineAz=(float)std::exp(-std::pow(dAz/.55f,2));
  float line=(float)std::exp(-std::pow(alt/.11f,2))*(.7f+.3f*sstep(-.4f,0,alt));
  glow=glow+V3{1,.98f,.92f}*line*lineAz*wlWin*uHLInt*.45f;
  float domeWin=sstep(-16,-8,elevDeg)*(1-sstep(-2,4,elevDeg));
  glow=glow+V3{.10f,.15f,.30f}*.035f*domeWin*(1-(float)std::exp(-altp/6))*(1-.5f*az);
  return glow*uDawnInt;
}
static float airMassOf(float e){ float z=90-e; if(z>=96) return 40;
  return std::min(40.f,1/(std::cos(z*D2R)+.50572f*(float)std::pow(96.07995f-z,-1.6364f))); }
static V3 aces(const V3&x){ auto c=[](float v){return clampf((v*(2.51f*v+.03f))/(v*(2.43f*v+.59f)+.14f),0,1);};
  return {c(x.x),c(x.y),c(x.z)}; }
static float hash13(float x,float y,float z);
static V3 hash33(float x,float y,float z){
  float px=x*.1031f,py=y*.1030f,pz=z*.0973f;
  auto fr=[](float v){return v-std::floor(v);};
  px=fr(px);py=fr(py);pz=fr(pz);
  float dd=px*(py+33.33f)+py*(px+33.33f)+pz*(pz+33.33f);
  px+=dd;py+=dd;pz+=dd;
  float sm=px+py;
  return {fr(sm*pz),fr(2*px*py),fr(sm*px)};
}
static float vnoise(const V3&p){
  V3 i={std::floor(p.x),std::floor(p.y),std::floor(p.z)};
  V3 f={p.x-i.x,p.y-i.y,p.z-i.z}; f=f*f*(V3{3,3,3}-f*2);
  auto H=[](float x,float y,float z){ return hash13(x,y,z); };
  float n000=H(i.x,i.y,i.z),n100=H(i.x+1,i.y,i.z),n010=H(i.x,i.y+1,i.z),n110=H(i.x+1,i.y+1,i.z);
  float n001=H(i.x,i.y,i.z+1),n101=H(i.x+1,i.y,i.z+1),n011=H(i.x,i.y+1,i.z+1),n111=H(i.x+1,i.y+1,i.z+1);
  return mixf(mixf(mixf(n000,n100,f.x),mixf(n010,n110,f.x),f.y),
              mixf(mixf(n001,n101,f.x),mixf(n011,n111,f.x),f.y),f.z);
}
static V3 rotY(const V3&v,float a){ float c=std::cos(a),s=std::sin(a); return {c*v.x+s*v.z,v.y,-s*v.x+c*v.z}; }
static V3 rotX(const V3&v,float a){ float c=std::cos(a),s=std::sin(a); return {v.x,c*v.y-s*v.z,s*v.y+c*v.z}; }
struct V2{ float x,y; };
static V2 octEncode(const V3&nn){ V3 n=nn*(1/(std::abs(nn.x)+std::abs(nn.y)+std::abs(nn.z)));
  V2 q={n.x,n.z};
  if(n.y<0){ float ax=1-std::abs(q.y),ay=1-std::abs(q.x);
    float sx=q.x>=0?1:-1, sy=q.y>=0?1:-1; q={ax*sx,ay*sy}; }
  return {q.x*.5f+.5f,q.y*.5f+.5f}; }
static V3 octDecode(V2 f){ f={f.x*2-1,f.y*2-1}; V3 n={f.x,1-std::abs(f.x)-std::abs(f.y),f.y};
  float t=std::max(-n.y,0.f); n.x+=n.x>=0?-t:t; n.z+=n.z>=0?-t:t; return norm(n); }
static V3 kelvinStar(float t){ float x=clampf((t-2000)/10000,0,1);
  V3 c=mixv({1,.55f,.25f},{1,.93f,.86f},sstep(0,.4f,x));
  return mixv(c,{.72f,.82f,1},sstep(.4f,1,x)); }
static float hash13(float x,float y,float z){
  float px=x*.1031f,py=y*.1031f,pz=z*.1031f;
  auto fr=[](float v){return v-std::floor(v);};
  px=fr(px);py=fr(py);pz=fr(pz);
  float dd=px*(pz+31.32f)+py*(py+31.32f)+pz*(px+31.32f); // dot(p,p.zyx+31.32), panel line 794
  px+=dd;py+=dd;pz+=dd;
  return fr((px+py)*pz);
}

static float expHeightK(float hh,float y0,float y1){
  float ya=std::max(0.f,std::min(y0,y1)), yb=std::max(0.f,std::max(y0,y1));
  return (yb-ya)<1e-3f?(float)std::exp(-ya/hh):hh*((float)std::exp(-ya/hh)-(float)std::exp(-yb/hh))/(yb-ya); }
static float fogT(float d,float y0,float y1){
  float od=uFogDensity*d*expHeightK(std::max(1.f,uFogHeight),y0,y1);
  return (float)std::exp(-od*od); }
static V3 applyMedia(const V3&col,const V3&dir,float d,const V3&hzGlow,const V3&trans){
  bool hf=uFogOn>.5f&&uCamHeight<3000, af=uAFOn>.5f&&uCamHeight<6000;
  if(!hf&&!af) return col;
  float y0=uCamHeight, y1=uCamHeight+dir.y*d;
  float cosS=clampf(dot(dir,uSunDir),-1,1);
  V3 sunL=trans*uSunColor*uSunIntensity*.02f;
  float Tf=1; V3 Lf={0,0,0};
  if(hf){ Tf=fogT(d,y0,y1); float g=.55f;
    float hg=(1-g*g)/(4*3.14159f*(float)std::pow(1+g*g-2*g*cosS,1.5f));
    Lf=uFogColor*(uSkyAmb*.9f+hzGlow*.35f)+uFogScatter*hg*sunL*std::max(uSunDir.y+.1f,0.f); }
  V3 Ta={1,1,1},La={0,0,0};
  if(af){ float od=uAFDensity*std::max(0.f,d-uAFStart)*expHeightK(std::max(1.f,uAFHeight),y0,y1);
    if(od>1e-6f){ V3 beta=mixv(V3{5.8e-6f,13.5e-6f,33.1e-6f}*(1/13.5e-6f),V3{1,1,1},uAFMie);
      Ta={(float)std::exp(-beta.x*od),(float)std::exp(-beta.y*od),(float)std::exp(-beta.z*od)};
      float g=uAFG;
      float hg=(1-g*g)/(4*3.14159f*(float)std::pow(1+g*g-2*g*cosS,1.5f));
      La=((uSkyAmb*.9f+hzGlow*.25f)*uAFSky+sunL*hg*4*3.14159f*std::max(uSunDir.y+.08f,0.f)*mixf(1,.35f,uAFMie))*uAFTint; } }
  V3 T=Ta*Tf;
  V3 Lin=La*(V3{1,1,1}-Ta)*mixf(1,Tf,.5f)+Lf*(1-Tf)*mixv(V3{1,1,1},Ta,.5f);
  return col*T+Lin; }
static V3 starField(const V3&d,float pixAng,float am){
  V3 sd=rotX(rotY(d,uStarRot),uMilkyTilt);
  V3 col={0,0,0};
  float band=(float)std::exp(-std::pow(sd.y/.15f,2));
  float n=vnoise(sd*6)*.5f+vnoise(sd*13+V3{3.1f,3.1f,3.1f})*.3f+vnoise(sd*29+V3{7.3f,7.3f,7.3f})*.2f;
  float dust=sstep(.35f,.7f,vnoise(sd*9+V3{5.2f,1.1f,8.8f}))*(float)std::exp(-std::pow(sd.y/.06f,2))*.8f;
  float mw=band*(.35f+.65f*n)*(1-dust)*uMilky;
  col=col+mixv(V3{.55f,.62f,.9f},V3{.95f,.85f,.7f},dust*.6f)*mw*3.2e-4f;
  V2 ouv=octEncode(sd);
  for(int o=0;o<4;o++){
    if((float)o>=uStarLayers) break;
    float freq=o==0?18:(o==1?46:(o==2?110:230));
    float density=o==0?.28f:(o==1?.42f:(o==2?.6f:.7f));
    float layerGain=o==0?1:(o==1?.4f:(o==2?.15f:.06f));
    V2 gp={ouv.x*freq,ouv.y*freq}, base={std::floor(gp.x),std::floor(gp.y)};
    for(int j=-1;j<=1;j++) for(int i=-1;i<=1;i++){
      V2 cell={base.x+i,base.y+j};
      V3 h=hash33(cell.x,cell.y,(float)o*17);
      if(h.x<1-density*uStarDensity) continue;
      V2 suv={(cell.x+.5f+((h.y-.5f)*.9f))/freq,(cell.y+.5f+((h.z-.5f)*.9f))/freq};
      V3 sdir=octDecode({clampf(suv.x,0,1),clampf(suv.y,0,1)});
      float ang=(float)std::acos(clampf(dot(sd,sdir),-1,1));
      float bright=((float)std::pow(hash13(cell.x,cell.y,3.3f+(float)o),8)*8+.15f)*layerGain;
      bright*=mixf(1,band*2.2f+.35f,uMilky*.6f);
      float ph=hash13(cell.x,cell.y,5.7f)*6.283f;
      float tw=1-uStarTwinkle*(.3f+.5f*clampf(am/6,0,1))*(.5f+.5f*(float)std::sin(uTime*(3+hash13(cell.x,cell.y,2.2f)*8)+ph));
      float T=mixf(2800,11000,(float)std::pow(hash13(cell.x,cell.y,9.1f),1.6f));
      V3 sc=mixv({1,1,1},kelvinStar(T),uStarColor);
      float ps=std::max(uStarSize*(.0006f+bright*.00028f),pixAng*.9f);
      float core=(1-sstep(ps*.55f,ps,ang))*(1+.6f*(bright>=2.5f?1:0));
      core*=3.2f;
      float halo=(float)std::exp(-std::pow(ang/(ps*(2+6*uStarGlow)),1.5f))*.045f*uStarGlow*(bright/8+.03f);
      float spikes=0;
      if(o==0&&bright>2.5f){
        V3 up={1e-3f,1,0}; V3 cr={sdir.y*up.z-sdir.z*up.y,sdir.z*up.x-sdir.x*up.z,sdir.x*up.y-sdir.y*up.x};
        V3 t1=norm(cr); V3 t2={sdir.y*t1.z-sdir.z*t1.y,sdir.z*t1.x-sdir.x*t1.z,sdir.x*t1.y-sdir.y*t1.x};
        V3 dd=sd-sdir; V2 q={dot(dd,t1),dot(dd,t2)};
        float w=std::max(pixAng*.6f,ps*.35f);
        spikes=((float)std::exp(-std::abs(q.x)/w*.5f)*(float)std::exp(-std::abs(q.y)*70)
               +(float)std::exp(-std::abs(q.y)/w*.5f)*(float)std::exp(-std::abs(q.x)*70))*.3f*uStarGlow*(bright/8); }
      col=col+sc*(core+halo+spikes)*bright*tw*2.6e-3f;
    }
  }
  return col*uStarBright;
}
static void render(float sunTime,float yawDeg,float pitchDeg,int W,int H,const std::string&path){
  uSunDir=sunDirAt(sunTime,-26,0); uSunColor=kelvinRGB(5800); uSunAng=0.53f*D2R/2;
  float yaw=yawDeg*D2R,pitch=pitchDeg*D2R;
  uCamFwd={(float)(std::sin(yaw)*std::cos(pitch)),(float)std::sin(pitch),(float)(-std::cos(yaw)*std::cos(pitch))};
  uCamRight={(float)std::cos(yaw),0,(float)std::sin(yaw)};
  uCamUp={(float)(-std::sin(pitch)*std::sin(yaw)),(float)std::cos(pitch),(float)(std::sin(pitch)*std::cos(yaw))};
  uTanHalf=std::tan(72*D2R/2);
  float elevDeg=(float)(std::asin(clampf(uSunDir.y,-1,1))/D2R);
  std::printf("sun %.2fh elev %.2f deg yaw %.0f pitch %.0f -> %s\n",sunTime,elevDeg,yawDeg,pitchDeg,path.c_str());
  V3 ro={0,uPlanetR+uCamHeight,0};
  { V3 hd=norm(V3{uSunDir.x+1e-4f,0,uSunDir.z});
    V3 s0,t0,s1,t1,s2,t2; float g0,g1,g2;
    atmosphere(ro,{0,1,0},s0,t0,g0);
    atmosphere(ro,norm(hd+V3{0,.08f,0}),s1,t1,g1);
    atmosphere(ro,norm(V3{0,0,0}-hd+V3{0,.08f,0}),s2,t2,g2);
    uSkyAmb=(s0*uSkyTint*uSkyBright)*.4f+(s1*uSkyTint*uSkyBright)*.35f+(s2*uSkyTint*uSkyBright)*.25f; }
  float pixAng=2*uTanHalf/H;
  std::vector<uint8_t> px(W*H*3);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
    float uvx=((x+.5f)*2-W)/H, fragY=H-(y+.5f), uvy=(fragY*2-H)/H; // panel line 1139
    V3 dir=norm(uCamFwd+uCamRight*uvx*uTanHalf+uCamUp*uvy*uTanHalf); // panel line 1140
    V3 sky,trans; float ground; atmosphere(ro,dir,sky,trans,ground);
    sky=sky*uSkyTint*uSkyBright;
    float hx=dir.x+1e-5f, hz=dir.z+1e-5f; { float l=(float)std::sqrt(hx*hx+hz*hz); hx/=l; hz/=l; }
    float sx=uSunDir.x+1e-5f, sz=uSunDir.z+1e-5f; { float l=(float)std::sqrt(sx*sx+sz*sz); sx/=l; sz/=l; }
    float facing=.5f+.5f*(hx*sx+hz*sz); // panel line 1148
    float hdrScale=uSunIntensity/22*uSkyBright;
    float groundObs=1-sstep(1500,12000,uCamHeight);
    V3 glow=dawnGlow(dir,elevDeg,facing)*hdrScale*groundObs;
    V3 hzGlow=dawnGlow(norm(V3{dir.x+1e-5f,1e-5f,dir.z+1e-5f}),elevDeg,facing)*hdrScale*groundObs;
    float skyLum=sky.x*.2126f+sky.y*.7152f+sky.z*.0722f;
    float hzDip=-(float)std::acos(clampf(uPlanetR/(uPlanetR+uCamHeight),0,1));
    float ext=sstep(hzDip-.01f,hzDip+.12f*groundObs+.002f,dir.y);
    V3 col;
    if(ground>.5f){
      float gn,gf; rsi(ro,dir,uPlanetR,gn,gf); V3 hp=ro+dir*gn; V3 n=norm(hp);
      float nd=std::max(dot(n,uSunDir),0.f);
      V3 amb=sky*.35f+V3{.002f,.003f,.006f}*uSkyBright;
      V3 sunP=trans*uSunColor*uSunIntensity*.09f*nd;
      col=uGroundColor*uGroundBright*(sunP+amb)+sky;
    } else {
      col=sky+glow;
      if(uStarsOn>.5f&&uStarCeil>std::max(skyLum,1e-7f)*uStarLimit*.6f){
        float am=airMassOf((float)(std::asin(clampf(dir.y,-1,1))/D2R));
        V3 st;
        float aaNeed=std::max(skyLum,1e-7f)*uStarLimit*8<=uStarCeil?1:0;
        if(uStarAA>1.5f&&aaNeed>.5f){
          V3 s0={0,0,0};
          const float ox[4]={-.375f,.125f,.375f,-.125f}, oy[4]={-.125f,-.375f,.125f,.375f};
          for(int q=0;q<4;q++){
            V3 d2=norm(dir+(uCamRight*ox[q]+uCamUp*oy[q])*pixAng);
            s0=s0+starField(d2,pixAng*.7f,am); }
          st=s0*.25f*trans;
        } else st=starField(dir,pixAng,am)*trans;
        float skyL=std::max(skyLum,1e-7f), thresh=skyL*uStarLimit;
        float l=st.x*.2126f+st.y*.7152f+st.z*.0722f;
        float vis=sstep(thresh*.6f,thresh*2.5f,l+1e-9f);
        col=col+st*vis*ext;
      }
      float ang=(float)std::acos(clampf(dot(dir,uSunDir),-1,1));
      float soft=1+(2.2f-1)*(1-sstep(0,4,elevDeg));
      float disc=1-sstep(uSunAng*(1-uSunSoft*.9f*soft),uSunAng,ang);
      float limb=1+(0.55f-1)*sstep(0,uSunAng,ang);
      V3 ext=expv(V3{5.8e-6f*uRayleigh*uHr,13.5e-6f*uRayleigh*uHr,33.1e-6f*uRayleigh*uHr}*-airMassOf(elevDeg)
                  +V3{21e-6f*1.1f*uMie*uHm,21e-6f*1.1f*uMie*uHm,21e-6f*1.1f*uMie*uHm}*-airMassOf(elevDeg));
      V3 tr2=trans*trans;
      V3 mx={std::max(ext.x,tr2.x),std::max(ext.y,tr2.y),std::max(ext.z,tr2.z)};
      V3 sunCol=uSunColor*mx;
      col=col+disc*limb*sunCol*uSunIntensity*uSunDiscBoost*(.35f+.65f*sstep(-1,8,elevDeg));
      float sg=(float)(std::exp(-ang*40)*.35+std::exp(-ang*9)*.03+std::exp(-ang*2.5)*.004);
      col=col+sg*uBloom*sunCol*uSunIntensity*.6f;
      float hRef=std::max(uFogHeight*4,uAFHeight*5);
      float dS=dir.y>.002f?std::min(60000.f,hRef/dir.y):60000.f;
      col=applyMedia(col,dir,dS,hzGlow,trans);
    }
    float autoEV=-.35f*sstep(-8,-1,elevDeg)-1.0f*sstep(-1,6,elevDeg)-.6f*sstep(6,30,elevDeg);
    col=col*(float)std::exp2(uEV+autoEV);
    col=aces(col);
    // panel line 1260: length(uv*vec2(uRes.y/uRes.x,1.)*.9)
    float vx=uvx*(float(H)/float(W))*.9f, vy=uvy*.9f;
    float vig=1-uVignette*(float)std::pow(std::sqrt(vx*vx+vy*vy),2.2f);
    col=col*clampf(vig,0,1);
    col={(float)std::pow(std::max(col.x,0.f),1/2.2f),(float)std::pow(std::max(col.y,0.f),1/2.2f),(float)std::pow(std::max(col.z,0.f),1/2.2f)};
    float gr=(hash13(x+.5f,y+.5f,50.f)-.5f)*uGrain*.12f;
    col=col+V3{gr,gr,gr};
    uint8_t*p=&px[(y*W+x)*3];
    p[0]=(uint8_t)(clampf(col.x,0,1)*255); p[1]=(uint8_t)(clampf(col.y,0,1)*255); p[2]=(uint8_t)(clampf(col.z,0,1)*255);
  }
  // 24-bit BMP
  int stride=(W*3+3)&~3;
  std::vector<uint8_t> bmp(54+stride*H,0);
  bmp[0]='B';bmp[1]='M'; *(int*)&bmp[2]=54+stride*H; *(int*)&bmp[10]=54; *(int*)&bmp[14]=40;
  *(int*)&bmp[18]=W; *(int*)&bmp[22]=H; *(short*)&bmp[26]=1; *(short*)&bmp[28]=24;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){ uint8_t*s=&px[(y*W+x)*3]; uint8_t*d=&bmp[54+(H-1-y)*stride+x*3];
    d[0]=s[2];d[1]=s[1];d[2]=s[0]; }
  std::ofstream f(path,std::ios::binary); f.write((char*)bmp.data(),bmp.size());
}
int main(){
  render(6.4f,35,-4,480,270,"/tmp/skyref/htmlsky_0640_default.bmp");
  render(6.4f,35,18,480,270,"/tmp/skyref/htmlsky_0640_lookup.bmp");
  render(7.6f,35,18,480,270,"/tmp/skyref/htmlsky_0760_lookup.bmp");
  render(12.0f,35,18,480,270,"/tmp/skyref/htmlsky_1200_lookup.bmp");
  render(6.4f,87.3f,5.4f,480,270,"/tmp/skyref/htmlsky_0640_sun.bmp");
  render(7.6f,79.0f,21.4f,480,270,"/tmp/skyref/htmlsky_0760_sun.bmp");
  render(5.889f,90.7f,-1.0f,480,270,"/tmp/skyref/htmlsky_0589_sun.bmp");
  render(6.0f,90.0f,0.0f,480,270,"/tmp/skyref/htmlsky_0600_sun.bmp");
  render(18.111f,269.3f,-1.0f,480,270,"/tmp/skyref/htmlsky_1811_sun.bmp");
  render(0.0f,35.0f,40.0f,480,270,"/tmp/skyref/htmlsky_0000_night.bmp");
  return 0;
}
