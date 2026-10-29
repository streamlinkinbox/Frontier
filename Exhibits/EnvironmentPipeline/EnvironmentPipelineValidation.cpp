/*==========================================================================================================================================
                                               ENVIRONMENT PIPELINE VALIDATION (EXHIBIT)
                                     ------------------------------------------------------------------
                 Phase 6 portable cores vs analytic oracles: atmosphere, celestial, fog, clouds, weather.
==========================================================================================================================================*/

// Analytic anchors: exponential-atmosphere column depths (tau = beta * H vertically), phase-function
// normalisation, closed-form height-fog integrals cross-checked by independent quadrature, Beer-Lambert
// through uniform cloud slabs, known Julian/solstice/equinox geometry, exact wrap identities for the
// analytic precipitation volume, and endpoint/count exactness for lightning channels.
// Usage: EnvironmentPipelineValidation [ppm-prefix]  (writes <prefix>sky-strip.ppm)

#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/BarrierDerivation.h"
#include "Engine/Rendering/CelestialModel.h"
#include "Engine/Rendering/CloudField.h"
#include "Engine/Rendering/EnvironmentUnits.h"
#include "Engine/Rendering/FogMedium.h"
#include "Engine/Rendering/LensFlareModel.h"
#include "Engine/Rendering/TransientTargetAtlas.h"
#include "Engine/Rendering/WeatherEffects.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

int Failures = 0;
int Total    = 0;
constexpr double Pi = 3.14159265358979323846;
constexpr double D2R = Pi / 180.0;

void Check(bool Condition, const char* Label)
{
    ++Total;
    if (!Condition)
    {
        ++Failures;
        std::printf("FAIL  %s\n", Label);
    }
}
void Print(const char* Format, ...)
{
    va_list Args; va_start(Args, Format); std::vprintf(Format, Args); va_end(Args);
}
bool Near(double A, double B, double Tol) { return std::abs(A - B) <= Tol; }
bool NearRel(double A, double B, double RelTol) { return std::abs(A - B) <= RelTol * std::max(std::abs(B), 1e-30); }

SpatialVector DirectionFromElevationAzimuth(double ElevationDeg, double AzimuthDeg)
{
    const double El = ElevationDeg * D2R, Az = AzimuthDeg * D2R;
    return {std::cos(El) * std::sin(Az), std::cos(El) * std::cos(Az), std::sin(El)};   // local: X east, Y north, Z up
}

// Planet-centred frame matching AtmosphereModel's canonical frame: observer on +Y.
// Local (east, north, up) at the observer maps to world (X east, Z north, Y up).
SpatialVector LocalToPlanet(const SpatialVector& Local, double RadiusKm)
{
    (void)RadiusKm;
    return {Local.X, Local.Z, Local.Y};
}

} // namespace

int main(int Argc, char** Argv)
{
    const std::string PpmPrefix = Argc > 1 ? Argv[1] : "";
    const AtmosphereParameters Atmo{};

    // ------------------------------------------------------------------ atmosphere medium
    {
        Check(Near(RayleighDensity(Atmo, 0.0), 1.0, 1e-15), "density: Rayleigh sea level = 1");
        Check(Near(RayleighDensity(Atmo, 8.0), std::exp(-1.0), 1e-12), "density: Rayleigh at H = 1/e");
        Check(Near(MieDensity(Atmo, 1.2), std::exp(-1.0), 1e-12), "density: Mie at H = 1/e");
        Check(Near(OzoneDensity(Atmo, 25.0), 1.0, 1e-15) && Near(OzoneDensity(Atmo, 10.0), 0.0, 1e-15) &&
              Near(OzoneDensity(Atmo, 40.0), 0.0, 1e-15) && Near(OzoneDensity(Atmo, 17.5), 0.5, 1e-12),
              "density: ozone tent shape");

        // Vertical transmittance from the ground: tau_R = beta*H_R, tau_M = beta_ext*H_M,
        // tau_O3 = beta * (tent area = width) exactly (triangle: 0.5 * 30 km * 1 = 15 km).
        const double TauR = 8.0, TauM = 1.2, TauO = 15.0;
        const double MieExt = Atmo.MieScattering + Atmo.MieAbsorption;
        const SpectrumRgb Expected{
            std::exp(-(Atmo.RayleighScattering.R * TauR + MieExt * TauM + Atmo.OzoneAbsorption.R * TauO)),
            std::exp(-(Atmo.RayleighScattering.G * TauR + MieExt * TauM + Atmo.OzoneAbsorption.G * TauO)),
            std::exp(-(Atmo.RayleighScattering.B * TauR + MieExt * TauM + Atmo.OzoneAbsorption.B * TauO))};
        const SpectrumRgb Numeric = TransmittanceToTop(Atmo, Atmo.GroundRadiusKm, 1.0, 512);
        Print("vertical transmittance: numeric (%.5f, %.5f, %.5f) vs analytic (%.5f, %.5f, %.5f)\n",
              Numeric.R, Numeric.G, Numeric.B, Expected.R, Expected.G, Expected.B);
        Check(NearRel(Numeric.R, Expected.R, 0.01) && NearRel(Numeric.G, Expected.G, 0.01) &&
              NearRel(Numeric.B, Expected.B, 0.01), "transmittance: vertical column matches analytic optical depth");

        // Monotone toward the horizon + wavelength ordering (blue dies first).
        const SpectrumRgb T1 = TransmittanceToTop(Atmo, Atmo.GroundRadiusKm, 1.0);
        const SpectrumRgb T2 = TransmittanceToTop(Atmo, Atmo.GroundRadiusKm, 0.5);
        const SpectrumRgb T3 = TransmittanceToTop(Atmo, Atmo.GroundRadiusKm, 0.02);
        Check(T1.B > T2.B && T2.B > T3.B, "transmittance: decreases toward horizon");
        Check(T3.R > T3.G && T3.G > T3.B, "transmittance: horizon path is reddened (R>G>B)");
        Check(T3.B < 0.05, "transmittance: blue nearly extinct on the grazing path");

        // Phase-function normalisation by independent quadrature (Simpson on mu).
        const auto IntegratePhase = [](auto&& Phase) {
            const int N = 8000;
            double Sum = 0.0;
            for (int I = 0; I <= N; ++I)
            {
                const double Mu = -1.0 + 2.0 * static_cast<double>(I) / N;
                const double W = (I == 0 || I == N) ? 1.0 : (I % 2 ? 4.0 : 2.0);
                Sum += W * Phase(Mu);
            }
            return 2.0 * Pi * Sum * (2.0 / N) / 3.0;
        };
        Check(Near(IntegratePhase([](double Mu) { return RayleighPhase(Mu); }), 1.0, 0.005),
              "phase: Rayleigh integrates to 1");
        Check(Near(IntegratePhase([](double Mu) { return HenyeyGreensteinPhase(Mu, 0.8); }), 1.0, 0.02),
              "phase: Henyey-Greenstein (g=0.8) integrates to 1");
        Check(HenyeyGreensteinPhase(1.0, 0.8) > HenyeyGreensteinPhase(-1.0, 0.8) * 50.0,
              "phase: HG strongly forward-peaked at g=0.8");

        // LUT fidelity.
        TransmittanceLut Lut;
        Check(Lut.Bake(Atmo, 128, 32, 48), "LUT: bake builds");
        Check(!TransmittanceLut{}.Bake(Atmo, 1, 32), "LUT: degenerate size refused");
        double MeanErr = 0.0, MaxErr = 0.0; int Samples = 0;
        for (double Alt : {0.0, 2.0, 10.0, 40.0, 90.0})
        {
            for (double Mu : {-0.2, 0.1, 0.3, 0.6, 0.9, 1.0})
            {
                const double Radius = Atmo.GroundRadiusKm + Alt;
                const SpectrumRgb Direct = TransmittanceToTop(Atmo, Radius, Mu, 256);
                const SpectrumRgb ViaLut = Lut.Sample(Atmo, Radius, Mu);
                const double Err = std::max({std::abs(Direct.R - ViaLut.R) / std::max(Direct.R, 1e-6),
                                             std::abs(Direct.G - ViaLut.G) / std::max(Direct.G, 1e-6),
                                             std::abs(Direct.B - ViaLut.B) / std::max(Direct.B, 1e-6)});
                MeanErr += Err; MaxErr = std::max(MaxErr, Err); ++Samples;
            }
        }
        MeanErr /= Samples;
        Print("LUT vs direct: mean rel err %.3f%%, max %.3f%%\n", 100.0 * MeanErr, 100.0 * MaxErr);
        Check(MeanErr < 0.03, "LUT: mean error under 3% across altitude/angle grid");
        const TransmittanceLut Empty;
        const SpectrumRgb Fallback = Empty.Sample(Atmo, Atmo.GroundRadiusKm, 0.7);
        Check(Near(Fallback.G, TransmittanceToTop(Atmo, Atmo.GroundRadiusKm, 0.7).G, 1e-12),
              "LUT: unbaked sample falls back to direct integration");
    }

    // ------------------------------------------------------------------ sky scattering behaviour
    {
        const SpatialVector Observer{0.0, Atmo.GroundRadiusKm + 0.002, 0.0};
        const SpectrumRgb SunIrradiance{1.0, 1.0, 1.0};
        TransmittanceLut Lut;
        (void)Lut.Bake(Atmo, 128, 32, 48);

        // Noon: sun near zenith; look 45 degrees up, away from the sun azimuth.
        const SpatialVector SunNoon = LocalToPlanet(DirectionFromElevationAzimuth(80.0, 180.0), 0.0);
        const SpatialVector View45  = LocalToPlanet(DirectionFromElevationAzimuth(45.0, 0.0), 0.0);
        const SpectrumRgb NoonSky = SingleScattering(Atmo, Observer, View45, SunNoon, SunIrradiance, &Lut, 32);
        Print("noon sky (45 deg): R %.4f G %.4f B %.4f\n", NoonSky.R, NoonSky.G, NoonSky.B);
        Check(NoonSky.B > NoonSky.G && NoonSky.G > NoonSky.R, "sky: noon is blue (B>G>R)");

        // Sunset: sun on the horizon; look toward it slightly above the horizon.
        const SpatialVector SunSet  = LocalToPlanet(DirectionFromElevationAzimuth(0.5, 270.0), 0.0);
        const SpatialVector ViewSun = LocalToPlanet(DirectionFromElevationAzimuth(3.0, 270.0), 0.0);
        const SpectrumRgb SunsetSky = SingleScattering(Atmo, Observer, ViewSun, SunSet, SunIrradiance, &Lut, 48);
        Print("sunset sky (toward sun): R %.4f G %.4f B %.4f\n", SunsetSky.R, SunsetSky.G, SunsetSky.B);
        Check(SunsetSky.R > SunsetSky.B, "sky: sunset view is reddened (R>B)");

        // Deep night: sun 30 degrees below horizon (beyond the 100 km horizon dip ~10 deg) -> exactly dark.
        const SpatialVector SunNight = LocalToPlanet(DirectionFromElevationAzimuth(-30.0, 270.0), 0.0);
        const SpectrumRgb NightSky = SingleScattering(Atmo, Observer, View45, SunNight, SunIrradiance, &Lut, 32);
        Check(NightSky.R == 0.0 && NightSky.G == 0.0 && NightSky.B == 0.0, "sky: deep night is exactly dark");

        // LUT vs direct sun transmittance inside the march agrees closely.
        const SpectrumRgb NoonDirect = SingleScattering(Atmo, Observer, View45, SunNoon, SunIrradiance, nullptr, 32);
        Check(NearRel(NoonSky.B, NoonDirect.B, 0.05), "sky: LUT-accelerated march matches direct within 5%");

        // Multiple-scattering approximation: non-negative, bounded, increases total.
        const SpectrumRgb MS = MultipleScatteringApprox(Atmo, Observer, View45, SunNoon, NoonSky, SunIrradiance);
        Check(MS.R >= 0.0 && MS.G >= 0.0 && MS.B >= 0.0, "multiple scattering: non-negative");
        Check(MS.B < 5.0 * NoonSky.B + 1e-12, "multiple scattering: bounded boost");
        Print("multiple scattering adds (%.4f, %.4f, %.4f)\n", MS.R, MS.G, MS.B);
    }

    // ------------------------------------------------------------------ celestial
    {
        Check(Near(JulianDate(2000, 1, 1, 12.0), 2451545.0, 1e-9), "Julian: J2000 epoch exact");
        Check(Near(JulianDate(1970, 1, 1, 0.0), 2440587.5, 1e-9), "Julian: Unix epoch");

        const SolarPosition June = SunPosition(JulianDate(2026, 6, 21, 12.0), 0.0, 0.0);
        const SolarPosition Dec  = SunPosition(JulianDate(2026, 12, 21, 12.0), 0.0, 0.0);
        const SolarPosition Mar  = SunPosition(JulianDate(2026, 3, 20, 12.0), 0.0, 0.0);
        Print("declination: Jun %+.2f, Dec %+.2f, Mar %+.2f deg\n", June.DeclinationRad / D2R,
              Dec.DeclinationRad / D2R, Mar.DeclinationRad / D2R);
        Check(Near(June.DeclinationRad / D2R, 23.44, 0.4), "sun: June solstice declination +23.44");
        Check(Near(Dec.DeclinationRad / D2R, -23.44, 0.4), "sun: December solstice declination -23.44");
        Check(std::abs(Mar.DeclinationRad / D2R) < 1.5, "sun: equinox declination ~0");

        // Solar-noon elevation identity at Benoni, ZA (owner locale): max elevation over the day
        // equals 90 - |lat - declination| at that moment.
        const double Lat = -26.32, Lon = 28.31;
        double BestElevation = -10.0, BestDeclination = 0.0;
        for (double Hour = 0.0; Hour < 24.0; Hour += 5.0 / 60.0)
        {
            const SolarPosition S = SunPosition(JulianDate(2026, 10, 22, Hour), Lat, Lon);
            if (S.ElevationRad > BestElevation) { BestElevation = S.ElevationRad; BestDeclination = S.DeclinationRad; }
        }
        const double Identity = 90.0 - std::abs(Lat - BestDeclination / D2R);
        Print("Benoni 2026-10-22: max sun elevation %.2f deg (identity %.2f)\n", BestElevation / D2R, Identity);
        Check(Near(BestElevation / D2R, Identity, 0.35), "sun: solar-noon elevation identity holds");
        Check(BestElevation / D2R > 55.0, "sun: late-October noon sun is high in Gauteng");

        // Equinox sunrise is due east at the equator.
        double SunriseAzimuth = -1.0;
        for (double Hour = 4.0; Hour < 9.0; Hour += 1.0 / 60.0)
        {
            const SolarPosition S = SunPosition(JulianDate(2026, 3, 20, Hour), 0.0, 0.0);
            if (S.ElevationRad >= 0.0) { SunriseAzimuth = S.AzimuthRad / D2R; break; }
        }
        Print("equinox sunrise azimuth %.2f deg\n", SunriseAzimuth);
        Check(Near(SunriseAzimuth, 90.0, 2.0), "sun: equinox sunrise due east");

        Check(ClassifyDayPhase(0.0) == DayPhase::Day && ClassifyDayPhase(-3.0 * D2R) == DayPhase::CivilTwilight &&
              ClassifyDayPhase(-9.0 * D2R) == DayPhase::NauticalTwilight &&
              ClassifyDayPhase(-15.0 * D2R) == DayPhase::AstronomicalTwilight &&
              ClassifyDayPhase(-30.0 * D2R) == DayPhase::Night, "twilight: phase thresholds");
        const SpatialVector Down = SunDirectionLocal(SolarPosition{-Pi / 2, 0.0, 0.0});
        Check(Near(Down.Z, -1.0, 1e-12) && Near(Down.X, 0.0, 1e-12), "sun direction: nadir maps to -Z");

        const LunarPhase New = MoonPhase(NewMoonEpochJulian);
        const LunarPhase Full = MoonPhase(NewMoonEpochJulian + SynodicMonthDays / 2.0);
        const LunarPhase Again = MoonPhase(NewMoonEpochJulian + SynodicMonthDays);
        Check(Near(New.Illumination, 0.0, 1e-12) && Near(Full.Illumination, 1.0, 1e-9) &&
              Near(Again.AgeDays, 0.0, 1e-9), "moon: epoch/half/full cycle identities");
        bool Waxing = true;
        double Previous = -1.0;
        for (double Day = 0.0; Day <= SynodicMonthDays / 2.0; Day += 2.0)
        {
            const double I = MoonPhase(NewMoonEpochJulian + Day).Illumination;
            Waxing &= I >= Previous - 1e-12;
            Previous = I;
        }
        Check(Waxing, "moon: illumination waxes monotonically to full");

        const std::vector<StarRecord> Stars = GenerateStarCatalogue(4000, 1234);
        const std::vector<StarRecord> Stars2 = GenerateStarCatalogue(4000, 1234);
        const std::vector<StarRecord> Stars3 = GenerateStarCatalogue(4000, 999);
        bool Unit = true; double North = 0.0, MagSum = 0.0; bool MagRange = true;
        for (const StarRecord& S : Stars)
        {
            const double L = std::sqrt(S.Direction.X * S.Direction.X + S.Direction.Y * S.Direction.Y +
                                       S.Direction.Z * S.Direction.Z);
            Unit &= Near(L, 1.0, 1e-9);
            North += S.Direction.Z > 0.0 ? 1.0 : 0.0;
            MagSum += S.Magnitude;
            MagRange &= S.Magnitude >= -1.0 && S.Magnitude <= 6.0;
        }
        Check(Stars.size() == 4000 && Unit, "stars: count + unit directions");
        Check(Stars[0].Direction.X == Stars2[0].Direction.X && Stars[0].Magnitude == Stars2[0].Magnitude &&
              Stars[0].Direction.X != Stars3[0].Direction.X, "stars: seed-deterministic catalogue");
        Check(std::abs(North / 4000.0 - 0.5) < 0.04, "stars: hemispheres balanced");
        Check(MagRange && MagSum / 4000.0 > 3.0 && MagSum / 4000.0 < 4.6, "stars: magnitudes faint-heavy in range");
    }

    // ------------------------------------------------------------------ fog
    {
        FogParameters Fog;
        Fog.DensityAtBase = 0.02; Fog.BaseHeightM = 10.0; Fog.FalloffHeightM = 25.0;
        Check(NearRel(FogTransmittance(Fog, {0, 0, 5}, {1, 0, 0}, 100.0), std::exp(-2.0), 1e-12),
              "fog: horizontal below base is exact Beer-Lambert");
        const double AboveExpected = std::exp(-0.02 * std::exp(-(35.0 - 10.0) / 25.0) * 100.0);
        Check(NearRel(FogTransmittance(Fog, {0, 0, 35}, {1, 0, 0}, 100.0), AboveExpected, 1e-12),
              "fog: horizontal above base uses local density");
        // Slanted rays vs independent Simpson quadrature of the same profile.
        const auto NumericFog = [&](SpatialVector Origin, SpatialVector Dir, double Dist) {
            const int N = 4000;
            double Sum = 0.0;
            for (int I = 0; I <= N; ++I)
            {
                const double T = Dist * static_cast<double>(I) / N;
                const double Z = Origin.Z + Dir.Z * T;
                const double Rho = Fog.DensityAtBase * std::exp(-std::max(Z - Fog.BaseHeightM, 0.0) / Fog.FalloffHeightM);
                const double W = (I == 0 || I == N) ? 1.0 : (I % 2 ? 4.0 : 2.0);
                Sum += W * Rho;
            }
            return std::exp(-Sum * (Dist / N) / 3.0);
        };
        const SpatialVector Up45{0.0, 0.70710678, 0.70710678};
        const SpatialVector Down30{0.0, 0.8660254, -0.5};
        Check(NearRel(FogTransmittance(Fog, {0, 0, 2}, Up45, 80.0), NumericFog({0, 0, 2}, Up45, 80.0), 1e-4),
              "fog: rising ray matches independent quadrature");
        Check(NearRel(FogTransmittance(Fog, {0, 0, 60}, Down30, 90.0), NumericFog({0, 0, 60}, Down30, 90.0), 1e-4),
              "fog: descending ray matches independent quadrature");
        Check(FogTransmittance(Fog, {0, 0, 0}, {1, 0, 0}, 0.0) == 1.0, "fog: zero distance is clear");
        const SpectrumRgb Clear{0.2, 0.4, 0.6};
        const SpectrumRgb Fogged0 = ApplyFog(Fog, Clear, 0.0);
        const SpectrumRgb Fogged1 = ApplyFog(Fog, Clear, 1.0);
        Check(Fogged0.R == Fog.Colour.R && Fogged1.B == Clear.B, "fog: apply endpoints exact");
    }

    // ------------------------------------------------------------------ clouds
    {
        CloudParameters Clouds;
        Check(CloudDensity(Clouds, {0, 0, 100}) == 0.0 && CloudDensity(Clouds, {0, 0, 5000}) == 0.0,
              "clouds: zero outside the slab");
        Clouds.Coverage = 0.0;
        Check(CloudDensity(Clouds, {500, -700, 1200}) == 0.0, "clouds: zero coverage is clear");
        Clouds.Coverage = 0.55;
        const double SampleA = CloudDensity(Clouds, {1234, -567, 1300});
        const double SampleB = CloudDensity(Clouds, {1234, -567, 1300});
        Check(SampleA == SampleB, "clouds: density is seed-deterministic (bitwise)");
        const double Nearby = CloudDensity(Clouds, {1235, -567, 1300});
        Check(std::abs(SampleA - Nearby) < 0.05, "clouds: density field is continuous at 1 m");
        double MaxDensity = 0.0;
        for (int I = 0; I < 60; ++I)
            MaxDensity = std::max(MaxDensity, CloudDensity(Clouds, {I * 173.0 - 4000, I * 311.0 - 9000, 1200.0}));
        Check(MaxDensity > 0.1, "clouds: coverage produces real density in the slab");

        // Uniform slab: Beer-Lambert is exact for the midpoint march.
        CloudParameters Uniform = Clouds;
        Uniform.UniformDensityOverride = 0.8;
        const double Slab = Uniform.TopHeightM - Uniform.BaseHeightM;   // 1000 m
        const double SigmaRho = Uniform.ExtinctionPerM * 0.8;
        const CloudMarchResult Vertical = MarchClouds(Uniform, {0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {1, 1, 1}, 5000.0, 32, 0);
        Print("cloud uniform vertical T: %.6f vs Beer %.6f\n", Vertical.Transmittance, std::exp(-SigmaRho * Slab));
        Check(NearRel(Vertical.Transmittance, std::exp(-SigmaRho * Slab), 1e-9),
              "clouds: uniform vertical march is exact Beer-Lambert");
        const SpatialVector Sixty{0.0, 0.8660254, 0.5};
        const CloudMarchResult Angled = MarchClouds(Uniform, {0, 0, 0}, Sixty, {0, 0, 1}, {1, 1, 1}, 8000.0, 64, 0);
        Check(NearRel(Angled.Transmittance, std::exp(-SigmaRho * Slab / 0.5), 1e-9),
              "clouds: uniform 60-degree march path length exact");

        Uniform.UniformDensityOverride = 0.0;
        const CloudMarchResult ClearMarch = MarchClouds(Uniform, {0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {1, 1, 1});
        Check(ClearMarch.Transmittance == 1.0 && ClearMarch.Radiance.R == 0.0, "clouds: empty slab is transparent");

        // Forward scattering: toward the sun brighter than away (uniform slab, sun high).
        Uniform.UniformDensityOverride = 0.5;
        const SpatialVector SunDir{0.0, 0.35, 0.9367497};
        const CloudMarchResult Toward = MarchClouds(Uniform, {0, 0, 0}, SunDir, SunDir, {1, 1, 1}, 20000.0, 64, 4);
        const CloudMarchResult Away = MarchClouds(Uniform, {0, 0, 0}, {0.0, -0.35, 0.9367497}, SunDir, {1, 1, 1}, 20000.0, 64, 4);
        Print("cloud scatter: toward sun %.4f vs away %.4f\n", Toward.Radiance.R, Away.Radiance.R);
        Check(Toward.Radiance.R > Away.Radiance.R, "clouds: forward scattering brightens the sunward view");
        Check(std::isfinite(Toward.Radiance.R) && Toward.Transmittance > 0.0 && Toward.Transmittance <= 1.0,
              "clouds: march outputs finite and bounded");
    }

    // ------------------------------------------------------------------ weather
    {
        const PrecipitationParameters Params{};
        const PrecipitationField Rain(Params);
        const SpatialVector Anchor{100.0, -50.0, 10.0};
        bool InVolume = true;
        DeterministicRandom Probe(9, 9);
        for (int Trial = 0; Trial < 200; ++Trial)
        {
            const uint32_t Index = Probe.NextBelow(10000);
            const double Time = Probe.NextUnit() * 37.0;
            const SpatialVector P = Rain.ParticleAt(Index, Time, Anchor);
            InVolume &= P.X >= Anchor.X - Params.VolumeExtentM / 2 - 1e-9 &&
                        P.X < Anchor.X + Params.VolumeExtentM / 2 + 1e-9 &&
                        P.Z >= Anchor.Z - 1e-9 && P.Z < Anchor.Z + Params.VolumeHeightM + 1e-9;
        }
        Check(InVolume, "precipitation: particles stay inside the anchored volume");
        const SpatialVector A0 = Rain.ParticleAt(777, 3.25, Anchor);
        const SpatialVector A1 = Rain.ParticleAt(777, 3.25 + Rain.VerticalPeriod(777), Anchor);
        Check(Near(A0.Z, A1.Z, 1e-6), "precipitation: vertical wrap is exactly periodic");
        const SpatialVector V = Rain.ParticleVelocity(777);
        Check(V.Z <= -0.8 * Params.FallSpeedMps + 1e-12 && V.Z >= -1.2 * Params.FallSpeedMps - 1e-12,
              "precipitation: fall-speed variation within +-20%");
        Check(Rain.ParticleAt(5, 1.0, Anchor).X == Rain.ParticleAt(5, 1.0, Anchor).X,
              "precipitation: deterministic positions");

        const SpatialVector From{0, 0, 1200}, To{150, 80, 0};
        const LightningBolt Bolt = GenerateLightningBolt(From, To, 6, 0.12, 4242);
        Check(Bolt.MainChannel.size() == 65, "lightning: depth 6 -> 65 channel points");
        Check(Bolt.MainChannel.front().X == From.X && Bolt.MainChannel.front().Z == From.Z &&
              Bolt.MainChannel.back().X == To.X && Bolt.MainChannel.back().Z == To.Z,
              "lightning: endpoints exact");
        const LightningBolt Bolt2 = GenerateLightningBolt(From, To, 6, 0.12, 4242);
        Check(Bolt.MainChannel[17].X == Bolt2.MainChannel[17].X && Bolt.MainChannel[17].Y == Bolt2.MainChannel[17].Y,
              "lightning: same seed, identical bolt");
        bool RootsOnChannel = true;
        for (const auto& Branch : Bolt.Branches)
        {
            bool Found = false;
            for (const SpatialVector& P : Bolt.MainChannel)
                Found |= (P.X == Branch.front().X && P.Y == Branch.front().Y && P.Z == Branch.front().Z);
            RootsOnChannel &= Found;
        }
        Check(RootsOnChannel, "lightning: branches root exactly on the main channel");
        Check(LightningIntensity(0.0, 4242) == 1.0 && LightningIntensity(-1.0, 4242) == 0.0,
              "lightning: envelope starts at peak, silent before");
        Check(LightningIntensity(2.0, 4242) < 0.05, "lightning: flash decays away");
        double RestrikeMax = 0.0;
        for (double T = 0.08; T < 0.5; T += 0.005) RestrikeMax = std::max(RestrikeMax, LightningIntensity(T, 4242));
        Check(RestrikeMax > 0.2, "lightning: restrikes rebrighten the envelope");

        // Flare layout identities.
        const auto Ghosts = LayoutFlareGhosts(0.4, -0.3, 1.0);
        bool Collinear = true;
        for (const FlareGhost& G : Ghosts)
            Collinear &= Near(G.PositionX * -0.3 - G.PositionY * 0.4, 0.0, 1e-15);
        Check(Collinear, "flare: ghosts lie on the sun-centre axis");
        const auto Centred = LayoutFlareGhosts(0.0, 0.0, 0.7);
        const auto Hidden = LayoutFlareGhosts(0.4, -0.3, 0.0);
        bool CentreOk = true, HiddenOk = true;
        for (const FlareGhost& G : Centred) CentreOk &= G.PositionX == 0.0 && G.PositionY == 0.0;
        for (const FlareGhost& G : Hidden) HiddenOk &= G.Strength == 0.0;
        Check(CentreOk && HiddenOk, "flare: centred sun collapses ghosts; zero visibility hides them");
    }

    // ------------------------------------------------------------------ environment declaration
    {
        const EnvironmentFrameDeclaration Frame = DeclareEnvironmentFrame(1920, 1080);
        ResolvedSubmissionOrder Order;
        std::string Diagnostic;
        Check(ResolveSubmissionOrder(Frame.Units, Order, Diagnostic), "environment: order resolves");
        const auto PositionOf = [&](const char* Name) {
            for (uint32_t Pos = 0; Pos < Order.Order.size(); ++Pos)
                if (Frame.Units[Order.Order[Pos]].Name == Name) return static_cast<int>(Pos);
            return -1;
        };
        const bool Ordered =
            PositionOf("AtmosphereTransmittanceSubmission") < PositionOf("AtmosphereMultipleSubmission") &&
            PositionOf("AtmosphereMultipleSubmission") < PositionOf("CelestialSkyRasterization") &&
            PositionOf("CelestialSkyRasterization") < PositionOf("NightStarsRasterization") &&
            PositionOf("CloudRecordsSubmission") < PositionOf("CloudShadowProjectSubmission") &&
            PositionOf("CloudShadowProjectSubmission") < PositionOf("CloudTraceSubmission") &&
            PositionOf("CloudTraceSubmission") < PositionOf("CloudPresentRasterization") &&
            PositionOf("CloudPresentRasterization") < PositionOf("FogMediumSubmission") &&
            PositionOf("FogMediumSubmission") < PositionOf("PrecipitationRasterization") &&
            PositionOf("FlareVisibilitySubmission") < PositionOf("FlareImageRasterization");
        Check(Ordered, "environment: resolved order honours the chain");
        bool DepthImported = false;
        for (const std::string& T : Order.ImportedTargets) DepthImported |= T == "SurfaceDepth";
        Check(DepthImported, "environment: surface depth is the imported read");

        DerivedBarriers Barriers;
        Check(DeriveBarriers(Frame.Units, Order, Frame.Targets, Barriers, Diagnostic),
              "environment: barriers derive");
        if (!Diagnostic.empty()) Print("      diagnostic: %s\n", Diagnostic.c_str());
        Print("environment barriers: %zu derived\n", Barriers.Barriers.size());

        ResolvedTransientAtlas Atlas;
        Check(ResolveTransientTargetAtlas(Frame.Units, Order, Frame.Targets, Atlas, Diagnostic),
              "environment: atlas resolves");
        bool LutsOffAtlas = true;
        for (const AtlasPlacement& Placement : Atlas.Placements)
            if (Placement.Name == "TransmittanceLut" || Placement.Name == "MultipleScatteringLut" ||
                Placement.Name == "SurfaceDepth")
                LutsOffAtlas &= (Placement.Bin == UINT32_MAX);
        Check(LutsOffAtlas, "environment: LUTs + imported depth stay off the transient atlas");
        ResolvedSubmissionOrder Again;
        (void)ResolveSubmissionOrder(Frame.Units, Again, Diagnostic);
        Check(Again.Order == Order.Order, "environment: resolution deterministic");
    }

    // ------------------------------------------------------------------ evidence render
    if (!PpmPrefix.empty())
    {
        TransmittanceLut Lut;
        (void)Lut.Bake(Atmo, 128, 32, 48);
        const uint32_t W = 96, H = 64;
        const std::string Path = PpmPrefix + "sky-strip.ppm";
        FILE* File = std::fopen(Path.c_str(), "wb");
        if (File != nullptr)
        {
            std::fprintf(File, "P6\n%u %u\n255\n", W * 3, H);
            const SpatialVector Observer{0.0, Atmo.GroundRadiusKm + 0.002, 0.0};
            const SpectrumRgb SunIrradiance{20.0, 20.0, 20.0};
            CloudParameters Clouds;
            Clouds.Coverage = 0.6;
            for (uint32_t Y = 0; Y < H; ++Y)
            {
                for (uint32_t X = 0; X < W * 3; ++X)
                {
                    const uint32_t Panel = X / W;
                    const double U = static_cast<double>(X % W) / W;          // azimuth sweep rel. sun
                    const double Elevation = -1.0 + (1.0 - static_cast<double>(Y) / H) * 55.0;
                    const double SunElevation = Panel == 0 ? 45.0 : 1.2;
                    const double SunAzimuth = 270.0;
                    const double ViewAzimuth = SunAzimuth + (U - 0.5) * 140.0;
                    const SpatialVector SunDir = LocalToPlanet(DirectionFromElevationAzimuth(SunElevation, SunAzimuth), 0.0);
                    const SpatialVector ViewDir = LocalToPlanet(DirectionFromElevationAzimuth(Elevation, ViewAzimuth), 0.0);
                    SpectrumRgb Value = SingleScattering(Atmo, Observer, ViewDir, SunDir, SunIrradiance, &Lut, 24);
                    Value.R += 0.0;   // (multiple scattering folded into the strip would double the march cost)
                    if (Panel == 2 && Elevation > 0.0)
                    {
                        const SpatialVector LocalView = DirectionFromElevationAzimuth(Elevation, ViewAzimuth);
                        const SpatialVector LocalSun = DirectionFromElevationAzimuth(SunElevation, SunAzimuth);
                        const CloudMarchResult CM = MarchClouds(Clouds, {0, 0, 2}, LocalView, LocalSun, {0.9, 0.95, 1.0}, 30000.0, 40, 4);
                        Value.R = Value.R * CM.Transmittance + CM.Radiance.R * 1.4;
                        Value.G = Value.G * CM.Transmittance + CM.Radiance.G * 1.4;
                        Value.B = Value.B * CM.Transmittance + CM.Radiance.B * 1.4;
                    }
                    const auto ToByte = [](double V) {
                        const double Mapped = std::pow(1.0 - std::exp(-std::max(V, 0.0) * 1.6), 1.0 / 2.2);
                        return static_cast<unsigned char>(std::clamp(Mapped, 0.0, 1.0) * 255.0);
                    };
                    const unsigned char RGB[3] = {ToByte(Value.R), ToByte(Value.G), ToByte(Value.B)};
                    std::fwrite(RGB, 1, 3, File);
                }
            }
            std::fclose(File);
            Print("evidence image -> %s\n", Path.c_str());
        }
    }

    Print("Environment pipeline validation: %d/%d checks passed\n", Total - Failures, Total);
    return Failures == 0 ? 0 : 1;
}
