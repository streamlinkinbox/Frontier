#pragma once
#include <algorithm>
#include <cmath>
namespace Frontier {
inline float StarMinimumLuminance(float Magnitude) noexcept {return std::pow(10.f,-.4f*Magnitude);}
// Same normalized signal as celestial-controls.jsx. Direction phase keeps binned duplicates identical.
inline float StarTwinkleFlux(float Seconds,float Depth,float Rate,float Phase=0) noexcept {
 constexpr float Tau=6.2831853071795864769f;float T=(Seconds+Phase)*Tau*Rate;
 return 1-std::clamp(Depth,0.f,1.f)*(.5f+.3f*std::sin(T)+.2f*std::sin(T*.47f+.8f));
}
inline float StarDirectionPhase(float X,float Y,float Z) noexcept {return X*13+Y*37+Z*71;}
}
