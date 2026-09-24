#include "LensFlareInspectorPanel.h"
#include "ControlPanel.h"
#include "AtmosphericOptics.h"
#include "SunColourTemperature.h"
#include "SunReferenceDraw.h"
#include <imgui_internal.h>
#include <array>
#include <vector>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <bit>

namespace Frontier {
namespace {
constexpr ImU32 Ink=IM_COL32(233,233,233,255),Muted=IM_COL32(145,145,145,255),Accent=IM_COL32(214,184,128,255);
using namespace SunReference;
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

constexpr ImGuiID CacheOwner=0x4c465052;
EditorProperty* Find(EditorSheet& S,const char* Name){for(auto& G:S.Groups)for(unsigned I=0;I<G.PropertyCount;++I)if(std::strcmp(G.Properties[I].Label,Name)==0)return &G.Properties[I];return nullptr;}
struct Cache {
    ImTextureData Texture;
    std::vector<float> Linear;
    std::array<float,32> Key{};
    uint64_t Revision=0,Baked=0;
    bool ExportFailed=false;
    Cache(){Texture.Create(ImTextureFormat_RGBA32,640,280);Texture.UseColors=true;ImGui::RegisterUserTexture(&Texture);Linear.resize(640*280*3);}
};
Cache* Existing(){if(!ImGui::GetCurrentContext())return nullptr;for(auto& H:ImGui::GetCurrentContext()->Hooks)if(H.Owner==CacheOwner)return static_cast<Cache*>(H.UserData);return nullptr;}
void Cleanup(ImGuiContext*,ImGuiContextHook* Hook){auto* C=static_cast<Cache*>(Hook->UserData);ImGui::UnregisterUserTexture(&C->Texture);delete C;}
Cache& Resource(){if(auto* C=Existing())return *C;auto* C=new Cache;ImGuiContextHook H;H.Type=ImGuiContextHookType_Shutdown;H.Owner=CacheOwner;H.UserData=C;H.Callback=Cleanup;ImGui::AddContextHook(ImGui::GetCurrentContext(),&H);return *C;}
void Update(Cache& C,EditorSheet& S){
    const char* Labels[]={"Enabled","Type","Intensity","Ghosts","Halo Radius","Chromatic","Aperture Blades","Streak gain","Anamorphic","Streaks","Starburst","Spread","Rotation","Ray pairs","Ghost brightness","Ghost spacing","Halo brightness","Halo width","Ghost shape","Preview X","Preview Y"};
    std::array<float,32> K{};unsigned N=0;
    for(auto* Label:Labels){auto* P=Find(S,Label);if(!P)return;K[N++]=P->Category==EditorPropertyCategory::Switch?float(P->On):P->Category==EditorPropertyCategory::Select?float(P->Picked):P->Figure;}
    if(C.Revision&&K==C.Key)return;
    auto V=[&](const char* L){return Find(S,L)->Figure;};
    AtmosphericOptics::LensFlareSettings F;
    F.Enabled=Find(S,"Enabled")->On;F.CustomMix=Find(S,"Type")->Picked==4;F.Category=static_cast<AtmosphericOptics::LensFlareCategory>(Find(S,"Type")->Picked);
    F.Intensity=V("Intensity");F.GhostCount=uint32_t(V("Ghosts"));F.HaloRadius=V("Halo Radius");F.Chromatic=V("Chromatic");F.ApertureBlades=uint32_t(V("Aperture Blades"));F.StreakGain=V("Streak gain");
    auto& P=F.Layers;P.Anamorphic=float(Find(S,"Anamorphic")->On);P.Streaks=float(Find(S,"Streaks")->On);P.Burst=float(Find(S,"Starburst")->On);
    P.Spread=V("Spread");P.Rotation=V("Rotation");P.RayPairs=V("Ray pairs");P.GhostGain=V("Ghost brightness");P.GhostSpacing=V("Ghost spacing");P.HaloGain=V("Halo brightness");P.HaloWidth=V("Halo width");
    auto Shape=Find(S,"Ghost shape")->Picked;P.GhostSides=Shape==1?6:Shape==2?8:0;
    float Sun[]={V("Preview X"),V("Preview Y")};
    for(int Y=0;Y<280;++Y)for(int X=0;X<640;++X){
        float UV[]={(X+.5f)/640,(Y+.5f)/280};float* RGB=&C.Linear[(Y*640+X)*3];
        AtmosphericOptics::LensFlare(F,UV,Sun,1,640.f/280,RGB);
        unsigned char* Out=C.Texture.Pixels+(Y*640+X)*4;
        for(int I=0;I<3;++I){float L=std::max(RGB[I],0.f);Out[I]=static_cast<unsigned char>(255*SunColourTemperature::DisplayChannel(L/(1+L))+.5f);}Out[3]=255;
    }
    C.Key=K;++C.Revision;ImTextureDataQueueUpload(&C.Texture,0,0,640,280);
}
struct Panel {
    ControlPanel& Controls;EditorSheet& Sheet;ImDrawList* D;ImVec2 O;ImFont* Font;
    ImVec2 At(float X,float Y){return {O.x+X,O.y+Y};}
    void Text(float X,float Y,const char* T,float Size=12,ImU32 C=Ink){D->AddText(Font,Size,At(X,Y),C,T);}
    void Wrap(float X,float Y,float W,const char* T){D->AddText(Font,11,At(X,Y),Muted,T,nullptr,W);}
    void Card(float X,float Y,float W,float H,const char* Title){
        int Start=D->VtxBuffer.Size;D->AddRectFilled(At(X,Y),At(X+W,Y+H),IM_COL32_WHITE,22);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(D,Start,D->VtxBuffer.Size,At(X,Y),At(X+W,Y+H),IM_COL32(37,37,37,255),IM_COL32(32,32,32,255));
        D->AddRect(At(X,Y),At(X+W,Y+H),IM_COL32(52,52,52,255),22);
        Mark M=std::strcmp(Title,"Flare composite")==0?Mark::Sun:std::strcmp(Title,"Flare layers")==0?Mark::Sparkles:std::strcmp(Title,"Lens flare image")==0?Mark::Bake:Mark::Orbit;
        Icon(D,At(X+23,Y+21),M,Accent,17);
        Text(X+48,Y+23,Title);
    }
    bool Button(float X,float Y,float W,const char* ID,const char* Label,bool On=false,bool Disabled=false){
        ImGui::SetCursorScreenPos(At(X,Y));ImGui::BeginDisabled(Disabled);bool Hit=ImGui::InvisibleButton(ID,{W,32});ImGui::EndDisabled();
        D->AddRectFilled(At(X,Y),At(X+W,Y+32),On?IM_COL32(49,45,37,255):IM_COL32(40,40,40,255),12);
        D->AddRect(At(X,Y),At(X+W,Y+32),On?Accent:IM_COL32(62,62,62,255),12);
        float Width=Font->CalcTextSizeA(11,10000,0,Label).x;Text(X+(W-Width)/2,Y+10,Label,11,Disabled?IM_COL32(94,94,94,255):Ink);return Hit;
    }
    void Slider(float X,float Y,float W,const char* Label,bool Custom=false){
        auto* P=Find(Sheet,Label);if(!P)return;Text(X,Y,Label,11,Muted);
        ImGui::SetCursorScreenPos(At(X,Y+22));ImGui::PushID(Label);ImGui::BeginChild(Label,{W,30},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        if(Controls.SliderPill("##value",&P->Figure,P->Minimum,P->Maximum,P->Decimals,P->Unit,false,false,true)){
            if(!std::isfinite(P->Figure))P->Figure=P->Minimum;
            P->Figure=std::clamp(P->Figure,P->Minimum,P->Maximum);
            if(P->Decimals==0)P->Figure=std::round(P->Figure);
            if(Custom)Find(Sheet,"Type")->Picked=4;
        }
        ImGui::EndChild();ImGui::PopID();
    }
    void Select(float X,float Y,float W,const char* Label){auto* P=Find(Sheet,Label);Text(X,Y,Label,11,Muted);ImGui::SetCursorScreenPos(At(X,Y+22));ImGui::BeginChild(Label,{W,32},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);Controls.DropDown("##select",&P->Picked,P->Options,P->OptionCount);ImGui::EndChild();}
};
}
LensFlarePreviewInfo QueryLensFlarePreview() noexcept {if(auto* C=Existing())return {&C->Texture,C->Revision,C->Baked,640,280};return {};}
bool ExportLensFlarePreview(const char* Path) noexcept {
    auto* C=Existing();if(!C||!C->Revision||!Path)return false;
    try{
        std::filesystem::path P(Path);if(P.has_parent_path())std::filesystem::create_directories(P.parent_path());
        std::ofstream File(P,std::ios::binary|std::ios::trunc);File<<"PF\n640 280\n"<<(std::endian::native==std::endian::little?"-1.0\n":"1.0\n");
        for(int Y=279;Y>=0;--Y)File.write(reinterpret_cast<const char*>(&C->Linear[Y*640*3]),640*3*sizeof(float));
        File.close();if(!File){C->ExportFailed=true;return false;}C->Baked=C->Revision;C->ExportFailed=false;return true;
    }catch(...){C->ExportFailed=true;return false;}
}
void RecordLensFlareInspector(ControlPanel& Controls,EditorInstance& Row,EditorSheet& Sheet) noexcept {
    auto* Type=Find(Sheet,"Type");if(!Type||!Find(Sheet,"Preview X")){ImGui::TextUnformatted("Lens Flare properties unavailable");return;}
    auto& Cache=Resource();Update(Cache,Sheet);
    ImFont* Font=ImGui::GetFont();for(auto* F:ImGui::GetIO().Fonts->Fonts)if(std::strcmp(F->GetDebugName(),"Sun reference / regular")==0)Font=F;
    ImGui::PushFont(Font,14);float W=ImGui::GetContentRegionAvail().x-40;W=std::max(W,240.f);ImVec2 Origin=ImGui::GetCursorScreenPos();Origin.x+=20;Origin.y+=20;
    Panel U{Controls,Sheet,ImGui::GetWindowDrawList(),Origin,Font};bool Wide=W>=700;float Gap=16,Col=Wide?(W-Gap)/2:W;
    const float HeaderShift=W<360?44:0;
    U.Text(0,0,"Inspector / Environment / Optics",10,Muted);U.Text(0,44,"Lens Flare",32);
    if(U.Button(W-94,44+HeaderShift,94,"##flare-enabled",Find(Sheet,"Enabled")->On?"Enabled":"Disabled",Find(Sheet,"Enabled")->On))Find(Sheet,"Enabled")->On=!Find(Sheet,"Enabled")->On;
    U.Text(0,98+HeaderShift,"BAKING",10,Muted);
    U.Card(0,121+HeaderShift,W,162,"Lens flare image");
    U.D->AddImageRounded(Cache.Texture.GetTexRef(),U.At(24,185+HeaderShift),U.At(132,232.25f+HeaderShift),{0,0},{1,1},IM_COL32_WHITE,12);
    U.Button(148,174+HeaderShift,Wide?170:W-172,"##use-baked",W<360?"Use image":"Use baked image",false,true);
    if(Wide){if(U.Button(330,174+HeaderShift,158,"##bake-flare","Bake / export HDR"))ExportLensFlarePreview("Exports/LensFlare.pfm");U.Wrap(148,222+HeaderShift,W-172,"One cached image feeds this thumbnail and the composite. Scene baked-image playback is not wired yet.");}
    else {if(U.Button(148,214+HeaderShift,W-172,"##bake-flare","Export HDR"))ExportLensFlarePreview("Exports/LensFlare.pfm");}
    float CY=299+HeaderShift,CH=141+(W-48)*280/640+14+44+(Wide?72:140)+50;
    U.Card(0,CY,W,CH,"Flare composite");char Text[96];std::snprintf(Text,sizeof(Text),"%.2f ×",double(Find(Sheet,"Intensity")->Figure));U.Text(24,CY+64,Text,40);
    U.Text(24,CY+111,"Layered light · additive linear radiance",11,Muted);
    float ImageY=CY+141,ImageH=(W-48)*280/640;
    U.D->AddRectFilled(U.At(24,ImageY),U.At(W-24,ImageY+ImageH),IM_COL32(8,8,8,255),14);
    U.D->AddImageRounded(Cache.Texture.GetTexRef(),U.At(24,ImageY),U.At(W-24,ImageY+ImageH),{0,0},{1,1},IM_COL32_WHITE,14);
    ImGui::SetCursorScreenPos(U.At(24,ImageY));ImGui::InvisibleButton("##flare-source",{W-48,ImageH},ImGuiButtonFlags_EnableNav);
    if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0)){
        auto M=ImGui::GetIO().MousePos;Find(Sheet,"Preview X")->Figure=std::clamp((M.x-Origin.x-24)/(W-48),.06f,.94f);Find(Sheet,"Preview Y")->Figure=std::clamp((M.y-Origin.y-ImageY)/ImageH,.08f,.92f);
    }
    if(ImGui::IsItemFocused())for(auto Key:{ImGuiKey_LeftArrow,ImGuiKey_RightArrow,ImGuiKey_UpArrow,ImGuiKey_DownArrow})if(ImGui::IsKeyPressed(Key)){
        auto* X=Find(Sheet,"Preview X");auto* Y=Find(Sheet,"Preview Y");float Step=ImGui::GetIO().KeyShift?.1f:.02f;
        X->Figure=std::clamp(X->Figure+(Key==ImGuiKey_RightArrow?Step:Key==ImGuiKey_LeftArrow?-Step:0),.06f,.94f);Y->Figure=std::clamp(Y->Figure+(Key==ImGuiKey_DownArrow?Step:Key==ImGuiKey_UpArrow?-Step:0),.08f,.92f);
    }
    float After=ImageY+ImageH+14;U.Wrap(24,After,W-160,"Drag the light · ghosts follow the optical axis");
    if(U.Button(W-126,After-5,102,"##flare-recenter","Recenter")){Find(Sheet,"Preview X")->Figure=.28f;Find(Sheet,"Preview Y")->Figure=.43f;}
    // Card height follows image aspect, including on very wide inspectors.
    float ControlsY=After+44;
    U.Slider(24,ControlsY,Wide?(W-72)/2:W-48,"Intensity");
    U.Slider(Wide?(W+24)/2:24,ControlsY+(Wide?0:68),Wide?(W-72)/2:W-48,"Spread",true);
    float ActualEnd=ControlsY+(Wide?72:140);
    U.Wrap(24,ActualEnd,W-48,Cache.ExportFailed?"Export failed. Check the output directory.":Cache.Baked==Cache.Revision?"HDR image exported · Exports/LensFlare.pfm":Cache.Baked?"Settings changed · exported bake is out of date":"Live procedural preview · HDR export uses these same cached linear pixels.");
    float LY=std::max(CY+CH,ActualEnd+50)+Gap;
    U.Card(0,LY,Col,565,"Flare layers");U.Wrap(24,LY+55,Col-48,"Combine layers instead of choosing just one. Editing a layer selects Custom layers.");
    U.Select(24,LY+98,Col-48,"Type");
    const char* Names[]={"Anamorphic","Streaks","Starburst"};const char* Descriptions[]={"Cool horizontal optical streak","Rotatable warm light rays","Radial diffraction spikes"};
    // Match the Sun quick tiles, retaining a single horizontal row at compact widths.
    const float TileWidth=(Col-66)/3,TileY=LY+174;
    for(int I=0;I<3;++I){
        auto* P=Find(Sheet,Names[I]);float X=24+I*(TileWidth+9);bool On=Type->Picked==4&&P->On;
        ImGui::SetCursorScreenPos(U.At(X,TileY));
        if(ImGui::InvisibleButton(Names[I],{TileWidth,106},ImGuiButtonFlags_EnableNav)){P->On=!On;Type->Picked=4;On=P->On;}
        const ImU32 Key=On?IM_COL32(105,200,132,255):IM_COL32(204,118,115,255);
        const ImU32 Symbol=On?IM_COL32(22,50,30,255):IM_COL32(55,29,28,255);
        U.D->AddRectFilled(U.At(X,TileY),U.At(X+TileWidth,TileY+106),IM_COL32(32,32,32,255),16);
        U.D->AddRect(U.At(X,TileY),U.At(X+TileWidth,TileY+106),On?IM_COL32(50,50,50,255):IM_COL32(73,50,50,255),16);
        const float CX=X+TileWidth/2,CY=TileY+29;
        U.D->AddCircleFilled(U.At(CX,CY),15.5f,Key,32);
        if(I==2)Icon(U.D,U.At(CX-9,CY-9),Mark::Sparkles,Symbol,18);
        else if(I==0){
            U.D->AddLine(U.At(CX-8,CY),U.At(CX+8,CY),Symbol,1.4f);
            for(float Side:{-1.f,1.f}){U.D->AddLine(U.At(CX+Side*8,CY),U.At(CX+Side*4,CY-4),Symbol,1.4f);U.D->AddLine(U.At(CX+Side*8,CY),U.At(CX+Side*4,CY+4),Symbol,1.4f);}
        } else {
            U.D->AddLine(U.At(CX-6,CY+6),U.At(CX+6,CY-6),Symbol,1.4f);
            U.D->AddLine(U.At(CX-3,CY-6),U.At(CX+6,CY-6),Symbol,1.4f);U.D->AddLine(U.At(CX+6,CY-6),U.At(CX+6,CY+3),Symbol,1.4f);
        }
        const float LabelSize=std::min(10.f,10.f*(TileWidth-8)/Font->CalcTextSizeA(10,10000,0,Names[I]).x);
        U.Text(X+(TileWidth-Font->CalcTextSizeA(LabelSize,10000,0,Names[I]).x)/2,TileY+57,Names[I],LabelSize,IM_COL32(200,200,200,255));
        const char* State=On?"ON":"OFF";
        U.Text(X+(TileWidth-Font->CalcTextSizeA(8,10000,0,State).x)/2,TileY+80,State,8,Key);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s%s",Descriptions[I],Type->Picked==4?"":" · click to enable Custom layers");
    }
    U.Slider(24,LY+323,Col-48,"Rotation",true);U.Slider(24,LY+414,Col-48,"Ray pairs",true);
    U.Wrap(24,LY+504,Col-48,"Anamorphic stays horizontal. Rotation affects streaks and starburst.");
    float GX=Wide?Col+Gap:0,GY=Wide?LY:LY+581;
    U.Card(GX,GY,Col,565,"Lens ghosts");std::snprintf(Text,sizeof(Text),"%.0f",double(Find(Sheet,"Ghosts")->Figure));U.Text(GX+24,GY+60,Text,42);U.Text(GX+80,GY+83,"elements",12,Muted);
    if(U.Button(GX+Col-100,GY+65,34,"##ghost-minus","−")){Find(Sheet,"Ghosts")->Figure=std::max(0.f,Find(Sheet,"Ghosts")->Figure-1);Type->Picked=4;}
    if(U.Button(GX+Col-58,GY+65,34,"##ghost-plus","+")){Find(Sheet,"Ghosts")->Figure=std::min(24.f,Find(Sheet,"Ghosts")->Figure+1);Type->Picked=4;}
    U.Wrap(GX+24,GY+119,Col-48,Type->Picked==4?"Aperture-shaped internal reflections":"Legacy preset: round ghosts, maximum 8");
    const char* Shapes[]={"Round","Hexagon","Octagon"};float BW=(Col-64)/3;
    for(int I=0;I<3;++I){float X=GX+24+I*(BW+8),Y=GY+153;bool Selected=Type->Picked==4&&Find(Sheet,"Ghost shape")->Picked==unsigned(I);
        ImGui::SetCursorScreenPos(U.At(X,Y));if(ImGui::InvisibleButton(Shapes[I],{BW,60})){Find(Sheet,"Ghost shape")->Picked=unsigned(I);Type->Picked=4;}
        U.D->AddRectFilled(U.At(X,Y),U.At(X+BW,Y+60),Selected?IM_COL32(45,42,36,255):IM_COL32(38,38,38,255),12);
        U.D->AddRect(U.At(X,Y),U.At(X+BW,Y+60),Selected?Accent:IM_COL32(58,58,58,255),12);
        if(I==0)U.D->AddCircle(U.At(X+BW/2,Y+21),11,Selected?Accent:Muted,32,1);
        else {int Sides=I==1?6:8;for(int J=0;J<Sides;++J){float A=J*2*Pi/Sides,B=(J+1)*2*Pi/Sides;U.D->AddLine(U.At(X+BW/2+11*std::cos(A),Y+21+11*std::sin(A)),U.At(X+BW/2+11*std::cos(B),Y+21+11*std::sin(B)),Selected?Accent:Muted,1);}}
        U.Text(X+(BW-Font->CalcTextSizeA(10,10000,0,Shapes[I]).x)/2,Y+42,Shapes[I],10,Muted);
    }
    U.Slider(GX+24,GY+221,Col-48,"Ghosts",true);U.Slider(GX+24,GY+302,Col-48,"Ghost brightness",true);U.Slider(GX+24,GY+383,Col-48,"Ghost spacing",true);U.Slider(GX+24,GY+464,Col-48,"Chromatic");
    float HY=GY+581;U.Card(0,HY,W,Wide?248:465,"Halo & optical response");
    U.Slider(24,HY+62,Wide?(W-72)/2:W-48,"Halo Radius");U.Slider(24,HY+143,Wide?(W-72)/2:W-48,"Halo width",true);
    float HX=Wide?(W+24)/2:24,HO=Wide?0:164;
    U.Slider(HX,HY+62+HO,Wide?(W-72)/2:W-48,"Halo brightness",true);U.Slider(HX,HY+143+HO,Wide?(W-72)/2:W-48,"Streak gain");
    float End=HY+(Wide?264:481),LegacyH=W<360?178:146;U.Card(0,End,W,LegacyH,"Legacy preset settings");
    U.Slider(24,End+57,W-48,"Aperture Blades");U.Wrap(24,End+119,W-48,"Aperture blades affect legacy presets; Custom layers uses Ray pairs.");
    Update(Cache,Sheet); // image command references the same texture, including edits made below it this frame.
    ImGui::SetCursorScreenPos(U.At(0,End+LegacyH+22));ImGui::TextDisabled("Preview pose is authoring-only. Scene flare follows the camera and Sun visibility.");ImGui::Dummy({0,20});ImGui::PopFont();
    (void)Row;
}
}
