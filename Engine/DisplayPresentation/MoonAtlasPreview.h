#pragma once
#include "MoonConstantRecord.h"
#include "SunColourTemperature.h"
#include <algorithm>
namespace Frontier {
// Real registered atlas pixels and the same CPU moon evaluator as the scene.
// Fixed display radius keeps the sphere legible; it is not the authored angular size.
inline void MoonAtlasPreview(unsigned char* Pixels,int Side,const MoonAlbedoView& View,const float Tint[3],float Tilt,float Haze,float Gamma,float Phase,float Roll,float Pitch,float Brightness) noexcept {
 MoonDrawEntry E;E.Albedo=View;E.AngularRadius=.5f;E.Phase=MoonPhaseToReference(Phase);E.Roll=Roll*3.14159265358979323846f/180;E.Tilt=(Tilt-Pitch)*3.14159265358979323846f/180;E.Haze=Haze;E.Gamma=Gamma;E.Brightness=Brightness;E.Glow=0;for(int I=0;I<3;++I)E.Tint[I]=Tint[I];
 const float Sin=std::sin(E.AngularRadius),Transmission[]={1,1,1};
 for(int Y=0;Y<Side;++Y)for(int X=0;X<Side;++X){auto* P=Pixels+(Y*Side+X)*4;float U=(X+.5f)*2/Side-1,V=(Y+.5f)*2/Side-1,R=U*U+V*V;
  if(!View.Texels||!View.Width||!View.Height||R>1){for(int C=0;C<4;++C)P[C]=0;continue;}
  float Direction[]={-U*Sin,-V*Sin,std::sqrt(std::max(0.f,1-R*Sin*Sin))},RGB[3];EvaluateMoons(&E,1,Direction,Transmission,RGB);
  for(int C=0;C<3;++C){float L=std::max(0.f,RGB[C]);P[C]=static_cast<unsigned char>(255*SunColourTemperature::DisplayChannel(L/(1+L))+.5f);}P[3]=255;
 }
}
}
