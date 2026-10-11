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
 // ⚠️ ONLY the volumes that HAVE a physical centre get a viewport marker. Global systems (sky, sun, stars,
 // moons, flare, wind, the global cloud deck, precipitation, the two global fogs) used to be laid out as a
 // camera-facing shelf of discs across the top of the view — twelve editor proxies pinned over the render,
 // standing in front of the scene at every camera angle, for entities whose position is not a thing that
 // exists. They are reachable in the outliner, which is where a global system belongs. The local cloud and
 // the local fog keep their markers: those ARE world-space centres, they are how you grab the volume, and
 // the transform gizmo now follows them (GameExecution ⑤, the volume branch).
 static uint32_t Billboards(EditorBillboard* Out,uint32_t Capacity,EditorBillboardCamera& C,void* Context) noexcept {
  auto& S=*static_cast<EditorInspectorSequence*>(Context);S.Synchronize();
  const auto E=S.Camera.QuerySpatialLocation(),F=S.Camera.QueryForwardVector(),R=S.Camera.QueryRightVector(),U=S.Camera.QueryUpwardVector();
  const float Basis[4][3]={{E.x,E.y,E.z},{F.x,F.y,F.z},{R.x,R.y,R.z},{U.x,U.y,U.z}};
  std::memcpy(C.Eye,Basis[0],sizeof(C.Eye));std::memcpy(C.Forward,Basis[1],sizeof(C.Forward));
  std::memcpy(C.Right,Basis[2],sizeof(C.Right));std::memcpy(C.Up,Basis[3],sizeof(C.Up));
  C.Fov=S.Camera.QueryFieldOfViewRadians();C.Aspect=S.Camera.QueryAspectRatio();C.Near=S.Camera.QueryNearPlaneDistance();
  if(!Out||C.ViewWidth<40||C.ViewHeight<40)return 0;
  unsigned N=0;
  for(unsigned I=0;I<S.Count&&N<Capacity;++I){
   const auto Key=S.Rows[I].InspectorKey;
   if((Key>>32)!=2||uint32_t(Key)==0||uint32_t(Key)>kCelestialEntityCount||!S.Effective(I))continue;
   const float* Centre=VolumeCentre(S.Celestial,static_cast<CelestialEntity>(uint32_t(Key)-1));
   if(!Centre)continue;                       // a global system has no centre to mark
   auto& M=Out[N++];M={};M.Key=Key;M.Artwork=S.Rows[I].Artwork;M.Global=false;
   std::snprintf(M.Label,sizeof(M.Label),"%s",S.Rows[I].Label);
   std::memcpy(M.World,Centre,sizeof(M.World));
  }
  return N;
 }
 // The one place that answers "does this celestial entity live somewhere?" — the marker, the gizmo and the
 //    drag all ask it, so they cannot disagree about which entities are movable.
 static const float* VolumeCentre(CelestialSequence& Celestial,CelestialEntity Entity) noexcept {
  if(Entity==CelestialEntity::LocalCloud)return Celestial.LocalCloud.Centre;
  if(Entity==CelestialEntity::LocalFog)return Celestial.LocalFog.Centre;
  return nullptr;
 }
 // The picked row's movable volume, or none. Used by the frame loop to seat the transform gizmo.
 const float* PickedVolumeCentre(uint32_t Pick,CelestialEntity* OutEntity=nullptr) const noexcept {
  if(Pick>=Count)return nullptr;
  const auto Key=Rows[Pick].InspectorKey;
  if((Key>>32)!=2||uint32_t(Key)==0||uint32_t(Key)>kCelestialEntityCount)return nullptr;
  const auto Entity=static_cast<CelestialEntity>(uint32_t(Key)-1);
  const float* Centre=VolumeCentre(Celestial,Entity);
  if(Centre&&OutEntity)*OutEntity=Entity;
  return Centre;
 }
 bool TakeProjectionChanged() noexcept {const bool Result=ProjectionChanged;ProjectionChanged=false;return Result;}
};
}
