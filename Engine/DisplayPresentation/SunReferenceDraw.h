#pragma once
// Direct geometry transcription, not replacement artwork.
// Sources: environment-graphics.jsx SunGizmo / IlluminanceCurve; src.jsx DayCurve.
// SVG viewBox coordinates and sample counts intentionally match those components.
#include <imgui.h>
#include <array>
#include <algorithm>
#include <cmath>
namespace Frontier::SunReference {
constexpr float Pi=3.14159265358979323846f;
inline ImU32 Colour(unsigned R,unsigned G,unsigned B,float A=1) {return IM_COL32(R,G,B,int(A*255+0.5f));}
struct Canvas {
    ImDrawList* D; ImVec2 Origin; float Scale;
    ImVec2 P(float X,float Y) const {return {Origin.x+X*Scale,Origin.y+Y*Scale};}
    void Line(ImVec2 A,ImVec2 B,ImU32 C,float W=1) const {D->AddLine(P(A.x,A.y),P(B.x,B.y),C,W*Scale);}
    void Circle(float X,float Y,float R,ImU32 C) const {D->AddCircleFilled(P(X,Y),R*Scale,C,64);}
    void Ellipse(float X,float Y,float Rx,float Ry,ImU32 C,bool Fill,float W=1) const {
        if(Fill) D->AddEllipseFilled(P(X,Y),{Rx*Scale,Ry*Scale},C,0,96);
        else D->AddEllipse(P(X,Y),{Rx*Scale,Ry*Scale},C,0,96,W*Scale);
    }
    void Text(float X,float Y,const char* S,ImU32 C,float Size=10,bool Centre=true) const {
        ImFont* F=ImGui::GetFont();auto At=P(X,Y-Size);
        if(Centre) At.x-=F->CalcTextSizeA(Size*Scale,10000,0,S).x/2;
        D->AddText(F,Size*Scale,At,C,S);
    }
    template<size_t N> void Stroke(const std::array<ImVec2,N>& Points,ImU32 C,float W,float Dash=0,float Gap=0) const {
        if(Dash==0) {
            for(auto V:Points) D->PathLineTo(P(V.x,V.y));
            D->PathStroke(C,W*Scale);return;
        }
        float Phase=0;
        for(size_t I=1;I<N;++I) {
            auto A=Points[I-1],B=Points[I];float Dx=B.x-A.x,Dy=B.y-A.y,L=std::hypot(Dx,Dy),Done=0;
            while(Done<L) {
                if(L-Done<0.00001f)break;
                bool Ink=Phase<Dash;float Step=std::min(L-Done,(Ink?Dash:Dash+Gap)-Phase);
                if(Step<0.00001f){Phase=Ink?Dash:0;continue;}
                if(Ink) Line({A.x+Dx*Done/L,A.y+Dy*Done/L},{A.x+Dx*(Done+Step)/L,A.y+Dy*(Done+Step)/L},C,W);
                Done+=Step;Phase+=Step;if(Phase>=Dash+Gap-0.00001f)Phase=0;
            }
        }
    }
    void Dash(ImVec2 A,ImVec2 B,ImU32 C,float W,float On,float Off) const {Stroke(std::array<ImVec2,2>{A,B},C,W,On,Off);}
    void Glow(float X,float Y,float R,unsigned Red,unsigned Green,unsigned Blue,float Alpha) const {
        // Non-overlapping radial annuli, linearly interpolated vertex alpha as in SVG.
        const ImVec2 UV=ImGui::GetFontTexUvWhitePixel();
        for(int Ring=0;Ring<24;++Ring) for(int I=0;I<64;++I) {
            float R0=R*Ring/24,R1=R*(Ring+1)/24,A=I*2*Pi/64,B=(I+1)*2*Pi/64;
            ImVec2 V[4]={P(X+R0*std::cos(A),Y+R0*std::sin(A)),P(X+R1*std::cos(A),Y+R1*std::sin(A)),P(X+R1*std::cos(B),Y+R1*std::sin(B)),P(X+R0*std::cos(B),Y+R0*std::sin(B))};
            ImU32 Inner=Colour(Red,Green,Blue,Alpha*(1-R0/R)),Outer=Colour(Red,Green,Blue,Alpha*(1-R1/R));
            D->PrimReserve(6,4);unsigned Base=D->_VtxCurrentIdx;
            for(unsigned J:{0u,1u,2u,0u,2u,3u})D->PrimWriteIdx(ImDrawIdx(Base+J));
            D->PrimWriteVtx(V[0],UV,Inner);D->PrimWriteVtx(V[1],UV,Outer);D->PrimWriteVtx(V[2],UV,Outer);D->PrimWriteVtx(V[3],UV,Inner);
        }
    }
};
inline Canvas Fit(ImDrawList* D,ImVec2 Min,ImVec2 Size,float W,float H) {
    float S=std::min(Size.x/W,Size.y/H);return {D,{Min.x+(Size.x-W*S)/2,Min.y+(Size.y-H*S)/2},S};
}
inline ImVec2 OrbitPoint(float T,float Azimuth) {
    float A=(Azimuth-135)*Pi/180;return {180-103*std::cos(T)*std::cos(A),143-103*std::sin(T)+103*.45f*std::cos(T)*std::sin(A)};
}
inline void Orbit(Canvas C,float Azimuth,float Elevation) {
    C.Ellipse(180,143,103,49,Colour(21,21,21,.6f),true);
    for(int I=0;I<17;++I) {
        float X=77+I*13,DX=(X-180)/103;
        if(std::abs(DX)<=1){float H=49*std::sqrt(1-DX*DX);C.Line({X,143-H},{X,143+H},Colour(255,255,255,12/255.f),.65f);}
        float Y=94+I*13,DY=(Y-143)/49;
        if(std::abs(DY)<=1){float W=103*std::sqrt(1-DY*DY);C.Line({180-W,Y},{180+W,Y},Colour(255,255,255,12/255.f),.65f);}
    }
    C.Ellipse(180,143,103,49,Colour(112,112,112,.65f),false,.9f);
    std::array<ImVec2,81> Points;
    // The JSX rounds SVG path coordinates to two decimals.
    auto Rounded=[](ImVec2 P){return ImVec2(std::round(P.x*100)/100,std::round(P.y*100)/100);};
    for(int I=0;I<=80;++I)Points[I]=Rounded(OrbitPoint(Pi+I*Pi/80,Azimuth));
    C.Stroke(Points,Colour(119,115,107,.5f),1,2,3);
    for(int I=0;I<=80;++I)Points[I]=Rounded(OrbitPoint(I*Pi/80,Azimuth));
    C.Stroke(Points,Colour(226,172,91),1.8f);
    auto Sun=OrbitPoint(Elevation*Pi/180,Azimuth),Horizon=OrbitPoint(0,Azimuth);
    C.Dash(Sun,Horizon,Colour(184,170,142,.55f),.8f,2,4);
    C.Ellipse(Sun.x,Horizon.y,6,2.3f,Colour(197,192,182,.32f),true);
    C.Circle(Horizon.x,Horizon.y,5.5f,Colour(24,24,24));
    C.D->AddCircle(C.P(Horizon.x,Horizon.y),5.5f*C.Scale,Colour(241,239,231),64,1.7f*C.Scale);
    bool Night=Elevation<0;
    if(!Night)C.Glow(Sun.x,Sun.y,23,255,218,135,.3f);
    C.Circle(Sun.x,Sun.y,8,Night?Colour(155,128,92):Colour(244,205,117));
    C.D->AddCircle(C.P(Sun.x,Sun.y),8*C.Scale,Night?Colour(155,128,92):Colour(184,144,69),64,C.Scale);
    if(!Night)C.Circle(Sun.x,Sun.y,4.4f,Colour(255,244,199));
    C.Text(180,24,"12h",Colour(104,103,98),9);C.Text(180,269,"24h",Colour(104,103,98),9);
    C.Text(180,81,"N",Colour(156,155,149));C.Text(180,210,"S",Colour(156,155,149));
    C.Text(47,147,"W",Colour(156,155,149));C.Text(313,147,"E",Colour(156,155,149));
}
inline float DayHeight(float T){return 60-42*std::sin((T-6)/24*Pi*2);}
inline void Day(Canvas C,float Time) {
    C.D->AddRectFilled(C.P(18,10),C.P(134,108),Colour(131,151,193,9/255.f),4*C.Scale);
    C.D->AddRectFilled(C.P(366,10),C.P(482,108),Colour(131,151,193,9/255.f),4*C.Scale);
    C.Dash({18,60},{482,60},Colour(140,128,107,68/255.f),1,3,5);
    std::array<ImVec2,97> All;for(int I=0;I<=96;++I)All[I]={18+I/96.f*464,DayHeight(I/4.f)};
    C.Stroke(All,Colour(168,161,161,.45f),1.3f);
    std::array<ImVec2,49> Lit;for(int I=0;I<=48;++I)Lit[I]={134+I/48.f*232,DayHeight(6+I/4.f)};
    C.Stroke(Lit,Colour(218,193,138),1.5f);
    float X=18+Time/24*464,Y=DayHeight(Time);bool Night=Time<6||Time>=18;
    C.Dash({X,10},{X,110},Colour(192,173,140,.45f),1,2,4);
    C.Circle(X,Y,11,Night?Colour(167,183,223,.1f):Colour(233,200,137,.1f));
    C.Circle(X,Y,4,Night?Colour(179,194,223):Colour(239,209,154));
    C.Text(28,115,"NIGHT",Colour(122,119,112),8,false);C.Text(235,115,"DAY",Colour(122,119,112),8,false);C.Text(442,115,"NIGHT",Colour(122,119,112),8,false);
}
inline float IlluminanceY(float T){return 76-56*(1-std::cos(T*Pi/2));}
inline void Illuminance(Canvas C,float Intensity,bool Bound=true) {
    const float T=Intensity/150;
    for(float Y:{24.f,50.f,77.f})C.Dash({14,Y},{346,Y},Colour(255,255,255,11/255.f),1,2,5);
    std::array<ImVec2,61> All,Selected;
    for(int I=0;I<=60;++I){float A=I/60.f;All[I]={14+A*332,IlluminanceY(A)};Selected[I]={14+T*A*332,IlluminanceY(T*A)};}
    C.Stroke(All,Colour(89,82,70),1.2f);
    if(!Bound)return; // Preserve the reference curve, without inventing a selected klux value.
    const float X=14+T*332,Y=IlluminanceY(T);
    // SVG fill: selected path followed by L(x,88) H14 Z. Gradient uses its own bounds.
    const ImVec2 UV=ImGui::GetFontTexUvWhitePixel();
    if(T>0)for(int I=1;I<=60;++I){
        auto A=Selected[I-1],B=Selected[I];
        ImVec2 V[4]={C.P(A.x,A.y),C.P(B.x,B.y),C.P(B.x,88),C.P(A.x,88)};
        C.D->PrimReserve(6,4);unsigned Base=C.D->_VtxCurrentIdx;
        for(unsigned J:{0u,1u,2u,0u,2u,3u})C.D->PrimWriteIdx(ImDrawIdx(Base+J));
        C.D->PrimWriteVtx(V[0],UV,Colour(216,189,130,.2f*(88-A.y)/(88-Y)));
        C.D->PrimWriteVtx(V[1],UV,Colour(216,189,130,.2f*(88-B.y)/(88-Y)));
        C.D->PrimWriteVtx(V[2],UV,Colour(216,189,130,0));C.D->PrimWriteVtx(V[3],UV,Colour(216,189,130,0));
    }
    C.Stroke(Selected,Colour(218,193,139),1.6f);
    C.Dash({X,Y+6},{X,88},Colour(200,175,122,.35f),1,2,4);
    C.Circle(X,Y,10,Colour(232,199,106,.07f));C.Circle(X,Y,3.4f,Colour(239,226,194));
}

}
