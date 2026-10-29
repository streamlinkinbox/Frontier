/*==========================================================================================================================================
                                                  SOLAR PIXEL FILTER VALIDATION
==========================================================================================================================================*/
// 🧩 Compare solar pixel integrals against numerical quadrature and flux constraints.

#include "Engine/Rendering/SolarDiscFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

// Independent Cartesian source integration, used only as a high-resolution numerical comparison.
static double CartesianReference(double Radius, double Sigma, double Dx, double Dy, double Pitch,
                                 const Frontier::SunDiscProfile& Profile)
{
    constexpr int Grid = 600;
    constexpr double RootTwo = 1.4142135623730951;
    double WeightedMass = 0.0, WeightSum = 0.0;
    for (int Y = 0; Y < Grid; ++Y) for (int X = 0; X < Grid; ++X)
    {
        const double U = 2.0*(X+0.5)/Grid-1.0, V = 2.0*(Y+0.5)/Grid-1.0;
        const double R2 = U*U+V*V;
        if (R2 >= 1.0) continue;
        const double Mu = std::sqrt(1.0-R2), D = 1.0-Mu;
        const double Weight = 1.0 - Profile.LimbLinear*D - Profile.LimbQuadratic*D*D;
        const double X0 = (Dx-Pitch*0.5-U*Radius)/(RootTwo*Sigma);
        const double X1 = (Dx+Pitch*0.5-U*Radius)/(RootTwo*Sigma);
        const double Y0 = (Dy-Pitch*0.5-V*Radius)/(RootTwo*Sigma);
        const double Y1 = (Dy+Pitch*0.5-V*Radius)/(RootTwo*Sigma);
        WeightedMass += Weight * 0.25 * (std::erf(X1)-std::erf(X0)) * (std::erf(Y1)-std::erf(Y0));
        WeightSum += Weight;
    }
    return WeightedMass / (WeightSum*Pitch*Pitch);
}

int main()
{
    using namespace Frontier;
    constexpr double Pitch = 0.001;
    const double Radii[] = {0.1, 0.4, 1.0, 2.0, 3.5, 4.0, 4.1, 5.0, 8.0, 16.0, 32.0};
    const double Offsets[] = {0.0, 0.17, 0.49};
    double WorstSmall = 0, WorstResolved = 0, WorstNegative = 0;
    bool Finite = true;
    for (double RadiusPx : Radii) for (double Offset : Offsets)
    {
        SunDiscProfile Profile; Profile.AngularRadiusRad = RadiusPx * Pitch; Profile.EdgeBlurRad = 0.6 * Pitch;
        SolarDiscPixelFilter Filter(Profile);
        const int Extent = static_cast<int>(std::ceil(RadiusPx + 8.0));
        double Flux = 0.0;
        for (int Y = -Extent; Y <= Extent; ++Y) for (int X = -Extent; X <= Extent; ++X)
        {
            const auto R = Filter.Evaluate((X-Offset)*Pitch, (Y+0.31)*Pitch, Pitch, Pitch, {1,0.5,0.25});
            Finite &= std::isfinite(R.R) && std::isfinite(R.G) && std::isfinite(R.B);
            WorstNegative = std::min(WorstNegative, R.R);
            Flux += R.R * Pitch * Pitch;
        }
        const double Error = std::abs(Flux-1.0);
        if (RadiusPx <= 3.0) WorstSmall = std::max(WorstSmall, Error);
        else WorstResolved = std::max(WorstResolved, Error);
        std::printf("radius=%5.1f px offset=%.2f flux=%.9g error=%.6g\n", RadiusPx, Offset, Flux, Error);
    }
    double WorstReference = 0.0;
    for (double RadiusPx : {0.4, 1.0, 2.8})
    {
        SunDiscProfile Profile; Profile.AngularRadiusRad = RadiusPx*Pitch; Profile.EdgeBlurRad = 0.6*Pitch;
        SolarDiscPixelFilter Filter(Profile);
        const double Peak = Filter.Evaluate(0,0,Pitch,Pitch,{1,1,1}).R;
        for (double X : {0.0, 0.7*RadiusPx, RadiusPx, RadiusPx+0.6})
        {
            const double Y = 0.23*Pitch;
            const double Estimate = Filter.Evaluate(X*Pitch,Y,Pitch,Pitch,{1,1,1}).R;
            const double Reference = CartesianReference(RadiusPx*Pitch,0.6*Pitch,X*Pitch,Y,Pitch,Profile);
            WorstReference = std::max(WorstReference, std::abs(Estimate-Reference)/Peak);
        }
    }
    SunDiscProfile Near; Near.AngularRadiusRad = 0.004; Near.EdgeBlurRad = 0.0006;
    SolarDiscPixelFilter Below(Near); Near.AngularRadiusRad += 1e-9; SolarDiscPixelFilter Above(Near);
    const double BoundaryPeak = Below.Evaluate(0,0,Pitch,Pitch,{1,1,1}).R;
    double BoundaryJump = 0.0;
    for (int Y = -6; Y <= 6; ++Y) for (int X = -6; X <= 6; ++X)
    {
        const double A = Below.Evaluate((X-0.13)*Pitch,(Y+0.2)*Pitch,Pitch,Pitch,{1,1,1}).R;
        const double B = Above.Evaluate((X-0.13)*Pitch,(Y+0.2)*Pitch,Pitch,Pitch,{1,1,1}).R;
        BoundaryJump = std::max(BoundaryJump,std::abs(A-B)/BoundaryPeak);
    }
    SunDiscProfile P; P.AngularRadiusRad = 0.0015; P.EdgeBlurRad = 0.0006;
    SolarDiscPixelFilter Filter(P);
    const auto Centre = Filter.Evaluate(0,0,Pitch,Pitch,{1,1,1});
    const auto Right = Filter.Evaluate(0.002,0,Pitch,Pitch,{1,1,1});
    const auto Left = Filter.Evaluate(-0.002,0,Pitch,Pitch,{1,1,1});
    const auto Far = Filter.Evaluate(0.02,0,Pitch,Pitch,{1,1,1});
    const double Symmetry = std::abs(Right.R-Left.R) / std::max(1.0, Centre.R);
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    SunDiscProfile Invalid = P; Invalid.AngularRadiusRad = NaN;
    SolarDiscPixelFilter InvalidFilter(Invalid);
    const bool Guarded = InvalidFilter.Evaluate(0,0,Pitch,Pitch,{1,1,1}).R == 0.0 &&
                         Filter.Evaluate(NaN,0,Pitch,Pitch,{1,1,1}).R == 0.0 &&
                         Filter.Evaluate(0,0,Pitch,Pitch,{-1,1,1}).R == 0.0 &&
                         Filter.Evaluate(0,0,0,Pitch,{1,1,1}).R == 0.0;
    const bool Success = Finite && Guarded && WorstNegative >= -1e-12 && WorstSmall < 1e-5 &&
                         WorstResolved < 0.001 && WorstReference < 0.01 && BoundaryJump < 1e-4 &&
                         Symmetry < 1e-7 && Centre.R > Right.R && Far.R == 0;
    std::printf("%s finite, non-negative, symmetric, decreasing; worst flux error small %.9g resolved %.9g, symmetry %.9g\n",
                Success ? "PASS" : "FAIL", WorstSmall, WorstResolved, Symmetry);
    std::printf("Cartesian 600x600 quadrature max error / peak %.9g; blended transition jump / peak %.9g\n",
                WorstReference, BoundaryJump);
    return Success ? 0 : 1;
}
