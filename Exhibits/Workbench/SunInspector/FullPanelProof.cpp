#include "CelestialSequence.h"
#include "InspectorPanel.h"
#include "ControlPanel.h"
#include "SunColourTemperature.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <limits>
using namespace Frontier;
using namespace Frontier::ProjectZero;
static unsigned Checks=0;
static void Check(bool V,const char* Why){++Checks;if(!V)throw std::runtime_error(Why);}
static EditorProperty& Find(EditorSheet& S,const char* Name){for(auto& G:S.Groups)for(unsigned I=0;I<G.PropertyCount;++I)if(std::strcmp(G.Properties[I].Label,Name)==0)return G.Properties[I];throw std::runtime_error(Name);}
static ImGuiWindow* Window(const char* Name){for(auto* W:ImGui::GetCurrentContext()->Windows)if(std::strstr(W->Name,Name))return W;throw std::runtime_error(Name);}
int main(){
    auto Sun=std::make_unique<CelestialSequence>();auto Sheet=std::make_unique<EditorSheet>();auto Other=std::make_unique<EditorSheet>();
    const float Camera[3]={};Sun->Shown[static_cast<unsigned>(CelestialEntity::Sun)]=true;Sun->Observation.LocalHours=14.5f;Sun->Tick(0,Camera,0);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
    Check(Sheet->Appearance==EditorSheetAppearance::Sun,"typed Sun presentation");
    unsigned Total=0;for(auto& G:Sheet->Groups)Total+=G.PropertyCount;Check(Total==18,"15 original properties plus temperature, colour source and day duration");
    for(const char* Label:{"Angular Diameter","Sun Tint","Intensity","Direct","Local Hours","Animate","Speed","Latitude","Longitude","Day of Month","Month","Elevation","Azimuth","Declination","Equation of Time"}) (void)Find(*Sheet,Label);
    Check(!Sun->SunUseTemperature,"RGB compatibility default");
    *Other=*Sheet;Sun->BuildSheet(CelestialEntity::Count,*Other);Check(Other->Appearance==EditorSheetAppearance::Generic,"unknown entity clears to generic");
    auto Saved=std::make_unique<EditorSheet>(*Sheet);
    for(const char* Label:{"Angular Diameter","Intensity","Direct","Local Hours","Latitude","Longitude","Day of Month","Month"}){
        auto& P=Find(*Sheet,Label);float V=P.Minimum+(P.Maximum-P.Minimum)*.5f;if(P.Decimals==0)V=std::floor(V);
        P.Figure=V;Sun->ApplySheet(CelestialEntity::Sun,*Sheet);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);Check(std::abs(Find(*Sheet,Label).Figure-V)<.001f,"native slider round trip");
    }
    Find(*Sheet,"Sun Tint").ColourTint[0]=.35f;Find(*Sheet,"Speed").Picked=3;
    Sun->ApplySheet(CelestialEntity::Sun,*Sheet);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
    Check(std::abs(Find(*Sheet,"Sun Tint").ColourTint[0]-.35f)<.001f,"RGB tint round trip");Check(Find(*Sheet,"Speed").Picked==3,"speed round trip");
    Sun->ApplySheet(CelestialEntity::Sun,*Saved);Sun->Tick(0,Camera,0);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
    auto Apply=[&](){Sun->ApplySheet(CelestialEntity::Sun,*Sheet);Sun->Tick(0,Camera,0);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);};
    // Tabulated Planckian-locus chromaticities: check the conversion before testing its bindings.
    const float CctCases[][3]={{2000,.5269f,.4133f},{3000,.4366f,.4042f},{6500,.3135f,.3237f},{10000,.2807f,.2883f}};
    for(auto& C:CctCases){auto XY=SunColourTemperature::Chromaticity(C[0]);Check(std::abs(XY[0]-C[1])<.002f&&std::abs(XY[1]-C[2])<.002f,"CCT chromaticity reference");}
    for(int K=2000;K<=10000;K+=100){auto RGB=SunColourTemperature::LinearRgb(float(K));for(float C:RGB)Check(std::isfinite(C)&&C>=0&&C<=1,"bounded finite linear RGB");}
    float NativeGain=Sun->Light.Intensity;
    // Remember even HDR manual tint; switching to temperature must not destroy it.
    auto& Manual=Find(*Sheet,"Sun Tint");Manual.ColourTint[0]=1.6f;Manual.ColourTint[1]=.4f;Manual.ColourTint[2]=.2f;Apply();
    Find(*Sheet,"Temperature").Figure=2000;Apply();
    Check(Sun->SunUseTemperature&&Find(*Sheet,"Colour source").Picked==1,"Kelvin edit activates native temperature source");
    Check(Sun->Light.Colour[0]>Sun->Light.Colour[2]&&Sun->Light.Intensity==NativeGain,"warm tint without changing gain");
    Check(Find(*Sheet,"Sun Tint").ColourTint[0]==1.6f,"manual HDR tint remembered");
    const auto Warm=Sun->PackSkyRecord();const float Zenith[]={0,0,1};
    const auto WarmSky=AtmosphereModel::Integrate(Sun->Medium,Sun->Light,2,Zenith,16,8);
    for(int C=0;C<3;++C)Check(std::abs(Warm.SunRadiance[C]-Sun->Light.Colour[C]*Sun->Light.Intensity)<.0001f,"Kelvin reaches packed renderer radiance");
    Find(*Sheet,"Temperature").Figure=10000;Apply();auto Cool=Sun->PackSkyRecord();
    const auto CoolSky=AtmosphereModel::Integrate(Sun->Medium,Sun->Light,2,Zenith,16,8);
    Check(Sun->Light.Colour[2]>Sun->Light.Colour[0],"cool tint");
    Check(Warm.SunRadiance[0]/Warm.SunRadiance[2]>Cool.SunRadiance[0]/Cool.SunRadiance[2],"warm/cool renderer record response");
    Check(WarmSky.Radiance[0]*CoolSky.Radiance[2]>CoolSky.Radiance[0]*WarmSky.Radiance[2],"warm/cool actual CPU atmosphere response");
    Find(*Sheet,"Intensity").Figure=11;Apply();auto Low=Sun->PackSkyRecord();
    Find(*Sheet,"Intensity").Figure=22;Apply();auto High=Sun->PackSkyRecord();
    for(int C=0;C<3;++C){Check(std::abs(High.SunRadiance[C]-2*Low.SunRadiance[C])<.0001f,"gain reaches sky radiance");Check(std::abs(High.SunDirect[C]-2*Low.SunDirect[C])<.0001f,"gain reaches direct sunlight");}
    Find(*Sheet,"Intensity").Figure=0;Apply();auto Dark=Sun->PackSkyRecord();for(int C=0;C<3;++C)Check(Dark.SunRadiance[C]==0&&Dark.SunDirect[C]==0,"zero gain is dark");
    Find(*Sheet,"Intensity").Figure=NativeGain;Find(*Sheet,"Colour source").Picked=0;Apply();
    Check(!Sun->SunUseTemperature&&Sun->Light.Colour[0]==1.6f&&Sun->Light.Colour[1]==.4f,"RGB selector restores manual tint exactly");
    Find(*Sheet,"Colour source").Picked=1;Apply();Check(Sun->SunUseTemperature,"mode selector activates stored Kelvin");
    Find(*Sheet,"Sun Tint").ColourTint[0]=.31f;Apply();Check(!Sun->SunUseTemperature&&Sun->Light.Colour[0]==.31f,"manual RGB edit exits temperature mode");
    Find(*Sheet,"Temperature").Figure=1000;Apply();Check(Sun->SunTemperatureKelvin==2000,"lower Kelvin clamp");
    Find(*Sheet,"Temperature").Figure=20000;Apply();Check(Sun->SunTemperatureKelvin==10000,"upper Kelvin clamp");
    Find(*Sheet,"Temperature").Figure=std::numeric_limits<float>::quiet_NaN();Apply();Check(Sun->SunTemperatureKelvin==10000,"NaN Kelvin retains last valid value");
    Find(*Sheet,"Temperature").Figure=std::numeric_limits<float>::infinity();Apply();Check(Sun->SunTemperatureKelvin==10000,"infinite Kelvin retains last valid value");
    auto Stable=Sun->PackSkyRecord();for(int I=0;I<10;++I)Apply();auto Repeated=Sun->PackSkyRecord();
    Check(std::memcmp(&Stable,&Repeated,sizeof(Stable))==0,"unchanged property sheets do not drift colour or radiance");
    Sun->ApplySheet(CelestialEntity::Sun,*Saved);Sun->Tick(0,Camera,0);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
    Find(*Sheet,"Day duration").Figure=6;Find(*Sheet,"Animate").On=true;Apply();
    Check(std::abs(Sun->Clock.SpeedTimes-4)<.0001f,"six real hour cycle uses four-times solar clock");
    const float StartTime=Sun->Observation.LocalHours;
    Sun->Tick(3600,Camera,0);Check(std::abs(Sun->Observation.LocalHours-std::fmod(StartTime+4,24.f))<.001f,"dynamic advances four solar hours in one real hour");
    Sun->BuildSheet(CelestialEntity::Sun,*Sheet);Apply();Check(Sun->Clock.SpeedTimes==4,"custom duration survives repeated sheet application");
    Find(*Sheet,"Animate").On=false;Apply();float Frozen=Sun->Observation.LocalHours;auto FrozenDirection=Sun->PackSkyRecord();
    Sun->Tick(3600,Camera,0);Check(Sun->Observation.LocalHours==Frozen,"static holds time");
    auto HeldDirection=Sun->PackSkyRecord();Check(std::memcmp(&FrozenDirection,&HeldDirection,sizeof(HeldDirection))==0,"static holds renderer sky record");
    Find(*Sheet,"Local Hours").Figure=23;Find(*Sheet,"Animate").On=true;Apply();Sun->Tick(3600,Camera,0);Check(std::abs(Sun->Observation.LocalHours-3)<.001f,"dynamic wraps midnight");
    Sun->BuildSheet(CelestialEntity::Sun,*Sheet);Find(*Sheet,"Day duration").Figure=std::numeric_limits<float>::quiet_NaN();Apply();Check(Sun->Clock.SpeedTimes==4,"nonfinite duration ignored");
    Find(*Sheet,"Day duration").Figure=0;Apply();Check(std::abs(24/Sun->Clock.SpeedTimes-.01f)<.00001f,"minimum duration clamped");
    Find(*Sheet,"Day duration").Figure=1000;Apply();Check(std::abs(24/Sun->Clock.SpeedTimes-168)<.001f,"maximum duration clamped");
    Find(*Sheet,"Speed").Picked=2;Apply();Check(Sun->Clock.SpeedTimes==30&&std::abs(Find(*Sheet,"Day duration").Figure-.8f)<.0001f,"speed preset updates duration");
    Sun->ApplySheet(CelestialEntity::Sun,*Saved);Sun->Tick(0,Camera,0);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
    ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DeltaTime=1.f/60;
    IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::StyleColorsDark();auto& Style=ImGui::GetStyle();Style.WindowPadding={0,0};Style.WindowRounding=0;Style.WindowBorderSize=0;
    Style.Colors[ImGuiCol_WindowBg]={.105f,.105f,.105f,1};Style.Colors[ImGuiCol_TitleBgActive]={.075f,.075f,.075f,1};Style.Colors[ImGuiCol_TitleBg]={.075f,.075f,.075f,1};
    auto Controls=std::make_unique<ControlPanel>();auto Inspector=std::make_unique<InspectorPanel>();Inspector->AssignControls(Controls.get());
    auto Row=std::make_unique<EditorInstance>();std::snprintf(Row->Label,sizeof(Row->Label),"Sun");Row->Category=EditorInstanceCategory::Light;
    int Width=1024,Height=2100;float Scale=1;
    auto Tick=[&](){
        IO.DisplaySize={float(Width),float(Height)};IO.DisplayFramebufferScale={Scale,Scale};
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(IO.DisplaySize);Inspector->Record(Row.get(),0,Sheet.get());
        Sun->ApplySheet(CelestialEntity::Sun,*Sheet);Sun->Tick(0,Camera,0);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
        ImGui::Render();FrontierProof::AcknowledgeTextures();
    };
    auto Rest=[&](){for(int I=0;I<4;++I)Tick();};
    auto Click=[&](ImVec2 P){IO.AddMousePosEvent(P.x,P.y);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMouseButtonEvent(0,false);Tick();IO.AddMousePosEvent(-100,-100);};
    auto SliderClick=[&](const char* Name,const char* Label){auto* W=Window(Name);float Old=Find(*Sheet,Label).Figure;Click({W->Pos.x+W->Size.x*.82f,W->Pos.y+15});Check(std::abs(Find(*Sheet,Label).Figure-Old)>.05f,"real native SliderPill pointer write-back");};
    Rest();SliderClick("##sun-diameter","Angular Diameter");SliderClick("##sun-time","Local Hours");
    Find(*Sheet,"Local Hours").Figure=2;Apply();Rest();float PreviousAz=Find(*Sheet,"Azimuth").Figure;
    SliderClick("##sun-direction-time","Local Hours");Check(std::abs(Find(*Sheet,"Azimuth").Figure-PreviousAz)>.01f,"direction time control updates shared ephemeris");
    Check(Find(*Sheet,"Local Hours").Figure==Sun->Observation.LocalHours,"direction and daylight share one clock");
    SliderClick("##sun-intensity","Intensity");Check(Sun->Light.Intensity==Find(*Sheet,"Intensity").Figure,"intensity pointer edit reaches renderer light");
    SliderClick("##sun-temperature","Temperature");Check(Sun->SunUseTemperature,"temperature pointer edit changes native colour source");
    // Read-only direction sliders must not alter solver inputs or the solved output.
    float Az=Find(*Sheet,"Azimuth").Figure;auto* AzW=Window("##sun-azimuth");Click({AzW->Pos.x+AzW->Size.x*.8f,AzW->Pos.y+15});
    Check(std::abs(Find(*Sheet,"Azimuth").Figure-Az)<.001f,"direction cannot be manually edited");
    // Coordinates follow the native panel's content origin, not a captured image overlay.
    auto* Props=Window("##sun-properties");ImVec2 O=Props->DC.CursorStartPos;float W=Props->WorkRect.GetWidth()-40;O.x+=20;O.y+=20;
    float Tile=std::min(160.f,(W-27)/4);bool Old=Find(*Sheet,"Animate").On;
    Click({O.x+2*(Tile+9)+Tile/2,O.y+436+53});Check(Find(*Sheet,"Animate").On!=Old,"day-cycle quick control writes Clock.Animate");
    Old=Find(*Sheet,"Animate").On;Click({O.x+3*(Tile+9)+Tile/2,O.y+436+53});Check(Find(*Sheet,"Animate").On!=Old,"dynamic static tile controls shared clock");
    SliderClick("##sun-day-duration","Day duration");Check(std::abs(24/Sun->Clock.SpeedTimes-Find(*Sheet,"Day duration").Figure)<.001f,"duration pointer input reaches clock rate");
    bool Visible=Row->Visible;Click({O.x+W-45,O.y+78});Check(Row->Visible!=Visible,"enabled header controls actual row visibility");Row->Visible=true;
    // Disabled bake/load and unbound sunlight/disc quick controls do not invent project state.
    float Before=Sun->Light.Intensity;Click({O.x+230,O.y+228});Click({O.x+Tile/2,O.y+489});Check(Sun->Light.Intensity==Before,"unbound controls do not change gain");
    Sun->ApplySheet(CelestialEntity::Sun,*Saved);Sun->Clock.Animate=true;Sun->Tick(0,Camera,0);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);Find(*Sheet,"Temperature").Figure=6000;Apply();Rest();
    auto Capture=[&](const char* Name){
        int PW=int(Width*Scale),PH=int(Height*Scale);std::vector<unsigned char> Pixels(size_t(PW)*PH*3,24);
        for(auto* L:ImGui::GetDrawData()->CmdLists)FrontierProof::Draw(L,Pixels.data(),PW,PH,{0,0},{Scale,Scale});
        char Path[180];std::snprintf(Path,sizeof(Path),"Exhibits/Gallery/SunFullPanel/%s.png",Name);Check(stbi_write_png(Path,PW,PH,3,Pixels.data(),PW*3)!=0,"native full-panel PNG");
    };
    Find(*Sheet,"Day duration").Figure=6;Apply();Rest();Capture("Sun-Full");
    Find(*Sheet,"Animate").On=false;Apply();Rest();Capture("Sun-Static");
    Find(*Sheet,"Animate").On=true;Apply();Rest();
    Find(*Sheet,"Temperature").Figure=2500;Apply();Rest();Capture("Sun-Warm");
    Find(*Sheet,"Temperature").Figure=10000;Apply();Rest();Capture("Sun-Cool");
    Find(*Sheet,"Temperature").Figure=6000;Apply();Rest();
    // Verify the real collapsing native-properties section; its open state is ImGui-owned.
    Props=Window("##sun-properties");O=Props->DC.CursorStartPos;O.x+=20;O.y+=20;
    Click({O.x+160,O.y+626+594+16+329+24+228+34});
    Height=2450;Rest();Check(Window("##sun-native-extra")->Size.y>300,"rounded supplement expanded");Capture("Sun-NativeProperties");
    Width=480;Height=3350;Rest();Capture("Sun-Narrow");
    // On a normal-height panel, the very same content must remain scrollable.
    Height=900;Rest();Props=Window("##sun-properties");Check(Props->ScrollMax.y>1500,"narrow panel scroll extent");ImGui::SetScrollY(Props,650);Rest();Check(Props->Scroll.y>600,"native scrolling");Capture("Sun-Scrolled");
    Width=320;Height=900;ImGui::SetScrollY(Props,0);Rest();
    Check(Window("##sun-azimuth")->Size.x>=160,"compact direction controls stack instead of crushing native slider");
    Check(Window("##sun-time")->Size.x<=Width,"compact native time slider fits panel");
    // DPI smoke capture checks native text/texture sampling without changing logical dimensions.
    Width=1024;Height=900;Scale=2;ImGui::SetScrollY(Props,0);Rest();Capture("Sun-2x-Top");
    ImGui::DestroyContext();std::printf("PASS %u checks: full InspectorPanel, two bake targets, five reference cards, bound temperature and intensity, renderer-record and CPU-atmosphere response, native sliders and quick-cycle write-back, read-only direction, visibility, property round trips, supplement, scroll, narrow and 2x captures.\n",Checks);
}
