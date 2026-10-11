#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::SunColourTemperature {
constexpr float MinimumKelvin=2000.0f, MaximumKelvin=10000.0f, DefaultKelvin=6500.0f;
inline float Clamp(float Kelvin) noexcept {
    return std::isfinite(Kelvin)?std::clamp(Kelvin,MinimumKelvin,MaximumKelvin):DefaultKelvin;
}
// Planckian-locus polynomial approximation in CIE 1931 xy, within the UI's 2000–10000 K range.
// This is a chromaticity/tint approximation, not a spectral or photometrically calibrated renderer.
inline std::array<float,2> Chromaticity(float Kelvin) noexcept {
    const double T=Clamp(Kelvin),I=1.0/T;
    const double X=T<=4000 ? -0.2661239e9*I*I*I-0.2343580e6*I*I+0.8776956e3*I+0.179910
                           : -3.0258469e9*I*I*I+2.1070379e6*I*I+0.2226347e3*I+0.240390;
    const double Y=T<=2222 ? -1.1063814*X*X*X-1.34811020*X*X+2.18555832*X-0.20219683
                 : T<=4000 ? -0.9549476*X*X*X-1.37418593*X*X+2.09137015*X-0.16748867
                           : 3.0817580*X*X*X-5.87338670*X*X+3.75112997*X-0.37001483;
    return {static_cast<float>(X),static_cast<float>(Y)};
}
inline std::array<float,3> LinearRgb(float Kelvin) noexcept {
    const auto XY=Chromaticity(Kelvin);
    const double X=XY[0]/XY[1],Y=1.0,Z=(1.0-XY[0]-XY[1])/XY[1];
    // XYZ -> linear sRGB/Rec.709. Do not apply sRGB transfer to renderer inputs.
    std::array<float,3> C={static_cast<float>(3.2406*X-1.5372*Y-0.4986*Z),
                          static_cast<float>(-0.9689*X+1.8758*Y+0.0415*Z),
                          static_cast<float>(0.0557*X-0.2040*Y+1.0570*Z)};
    for(auto& V:C)V=std::max(V,0.0f);
    const float Peak=std::max({C[0],C[1],C[2]});
    for(auto& V:C)V/=Peak;
    return C; // Peak-normalized tint, matching the existing native RGB-tint convention.
}
inline float DisplayChannel(float Linear) noexcept {
    const float C=std::clamp(Linear,0.0f,1.0f);
    return C<=0.0031308f?12.92f*C:1.055f*std::pow(C,1.0f/2.4f)-0.055f;
}
}
