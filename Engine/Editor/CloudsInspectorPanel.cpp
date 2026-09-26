#include "CloudsInspectorPanel.h"
#include "WindBindingControls.h"
#include "ControlPanel.h"
#include "SunReferenceDraw.h"
#include "CloudDensityPreview.h"
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
struct CloudCache {ImTextureData Top,Side;std::array<float,17> Key{};bool Valid=false;CloudCache(){for(auto* T:{&Top,&Side}){T->Create(ImTextureFormat_RGBA32,192,96);T->UseColors=true;std::memset(T->Pixels,0,192*96*4);ImGui::RegisterUserTexture(T);}}};
void Cleanup(ImGuiContext*,ImGuiContextHook* H){auto* C=static_cast<CloudCache*>(H->UserData);ImGui::UnregisterUserTexture(&C->Top);ImGui::UnregisterUserTexture(&C->Side);delete C;}
CloudCache& Cache(){constexpr ImGuiID Owner=0x434c4f55;for(auto& H:ImGui::GetCurrentContext()->Hooks)if(H.Owner==Owner)return *static_cast<CloudCache*>(H.UserData);auto* C=new CloudCache;ImGuiContextHook H;H.Owner=Owner;H.Type=ImGuiContextHookType_Shutdown;H.Callback=Cleanup;H.UserData=C;ImGui::AddContextHook(ImGui::GetCurrentContext(),&H);return *C;}
void Update(CloudCache& C,EditorSheet& S,bool Local,float Aspect){auto F=[&](const char* N){return Find(S,N)->Figure;};CloudLayerSettings G;LocalVolumeSettings L;
 G.Enabled=L.Enabled=Find(S,"Enabled")->On;G.Coverage=L.Coverage=F("Coverage");G.Density=L.Density=F("Density");G.Scale=L.Scale=F("Feature Scale");
 if(Local){for(int I=0;I<3;++I){L.Centre[I]=Find(S,"Centre")->Axes[I];L.HalfSize[I]=Find(S,"Half Size")->Axes[I];}}
 else{G.Base=F("Base");G.Thickness=F("Thickness");G.CeilingMetres=F("Ceiling");G.Anvil=F("Anvil");G.Type=static_cast<CloudTypeCategory>(Find(S,"Type")->Picked);}
 std::array<float,17> Key={float(Local),float(G.Enabled),G.Coverage,G.Density,G.Scale,G.Base,G.Thickness,G.CeilingMetres,G.Anvil,float(G.Type),L.Centre[0],L.Centre[1],L.Centre[2],L.HalfSize[0],L.HalfSize[1],L.HalfSize[2],Aspect};
 if(C.Valid&&Key==C.Key)return;
 CloudDensityPreview(C.Top.Pixels,C.Side.Pixels,192,96,G,L,Local,Aspect);C.Key=Key;C.Valid=true;ImTextureDataQueueUpload(&C.Top,0,0,192,96);ImTextureDataQueueUpload(&C.Side,0,0,192,96);
}
void Select(Panel& U,float X,float Y,float W,const char* N){auto& P=*Find(U.Sheet,N);U.Text(X,Y,N,11,Muted);ImGui::SetCursorScreenPos(U.At(X,Y+22));ImGui::BeginChild(N,{W,32},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);U.Controls.DropDown("##select",&P.Picked,P.Options,P.OptionCount);ImGui::EndChild();}
void Axes(Panel& U,float Y,float W,const char* N,const char* Caption){auto& P=*Find(U.Sheet,N);U.Text(24,Y,Caption,11,Muted);ImGui::SetCursorScreenPos(U.At(24,Y+24));ImGui::BeginChild(N,{W-48,32},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);U.Controls.AxisVec3("##axes",P.Axes,1,true);ImGui::EndChild();}
}
void RecordCloudsInspector(ControlPanel& Controls,EditorInstance&,EditorSheet& Sheet){
 RecordWindBindingControls(Sheet);
 if(!Find(Sheet,"Coverage")||!Find(Sheet,"Anisotropy")){ImGui::TextUnformatted("Cloud properties unavailable");return;}
 bool Local=Sheet.Appearance==EditorSheetAppearance::LocalCloud;auto* Font=ImGui::GetFont();for(auto* F:ImGui::GetIO().Fonts->Fonts)if(!std::strcmp(F->GetDebugName(),"Sun reference / regular"))Font=F;ImGui::PushFont(Font,14);ImVec2 O=ImGui::GetCursorScreenPos();O.x+=20;O.y+=20;float W=std::max(240.f,ImGui::GetContentRegionAvail().x-40);Panel U{Controls,Sheet,ImGui::GetWindowDrawList(),O,Font};auto& Cached=Cache();Update(Cached,Sheet,Local,(W-48)/182);char Text[160];
 U.Text(0,8,"Inspector / Environment",8,Muted);U.Text(0,48,"Clouds",25);U.Text(0,83,Local?"LOCAL VOLUMETRIC CLOUD · bounded volume":"GLOBAL VOLUMETRIC CLOUDS · atmospheric layer",10,Muted);
 U.Card(0,110,W,153,"Cloud settings");float TW=std::min(132.f,(W-60)/2);U.Tile(24,165,TW,"Enabled",&Find(Sheet,"Enabled")->On);U.Tile(36+TW,165,TW,"Follow wind",&Find(Sheet,"Follow Wind")->On);
 U.Card(0,279,W,422,"Cloud coverage");auto& Coverage=*Find(Sheet,"Coverage");std::snprintf(Text,sizeof(Text),"%.0f%%",double(Coverage.Figure*100));U.Text(24,342,Text,30);U.Wrap(24,388,W-48,Local?"One continuous density field within the local bounds":"One continuous density field · gaps close as coverage increases");
 U.D->AddImageRounded(Cached.Top.GetTexRef(),U.At(24,425),U.At(W-24,607),{0,0},{1,1},IM_COL32_WHITE,12);U.Text(32,438,"TOP-DOWN DENSITY",9,IM_COL32(194,211,229,255));
 Coverage.Figure*=100;Coverage.Maximum=100;Coverage.Decimals=0;std::snprintf(Coverage.Unit,sizeof(Coverage.Unit),"%%");U.Slider(24,625,W-48,"Coverage");Coverage.Figure/=100;Coverage.Maximum=1;Coverage.Decimals=2;Coverage.Unit[0]=0;
 U.Wrap(24,680,W-48,"Static density diagnostic · not measured sky cover or a lit render");
 bool Wide=W>=760;float CW=Wide?(W-16)/2:W,X2=Wide?CW+16:0,Y=717,Y2=Wide?Y:Y+468;
 U.Card(0,Y,CW,452,Local?"Local bounds":"Cloud base");
 if(Local){auto* Centre=Find(Sheet,"Centre");auto* Half=Find(Sheet,"Half Size");std::snprintf(Text,sizeof(Text),"%.0f × %.0f × %.0f m",double(Half->Axes[0]*2),double(Half->Axes[1]*2),double(Half->Axes[2]*2));U.Wrap(24,Y+65,CW-48,Text);
  Canvas C=Fit(U.D,U.At(24,Y+107),{CW-48,185},340,200);float Max=std::max({Half->Axes[0],Half->Axes[1],Half->Axes[2],.1f});float SX=Half->Axes[0]/Max*85,SY=Half->Axes[1]/Max*45,SZ=Half->Axes[2]/Max*80;auto P=[&](int I){float X=I&1?SX:-SX,Q=I&2?SY:-SY,Z=I&4?SZ:-SZ;return ImVec2(170+X+Q,100+Q*.5f-Z);};for(int I=0;I<8;++I)for(int Bit:{1,2,4})if(!(I&Bit))C.Line(P(I),P(I|Bit),Colour(158,185,215,.65f));C.Text(170,194,"World-space bounds · Z up",Muted,9);
  (void)Centre;Axes(U,Y+311,CW,"Centre","Centre · world X / Y / Z (m)");Axes(U,Y+378,CW,"Half Size","Half extents · X / Y / Z (m)");
 }else{float Base=Find(Sheet,"Base")->Figure,Ceil=Find(Sheet,"Ceiling")->Figure,Top=std::min(Ceil,Base+Find(Sheet,"Thickness")->Figure);std::snprintf(Text,sizeof(Text),"%.2f km",double(Base/1000));U.Text(24,Y+66,Text,30);U.Wrap(24,Y+112,CW-48,"World Z altitude · drag the base line");
  float L=48,R=CW-32,T=Y+159,B=Y+337;U.D->AddImageRounded(Cached.Side.GetTexRef(),U.At(L,T),U.At(R,B),{0,0},{1,1},IM_COL32_WHITE,9);for(int I=0;I<=4;++I){float YY=T+(B-T)*I/4;U.D->AddLine(U.At(L,YY),U.At(R,YY),IM_COL32(140,162,189,35));std::snprintf(Text,sizeof(Text),"%.0f",double(Ceil/1000*(1-I/4.f)));U.Text(24,YY-4,Text,9,Muted);}float BY=B-Base/Ceil*(B-T),TY=B-Top/Ceil*(B-T);U.D->AddLine(U.At(L,BY),U.At(R,BY),IM_COL32(206,227,249,255),1.3f);U.D->AddLine(U.At(L,TY),U.At(R,TY),IM_COL32(143,173,210,160));U.D->AddCircleFilled(U.At(R-12,BY),5,IM_COL32(206,227,249,255));ImGui::SetCursorScreenPos(U.At(L,T));ImGui::InvisibleButton("##cloud-base-drag",{R-L,B-T},ImGuiButtonFlags_EnableNav);if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0))Find(Sheet,"Base")->Figure=std::clamp((U.At(0,B).y-ImGui::GetIO().MousePos.y)/(B-T)*Ceil,100.f,Ceil);if(ImGui::IsItemFocused()){float& V=Find(Sheet,"Base")->Figure;if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))V=std::min(Ceil,V+100);if(ImGui::IsKeyPressed(ImGuiKey_DownArrow))V=std::max(100.f,V-100);}
  U.Text(48,Y+350,"0 m world datum · not terrain-relative AGL",9,Muted);U.Slider(24,Y+383,CW-48,"Base");
 }
 U.Card(X2,Y2,CW,452,Local?"Volume section":"Layer thickness");
 if(Local){U.Text(X2+24,Y2+70,"X–Z section",25);U.Wrap(X2+24,Y2+116,CW-48,"Real density through the centre of the bounded volume");U.D->AddImageRounded(Cached.Side.GetTexRef(),U.At(X2+24,Y2+160),U.At(X2+CW-24,Y2+337),{0,0},{1,1},IM_COL32_WHITE,12);auto* P=Find(Sheet,"Centre");auto* H=Find(Sheet,"Half Size");std::snprintf(Text,sizeof(Text),"Base %.0f m · top %.0f m (world Z)",double(P->Axes[2]-H->Axes[2]),double(P->Axes[2]+H->Axes[2]));U.Wrap(X2+24,Y2+365,CW-48,Text);U.Wrap(X2+24,Y2+401,CW-48,"The engine softly fades density inside the box boundary.");}
 else{float Thick=Find(Sheet,"Thickness")->Figure,Base=Find(Sheet,"Base")->Figure,Ceil=Find(Sheet,"Ceiling")->Figure;std::snprintf(Text,sizeof(Text),"%.2f km",double(Thick/1000));U.Text(X2+24,Y2+66,Text,30);U.Wrap(X2+24,Y2+112,CW-48,"Vertical development · density profile");float Span=CW-80;for(int I=0;I<80;++I){float H=(I+.5f)/80,V=VolumetricMedia::HeightProfile(static_cast<CloudTypeCategory>(Find(Sheet,"Type")->Picked),H,Find(Sheet,"Anvil")->Figure);float YY=Y2+304-H*142;U.D->AddLine(U.At(X2+40,YY),U.At(X2+40+Span*std::min(1.f,V/1.6f),YY),Colour(160,190,223,.3f),2);}std::snprintf(Text,sizeof(Text),"Effective top %.2f km%s",double(std::min(Ceil,Base+Thick)/1000),Base+Thick>Ceil?" · ceiling clipped":"");U.Wrap(X2+24,Y2+327,CW-48,Text);U.Slider(X2+24,Y2+383,CW-48,"Thickness");}
 float End=Y2+468;U.Card(0,End,W,Local?270:402,"Cloud body");U.Slider(24,End+60,W-48,"Density");U.Slider(24,End+128,W-48,"Feature Scale");U.Slider(24,End+196,W-48,"Anisotropy");if(!Local){Select(U,24,End+264,(W-64)/2,"Type");U.Slider(40+(W-64)/2,End+264,(W-64)/2,"Anvil");U.Slider(24,End+326,W-48,"Ceiling");}
 End+=Local?286:418;U.Wrap(0,End,W,"Live cloud controls. Density previews are static at time zero; CPU/GPU scene volumes use the selected wind source.");End+=58;
 if(!Local){ImGuiID ID=ImGui::GetID("##cloud-shadow-fold");bool Open=ImGui::GetStateStorage()->GetBool(ID);U.Card(0,End,W,52,"GPU cloud shadows · separate field");U.Text(W-32,End+22,Open?"−":"+",13,Muted);ImGui::SetCursorScreenPos(U.At(0,End));if(ImGui::InvisibleButton("##cloud-shadow-fold",{W,52})){Open=!Open;ImGui::GetStateStorage()->SetBool(ID,Open);}if(Open){End+=68;for(auto& G:Sheet.Groups)if(!std::strcmp(G.Title,"Cloud Shadows")||!std::strcmp(G.Title,"Shadow Clock")){for(unsigned I=0;I<G.PropertyCount;++I){auto& P=G.Properties[I];if(P.Category==EditorPropertyCategory::Slider)U.Slider(24,End,W-48,P.Label);else if(P.Category==EditorPropertyCategory::Select)Select(U,24,End,W-48,P.Label);else U.Tile(24,End,132,P.Label,&P.On);End+=72;}}}else End+=52;}
 ImGui::SetCursorScreenPos(U.At(0,End+24));ImGui::Dummy({W,1});ImGui::PopFont();
}
}
