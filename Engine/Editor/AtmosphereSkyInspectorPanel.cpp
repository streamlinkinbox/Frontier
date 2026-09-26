#include "AtmosphereSkyInspectorPanel.h"
#include "ControlPanel.h"
#include "SkyBakePreview.h"
#include "SunColourTemperature.h"
#include "SunReferenceDraw.h"
#include <imgui_internal.h>
#include <array>
#include <cstring>
#include <cstdio>
namespace Frontier {
namespace {
using namespace SunReference;
constexpr ImU32 Ink=IM_COL32(233,233,233,255),Muted=IM_COL32(145,145,145,255),Accent=IM_COL32(153,170,250,255);
EditorProperty* Find(EditorSheet& S,const char* Name){for(auto& G:S.Groups)for(unsigned I=0;I<G.PropertyCount;++I)if(std::strcmp(G.Properties[I].Label,Name)==0)return &G.Properties[I];return nullptr;}
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
    ControlPanel& Controls;EditorSheet& Sheet;ImDrawList* D;ImVec2 O;ImFont* Font;
    ImVec2 At(float X,float Y){return {O.x+X,O.y+Y};}
    void Text(float X,float Y,const char* T,float Size=12,ImU32 C=Ink){D->AddText(Font,Size,At(X,Y),C,T);}
    void Wrap(float X,float Y,float W,const char* T){D->AddText(Font,11,At(X,Y),Muted,T,nullptr,W);}
    void Card(float X,float Y,float W,float H,const char* Title){
        int Start=D->VtxBuffer.Size;D->AddRectFilled(At(X,Y),At(X+W,Y+H),IM_COL32_WHITE,22);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(D,Start,D->VtxBuffer.Size,At(X,Y),At(X+W,Y+H),IM_COL32(37,37,37,255),IM_COL32(32,32,32,255));
        D->AddRect(At(X,Y),At(X+W,Y+H),IM_COL32(52,52,52,255),22);
        Mark M=std::strcmp(Title,"Flare composite")==0?Mark::Sun:std::strcmp(Title,"Flare layers")==0?Mark::Sparkles:std::strcmp(Title,"Atmosphere bake")==0?Mark::Bake:Mark::Orbit;
        if(Title[0])Icon(D,At(X+23,Y+21),M,Accent,17);
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
constexpr ImGuiID Owner=0x534b5950;
struct SkyCache {
    ImTextureData Radiance,Transmission,FullSphere,Profile;
    const uint16_t* Source=nullptr;uint64_t Revision=0;int View=0;
    SkyCache(){for(auto* T:{&Radiance,&Transmission,&FullSphere,&Profile}){T->Create(ImTextureFormat_RGBA32,640,T==&FullSphere?320:160);T->UseColors=true;ImGui::RegisterUserTexture(T);}}
};
void Cleanup(ImGuiContext*,ImGuiContextHook* H){auto* C=static_cast<SkyCache*>(H->UserData);for(auto* T:{&C->Radiance,&C->Transmission,&C->FullSphere,&C->Profile})ImGui::UnregisterUserTexture(T);delete C;}
SkyCache& Cache(){for(auto& H:ImGui::GetCurrentContext()->Hooks)if(H.Owner==Owner)return *static_cast<SkyCache*>(H.UserData);auto* C=new SkyCache;ImGuiContextHook H;H.Owner=Owner;H.Type=ImGuiContextHookType_Shutdown;H.Callback=Cleanup;H.UserData=C;ImGui::AddContextHook(ImGui::GetCurrentContext(),&H);return *C;}
void Update(SkyCache& C,const EditorSkyImage& Image){
    if(!Image.Pixels||Image.Width!=256||Image.Height!=512)return;
    if(C.Source==Image.Pixels&&C.Revision==Image.Revision)return;
    for(int Layer=0;Layer<4;++Layer){auto& Texture=Layer==0?C.Radiance:Layer==1?C.Transmission:Layer==2?C.FullSphere:C.Profile;
        for(int Y=0;Y<Texture.Height;++Y)for(int X=0;X<640;++X){
            auto Direction=Layer==3?SkyAtmosphereProfileDirection((Y+.5f)/160,Image.BakedSunDirection):SkyPanoramaDirection((X+.5f)/640,(Y+.5f)/320);
            auto RGB=SkyPreviewSample(Image.Pixels,Image.Width,Direction.data(),Layer==1);auto* Out=Texture.Pixels+(Y*640+X)*4;
            for(int I=0;I<3;++I){float V=std::max(RGB[I],0.f);if(Layer!=1)V=V/(1+V);Out[I]=static_cast<unsigned char>(255*SunColourTemperature::DisplayChannel(V)+.5f);}Out[3]=255;
        }
    }
    C.Source=Image.Pixels;C.Revision=Image.Revision;
    for(auto* T:{&C.Radiance,&C.Transmission,&C.FullSphere,&C.Profile})ImTextureDataQueueUpload(T,0,0,T->Width,T->Height);
}
// The original reference's wavelength curves. These are labelled illustrations, not bake data.
void GradientQuad(Canvas C,ImVec2 A,ImVec2 B,ImVec2 CC,ImVec2 D,ImU32 CA,ImU32 CB,ImU32 CColour,ImU32 CD){
    ImVec2 UV=ImGui::GetFontTexUvWhitePixel();C.D->PrimReserve(6,4);unsigned Base=C.D->_VtxCurrentIdx;
    for(unsigned I:{0u,1u,2u,0u,2u,3u})C.D->PrimWriteIdx(ImDrawIdx(Base+I));
    C.D->PrimWriteVtx(C.P(A.x,A.y),UV,CA);C.D->PrimWriteVtx(C.P(B.x,B.y),UV,CB);C.D->PrimWriteVtx(C.P(CC.x,CC.y),UV,CColour);C.D->PrimWriteVtx(C.P(D.x,D.y),UV,CD);
}
void Spectrum(Canvas C,float X,float Y,float Width,bool Scatter=false){
    const ImU32 OzoneStops[]={Colour(138,116,185,.7f),Colour(117,151,201,.7f),Colour(155,183,160,.7f),Colour(201,180,125,.7f),Colour(192,134,129,.7f)};
    const ImU32 ScatterStops[]={Colour(146,127,176,.65f),Colour(127,157,201,.65f),Colour(148,181,169,.65f),Colour(197,178,126,.65f),Colour(194,139,124,.65f)};
    const float Offset[]={0,.25f,.5f,Scatter?.75f:.72f,1};const auto* Stops=Scatter?ScatterStops:OzoneStops;
    for(int I=0;I<4;++I)C.D->AddRectFilledMultiColor(C.P(X+Width*Offset[I],Y),C.P(X+Width*Offset[I+1],Y+3),Stops[I],Stops[I+1],Stops[I+1],Stops[I]);
}
void Scattering(Canvas C,float Rayleigh,float Haze){
    for(float Y:{35.f,80.f,125.f,170.f})C.Dash({24,Y},{595,Y},Colour(255,255,255,12/255.f),1,2,6);
    for(float X:{24.f,167.f,310.f,452.f,595.f})C.Line({X,20},{X,181},Colour(255,255,255,7/255.f));
    auto Bezier=[](ImVec2 A,ImVec2 B,ImVec2 CC,ImVec2 D,float T){float U=1-T;return ImVec2(U*U*U*A.x+3*U*U*T*B.x+3*U*T*T*CC.x+T*T*T*D.x,U*U*U*A.y+3*U*U*T*B.y+3*U*T*T*CC.y+T*T*T*D.y);};
    std::array<ImVec2,81> Line;
    for(int I=0;I<=80;++I){float T=I/40.f;Line[I]=I<=40?Bezier({24,160-Rayleigh*43},{135,160-Rayleigh*35},{150,148},{320,158},T):Bezier({320,158},{490,168},{510,171},{595,172},T-1);}
    float MinimumY=181;for(auto P:Line)MinimumY=std::min(MinimumY,P.y);
    auto Fill=[&](float Y){return Colour(145,175,210,.19f*(181-Y)/std::max(.001f,181-MinimumY));};
    for(int I=1;I<=80;++I)GradientQuad(C,Line[I-1],Line[I],{Line[I].x,181},{Line[I-1].x,181},Fill(Line[I-1].y),Fill(Line[I].y),Fill(181),Fill(181));
    C.Stroke(Line,Colour(161,185,216),2);
    for(int I=0;I<=80;++I){float T=I/80.f,U=1-T;Line[I]={U*U*24+2*U*T*310+T*T*595,U*U*(170-Haze*1.1f)+2*U*T*(175-Haze*.9f)+T*T*(177-Haze*.7f)};}
    C.Stroke(Line,Colour(204,186,149),1.2f,5,5);Spectrum(C,24,193,571,true);C.Text(24,218,"380 nm",Muted,10,false);C.Text(310,218,"Visible spectrum",Muted);C.Text(555,218,"780 nm",Muted,10,false);
}
void Haze(Canvas C,float Amount){
    C.D->AddRectFilled(C.P(25,16),C.P(335,116),Colour(27,27,27),10*C.Scale);C.D->AddRect(C.P(25,16),C.P(335,116),Colour(255,255,255,11/255.f),10*C.Scale);
    C.D->AddRectFilledMultiColor(C.P(25,16),C.P(335,116),Colour(202,184,156,0),Colour(202,184,156,Amount/450),Colour(202,184,156,Amount/450),Colour(202,184,156,0));
    ImVec2 UV=ImGui::GetFontTexUvWhitePixel();C.D->PrimReserve(3,3);unsigned Base=C.D->_VtxCurrentIdx;
    for(unsigned I:{0u,1u,2u})C.D->PrimWriteIdx(ImDrawIdx(Base+I));
    C.D->PrimWriteVtx(C.P(34,67),UV,Colour(215,192,151,.42f));C.D->PrimWriteVtx(C.P(327,35),UV,Colour(215,192,151,.38f*std::exp(-Amount/22)));C.D->PrimWriteVtx(C.P(327,100),UV,Colour(215,192,151,.38f*std::exp(-Amount/22)));
    for(int I=0;I<32;++I)C.Circle(float(50+I*53%275),float(28+I*31%75),.6f+I%3*.45f,Colour(214,193,157,Amount/200));
    std::array<ImVec2,45> P;for(int I=0;I<45;++I)P[I]={35+I*6.5f,108-78*std::exp(-Amount/25*I/44)};C.Stroke(P,Colour(212,188,148),1.4f);C.Circle(35,67,4,Colour(237,219,184));C.Text(25,138,"LIGHT SOURCE",Muted,9,false);C.Text(268,138,"DISTANCE",Muted,9,false);
}
void Ozone(Canvas C,float Amount){
    for(float Y:{36.f,77.f,118.f})C.Dash({22,Y},{338,Y},Colour(255,255,255,11/255.f),1,2,5);
    C.Dash({22,36},{338,36},Colour(188,165,130,.5f),1,4,5);
    std::array<ImVec2,81> P;for(int I=0;I<=80;++I){float T=I/80.f;P[I]={22+T*316,36+Amount*.78f*std::exp(-std::pow((T-.59f)/.23f,2.0f))};}
    auto Fill=[&](float Y){return Colour(184,160,209,.2f-.18f*(Y-36)/std::max(.001f,Amount*.78f));};
    for(int I=1;I<=80;++I)GradientQuad(C,{P[I-1].x,36},{P[I].x,36},P[I],P[I-1],Fill(36),Fill(36),Fill(P[I].y),Fill(P[I-1].y));
    C.Stroke(P,Colour(190,163,221),1.6f);C.Circle(22+.59f*316,36+Amount*.78f,3.5f,Colour(216,195,237));C.Dash({22+.59f*316,42+Amount*.78f},{22+.59f*316,138},Colour(183,154,205,.25f),1,2,4);
    Spectrum(C,22,142,316);C.Text(22,166,"380 nm",Muted,9,false);C.Text(180,166,"VISIBLE LIGHT",Muted,9);C.Text(301,166,"780 nm",Muted,9,false);
}
}
void RecordAtmosphereSkyInspector(ControlPanel& Controls,EditorInstance&,EditorSheet& Sheet) noexcept {
    if(!Find(Sheet,"Rayleigh")||!Find(Sheet,"Fetch Baked Dome")){ImGui::TextUnformatted("Atmosphere / Sky properties unavailable");return;}
    auto& Cached=Cache();Update(Cached,Sheet.SkyImage);ImFont* Font=ImGui::GetFont();for(auto* F:ImGui::GetIO().Fonts->Fonts)if(std::strcmp(F->GetDebugName(),"Sun reference / regular")==0)Font=F;
    ImGui::PushFont(Font,14);float W=std::max(240.f,ImGui::GetContentRegionAvail().x-40);auto Origin=ImGui::GetCursorScreenPos();Origin.x+=20;Origin.y+=20;Panel U{Controls,Sheet,ImGui::GetWindowDrawList(),Origin,Font};bool Wide=W>=700;float Col=Wide?(W-16)/2:W;
    U.Text(0,0,"Inspector / Environment",10,Muted);U.Text(0,40,"Atmosphere",W<360?24:32);U.Text(0,97,"BAKING",10,Muted);
    U.Card(0,120,W,225,"Atmosphere bake");auto* Fetch=Find(Sheet,"Fetch Baked Dome");float TileW=W<360?96:119,BakeX=TileW+36;
    ImGui::SetCursorScreenPos(U.At(24,179));if(ImGui::InvisibleButton("##sky-fetch",{TileW,106}))Fetch->On=!Fetch->On;
    ImU32 Key=Fetch->On?IM_COL32(105,200,132,255):IM_COL32(204,118,115,255);
    U.D->AddRectFilled(U.At(24,179),U.At(24+TileW,285),IM_COL32(32,32,32,255),16);U.D->AddRect(U.At(24,179),U.At(24+TileW,285),Fetch->On?IM_COL32(50,50,50,255):IM_COL32(73,50,50,255),16);
    U.D->AddCircleFilled(U.At(24+TileW/2,208),15.5f,Key,32);Icon(U.D,U.At(24+TileW/2-9,199),Mark::Bake,Fetch->On?IM_COL32(22,50,30,255):IM_COL32(55,29,28,255),18);
    const char* ToggleText="Use baked image";U.Text(24+(TileW-Font->CalcTextSizeA(10,10000,0,ToggleText).x)/2,236,ToggleText,10);U.Text(24+TileW/2-8,259,Fetch->On?"ON":"OFF",8,Key);
    if(U.Button(BakeX,179,std::min(158.f,W-BakeX-24),"##sky-bake",Sheet.SkyImage.Pending?"Queued":"Bake atmosphere",false,Sheet.SkyImage.Pending))Sheet.SkyImage.RequestBake=true;
    const char* State=Sheet.SkyImage.Pending?"Bake requested · waiting for renderer":!Sheet.SkyImage.Pixels?"No baked image · analytic sky":Sheet.SkyImage.Stale?"STALE BAKE · renderer uses analytic sky":!Sheet.SkyImage.Resident?"Current CPU bake · not uploaded":Fetch->On?"Current resident bake · fetch enabled":"Current bake · analytic mode selected";
    U.Wrap(BakeX,230,W-BakeX-24,State);U.Text(24,315,"RGBA16F · 256 × 512 · radiance + transmittance",10,Muted);
    float Y=361,BW=(W-72)/4;const char* Labels[]={W<360?"Air":"Atmosphere",W<360?"Sky":"Panorama",W<360?"Transmit":"Transmittance",W<360?"Sphere":"Full sphere"};
    // Controls first so the chosen image and aspect update together on the same frame.
    for(int I=0;I<4;++I){
        if(U.Button(24+I*(BW+8),Y+58,BW,Labels[I],Labels[I],Cached.View==I))Cached.View=I;
    }
    float ImageH=(W-48)/(Cached.View==3?2:4),Height=ImageH+153;
    // Background is drawn on the parent list before the image; keep selector above its surface.
    // Re-draw the selector after the card, preserving the first pass's input hit regions.
    U.Card(0,Y,W,Height,"Baked atmosphere");
    for(int I=0;I<4;++I){float X=24+I*(BW+8);U.D->AddRectFilled(U.At(X,Y+58),U.At(X+BW,Y+90),Cached.View==I?IM_COL32(49,45,37,255):IM_COL32(40,40,40,255),12);U.D->AddRect(U.At(X,Y+58),U.At(X+BW,Y+90),Cached.View==I?Accent:IM_COL32(62,62,62,255),12);const char* Label=Labels[I];U.Text(X+(BW-Font->CalcTextSizeA(11,10000,0,Label).x)/2,Y+68,Label,11);}
    U.D->AddRectFilled(U.At(24,Y+108),U.At(W-24,Y+108+ImageH),IM_COL32(16,19,23,255),14);
    if(Sheet.SkyImage.Pixels)U.D->AddImageRounded((Cached.View==3?Cached.FullSphere:Cached.View==2?Cached.Transmission:Cached.View==1?Cached.Radiance:Cached.Profile).GetTexRef(),U.At(24,Y+108),U.At(W-24,Y+108+ImageH),{0,0},{1,1},IM_COL32_WHITE,14);
    else U.Wrap(44,Y+108+ImageH/2,W-88,"Bake the atmosphere to see the actual HDR image here. No substitute illustration.");
    U.Wrap(24,Y+116+ImageH,W-48,Cached.View==0?"Altitude profile · sampled opposite the baked Sun · zenith above, horizon below":Cached.View==3?"Full sphere · zenith / horizon / nadir; black below ground is expected":"360° sky panorama · zenith at top, horizon below · decoded from the bake");Y+=Height+16;
    U.Wrap(8,Y,W-16,"Atmospheric scattering only: the bright area is scattered sunlight, not the Sun disc. Moon, stars, clouds and fog remain separate.");Y+=52;
    U.Card(0,Y,W,485,"Atmospheric scattering");char Text[64];std::snprintf(Text,sizeof(Text),"%.1f ×",double(Find(Sheet,"Rayleigh")->Figure));U.Text(24,Y+65,Text,40);U.Text(24,Y+115,"How air molecules scatter sunlight",11,Muted);
    U.D->PushClipRect(U.At(24,Y+145),U.At(W-24,Y+395),true);Scattering(Fit(U.D,U.At(24,Y+145),{W-48,250},620,225),Find(Sheet,"Rayleigh")->Figure,Find(Sheet,"Mie")->Figure/6*100);U.D->PopClipRect();U.Slider(24,Y+409,W-48,"Rayleigh");Y+=501;
    U.Card(0,Y,Col,380,"Aerosol haze");std::snprintf(Text,sizeof(Text),"%.2f ×",double(Find(Sheet,"Mie")->Figure));U.Text(24,Y+62,Text,38);U.Wrap(24,Y+110,Col-48,"Suspended particles soften and attenuate light");Haze(Fit(U.D,U.At(24,Y+146),{Col-48,160},360,150),Find(Sheet,"Mie")->Figure/6*100);U.Slider(24,Y+308,Col-48,"Mie");
    float OX=Wide?Col+16:0,OY=Wide?Y:Y+396;U.Card(OX,OY,Col,380,"Ozone absorption");std::snprintf(Text,sizeof(Text),"%.2f ×",double(Find(Sheet,"Ozone")->Figure));U.Text(OX+24,OY+62,Text,38);U.Wrap(OX+24,OY+110,Col-48,"Selective absorption across visible wavelengths");Ozone(Fit(U.D,U.At(OX+24,OY+146),{Col-48,160},360,178),Find(Sheet,"Ozone")->Figure/4*100);U.Slider(OX+24,OY+308,Col-48,"Ozone");Y=OY+396;
    U.Card(0,Y,W,410,"Density falloff");U.Slider(24,Y+60,W-48,"Rayleigh Scale H");
    for(int I=0;I<10;++I){float Km=float(9-I),Width=8+std::exp(-Km/(Find(Sheet,"Rayleigh Scale H")->Figure/1000))*85;std::snprintf(Text,sizeof(Text),"%d km",9-I);U.Text(24,Y+138+I*19,Text,10,Muted);U.D->AddRectFilled(U.At(70,Y+140+I*19),U.At(70+(W-100)*Width/100,Y+148+I*19),IM_COL32(125,148,169,130),4);}
    U.Slider(24,Y+336,W-48,"Mie Scale H");Y+=426;
    U.Card(0,Y,W,150,"Ground reflectance");ImGui::SetCursorScreenPos(U.At(24,Y+61));Controls.ColourChip("##ground-albedo",Find(Sheet,"Ground Albedo")->ColourTint);U.Wrap(24,Y+110,W-48,"Original RGB ground albedo · applied separately, not stored in the smooth dome bake.");Y+=166;
    // Retain native fields absent from the reference without inventing physical percentages.
    ImGui::SetCursorScreenPos(U.At(0,Y));ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{24,20});ImGui::BeginChild("##sky-extra",{W,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleColor(ImGuiCol_Header,IM_COL32(38,38,38,255));ImGui::PushStyleColor(ImGuiCol_HeaderHovered,IM_COL32(49,49,49,255));ImGui::PushStyleColor(ImGuiCol_HeaderActive,IM_COL32(57,57,57,255));
    bool Expanded=ImGui::CollapsingHeader("Additional atmosphere settings");ImGui::PopStyleColor(3);
    if(Expanded)for(const char* Label:{"Sky Brightness","Sky Tint","Mie Anisotropy","Atmosphere","Horizon Glow","White Line","Line at civil only","Air Mass","Tier Samples"}){
        auto* P=Find(Sheet,Label);if(!P)continue;ImGui::PushID(Label);ImGui::TextUnformatted(Label);
        if(P->Category==EditorPropertyCategory::Slider)Controls.SliderPill("##extra",&P->Figure,P->Minimum,P->Maximum,P->Decimals,P->Unit,false,false,true);
        else if(P->Category==EditorPropertyCategory::Colour)Controls.ColourChip("##extra",P->ColourTint);
        else if(P->Category==EditorPropertyCategory::Switch)Controls.Switch("##extra",&P->On);
        else Controls.Readout(P->Text);
        ImGui::Dummy({0,12});ImGui::PopID();
    }
    ImGui::EndChild();float EH=ImGui::GetItemRectSize().y;ImGui::PopStyleVar();U.Card(0,Y,W,EH,"");
    ImGui::SetCursorScreenPos(U.At(0,Y+EH+20));ImGui::TextDisabled("Reference diagrams are illustrative. The panorama alone displays actual baked pixels.");ImGui::Dummy({0,20});ImGui::PopFont();
}
}
