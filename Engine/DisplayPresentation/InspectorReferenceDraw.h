#pragma once
// Geometry transcribed from the existing HTML inspector, not replacement artwork.
// Iris: property-graphics.jsx (220x220). Focus: camera-graphics.jsx (360x174).
// Weather marks: lucide-react 0.468.0 CloudRain / CloudSnow / CloudHail (24x24).
// See InspectorReferenceDraw.NOTICE for the Lucide license.
#include "SunReferenceDraw.h"
#include <cstdio>
namespace Frontier::InspectorReference {
using SunReference::Canvas;using SunReference::Colour;using SunReference::Pi;
inline float IrisRadius(float Aperture){return 10+std::clamp((16/Aperture-1)/(16/1.4f-1),0.f,1.f)*47;}
inline float FocusX(float Metres){return 22+std::log1p(std::clamp(Metres,0.f,100.f))/std::log(101.f)*316;}
inline float FocusFromX(float X){return std::round(std::clamp(std::expm1((X-22)/316*std::log(101.f)),1.f,100.f));}
inline void Iris(Canvas C,float Aperture){
 C.Circle(110,110,97,Colour(23,23,23));C.D->AddCircle(C.P(110,110),97*C.Scale,Colour(82,80,82),96,C.Scale);
 for(int I=0;I<=64;++I){float T=I/64.f;C.Circle(110,110,88*(1-T),Colour(unsigned(35+32*T),unsigned(34+30*T),unsigned(37+34*T)));}
 C.D->AddCircle(C.P(110,110),88*C.Scale,Colour(69,66,71),96,C.Scale);
 for(int I=0;I<48;++I){float A=I*Pi/24-Pi/2;C.Line({110+94*std::cos(A),110+94*std::sin(A)},{110+90*std::cos(A),110+90*std::sin(A)},Colour(122,116,126),.6f);}
 float R=IrisRadius(Aperture);auto Point=[&](float A,float Radius){return C.P(110+std::cos(A)*Radius,110+std::sin(A)*Radius);};
 for(int I=0;I<8;++I){float A=I*Pi/4;std::array<ImVec2,19> P;P[0]=Point(A,R);P[1]=Point(A+Pi/4,R);for(int J=0;J<=16;++J)P[J+2]=Point(A+1.07f-.78f*J/16,86);
  // SVG's blade outline is concave and counter-clockwise; ImGui fill expects clockwise.
  std::reverse(P.begin(),P.end());C.D->AddConcavePolyFilled(P.data(),int(P.size()),I%2?Colour(57,54,62):Colour(52,50,56));C.D->AddPolyline(P.data(),int(P.size()),Colour(121,113,126),.65f*C.Scale,ImDrawFlags_Closed);
 }
 std::array<ImVec2,8> Hole;for(int I=0;I<8;++I)Hole[I]=Point(I*Pi/4,R);C.D->AddConvexPolyFilled(Hole.data(),8,Colour(16,16,20));C.D->AddPolyline(Hole.data(),8,Colour(183,167,196),C.Scale,ImDrawFlags_Closed);C.Circle(110,110,R*.55f,Colour(150,144,182,7/255.f));C.Circle(110,110,2,Colour(193,182,205,.6f));
}
inline void FocusTarget(Canvas C,float X,float DX,float DY,float Opacity){
 auto Col=Colour(170,187,209,Opacity);C.Line({X-3+DX,62+DY},{X+3+DX,62+DY},Col,.7f);C.Line({X+DX,55+DY},{X+DX,93+DY},Col,.7f);C.Line({X-3+DX,86+DY},{X+3+DX,86+DY},Col,.7f);
}
inline void Focus(Canvas C,float Subject,float Near,float Far){
 const float N=FocusX(Near),F=FocusX(Far),P=FocusX(Subject);
 C.D->AddRectFilled(C.P(22,31),C.P(338,121),Colour(24,25,28),9*C.Scale);C.D->AddRect(C.P(22,31),C.P(338,121),Colour(255,255,255,8/255.f),9*C.Scale,C.Scale);
 C.D->AddRectFilled(C.P(N,32),C.P(N+std::max(1.f,F-N),120),Colour(142,172,211,35/255.f));for(float X:{N,F})C.Dash({X,32},{X,120},Colour(163,186,219,.55f),1,3,5);
 for(float M:{1.f,3.f,10.f,30.f,100.f}){float X=FocusX(M);C.Line({X,120},{X,125},Colour(110,120,138));char T[12];std::snprintf(T,sizeof(T),"%.0f m",double(M));C.Text(X,144,T,Colour(126,137,155),9);}
 for(int I=0;I<17;++I){float X=30+I*18.5f;if(X>=N&&X<=F)FocusTarget(C,X,0,0,.75f);else{
   // Native approximation of the reference's CSS Gaussian blur(1.7px).
   float Sum=0;for(int Y=-4;Y<=4;++Y)for(int J=-4;J<=4;++J)Sum+=std::exp(-(J*J+Y*Y)*.125f);
   for(int Y=-4;Y<=4;++Y)for(int J=-4;J<=4;++J)FocusTarget(C,X,J*.85f,Y*.85f,.2f*std::exp(-(J*J+Y*Y)*.125f)/Sum);
  }}
 C.Line({P,21},{P,121},Colour(210,223,241),1.2f);C.D->AddTriangleFilled(C.P(P-5,16),C.P(P+5,16),C.P(P,23),Colour(210,223,241));C.Circle(P,76,9,Colour(31,41,55));C.D->AddCircle(C.P(P,76),9*C.Scale,Colour(196,215,238),40,C.Scale);C.Line({P-3,76},{P+3,76},Colour(227,237,247));C.Line({P,73},{P,79},Colour(227,237,247));C.Text(22,168,"DISTANCE · LOG SCALE",Colour(126,137,155),8,false);
}
inline void RoundedLine(Canvas C,ImVec2 A,ImVec2 B,ImU32 Colour){C.Line(A,B,Colour,2);C.Circle(A.x,A.y,1,Colour);C.Circle(B.x,B.y,1,Colour);}
inline void CloudArc(Canvas C,ImVec2 A,ImVec2 B,float R,bool Large,bool Sweep){
 float DX=(A.x-B.x)/2,DY=(A.y-B.y)/2,D2=DX*DX+DY*DY,K=(Large==Sweep?-1.f:1.f)*std::sqrt(std::max(0.f,(R*R-D2)/D2));float X=(A.x+B.x)/2+K*DY,Y=(A.y+B.y)/2-K*DX;
 float Start=std::atan2(A.y-Y,A.x-X),End=std::atan2(B.y-Y,B.x-X),Delta=End-Start;if(Sweep&&Delta<0)Delta+=2*Pi;if(!Sweep&&Delta>0)Delta-=2*Pi;for(int I=1;I<=48;++I){float T=Start+Delta*I/48;C.D->PathLineTo(C.P(X+R*std::cos(T),Y+R*std::sin(T)));}
}
inline void WeatherType(Canvas C,unsigned Kind,ImU32 Col){
 C.D->PathLineTo(C.P(4,14.899f));CloudArc(C,{4,14.899f},{15.71f,8},7,true,true);C.D->PathLineTo(C.P(17.5f,8));CloudArc(C,{17.5f,8},{20,16.242f},4.5f,false,true);C.D->PathStroke(Col,2*C.Scale);C.Circle(4,14.899f,1,Col);C.Circle(20,16.242f,1,Col);
 if(Kind==3){for(ImVec2 P:std::array<ImVec2,6>{{{8,15},{8,19},{12,17},{12,21},{16,15},{16,19}}})RoundedLine(C,P,{P.x+.01f,P.y},Col);}
 else if(Kind==2||Kind==4){for(float X:{8.f,16.f}){RoundedLine(C,{X,14},{X,16},Col);RoundedLine(C,{X,20},{X+.01f,20},Col);}RoundedLine(C,{12,16},{12,18},Col);RoundedLine(C,{12,22},{12.01f,22},Col);}
 else{RoundedLine(C,{16,14},{16,20},Col);RoundedLine(C,{8,14},{8,20},Col);RoundedLine(C,{12,16},{12,22},Col);}
}
}
