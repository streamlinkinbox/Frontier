#include "CameraInspectorPanel.h"
#include "ControlPanel.h"
#include "SunReferenceDraw.h"
#include "CameraOptics.h"
#include "InspectorReferenceDraw.h"
#include "SunColourTemperature.h"
#include <array>
#include <imgui_internal.h>
#include <cstdio>
#include <cstring>
namespace Frontier { namespace {
using namespace SunReference;
constexpr ImU32 Ink=IM_COL32(233,233,233,255),Muted=IM_COL32(145,145,145,255);
EditorProperty* Find(EditorSheet& S,const char* N){for(auto& G:S.Groups)for(unsigned I=0;I<G.PropertyCount;++I)if(!std::strcmp(G.Properties[I].Label,N))return &G.Properties[I];return nullptr;}
struct Panel {
 ControlPanel& Controls;EditorSheet& Sheet;ImDrawList* D;ImVec2 O;ImFont* Font;
 ImVec2 At(float X,float Y){return {O.x+X,O.y+Y};}
 void Text(float X,float Y,const char* T,float Size=12,ImU32 C=Ink){D->AddText(Font,Size,At(X,Y),C,T);}
 void Wrap(float X,float Y,float W,const char* T){D->AddText(Font,11,At(X,Y),Muted,T,nullptr,W);}
 void Card(float X,float Y,float W,float H,const char* Title){int Start=D->VtxBuffer.Size;D->AddRectFilled(At(X,Y),At(X+W,Y+H),IM_COL32_WHITE,22);ImGui::ShadeVertsLinearColorGradientKeepAlpha(D,Start,D->VtxBuffer.Size,At(X,Y),At(X+W,Y+H),IM_COL32(37,37,37,255),IM_COL32(32,32,32,255));D->AddRect(At(X,Y),At(X+W,Y+H),IM_COL32(52,52,52,255),22);Text(X+24,Y+23,Title);}
 void Slider(float X,float Y,float W,const char* N){auto& P=*Find(Sheet,N);Text(X,Y,N,11,Muted);ImGui::SetCursorScreenPos(At(X,Y+22));ImGui::PushID(N);ImGui::BeginChild(N,{W,30},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);Controls.SliderPill("##value",&P.Figure,P.Minimum,P.Maximum,P.Decimals,P.Unit,false,false,true);ImGui::EndChild();ImGui::PopID();}
 void Tile(float X,float Y,float W,const char* N,bool* Value){ImGui::SetCursorScreenPos(At(X,Y));ImGui::BeginDisabled(!Value);if(ImGui::InvisibleButton(N,{W,65})&&Value)*Value=!*Value;ImGui::EndDisabled();bool On=Value&&*Value;ImU32 C=!Value?Muted:On?IM_COL32(131,207,158,255):IM_COL32(221,137,137,255);D->AddRectFilled(At(X,Y),At(X+W,Y+65),IM_COL32(34,34,34,255),13);D->AddRect(At(X,Y),At(X+W,Y+65),IM_COL32(56,56,56,255),13);if(Value){D->AddCircle(At(X+W/2,Y+20),7,C,20,1.4f);D->AddLine(At(X+W/2,Y+10),At(X+W/2,Y+18),C,1.5f);}else{float M=X+W/2;D->AddRect(At(M-7,Y+15),At(M+7,Y+28),C,2);D->AddLine(At(M-11,Y+12),At(M+2,Y+12),C,1.4f);D->AddLine(At(M-2,Y+8),At(M+2,Y+12),C,1.4f);D->AddLine(At(M-2,Y+16),At(M+2,Y+12),C,1.4f);}float L=Font->CalcTextSizeA(10,10000,0,N).x;Text(X+(W-L)/2,Y+42,N,10,C);}
};
constexpr ImU32 Accent=IM_COL32(192,167,216,255);
void Distance(char* T,size_t Size,float V){if(std::isfinite(V))std::snprintf(T,Size,"%.1f m",double(V));else std::snprintf(T,Size,"infinity");}
}
void RecordCameraInspector(ControlPanel& Controls,EditorInstance&,EditorSheet& Sheet){
 auto* Font=ImGui::GetFont();for(auto* F:ImGui::GetIO().Fonts->Fonts)if(!std::strcmp(F->GetDebugName(),"Sun reference / regular"))Font=F;
 ImGui::PushFont(Font,14);ImGui::PushID("camera-optics");ImVec2 O=ImGui::GetCursorScreenPos();O.x+=20;O.y+=20;float W=std::max(240.f,ImGui::GetContentRegionAvail().x-40);Panel U{Controls,Sheet,ImGui::GetWindowDrawList(),O,Font};
 CameraOpticsSettings S;S.Focal=Find(Sheet,"Focal Length")->Figure;S.SensorWidth=Find(Sheet,"Sensor Width")->Figure;S.Aperture=Find(Sheet,"Aperture")->Figure;S.Subject=Find(Sheet,"Subject Distance")->Figure;auto Opt=EvaluateCameraOptics(S,Sheet.CameraAspect);char Text[180];
 U.Text(0,8,"Inspector / Scene / Cameras",8,Muted);U.Text(0,48,Sheet.CameraLive?"Main Camera":"Cine Camera · lens study",25);U.Wrap(0,83,W,Sheet.CameraLive?"LIVE PROJECTION · aperture and focus are diagnostics only":"INACTIVE STUDY · does not switch or modify the viewport camera");
 float H=std::max(170.f,(W-48)*290/640);float Bottom=110+H+270;U.Card(0,110,W,H+270,"Lens + field of view");std::snprintf(Text,sizeof(Text),"%.1f mm",double(S.Focal));U.Text(24,164,Text,30);
 Canvas C=Fit(U.D,U.At(24,212),{W-48,H},640,290);float Extent=std::min(113.f,34+Opt.Horizontal*.85f),Gap=8+std::min(38.f,Opt.Pupil*.75f);
 C.Line({30,141},{610,141},Colour(179,165,191,.15f));C.Line({235,141},{553,141-Extent},Accent);C.Line({235,141},{553,141+Extent},Accent);C.Line({553,141-Extent},{553,141+Extent},Accent,2);
 for(float Y:{107.f,175.f}){C.Line({78,Y},{235,141},Colour(183,157,208,.36f));C.Line({235,141},{553,Y<141?141+Extent:141-Extent},Colour(183,157,208,.36f));}
 U.D->AddRectFilled(C.P(68,100),C.P(80,182),Colour(164,182,197,.18f),3);U.D->AddRect(C.P(68,100),C.P(80,182),Colour(185,199,214),3);
 for(int I=0;I<6;++I)C.Line({71,108.f+I*13},{77,108.f+I*13},Colour(208,218,226),.6f);
 C.Line({235,72},{235,141-Gap},Accent,7);C.Line({235,141+Gap},{235,210},Accent,7);C.Line({225,141-Gap},{245,141-Gap},Colour(226,208,244));C.Line({225,141+Gap},{245,141+Gap},Colour(226,208,244));
 std::snprintf(Text,sizeof(Text),"%.1f degrees",double(Opt.Horizontal));C.Text(380,132,Text,Accent,20);C.Text(380,154,"HORIZONTAL FOV",Muted,9);
 C.Text(75,236,"Sensor",Ink,10);std::snprintf(Text,sizeof(Text),"%.1f x %.1f mm",double(S.SensorWidth),double(Opt.SensorHeight));C.Text(75,255,Text,Muted,9);std::snprintf(Text,sizeof(Text),"f/%.1f",double(S.Aperture));C.Text(235,236,Text,Accent,11);std::snprintf(Text,sizeof(Text),"%.1f mm pupil",double(Opt.Pupil));C.Text(235,255,Text,Muted,9);std::snprintf(Text,sizeof(Text),"Subject plane %.1f m",double(S.Subject));C.Text(535,272,Text,Muted,9);
 U.Wrap(24,214+H,W-48,"Optical schematic · not to scale. Sensor height follows the viewport aspect for the live camera.");
 bool Wide=W>=760;float CW=Wide?(W-16)/2:W;U.Slider(24,Bottom-99,std::min(480.f,W-48),"Focal Length");std::snprintf(Text,sizeof(Text),"Vertical FOV %.1f° · frame at subject %.2f x %.2f m",double(Opt.Vertical),double(Opt.FrameWidth),double(Opt.FrameHeight));U.Wrap(24,Bottom-32,W-48,Text);
 float Y=Bottom+16,Y2=Wide?Y:Y+480,X2=Wide?CW+16:0;
 U.Card(0,Y,CW,464,"Aperture study");std::snprintf(Text,sizeof(Text),"f / %.1f",double(S.Aperture));U.Text(24,Y+63,Text,30);U.Wrap(24,Y+109,CW-48,"Entrance pupil and acceptable-sharpness limits update together. No renderer exposure or blur is changed.");InspectorReference::Iris(Fit(U.D,U.At((CW-132)/2,Y+164),{132,132},220,220),S.Aperture);U.Slider(24,Y+327,CW-48,"Aperture");std::snprintf(Text,sizeof(Text),"Pupil diameter %.1f mm",double(Opt.Pupil));U.Wrap(24,Y+398,CW-48,Text);char Depth[32];Distance(Depth,sizeof(Depth),Opt.Far-Opt.Near);std::snprintf(Text,sizeof(Text),"In-focus depth %s",Depth);U.Wrap(24,Y+426,CW-48,Text);
 U.Card(X2,Y2,CW,464,"Subject plane + sharpness");std::snprintf(Text,sizeof(Text),"%.1f m",double(S.Subject));U.Text(X2+24,Y2+63,Text,30);U.Wrap(X2+24,Y2+109,CW-48,"Drag the subject plane. Thin-lens diagnostic · circle of confusion 0.03 mm.");
 Canvas FocusCanvas=Fit(U.D,U.At(X2+24,Y2+159),{CW-48,CW<400?126.f:150.f},360,174);
 InspectorReference::Focus(FocusCanvas,S.Subject,Opt.Near,Opt.Far);
 ImGui::SetCursorScreenPos(FocusCanvas.P(0,0));ImGui::InvisibleButton("##subject-plane",{360*FocusCanvas.Scale,174*FocusCanvas.Scale},ImGuiButtonFlags_EnableNav);float& Subject=Find(Sheet,"Subject Distance")->Figure;
 if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0))Subject=InspectorReference::FocusFromX((ImGui::GetIO().MousePos.x-FocusCanvas.Origin.x)/FocusCanvas.Scale);
 if(ImGui::IsItemFocused()){if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))Subject=std::max(1.f,Subject-1);if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))Subject=std::min(100.f,Subject+1);}
 U.Slider(X2+24,Y2+327,CW-48,"Subject Distance");char Near[32],Far[32];Distance(Near,sizeof(Near),Opt.Near);Distance(Far,sizeof(Far),Opt.Far);std::snprintf(Text,sizeof(Text),"Near %s / far %s",Near,Far);U.Wrap(X2+24,Y2+402,CW-48,Text);
 Y=Y2+480;U.Card(0,Y,W,196,"Sensor / support");U.Slider(24,Y+60,std::min(480.f,W-48),"Sensor Width");std::snprintf(Text,sizeof(Text),"Aspect %.3f · hyperfocal %.1f m · pinhole renderer",double(Sheet.CameraAspect),double(Opt.Hyperfocal));U.Wrap(24,Y+130,W-48,Text);
 U.Wrap(0,Y+217,W,"Aperture and subject distance are session optical-study settings only. No depth-of-field, physical exposure, autofocus, camera switching or file persistence is claimed. Lens changes on Main Camera drive the real perspective projection.");ImGui::SetCursorScreenPos(U.At(0,Y+310));ImGui::Dummy({W,1});ImGui::PopID();ImGui::PopFont();
}
}
