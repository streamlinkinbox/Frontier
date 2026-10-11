#include "CpuShader.h"
#include "PaintScene.slang"
#include "PngWriteCounterpart.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>
static unsigned Checks=0;
void Check(bool x,const char* why){++Checks;if(!x)throw std::runtime_error(why);}
bool Finite(vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&v.x>=0&&v.y>=0&&v.z>=0;}
float Error(vec3 a,vec3 b){return length(a-b)/std::max(1.f,length(a));}
void TestCore(){
 auto p=AutomotivePaintDefaults();auto zero=p;zero.Density=0;
 for(unsigned i=0;i<4096;++i){
  vec2 uv(APRandom(i)*.1f-.05f,APRandom(i+7000)*.1f-.05f);
  auto z=AutomotivePrepareFlakes(zero,uv,vec2(.00001f,0),vec2(0,.00001f));Check(z.Weight==0&&z.MeanWeight==0,"zero density means zero flakes");
  auto s=AutomotivePrepareFlakes(p,uv,vec2(.00001f,0),vec2(0,.00001f));auto same=AutomotivePrepareFlakes(p,uv,vec2(.00001f,0),vec2(0,.00001f));Check(s.Weight==same.Weight&&s.Slope.x==same.Slope.x&&s.Slope.y==same.Slope.y,"stable object/material coordinates");
  vec3 v=normalize(vec3(APRandom(i+90)-.5f,APRandom(i+120)-.5f,.2f)),l=normalize(vec3(APRandom(i+150)-.5f,APRandom(i+180)-.5f,.2f));
  auto a=AutomotiveEvaluatePaint(p,s,v,l),b=AutomotiveEvaluatePaint(p,s,l,v);Check(Finite(a),"finite nonnegative BRDF");Check(Error(a,b)<.00002f,"reciprocity");
  auto changed=p;changed.NormalSpread=.6f;changed.Density=1;changed.DiameterMm=2;
  Check(Error(AutomotiveEvaluateCoat(p,v,l),AutomotiveEvaluateCoat(changed,v,l))==0,"flake parameters cannot perturb the coat");
 }
}
void TestLimits(){
 auto p=AutomotivePaintDefaults();
 auto resolved=AutomotivePrepareFlakes(p,vec2(.02f),vec2(0),vec2(0));auto far=AutomotivePrepareFlakes(p,vec2(.02f),vec2(.1f),vec2(.1f));
 Check(resolved.Unresolved==0&&far.Unresolved==1,"footprint transition endpoints");
 auto far2=AutomotivePrepareFlakes(p,vec2(.017f,.024f),vec2(.1f),vec2(.1f));vec3 v(0,0,1),l=normalize(vec3(.3f,.2f,1));
 Check(Error(AutomotiveEvaluatePaint(p,far,v,l),AutomotiveEvaluatePaint(p,far2,v,l))<1e-6f,"unresolved population independent of pixel seed");
 Check(length(AutomotiveEvaluatePaint(p,far,v,vec3(0,0,-1)))==0,"no backface emission");
 auto pearl=p;pearl.PearlWeight=1;Check(Error(AutomotiveEvaluatePaint(pearl,far,v,l),AutomotiveEvaluatePaint(p,far,v,l))>1e-5f,"film changes Fresnel response");
 auto bad=p;bad.Density=std::numeric_limits<float>::quiet_NaN();bad.DiameterMm=0;bad.NormalSpread=-5;bad.FlakeRoughness=0;
 auto safe=AutomotivePrepareFlakes(bad,vec2(0),vec2(0),vec2(0));Check(safe.Weight==0&&Finite(AutomotiveEvaluatePaint(bad,safe,v,l)),"parameter limits avoid division by zero and invalid density");
}
void TestPopulation(){
 auto p=AutomotivePaintDefaults();auto far=AutomotivePrepareFlakes(p,vec2(0),vec2(.1f),vec2(.1f));vec3 v(0,0,1);
 double mean=0;const unsigned N=100000;for(unsigned i=0;i<N;++i){vec2 uv(APRandom(i+290000)*.5f,APRandom(i+590000)*.5f);auto s=AutomotivePrepareFlakes(p,uv,vec2(0),vec2(0));mean+=s.Weight*APBeckmann(v,s.Slope,s.Alpha);}
 double expected=far.MeanWeight*APBeckmann(v,vec2(0),far.PopulationAlpha);Check(std::abs(mean/N-expected)/expected<.12,"distant NDF agrees with finite-flake population average");
 std::printf("Population D: sampled %.6f, analytic %.6f\n",mean/N,expected);
}
void TestFurnace(){
 auto p=AutomotivePaintDefaults();auto far=AutomotivePrepareFlakes(p,vec2(0),vec2(.1f),vec2(.1f));
 for(float cosine:{1.f,.5f,.1f}){
  vec3 wo(sqrt(1-cosine*cosine),0,cosine),integral(0);for(unsigned i=0;i<32768;++i){float z=(i+.5f)/32768.f,az=6.2831853f*APRandom(i+870000);vec3 wi(sqrt(1-z*z)*cos(az),sqrt(1-z*z)*sin(az),z);integral+=AutomotiveEvaluatePaint(p,far,wo,wi)*(z*6.2831853f/32768.f);}
  Check(Finite(integral)&&std::max({integral.x,integral.y,integral.z})<1.05f,"population furnace sanity, default paint");std::printf("Furnace mu %.2f: %.5f %.5f %.5f\n",cosine,integral.x,integral.y,integral.z);
 }
}
void TestDensity(){
 auto p=AutomotivePaintDefaults();float sums[6]={};const float levels[6]={0,1,2,4,8,16};
 for(unsigned i=0;i<2048;++i){vec2 uv(APRandom(i+800)*.2f,APRandom(i+19000)*.2f);float previous=-1;uint first=0;float firstWeight=0;
  for(int level=0;level<6;++level){p.Density=levels[level];auto f=AutomotivePrepareFlakes(p,uv,vec2(0),vec2(0));
   Check(f.Weight>=previous,"density adds coverage rather than replacing the pattern");previous=f.Weight;sums[level]+=f.Weight;
   if(level==1){first=f.FacetKey;firstWeight=f.Weight;}if(level>1&&firstWeight==1)Check(f.FacetKey==first,"existing fully covered flake identity is stable as density increases");
   Check(f.MeanWeight>=0&&f.MeanWeight<=1,"bounded dense population");
  }
 }
 Check(sums[0]==0&&sums[5]>sums[1]*2.0f,"above-one density actually adds resolved flakes");
 for(int i=1;i<6;++i)std::printf("Density %.0fx: resolved coverage %.5f\n",levels[i],sums[i]/2048);
 p.Density=16;auto maximum=AutomotivePrepareFlakes(p,vec2(.031f),vec2(0),vec2(0));p.Density=1000;auto capped=AutomotivePrepareFlakes(p,vec2(.031f),vec2(0),vec2(0));Check(maximum.Weight==capped.Weight&&maximum.FacetKey==capped.FacetKey,"documented density cap is safe");
}
void TestPalette(){
 auto p=AutomotivePaintDefaults();p.Density=4;auto palette=AutomotiveRgbPalette();unsigned families[3]={};
 for(unsigned i=0;i<4096;++i){auto raw=AutomotivePrepareFlakes(p,vec2(APRandom(i+93000)*.1f,APRandom(i+41000)*.1f),vec2(0),vec2(0));auto coloured=AutomotiveApplyFlakePalette(raw,palette);auto repeat=AutomotiveApplyFlakePalette(raw,palette);
  Check(Error(coloured.Colour,repeat.Colour)==0,"colour is fixed to flake ID, not time or camera");Check(coloured.Slope.x==raw.Slope.x&&coloured.Slope.y==raw.Slope.y,"colour cannot alter flake orientation");
  if(raw.Weight==0)continue;
  int family=coloured.Colour.x>coloured.Colour.y&&coloured.Colour.x>coloured.Colour.z?0:coloured.Colour.y>coloured.Colour.z?1:2;++families[family];
  const auto lo=palette.Minimum[family],hi=palette.Maximum[family],c=coloured.Colour;
  Check(c.x>=lo.x-1e-6f&&c.x<=hi.x+1e-6f&&c.y>=lo.y-1e-6f&&c.y<=hi.y+1e-6f&&c.z>=lo.z-1e-6f&&c.z<=hi.z+1e-6f,"each flake stays inside one colour-family range");
 }
 Check(families[0]>100&&families[1]>100&&families[2]>100,"red green and blue families all occur");
 std::printf("RGB family counts: %u %u %u\n",families[0],families[1],families[2]);
}
void TestPaletteWeights(){
 auto p=AutomotivePaintDefaults();auto palette=AutomotiveRgbPalette();auto raw=AutomotivePrepareFlakes(p,vec2(.003f),vec2(0),vec2(0));
 palette.Weight[0]=0;palette.Weight[1]=1;palette.Weight[2]=0;
 for(uint i=0;i<512;++i){raw.FacetKey=i;auto s=AutomotiveApplyFlakePalette(raw,palette);Check(s.Colour.y>s.Colour.x&&s.Colour.y>s.Colour.z,"zero-share colours are excluded");}
 auto green=AutomotiveApplyFlakePalette(raw,palette);Check(Error(green.MeanColour,(palette.Minimum[1]+palette.Maximum[1])*.5f)<1e-6f,"distant flakes use weighted range mean, not unrelated silver");
 palette.Weight[1]=0;auto zero=AutomotiveApplyFlakePalette(raw,palette);Check(Error(zero.Colour,raw.Colour)==0,"all-zero shares use explicit fallback");
 palette.Count=8;for(int i=0;i<8;++i){palette.Weight[i]=1;palette.Minimum[i]=vec3(.2f);palette.Maximum[i]=vec3(.8f);}auto full=AutomotiveApplyFlakePalette(raw,palette);Check(Error(full.MeanColour,vec3(.5f))<1e-6f,"eight colour ranges supported");palette.Count=100;Check(Error(full.Colour,AutomotiveApplyFlakePalette(raw,palette).Colour)==0,"palette count safely bounded");
}
void TestCoatColour(){
 auto p=AutomotivePaintDefaults();vec3 v(0,0,1),l=normalize(vec3(.1f,.1f,1));const auto white=AutomotiveEvaluateCoat(p,v,l);
 p.CoatTint=vec3(.02f,.15f,.9f);p.CoatTintStrength=.5f;auto blue=AutomotiveEvaluateCoat(p,v,l);Check(blue.z>blue.x&&blue.x<white.x,"blue clearcoat tint drives reflection");
 p.Density=16;Check(Error(blue,AutomotiveEvaluateCoat(p,v,l))==0,"dense flakes leave coat reflection independent");p.CoatWeight=0;Check(length(AutomotiveEvaluateCoat(p,v,l))==0,"disabled coat cannot reflect tinted light");
 p.CoatWeight=1;p.CoatTintStrength=0;Check(Error(white,AutomotiveEvaluateCoat(p,v,l))==0,"zero tint restores previous white coat exactly");
}
void WriteProbes(const std::filesystem::path& out){
 auto p=AutomotivePaintDefaults();
 std::ofstream probes(out/"CpuProbes.json");probes<<"[";
 for(unsigned i=0;i<64;++i){auto pp=p;pp.PearlWeight=i%32>=16?1.f:0.f;pp.Density=i<32?(i%4)*.3f:float(1u<<(i%5));
  if(i>=32){pp.CoatTint=vec3(.03f,.2f,.9f);pp.CoatTintStrength=.35f;}
  float footprint=i>=48?.01f:.00004f;auto f=AutomotivePrepareFlakes(pp,vec2(.0003f*i,.00017f*i),vec2(footprint,0),vec2(0,footprint));if(i>=32)f=AutomotiveApplyFlakePalette(f,AutomotiveRgbPalette());
  vec3 a=AutomotiveEvaluatePaint(pp,f,normalize(vec3(.1f,0,1)),normalize(vec3((float(i%32)-16)*.025f,.15f,1)));if(i)probes<<",";probes<<"["<<a.x<<","<<a.y<<","<<a.z<<"]";
 }probes<<"]\n";
}
void Tests(const std::filesystem::path& out){
 TestCore();TestLimits();TestPopulation();TestFurnace();TestDensity();TestPalette();TestPaletteWeights();TestCoatColour();WriteProbes(out);
 std::printf("PASS %u checks: shared native automotive flake shader.\n",Checks);
}
void Render(const std::filesystem::path& out,const char* name,AutomotivePaintParameters p,float phase,float yaw,float distance,const AutomotiveFlakePalette& palette){
 const int W=960,H=600;std::vector<unsigned char> pixels(W*H*3);std::atomic<int> next{0};std::atomic<unsigned> invalid{0};std::vector<std::thread> workers;
 for(int worker=0;worker<4;++worker)workers.emplace_back([&](){for(int y;(y=next.fetch_add(1))<H;)for(int x=0;x<W;++x){vec3 colour(0);for(int sy=0;sy<2;++sy)for(int sx=0;sx<2;++sx){vec3 c=APStudio(vec2(x+(sx+.5f)/2,H-y-(sy+.5f)/2),vec2(W,H),p,phase,yaw,distance,palette);if(!Finite(c))++invalid;colour+=c*.25f;}colour=APTonemap(colour);auto* dst=&pixels[(y*W+x)*3];dst[0]=static_cast<unsigned char>(colour.x*255+.5f);dst[1]=static_cast<unsigned char>(colour.y*255+.5f);dst[2]=static_cast<unsigned char>(colour.z*255+.5f);}});
 for(auto& w:workers){w.join();}Check(invalid==0,"no nonfinite rendered samples");Check(stbi_write_png((out/name).string().c_str(),W,H,3,pixels.data(),W*3)!=0,"native PNG");std::printf("Rendered %s\n",name);
}
void Render(const std::filesystem::path& out,const char* name,AutomotivePaintParameters p,float phase,float yaw,float distance){
 auto palette=AutomotivePaletteDefaults();Render(out,name,p,phase,yaw,distance,palette);
}
int main(int argc,char** argv){try{
 std::filesystem::path out=argc>1?argv[1]:"Exhibits/Gallery/AutomotiveFlakes";std::filesystem::create_directories(out);Tests(out);
 if(argc>2&&std::string(argv[2])=="--test")return 0;
 auto p=AutomotivePaintDefaults(),smooth=p,pearl=p;smooth.Density=0;pearl.PearlWeight=1;
 Render(out,"paint-smooth.png",smooth,0,0,.19f);Render(out,"paint-metallic.png",p,0,0,.19f);Render(out,"paint-pearl.png",pearl,0,0,.19f);
 Render(out,"paint-close.png",p,0,0,.13f);Render(out,"paint-far.png",p,0,0,.37f);
 for(int i=0;i<6;++i){std::string name="light-"+std::to_string(i)+".png";Render(out,name.c_str(),p,-.25f+i*.1f,.12f,.19f);}
 auto coloured=AutomotivePaintDefaults();auto palette=AutomotiveRgbPalette();coloured.Density=4;coloured.CoatTint=vec3(.023153f,.162029f,.846873f);coloured.CoatTintStrength=.35f;
 Render(out,"paint-rgb-coat.png",coloured,0,0,.19f,palette);coloured.Density=12;Render(out,"paint-rgb-dense.png",coloured,0,0,.19f,palette);
 return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
