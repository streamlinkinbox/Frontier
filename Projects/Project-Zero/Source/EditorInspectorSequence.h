#pragma once
#include "EditorFeedSequence.h"
#include "CelestialSequence.h"
#include "FlyThroughSolver.h"
#include <cstring>
#include "../../../Engine/Editor/ViewportBillboards.h"
namespace Frontier::ProjectZero {
// Project-owned exchange around the native Inspector draw. Row identity survives moves and renames.
struct EditorInspectorSequence {
 EditorFeedSequence& Feed; CelestialSequence& Celestial; FlyThroughSolver& Camera;
 const SceneStructure& Level; const std::vector<InstanceRecord>& Live;
 EditorInstance* Rows; uint32_t& Count; EditorSheet& Sheet;
 EditorProperty* Tint=nullptr; bool ProjectionChanged=false,StarBefore=false;
 bool Effective(uint32_t I) const noexcept {
  if(!Rows[I].Visible)return false;
  uint32_t Depth=Rows[I].Depth;
  while(I&&Depth){--I;if(Rows[I].Depth<Depth){if(!Rows[I].Visible)return false;Depth=Rows[I].Depth;}}
  return true;
 }
 void Synchronize() noexcept {
  Celestial.SynchronizeWindRows(Rows,Count,kMaxEditorInstances);
  for(uint32_t I=0;I<Count;++I){const auto Key=Rows[I].InspectorKey;
   if((Key>>32)==4&&uint32_t(Key)>=1&&uint32_t(Key)<=5)Celestial.WindComponents[uint32_t(Key)-1].Shown=Effective(I);
   if(Key==0x300000000ull)Celestial.Enabled=Effective(I);
   if((Key>>32)==2&&uint32_t(Key)>0&&uint32_t(Key)<=kCelestialEntityCount){
    const auto E=static_cast<CelestialEntity>(uint32_t(Key)-1);
    Celestial.Shown[uint32_t(E)]=Effective(I);Celestial.RefreshRow(E,Rows[I]);
   }
  }
 }
 EditorProperty* StarSwitch() noexcept {
  for(uint32_t G=0;G<Sheet.GroupCount;++G)for(uint32_t P=0;P<Sheet.Groups[G].PropertyCount;++P)
   if(!std::strcmp(Sheet.Groups[G].Properties[P].Label,"Star field"))return &Sheet.Groups[G].Properties[P];
  return nullptr;
 }
 EditorSheet* Update(uint32_t Pick,bool Commit) noexcept {
  if(Pick>=Count)return nullptr;
  const auto Key=Rows[Pick].InspectorKey;
  const bool Environment=(Key>>32)==2&&uint32_t(Key)>0&&uint32_t(Key)<=kCelestialEntityCount;
  const auto Entity=static_cast<CelestialEntity>(uint32_t(Key)-1);
  if(!Commit){
   Synchronize();Tint=nullptr;
   if(Pick>=Count||Rows[Pick].InspectorKey!=Key){
    Pick=0;while(Pick<Count&&Rows[Pick].InspectorKey!=Key)++Pick;
    if(Pick==Count)return nullptr;
   }
   if((Key>>32)==4)Celestial.BuildWindComponentSheet(uint32_t(Key),Sheet);
   else if(Environment)Celestial.BuildSheet(Entity,Sheet);
   else Tint=Feed.BuildSheet(Pick,Rows,Count,&Sheet,Camera,Level,Live);
   Sheet.InspectorKey=Key;if(auto* P=StarSwitch())StarBefore=P->On;
  }else if(Sheet.InspectorKey==Key){
   if((Key>>32)==4)Celestial.ApplyWindComponentSheet(uint32_t(Key),Sheet);
   else if(Environment){
    if(Entity==CelestialEntity::Stars)if(auto* P=StarSwitch();P&&P->On!=StarBefore)Rows[Pick].Visible=P->On;
    Celestial.ApplySheet(Entity,Sheet);
   }else if((Key>>32)==1){
    const float Old=Camera.QueryFieldOfViewRadians();
    Feed.ApplyCameraSheet(uint32_t(Key)-1,Count,Level,Sheet,Camera);
    ProjectionChanged|=Camera.QueryFieldOfViewRadians()!=Old;
    if(Tint)std::memcpy(Rows[Pick].Tint,Tint->ColourTint,sizeof(Rows[Pick].Tint));
   }
   Synchronize();
  }
  return &Sheet;
 }
 static EditorSheet* Exchange(uint32_t Pick,bool Commit,void* Context) noexcept {
  return static_cast<EditorInspectorSequence*>(Context)->Update(Pick,Commit);
 }
 // Global entities have no physical centre. Keep their explicitly labelled editor proxies in a
 // camera-facing shelf; local volumes instead project their real, editable world-space centres.
 static uint32_t Billboards(EditorBillboard* Out,uint32_t Capacity,EditorBillboardCamera& C,void* Context) noexcept {
  auto& S=*static_cast<EditorInspectorSequence*>(Context);S.Synchronize();
  const auto E=S.Camera.QuerySpatialLocation(),F=S.Camera.QueryForwardVector(),R=S.Camera.QueryRightVector(),U=S.Camera.QueryUpwardVector();
  const float Basis[4][3]={{E.x,E.y,E.z},{F.x,F.y,F.z},{R.x,R.y,R.z},{U.x,U.y,U.z}};
  std::memcpy(C.Eye,Basis[0],sizeof(C.Eye));std::memcpy(C.Forward,Basis[1],sizeof(C.Forward));
  std::memcpy(C.Right,Basis[2],sizeof(C.Right));std::memcpy(C.Up,Basis[3],sizeof(C.Up));
  C.Fov=S.Camera.QueryFieldOfViewRadians();C.Aspect=S.Camera.QueryAspectRatio();C.Near=S.Camera.QueryNearPlaneDistance();
  if(!Out||C.ViewWidth<40||C.ViewHeight<40)return 0;
  unsigned N=0,Global=0;const unsigned Columns=std::max(1u,static_cast<unsigned>((C.ViewWidth-24)/44));
  for(unsigned I=0;I<S.Count&&N<Capacity;++I){
   const auto Key=S.Rows[I].InspectorKey;
   if((Key>>32)!=2||uint32_t(Key)==0||uint32_t(Key)>kCelestialEntityCount||!S.Effective(I))continue;
   const auto Entity=static_cast<CelestialEntity>(uint32_t(Key)-1);
   auto& M=Out[N++];M={};M.Key=Key;M.Artwork=S.Rows[I].Artwork;
   std::snprintf(M.Label,sizeof(M.Label),"%s",S.Rows[I].Label);
   const float* Centre=Entity==CelestialEntity::LocalCloud?S.Celestial.LocalCloud.Centre:Entity==CelestialEntity::LocalFog?S.Celestial.LocalFog.Centre:nullptr;
   M.Global=Centre==nullptr;
   if(Centre)std::memcpy(M.World,Centre,sizeof(M.World));
   else {
    const float X=30+44*float(Global%Columns),Y=30+44*float(Global/Columns);++Global;
    const float Depth=std::max(10.f,C.Near*2),Half=Depth*std::tan(C.Fov*.5f);
    for(unsigned A=0;A<3;++A)M.World[A]=C.Eye[A]+C.Forward[A]*Depth+C.Right[A]*(2*X/C.ViewWidth-1)*Half*C.Aspect+C.Up[A]*(1-2*Y/C.ViewHeight)*Half;
   }
  }
  return N;
 }
 bool TakeProjectionChanged() noexcept {const bool Result=ProjectionChanged;ProjectionChanged=false;return Result;}
};
}
