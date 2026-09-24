#pragma once
#include "WindField.h"
#include "Precipitation.h"
#include "AtmosphericOptics.h"
#include <algorithm>
namespace Frontier {
struct WeatherDiagnostics {
 static float RainVisibility(const PrecipitationSettings& P,bool Visible) noexcept {
  if(!Visible||!P.Enabled)return 0;
  if(P.Category==PrecipitationCategory::Rain)return std::clamp(P.RateMillimetresPerHour/10.f,0.f,1.f);
  if(P.Category==PrecipitationCategory::Drizzle)return std::clamp(P.RateMillimetresPerHour/20.f,0.f,.5f);
  return 0; // Ice does not produce the liquid-water rainbow.
 }
 // Fixed diagnostic camera: sun 10 degrees high behind viewer, rain column 500 m.
 // X/Y are normalized image coordinates. This is not the scene-camera renderer.
 static void RainbowPixel(const RainbowSettings& B,float X,float Y,float RGB[3]) noexcept {
  constexpr float Pi=3.14159265359f,Elev=10*Pi/180;
  float DX=(X-.5f)*120*Pi/180,DZ=(1-Y)*65*Pi/180;
  float Angle=std::sqrt(DX*DX+DZ*DZ),S=Angle>0?std::sin(Angle)/Angle:1;
  float Sun[]={0,-std::cos(Elev),std::sin(Elev)};
  float D[]={DX*S,std::cos(Angle)*std::cos(Elev)+DZ*S*std::sin(Elev),-std::cos(Angle)*std::sin(Elev)+DZ*S*std::cos(Elev)};
  float Bow[3];AtmosphericOptics::Rainbow(B,D,Sun,1,500,Bow);
  float Band=AtmosphericOptics::AlexanderAttenuation(B,Angle);
  for(int C=0;C<3;++C)RGB[C]=D[2]>=0?std::clamp((.035f+.008f*C)*Band+Bow[C],0.f,1.f):.035f;
 }
};
}
