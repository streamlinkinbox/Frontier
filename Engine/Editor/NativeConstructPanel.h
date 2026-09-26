#pragma once
#include "InspectorPanel.h"
#include "../DisplayPresentation/IconPresentation.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace Frontier {
// The native catalogue is a projection of the actual project roster, not the
// browser template library. Unsupported world generators cannot enter it.
// Existing environment systems are singleton records: activate/edit, not clone.
class NativeConstructPanel {
public:
 using Exchange = EditorSheet* (*)(uint32_t,bool,void*) noexcept;
 void Open() noexcept { Open_=true; Details_=false; Slide_=0; Search_[0]=0; }
 ImVec2 EnableCentre() const noexcept {return EnableCentre_;}
 ImVec2 SearchCentre() const noexcept {return SearchCentre_;}
 bool IsOpen() const noexcept {return Open_;}
 bool ShowingProperties() const noexcept {return Open_&&Details_;}
 uint64_t QueryKey() const noexcept {return Key_;}
 ImVec2 TileCentre(uint64_t Key) const noexcept {for(const auto& T:Tiles_)if(T.Key==Key)return T.Centre;return {-1,-1};}
 static bool Supported(const EditorInstance& Row) noexcept {
  return Row.InspectorKey!=0&&Row.Category!=EditorInstanceCategory::Folder&&Row.Category<EditorInstanceCategory::Count;
 }
 static IconSymbol Artwork(const EditorInstance& Row) noexcept {
  if(Row.Artwork!=IconSymbol::Count)return Row.Artwork;
  if(Row.Glyph==EditorGlyph::Fog||Row.Glyph==EditorGlyph::AerialFog)return IconSymbol::Fog;
  if(Row.Glyph==EditorGlyph::Effects)return IconSymbol::EnvironmentExposure;
  if(Row.Category==EditorInstanceCategory::Camera)return IconSymbol::Camera;
  if(Row.Category==EditorInstanceCategory::Geometry)return IconSymbol::EditorMesh;
  if(Row.Category==EditorInstanceCategory::Light)return IconSymbol::EditorPointLight;
  return IconSymbol::SkyScattering;
 }
 uint32_t Record(EditorInstance* Rows,uint32_t Count,InspectorPanel& Inspector,Exchange Fn,void* Context) noexcept {
  uint32_t Pick=kNoEditorInstance;
  if(!Open_)return Pick;
  if(ImGui::IsKeyPressed(ImGuiKey_Escape)){if(Details_)Details_=false;else Open_=false;}
  if(!Open_)return Pick;
  auto* View=ImGui::GetMainViewport();
  const ImVec2 Size(std::min(860.f,View->WorkSize.x-24.f),std::min(760.f,View->WorkSize.y-24.f));
  ImGui::SetNextWindowSize(Size,ImGuiCond_Always);
  ImGui::SetNextWindowPos({View->WorkPos.x+(View->WorkSize.x-Size.x)*.5f,View->WorkPos.y+(View->WorkSize.y-Size.y)*.5f},ImGuiCond_Appearing);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,IM_COL32(23,23,23,255));
  ImGui::PushStyleColor(ImGuiCol_ChildBg,IM_COL32(23,23,23,255));
  ImGui::PushStyleColor(ImGuiCol_Button,IM_COL32(38,38,38,255));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,IM_COL32(52,52,52,255));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,IM_COL32(65,65,65,255));
  ImGui::PushStyleColor(ImGuiCol_Border,IM_COL32(55,55,55,255));
  ImGui::PushStyleColor(ImGuiCol_FrameBg,IM_COL32(18,18,18,255));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,12.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{20,18});
  const bool Draw=ImGui::Begin("Construct",&Open_,ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
  if(Draw){
   ImGui::TextUnformatted("CONSTRUCT  /  FRONTIER");ImGui::SameLine();
   ImGui::TextDisabled(Details_?"     01 Entities  >  02 Properties":"     01 Entities  >  02 Properties");
   ImGui::Separator();
   const float Target=Details_?1.f:0.f;
   const float Delta=std::min(ImGui::GetIO().DeltaTime,0.1f)/.28f;
   Slide_=Slide_<Target?std::min(Target,Slide_+Delta):std::max(Target,Slide_-Delta);
   const float Smooth=Slide_*Slide_*(3.f-2.f*Slide_);
   const ImVec2 Region=ImGui::GetContentRegionAvail();
   ImGui::BeginChild("##construct-slide",Region,ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
   const ImVec2 Origin=ImGui::GetCursorScreenPos();
   if(Slide_<1.f){
    ImGui::SetCursorScreenPos({Origin.x-Smooth*Region.x,Origin.y});
    ImGui::BeginChild("##construct-catalogue",Region,ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginDisabled(Details_);
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##construct-search","Search existing engine entities...",Search_,sizeof(Search_));
    {auto A=ImGui::GetItemRectMin(),B=ImGui::GetItemRectMax();SearchCentre_={(A.x+B.x)*.5f,(A.y+B.y)*.5f};}
    constexpr const char* Groups[]={"All","Environment","Weather","Cameras","Geometry","Lighting"};
    ImGui::BeginChild("##construct-categories",{138,0});
    for(int G=0;G<6;++G)if(ImGui::Selectable(Groups[G],Group_==G,0,{0,34}))Group_=G;
    ImGui::EndChild();ImGui::SameLine();
    ImGui::BeginChild("##construct-tiles",{0,0});Tiles_.clear();
    unsigned Written=0;const float Width=ImGui::GetContentRegionAvail().x;
    const unsigned Columns=std::max(1u,unsigned(Width/155.f));const float TileWidth=(Width-(Columns-1)*8.f)/Columns;
    for(uint32_t I=0;I<Count;++I){auto& Row=Rows[I];if(!Supported(Row)||(Group_&&Group(Row)!=Group_)||!Matches(Row.Label))continue;
     if(Written%Columns)ImGui::SameLine(0,8);
     ImGui::PushID(int(I));const ImVec2 At=ImGui::GetCursorScreenPos();
     ImGui::InvisibleButton("##entity",{TileWidth,116});
     const bool Hit=ImGui::IsItemClicked();auto* D=ImGui::GetWindowDrawList();
     D->AddRectFilled(At,{At.x+TileWidth,At.y+116},ImGui::IsItemHovered()?IM_COL32(43,43,43,255):IM_COL32(32,32,32,255),7);
     D->AddRect(At,{At.x+TileWidth,At.y+116},IM_COL32(58,58,58,255),7);
     IconPresentation::Draw(D,Artwork(Row),{At.x+(TileWidth-52)*.5f,At.y+8},52);
     D->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{At.x+9,At.y+69},IM_COL32(216,216,216,255),Row.Label,nullptr,TileWidth-18);
     Tiles_.push_back({Row.InspectorKey,{At.x+TileWidth*.5f,At.y+40}});
     if(Hit){Key_=Row.InspectorKey;Details_=true;Pick=I;}
     ImGui::PopID();++Written;
    }
    if(!Written)ImGui::TextDisabled("No matching engine entities.");
    ImGui::Spacing();ImGui::TextWrapped("Existing scene records only. Select an entity to activate it or edit its native properties.");
    ImGui::EndChild();ImGui::EndDisabled();ImGui::EndChild();
   }
   if(Slide_>0.f){
    ImGui::SetCursorScreenPos({Origin.x+(1.f-Smooth)*Region.x,Origin.y});
    ImGui::BeginChild("##construct-properties",Region,ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginDisabled(!Details_||Slide_<1.f);
    if(ImGui::Button("< Entities"))Details_=false;
    uint32_t Index=kNoEditorInstance;for(uint32_t I=0;I<Count;++I)if(Rows[I].InspectorKey==Key_){Index=I;break;}
    if(Index<Count){
     Pick=Index; // Keep selection attached to the key when rows move.
     auto& Row=Rows[Index];ImGui::SameLine();ImGui::TextUnformatted(Row.Label);
     bool Enabled=Row.Visible;uint32_t ParentDepth=Row.Depth;
     for(uint32_t I=Index;I>0&&ParentDepth;){--I;if(Rows[I].Depth<ParentDepth){Enabled&=Rows[I].Visible;ParentDepth=Rows[I].Depth;}}
     EnableCentre_={-1,-1};
     if(!Enabled){ImGui::SameLine();if(ImGui::Button("Enable in world")){Row.Visible=true;uint32_t Depth=Row.Depth;for(uint32_t I=Index;I>0&&Depth;){--I;if(Rows[I].Depth<Depth){Rows[I].Visible=true;Depth=Rows[I].Depth;}}}
      auto A=ImGui::GetItemRectMin(),B=ImGui::GetItemRectMax();EnableCentre_={(A.x+B.x)*.5f,(A.y+B.y)*.5f};
     }
     ImGui::Separator();
     ImGui::BeginChild("##construct-inspector",{0,0},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);
     if(Fn){auto* Sheet=Fn(Index,false,Context);Inspector.Record(&Row,Index,Sheet,true);Fn(Index,true,Context);}
     else ImGui::TextWrapped("This project has not connected its native inspector exchange.");
     ImGui::EndChild();
    }else ImGui::TextDisabled("Entity no longer exists. Return to Entities.");
    ImGui::EndDisabled();ImGui::EndChild();
   }
   ImGui::EndChild();
  }
  ImGui::End();ImGui::PopStyleVar(2);ImGui::PopStyleColor(7);return Pick;
 }
private:
 struct Tile {uint64_t Key;ImVec2 Centre;};std::vector<Tile> Tiles_;ImVec2 SearchCentre_{},EnableCentre_{-1,-1};
 bool Open_=false,Details_=false;float Slide_=0;uint64_t Key_=0;int Group_=0;char Search_[80]={};
 bool Matches(const char* Label) const noexcept {if(!Search_[0])return true;for(const char* P=Label;*P;++P){size_t I=0;while(Search_[I]&&P[I]&&std::tolower(static_cast<unsigned char>(P[I]))==std::tolower(static_cast<unsigned char>(Search_[I])))++I;if(!Search_[I])return true;}return false;}
 static int Group(const EditorInstance& Row) noexcept {
  switch(Row.Glyph){case EditorGlyph::Wind:case EditorGlyph::Rain:case EditorGlyph::Cloud:case EditorGlyph::VolumeClouds:case EditorGlyph::LocalCloud:case EditorGlyph::VolumeFog:case EditorGlyph::Fog:case EditorGlyph::AerialFog:return 2;default:break;}
  if((Row.InspectorKey>>32)==2)return 1;
  if(Row.Category==EditorInstanceCategory::Camera)return 3;
  if(Row.Category==EditorInstanceCategory::Geometry)return 4;
  if(Row.Category==EditorInstanceCategory::Light)return 5;
  return 1;
 }
};
}
