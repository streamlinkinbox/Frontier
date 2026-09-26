#include "SunInspectorPanel.h"
#include "ControlPanel.h"
#include "../DisplayPresentation/SunReferenceDraw.h"
#include "../DisplayPresentation/SunColourTemperature.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {
namespace {
using namespace SunReference;
constexpr ImU32 Ink=IM_COL32(240,240,240,255), Muted=IM_COL32(150,150,150,255);
ImFont* Face(const char* Name) noexcept {
    for(auto* F:ImGui::GetIO().Fonts->Fonts) if(std::strcmp(F->GetDebugName(),Name)==0)return F;
    return nullptr;
}
EditorProperty* Find(EditorSheet& Sheet,const char* Name) noexcept {
    for(unsigned G=0;G<Sheet.GroupCount;++G)for(unsigned P=0;P<Sheet.Groups[G].PropertyCount;++P)
        if(std::strcmp(Sheet.Groups[G].Properties[P].Label,Name)==0)return &Sheet.Groups[G].Properties[P];
    return nullptr;
}
enum class Mark {Sun,Orbit,Sparkles,Bake,Upload,Reset,Arrow};
void Icon(ImDrawList* D,ImVec2 At,Mark M,ImU32 Tint,float Size=16) {
    Canvas C{D,At,Size/24};
    auto Line=[&](float X,float Y,float XX,float YY){C.Line({X,Y},{XX,YY},Tint,1.5f);};
    if(M==Mark::Sun) {
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
    ControlPanel& Controls;ImDrawList* D;ImVec2 Origin;ImFont* Body;ImFont* Light;float Width;
    ImVec2 At(float X,float Y)const{return {Origin.x+X,Origin.y+Y};}
    void Text(float X,float Y,const char* T,float Size=12,ImU32 C=Ink,bool Metric=false)const {
        D->AddText(Metric?Light:Body,Size,At(X,Y),C,T);
    }
    void Centre(float X,float Y,float W,const char* T,float Size=10,ImU32 C=Muted)const {
        Text(X+(W-Body->CalcTextSizeA(Size,10000,0,T).x)/2,Y,T,Size,C);
    }
    void Wrap(float X,float Y,float W,const char* T,float Size=10,ImU32 C=Muted)const {
        D->AddText(Body,Size,At(X,Y),C,T,nullptr,W);
    }
    void Rule(float X,float Y,float W)const {D->AddLine(At(X,Y),At(X+W,Y),Colour(255,255,255,12/255.f));}
    void Surface(float X,float Y,float W,float H,float Round=22)const {
        int First=D->VtxBuffer.Size;D->AddRectFilled(At(X,Y),At(X+W,Y+H),Colour(255,255,255),Round);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(D,First,D->VtxBuffer.Size,At(X,Y),At(X+W,Y+H),Colour(37,37,37),Colour(32,32,32));
        D->AddRect(At(X,Y),At(X+W,Y+H),Colour(52,52,52),Round);
    }
    void Card(float X,float Y,float W,float H,const char* Title,Mark M,ImU32 Accent)const {
        Surface(X,Y,W,H);Icon(D,At(X+24,Y+23),M,Accent);Text(X+50,Y+23,Title,12,Colour(202,202,202));
    }
    bool Button(float X,float Y,float W,float H,const char* Id,const char* Label,bool Enabled,Mark M,const char* Why=nullptr)const {
        ImGui::SetCursorScreenPos(At(X,Y));ImGui::BeginDisabled(!Enabled);
        bool Click=ImGui::InvisibleButton(Id,{W,H});ImGui::EndDisabled();
        bool Hot=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        D->AddRectFilled(At(X,Y),At(X+W,Y+H),Colour(Hot&&Enabled?51:40,Hot&&Enabled?51:40,Hot&&Enabled?51:40),8);
        D->AddRect(At(X,Y),At(X+W,Y+H),Colour(65,65,65),8);
        ImU32 C=Enabled?Colour(204,204,204):Colour(112,112,112);
        Icon(D,At(X+10,Y+(H-14)/2),M,C,14);Text(X+30,Y+(H-12)/2,Label,11,C);
        if(Hot&&Why)ImGui::SetTooltip("%s",Why);
        return Click;
    }
    bool Native(float X,float Y,float W,const char* Id,EditorProperty& P,bool Disabled=false)const {
        ImGui::SetCursorScreenPos(At(X,Y));
        ImGui::BeginChild(Id,{W,30},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::BeginDisabled(Disabled);bool Changed=Controls.SliderPill("##slider",&P.Figure,P.Minimum,P.Maximum,P.Decimals,P.Unit,false,false,true);ImGui::EndDisabled();
        ImGui::EndChild();
        return Changed;
    }
    void SolvedSlider(float X,float Y,float W,const char* Id,float V,float Lo,float Hi)const {
        // Native SliderPill, disabled: this is a solver result, never a manual-direction write-back.
        EditorProperty P;P.Figure=V;P.Minimum=Lo;P.Maximum=Hi;P.Decimals=1;std::snprintf(P.Unit,sizeof(P.Unit),"deg");
        Native(X,Y,W,Id,P,true);
    }
    void Unbound(float X,float Y,float W,const char* Why)const {
        D->AddRectFilled(At(X,Y+12),At(X+W,Y+15),Colour(66,66,66),2);
        Text(X,Y+25,"Not available in current properties",10,Muted);
        if(ImGui::IsMouseHoveringRect(At(X,Y),At(X+W,Y+43)))ImGui::SetTooltip("%s",Why);
    }
    void Quick(float X,float Y,float W,const char* Id,const char* Label,Mark M,bool* On,const char* Why)const {
        ImGui::SetCursorScreenPos(At(X,Y));ImGui::BeginDisabled(On==nullptr);
        if(ImGui::InvisibleButton(Id,{W,106})&&On)*On=!*On;
        ImGui::EndDisabled();
        bool Lit=On&&*On;ImU32 ColourKey=On?(Lit?Colour(105,200,132):Colour(204,118,115)):Colour(106,106,106);
        D->AddRectFilled(At(X,Y),At(X+W,Y+106),Colour(32,32,32),16);
        D->AddRect(At(X,Y),At(X+W,Y+106),On&&!Lit?Colour(73,50,50):Colour(50,50,50),16);
        D->AddCircleFilled(At(X+W/2,Y+29),15.5f,ColourKey,32);
        Icon(D,At(X+W/2-9,Y+20),M,Lit?Colour(22,50,30):Colour(55,29,28),18);
        Centre(X,Y+57,W,Label,10,Colour(200,200,200));Centre(X,Y+80,W,On?(Lit?"ON":"OFF"):"UNAVAILABLE",8,ColourKey);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",Why);
    }
    void Bake(float X,float Y,float W,const char* Title,const char* Id)const {
        ImGui::PushID(Id);Card(X,Y,W,225,Title,Mark::Bake,Colour(172,172,172));
        Text(X+W-123,Y+26,"Procedural source",9,Colour(142,142,142));
        // Both source targets are drawn. Neither is aliased to the existing Sky-dome bake.
        Quick(X+24,Y+59,119,"##source","Use baked image",Mark::Bake,nullptr,"No native image binding exists for this Sun target.");
        float AX=X+159,AW=W-183;
        Button(AX,Y+59,AW,32,"##bake","Bake",false,Mark::Bake,"Sun-specific renderer bake is not exposed. No fake request or completed result is produced.");
        Button(AX,Y+100,AW,32,"##load","Load image",false,Mark::Upload,"Native image import/binding is not implemented for this target.");
        Wrap(AX,Y+143,AW,"No image source connected.",9,Colour(133,133,133));
        Rule(X+24,Y+181,W-48);Wrap(X+24,Y+192,W-48,"Renderer / imported-image binding unavailable",9,Colour(144,139,130));
        ImGui::PopID();
    }
};
void Extra(ControlPanel& C,EditorProperty& P) {
    ImGui::PushID(P.Label);ImGui::TextUnformatted(P.Label);
    switch(P.Category){
        case EditorPropertyCategory::Slider:C.SliderPill("##native",&P.Figure,P.Minimum,P.Maximum,P.Decimals,P.Unit,P.Hi,false,true);break;
        case EditorPropertyCategory::Colour:C.ColourChip("##native",P.ColourTint);break;
        case EditorPropertyCategory::Switch:C.Switch("##native",&P.On);break;
        case EditorPropertyCategory::Select:C.DropDown("##native",&P.Picked,P.Options,P.OptionCount);break;
        case EditorPropertyCategory::Readout:C.Readout(P.Text);break;
        default:break;
    }
    ImGui::Dummy({0,12});ImGui::PopID();
}
}
void PrepareSunInspectorFonts() noexcept {
    if(!ImGui::GetCurrentContext())return;
    // Preserve ImGui's original default face when this is the first startup font registration.
    if(ImGui::GetIO().Fonts->Fonts.empty())ImGui::GetIO().Fonts->AddFontDefault();
    const char* Names[]={"Sun reference / regular","Sun reference / light"};
    const char* Files[]={"EngineContent/Fonts/SunReference/DMSans-Regular.ttf","EngineContent/Fonts/SunReference/DMSans-Light.ttf"};
    for(int I=0;I<2;++I)if(!Face(Names[I])) {
        auto* File=std::fopen(Files[I],"rb");if(!File)continue;std::fclose(File);
        ImFontConfig Config;std::snprintf(Config.Name,sizeof(Config.Name),"%s",Names[I]);
        ImGui::GetIO().Fonts->AddFontFromFileTTF(Files[I],I?40.0f:14.0f,&Config);
    }
}
void RecordSunInspector(ControlPanel& Controls,EditorInstance& Row,EditorSheet& Sheet) noexcept {
    using namespace SunReference;
    auto* Time=Find(Sheet,"Local Hours");auto* Size=Find(Sheet,"Angular Diameter");auto* Animate=Find(Sheet,"Animate");
    auto* Az=Find(Sheet,"Azimuth");auto* El=Find(Sheet,"Elevation");
    auto* Duration=Find(Sheet,"Day duration");auto* Speed=Find(Sheet,"Speed");
    auto* Intensity=Find(Sheet,"Intensity");auto* Temperature=Find(Sheet,"Temperature");
    auto* ColourSource=Find(Sheet,"Colour source");auto* RgbTint=Find(Sheet,"Sun Tint");
    if(!Duration||!Speed||!Time||!Size||!Animate||!Az||!El||!Intensity||!Temperature||!ColourSource||!RgbTint){ImGui::TextUnformatted("Sun property sheet unavailable");return;}
    ImFont* Body=Face("Sun reference / regular"),*Light=Face("Sun reference / light");
    if(!Body)Body=Controls.QueryUi();
    if(!Light)Light=Body;
    ImGui::PushFont(Body,14);
    const float Available=ImGui::GetContentRegionAvail().x;
    float W=std::max(280.0f,std::min(Available-40,1080.0f));
    ImVec2 Origin=ImGui::GetCursorScreenPos();Origin.x+=std::max(0.0f,(Available-W)/2);Origin.y+=20;
    Panel U{Controls,ImGui::GetWindowDrawList(),Origin,Body,Light,W};
    const bool Two=W>=680;const float Gap=16,L=Two?(W-Gap)*1.1f/2.1f:W,R=Two?W-L-Gap:W;
    U.Text(0,0,"Inspector  /  Environment",11,Muted);
    U.Text(0,43,"DIRECTIONAL LIGHT",10,Colour(153,153,153));U.Text(0,64,Row.Label,38,Ink,true);
    U.Button(W-197,62,84,32,"##sun-reset","Reset",false,Mark::Reset,"Project-defined reset is not exposed by the current property protocol.");
    ImGui::SetCursorScreenPos(U.At(W-98,62));if(ImGui::InvisibleButton("##sun-enabled",{98,32}))Row.Visible=!Row.Visible;
    U.D->AddRectFilled(U.At(W-98,62),U.At(W,94),Colour(38,38,38),16);U.D->AddRect(U.At(W-98,62),U.At(W,94),Colour(59,59,59),16);
    U.D->AddCircleFilled(U.At(W-82,78),3,Row.Visible?Colour(150,207,161):Colour(198,115,112));U.Text(W-70,71,Row.Visible?"Enabled":"Disabled",11);
    // The requested quick bake area is at the top, with distinct lighting and disk targets.
    U.Text(0,129,"BAKING",10,Muted);U.Text(W-190,129,"Native target bindings pending",9,Colour(129,126,121));
    float BakeW=Two?(W-12)/2:W;
    U.Bake(0,153,BakeW,"Sun lighting bake","sun-lighting-bake");
    U.Bake(Two?BakeW+12:0,Two?153:390,BakeW,"Sun disk bake","sun-disk-bake");
    float Y=Two?404:641;
    U.Text(0,Y,"PROPERTIES",10,Muted);U.Text(125,Y,"Light, direction & atmosphere",10,Colour(133,133,133));Y+=32;
    const bool WrapTiles=W<600;
    float Tile=std::min(160.0f,(W-(WrapTiles?9:27))/(WrapTiles?2:4));
    U.Quick(0,Y,Tile,"##sunlight-quick","Sunlight",Mark::Sun,nullptr,"An independent sunlight enable flag is not in the native sheet. Use the Sunlight intensity card and native Direct gain.");
    U.Quick(Tile+9,Y,Tile,"##disc-quick","Sun disc",Mark::Sun,nullptr,"An independent solar-disc enable flag is not in the native sheet.");
    U.Quick(WrapTiles?0:(Tile+9)*2,Y+(WrapTiles?126:0),Tile,"##cycle-quick","Day cycle",Mark::Orbit,&Animate->On,"Native Clock.Animate. Turns time advancement on or off; does not invent a separate simulation flag.");
    U.Quick(WrapTiles?Tile+9:(Tile+9)*3,Y+(WrapTiles?126:0),Tile,"##motion-quick",Animate->On?"Dynamic":"Static",Mark::Orbit,&Animate->On,"Dynamic advances the shared clock. Static holds position. Day cycle and this tile control the same clock.");
    Y+=WrapTiles?252:126;
    U.Surface(0,Y,W,48,12);Icon(U.D,U.At(16,Y+15),Mark::Sparkles,Colour(226,181,138),18);
    U.Text(47,Y+8,"Lens Flare",12);U.Text(47,Y+27,"Optical effect subcomponent",9,Muted);Icon(U.D,U.At(W-30,Y+16),Mark::Arrow,Colour(143,143,143),16);
    ImGui::SetCursorScreenPos(U.At(0,Y));ImGui::BeginDisabled();ImGui::InvisibleButton("##sun-flare-link",{W,48});ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Select Lens Flare in the native outliner; child navigation is not exposed through this sheet.");
    Y+=64;
    // HTML's two-column grid: direction spans illuminance and temperature; then cycle / disc.
    const bool StackDirection=L<370;
    const float DirH=StackDirection?711:594,SmallH=(DirH-Gap)/2,BottomH=329;
    U.Card(0,Y,L,DirH,"Sun direction",Mark::Orbit,Colour(232,182,95));
    char Buffer[64];std::snprintf(Buffer,sizeof(Buffer),"%.1f°",double(El->Figure));U.Text(24,Y+57,Buffer,Two?54:46,Ink,true);
    U.Text(L-105,Y+70,El->Figure<0?"Below the\nhorizon":"Above the\nhorizon",10,Muted);
    Orbit(Fit(U.D,U.At(16,Y+123),{L-32,250},360,280),Az->Figure,El->Figure);
    U.Native(24,Y+385,L-48,"##sun-direction-time",*Time);
    U.Rule(24,Y+423,L-48);float Half=(L-66)/2;
    for(int I=0;I<2;++I){float X=StackDirection?24:24+I*(Half+18);float V=I?El->Figure:Az->Figure;
        float FieldY=Y+(StackDirection?I*117:0),FieldW=StackDirection?L-48:Half;
        U.D->AddRectFilled(U.At(X,FieldY+442),U.At(X+FieldW,FieldY+551),Colour(25,25,25,180/255.f),12);U.D->AddRect(U.At(X,FieldY+442),U.At(X+FieldW,FieldY+551),Colour(51,51,51),12);
        U.Text(X+12,FieldY+453,I?"Elevation":"Azimuth",10,Colour(161,161,161));std::snprintf(Buffer,sizeof(Buffer),"%.1f°",double(V));U.Text(X+12,FieldY+473,Buffer,24,Ink,true);
        U.SolvedSlider(X+8,FieldY+510,FieldW-16,I?"##sun-elevation":"##sun-azimuth",V,I?-90:0,I?90:360);
    }
    U.Text(24,Y+DirH-27,"Shared time above · angles solved by date / location",10,Muted);
    float IX=Two?L+Gap:0,IY=Two?Y:Y+DirH+Gap;
    U.Card(IX,IY,R,SmallH,"Sunlight intensity",Mark::Sun,Colour(232,199,106));
    std::snprintf(Buffer,sizeof(Buffer),"%.1f",double(Intensity->Figure));U.Text(IX+24,IY+58,Buffer,46,Ink,true);
    U.Text(IX+31+Light->CalcTextSizeA(46,10000,0,Buffer).x,IY+84,"×",17,Muted);
    U.Text(IX+24,IY+116,"Sunlight intensity multiplier",11,Muted);
    // Keep the reference illustration, normalized to the native 0–60 gain range, not klux.
    const float Fraction=std::isfinite(Intensity->Figure)?std::clamp(Intensity->Figure/Intensity->Maximum,0.0f,1.0f):0;
    Illuminance(Fit(U.D,U.At(IX+24,IY+137),{R-48,80},360,100),Fraction*150);
    U.Native(IX+24,IY+223,R-48,"##sun-intensity",*Intensity);
    U.Text(IX+24,IY+267,"0 ×",10,Muted);U.Text(IX+R-50,IY+267,"60 ×",10,Muted);
    float TY=IY+SmallH+Gap;
    U.Card(IX,TY,R,SmallH,"Temperature",Mark::Sparkles,Colour(230,156,121));
    if(ColourSource->Picked==1){
        std::snprintf(Buffer,sizeof(Buffer),"%.0f",double(Temperature->Figure));U.Text(IX+24,TY+58,Buffer,46,Ink,true);
        U.Text(IX+31+Light->CalcTextSizeA(46,10000,0,Buffer).x,TY+84,"K",17,Muted);
    } else U.Text(IX+24,TY+58,"RGB",46,Ink,true);
    ImGui::SetCursorScreenPos(U.At(IX+24,TY+109));
    ImGui::BeginChild("##sun-colour-source",{R-48,32},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    Controls.DropDown("##source",&ColourSource->Picked,ColourSource->Options,ColourSource->OptionCount);ImGui::EndChild();
    const ImU32 Stops[]={Colour(185,118,78),Colour(217,183,144),Colour(224,219,205),Colour(146,178,208),Colour(119,145,184)};const float Off[]={0,.35f,.58f,.85f,1};
    // Four contiguous gradient segments; only the outer ends are rounded.
    // Avoid antialiased internal edges, which would create seams between segments.
    const ImDrawListFlags SavedDrawFlags=U.D->Flags;U.D->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for(int I=0;I<4;++I){
        ImVec2 A=U.At(IX+24+(R-48)*Off[I],TY+148),B=U.At(IX+24+(R-48)*Off[I+1],TY+164);
        int First=U.D->VtxBuffer.Size;
        U.D->AddRectFilled(A,B,IM_COL32_WHITE,8,I==0?ImDrawFlags_RoundCornersLeft:I==3?ImDrawFlags_RoundCornersRight:ImDrawFlags_RoundCornersNone);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(U.D,First,U.D->VtxBuffer.Size,A,{B.x,A.y},Stops[I],Stops[I+1]);
    }
    U.D->Flags=SavedDrawFlags;
    U.Text(IX+24,TY+168,"Warm",10,Muted);U.Text(IX+R-47,TY+168,"Cool",10,Muted);
    if(U.Native(IX+24,TY+186,R-48,"##sun-temperature",*Temperature)){
        Temperature->Figure=SunColourTemperature::Clamp(Temperature->Figure);ColourSource->Picked=1;
    }
    U.Rule(IX+24,TY+234,R-48);
    const bool KelvinActive=ColourSource->Picked==1;
    const auto Tint=KelvinActive?SunColourTemperature::LinearRgb(Temperature->Figure):std::array<float,3>{RgbTint->ColourTint[0],RgbTint->ColourTint[1],RgbTint->ColourTint[2]};
    U.D->AddRectFilled(U.At(IX+24,TY+246),U.At(IX+36,TY+258),ImGui::ColorConvertFloat4ToU32({SunColourTemperature::DisplayChannel(Tint[0]),SunColourTemperature::DisplayChannel(Tint[1]),SunColourTemperature::DisplayChannel(Tint[2]),1}),3);
    U.Text(IX+44,TY+244, KelvinActive?(Temperature->Figure<4500?"Golden warmth":Temperature->Figure>7000?"Cool daylight":"Natural daylight"):"Manual RGB tint active",11,Colour(170,170,170));
    U.Text(IX+24,TY+267,KelvinActive?"Blackbody tint approximation · linear RGB":"Move Kelvin slider to use temperature",9,Muted);
    float CY=Two?Y+DirH+Gap:TY+SmallH+Gap;
    U.Card(0,CY,L,BottomH,"Daylight cycle",Mark::Sun,Colour(212,185,112));
    int Hour=int(Time->Figure),Minute=int(std::round((Time->Figure-Hour)*60));if(Minute==60){Minute=0;Hour=(Hour+1)%24;}
    std::snprintf(Buffer,sizeof(Buffer),"%02d:%02d",Hour,Minute);U.Text(24,CY+61,Buffer,40,Ink,true);
    const char* Period=Time->Figure<6||Time->Figure>=18?"Night":Time->Figure<12?"Morning":Time->Figure<17?"Afternoon":"Evening";
    U.D->AddRectFilled(U.At(L-109,CY+68),U.At(L-24,CY+94),Colour(44,44,44),13);U.Centre(L-109,CY+75,85,Period,10,Colour(188,188,188));
    Day(Fit(U.D,U.At(24,CY+121),{L-48,105},500,123),Time->Figure);
    U.Native(24,CY+241,L-48,"##sun-time",*Time);
    const char* Times[]={"00:00","06:00","12:00","18:00","24:00"};for(int I=0;I<5;++I)U.Text(24+I*(L-78)/4,CY+287,Times[I],10,Muted);
    float DX=Two?L+Gap:0,DY=Two?CY:CY+BottomH+Gap;
    U.Card(DX,DY,R,BottomH,"Sun disc",Mark::Sun,Colour(240,189,114));
    std::snprintf(Buffer,sizeof(Buffer),"%.2f°",double(Size->Figure));U.Text(DX+24,DY+93,Buffer,R<380?34:43,Ink,true);U.Text(DX+24,DY+146,"Angular diameter",11,Muted);
    const float DiscSide=R<380?90:126;Canvas Disc{U.D,U.At(DX+R-24-DiscSide,DY+68),DiscSide/126};
    Disc.Glow(63,63,63,201,165,103,15/255.f);Disc.Line({0,63},{126,63},Colour(255,255,255,18/255.f));Disc.Line({63,0},{63,126},Colour(255,255,255,18/255.f));
    std::array<ImVec2,97> Rim;for(int I=0;I<=96;++I)Rim[I]={63+55*std::cos(I*2*Pi/96),63+55*std::sin(I*2*Pi/96)};
    Disc.Stroke(Rim,Colour(255,255,255,21/255.f),1,3,3);
    const float Radius=(18+Size->Figure*30)/2;
    U.D->PushClipRect(U.At(DX+R-24-DiscSide,DY+57),U.At(DX+R-24,DY+204),true);
    Disc.Glow(63,63,Radius+32,224,183,117,32/255.f);Disc.Glow(63,63,Radius+12,245,219,152,85/255.f);Disc.Circle(63,63,Radius,Colour(236,219,184));U.D->PopClipRect();
    U.Native(DX+24,DY+217,R-48,"##sun-diameter",*Size);U.Text(DX+24,DY+262,"Pinpoint",10,Muted);U.Text(DX+R-84,DY+262,"Broad disc",10,Muted);
    U.Rule(DX+24,DY+287,R-48);U.D->AddCircleFilled(U.At(DX+28,DY+306),3.5f,Colour(212,189,146));U.Text(DX+40,DY+301,"Apparent size of the sun in the sky",10,Muted);
    float End=DY+BottomH+24;
    U.Card(0,End,W,204,"Dynamic settings",Mark::Orbit,Colour(212,185,112));
    U.Text(24,End+53,Animate->On?"Dynamic · clock advancing":"Static · position held",18,Ink);
    U.Text(24,End+86,"Full day duration · real hours per complete 24-hour solar cycle",10,Muted);
    U.Native(24,End+111,W-48,"##sun-day-duration",*Duration);
    U.Wrap(24,End+157,W-48,"6 h = one full cycle in six real hours. Both time sliders position the same Sun; static stops automatic movement.",11,Muted);
    End+=228;
    ImGui::SetCursorScreenPos(U.At(0,End));
    // Authoring-only supplement. Solver inputs remain intact in the project sheet.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{24,20});
    ImGui::PushStyleColor(ImGuiCol_ChildBg,IM_COL32(0,0,0,0));
    ImGui::BeginChild("##sun-native-extra",{W,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const ImGuiID OpenId=ImGui::GetID("##additional-sun-open");
    bool Expanded=ImGui::GetStateStorage()->GetBool(OpenId,false);
    ImVec2 Header=ImGui::GetCursorScreenPos();
    if(ImGui::InvisibleButton("##additional-sun-toggle",{ImGui::GetContentRegionAvail().x,28})){
        Expanded=!Expanded;ImGui::GetStateStorage()->SetBool(OpenId,Expanded);
    }
    auto* CardDraw=ImGui::GetWindowDrawList();
    CardDraw->AddText(Body,12,{Header.x,Header.y+7},Ink,"Additional Sun settings");
    const float ArrowX=Header.x+ImGui::GetContentRegionAvail().x-12;
    if(Expanded)CardDraw->AddTriangleFilled({ArrowX-4,Header.y+10},{ArrowX+4,Header.y+10},{ArrowX,Header.y+15},Muted);
    else CardDraw->AddTriangleFilled({ArrowX-2,Header.y+8},{ArrowX-2,Header.y+16},{ArrowX+3,Header.y+12},Muted);
    if(Expanded) {
        ImGui::TextWrapped("Direct sunlight, manual tint and seasonal date. Editing tint switches to RGB mode.");
        ImGui::Dummy({0,10});
        for(const char* Label:{"Direct","Sun Tint","Day of Month","Month"})
            if(auto* P=Find(Sheet,Label))Extra(Controls,*P);
    }
    ImGui::EndChild();
    const float CardHeight=ImGui::GetItemRectSize().y;
    ImGui::PopStyleColor();ImGui::PopStyleVar();
    // Parent draw list is submitted before its child: same gradient/radius as every other card.
    U.Surface(0,End,W,CardHeight,22);
    ImGui::SetCursorScreenPos({Origin.x,ImGui::GetCursorScreenPos().y+12});ImGui::TextDisabled("Bound controls apply in real time. Unavailable controls are disabled.");
    ImGui::Dummy({0,16});ImGui::PopFont();
}
}
