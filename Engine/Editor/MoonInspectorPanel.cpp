#include "MoonInspectorPanel.h"
#include "ControlPanel.h"
#include "MoonReferenceDraw.h"
#include "MoonAtlasPreview.h"
#include "SunColourTemperature.h"
#include "SunReferenceDraw.h"
#include <imgui_internal.h>
#include <array>
#include <cstring>
#include <cstdio>
namespace Frontier {
namespace {
using namespace SunReference;
constexpr ImU32 Ink=IM_COL32(233,233,233,255),Muted=IM_COL32(145,145,145,255),Accent=IM_COL32(181,196,223,255);
EditorProperty* Find(EditorSheet& S,const char* Name){for(auto& G:S.Groups)for(unsigned I=0;I<G.PropertyCount;++I)if(std::strcmp(G.Properties[I].Label,Name)==0)return &G.Properties[I];return nullptr;}
enum class Mark {Moon,Sun,Orbit,Sparkles,Bake,Upload,Reset,Arrow};
void Icon(ImDrawList* D,ImVec2 At,Mark M,ImU32 Tint,float Size=16) {
    Canvas C{D,At,Size/24};
    auto Line=[&](float X,float Y,float XX,float YY){C.Line({X,Y},{XX,YY},Tint,1.5f);};
    if(M==Mark::Moon){C.Circle(12,12,8,IM_COL32(129,151,176,255));C.Circle(10,10,6.3f,IM_COL32(180,195,207,255));C.Circle(8,9,1.8f,IM_COL32(126,147,168,255));C.Circle(13,14,2.3f,IM_COL32(112,135,159,255));C.Circle(14,7,1,IM_COL32(143,162,183,255));}
    else if(M==Mark::Sun) {
        C.Circle(12,12,4.35f,(Tint&0x00ffffffu)|(33u<<24));
        C.D->AddCircle(C.P(12,12),4.35f*C.Scale,Tint,32,1.5f*C.Scale);
        for(int I=0;I<8;++I){float A=I*Pi/4;Line(12+7.8f*std::cos(A),12+7.8f*std::sin(A),12+10*std::cos(A),12+10*std::sin(A));}
    } else if(M==Mark::Orbit || M==Mark::Reset) {
        D->PathArcTo(C.P(12,12),10*C.Scale,.25f*Pi,1.85f*Pi,48);D->PathStroke(Tint,1.5f*C.Scale);
        if(M==Mark::Orbit){for(auto P:{ImVec2(19,5),ImVec2(5,19),ImVec2(12,12)})D->AddCircle(C.P(P.x,P.y),(P.x==12?3:2)*C.Scale,Tint,20,1.5f*C.Scale);}
        else {Line(2,3,2,9);Line(2,9,8,9);}
    } else if(M==Mark::Sparkles) {
        const ImVec2 P[]={{12,2},{14.1f,8.5f},{15.5f,9.9f},{22,12},{15.5f,14.1f},{14.1f,15.5f},{12,22},{9.9f,15.5f},{8.5f,14.1f},{2,12},{8.5f,9.9f},{9.9f,8.5f},{12,2}};
        for(unsigned I=1;I<13;++I)C.Line(P[I-1],P[I],Tint,1.5f);
        Line(20,3,20,7);Line(18,5,22,5);Line(4,17,4,19);Line(3,18,5,18);
    } else if(M==Mark::Bake) {
        Line(12,5,19,5);Line(19,5,21,7);Line(21,7,21,19);Line(21,19,19,21);Line(19,21,7,21);Line(7,21,5,19);Line(5,19,5,13);
        Line(2,9,13,9);Line(9,5,13,9);Line(13,9,9,13);Line(3,2,3,5);Line(1.5f,3.5f,4.5f,3.5f);
        D->AddRect(C.P(14,14),C.P(17,17),Tint,0,0,1.5f*C.Scale);
    } else if(M==Mark::Upload){Line(12,16,12,3);Line(7,8,12,3);Line(12,3,17,8);Line(3,16,3,21);Line(3,21,21,21);Line(21,21,21,16);}
    else {Line(9,5,16,12);Line(16,12,9,19);}
}

struct Panel {
    ControlPanel& Controls;EditorSheet& Sheet;ImDrawList* D;ImVec2 O;ImFont* Font;ImTextureRef Glyph;bool GlyphReady=false;
    ImVec2 At(float X,float Y){return {O.x+X,O.y+Y};}
    void Text(float X,float Y,const char* T,float Size=12,ImU32 C=Ink){D->AddText(Font,Size,At(X,Y),C,T);}
    void Wrap(float X,float Y,float W,const char* T){D->AddText(Font,11,At(X,Y),Muted,T,nullptr,W);}
    void Card(float X,float Y,float W,float H,const char* Title){
        int Start=D->VtxBuffer.Size;D->AddRectFilled(At(X,Y),At(X+W,Y+H),IM_COL32_WHITE,22);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(D,Start,D->VtxBuffer.Size,At(X,Y),At(X+W,Y+H),IM_COL32(37,37,37,255),IM_COL32(32,32,32,255));
        D->AddRect(At(X,Y),At(X+W,Y+H),IM_COL32(52,52,52,255),22);
        Mark M=std::strcmp(Title,"Moon rotation")==0?Mark::Orbit:std::strcmp(Title,"Lunar position")==0?Mark::Arrow:Mark::Moon;
        if(Title[0]){if(M==Mark::Moon){if(GlyphReady)D->AddImage(Glyph,At(X+21,Y+19),At(X+41,Y+39));}else Icon(D,At(X+23,Y+21),M,Accent,17);}
        Text(X+48,Y+23,Title);
    }
    bool Button(float X,float Y,float W,const char* ID,const char* Label,bool On=false,bool Disabled=false){
        ImGui::SetCursorScreenPos(At(X,Y));ImGui::BeginDisabled(Disabled);bool Hit=ImGui::InvisibleButton(ID,{W,32});ImGui::EndDisabled();
        D->AddRectFilled(At(X,Y),At(X+W,Y+32),On?IM_COL32(49,45,37,255):IM_COL32(40,40,40,255),12);
        D->AddRect(At(X,Y),At(X+W,Y+32),On?Accent:IM_COL32(62,62,62,255),12);
        float Width=Font->CalcTextSizeA(11,10000,0,Label).x;Text(X+(W-Width)/2,Y+10,Label,11,Disabled?IM_COL32(94,94,94,255):Ink);return Hit;
    }
    void Slider(float X,float Y,float W,const char* Label,bool Custom=false){
        auto* P=Find(Sheet,Label);if(!P)return;Text(X,Y,Label[0]=='M'&&Label[2]==' '?Label+3:Label,11,Muted);
        ImGui::SetCursorScreenPos(At(X,Y+22));ImGui::PushID(Label);ImGui::BeginChild(Label,{W,30},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        if(Controls.SliderPill("##value",&P->Figure,P->Minimum,P->Maximum,P->Decimals,P->Unit,false,false,true)){
            if(!std::isfinite(P->Figure))P->Figure=P->Minimum;
            P->Figure=std::clamp(P->Figure,P->Minimum,P->Maximum);
            if(P->Decimals==0)P->Figure=std::round(P->Figure);
            if(Custom)Find(Sheet,"Type")->Picked=4;
        }
        ImGui::EndChild();ImGui::PopID();
    }
    void Select(float X,float Y,float W,const char* Label){auto* P=Find(Sheet,Label);Text(X,Y,Label[0]=='M'&&Label[2]==' '?Label+3:Label,11,Muted);ImGui::SetCursorScreenPos(At(X,Y+22));ImGui::BeginChild(Label,{W,32},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);Controls.DropDown("##select",&P->Picked,P->Options,P->OptionCount);ImGui::EndChild();}
};
struct MoonCache {
 ImTextureData Disc,Thumbnails[6];float Phase=-1,Roll=-1,Pitch=-999,Bright=-1;int Slot=0,Body=-1;uint64_t Revision=0;bool Ready[6]{};
 MoonCache(){Disc.Create(ImTextureFormat_RGBA32,256,256);Disc.UseColors=true;std::memset(Disc.Pixels,0,256*256*4);ImGui::RegisterUserTexture(&Disc);for(auto& T:Thumbnails){T.Create(ImTextureFormat_RGBA32,96,96);T.UseColors=true;std::memset(T.Pixels,0,96*96*4);ImGui::RegisterUserTexture(&T);}}
};
void Cleanup(ImGuiContext*,ImGuiContextHook* H){auto* C=static_cast<MoonCache*>(H->UserData);ImGui::UnregisterUserTexture(&C->Disc);for(auto& T:C->Thumbnails)ImGui::UnregisterUserTexture(&T);delete C;}
MoonCache& Cache(){constexpr ImGuiID Owner=0x4d4f4f4e;for(auto& H:ImGui::GetCurrentContext()->Hooks)if(H.Owner==Owner)return *static_cast<MoonCache*>(H.UserData);auto* C=new MoonCache;ImGuiContextHook H;H.Owner=Owner;H.Type=ImGuiContextHookType_Shutdown;H.Callback=Cleanup;H.UserData=C;ImGui::AddContextHook(ImGui::GetCurrentContext(),&H);return *C;}
void UpdateAtlas(MoonCache& C,const EditorSheet& Sheet){if(C.Revision==Sheet.MoonAtlasRevision)return;for(int I=0;I<6;++I){auto& B=Sheet.MoonBodies[I];C.Ready[I]=B.Pixels&&B.Width&&B.Height;MoonAtlasPreview(C.Thumbnails[I].Pixels,96,{B.Pixels,B.Width,B.Height,4},B.Tint,B.Tilt,B.Haze,B.Gamma,.5f,0,0,1.6f);ImTextureDataQueueUpload(&C.Thumbnails[I],0,0,96,96);}C.Revision=Sheet.MoonAtlasRevision;C.Body=-1;}
EditorProperty& Property(EditorSheet& Sheet,int Slot,const char* Name){char Label[28];std::snprintf(Label,sizeof(Label),"M%d %s",Slot+1,Name);return *Find(Sheet,Label);}
void Slider(Panel& U,int Slot,const char* Name,float X,float Y,float W,bool Disabled=false){char Label[28];std::snprintf(Label,sizeof(Label),"M%d %s",Slot+1,Name);ImGui::BeginDisabled(Disabled);U.Slider(X,Y,W,Label);ImGui::EndDisabled();}
void Tile(Panel& U,float X,float Y,const char* Label,bool& On){ImGui::SetCursorScreenPos(U.At(X,Y));if(ImGui::InvisibleButton(Label,{106,106}))On=!On;ImU32 Key=On?IM_COL32(105,200,132,255):IM_COL32(204,118,115,255);U.D->AddRectFilled(U.At(X,Y),U.At(X+106,Y+106),IM_COL32(32,32,32,255),16);U.D->AddRect(U.At(X,Y),U.At(X+106,Y+106),On?IM_COL32(50,50,50,255):IM_COL32(73,50,50,255),16);U.D->AddCircleFilled(U.At(X+53,Y+29),15.5f,Key,32);Icon(U.D,U.At(X+44,Y+20),Mark::Orbit,IM_COL32(25,40,33,255),18);U.Text(X+(106-U.Font->CalcTextSizeA(11,10000,0,Label).x)/2,Y+57,Label,11);U.Text(X+45,Y+82,On?"ON":"OFF",8,Key);}
bool Drag(Canvas C,const char* ID,float& A,float& B,float AX,float AY,bool Disabled=false){ImGui::SetCursorScreenPos(C.Origin);ImGui::BeginDisabled(Disabled);ImGui::InvisibleButton(ID,{360*C.Scale,257*C.Scale},ImGuiButtonFlags_EnableNav);bool Changed=false;
 if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){A+=ImGui::GetIO().MouseDelta.x*AX;B-=ImGui::GetIO().MouseDelta.y*AY;Changed=true;}
 if(ImGui::IsItemFocused()&&!Disabled){float Step=ImGui::GetIO().KeyShift?10.f:1.f;for(auto K:{ImGuiKey_LeftArrow,ImGuiKey_RightArrow,ImGuiKey_UpArrow,ImGuiKey_DownArrow,ImGuiKey_Home})if(ImGui::IsKeyPressed(K)){if(K==ImGuiKey_Home){A=0;B=0;}else if(K==ImGuiKey_LeftArrow)A-=Step;else if(K==ImGuiKey_RightArrow)A+=Step;else if(K==ImGuiKey_UpArrow)B+=Step;else B-=Step;Changed=true;}}
 ImGui::EndDisabled();if(Changed){A=std::fmod(std::fmod(A,360.f)+360.f,360.f);B=std::clamp(B,ID[2]=='p'?-90.f:-180.f,ID[2]=='p'?90.f:180.f);}return Changed;
}
void Disc(Canvas C,MoonCache& Cache,float Size,float Roll,float Pitch,bool Comparison=false,bool Guide=false){float R=Comparison?20+54*std::sqrt(Size/(Size+12)):60;
 if(Comparison){for(int I=0;I<90;++I){float A=I*2*Pi/90;C.Line({128+64*std::cos(A),104+64*std::sin(A)},{128+64*std::cos(A+.02f),104+64*std::sin(A+.02f)},Colour(170,185,204,.133f),1);}}
 if(Cache.Body>=0&&Cache.Ready[Cache.Body])C.D->AddImage(Cache.Disc.GetTexRef(),C.P(128-R,104-R),C.P(128+R,104+R));else C.Text(128,104,"Texture unavailable",Colour(134,155,182),10);
 float A=Roll*Pi/180;auto Rot=[&](float X,float Y){return ImVec2(128+X*std::cos(A)-Y*std::sin(A),104+X*std::sin(A)+Y*std::cos(A));};C.Line(Rot(0,-R-4),Rot(0,-R-10),Colour(197,217,242));
 if(Guide){for(int I=1;I<=120;++I){float T=I*Pi/60,TT=(I-1)*Pi/60,P=Pitch*Pi/180;if(std::sin(T)*std::cos(P)>=0&&I%3!=0)C.Line(Rot(std::cos(TT)*R,std::sin(TT)*std::sin(P)*R),Rot(std::cos(T)*R,std::sin(T)*std::sin(P)*R),Colour(211,226,240,.35f),.7f);}}
 char Text[96];if(Comparison){C.Line({128-R,182},{128+R,182},Colour(125,149,181),.7f);C.Line({128-R,178},{128-R,185},Colour(125,149,181),.7f);C.Line({128+R,178},{128+R,185},Colour(125,149,181),.7f);std::snprintf(Text,sizeof(Text),Size>=10000?"%.3g° angular diameter":"%.2f° angular diameter",double(Size));}else std::snprintf(Text,sizeof(Text),"Roll %.0f° · Pitch %.0f°",double(Roll),double(Pitch));C.Text(128,202,Text,Colour(134,155,182),9);
}
}
void RecordMoonInspector(ControlPanel& Controls,EditorInstance&,EditorSheet& Sheet){
 if(!Find(Sheet,"M1 Phase")||!Find(Sheet,"M4 Pitch")){ImGui::TextUnformatted("Moon properties unavailable");return;}
 auto& Cached=Cache();UpdateAtlas(Cached,Sheet);auto* Font=ImGui::GetFont();for(auto* F:ImGui::GetIO().Fonts->Fonts)if(std::strcmp(F->GetDebugName(),"Sun reference / regular")==0)Font=F;ImGui::PushFont(Font,14);ImVec2 O=ImGui::GetCursorScreenPos();O.x+=20;O.y+=20;float W=std::max(240.f,ImGui::GetContentRegionAvail().x-40);Panel U{Controls,Sheet,ImGui::GetWindowDrawList(),O,Font,{},false};
 U.Text(0,8,"Inspector / Environment",8,Muted);U.Text(0,48,"Moon",25);U.Text(0,83,"Scene moons · choose a body for each",11,Muted);
 // Instance selection stays compact inside settings; there is no duplicate row above it.
 unsigned HeaderBody=std::min(Property(Sheet,Cached.Slot,"Preset").Picked,5u);U.Glyph=Cached.Thumbnails[HeaderBody].GetTexRef();U.GlyphReady=Cached.Ready[HeaderBody];U.Card(0,110,W,178,"Moon settings");
 ImGui::SetCursorScreenPos(U.At(W-(W>=450?224:124),126));ImGui::SetNextItemWidth(W>=450?200:100);
 auto& Current=Property(Sheet,Cached.Slot,"Preset");if(ImGui::BeginCombo("##moon-instance",Current.Options[std::min(Current.Picked,5u)])){for(int I=0;I<4;++I){ImGui::PushID(I);auto& P=Property(Sheet,I,"Preset");if(ImGui::Selectable(P.Options[std::min(P.Picked,5u)],I==Cached.Slot))Cached.Slot=I;ImGui::PopID();}ImGui::EndCombo();}
 if(ImGui::IsItemHovered())ImGui::SetTooltip("Active moon instance");

 int S=Cached.Slot;ImGui::PushID(S);auto& Visible=Property(Sheet,S,"Visible").On;auto& Follow=Property(Sheet,S,"Follow Sky").On;auto& Az=Property(Sheet,S,"Azimuth").Figure;auto& El=Property(Sheet,S,"Elevation").Figure;auto& Phase=Property(Sheet,S,"Phase").Figure;auto& Size=Property(Sheet,S,"Size").Figure;auto& Roll=Property(Sheet,S,"Roll").Figure;auto& Pitch=Property(Sheet,S,"Pitch").Figure;auto& Bright=Property(Sheet,S,"Bright").Figure;
 unsigned IconBody=std::min(Property(Sheet,S,"Preset").Picked,5u);U.Glyph=Cached.Thumbnails[IconBody].GetTexRef();U.GlyphReady=Cached.Ready[IconBody];
 bool Wide=W>=760;Tile(U,24,161,"Visible",Visible);Tile(U,142,161,"Follow sky",Follow);
 auto& Catalogue=Property(Sheet,S,"Preset");
 // A dedicated, single horizontal catalogue. The arrows browse the strip;
 // selecting either a sphere or its caption changes only this scene instance.
 U.Card(0,304,W,216,"Moon catalogue");
 const float StripWidth=W-128,CellWidth=112,Stride=124,ContentWidth=732;
 float Padding=std::max(0.f,(StripWidth-ContentWidth)/2);
 ImGui::SetCursorScreenPos(U.At(64,358));ImGui::SetNextWindowContentSize({ContentWidth+Padding*2,124});
 ImGui::BeginChild("##moon-catalogue-strip",{StripWidth,150},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
 auto* Strip=ImGui::GetCurrentWindow();
 if(ImGui::IsWindowAppearing())ImGui::SetScrollFromPosX(Strip,Padding+std::min(Catalogue.Picked,5u)*Stride+CellWidth/2,.5f);
 ImGui::SetCursorPosX(Padding);
 for(unsigned I=0;I<6;++I){ImGui::PushID(int(I));if(I)ImGui::SameLine(0,12);ImVec2 P=ImGui::GetCursorScreenPos();auto* D=ImGui::GetWindowDrawList();
  if(ImGui::InvisibleButton("##catalogue-body",{CellWidth,124},ImGuiButtonFlags_EnableNav))Catalogue.Picked=I;
  bool Selected=Catalogue.Picked==I;D->AddRectFilled(P,{P.x+CellWidth,P.y+124},Selected?IM_COL32(42,43,46,255):ImGui::IsItemHovered()?IM_COL32(38,38,38,255):IM_COL32(32,32,32,255),14);
  if(Selected)D->AddRect(P,{P.x+CellWidth,P.y+124},Accent,14);
  if(Cached.Ready[I])D->AddImage(Cached.Thumbnails[I].GetTexRef(),{P.x+12,P.y+8},{P.x+100,P.y+96});
  else D->AddText(Font,10,{P.x+12,P.y+43},Muted,"Unavailable");
  const char* Name=Catalogue.Options[I];D->AddText(Font,12,{P.x+(CellWidth-Font->CalcTextSizeA(12,10000,0,Name).x)/2,P.y+104},Selected?Ink:Muted,Name);ImGui::PopID();
 }
 ImGui::EndChild();
 if(U.Button(20,394,32,"##previous-bodies","<",false,Strip->Scroll.x<=0))ImGui::SetScrollX(Strip,std::max(0.f,Strip->Scroll.x-Stride));
 if(U.Button(W-52,394,32,"##next-bodies",">",false,Strip->Scroll.x>=Strip->ScrollMax.x))ImGui::SetScrollX(Strip,std::min(Strip->ScrollMax.x,Strip->Scroll.x+Stride));
 unsigned Body=std::min(Catalogue.Picked,5u);
 float EffectivePhase=Follow?Sheet.MoonPhase:Phase,EffectiveAz=Follow?Sheet.MoonAzimuth:Az,EffectiveEl=Follow?Sheet.MoonElevation:El;
 if(Cached.Body!=int(Body)||Cached.Phase!=EffectivePhase||Cached.Roll!=Roll||Cached.Pitch!=Pitch||Cached.Bright!=Bright){auto& B=Sheet.MoonBodies[Body];MoonAtlasPreview(Cached.Disc.Pixels,256,{B.Pixels,B.Width,B.Height,4},B.Tint,B.Tilt,B.Haze,B.Gamma,EffectivePhase,Roll,Pitch,Bright);Cached.Body=int(Body);Cached.Phase=EffectivePhase;Cached.Roll=Roll;Cached.Pitch=Pitch;Cached.Bright=Bright;ImTextureDataQueueUpload(&Cached.Disc,0,0,256,256);}

 float Lit=(1-std::cos(EffectivePhase*2*Pi))*.5f,Y=536;char Text[160];
 U.Card(0,Y,W,443,"Lunar phase");std::snprintf(Text,sizeof(Text),"%.1f days",double(EffectivePhase*29.53f));U.Text(24,Y+72,Text,30);std::snprintf(Text,sizeof(Text),"%.0f%% illuminated",double(Lit*100));U.Text(W>450?W-150:24,Y+(W>450?83:112),Text,11,Accent);U.Wrap(24,Y+136,W-48,Follow?"Solved sky phase · switch Follow sky off to edit":"Position in the 29.53-day synodic cycle");Disc(Fit(U.D,U.At(24,Y+162),{W-48,199},256,215),Cached,Size,Roll,Pitch);
 auto& PhaseProp=Property(Sheet,S,"Phase");float AuthoredPhase=Phase;PhaseProp.Figure=EffectivePhase*29.53f;PhaseProp.Maximum=29.53f;std::snprintf(PhaseProp.Unit,sizeof(PhaseProp.Unit),"days");Slider(U,S,"Phase",24,Y+373,W-48,Follow);Phase=Follow?AuthoredPhase:PhaseProp.Figure/29.53f;PhaseProp.Maximum=1;PhaseProp.Unit[0]=0;Y+=459;
 float CW=Wide?(W-16)/2:W,X2=Wide?CW+16:0,Y2=Wide?Y:Y+468;
 U.Card(0,Y,CW,452,"Moonlight");std::snprintf(Text,sizeof(Text),"%.2f ×",double(Bright));U.Text(24,Y+72,Text,30);U.Wrap(24,Y+118,CW-48,"Full-disc brightness multiplier · not calibrated lux");Canvas L{U.D,U.At(CW/2,Y+216),1};L.Glow(0,0,78,168,188,219,.075f);if(Cached.Ready[Body])U.D->AddImage(Cached.Disc.GetTexRef(),L.P(-43,-43),L.P(43,43));else U.Text(24,Y+216,"Texture unavailable",11,Muted);std::snprintf(Text,sizeof(Text),"Phase-weighted strength  %.3f ×",double(Bright*Lit));U.Text(24,Y+287,Text,11,Muted);Slider(U,S,"Bright",24,Y+319,CW-48);Slider(U,S,"Glow",24,Y+386,CW-48);
 U.Card(X2,Y2,CW,452,"Moon size");std::snprintf(Text,sizeof(Text),Size>=10000?"%.3g°":"%.2f°",double(Size));U.Text(X2+24,Y2+72,Text,30);U.Wrap(X2+24,Y2+118,CW-48,"Angular diameter · compressed preview for oversized moons");Disc(Fit(U.D,U.At(X2+24,Y2+148),{CW-48,175},256,215),Cached,Size,Roll,Pitch,true);
 U.Text(X2+24,Y2+328,"Type any larger value",11,Muted);ImGui::SetCursorScreenPos(U.At(X2+CW-126,Y2+322));ImGui::SetNextItemWidth(102);ImGui::PushStyleColor(ImGuiCol_FrameBg,IM_COL32(28,32,39,255));ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5);bool SizeChanged=ImGui::InputFloat("##moon-size",&Size,0,0,"%.2f");ImGui::PopStyleVar();ImGui::PopStyleColor();if(SizeChanged){if(!std::isfinite(Size))Size=.52f;Size=std::max(.1f,Size);}Property(Sheet,S,"Size").Maximum=std::max(180.f,Size);Slider(U,S,"Size",X2+24,Y2+356,CW-48);if(U.Button(X2+24,Y2+414,CW-48,"##natural-size","Natural size · 0.52°"))Size=.52f;
 Y=(Wide?Y:Y2)+468;Y2=Wide?Y:Y+500;
 U.Card(0,Y,CW,484,"Lunar position");U.Wrap(24,Y+63,CW-48,Follow?"Solved direction · Follow sky is ON":"Position in the sky · drag the spherical dial");auto SphereCanvas=Fit(U.D,U.At(24,Y+96),{CW-48,257},360,257);Drag(SphereCanvas,"##position",Az,El,.7f,.65f,Follow);MoonReference::Sphere(SphereCanvas,EffectiveAz,EffectiveEl,Roll,Size,Cached.Disc.GetTexRef(),Cached.Ready[Body]);
 float SavedAz=Az,SavedEl=El;if(Follow){Az=EffectiveAz;El=EffectiveEl;}Slider(U,S,"Azimuth",24,Y+362,CW-48,Follow);Slider(U,S,"Elevation",24,Y+426,CW-48,Follow);if(Follow){Az=SavedAz;El=SavedEl;}
 U.Card(X2,Y2,CW,532,"Moon rotation");U.Wrap(X2+24,Y2+63,CW-48,"Surface orientation · independent of sky position");auto DiscCanvas=Fit(U.D,U.At(X2+24,Y2+105),{CW-48,215},256,215);auto Hit=Fit(U.D,U.At(X2+24,Y2+90),{CW-48,257},360,257);Drag(Hit,"##rotation",Roll,Pitch,.8f,.8f);Disc(DiscCanvas,Cached,Size,Roll,Pitch,false,true);Slider(U,S,"Roll",X2+24,Y2+362,CW-48);Slider(U,S,"Pitch",X2+24,Y2+426,CW-48);
 if(U.Button(X2+24,Y2+488,CW-48,"##reset-orientation","Reset orientation")){Roll=Pitch=0;}
 Y=(Wide?Y:Y2)+552;U.Wrap(0,Y,W,"Real registered body textures in every moon preview. Display framing is compressed; atmosphere and scene occlusion are not included.");ImGui::SetCursorScreenPos(U.At(0,Y+60));ImGui::Dummy({W,1});ImGui::PopID();ImGui::PopFont();
}
}
