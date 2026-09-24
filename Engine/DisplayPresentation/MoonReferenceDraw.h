#pragma once
// Native transcription of moon-controls.jsx: reference diagrams, not rendered atlas imagery.
#include "SunReferenceDraw.h"
namespace Frontier::MoonReference {
using namespace SunReference;
inline ImU32 Mix(ImU32 A,ImU32 B,float T){T=std::clamp(T,0.f,1.f);ImU32 C=0;for(unsigned I=0;I<4;++I){unsigned X=(A>>(I*8))&255,Y=(B>>(I*8))&255;C|=unsigned(X+(float(Y)-X)*T+.5f)<<(I*8);}return C;}
inline void Radial(Canvas C,float X,float Y,float R,ImU32 A,ImU32 B,ImU32 End,float Stop=.6f,float Fx=.28f,float Fy=.24f,float Radius=.82f){
 auto ColourAt=[&](float U,float V){float D=std::hypot((U+1)*.5f-Fx,(V+1)*.5f-Fy)/Radius;return D<Stop?Mix(A,B,D/Stop):Mix(B,End,(D-Stop)/(1-Stop));};
 auto UV=ImGui::GetFontTexUvWhitePixel();
 for(int Ring=0;Ring<16;++Ring)for(int I=0;I<64;++I){float R0=Ring/16.f,R1=(Ring+1)/16.f,T=I*2*Pi/64,TT=(I+1)*2*Pi/64;ImVec2 P[]={{R0*std::cos(T),R0*std::sin(T)},{R1*std::cos(T),R1*std::sin(T)},{R1*std::cos(TT),R1*std::sin(TT)},{R0*std::cos(TT),R0*std::sin(TT)}};
 C.D->PrimReserve(6,4);unsigned Base=C.D->_VtxCurrentIdx;for(unsigned J:{0u,1u,2u,0u,2u,3u})C.D->PrimWriteIdx(ImDrawIdx(Base+J));for(auto P0:P)C.D->PrimWriteVtx(C.P(X+R*P0.x,Y+R*P0.y),UV,ColourAt(P0.x,P0.y));}
}
inline std::array<float,3> Project(float Az,float El){float A=Az*Pi/180,B=El*Pi/180,X=std::sin(A)*std::cos(B),Z=std::cos(A)*std::cos(B),Y=std::sin(B);return {180+85*X,117-85*(Y*std::cos(.34f)-Z*std::sin(.34f)),Z*std::cos(.34f)+Y*std::sin(.34f)};}
inline void Sphere(Canvas C,float Az,float El,float Roll,float Size,ImTextureRef Texture,bool Ready){
 C.Ellipse(180,117,102,102,Colour(145,169,192,.133f),false);
 for(int I=0;I<36;++I){float A=I*Pi/18;C.Line({180+102*std::sin(A),117-102*std::cos(A)},{180+(102-(I%3==0?5:2))*std::sin(A),117-(102-(I%3==0?5:2))*std::cos(A)},Colour(128,152,176,.45f),.7f);}
 Radial(C,180,117,85,Colour(69,82,99),Colour(37,47,60),Colour(21,27,37));C.Ellipse(180,117,85,85,Colour(120,151,186,.333f),false);
 std::array<ImVec2,73> Line;
 for(float Lat:{-60.f,-30.f,0.f,30.f,60.f}){for(int I=0;I<73;++I){auto P=Project(I*5.f,Lat);Line[I]={P[0],P[1]};}C.Stroke(Line,Lat==0?Colour(181,213,240,.55f):Colour(122,153,181,.2f),Lat==0?1.1f:.6f);}
 for(float Lon:{0.f,30.f,60.f,90.f,120.f,150.f}){for(int I=0;I<73;++I){auto P=Project(Lon,I*5.f);Line[I]={P[0],P[1]};}C.Stroke(Line,Colour(144,175,199,.19f),.6f);}
 auto P=Project(Az,El);std::array<ImVec2,37> Arc;for(int I=0;I<37;++I){auto Q=Project(Az,I*El/36);Arc[I]={Q[0],Q[1]};}C.Stroke(Arc,Colour(170,207,235),1.4f,P[2]<0?3:0,3);C.Dash({180,117},{P[0],P[1]},Colour(200,222,242,.35f),1,2,4);
 C.Circle(P[0],P[1],17,Colour(182,210,237,.047f));float Radius=6+std::sqrt(std::min(Size,12.f))*3;
 if(Ready)C.D->AddImage(Texture,C.P(P[0]-Radius,P[1]-Radius),C.P(P[0]+Radius,P[1]+Radius));
 float A=Roll*Pi/180;C.Line({P[0]+11*std::sin(A),P[1]-11*std::cos(A)},{P[0]+15*std::sin(A),P[1]-15*std::cos(A)},Colour(198,220,237));C.Circle(180,117,2,Colour(197,216,229));
 C.Text(180,9,"N",Colour(140,162,189),9);C.Text(68,121,"W",Colour(140,162,189),9);C.Text(292,121,"E",Colour(140,162,189),9);C.Text(180,231,"S",Colour(140,162,189),9);C.Text(180,252,P[2]<0?"Far side · dashed guide":"Near side · drag to orbit",Colour(140,162,189),9);
}
}
