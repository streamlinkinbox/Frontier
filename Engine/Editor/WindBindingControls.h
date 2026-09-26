#pragma once
#include "EditorInstance.h"
#include <imgui.h>
#include <cstring>
namespace Frontier {
inline void RecordWindBindingControls(EditorSheet& Sheet){
 EditorProperty *Own=nullptr,*Source=nullptr;
 for(uint32_t G=0;G<Sheet.GroupCount;++G)for(uint32_t I=0;I<Sheet.Groups[G].PropertyCount;++I){auto& P=Sheet.Groups[G].Properties[I];
  if(!std::strcmp(P.Label,"Own Wind"))Own=&P;
  if(!std::strcmp(P.Label,"Wind Source"))Source=&P;
 }
 if(!Own||!Source)return;
 ImGui::PushID("wind-binding");ImGui::SeparatorText("Wind source");
 ImGui::Checkbox("Own wind component",&Own->On);
 ImGui::TextWrapped("Own wind adds an editable child. Shared sources reference the same component, not a copy.");
 const uint32_t Pick=Source->Picked<Source->OptionCount?Source->Picked:0;
 if(ImGui::BeginCombo("Source",Source->Options[Pick])){
  for(uint32_t I=0;I<Source->OptionCount;++I)if(ImGui::Selectable(Source->Options[I],I==Pick))Source->Picked=I;
  ImGui::EndCombo();
 }
 ImGui::TextWrapped("Follow Wind still controls advection. Removing a source falls back to global wind. Analytic height/aerial fog is horizontally uniform; it has no advected noise.");
 ImGui::PopID();
}
}
