#include "EditorInspectorSequence.h"
#include "EditorHost.h"
#include "IconPresentation.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <memory>
#include <cstdio>
#include <stdexcept>
using namespace Frontier;using namespace Frontier::ProjectZero;
unsigned Checks=0;
void Check(bool V,const char* N){++Checks;if(!V)throw std::runtime_error(N);}
EditorProperty& Find(EditorSheet& S,const char* N){for(unsigned G=0;G<S.GroupCount;++G)for(unsigned P=0;P<S.Groups[G].PropertyCount;++P)if(!std::strcmp(S.Groups[G].Properties[P].Label,N))return S.Groups[G].Properties[P];throw std::runtime_error(N);}
int main(){
 auto Feed=std::make_unique<EditorFeedSequence>();auto Sky=std::make_unique<CelestialSequence>();auto Level=std::make_unique<SceneStructure>();auto Camera=std::make_unique<FlyThroughSolver>();auto Sheet=std::make_unique<EditorSheet>();auto Rows=std::make_unique<EditorInstance[]>(kMaxEditorInstances);
 unsigned Count=Feed->FillRoster(Rows.get(),*Level),Folder=Count;Count+=Sky->AppendRoster(Rows.get(),Count,kMaxEditorInstances);
 EditorInspectorSequence Session{*Feed,*Sky,*Camera,*Level,Level->QueryInstances(),Rows.get(),Count,*Sheet};
 auto Index=[&](uint64_t Key){for(unsigned I=0;I<Count;++I)if(Rows[I].InspectorKey==Key)return I;throw std::runtime_error("key missing");};
 unsigned Main=0,Cine=0;for(unsigned I=0;I<Count;++I){if(!std::strcmp(Rows[I].Label,"Main Camera"))Main=I;if(!std::strcmp(Rows[I].Label,"Cine Camera"))Cine=I;}
 Session.Update(Main,false);Find(*Sheet,"Focal Length").Figure=100;Session.Update(Main,true);Check(Session.TakeProjectionChanged(),"projection invalidation");const float Fov=Camera->QueryFieldOfViewRadians();
 Session.Update(Cine,false);Check(!Sheet->CameraLive,"same-frame cine selection");Find(*Sheet,"Focal Length").Figure=120;Session.Update(Cine,true);Check(Camera->QueryFieldOfViewRadians()==Fov,"cine independent");
 const auto MainKey=Rows[Main].InspectorKey;std::swap(Rows[Main],Rows[Cine]);Main=Index(MainKey);Session.Update(Main,false);Check(Sheet->CameraLive,"reordered main owner");Find(*Sheet,"Focal Length").Figure=50;Session.Update(Main,true);Check(Camera->QueryFieldOfViewRadians()>Fov,"reordered edit reaches real camera");
 unsigned Stars=Index(0x200000001ull+uint32_t(CelestialEntity::Stars));Rows[Stars].Visible=true;Session.Update(Stars,false);Find(*Sheet,"Star field").On=false;Session.Update(Stars,true);Check(!Rows[Stars].Visible&&!Sky->Shown[uint32_t(CelestialEntity::Stars)],"star switch writes eye");
 Rows[Stars].Visible=true;Rows[Folder].Visible=false;Session.Update(Stars,false);Session.Update(Stars,true);Check(Rows[Stars].Visible&&!Sky->Shown[uint32_t(CelestialEntity::Stars)],"hidden parent preserves author visibility");Rows[Folder].Visible=true;Session.Synchronize();Check(Sky->Shown[uint32_t(CelestialEntity::Stars)],"parent restores effective star visibility");
 unsigned Wind=Index(0x200000001ull+uint32_t(CelestialEntity::Wind));Session.Update(Wind,false);Find(*Sheet,"Speed").Figure=19;Session.Update(Wind,true);Check(Sky->Wind.Speed==19,"wind edits project simulation field");
 unsigned Clouds=Index(0x200000001ull+uint32_t(CelestialEntity::CloudLayer));Session.Update(Clouds,false);Find(*Sheet,"Density").Figure=2;Session.Update(Clouds,true);Check(Sky->Cloud.Density==2,"cloud edits density source");
 unsigned Rain=Index(0x200000001ull+uint32_t(CelestialEntity::Precipitation));Session.Update(Rain,false);Find(*Sheet,"Intensity").Figure=36;Session.Update(Rain,true);Check(Sky->Precip.RateMillimetresPerHour==36,"precipitation edits emitter source");
 Rows[Clouds].Visible=false;Rows[Rain].Visible=true;Session.Synchronize();Check(!Sky->Shown[uint32_t(CelestialEntity::Precipitation)]&&Rows[Rain].Visible,"cloud parent gates rain without destroying eye");Rows[Clouds].Visible=true;Session.Synchronize();Check(Sky->Shown[uint32_t(CelestialEntity::Precipitation)],"cloud eye restores rain");
 ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DisplaySize={1440,1000};IO.DeltaTime=1.f/60;IO.ConfigFlags|=ImGuiConfigFlags_DockingEnable;IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
 auto Host=std::make_unique<EditorHost>();Host->ApplyTheme();Host->AssignInspectorWorkspace(true);Host->AssignInspectorExchange(&EditorInspectorSequence::Exchange,&Session);
 auto Tick=[&](){ImGui::NewFrame();Host->Record(Rows.get(),Count,Sheet.get());ImGui::Render();FrontierProof::AcknowledgeTextures();};
 for(unsigned I=Folder+1;I<Count;++I){Host->PickInstance(I);Tick();Check(Sheet->InspectorKey==Rows[I].InspectorKey,"host selection sheet same-frame ownership");Check(Sheet->Appearance!=EditorSheetAppearance::Generic,"completed environment native route");}
 Host->PickInstance(Main);for(int I=0;I<3;++I)Tick();Check(Sheet->CameraLive,"host camera route");auto* V=ImGui::FindWindowByName("Viewport");Check(!V||!V->Active,"no viewport workspace");
 ImGuiWindow* Slider=nullptr;for(auto* W:ImGui::GetCurrentContext()->Windows)if(W->Active&&std::strstr(W->Name,"/Focal Length_"))Slider=W;Check(Slider!=nullptr,"native main camera slider");
 const float Before=Camera->QueryFieldOfViewRadians();IO.AddMousePosEvent(Slider->Pos.x+Slider->Size.x*.65f,Slider->Pos.y+15);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMouseButtonEvent(0,false);Tick();Check(Camera->QueryFieldOfViewRadians()!=Before,"host native input commits camera");
 std::vector<unsigned char> Pixels(1440*1000*3,24);for(auto* D:ImGui::GetDrawData()->CmdLists)FrontierProof::Draw(D,Pixels.data(),1440,1000,{0,0},{1,1});Check(stbi_write_png("Exhibits/Gallery/MainEditorNative/MainEditor-Camera.png",1440,1000,3,Pixels.data(),1440*3)!=0,"capture");
 for(auto Icon:{IconSymbol::Camera,IconSymbol::FolderEnvironment,IconSymbol::Sun,IconSymbol::Moon,IconSymbol::OutlinerStars,IconSymbol::Clouds,IconSymbol::LocalCloud,IconSymbol::LocalFog,IconSymbol::SkyScattering,IconSymbol::Wind,IconSymbol::OutlinerPrecipitation,IconSymbol::Rainbow,IconSymbol::LensFlare})std::printf("ICON %s: %s — %s\n",IconArt::Name(Icon),IconArt::ResultName(IconPresentation::Result(Icon)),IconPresentation::Diagnostic(Icon));

 // Construct is the actual native EditorHost route, not a browser mock.
 for(unsigned I=0;I<Count;++I)Check(NativeConstructPanel::Supported(Rows[I])==(Rows[I].Category!=EditorInstanceCategory::Folder),"catalogue projects only real entity rows");
 for(unsigned I=0;I<Count;++I){
  Check(std::strcmp(Rows[I].Label,"Terrain")&&std::strcmp(Rows[I].Label,"Forest")&&std::strcmp(Rows[I].Label,"Water"),"no unsupported entity templates");
  if(!std::strcmp(Rows[I].Label,"Scene"))Check(Rows[I].Artwork==IconSymbol::FolderScene,"HTML scene folder artwork");
  if(!std::strcmp(Rows[I].Label,"World"))Check(Rows[I].Artwork==IconSymbol::FolderWorld,"HTML world folder artwork");
 }
 IconArt ApprovedArt("EngineContent/Icons");
 for(auto Icon:{IconSymbol::FolderScene,IconSymbol::FolderWorld,IconSymbol::FolderEnvironment,IconSymbol::Clouds,IconSymbol::LocalCloud,IconSymbol::LocalFog}){
  auto Art=ApprovedArt.Rasterize(Icon,30,30);Check(Art->Result==IconResult::Ready,"approved native artwork resolves");
 }
 auto Capture=[&](const char* Name){std::vector<unsigned char> P(1440*1000*3,24);for(auto* D:ImGui::GetDrawData()->CmdLists)FrontierProof::Draw(D,P.data(),1440,1000,{0,0},{1,1});Check(stbi_write_png(Name,1440,1000,3,P.data(),1440*3)!=0,"native construct capture");};
 auto Click=[&](ImVec2 At){IO.AddMousePosEvent(At.x,At.y);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMouseButtonEvent(0,false);Tick();};
 Host->OpenConstruct();for(int I=0;I<4;++I)Tick();Check(Host->QueryConstructOpen(),"native Construct opens");
 Capture("Exhibits/Gallery/NativeConstruct/Catalogue.png");
 Click(Host->QueryConstructSearchCentre());IO.AddInputCharactersUTF8("Wind");Tick();Tick();
 const uint64_t WindKey=Rows[Wind].InspectorKey;
 ImVec2 WindCentre=Host->QueryConstructTileCentre(WindKey);Check(WindCentre.x>0,"native search finds Wind");
 Click(WindCentre);for(int I=0;I<24;++I)Tick();
 Check(Host->QueryConstructKey()==WindKey&&Host->QueryPickedInstance()==Wind,"native tile selects the engine row");
 Check(Sheet->Appearance==EditorSheetAppearance::Wind,"slide mounts native Wind inspector");
 auto Enable=Host->QueryConstructEnableCentre();Check(Enable.x>0,"inactive entity offers native enable action");Click(Enable);
 Check(Sky->Shown[uint32_t(CelestialEntity::Wind)]&&Sky->Enabled,"native enable reaches world including ancestor gate");
 ImGuiWindow* Speed=nullptr;for(auto* W:ImGui::GetCurrentContext()->Windows)if(W->Active&&std::strstr(W->Name,"/Speed_"))Speed=W;
 Check(Speed!=nullptr,"native Construct Wind slider present");
 float OldSpeed=Sky->Wind.Speed;Click({Speed->Pos.x+Speed->Size.x*.71f,Speed->Pos.y+15});
 Check(Sky->Wind.Speed!=OldSpeed,"Construct slider commits to engine WindField settings");
 Capture("Exhibits/Gallery/NativeConstruct/Wind-properties.png");
 // Identity is not the displayed ordinal: reorder while properties remain open.
 std::swap(Rows[Wind],Rows[Clouds]);Tick();Check(Sheet->InspectorKey==WindKey&&Rows[Host->QueryPickedInstance()].InspectorKey==WindKey,"Construct resolves key and selection after outliner reorder");
 IO.AddKeyEvent(ImGuiKey_Escape,true);Tick();IO.AddKeyEvent(ImGuiKey_Escape,false);for(int I=0;I<24;++I)Tick();
 Check(Host->QueryConstructOpen(),"Escape slides back before closing");
 IO.AddKeyEvent(ImGuiKey_Escape,true);Tick();IO.AddKeyEvent(ImGuiKey_Escape,false);Tick();Check(!Host->QueryConstructOpen(),"second Escape closes");
 Host.reset();for(auto* T:ImGui::GetPlatformIO().Textures){T->SetTexID(ImTextureID_Invalid);T->SetStatus(ImTextureStatus_Destroyed);}ImGui::DestroyContext();std::printf("PASS %u checks: real host selection, native input, project bindings, reorder identity and visibility. CPU only.\n",Checks);
}
