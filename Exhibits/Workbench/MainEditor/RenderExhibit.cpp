// Actual native EditorHost captures. The web exhibit only displays these PNGs.
#include "EditorInspectorSequence.h"
#include "EditorHost.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <memory>
#include <filesystem>
#include <fstream>
#include <stdexcept>
using namespace Frontier;
using namespace Frontier::ProjectZero;
int main(int Argc,char** Argv){
 if(Argc!=2)return 2;
 const std::filesystem::path Output=Argv[1];std::filesystem::create_directories(Output);
 auto Feed=std::make_unique<EditorFeedSequence>();auto Sky=std::make_unique<CelestialSequence>();
 auto Level=std::make_unique<SceneStructure>();auto Camera=std::make_unique<FlyThroughSolver>();
 auto Sheet=std::make_unique<EditorSheet>();auto Rows=std::make_unique<EditorInstance[]>(kMaxEditorInstances);
 // Deliberate exhibit fixture: no imported level, environment eyes on, fixed time step.
 Sky->Enabled=true;for(auto& Shown:Sky->Shown)Shown=true;
 const float At[3]={0,0,2};Sky->Tick(1.f/60,At,0);
 uint32_t Count=Feed->FillRoster(Rows.get(),*Level);Count+=Sky->AppendRoster(Rows.get(),Count,kMaxEditorInstances);
 Camera->AssignAspectRatio(16.f/9);
 EditorInspectorSequence Session{*Feed,*Sky,*Camera,*Level,Level->QueryInstances(),Rows.get(),Count,*Sheet};
 ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DisplaySize={1440,1000};IO.DeltaTime=1.f/60;
 IO.ConfigFlags|=ImGuiConfigFlags_DockingEnable;IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
 auto Host=std::make_unique<EditorHost>();Host->ApplyTheme();Host->AssignInspectorWorkspace(true);Host->AssignInspectorExchange(&EditorInspectorSequence::Exchange,&Session);
 auto Tick=[&](){ImGui::NewFrame();Host->Record(Rows.get(),Count,Sheet.get());ImGui::Render();FrontierProof::AcknowledgeTextures();};
 auto Rest=[&](){for(int I=0;I<3;++I)Tick();};
 auto Capture=[&](const std::string& Name){std::vector<unsigned char> Pixels(1440*1000*3,24);for(auto* D:ImGui::GetDrawData()->CmdLists)FrontierProof::Draw(D,Pixels.data(),1440,1000,{0,0},{1,1});if(!stbi_write_png((Output/Name).string().c_str(),1440,1000,3,Pixels.data(),1440*3))throw std::runtime_error("capture failed");};
 struct Entry{const char* Slug;const char* Label;uint64_t Key;};
 const Entry Entries[]={
  {"camera","Main Camera",0},{"cine","Cine Camera",0},
  {"atmosphere","Atmosphere",0x200000001ull+uint32_t(CelestialEntity::Atmosphere)},
  {"sun","Sun",0x200000001ull+uint32_t(CelestialEntity::Sun)},
  {"sky","Sky scattering",0x200000001ull+uint32_t(CelestialEntity::Sky)},
  {"moon","Moon",0x200000001ull+uint32_t(CelestialEntity::Moons)},
  {"stars","Stars",0x200000001ull+uint32_t(CelestialEntity::Stars)},
  {"clouds","Global clouds",0x200000001ull+uint32_t(CelestialEntity::CloudLayer)},
  {"local-cloud","Local clouds",0x200000001ull+uint32_t(CelestialEntity::LocalCloud)},
  {"height-fog","Height fog",0x200000001ull+uint32_t(CelestialEntity::HeightFog)},
  {"aerial-fog","Atmospheric fog",0x200000001ull+uint32_t(CelestialEntity::AtmosphericFog)},
  {"local-fog","Local fog",0x200000001ull+uint32_t(CelestialEntity::LocalFog)},
  {"wind","Wind",0x200000001ull+uint32_t(CelestialEntity::Wind)},
  {"precipitation","Precipitation",0x200000001ull+uint32_t(CelestialEntity::Precipitation)},
  {"rainbow","Rainbow",0x200000001ull+uint32_t(CelestialEntity::Rainbow)},
  {"lens","Lens flare",0x200000001ull+uint32_t(CelestialEntity::LensFlare)}
 };
 std::ofstream Manifest(Output/"captures.json");Manifest<<"[\n";bool First=true;
 for(const auto& E:Entries){
  uint32_t Pick=kNoEditorInstance;
  for(uint32_t I=0;I<Count;++I)if(E.Key?Rows[I].InspectorKey==E.Key:!std::strcmp(Rows[I].Label,E.Label))Pick=I;
  if(Pick==kNoEditorInstance)throw std::runtime_error("missing project row");
  Host->PickInstance(Pick);Rest();
  for(auto* W:ImGui::GetCurrentContext()->Windows)if(W->Active&&W->RootWindow&&!std::strcmp(W->RootWindow->Name,"Outliner")&&W->ScrollMax.y>0)ImGui::SetScrollY(W,Pick>=Count-6?W->ScrollMax.y:0);
  for(auto* W:ImGui::GetCurrentContext()->Windows)if(W->Active&&W->RootWindow&&!std::strcmp(W->RootWindow->Name,"Inspector"))ImGui::SetScrollY(W,0);
  Rest();Capture(std::string(E.Slug)+".png");
  bool Lower=false;
  for(auto* W:ImGui::GetCurrentContext()->Windows)if(W->Active&&W->RootWindow&&!std::strcmp(W->RootWindow->Name,"Inspector")&&W->ScrollMax.y>40){ImGui::SetScrollY(W,W->ScrollMax.y);Lower=true;}
  if(Lower){Rest();Capture(std::string(E.Slug)+"-lower.png");}
  if(!First)Manifest<<",\n";First=false;
  Manifest<<"{\"id\":\""<<E.Slug<<"\",\"label\":\""<<E.Label<<"\",\"lower\":"<<(Lower?"true":"false")<<"}";
 }
 Manifest<<"\n]\n";
 Host.reset();for(auto* T:ImGui::GetPlatformIO().Textures){T->SetTexID(ImTextureID_Invalid);T->SetStatus(ImTextureStatus_Destroyed);}ImGui::DestroyContext();
 std::printf("Rendered 16 native editor selections at 1440 x 1000; real host, fixed exhibit fixture, CPU draw data.\n");
}
