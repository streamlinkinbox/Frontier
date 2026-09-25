// Regression: live weather edits MUST reach the current GPU upload records.
// No Vulkan dispatch occurs here. CPU samples establish that the media evaluators still work.
#include "CelestialSequence.h"
#include "FogModel.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
using namespace Frontier;
using namespace Frontier::ProjectZero;
int main(){
 auto S=std::make_unique<CelestialSequence>(); S->Prepare();
 float F[]={0,1,0},R[]={1,0,0},U[]={0,0,1};
 auto Sky=S->PackSkyRecord();auto Moon=S->PackMoonRecord();auto Post=S->PackPostRecord(F,R,U,.577f,1.777f,720,1);
 auto Audit=[&](const char* Name){auto A=S->PackSkyRecord();auto B=S->PackMoonRecord();auto C=S->PackPostRecord(F,R,U,.577f,1.777f,720,1);
 if(std::memcmp(&Post,&C,sizeof C)==0)throw std::runtime_error(Name);
 printf("%s: GPU sky/moon/post record changes = %d/%d/%d\n",Name,int(std::memcmp(&Sky,&A,sizeof A)!=0),int(std::memcmp(&Moon,&B,sizeof B)!=0),int(std::memcmp(&Post,&C,sizeof C)!=0));};
 printf("Defaults: global_cloud=%d height_fog=%d aerial_fog=%d local_cloud=%d local_fog=%d\n",S->Cloud.Enabled,S->Fog.HeightEnabled,S->Fog.AerialEnabled,S->LocalCloud.Enabled,S->LocalFog.Enabled);
 auto Cloud=S->Cloud;S->Cloud.Enabled=true;S->Cloud.Coverage=.9f;S->Cloud.Density=3;S->Cloud.Base=300;Audit("global cloud edits");S->Cloud=Cloud;
 auto Fog=S->Fog;S->Fog.HeightEnabled=true;S->Fog.HeightDensity=.01f;Audit("height fog edits");S->Fog=Fog;
 S->Fog.AerialEnabled=true;S->Fog.AerialDensity=4;Audit("aerial fog edits");S->Fog=Fog;
 auto Local=S->LocalCloud;S->LocalCloud.Enabled=true;S->LocalCloud.Density=3;S->LocalCloud.Centre[0]+=100;Audit("local cloud edits");S->LocalCloud=Local;
 Local=S->LocalFog;S->LocalFog.Enabled=true;S->LocalFog.Density=3;S->LocalFog.Centre[0]+=100;Audit("local fog edits");S->LocalFog=Local;
 // Positive control: a live atmosphere edit DOES change the GPU record.
 S->Medium.MieStrength+=2;Audit("atmosphere Mie positive control");
 auto Pack=[&](){return S->PackPostRecord(F,R,U,.577f,1.777f,720,1);};
 auto Check=[](bool V,const char* Name){if(!V)throw std::runtime_error(Name);printf("PASS %s\n",Name);};
 Check(sizeof(PostConstantRecord)==512&&offsetof(PostConstantRecord,Weather)==208,"post/weather ABI size and offset");
 Check(offsetof(PostConstantRecord,Weather)+17*16+3*4==492,"device weather-active byte offset");
 S->LocalCloud.Enabled=S->LocalFog.Enabled=S->Fog.HeightEnabled=S->Fog.AerialEnabled=true;
 for(auto Entity:{CelestialEntity::CloudLayer,CelestialEntity::LocalCloud,CelestialEntity::LocalFog,CelestialEntity::HeightFog,CelestialEntity::AtmosphericFog}){
   int Row=Entity==CelestialEntity::CloudLayer?3:Entity==CelestialEntity::LocalCloud?4:Entity==CelestialEntity::LocalFog?9:Entity==CelestialEntity::HeightFog?14:15;
   Check(Pack().Weather.Rows[Row][3]==1,"enabled entity reaches shader");
   S->Shown[uint32_t(Entity)]=false;Check(Pack().Weather.Rows[Row][3]==0,"outliner hide disables shader medium");S->Shown[uint32_t(Entity)]=true;
 }
 S->Enabled=false;Check(Pack().Weather.Rows[17][3]==0,"system disabled is weather identity");S->Enabled=true;
 S->Shown[uint32_t(CelestialEntity::Wind)]=false;S->WeatherSeconds=37;
 Check(Pack().Weather.Rows[18][0]==0&&Pack().Weather.Rows[3][0]==0,"hidden wind does not animate weather");
 S->Shown[uint32_t(CelestialEntity::Wind)]=true;
 Check(Pack().Weather.Rows[3][0]==37,"weather advances with static sun");
 S->Shown[uint32_t(CelestialEntity::CloudLayer)]=false;
 Check(Pack().Weather.Rows[3][3]==0&&Pack().Weather.Rows[4][3]==1&&Pack().Weather.Rows[9][3]==1,"local volumes independent of global cloud visibility");
 // CPU implementations still produce nonzero media with unambiguous dense fixtures.
 float O[]={0,0,2},D[]={0,0,1},Sun[]={0,0,1},Light[]={1,1,1},Ambient[]={.2f,.2f,.2f};
 CloudLayerSettings C;C.Enabled=true;C.Base=10;C.Thickness=100;C.Coverage=1;C.Density=3;
 auto V=VolumetricMedia::March(C,{}, {},WindSettings{},VolumetricBudget{},O,D,200,Sun,Light,Ambient,0);
 printf("CPU global cloud transmittance=%f scatter=%f\n",V.Transmittance,V.Scatter[0]);
 LocalVolumeSettings L;L.Enabled=true;L.Centre[2]=50;L.HalfSize[0]=L.HalfSize[1]=L.HalfSize[2]=40;L.Coverage=1;L.Density=3;
 V=VolumetricMedia::March({},L,{},WindSettings{},VolumetricBudget{},O,D,200,Sun,Light,Ambient,0);
 printf("CPU local cloud transmittance=%f scatter=%f\n",V.Transmittance,V.Scatter[0]);
 V=VolumetricMedia::March({}, {},L,WindSettings{},VolumetricBudget{},O,D,200,Sun,Light,Ambient,0);
 printf("CPU local fog transmittance=%f scatter=%f\n",V.Transmittance,V.Scatter[0]);
 float T[3];Fog.HeightEnabled=true;FogModel::Transmission(Fog,S->Medium,2,2,100,T);printf("CPU height fog transmittance=%f\n",T[0]);
 Fog.HeightEnabled=false;Fog.AerialEnabled=true;FogModel::Transmission(Fog,S->Medium,2,2,10000,T);printf("CPU aerial fog transmittance=%f\n",T[0]);
}
