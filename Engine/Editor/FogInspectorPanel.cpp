#include "FogInspectorPanel.h"
#include "WindBindingControls.h"
#include "WindPanelSurface.h"
#include "ControlPanel.h"
#include "SunReferenceDraw.h"
#include "FogModel.h"
#include "FogPanelSurface.h"
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
struct Models {FogSettings Fog;AtmosphereMedium Air;LocalVolumeSettings Volume;bool Local=false,Aerial=false;};
Models Read(EditorSheet& S){Models M;M.Local=S.Appearance==EditorSheetAppearance::LocalFog;M.Aerial=S.Appearance==EditorSheetAppearance::AerialFog;auto F=[&](const char* N){return Find(S,N)->Figure;};bool On=Find(S,"Enabled")->On;
 for(int C=0;C<3;++C){M.Air.RayleighScattering[C]=S.FogPreview.Rayleigh[C];}M.Air.MieScattering=S.FogPreview.Mie;M.Air.RayleighScaleHeight=S.FogPreview.RayleighHeight;M.Air.MieScaleHeight=S.FogPreview.MieHeight;
 if(M.Local){auto& V=M.Volume;V.Enabled=On;V.Density=F("Density");V.Coverage=F("Coverage");V.Scale=F("Feature Scale");V.Anisotropy=F("Anisotropy");V.FollowWind=Find(S,"Follow Wind")->On;for(int C=0;C<3;++C){V.Centre[C]=Find(S,"Centre")->Axes[C];V.HalfSize[C]=Find(S,"Half Size")->Axes[C];}}
 else if(M.Aerial){M.Fog.AerialEnabled=On;M.Fog.AerialDensity=F("Density");M.Fog.AerialStart=F("Start");M.Fog.AerialMie=F("Mie Blend");}
 else{M.Fog.HeightEnabled=On;M.Fog.HeightDensity=F("Density");M.Fog.FalloffHeight=F("Falloff Height");M.Fog.SunScatter=F("Sun Scatter");for(int C=0;C<3;++C)M.Fog.HeightColour[C]=Find(S,"Colour")->ColourTint[C];}return M;}
void Sample(const Models& M,float D,float T[3],float RGB[3]){if(M.Local){auto S=FogModel::LocalProbe(M.Volume,D);for(int C=0;C<3;++C){T[C]=S.Transmittance;RGB[C]=.8f*T[C]+S.Scatter[C];}}else{FogModel::Transmission(M.Fog,M.Air,2,2,D,T);AtmosphereLight L;L.Direction[0]=0;L.Direction[1]=L.Direction[2]=.70710678f;L.Intensity=1;float Dir[]={0,1,0},Ambient[]={.18f,.18f,.18f};for(int C=0;C<3;++C)RGB[C]=.8f;FogModel::Apply(M.Fog,M.Air,L,2,2,D,Dir,Ambient,RGB);}}
struct Profile {std::array<float,32> Key{};float Values[65][3]{};bool Valid=false;};
void Cleanup(ImGuiContext*,ImGuiContextHook* H){delete static_cast<Profile*>(H->UserData);}
Profile& Cache(){constexpr ImGuiID Owner=0x464f4730;for(auto& H:ImGui::GetCurrentContext()->Hooks)if(H.Owner==Owner)return *static_cast<Profile*>(H.UserData);auto* P=new Profile;ImGuiContextHook H;H.Owner=Owner;H.Type=ImGuiContextHookType_Shutdown;H.Callback=Cleanup;H.UserData=P;ImGui::AddContextHook(ImGui::GetCurrentContext(),&H);return *P;}
void Update(Profile& P,const Models& M,float Range){const auto& F=M.Fog;const auto& V=M.Volume;const auto& A=M.Air;std::array<float,32> K={float(M.Local),float(M.Aerial),Range,float(F.HeightEnabled),F.HeightDensity,F.FalloffHeight,F.SunScatter,float(F.AerialEnabled),F.AerialDensity,F.AerialStart,F.AerialMie,float(V.Enabled),V.Density,V.Coverage,V.Scale,V.Anisotropy,V.Centre[0],V.Centre[1],V.Centre[2],V.HalfSize[0],V.HalfSize[1],V.HalfSize[2],A.RayleighScattering[0],A.RayleighScattering[1],A.RayleighScattering[2],A.MieScattering,A.RayleighScaleHeight,A.MieScaleHeight,F.HeightColour[0],F.HeightColour[1],F.HeightColour[2],float(V.FollowWind)};if(P.Valid&&K==P.Key)return;P.Valid=true;P.Key=K;for(int I=0;I<=64;++I){float RGB[3];Sample(M,Range*I/64,P.Values[I],RGB);}}
void Axes(Panel& U,float X,float Y,float W,const char* N,const char* Caption){auto& P=*Find(U.Sheet,N);U.Text(X,Y,Caption,11,Muted);ImGui::SetCursorScreenPos(U.At(X,Y+24));ImGui::BeginChild(N,{W,32},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);U.Controls.AxisVec3("##axes",P.Axes,1,true);ImGui::EndChild();}
void Beam(Panel& U,float X,float Y,float W,const Models& Source){
 Models M=Source;if(M.Local)M.Volume.Enabled=true;else if(M.Aerial)M.Fog.AerialEnabled=true;else M.Fog.HeightEnabled=true;
 const bool Live=M.Local?Source.Volume.Enabled:M.Aerial?Source.Fog.AerialEnabled:Source.Fog.HeightEnabled;
 const float Distance=M.Local||M.Aerial?400.f:120.f,Spread=M.Local?std::max(0.f,M.Volume.Anisotropy):M.Aerial?M.Fog.AerialMie:M.Fog.SunScatter;
 auto Transport=[&](float D,float Out[3]){if(!M.Local&&!M.Aerial)FogModel::Transmission(M.Fog,M.Air,25,25,D,Out);else{float RGB[3];Sample(M,M.Aerial?M.Fog.AerialStart+D:D,Out,RGB);}};
 U.Text(X,Y,"BEAM CHAMBER",9,Muted);U.Text(X+W-58,Y,Live?"LIVE":"PREVIEW",9,Live?IM_COL32(170,214,183,255):IM_COL32(221,151,139,255));
 U.Wrap(X,Y+19,W,M.Local?"Interior medium":M.Aerial?"Aerial medium after Start":"Height medium · 25 m layer");
 const float L=X+4,R=X+W-4,CY=Y+83;for(float G:{CY-26,CY,CY+26})U.D->AddLine(U.At(L,G),U.At(R,G),IM_COL32(255,255,255,18),1);
 float Range=Distance;for(int I=0;I<=68;++I){float D=Distance*I/68,T[3];Transport(D,T);float Mean=T[0]*.2126f+T[1]*.7152f+T[2]*.0722f,Exposure=.35f+.65f*std::pow(std::max(Mean,.000001f),.15f),F=float(I)/68,Half=3+F*F*(11+Spread*22),XX=L+(R-L)*F,A=(Live?1.f:.82f)*(.13f+std::min(2.f,Spread)*.2f)*Exposure;ImU32 C=M.Local||M.Aerial?Colour(212,227,236,A):Colour(255*M.Fog.HeightColour[0],255*M.Fog.HeightColour[1],255*M.Fog.HeightColour[2],A);U.D->AddLine(U.At(XX,CY-Half),U.At(XX,CY+Half),C,3.2f);if(Mean<=.02f&&Range==Distance)Range=D;}
 U.D->AddLine(U.At(L,CY),U.At(R,CY),IM_COL32(238,248,255,185),1.5f);U.D->AddCircleFilled(U.At(L,CY),4,IM_COL32(255,241,202,255));
 if(Range<Distance){float RX=L+(R-L)*Range/Distance;U.D->AddLine(U.At(RX,CY-38),U.At(RX,CY+38),IM_COL32(237,143,143,200),1);}
 char Text[96];std::snprintf(Text,sizeof(Text),"2%% range  %s",Range<Distance?"inside diagnostic span":"beyond diagnostic span");U.Text(X,Y+133,Text,9,Muted);float T[3];Transport(Distance,T);float Mean=T[0]*.2126f+T[1]*.7152f+T[2]*.0722f;std::snprintf(Text,sizeof(Text),"%.0f%% physical at %.0f m",double(Mean*100),double(Distance));U.Text(X+W-132,Y+133,Text,9,Muted);
}
}
void RecordFogInspector(ControlPanel& Controls,EditorInstance&,EditorSheet& Sheet){
 if(!Find(Sheet,"Density")||!Find(Sheet,"Enabled")){ImGui::TextUnformatted("Fog properties unavailable");return;}
 auto* Font=ImGui::GetFont();for(auto* F:ImGui::GetIO().Fonts->Fonts)if(!std::strcmp(F->GetDebugName(),"Sun reference / regular"))Font=F;ImGui::PushFont(Font,14);ImGui::PushID(static_cast<int>(Sheet.Appearance));ImVec2 O=ImGui::GetCursorScreenPos();O.x+=20;O.y+=20;float W=std::max(240.f,ImGui::GetContentRegionAvail().x-40);Panel U{Controls,Sheet,ImGui::GetWindowDrawList(),O,Font};Models M=Read(Sheet);char Text[160];
 U.Text(0,8,"Inspector / Environment",8,Muted);U.Text(0,48,"Fog",25);U.Text(0,83,M.Local?"LOCAL VOLUMETRIC FOG · bounded medium":M.Aerial?"AERIAL PERSPECTIVE · surface-distance fog":"HEIGHT FOG · exponential vertical density",10,Muted);
 FogCards::FogValues Air;Air.Kind=M.Local?FogCards::FogKind::Local:M.Aerial?FogCards::FogKind::Aerial:FogCards::FogKind::Height;Air.Enabled=Find(Sheet,"Enabled")->On;Air.Density=Find(Sheet,"Density")->Figure;
 if(M.Local){Air.Coverage=M.Volume.Coverage;Air.Anisotropy=M.Volume.Anisotropy;}else if(M.Aerial){Air.Start=M.Fog.AerialStart;Air.MieBlend=M.Fog.AerialMie;}else{Air.Falloff=M.Fog.FalloffHeight;Air.SunScatter=M.Fog.SunScatter;for(int C=0;C<3;++C)Air.Tint[C]=M.Fog.HeightColour[C];}
 // ResolveFogShape with nothing saved: the box the Half Size axes already describe.
 static FogCards::FogShape Shape;Shape.Wide=M.Volume.HalfSize[0]*2;Shape.Deep=M.Volume.HalfSize[1]*2;Shape.Tall=M.Volume.HalfSize[2]*2;Shape.Radius=M.Volume.HalfSize[0];
 U.Card(0,110,W,142,"Fog settings");U.Tile(24,164,112,"Enabled",&Find(Sheet,"Enabled")->On);if(M.Local)U.Tile(148,164,std::min(132.f,W-172),"Follow wind",&Find(Sheet,"Follow Wind")->On);
 float Range=M.Local?2*M.Volume.HalfSize[1]:M.Aerial?20000:500;ImGuiID ProbeID=ImGui::GetID("##probe-distance");float Probe=std::clamp(ImGui::GetStateStorage()->GetFloat(ProbeID,Range*.4f),0.f,Range);auto& P=Cache();Update(P,M,Range);float T[3],RGB[3];Sample(M,Probe,T,RGB);float Mean=T[0]*.2126f+T[1]*.7152f+T[2]*.0722f;
 U.Card(0,268,W,435,"Visibility through fog");std::snprintf(Text,sizeof(Text),"%.1f%%",double(Mean*100));U.Text(24,331,Text,32);std::snprintf(Text,sizeof(Text),"Light transmitted at %.0f m",double(Probe));U.Wrap(24,378,W-48,Text);U.Wrap(24,402,W-48,M.Local?"Sampled +Y path through the volume centre · static at time zero":"Horizontal probe at world Z = 2 m · selected medium only");
 float X0=48,X1=W-28,Y0=459,Y1=594;U.D->AddRectFilled(U.At(X0-8,Y0-12),U.At(X1+8,Y1+12),IM_COL32(26,30,33,255),12);for(int I=0;I<3;++I){float Y=Y0+(Y1-Y0)*I*.5f;U.D->AddLine(U.At(X0,Y),U.At(X1,Y),IM_COL32(147,178,182,28));}U.Text(12,Y0-5,"100",9,Muted);U.Text(25,Y1-5,"0",9,Muted);
 const ImU32 Colours[]={IM_COL32(203,142,142,255),IM_COL32(153,204,165,255),IM_COL32(150,184,225,255)};
 for(int C=0;C<(M.Aerial?3:1);++C)for(int I=1;I<=64;++I)U.D->AddLine(U.At(X0+(X1-X0)*(I-1)/64,Y1-P.Values[I-1][C]*(Y1-Y0)),U.At(X0+(X1-X0)*I/64,Y1-P.Values[I][C]*(Y1-Y0)),M.Aerial?Colours[C]:IM_COL32(168,209,210,255),1.6f);
 float PX=X0+Probe/Range*(X1-X0);U.D->AddLine(U.At(PX,Y0),U.At(PX,Y1),IM_COL32(206,224,221,65));U.D->AddCircleFilled(U.At(PX,Y1-Mean*(Y1-Y0)),4,IM_COL32(217,234,230,255));ImGui::SetCursorScreenPos(U.At(X0,Y0-12));ImGui::InvisibleButton("##fog-probe",{X1-X0,Y1-Y0+24},ImGuiButtonFlags_EnableNav);if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0))Probe=std::clamp((ImGui::GetIO().MousePos.x-U.At(X0,0).x)/(X1-X0)*Range,0.f,Range);if(ImGui::IsItemFocused()){float Step=Range/100;if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))Probe=std::max(0.f,Probe-Step);if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))Probe=std::min(Range,Probe+Step);if(ImGui::IsKeyPressed(ImGuiKey_Home))Probe=0;}ImGui::GetStateStorage()->SetFloat(ProbeID,Probe);
 U.Text(X0,614,"0 m",10,Muted);std::snprintf(Text,sizeof(Text),"%.0f m",double(Range));U.Text(X1-54,614,Text,10,Muted);U.Wrap(24,647,W-48,"Drag the probe · arrow keys adjust diagnostic distance, not scene settings");
 bool Wide=W>=760;float CW=Wide?(W-16)/2:W,X2=Wide?CW+16:0,Y=719,MediumH=M.Local?620:560,Y2=Wide?Y:Y+MediumH+16;
 U.Card(0,Y,CW,MediumH,"Medium");U.Slider(24,Y+66,CW-48,"Density");U.Slider(24,Y+145,CW-48,M.Local?"Coverage":M.Aerial?"Start":"Falloff Height");U.Slider(24,Y+224,CW-48,M.Local?"Feature Scale":M.Aerial?"Mie Blend":"Sun Scatter");if(M.Local)U.Slider(24,Y+303,CW-48,"Anisotropy");else U.Wrap(24,Y+294,CW-48,M.Aerial?"Uses the selected atmosphere coefficients; Mie blend moves from spectral Rayleigh extinction toward grey aerosol extinction.":"Falloff is the e-folding height; Sun Scatter changes in-scattered light, not extinction.");FogCards::PaintChamber(U.D,U.Font,U.At(24,Y+(M.Local?390:350)),CW-48,Air);
 float TechH=M.Local?56+FogCards::VolumeHeight(U.Font,CW-48,Shape)+24:M.Aerial?416:56+FogCards::ProfileHeight(U.Font,CW-48)+18+46+24;U.Card(X2,Y2,CW,TechH,M.Local?"Fog volume shape":M.Aerial?"Spectral transmission":"Height and tint");
 if(M.Local){FogCards::PaintVolume(U.D,U.Font,U.At(X2+24,Y2+56),CW-48,Shape,M.Volume.Centre,&Shape);
 }else if(M.Aerial){Models Preview=M;Preview.Fog.AerialEnabled=true;float ST[3],SRGB[3];Sample(Preview,Probe,ST,SRGB);float Tau=-std::log(std::max(ST[1],.000001f)),Mix=std::clamp(M.Fog.AerialMie,0.f,1.f);ImGuiID SpectrumID=ImGui::GetID("##fog-spectrum-wavelength");float Wave=std::clamp(ImGui::GetStateStorage()->GetFloat(SpectrumID,550),380.f,780.f);auto Spectral=[&](float L){float Shape=(1-Mix)*std::pow(550/L,4.f)+Mix*std::pow(550/L,1.3f);return 100*std::exp(-Tau*Shape);};std::snprintf(Text,sizeof(Text),"%.0f nm     %.1f%%",double(Wave),double(Spectral(Wave)));U.Text(X2+24,Y2+67,Text,27);U.Wrap(X2+24,Y2+106,CW-48,"Atmosphere source · active medium coefficients");float L=X2+46,R=X2+CW-26,Top=Y2+150,Bottom=Y2+305;for(int I=0;I<3;++I){float YY=Top+(Bottom-Top)*I*.5f;U.D->AddLine(U.At(L,YY),U.At(R,YY),IM_COL32(147,178,182,28));}for(int I=1;I<=80;++I){float A=380+(I-1)*5,B=380+I*5;U.D->AddLine(U.At(L+(R-L)*(I-1)/80,Bottom-Spectral(A)/100*(Bottom-Top)),U.At(L+(R-L)*I/80,Bottom-Spectral(B)/100*(Bottom-Top)),IM_COL32(158,184,208,255),1.6f);}float WX=L+(R-L)*(Wave-380)/400;U.D->AddLine(U.At(WX,Top),U.At(WX,Bottom),IM_COL32(229,215,189,100));U.D->AddCircleFilled(U.At(WX,Bottom-Spectral(Wave)/100*(Bottom-Top)),4,IM_COL32(207,225,235,255));ImGui::SetCursorScreenPos(U.At(L,Top));ImGui::InvisibleButton("##aerial-spectrum",{R-L,Bottom-Top},ImGuiButtonFlags_EnableNav);if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0))Wave=std::clamp(380+(ImGui::GetIO().MousePos.x-U.At(L,0).x)/(R-L)*400,380.f,780.f);if(ImGui::IsItemFocused()){if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))Wave=std::max(380.f,Wave-4);if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))Wave=std::min(780.f,Wave+4);if(ImGui::IsKeyPressed(ImGuiKey_Home))Wave=380;if(ImGui::IsKeyPressed(ImGuiKey_End))Wave=780;}ImGui::GetStateStorage()->SetFloat(SpectrumID,Wave);U.Text(L,Bottom+13,"380 nm",9,Muted);U.Text(R-42,Bottom+13,"780 nm",9,Muted);U.Wrap(X2+24,Y2+353,CW-48,"Illustrative wavelength response at the shared Distance probe; physical RGB transmission remains in Visibility.");}
 else{float Authored[2]={Air.Density,Air.Falloff};FogCards::PaintProfile(U.D,U.Font,U.At(X2+24,Y2+56),CW-48,Air,&Authored[0],&Authored[1]);if(Authored[0]!=Air.Density)Find(Sheet,"Density")->Figure=Authored[0];if(Authored[1]!=Air.Falloff)Find(Sheet,"Falloff Height")->Figure=Authored[1];float ChipY=Y2+56+FogCards::ProfileHeight(U.Font,CW-48)+18;U.Text(X2+24,ChipY,"Fog tint \u00b7 linear RGB",11,Muted);ImGui::SetCursorScreenPos(U.At(X2+24,ChipY+14));ImGui::BeginChild("Colour",{CW-48,32},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);Controls.ColourChip("##fog-colour",Find(Sheet,"Colour")->ColourTint);ImGui::EndChild();}
 float End=std::max(Y+MediumH,Y2+TechH+16)+16;
 // WindPanel.jsx WindBinding: the field select, the editor shortcut and the live composite preview.
 {
  static WindCards::WindMotes Motes;
  auto* Source=Find(Sheet,"Wind Source");
  WindCards::BindingValues Bound;
  // Wind Source always resolves: entry 0 is the shared Global wind, not the browser's still-air blank.
  const unsigned Pick=Source&&Source->Picked<Source->OptionCount?Source->Picked:0;
  Bound.Assigned=Source!=nullptr;
  Bound.FieldName=Source?Source->Options[Pick]:nullptr;
  Bound.Following=Find(Sheet,"Follow Wind")?Find(Sheet,"Follow Wind")->On:true;
  // No authored WindField reaches the native sheet, so ResolveWind's own fallback pair stands in.
  const WindCards::WindComposite Air=WindCards::Resolve(7.f,250.f,.25f);
  const float Body=WindCards::BindingBodyHeight(U.Font,W-48,Bound);
  U.Card(0,End,W,49+Body+104,"Wind binding");
  auto Hits=WindCards::PaintBindingBody(U.D,U.Font,U.At(24,End+49),W-48,Bound,Air,Motes,ImGui::GetIO().DeltaTime,IM_COL32(34,34,34,255));
  if(Source&&Source->OptionCount>1){
   ImGui::SetCursorScreenPos({Hits.Select.x,Hits.Select.y});
   if(ImGui::InvisibleButton("##fog-wind-source",{Hits.Select.z-Hits.Select.x,Hits.Select.w-Hits.Select.y}))
    Source->Picked=(Source->Picked+1)%Source->OptionCount;
  }
  ImGui::SetCursorScreenPos(U.At(24,End+49+Body+12));
  ImGui::BeginChild("##fog-wind-binding",{W-48,76},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);RecordWindBindingControls(Sheet);ImGui::EndChild();
  End+=49+Body+104+16;
 }
 U.Wrap(0,End,W,"Live CPU/GPU fog controls. Local fog uses its selected wind source; analytic fog has no horizontal noise to advect.");ImGui::SetCursorScreenPos(U.At(0,End+86));ImGui::Dummy({W,1});ImGui::PopID();ImGui::PopFont();
}
}
