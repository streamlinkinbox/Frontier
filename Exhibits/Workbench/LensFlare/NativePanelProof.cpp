#include "CelestialSequence.h"
#include "InspectorPanel.h"
#include "ControlPanel.h"
#include "LensFlareInspectorPanel.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <limits>
using namespace Frontier;
using namespace Frontier::ProjectZero;
static unsigned Checks=0;
static void Check(bool V,const char* Why){++Checks;if(!V)throw std::runtime_error(Why);}
static EditorProperty& Find(EditorSheet& S,const char* Label){for(auto& G:S.Groups)for(unsigned I=0;I<G.PropertyCount;++I)if(std::strcmp(G.Properties[I].Label,Label)==0)return G.Properties[I];throw std::runtime_error(Label);}
static ImGuiWindow* Window(const char* Name){for(auto* W:ImGui::GetCurrentContext()->Windows)if(std::strstr(W->Name,Name))return W;throw std::runtime_error(Name);}
static double Difference(const AtmosphericOptics::LensFlareSettings& A,const AtmosphericOptics::LensFlareSettings& B){double Sum=0;const float Sun[]={.28f,.43f};for(int Y=0;Y<56;++Y)for(int X=0;X<128;++X){const float UV[]={(X+.5f)/128,(Y+.5f)/56};float L[3],R[3];AtmosphericOptics::LensFlare(A,UV,Sun,1,128.f/56,L);AtmosphericOptics::LensFlare(B,UV,Sun,1,128.f/56,R);for(int C=0;C<3;++C){if(!std::isfinite(L[C])||!std::isfinite(R[C]))throw std::runtime_error("nonfinite pixels");Sum+=std::abs(L[C]-R[C]);}}return Sum;}
int main(){
    auto Scene=std::make_unique<CelestialSequence>();auto Sheet=std::make_unique<EditorSheet>();const float Camera[]={0,0,2};
    Scene->Shown[unsigned(CelestialEntity::Sun)]=true;Scene->Shown[unsigned(CelestialEntity::LensFlare)]=true;
    Scene->Observation.LocalHours=12;Scene->Tick(0,Camera,0);Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);
    Check(Sheet->Appearance==EditorSheetAppearance::LensFlare,"typed native route");unsigned Count=0;for(auto& G:Sheet->Groups)Count+=G.PropertyCount;Check(Count==21,"all 21 native fields provided");
    auto Apply=[&](){Scene->ApplySheet(CelestialEntity::LensFlare,*Sheet);Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);};
    Check(!Scene->Flare.CustomMix,"original preset appearance retained by default");
    Find(*Sheet,"Type").Picked=4;Apply();Check(Scene->Flare.CustomMix,"custom mix selectable");
    auto Defaults=Scene->Flare;
    struct Fixture{const char* Label;float Value;};
    const Fixture Fields[]={{"Intensity",2},{"Ghosts",18},{"Halo Radius",.6f},{"Chromatic",.1f},{"Streak gain",2},{"Spread",1.5f},{"Rotation",40},{"Ray pairs",5},{"Ghost brightness",1.2f},{"Ghost spacing",1.8f},{"Halo brightness",2},{"Halo width",.1f}};
    for(auto F:Fields){Scene->Flare=Defaults;Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);Find(*Sheet,F.Label).Figure=F.Value;Apply();Check(Difference(Defaults,Scene->Flare)>.001,F.Label);}
    for(const char* Label:{"Anamorphic","Streaks","Starburst"}){Scene->Flare=Defaults;Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);Find(*Sheet,Label).On=!Find(*Sheet,Label).On;Apply();Check(Difference(Defaults,Scene->Flare)>.001,"independent layer changes rendered pixels");}
    for(unsigned Shape:{0u,2u}){Scene->Flare=Defaults;Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);Find(*Sheet,"Ghost shape").Picked=Shape;Apply();Check(Difference(Defaults,Scene->Flare)>.001,"ghost geometry changes pixels");}
    Scene->Flare=Defaults;Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);
    Find(*Sheet,"Halo width").Figure=std::numeric_limits<float>::quiet_NaN();Find(*Sheet,"Ghosts").Figure=-100;Apply();Check(Scene->Flare.Layers.HaloWidth==Defaults.Layers.HaloWidth&&Scene->Flare.GhostCount==0,"nonfinite and negative inputs validated");
    Find(*Sheet,"Ghosts").Figure=999;Find(*Sheet,"Ray pairs").Figure=7.4f;Apply();Check(Scene->Flare.GhostCount==24&&Scene->Flare.Layers.RayPairs==7,"bounded counts and integer rays");
    Scene->Flare=Defaults;Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);
    float AtSource[3];const float Source[]={.28f,.43f};AtmosphericOptics::LensFlare(Scene->Flare,Source,Source,1,640.f/280,AtSource);
    Check(std::isfinite(AtSource[0])&&std::isfinite(AtSource[1])&&std::isfinite(AtSource[2]),"source-centre angular guard");
    const float Forward[]={0,0,1},Right[]={1,0,0},Up[]={0,1,0};
    auto Packed=Scene->PackPostRecord(Forward,Right,Up,1,640.f/280,280,1);
    Check(sizeof(Packed)==544&&offsetof(PostConstantRecord,PostLayers)==128,"extended ABI preserves original offsets");
    Check(Packed.PostFlareUv[3]==1&&Packed.PostLayers[2]==1&&Packed.PostLayers[3]==0&&Packed.PostLayers[4]==1,"layer flags reach actual renderer record");
    Check(Packed.PostLayers[10]==6&&Packed.PostLayers[13]==Defaults.Layers.HaloWidth,"shape and halo width reach actual renderer record");
    Find(*Sheet,"Type").Picked=1;Apply();Check(!Scene->Flare.CustomMix,"legacy preset restores old path");Find(*Sheet,"Halo width").Figure=.08f;Apply();Check(Scene->Flare.CustomMix,"algorithm-specific edit activates custom layers");
    Scene->Flare=Defaults;Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);
    // Check periodicity for all selectable integer ray-pair counts with every other layer disabled.
    auto P=LFDefaults();P.Anamorphic=P.Streaks=P.Ghosts=P.HaloGain=0;
    for(int N=2;N<=12;++N){P.RayPairs=float(N);float A[3],B[3];LFRender(P,.1f,.500001f,.5f,.5f,1,1,A[0],A[1],A[2]);LFRender(P,.1f,.499999f,.5f,.5f,1,1,B[0],B[1],B[2]);Check(std::abs(A[0]-B[0])<.0001f,"angular seam continuity");}
    ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;IO.DeltaTime=1.f/60;IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::StyleColorsDark();auto& Style=ImGui::GetStyle();Style.WindowPadding={0,0};Style.WindowRounding=0;Style.WindowBorderSize=0;Style.Colors[ImGuiCol_WindowBg]={.105f,.105f,.105f,1};Style.Colors[ImGuiCol_TitleBgActive]={.075f,.075f,.075f,1};Style.Colors[ImGuiCol_TitleBg]={.075f,.075f,.075f,1};
    auto Controls=std::make_unique<ControlPanel>();auto Inspector=std::make_unique<InspectorPanel>();Inspector->AssignControls(Controls.get());auto Row=std::make_unique<EditorInstance>();std::snprintf(Row->Label,sizeof(Row->Label),"Lens Flare");Row->Category=EditorInstanceCategory::Light;
    int Width=1024,Height=2400;float Scale=1;
    auto Tick=[&](){IO.DisplaySize={float(Width),float(Height)};IO.DisplayFramebufferScale={Scale,Scale};ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(IO.DisplaySize);Inspector->Record(Row.get(),0,Sheet.get());Apply();ImGui::Render();FrontierProof::AcknowledgeTextures();};
    auto Rest=[&](){for(int I=0;I<3;++I)Tick();};
    auto Click=[&](ImVec2 P){IO.AddMousePosEvent(P.x,P.y);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMouseButtonEvent(0,false);Tick();IO.AddMousePosEvent(-100,-100);};
    Rest();auto Info=QueryLensFlarePreview();Check(Info.Texture&&Info.Revision==1,"one managed image generated initially");Rest();Check(QueryLensFlarePreview().Revision==Info.Revision,"unchanged frames reuse cached image");
    unsigned Images=0;for(auto* L:ImGui::GetDrawData()->CmdLists)for(auto& C:L->CmdBuffer)if(C.TexRef._TexData==Info.Texture)++Images;Check(Images>=2,"thumbnail and composite reference same image resource");
    for(const char* Label:{"Intensity","Halo Radius","Halo width","Ghosts","Rotation"}){char Needle[64];std::snprintf(Needle,sizeof(Needle),"/%s_",Label);auto* W=Window(Needle);float Old=Find(*Sheet,Label).Figure;Click({W->Pos.x+W->Size.x*.82f,W->Pos.y+15});Check(Find(*Sheet,Label).Figure!=Old,"native slider pointer write-back");}
    Check(QueryLensFlarePreview().Revision>Info.Revision,"sliders invalidate shared preview image");
    Scene->Flare=Defaults;Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);Rest();
    auto* Props=Window("##flare-properties");ImVec2 O=Props->DC.CursorStartPos;O.x+=20;O.y+=20;float W=Props->WorkRect.GetWidth()-40;
    float CH=141+(W-48)*280/640+14+44+72+50,LY=299+CH+16;
    Find(*Sheet,"Type").Picked=0;Apply();Rest();Click({O.x+24+((W-16)/2-66)/6,O.y+LY+203});Check(Scene->Flare.CustomMix&&Find(*Sheet,"Anamorphic").On,"first layer click enables visible layer from a legacy preset");
    bool Old=Find(*Sheet,"Streaks").On;Click({O.x+24+(((W-16)/2-66)/3+9)+((W-16)/2-66)/6,O.y+LY+203});Check(Find(*Sheet,"Streaks").On!=Old,"layer toggle pointer write-back");
    Click({O.x+24+(W-48)*.7f,O.y+299+141+((W-48)*280/640)*.3f});Check(std::abs(Scene->FlarePreviewX-.7f)<.01f&&std::abs(Scene->FlarePreviewY-.3f)<.01f,"drag source updates preview pose");
    float BeforeArrow=Scene->FlarePreviewX;IO.AddKeyEvent(ImGuiKey_RightArrow,true);Tick();IO.AddKeyEvent(ImGuiKey_RightArrow,false);Tick();Check(Scene->FlarePreviewX>BeforeArrow,"focused preview responds to arrow key");
    Check(Scene->Observation.LocalHours==12,"preview pose does not change scene clock");
    float Col=(W-16)/2,GX=Col+16;float GhostBefore=Find(*Sheet,"Ghosts").Figure;
    Click({O.x+GX+Col-41,O.y+LY+81});Check(Find(*Sheet,"Ghosts").Figure==GhostBefore+1,"ghost plus pointer input");
    float BW=(Col-64)/3;Click({O.x+GX+24+BW/2,O.y+LY+183});Check(Find(*Sheet,"Ghost shape").Picked==0,"round shape pointer input");
    Rest();Check(ExportLensFlarePreview(".cache/lens-flare-export.pfm"),"HDR bake export succeeds");Info=QueryLensFlarePreview();Check(Info.BakedRevision==Info.Revision,"export records exact resource revision");
    std::ifstream File(".cache/lens-flare-export.pfm",std::ios::binary);std::string Line;std::getline(File,Line);Check(Line=="PF","linear RGB PFM export");std::getline(File,Line);Check(Line=="640 280","bake dimensions match preview");std::getline(File,Line);
    std::vector<float> Image(640*280*3);File.read(reinterpret_cast<char*>(Image.data()),Image.size()*sizeof(float));Check(File.good(),"complete HDR pixel payload");
    auto F=Scene->Flare;const float UV[]={320.5f/640,140.5f/280},Sun[]={Scene->FlarePreviewX,Scene->FlarePreviewY};float RGB[3];AtmosphericOptics::LensFlare(F,UV,Sun,1,640.f/280,RGB);
    for(int C=0;C<3;++C)Check(std::abs(Image[((279-140)*640+320)*3+C]-RGB[C])<.000001f,"export pixels are actual algorithm output");
    Find(*Sheet,"Halo width").Figure=.1f;Apply();Rest();Check(QueryLensFlarePreview().BakedRevision!=QueryLensFlarePreview().Revision,"edits mark exported bake stale");
    auto Capture=[&](const char* Name){int PW=int(Width*Scale),PH=int(Height*Scale);std::vector<unsigned char> Pixels(size_t(PW)*PH*3,24);for(auto* L:ImGui::GetDrawData()->CmdLists)FrontierProof::Draw(L,Pixels.data(),PW,PH,{0,0},{Scale,Scale});char Path[200];std::snprintf(Path,sizeof(Path),"Exhibits/Gallery/LensFlareNative/%s.png",Name);Check(stbi_write_png(Path,PW,PH,3,Pixels.data(),PW*3)!=0,"native capture");};
    Scene->Flare=Defaults;Scene->FlarePreviewX=.28f;Scene->FlarePreviewY=.43f;Scene->BuildSheet(CelestialEntity::LensFlare,*Sheet);Rest();Capture("Lens-Flare-Full");
    Find(*Sheet,"Streaks").On=true;Find(*Sheet,"Rotation").Figure=35;Find(*Sheet,"Ghost shape").Picked=2;Find(*Sheet,"Halo brightness").Figure=2;Apply();Rest();Capture("Lens-Flare-Mixed");
    Width=480;Height=3600;Rest();Capture("Lens-Flare-Narrow");Height=900;Rest();Props=Window("##flare-properties");Check(Props->ScrollMax.y>1500,"narrow layout scrolls");ImGui::SetScrollY(Props,1400);Rest();Capture("Lens-Flare-Scrolled");
    Width=320;Height=900;ImGui::SetScrollY(Props,0);Rest();Check(Window("/Intensity_")->Size.x<=320,"compact native controls fit");
    Width=1024;Height=950;Scale=2;ImGui::SetScrollY(Props,0);Rest();Capture("Lens-Flare-2x");
    Scene->BuildSheet(CelestialEntity::Stars,*Sheet);Check(Sheet->Appearance==EditorSheetAppearance::Stars,"sheet route resets");
    ImGui::DestroyContext();Check(QueryLensFlarePreview().Texture==nullptr,"preview context resource teardown");
    std::printf("PASS %u checks: native Lens Flare inspector, custom layer pixels, shared image, slider input, export, cache invalidation, renderer record, narrow scrolling and teardown.\n",Checks);
}
