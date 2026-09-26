#include "BakedIconArt.h"
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace Frontier;
int main(){
 unsigned Checks=0;auto Check=[&](bool V,const char* Why){++Checks;if(!V)throw std::runtime_error(Why);};
 IconArt Art("EngineContent/Icons");
 for(auto Symbol:{IconSymbol::Sun,IconSymbol::Moon}){
  const std::string Name=Symbol==IconSymbol::Sun?"sun":"moon";
  std::ifstream File("EngineContent/Icons/"+Name+".svg",std::ios::binary);std::string Source((std::istreambuf_iterator<char>(File)),{});
  const auto Path=std::filesystem::path("EngineContent/Icons/Baked")/(Name+".rgba");
  for(float Scale:{1.f,2.f,3.f,4.f}){
   auto R=Art.Rasterize(Symbol,24,24,Scale);Check(R->Result==IconResult::Ready&&!R->Substitute,"approved celestial bake renders");
   Check(R->Width==uint32_t(24*Scale)&&R->Rgba.size()==size_t(R->Width)*R->Height*4,"physical scale dimensions");
   unsigned Visible=0,Clear=0;for(size_t P=3;P<R->Rgba.size();P+=4){Visible+=R->Rgba[P]>100;Clear+=R->Rgba[P]==0;}
   Check(Visible>20&&Clear>20,"real image plus transparent margins");Check(Art.Rasterize(Symbol,24,24,Scale)==R,"cache reuse");
  }
  IconRaster R;R.Width=48;R.Height=24;Check(LoadApprovedIconBake(Path,Source,R),"rectangular request");Check(R.Rgba[3]==0,"letterbox transparent");
  Check(!LoadApprovedIconBake(Path,Source+" ",R),"stale source refused");
  Check(!LoadApprovedIconBake(Path.string()+".missing",Source,R),"missing bake refused");
  R.Width=513;Check(!LoadApprovedIconBake(Path,Source,R),"oversized request refused");R.Width=0;Check(!LoadApprovedIconBake(Path,Source,R),"zero request refused");
  const auto Temp=std::filesystem::path(".cache")/("invalid-"+Name+".rgba");
  {std::ofstream F(Temp,std::ios::binary);F<<"FIB1";}
  R.Width=24;Check(!LoadApprovedIconBake(Temp,Source,R),"truncated header refused");
  {std::ofstream F(Temp,std::ios::binary);F<<"FIB1"<<std::string(32,'\0');}
  Check(!LoadApprovedIconBake(Temp,Source,R),"invalid dimensions refused");std::filesystem::remove(Temp);
 }
 Check(Art.Rasterize(IconSymbol::Clouds,24,24)->Result==IconResult::Unsupported,"unrelated unsupported artwork remains explicit");
 std::cout<<"PASS "<<Checks<<" checks: Sun/Moon native baked rendering, 1x–4x, alpha, cache, missing/stale/malformed bake rejection.\n";
}
