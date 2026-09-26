#include "CelestialSequence.h"
#include "InspectorPanel.h"
#include "ControlPanel.h"
#include "SkyBakePreview.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <limits>
using namespace Frontier;
using namespace Frontier::ProjectZero;
static unsigned Checks=0;
static void Check(bool V,const char* S){++Checks;if(!V)throw std::runtime_error(S);}
static EditorProperty& Find(EditorSheet& S,const char* Name){for(auto& G:S.Groups)for(unsigned I=0;I<G.PropertyCount;++I)if(!std::strcmp(G.Properties[I].Label,Name))return G.Properties[I];throw std::runtime_error(Name);}
static ImGuiWindow* Window(const char* Name){for(auto* W:ImGui::GetCurrentContext()->Windows)if(std::strstr(W->Name,Name))return W;throw std::runtime_error(Name);}
int main(){
    auto Scene=std::make_unique<CelestialSequence>();auto Sheet=std::make_unique<EditorSheet>();auto Other=std::make_unique<EditorSheet>();std::vector<uint16_t> Halves;
    const float Camera[]={0,0,2};Scene->Observation.LocalHours=10;Scene->Clock.Animate=false;Scene->Shown[unsigned(CelestialEntity::Sun)]=true;Scene->Shown[unsigned(CelestialEntity::Sky)]=true;Scene->Shown[unsigned(CelestialEntity::Atmosphere)]=true;
    Scene->Budget.AtmosphereSamples=8;Scene->Budget.AtmosphereLightSamples=4;Scene->Tick(0,Camera,0);Scene->BuildSheet(CelestialEntity::Atmosphere,*Sheet);
    Check(Sheet->Appearance==EditorSheetAppearance::AtmosphereSky,"atmosphere typed route");unsigned Count=0;for(auto& G:Sheet->Groups)Count+=G.PropertyCount;Check(Count==18,"12 atmosphere and 6 sky fields retained");
    Scene->BuildSheet(CelestialEntity::Sky,*Other);Check(Other->Appearance==Sheet->Appearance&&Other->GroupCount==Sheet->GroupCount,"both rows share combined inspector");
    Check(!Sheet->SkyImage.Pixels,"missing bake is explicitly empty");
    auto Apply=[&](){Scene->ApplySheet(CelestialEntity::Atmosphere,*Sheet);Scene->Tick(0,Camera,0);Scene->BuildSheet(CelestialEntity::Atmosphere,*Sheet);};
    struct Fixture{const char* Name;float Value;};const Fixture Values[]={{"Rayleigh",2},{"Mie",2},{"Mie Anisotropy",.6f},{"Ozone",2},{"Rayleigh Scale H",9000},{"Mie Scale H",2000},{"Atmosphere",80000},{"Horizon Glow",2},{"White Line",2},{"Sky Brightness",2}};
    for(auto F:Values){float Old=Find(*Sheet,F.Name).Figure;Find(*Sheet,F.Name).Figure=F.Value;Apply();Check(Find(*Sheet,F.Name).Figure==F.Value,"original scalar round trip");Find(*Sheet,F.Name).Figure=Old;Apply();}
    Find(*Sheet,"Rayleigh").Figure=std::numeric_limits<float>::quiet_NaN();Apply();Check(std::isfinite(Scene->Medium.RayleighStrength),"nonfinite medium rejected");
    Find(*Sheet,"Mie Anisotropy").Figure=5;Apply();Check(Scene->Medium.MieAnisotropy==.99f,"anisotropy clamp");Find(*Sheet,"Mie Anisotropy").Figure=.78f;Apply();
    Find(*Sheet,"Sky Brightness").Figure=1.2f;Scene->ApplySheet(CelestialEntity::Sky,*Sheet);Scene->BuildSheet(CelestialEntity::Atmosphere,*Sheet);Check(Find(*Sheet,"Sky Brightness").Figure==1.2f,"sky edits visible from atmosphere row");Find(*Sheet,"Sky Brightness").Figure=1;Apply();
    ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DeltaTime=1.f/60;IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::StyleColorsDark();auto& Style=ImGui::GetStyle();Style.WindowPadding={0,0};Style.WindowRounding=0;Style.WindowBorderSize=0;Style.Colors[ImGuiCol_WindowBg]={.105f,.105f,.105f,1};Style.Colors[ImGuiCol_TitleBgActive]={.075f,.075f,.075f,1};
    auto Controls=std::make_unique<ControlPanel>();auto Inspector=std::make_unique<InspectorPanel>();Inspector->AssignControls(Controls.get());auto Row=std::make_unique<EditorInstance>();std::snprintf(Row->Label,sizeof(Row->Label),"Atmosphere");Row->Category=EditorInstanceCategory::Geometry;
    int Width=1024,Height=2700;float Scale=1;
    auto Tick=[&](){IO.DisplaySize={float(Width),float(Height)};IO.DisplayFramebufferScale={Scale,Scale};ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(IO.DisplaySize);Inspector->Record(Row.get(),0,Sheet.get());Apply();ImGui::Render();FrontierProof::AcknowledgeTextures();};
    auto Rest=[&](){for(int I=0;I<3;++I)Tick();};auto Click=[&](ImVec2 P){IO.AddMousePosEvent(P.x,P.y);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMouseButtonEvent(0,false);Tick();IO.AddMousePosEvent(-100,-100);};
    auto Capture=[&](const char* Name){int PW=int(Width*Scale),PH=int(Height*Scale);std::vector<unsigned char> Pixels(size_t(PW)*PH*3,24);for(auto* L:ImGui::GetDrawData()->CmdLists)FrontierProof::Draw(L,Pixels.data(),PW,PH,{0,0},{Scale,Scale});char Path[200];std::snprintf(Path,sizeof(Path),"Exhibits/Gallery/AtmosphereSky/%s.png",Name);Check(stbi_write_png(Path,PW,PH,3,Pixels.data(),PW*3)!=0,"native capture");};
    Rest();Capture("Sky-Empty");auto* Props=Window("##sky-properties");ImVec2 O=Props->DC.CursorStartPos;O.x+=20;O.y+=20;
    Click({O.x+270,O.y+193});Check(Sheet->SkyImage.Pending,"Bake button queues project action");Check(Scene->TakeSkyDomeBakeRequest(),"renderer consumes explicit bake request");Check(!Scene->TakeSkyDomeBakeRequest(),"request consumed once");
    Scene->AssignSkyDomeSlot(9);Scene->BakeSkyDome(Halves);Check(Scene->SkyDomeSlot==CelestialSequence::kNoSkyDomeSlot,"new CPU bake never falsely keeps old GPU residency");Scene->BuildSheet(CelestialEntity::Atmosphere,*Sheet);Check(Halves.size()==256u*512u*4u,"real baked atlas dimensions");Check(Scene->SkyPreviewHalves==Halves,"preview owns exact baked/upload source bytes");Check(Sheet->SkyImage.Pixels==Scene->SkyPreviewHalves.data()&&!Sheet->SkyImage.Stale,"sheet borrows current baked pixels");
    std::array<uint16_t,32> SampleAtlas{};float TexelValues[]={0,.25f,.5f,1,1,1,1,1};for(int P=0;P<8;++P)for(int C=0;C<4;++C)SampleAtlas[P*4+C]=SkyDomeHalfFromFloat(C==3?1:TexelValues[P]);
    float Zenith[]={0,0,1},East[]={1,0,0};Check(std::abs(SkyPreviewSample(SampleAtlas.data(),2,Zenith)[0]-.4375f)<.000001f,"known bilinear four-texel interpolation");Check(SkyPreviewSample(SampleAtlas.data(),2,Zenith,true)[0]==1,"transmission selects lower atlas half");Check(SkyPreviewSample(SampleAtlas.data(),2,East)[0]==.625f,"oct edge clamps without cross-layer bleed");
    float SunDirection[]={1,0,0};auto ProfileDirection=SkyAtmosphereProfileDirection(.5f,SunDirection);Check(ProfileDirection[0]<0&&ProfileDirection[2]>0,"overview samples an above-horizon altitude opposite the baked Sun");
    auto D=SkyPanoramaDirection(.25f,.25f);float U,V;SkyDomeOctFromDirection(D.data(),U,V);float Back[3];SkyDomeDirectionFromOct(U,V,Back);for(int C=0;C<3;++C)Check(std::abs(D[C]-Back[C])<.000001f,"panorama projection round trip");
    auto RGB=SkyPreviewSample(Halves.data(),256,D.data());auto T=SkyPreviewSample(Halves.data(),256,D.data(),true);Check(RGB[2]>0&&T[0]>=0&&T[0]<=1,"actual radiance and transmission channels decoded");
    for(float F:{0.f,.01f,.5f,1.f,22.f})Check(std::abs(SkyPreviewHalf(SkyDomeHalfFromFloat(F))-F)<.001f,"half decode");
    Check(Scene->SaveSkyDome(".cache/sky-preview.environment",Halves),"existing environment bake persistence");
    std::vector<uint16_t> Reloaded;uint64_t BeforeLoad=Scene->SkyPreviewRevision;
    Scene->AssignSkyDomeSlot(9);Check(Scene->LoadSkyDome(".cache/sky-preview.environment",Reloaded),"load matching persisted bake");
    Check(Scene->SkyDomeSlot==CelestialSequence::kNoSkyDomeSlot,"loaded CPU bake requires fresh upload/assignment");
    Check(Reloaded==Halves&&Scene->SkyPreviewHalves==Halves&&Scene->SkyPreviewRevision==BeforeLoad+1,"loaded preview shares exact restored bake bytes");
    Scene->Medium.RayleighStrength+=1;Check(!Scene->LoadSkyDome(".cache/sky-preview.environment",Reloaded),"reject stale file for different medium");Scene->Medium.RayleighStrength-=1;Scene->BuildSheet(CelestialEntity::Atmosphere,*Sheet);
    Scene->AssignSkyDomeSlot(7);Rest();Click({O.x+100,O.y+193});Check(Scene->QuerySkyDomeBaked(),"Use baked image native switch");Check(Scene->PackSkyRecord().Control[3]==8,"matching resident-slot fixture enables fetch lane");Capture("Sky-Full");
    const uint16_t* Original=Sheet->SkyImage.Pixels;const uint64_t Revision=Sheet->SkyImage.Revision;
    auto* Slider=Window("/Rayleigh_");float Before=Find(*Sheet,"Rayleigh").Figure;Click({Slider->Pos.x+Slider->Size.x*.82f,Slider->Pos.y+15});Check(Find(*Sheet,"Rayleigh").Figure!=Before,"real scattering slider pointer edit");Check(Sheet->SkyImage.Stale&&Scene->PackSkyRecord().Control[3]==0,"medium edit invalidates bake and drops to analytic rendering");Check(Sheet->SkyImage.Pixels==Original&&Sheet->SkyImage.Revision==Revision,"stale preview keeps actual old bake; no fake live redraw");Rest();Capture("Sky-Stale");
    Find(*Sheet,"Rayleigh").Figure=Before;Apply();Check(!Sheet->SkyImage.Stale,"restoring staging restores matching bake");Find(*Sheet,"Horizon Glow").Figure=2;Apply();Check(!Sheet->SkyImage.Stale,"separate analytic twilight does not invalidate smooth dome");
    Scene->Observation.LocalHours=11;Scene->Tick(0,Camera,0);Scene->BuildSheet(CelestialEntity::Atmosphere,*Sheet);Check(Sheet->SkyImage.Stale,"Sun movement invalidates bake");Scene->Observation.LocalHours=10;Scene->Tick(0,Camera,0);Scene->BuildSheet(CelestialEntity::Atmosphere,*Sheet);Rest();
    float W=Props->WorkRect.GetWidth()-40;Click({O.x+40+2*(W-72)/4+50,O.y+361+74});Rest();Capture("Sky-Transmittance");Click({O.x+48+3*(W-72)/4+50,O.y+361+74});Rest();Capture("Sky-FullSphere");Click({O.x+32+(W-72)/4+50,O.y+361+74});Rest();Capture("Sky-Panorama");Click({O.x+50,O.y+361+74});Rest();
    Width=480;Height=3650;Rest();Capture("Sky-Narrow");Height=900;Rest();Props=Window("##sky-properties");Check(Props->ScrollMax.y>1500,"narrow panel scrolls");ImGui::SetScrollY(Props,850);Rest();Capture("Sky-Scrolled");
    Width=320;ImGui::SetScrollY(Props,0);Rest();Check(Window("/Rayleigh_")->Size.x<=Width,"compact native slider fits");Width=1024;Height=900;Scale=2;Rest();Capture("Sky-2x");
    Scene->BuildSheet(CelestialEntity::Stars,*Sheet);Check(Sheet->Appearance==EditorSheetAppearance::Stars&&!Sheet->SkyImage.Pixels,"sheet reuse clears borrowed image");ImGui::DestroyContext();
    std::printf("PASS %u checks: combined atmosphere/sky bindings, real bake panorama, native bake request, staleness, renderer fetch contract, image decode, input, scroll and teardown. CPU proof; no GPU residency claim.\n",Checks);
}
