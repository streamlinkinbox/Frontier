#include "CelestialSequence.h"
#include "InspectorPanel.h"
#include "ControlPanel.h"
#include "MoonReferenceDraw.h"
#include "MoonAtlasPreview.h"
#include "ContentInterchange/TextureIndex.h"
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
static ImGuiWindow* Window(const char* Name){for(auto* W:ImGui::GetCurrentContext()->Windows)if(W->Active&&std::strstr(W->Name,Name))return W;throw std::runtime_error(Name);}
int main(){
 auto Scene=std::make_unique<CelestialSequence>();auto Sheet=std::make_unique<EditorSheet>();const float Camera[]={0,0,2};Scene->Clock.Animate=false;Scene->Shown[unsigned(CelestialEntity::Moons)]=true;Scene->Tick(0,Camera,0);Scene->BuildSheet(CelestialEntity::Moons,*Sheet);
 Check(Sheet->Appearance==EditorSheetAppearance::Moon,"typed Moon route");unsigned Count=0;for(auto& G:Sheet->Groups)Count+=G.PropertyCount;Check(Count==47&&Sheet->GroupCount==6,"39 original fields plus eight orientation fields fit existing sheet");
 auto Apply=[&](){Scene->ApplySheet(CelestialEntity::Moons,*Sheet);Scene->Tick(0,Camera,0);Scene->BuildSheet(CelestialEntity::Moons,*Sheet);};
 Check(Scene->PackMoonRecord().Control[0]==0,"unassigned atlas never samples slot zero");TextureIndex EmptyAtlas;uint32_t Slots[6]={0,1,2,3,4,5};Scene->AssignMoonAtlas(Slots,EmptyAtlas); // record contract fixture, no GPU upload
 for(int I=0;I<4;++I){char Label[32];auto Field=[&](const char* N)->EditorProperty&{std::snprintf(Label,sizeof(Label),"M%d %s",I+1,N);return Find(*Sheet,Label);};Field("Visible").On=true;Field("Follow Sky").On=false;Apply();
 struct F{const char* Name;float Value;};for(auto V:{F{"Azimuth",80.f+I*10},F{"Elevation",28},F{"Size",1000.f+I},F{"Bright",1.5f},F{"Glow",.7f},F{"Phase",.3f},F{"Roll",35.f+I},F{"Pitch",42.f+I}}){Field(V.Name).Figure=V.Value;Apply();Check(Field(V.Name).Figure==V.Value,"per-slot scalar round trip");}
 auto R=Scene->PackMoonRecord();Check(std::abs(R.Direction[I][3]-(35+I)*SunReference::Pi/180)<.000001f,"roll reaches reserved uniform lane");Check(std::abs(R.Surface[I][1]-(kMoonAtlas[Scene->MoonSlots[I].Preset].TiltDegrees-42-I)*SunReference::Pi/180)<.000001f,"pitch reaches renderer tilt");Check(R.Params[I][0]>8,"size above 40 and 360 remains rendered, not clamped");
 Field("Size").Figure=std::numeric_limits<float>::infinity();Apply();Check(Field("Size").Figure==1000+I,"nonfinite size rejected");Field("Size").Figure=.52f;Apply();}
 Check(Scene->PackMoonRecord().Control[0]==4,"all four slots retained");Find(*Sheet,"M1 Visible").On=false;Apply();Check(Scene->PackMoonRecord().Control[0]==3,"slot visibility gates draw count");Find(*Sheet,"M1 Visible").On=true;Find(*Sheet,"M1 Follow Sky").On=true;Apply();auto Auto=Scene->PackMoonRecord();Check(std::abs(Auto.Params[0][2]-MoonPhaseToReference(Scene->Frame().MoonPhase))<.000001f,"follow sky uses solver phase");for(int I=0;I<3;++I)Check(Auto.Direction[0][I]==Scene->Frame().Moon.Direction[I],"follow sky uses solver direction");Find(*Sheet,"M1 Follow Sky").On=false;Apply();
 Find(*Sheet,"M1 Preset").Picked=1;Find(*Sheet,"M1 Size").Figure=999;Apply();Check(Scene->MoonSlots[0].Size==kMoonAtlas[1].SizeDegrees,"preset reset survives stale size field");Find(*Sheet,"M1 Preset").Picked=0;Apply();
 // Actual CPU evaluator: asymmetric synthetic albedo makes both orientation axes observable.
 std::array<uint8_t,8*4*4> Texels{};for(int Y=0;Y<4;++Y)for(int X=0;X<8;++X){auto* P=Texels.data()+(Y*8+X)*4;P[0]=30+X*24;P[1]=20+Y*65;P[2]=90;P[3]=255;}
 MoonDrawEntry E;E.AngularRadius=.5f;E.Brightness=1;E.Phase=.25f;E.Glow=0;E.Albedo={Texels.data(),8,4,4};float Ray[]={.2f,.1f,std::sqrt(.95f)},Transmission[]={1,1,1},Before[3],After[3];EvaluateMoons(&E,1,Ray,Transmission,Before);E.Roll=1;EvaluateMoons(&E,1,Ray,Transmission,After);Check(std::abs(Before[0]-After[0])+std::abs(Before[1]-After[1])>1e-6f,"roll changes real CPU moon pixels");E.Roll=0;E.Tilt=1;EvaluateMoons(&E,1,Ray,Transmission,After);Check(std::abs(Before[0]-After[0])+std::abs(Before[1]-After[1])>1e-6f,"pitch changes real CPU albedo pixels");
 for(float Size:{.1f,40.f,179.99f,180.f,181.f,360.f,1000.f,std::numeric_limits<float>::max()}){E.AngularRadius=Size*(SunReference::Pi/360);for(auto R:std::array<std::array<float,3>,3>{{{0,0,1},{1,0,0},{0,0,-1}}}){EvaluateMoons(&E,1,R.data(),Transmission,After);Check(std::isfinite(After[0])&&std::isfinite(After[1])&&std::isfinite(After[2]),"oversized angular map finite at poles and antipode");}}
 MoonDrawEntry RollTest;RollTest.AngularRadius=.5f;RollTest.Phase=.25f;RollTest.Glow=0;RollTest.Roll=SunReference::Pi*.5f;float Up[]={0,std::sin(.2f),std::cos(.2f)},Down[]={0,-std::sin(.2f),std::cos(.2f)};EvaluateMoons(&RollTest,1,Up,Transmission,Before);EvaluateMoons(&RollTest,1,Down,Transmission,After);Check(After[0]>Before[0],"positive roll turns right-lit hemisphere clockwise toward bottom");
 E.AngularRadius=SunReference::Pi*.5f;EvaluateMoons(&E,1,Ray,Transmission,Before);E.AngularRadius+=.00001f;EvaluateMoons(&E,1,Ray,Transmission,After);Check(std::abs(Before[0]-After[0])+std::abs(Before[1]-After[1])<.0001f,"wide-disc mapping continuous at 180-degree diameter");
 Find(*Sheet,"M1 Roll").Figure=0;Find(*Sheet,"M1 Pitch").Figure=0;Apply();
    for(int I=1;I<4;++I){char Label[24];std::snprintf(Label,sizeof(Label),"M%d Preset",I+1);Find(*Sheet,Label).Picked=I;}Apply();
    TextureIndex Atlas;for(unsigned I=0;I<6;++I){std::string Path=".cache/cpp-sun-full/EngineContent/CelestialTextures/";Path+=kMoonAtlas[I].File;Slots[I]=Atlas.RegisterPath(Path,false);}std::vector<std::string> DecodeReport;Check(Atlas.Decode(0,&DecodeReport)==0,"all six actual body atlas files decode");Scene->AssignMoonAtlas(Slots,Atlas);Scene->BuildSheet(CelestialEntity::Moons,*Sheet);for(auto& Body:Sheet->MoonBodies)Check(Body.Pixels&&Body.Width>1&&Body.Height>1,"project lends real registered albedo pixels");
    std::vector<unsigned char> Preview(96*96*4);std::array<uint64_t,6> BodyHashes{};
    for(unsigned I=0;I<6;++I){auto& B=Sheet->MoonBodies[I];Check(B.Pixels==Atlas.QueryTextures()[Slots[I]].Texels.data(),"inspector borrows exactly the scene atlas level-zero bytes");MoonAtlasPreview(Preview.data(),96,{B.Pixels,B.Width,B.Height,4},B.Tint,B.Tilt,B.Haze,B.Gamma,.5f,0,0,1.6f);uint64_t Hash=1469598103934665603ull;for(auto V:Preview)Hash=(Hash^V)*1099511628211ull;BodyHashes[I]=Hash;for(unsigned J=0;J<I;++J)Check(BodyHashes[J]!=Hash,"catalogue bodies have distinct real textured previews");}
    float White[]={1,1,1};MoonAtlasPreview(Preview.data(),96,{},White,0,0,1,.5f,0,0,1.6f);Check(std::all_of(Preview.begin(),Preview.end(),[](auto V){return V==0;}),"missing atlas gives no fabricated moon pixels");
    ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DeltaTime=1.f/60;IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::StyleColorsDark();auto& Style=ImGui::GetStyle();Style.WindowPadding={0,0};Style.WindowRounding=0;Style.WindowBorderSize=0;Style.Colors[ImGuiCol_WindowBg]={.105f,.105f,.105f,1};Style.Colors[ImGuiCol_TitleBgActive]={.075f,.075f,.075f,1};
    auto Controls=std::make_unique<ControlPanel>();auto Inspector=std::make_unique<InspectorPanel>();Inspector->AssignControls(Controls.get());auto Row=std::make_unique<EditorInstance>();std::snprintf(Row->Label,sizeof(Row->Label),"Moon");Row->Category=EditorInstanceCategory::Geometry;
    int Width=1024,Height=2250;float Scale=1;
    auto Tick=[&](){IO.DisplaySize={float(Width),float(Height)};IO.DisplayFramebufferScale={Scale,Scale};ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(IO.DisplaySize);Inspector->Record(Row.get(),0,Sheet.get());Apply();ImGui::Render();FrontierProof::AcknowledgeTextures();};
    auto Rest=[&](){for(int I=0;I<3;++I)Tick();};auto Click=[&](ImVec2 P){IO.AddMousePosEvent(P.x,P.y);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMouseButtonEvent(0,false);Tick();IO.AddMousePosEvent(-100,-100);};
    auto Capture=[&](const char* Name){int PW=int(Width*Scale),PH=int(Height*Scale);std::vector<unsigned char> Pixels(size_t(PW)*PH*3,24);for(auto* L:ImGui::GetDrawData()->CmdLists)FrontierProof::Draw(L,Pixels.data(),PW,PH,{0,0},{Scale,Scale});char Path[200];std::snprintf(Path,sizeof(Path),"Exhibits/Gallery/MoonNative/%s.png",Name);Check(stbi_write_png(Path,PW,PH,3,Pixels.data(),PW*3)!=0,"native capture");};

 Rest();Capture("Moon-Full");auto* Props=Window("##moon-properties");ImVec2 O=Props->DC.CursorStartPos;O.x+=20;O.y+=20;float W=Props->WorkRect.GetWidth()-40;
 auto PickBody=[&](unsigned I){auto* Strip=Window("##moon-catalogue-strip");float Pad=std::max(0.f,(Strip->Size.x-732)/2);ImVec2 P=Strip->DC.CursorStartPos;Click({P.x+Pad+I*124+56,P.y+50});Rest();};
 PickBody(1);Check(Scene->MoonSlots[0].Preset==1,"horizontal catalogue sphere selects Ember");Capture("Moon-Ember");PickBody(0);Check(Scene->MoonSlots[0].Preset==0,"horizontal catalogue sphere selects Luna");
 Check(ImGui::GetCurrentContext()->OpenPopupStack.Size==0,"catalogue is its own panel, never a popup");Capture("Moon-Catalogue");
 auto* Strip=Window("##moon-catalogue-strip");Check(Strip->ScrollMax.x==0&&Strip->ScrollMax.y==0,"wide catalogue shows all six bodies in one row");
 auto DragMouse=[&](ImVec2 P,ImVec2 Delta){IO.AddMousePosEvent(P.x,P.y);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMousePosEvent(P.x+Delta.x,P.y+Delta.y);Tick();IO.AddMouseButtonEvent(0,false);Tick();Rest();IO.AddMousePosEvent(-100,-100);};
 // Wide layout: position card starts at 1463; its spherical view is at +96.
 float Az=Scene->MoonSlots[0].Azimuth,El=Scene->MoonSlots[0].Elevation;
 DragMouse({O.x+W/4,O.y+1674},{40,-20});Check(Scene->MoonSlots[0].Azimuth!=Az&&Scene->MoonSlots[0].Elevation!=El,"spherical dial native two-axis drag");
 float BeforeKey=Scene->MoonSlots[0].Azimuth;IO.AddKeyEvent(ImGuiKey_RightArrow,true);Tick();IO.AddKeyEvent(ImGuiKey_RightArrow,false);Tick();Check(Scene->MoonSlots[0].Azimuth==BeforeKey+1,"focused spherical dial keyboard step");
 DragMouse({O.x+W*.75f,O.y+1674},{40,-20});Check(Scene->MoonSlots[0].Roll>0&&Scene->MoonSlots[0].Pitch>0,"orientation native two-axis drag");Rest();Capture("Moon-Oriented");Click({O.x+W*.75f,O.y+1966});Check(Scene->MoonSlots[0].Roll==0&&Scene->MoonSlots[0].Pitch==0,"native reset orientation button");
 auto* PhaseSlider=Window("/M1 Phase_");Click({PhaseSlider->Pos.x+PhaseSlider->Size.x*.78f,PhaseSlider->Pos.y+15});Check(Scene->MoonSlots[0].Phase>.5f,"native days slider converts back to engine phase");
 Click({O.x+190,O.y+192});Check(Scene->MoonSlots[0].FollowSky,"native follow sky tile");float OldManual=Scene->MoonSlots[0].Azimuth;DragMouse({O.x+W/4,O.y+1674},{40,-20});Check(Scene->MoonSlots[0].Azimuth==OldManual,"follow sky protects stored manual position");Rest();Capture("Moon-FollowSky");Click({O.x+190,O.y+192});
 Click({O.x+W-120,O.y+136});Rest();auto Menu=ImGui::GetCurrentContext()->OpenPopupStack.back().Window->DC.CursorStartPos;Click({Menu.x+20,Menu.y+26});auto* Second=Window("/M2 Roll_");Check(Second->Active,"slot two gets its own native bindings");Rest();Capture("Moon-SecondSlot");Click({O.x+W-120,O.y+136});Rest();Menu=ImGui::GetCurrentContext()->OpenPopupStack.back().Window->DC.CursorStartPos;Click({Menu.x+20,Menu.y+8});
 Click({O.x+W-75,O.y+1329});IO.AddKeyEvent(ImGuiMod_Ctrl,true);IO.AddKeyEvent(ImGuiKey_A,true);Tick();IO.AddKeyEvent(ImGuiKey_A,false);IO.AddKeyEvent(ImGuiMod_Ctrl,false);Tick();IO.AddInputCharactersUTF8("1000");Tick();IO.AddKeyEvent(ImGuiKey_Enter,true);Tick();IO.AddKeyEvent(ImGuiKey_Enter,false);Tick();Check(Scene->MoonSlots[0].Size==1000,"native numeric entry accepts oversized moon");Rest();Capture("Moon-Oversized");Check(Find(*Sheet,"M1 Size").Maximum==1000,"size slider range grows with typed value");Find(*Sheet,"M1 Size").Figure=.52f;Apply();
 Width=480;Height=3500;Rest();Capture("Moon-Narrow");Strip=Window("##moon-catalogue-strip");Check(Strip->ScrollMax.x>0&&Strip->ScrollMax.y==0,"narrow catalogue remains horizontal, not a grid");float OldScroll=Strip->Scroll.x;auto Preset=Scene->MoonSlots[0].Preset;Click({O.x+440-36,O.y+410});Rest();Check(Strip->Scroll.x>OldScroll&&Scene->MoonSlots[0].Preset==Preset,"right arrow browses without replacing the selected moon");Capture("Moon-Catalogue-Scrolled");Click({O.x+36,O.y+410});Rest();Check(Strip->Scroll.x<OldScroll+124,"left arrow browses back");Height=900;Rest();Props=Window("##moon-properties");Check(Props->ScrollMax.y>1800,"narrow panel scroll");ImGui::SetScrollY(Props,1800);Rest();Capture("Moon-Scrolled");Width=320;ImGui::SetScrollY(Props,0);Rest();Check(Window("/M1 Phase_")->Size.x<=320,"compact slider fits");Width=1024;Height=900;Scale=2;Rest();Capture("Moon-2x");
 Scene->BuildSheet(CelestialEntity::Stars,*Sheet);Check(Sheet->Appearance==EditorSheetAppearance::Stars&&Sheet->MoonPhase==0&&!Sheet->MoonBodies[0].Pixels,"sheet reuse resets solved moon metadata");ImGui::DestroyContext();
 std::printf("PASS %u checks: native Moon catalogue, real atlas previews, phase conversion, pointer interactions, roll/pitch renderer lanes and CPU pixels, unlimited authored size, follow-sky protection, narrow/scroll and teardown. No GPU execution claim.\n",Checks);
}
