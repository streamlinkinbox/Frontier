#include "SlangCpuShim.h"
#include <stdexcept>
vec3 FetchEnergy(float,float){throw std::logic_error("Unexpected LUT path");}
vec3 FetchSheen(float,float){throw std::logic_error("Unexpected LUT path");}
vec4 FetchSheenFull(float,float){throw std::logic_error("Unexpected LUT path");}
#define FRONTIER_AUTOMOTIVE_SHOWCASE 1
#include "MaterialEvaluation.slang"
#include "ShowcaseStructure.h"
#include "AutomotiveShowcasePresets.h"
#include "MaterialIndex.h"
#include "PngWriteCounterpart.h"
#include "../AutomotiveFlakes/PaintScene.slang"
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <limits>
using namespace Frontier;
unsigned checks=0;
void Check(bool ok,const char* msg){++checks;if(!ok)throw std::runtime_error(msg);}
bool Finite(vec3 c){return std::isfinite(c.x)&&std::isfinite(c.y)&&std::isfinite(c.z)&&c.x>=0&&c.y>=0&&c.z>=0;}
ShadingRecord NativeRecord(const MaterialSlabRecord& s){
 auto m=AutomotiveDefaults();m.BaseColor=vec3(s.BaseColorR,s.BaseColorG,s.BaseColorB);m.SpecularColor=vec3(s.SpecularColorR,s.SpecularColorG,s.SpecularColorB);
 m.CoatColor=vec3(s.CoatColorR,s.CoatColorG,s.CoatColorB);m.CoatWeight=s.CoatWeight;m.CoatRoughness=s.CoatRoughness;m.CoatIor=s.CoatIor;
 m.SpecularRoughness=s.SpecularRoughness;m.ThinFilmWeight=s.ThinFilmWeight;m.ThinFilmThickness=s.ThinFilmThickness;m.ThinFilmIor=s.ThinFilmIor;
 m.AutomotiveData=vec4(s.Reserved0,s.Reserved1,0,0);m.AutomotiveDensity=s.SlateGlintDensity;m.AutomotiveFootprint=.00004f;
 return m;
}
void TestPrepared(const ShadingRecord& m,vec3 wo,vec3 wi,vec3 c){
   auto p=AutomotiveShowcasePaint(m);auto f=AutomotiveApplyFlakePalette(AutomotivePrepareFlakes(p,vec2(m.AutomotiveData.z,m.AutomotiveData.w),vec2(m.AutomotiveFootprint,0),vec2(0,m.AutomotiveFootprint)),AutomotiveShowcasePalette(m));
   Check(length(c-AutomotiveShowcaseEvaluatePrepared(m,p,f,wo,wi))<1e-6f,"prepared/native entry points match");
}
void TestMaterial(const MaterialSlabRecord& s){
 auto m=NativeRecord(s);ResolvedLayers layers{};vec3 wo=normalize(vec3(.2f,0,1));
 for(uint i=0;i<128;++i){
  m.AutomotiveData.z=APRandom(i+734)*.14f;m.AutomotiveData.w=APRandom(i+9234)*.09f;
  auto sample=SampleBsdf(m,layers,wo,vec4(APRandom(i+91),APRandom(i+812),APRandom(i+9210),APRandom(i+346)));
  if(sample.w>0){vec3 wi(sample.x,sample.y,sample.z);Check(std::abs(sample.w-PdfBsdf(m,layers,wo,wi))<1e-5f,"sampler/pdf agree");
   auto c=EvaluateBsdf(m,layers,wo,wi);Check(Finite(c),"finite nonnegative native BRDF");
   TestPrepared(m,wo,wi,c);
  }
 }
 auto f=EvaluateBsdf(m,layers,wo,vec3(0,0,-1));Check(length(f)==0,"opaque paint has no transmission lobe");
 m.AutomotiveFootprint=.1f;auto a=EvaluateBsdf(m,layers,wo,wo);m.AutomotiveData.z+=.035f;Check(length(a-EvaluateBsdf(m,layers,wo,wo))<1e-6f,"distant palette response is stable");
}
void TestCoat(){
 MaterialSlabDescriptor s;AuthorAutomotiveShowcase(s,1,.5f);auto m=NativeRecord(MaterialIndex::ConstructSlabRecord(s));auto p=AutomotiveShowcasePaint(m);
 p.Pigment=vec3(0);p.Density=0;auto f=AutomotivePrepareFlakes(p,vec2(0),vec2(0),vec2(0));vec3 v(0,0,1);
 auto c=AutomotiveShowcaseEvaluatePrepared(m,p,f,v,v);Check(std::abs(c.x-c.y)<1e-5f&&std::abs(c.x-c.z)<1e-5f,"candy dye does not colour the outer dielectric reflection");
 m.CoatWeight=0;Check(length(AutomotiveShowcaseEvaluatePrepared(m,p,f,v,v))==0,"disabled coat contributes nothing");
 s.SlateAutomotiveProfile=1.5f;Check(MaterialIndex::ConstructSlabRecord(s).Reserved0==0,"fractional profile IDs rejected");
 s.SlateAutomotiveSweep=std::numeric_limits<float>::quiet_NaN();Check(MaterialIndex::ConstructSlabRecord(s).Reserved1==0,"nonfinite sweep safely normalized");
}
void TestFurnace(unsigned profile){
 MaterialSlabDescriptor slab;AuthorAutomotiveShowcase(slab,profile,.5f);auto m=NativeRecord(MaterialIndex::ConstructSlabRecord(slab));m.AutomotiveFootprint=.05f;ResolvedLayers layers{};
 for(float mu:{1.0f,.5f,.1f}){vec3 wo(std::sqrt(1-mu*mu),0,mu),sum(0);
  for(uint i=0;i<8192;++i){auto s=SampleBsdf(m,layers,wo,vec4(APRandom(i+471),APRandom(i+85092),APRandom(i+527643),0));if(s.w>0){vec3 wi(s.x,s.y,s.z);sum+=EvaluateBsdf(m,layers,wo,wi)*(wi.z/s.w);}}
  sum=sum/8192.0f;Check(Finite(sum)&&sum.x<1.08f&&sum.y<1.08f&&sum.z<1.08f,"dense filtered white-furnace bound");printf("Furnace profile %u mu %.1f: %.5f %.5f %.5f\n",profile,mu,sum.x,sum.y,sum.z);
 }
}
vec3 StudioCoat(const ShadingRecord& m,vec3 v,vec3 n,vec3 t,vec3 b){
 vec3 colour(0);
 // Integrate the narrow outer coat with its own VNDF proposal: no six-dot softbox quadrature artefact.
 float r=std::clamp(m.CoatRoughness,.06f,.7f);
 for(uint i=0;i<128;++i){
  uint bits=i;bits=(bits<<16)|(bits>>16);bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
  vec3 h=SampleGgxVndf(v,vec2(r*r),vec2((i+.5f)/128,float(bits)*2.3283064365386963e-10f));vec3 l=reflect(-v,h);if(l.z<=0)continue;
  vec3 d=t*l.x+b*l.y+n*l.z;if(d.z<=0)continue;vec3 radiance(0);
  float x=d.x*1.15f/d.z,y=d.y*1.15f/d.z;
  if(std::abs(x+.38f)<.525f&&std::abs(y-.52f)<.15f)radiance+=vec3(46,40,32);
  x=d.x*.85f/d.z;y=d.y*.85f/d.z;
  if(std::abs(x-.8f)<.125f&&std::abs(y+.15f)<.525f)radiance+=vec3(12,17,26);
  float pdf=GgxVndfPdf(v,h,vec2(r*r))/(4*dot(v,h));
  if(pdf>0)colour+=AutomotiveShowcaseCoat(m,v,l)*radiance*(l.z/(pdf*128));
 }
 return colour;
}
vec3 StudioBaseLighting(const ShadingRecord& m,const AutomotivePaintParameters& p,const AutomotiveFlakeSurface& f,vec3 v,vec3 n,vec3 t,vec3 b){
 float coverage=mix(f.Weight,f.MeanWeight,f.Unresolved);vec3 colour=p.Pigment*(.12f*(1-coverage))*m.CoatColor;
 for(int box=0;box<2;++box)for(int y=0;y<8;++y)for(int x=0;x<6;++x){
  float u=(x+.5f)/6-.5f,w=(y+.5f)/8-.5f;
  vec3 light=box==0?vec3(-.38f+u*1.05f,.52f+w*.30f,1.15f):vec3(.8f+u*.25f,-.15f+w*1.05f,.85f);
  vec3 direction=normalize(light),l=APSceneLocal(direction,n,t,b),radiance=box==0?vec3(46,40,32):vec3(12,17,26);
  float omega=(box==0?.315f:.2625f)*std::abs(direction.z)/(dot(light,light)*48);
  colour+=max(vec3(0),AutomotiveShowcaseEvaluatePrepared(m,p,f,v,l)-AutomotiveShowcaseCoat(m,v,l))*radiance*(max(0.f,l.z)*omega);
 }
 return colour;
}
vec3 Studio(vec2 pixel,vec2 size,const ShadingRecord& material){
 auto hit=APIntersect(pixel,size,0,.19f);if(hit.Hit==0)return vec3(.014f,.018f,.024f);
 auto hx=APIntersect(pixel+vec2(1,0),size,0,.19f),hy=APIntersect(pixel+vec2(0,1),size,0,.19f);
 const auto& m=material; // Prepared UV state below is local to this hit.
 auto p=AutomotiveShowcasePaint(m);auto f=AutomotiveApplyFlakePalette(AutomotivePrepareFlakes(p,hit.UV,hx.Hit?hx.UV-hit.UV:vec2(.01f),hy.Hit?hy.UV-hit.UV:vec2(.01f)),AutomotiveShowcasePalette(m));
 vec3 n=hit.Normal,t=normalize(vec3(1,0,0)-n*n.x),b=cross(n,t),v=APSceneLocal(hit.View,n,t,b);
 vec3 colour=StudioBaseLighting(m,p,f,v,n,t,b);
 colour+=StudioCoat(m,v,n,t,b);
 return colour;
}
void Render(const std::filesystem::path& out,unsigned profile,const MaterialSlabRecord& slab){
 const int W=640,H=400;std::vector<unsigned char> pixels(W*H*3);auto m=NativeRecord(slab);std::atomic<int> next{0},invalid{0};std::vector<std::thread> workers;
 for(unsigned i=0;i<8;++i)workers.emplace_back([&]{for(int y;(y=next++)<H;){for(int x=0;x<W;++x){vec3 c(0);
  for(int a=0;a<2;++a)for(int b=0;b<2;++b)c+=Studio(vec2(x+(a+.5f)/2,H-1-y+(b+.5f)/2),vec2(W,H),m)*.25f;
  if(!Finite(c)){++invalid;}
  c=APTonemap(c);size_t j=(size_t(y)*W+x)*3;pixels[j]=static_cast<unsigned char>(255*clamp(c.x,0.f,1.f));pixels[j+1]=static_cast<unsigned char>(255*clamp(c.y,0.f,1.f));pixels[j+2]=static_cast<unsigned char>(255*clamp(c.z,0.f,1.f));
 }}});
 for(auto& w:workers){w.join();}
 Check(invalid==0,"native render finite");auto file=out/("native-paint-"+std::to_string(profile)+".png");Check(stbi_write_png(file.string().c_str(),W,H,3,pixels.data(),W*3)!=0,"native PNG saved");printf("Rendered %s\n",file.string().c_str());
}
int main(int argc,char** argv){try{
 std::filesystem::path out=argc>1?argv[1]:"Exhibits/Gallery/AutomotiveShowcase";std::filesystem::create_directories(out);
 ShowcaseStructure scene;scene.Construct();const auto& materials=scene.QueryMaterials();unsigned spheres=0,families[5]={};
 for(const auto& span:scene.QuerySpans())if(span.Name.rfind("Sphere",0)==0)++spheres;
 Check(kShowcaseGridSide==20&&spheres==400,"20 by 20 native sphere grid");Check(kShowcaseRevision==6,"stale r5 scene is invalidated");Check(sizeof(MaterialSlabRecord)==304,"GPU record ABI unchanged");
 MaterialIndex index;for(const auto& d:materials)index.Register(d);index.Finalise(1,nullptr);
 std::ofstream manifest(out/"Grid.json");manifest<<"{\"rows\":20,\"columns\":20,\"spheres\":"<<spheres<<",\"triangles\":"<<scene.QueryTriangles().size()<<",\"materials\":[";
 for(unsigned row=0;row<20;++row)for(unsigned col=0;col<20;++col){unsigned i=1+row*20+col;const auto& d=materials[i];const auto& r=index.QuerySlabRecords()[index.QueryRecords()[i].SlabOffset];
  if(row<15)Check(r.Reserved0==0,"original families retain standard shading");else{Check(r.Reserved0==float(row-14),"family ID survives resident packing");++families[row-15];Check(r.Reserved1==float(col)/19,"20 parameter steps survive packing");TestMaterial(r);}
  if(i>1){manifest<<",";}
  manifest<<"{\"row\":"<<row<<",\"column\":"<<col<<",\"name\":\""<<d.Name<<"\",\"profile\":"<<r.Reserved0<<",\"sweep\":"<<r.Reserved1<<"}";
 }
 manifest<<"]}\n";for(unsigned n:families)Check(n==20,"twenty members in each new family");TestCoat();for(unsigned p=1;p<=5;++p)TestFurnace(p);
 printf("PASS %u checks; %u spheres; %zu triangles; %zu materials.\n",checks,spheres,scene.QueryTriangles().size(),materials.size());
 if(argc<3||std::string(argv[2])!="--test")for(unsigned p=1;p<=5;++p){unsigned i=1+(p+14)*20+10;Render(out,p,index.QuerySlabRecords()[index.QueryRecords()[i].SlabOffset]);}
 return 0;
 }catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
