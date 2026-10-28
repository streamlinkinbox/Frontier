/*==========================================================================================================================================
                                              DAYLIGHT CELESTIAL VALIDATION
==========================================================================================================================================*/
// 🧩 Analytic, numerical and renderer checks for daylight sky, textured lunar light and static SDF bounces.

#include "Engine/Rendering/CelestialModel.h"
#include "Engine/Rendering/LunarBounceReservoir.h"
#include "Engine/Rendering/LunarDiscProjection.h"
#include "Engine/Rendering/ReferenceRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double Rad = Pi / 180.0;
int Checks = 0, Failures = 0;

void Require(bool Condition, const char* Label)
{
    ++Checks;
    if (!Condition) ++Failures;
    std::printf("%s %s\n", Condition ? "PASS" : "FAIL", Label);
}
double Lum(SpectrumRgb C) { return 0.2126*C.R + 0.7152*C.G + 0.0722*C.B; }
SpatialVector Unit(SpatialVector V)
{
    const double M = std::sqrt(V.X*V.X + V.Y*V.Y + V.Z*V.Z);
    return M > 0 ? SpatialVector{V.X/M,V.Y/M,V.Z/M} : SpatialVector{};
}
SpatialVector Cross(SpatialVector A, SpatialVector B)
{ return {A.Y*B.Z-A.Z*B.Y,A.Z*B.X-A.X*B.Z,A.X*B.Y-A.Y*B.X}; }
double Sum(const ReferenceImage& I)
{
    double Result = 0;
    for (const auto& P : I.Pixels) Result += Lum(P);
    return Result;
}
double Difference(const ReferenceImage& A, const ReferenceImage& B)
{
    double Result = 0;
    for (size_t I = 0; I < A.Pixels.size(); ++I)
        Result = std::max({Result, std::abs(A.Pixels[I].R-B.Pixels[I].R),
                          std::abs(A.Pixels[I].G-B.Pixels[I].G), std::abs(A.Pixels[I].B-B.Pixels[I].B)});
    return Result;
}
double IntegratedLum(const ReferenceImage& Image, const CullCamera& Camera)
{
    const double Ty = std::tan(Camera.FovYRadians*0.5), Tx = Ty*Camera.Aspect;
    const double Dx = 2.0*Tx/Image.Width, Dy = 2.0*Ty/Image.Height;
    double Total = 0;
    for (uint32_t Y = 0; Y < Image.Height; ++Y) for (uint32_t X = 0; X < Image.Width; ++X)
    {
        const double U = (2.0*(X+0.5)/Image.Width-1.0)*Tx;
        const double V = (1.0-2.0*(Y+0.5)/Image.Height)*Ty;
        Total += Lum(Image.Pixels[static_cast<size_t>(Y)*Image.Width+X]) * Dx*Dy / std::pow(1+U*U+V*V,1.5);
    }
    return Total;
}
CullCamera CameraToward(SpatialVector Position, SpatialVector Aim, double Fov, double Aspect = 1.0)
{
    CullCamera C; C.Position = Position; C.Forward = Unit(Aim);
    C.Right = Unit(Cross(C.Forward, std::abs(C.Forward.Z) < 0.98 ? SpatialVector{0,0,1} : SpatialVector{0,1,0}));
    C.Up = Unit(Cross(C.Right, C.Forward)); C.FovYRadians = Fov; C.Aspect = Aspect;
    return C;
}
ReferenceScene GroundScene()
{
    ReferenceScene Scene; ReferenceMesh Ground;
    Ground.Geometry.Vertices = {{-6,-6,0},{6,-6,0},{0,6,0}};
    Ground.Geometry.Indices = {0,1,2};
    Ground.Material.Albedo = {1,1,1}; Ground.Material.SpecularLevel = 0;
    Scene.Meshes.push_back(Ground); return Scene;
}
ReferenceRenderSettings BasicSettings()
{
    ReferenceRenderSettings S; S.Width = S.Height = 1; S.SunIrradiance = {1,1,1};
    S.SunDiscEnabled = false; S.EnvironmentSamples = 0; S.IndirectSamples = 0;
    S.AccumulationFrames = 1; S.Denoise = false; S.SpatialReuse = false;
    return S;
}
void SavePpm(const std::string& Path, const ReferenceImage& Image, double Exposure)
{
    std::vector<uint8_t> Bytes; EncodeDisplayRgb8(Image, Exposure, Bytes);
    std::ofstream File(Path, std::ios::binary);
    File << "P6\n" << Image.Width << ' ' << Image.Height << "\n255\n";
    File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
    if (!File) { ++Failures; std::fprintf(stderr, "could not write %s\n", Path.c_str()); }
}
struct Moments
{
    double Sum = 0, SumSq = 0; uint32_t Count = 0;
    void Add(double X) { Sum += X; SumSq += X*X; ++Count; }
    double Mean() const { return Sum/Count; }
    double Error() const { return std::sqrt(std::max(0.0, SumSq/Count-Mean()*Mean())/Count); }
};

void CatalogueChecks()
{
    std::stringstream Csv("id,ra_deg,dec_deg,mag,bv\r\n1,0,0,1.0,0.3\r\nbroken,not-a-number,0,0,0\r\n");
    const auto Stars = DecodeHipparcosCatalogue(Csv,2451545.0,0.0,-18.697374558*15.0);
    Require(Stars.size() == 1 && std::abs(Stars[0].Direction.X) < 1e-12 &&
            std::abs(Stars[0].Direction.Y) < 1e-12 && std::abs(Stars[0].Direction.Z-1.0) < 1e-12,
            "J2000 equatorial transit at the equator is overhead; malformed row rejected");
    std::ifstream Source("Exhibits/NightSky/Data/Hipparcos8.csv");
    const auto Catalogue = DecodeHipparcosCatalogue(Source,2460967.0416666665,-26.2,28.3);
    Require(Catalogue.size() > 40000 && Catalogue.size() < 42000, "real Hipparcos CSV decoded into local directions");
    if (!Catalogue.empty())
        std::printf("catalogue rows=%zu; first local ENU %.8g %.8g %.8g\n", Catalogue.size(),
                    Catalogue[0].Direction.X, Catalogue[0].Direction.Y, Catalogue[0].Direction.Z);
}

void LunarModelChecks(const std::shared_ptr<LunarAlbedoMap>& Map)
{
    ReferenceLunarProfile P; P.Albedo = Map;
    const LunarDiscProjection Full(P,{0,0,-1},{1,1,1});
    const LunarDiscProjection Quarter(P,{1,0,0},{1,1,1});
    const LunarDiscProjection New(P,{0,0,1},{1,1,1});
    const double FullFlux = Lum(Full.Irradiance()), QuarterFlux = Lum(Quarter.Irradiance());
    Require(Full.Valid() && std::abs(FullFlux/P.FullMoonFluxRatio-1.0) < 1e-12,
            "full-Moon broadband flux equals the configured magnitude-ratio reference at mean radius");
    Require(QuarterFlux > 0 && QuarterFlux < FullFlux*0.4 && Lum(New.Irradiance()) == 0.0,
            "geometric full, quarter and new Moon change integrated lunar irradiance");
    std::printf("full %.12g, quarter %.12g, new %.12g; quarter/full %.9g\n", FullFlux, QuarterFlux,
                Lum(New.Irradiance()), QuarterFlux/FullFlux);
    P.AngularRadiusRad *= 1.5;
    const LunarDiscProjection Closer(P,{0,0,-1},{1,1,1});
    const double AreaRatio = std::pow(std::sin(P.AngularRadiusRad)/std::sin(P.AngularRadiusRad/1.5),2);
    Require(std::abs(Lum(Closer.Irradiance())/FullFlux/AreaRatio-1.0) < 1e-12,
            "lunar flux changes with angular area when distance changes");

    double Worst = 0;
    for (double RadiusPx : {0.1,0.5,1.0,2.0,3.5,4.5,6.0,12.0,24.0})
    {
        P.AngularRadiusRad = 0.004596;
        const LunarDiscProjection Projection(P,{0,0,-1},{1,1,1});
        const double Pixel = std::sin(P.AngularRadiusRad)/RadiusPx;
        const int Reach = static_cast<int>(std::ceil(RadiusPx+5));
        double Flux = 0; bool Finite = true;
        for (int Y = -Reach; Y <= Reach; ++Y) for (int X = -Reach; X <= Reach; ++X)
        {
            const auto Q = Projection.Evaluate((X+0.23)*Pixel,(Y-0.17)*Pixel,Pixel,Pixel,{1,0,0},{0,1,0});
            Flux += Lum(Q.Radiance)*Pixel*Pixel;
            Finite &= std::isfinite(Q.Coverage) && Q.Coverage >= 0 && Q.Coverage <= 1 &&
                      std::isfinite(Q.Radiance.R) && Q.Radiance.R >= 0;
        }
        const double Error = std::abs(Flux/FullFlux-1);
        Worst = std::max(Worst,Error);
        std::printf("lunar radius=%5.1f px integrated %.12g relative error %.9g\n",RadiusPx,Flux,Error);
        if (!Finite) Worst = 1e9;
    }
    Require(Worst < 0.02, "textured lunar pixel filtering preserves integrated flux across subpixel and resolved sizes");
    const auto Centre = Full.Evaluate(0,0,0.00008,0.00008,{1,0,0},{0,1,0});
    const auto OffCentre = Full.Evaluate(0.002,0,0.00008,0.00008,{1,0,0},{0,1,0});
    Require(std::abs(Lum(Centre.Radiance)-Lum(OffCentre.Radiance)) > 0.01*Lum(Centre.Radiance),
            "actual demo albedo texture produces nonuniform lunar surface detail");
    std::printf("lunar filtering worst relative flux error %.9g\n",Worst);
}

void AtmosphereChecks()
{
    const AtmosphereParameters P;
    const auto Vertical = TransmittanceToTop(P,P.GroundRadiusKm,1.0,96);
    const double Height = P.TopRadiusKm-P.GroundRadiusKm;
    const double Rayleigh = P.RayleighScaleHeightKm * (1.0-std::exp(-Height/P.RayleighScaleHeightKm));
    const double Mie = (P.MieScattering+P.MieAbsorption)*P.MieScaleHeightKm*(1.0-std::exp(-Height/P.MieScaleHeightKm));
    const auto Exact = [&](double R,double O) { return std::exp(-R*Rayleigh-Mie-O*P.OzoneWidthKm); };
    const double VerticalError = std::max({std::abs(Vertical.R-Exact(P.RayleighScattering.R,P.OzoneAbsorption.R)),
        std::abs(Vertical.G-Exact(P.RayleighScattering.G,P.OzoneAbsorption.G)),
        std::abs(Vertical.B-Exact(P.RayleighScattering.B,P.OzoneAbsorption.B))});
    std::printf("vertical analytical optical-depth T RGB %.9g %.9g %.9g; numerical %.9g %.9g %.9g; max error %.9g\n",
        Exact(P.RayleighScattering.R,P.OzoneAbsorption.R),Exact(P.RayleighScattering.G,P.OzoneAbsorption.G),
        Exact(P.RayleighScattering.B,P.OzoneAbsorption.B),Vertical.R,Vertical.G,Vertical.B,VerticalError);
    Require(VerticalError < 0.001, "vertical beam matches independent exponential and triangular-layer integrals");

    ReferenceRenderer Renderer; Renderer.SetScene(GroundScene());
    const auto Camera = CameraToward({0,0,2},{0,0,-1},0.6);
    auto S = BasicSettings(); S.AtmosphereEnabled = true; S.SolarSurfaceLighting = true;
    S.StoreSurfaceSamples = true; S.ExhaustiveLights = true;
    double Worst = 0, SunsetRed = 0, SunsetBlue = 0;
    for (double Elevation : {90.0,30.0,10.0,5.0,1.0})
    {
        S.SunDirection = {std::cos(Elevation*Rad),0,std::sin(Elevation*Rad)};
        S.AtmosphericSunlight = false; const auto Clear = Renderer.Render(Camera,S);
        S.AtmosphericSunlight = true; const auto Filtered = Renderer.Render(Camera,S);
        const auto HighStep = TransmittanceToTop(P,P.GroundRadiusKm,S.SunDirection.Z,4096);
        const auto A = Filtered.SurfaceSamples[0].Direct, B = Clear.SurfaceSamples[0].Direct;
        const double R = A.R/B.R, G = A.G/B.G, Blue = A.B/B.B;
        Worst = std::max({Worst,std::abs(R-HighStep.R),std::abs(G-HighStep.G),std::abs(Blue-HighStep.B)});
        if (Elevation == 5.0) { SunsetRed = R; SunsetBlue = Blue; }
        std::printf("Sun elevation %5.1f: renderer beam T %.8g %.8g %.8g; 4096-step %.8g %.8g %.8g\n",
                    Elevation,R,G,Blue,HighStep.R,HighStep.G,HighStep.B);
    }
    Require(Worst < 0.015 && SunsetRed > SunsetBlue && SunsetBlue < 0.5,
            "surface Sun responds to elevation and sunset reddening; toggle removes attenuation");
    S.SolarSurfaceLighting = false; Require(Lum(Renderer.Render(Camera,S).Pixels[0]) == 0.0,
                                            "solar surface lighting is opt-in");
    S.SolarSurfaceLighting = true; S.AtmosphereEnabled = false; S.AtmosphericSunlight = true;
    const auto NoAtmosphere = Renderer.Render(Camera,S);
    S.AtmosphereEnabled = true; S.AtmosphericSunlight = false;
    Require(Difference(NoAtmosphere,Renderer.Render(Camera,S)) < 1e-12,
            "disabling the atmosphere or beam attenuation produces the same unattenuated direct Sun");

    ReferenceScene WallScene; ReferenceMesh Wall;
    Wall.Geometry.Vertices = {{0,-2,-2},{0,2,-2},{0,0,2}}; Wall.Geometry.Indices = {0,1,2};
    WallScene.Meshes.push_back(Wall); Renderer.SetScene(WallScene);
    const auto WallCamera = CameraToward({2,0,0},{-1,0,0},0.3);
    S.AtmosphereEnabled = false; S.SunDirection = Unit({1,0,0.1});
    const auto Above = Renderer.Render(WallCamera,S);
    S.SunDirection = Unit({1,0,-0.1}); const auto Below = Renderer.Render(WallCamera,S);
    Require(Above.SurfaceSamples[0].Hit && Lum(Above.SurfaceSamples[0].Direct) > 0 &&
            Lum(Below.SurfaceSamples[0].Direct) == 0.0,
            "planet blocks a below-horizon directional Sun even on a surface facing it");

    auto LitScene = GroundScene(); LitScene.Lights.push_back({ReferenceLight::Kind::Directional,{0,0,1},{0.2,0.2,0.2}});
    Renderer.SetScene(LitScene); auto Spatial = BasicSettings(); Spatial.Width = Spatial.Height = 8;
    Spatial.AtmosphereEnabled = false; Spatial.SunDirection = {0,0,1}; Spatial.AccumulationFrames = 4;
    Spatial.SolarSurfaceLighting = false; const auto Base = Renderer.Render(Camera,Spatial);
    Spatial.SolarSurfaceLighting = true; const auto WithSun = Renderer.Render(Camera,Spatial);
    Spatial.SpatialReuse = true; Spatial.SolarSurfaceLighting = false; const auto ReusedBase = Renderer.Render(Camera,Spatial);
    Spatial.SolarSurfaceLighting = true; const auto ReusedSun = Renderer.Render(Camera,Spatial);
    double SpatialError = 0, SpatialDelta = 0;
    for (size_t I = 0; I < Base.Pixels.size(); ++I)
    {
        const double Plain = WithSun.Pixels[I].R-Base.Pixels[I].R;
        const double Reused = ReusedSun.Pixels[I].R-ReusedBase.Pixels[I].R;
        SpatialError = std::max(SpatialError,std::abs(Plain-Reused));
        SpatialDelta = std::max(SpatialDelta,std::abs(Plain));
    }
    Require(SpatialDelta > 0.1 && SpatialError < 1e-12,
            "direct celestial light remains present when analytic-light spatial reuse runs");

    ReferenceRenderer Sky; Sky.SetScene({}); auto V = BasicSettings(); V.Width = V.Height = 16;
    V.AtmosphereEnabled = true; V.SunDirection = Unit({0.7,0,0.7});
    const auto SkyCamera = CameraToward({0,0,0},{0,0,1},0.8);
    const auto Initial = Sky.Render(SkyCamera,V);
    V.Atmosphere.RayleighScattering.R *= 2.0; V.Atmosphere.RayleighScattering.B *= 2.0;
    const auto Changed = Sky.Render(SkyCamera,V);
    ReferenceRenderer Fresh; Fresh.SetScene({}); const auto Rebuilt = Fresh.Render(SkyCamera,V);
    Require(Difference(Initial,Changed) > 1e-4 && Difference(Changed,Rebuilt) < 1e-12,
            "changing atmosphere parameters rebakes the cached LUT instead of reusing stale data");
    V.SunDirection = Unit({1,0,0.05}); const auto Sunset = Sky.Render(SkyCamera,V);
    Require(Difference(Changed,Sunset) > 1e-4, "sky scattering changes when the Sun changes position");
    std::printf("worst renderer beam vs high-step channel absolute error %.9g; updated-sky delta %.9g\n",
                Worst,Difference(Initial,Changed));
}

void BackgroundChecks(const std::shared_ptr<LunarAlbedoMap>& Map)
{
    ReferenceRenderer Renderer; Renderer.SetScene({});
    auto Camera = CameraToward({0,0,0},{0,0,1},60*Rad);
    auto S = BasicSettings(); S.Width = S.Height = 64; S.AtmosphereEnabled = false;
    S.SunDirection = {0,0,-1}; S.Lunar.Albedo = Map;
    auto Stars = std::make_shared<std::vector<StarRecord>>();
    Stars->push_back({Unit({0.17,0.03,1}),0.0,0.65}); S.SiderealStars = Stars;
    S.SiderealStarsEnabled = true; const auto Star0 = Renderer.Render(Camera,S);
    const double Flux0 = IntegratedLum(Star0,Camera);
    const double Expected0 = std::pow(10.0,-0.4*26.74);
    (*Stars)[0].Magnitude = 5.0; const double Flux5 = IntegratedLum(Renderer.Render(Camera,S),Camera);
    Require(std::abs(Flux0/Expected0-1.0) < 0.002 && std::abs(Flux5/Flux0-0.01) < 1e-8,
            "star pixel flux follows the astronomical magnitude law and survives subpixel projection");
    std::printf("magnitude 0 flux %.12g expected %.12g; magnitude 5 / 0 %.9g\n",Flux0,Expected0,Flux5/Flux0);
    (*Stars)[0].Magnitude = 2.0;
    S.AtmosphereEnabled = true; S.SunDirection = Unit({1,0,1});
    S.SiderealStarsEnabled = false; const auto DayWithout = Renderer.Render(Camera,S);
    S.SiderealStarsEnabled = true; const auto DayWith = Renderer.Render(Camera,S);
    const double DayDelta = Difference(DayWith,DayWithout), DayBackground = Sum(DayWithout)/DayWithout.Pixels.size();
    Require(DayDelta > 0 && DayDelta/DayBackground < 0.01,
            "daytime star is present in HDR but naturally washed out by sky scattering");
    S.StarBrightnessScale = 10000.0; const auto DayBoost = Renderer.Render(Camera,S);
    const double BoostedDelta = Difference(DayBoost,DayWithout);
    Require(std::abs(BoostedDelta/DayDelta/10000.0-1.0) < 1e-5,
            "optional artistic star gain scales the HDR star signal without changing the sky");
    S.StarBrightnessScale = 1.0;
    std::printf("day star max RGB change %.12g, mean sky luminance %.9g, ratio %.9g, 10000x gain %.9g\n",
                DayDelta,DayBackground,DayDelta/DayBackground,BoostedDelta/DayDelta);
    S.AtmosphereEnabled = false; S.CloudsEnabled = false; S.CloudSunScale = 0.0;
    const double ClearStar = IntegratedLum(Renderer.Render(Camera,S),Camera);
    S.CloudsEnabled = true; S.Clouds.UniformDensityOverride = 1.0;
    const double CloudyStar = IntegratedLum(Renderer.Render(Camera,S),Camera);
    Require(ClearStar > 0.0 && CloudyStar < 0.1*ClearStar,
            "cloud transmittance attenuates background stars even when atmosphere is disabled");
    std::printf("clear/cloudy star integrated flux %.12g / %.12g\n",ClearStar,CloudyStar);

    S.CloudsEnabled = false; S.AtmosphereEnabled = false; S.SunDirection = {0,0,1}; S.Lunar.Direction = {0,0,1};
    (*Stars)[0] = {{0,0,1},-1.0,0.2}; S.LunarDiscEnabled = false;
    const auto Unmasked = Renderer.Render(Camera,S); S.LunarDiscEnabled = true;
    const auto Masked = Renderer.Render(Camera,S);
    Require(Sum(Unmasked) > 0 && Sum(Masked) == 0.0,
            "a dark subpixel new Moon geometrically occludes a background star before PSF filtering");
    S.Lunar.Albedo.reset(); Require(Difference(Renderer.Render(Camera,S),Unmasked) == 0.0,
                                     "missing lunar map does not fabricate a textured Moon");
    S.Lunar.Albedo = Map; S.LunarDiscEnabled = true; S.SunDirection = Unit({1,0,0.6});
    Camera = CameraToward({0,0,0},{0,0,1},5*Rad); S.Width = S.Height = 96;
    S.AtmosphereEnabled = true; S.SiderealStarsEnabled = false;
    S.LunarDiscEnabled = false; const auto NoMoon = Renderer.Render(Camera,S);
    S.LunarDiscEnabled = true; const auto DayMoon = Renderer.Render(Camera,S);
    Require(Difference(DayMoon,NoMoon) > 1e-5, "textured partially illuminated Moon contributes in daylight");

    S.AtmosphereEnabled = false; S.SunDirection = Unit({-1,0,0.2});
    S.Lunar.Direction = Unit({1,0,-0.2}); Camera = CameraToward({0,0,0},S.Lunar.Direction,5*Rad);
    const auto Below = Renderer.Render(Camera,S);
    Require(Sum(Below) == 0.0, "planet blocks a Moon whose centre is below the geometric horizon");

    ReferenceScene Blocked; ReferenceMesh Panel;
    Panel.Geometry.Vertices = {{-2,-2,1},{2,-2,1},{0,2,1}}; Panel.Geometry.Indices = {0,1,2};
    Blocked.Meshes.push_back(Panel); Renderer.SetScene(Blocked);
    S.Lunar.Direction = {0,0,1}; S.SunDirection = {0,0,-1}; Camera = CameraToward({0,0,0},{0,0,1},5*Rad);
    const auto Hidden = Renderer.Render(Camera,S);
    Require(Hidden.Stats.SurfacePixels == S.Width*S.Height && Sum(Hidden) == 0.0,
            "foreground reference geometry blocks the lunar background");

    ReferenceScene TinyScene; ReferenceMesh Tiny;
    Tiny.Geometry.Vertices = {{-0.002,-0.002,1},{0.002,-0.002,1},{0,0.002,1}};
    Tiny.Geometry.Indices = {0,1,2}; TinyScene.Meshes.push_back(Tiny);
    Camera = CameraToward({0,0,0},{0,0,1},60*Rad); S.Width = S.Height = 64;
    S.LunarDiscEnabled = false; S.SiderealStarsEnabled = true; S.SunDirection = {0,0,-1};
    ReferenceRenderer Empty; Empty.SetScene({}); const auto ClearStarImage = Empty.Render(Camera,S);
    Renderer.SetScene(TinyScene); const auto TinyShadow = Renderer.Render(Camera,S);
    Require(TinyShadow.Stats.SurfacePixels == 0 && Sum(ClearStarImage) > 0 && Sum(TinyShadow) == 0.0,
            "subpixel triangle blocks the star's point ray before camera filtering");
}

void LunarDirectChecks(const std::shared_ptr<LunarAlbedoMap>& Map)
{
    ReferenceRenderer Renderer; Renderer.SetScene(GroundScene());
    auto Camera = CameraToward({0,0,2},{0,0,-1},0.6); auto S = BasicSettings();
    S.AtmosphereEnabled = false; S.Lunar.Albedo = Map; S.SunDirection = {0,0,-1};
    S.Lunar.Direction = {0,0,1}; S.StoreSurfaceSamples = true; S.ExhaustiveLights = true;
    const auto Dark = Renderer.Render(Camera,S); S.LunarSurfaceLighting = true;
    const auto Lit = Renderer.Render(Camera,S);
    const LunarDiscProjection Full(S.Lunar,S.SunDirection,S.SunIrradiance);
    const auto Beam = Full.Irradiance();
    Require(Dark.Stats.SurfacePixels == 1 && Lum(Dark.Pixels[0]) == 0.0 &&
            std::abs(Lit.Pixels[0].R/(Beam.R/Pi)-1.0) < 1e-12 &&
            std::abs(Lit.SurfaceSamples[0].Direct.R-Lit.Pixels[0].R) < 1e-15,
            "full Moon illuminates a surface with the integrated textured-disc beam; toggle off is dark");
    S.SunDirection = {0,0,1}; Require(Lum(Renderer.Render(Camera,S).Pixels[0]) == 0.0,
                                      "new Moon has no direct light when earthshine is disabled");

    auto Occluded = GroundScene(); ReferenceMesh Roof;
    Roof.Geometry.Vertices = {{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    Roof.Geometry.Indices = {0,1,2,0,2,3}; Occluded.Meshes.push_back(Roof);
    Camera = CameraToward({3,0,0.5},{-3,0,-0.5},0.3);
    Renderer.SetScene(GroundScene()); S.SunDirection = {0,0,-1}; const auto Clear = Renderer.Render(Camera,S);
    Renderer.SetScene(Occluded); const auto Shadow = Renderer.Render(Camera,S);
    Require(Clear.SurfaceSamples[0].Hit && Shadow.SurfaceSamples[0].Hit &&
            Lum(Clear.SurfaceSamples[0].Direct) > 0 && Lum(Shadow.SurfaceSamples[0].Direct) == 0.0,
            "reference triangle shadow ray blocks lunar surface illumination");
    std::printf("full-Moon top beam RGB %.12g %.12g %.12g; clear/shadow direct %.12g / %.12g\n",
                Beam.R,Beam.G,Beam.B,Lum(Clear.SurfaceSamples[0].Direct),Lum(Shadow.SurfaceSamples[0].Direct));
}

void IntersectionChecks()
{
    DistanceFieldScene Sphere; Sphere.AddPrimitive({DistancePrimitiveKind::Sphere,{0,0,5},{1,0,0},0,0.4});
    const auto Hit = Sphere.IntersectAnalytic({0,0,0},{0,0,1},1000);
    const auto Miss = Sphere.IntersectAnalytic({0,0,0},{0,1,0},1000);
    Require(Hit.Hit && Hit.Primitive == 0 && std::abs(Hit.Distance-4.0) < 1e-12 && !Miss.Hit,
            "analytic sphere ray intersection matches known entry distance and miss");
    DistanceFieldScene Box; Box.AddPrimitive({DistancePrimitiveKind::Box,{1,0,0},{0.5,1,1},0,0.4});
    const auto BoxHit = Box.IntersectAnalytic({0,0,0},{1,0,0},1000);
    const auto Inside = Box.IntersectAnalytic({1,0,0},{1,0,0},1000);
    Require(BoxHit.Hit && std::abs(BoxHit.Distance-0.5) < 1e-12 && Inside.Hit && Inside.Distance == 0.0,
            "axis-aligned box ray intersection and interior origin are exact");
    DistanceFieldScene Plane; Plane.AddPrimitive({DistancePrimitiveKind::HalfSpace,{1,0,0},{-1,0,0},0,0.4});
    const auto Direction = Unit({0.002,0,1});
    const auto Grazing = Plane.IntersectAnalytic({0,0,0},Direction,1000);
    const auto Limited = Plane.IntersectAnalytic({0,0,0},Direction,100);
    Require(Grazing.Hit && std::abs(Grazing.Distance-1.0/Direction.X) < 1e-10 && !Limited.Hit,
            "grazing half-space ray is exact and respects the maximum ray length");

    DistanceFieldScene Mixed;
    Mixed.AddPrimitive({DistancePrimitiveKind::Sphere,{0,0,3},{1,0,0},0,0.4});
    Mixed.AddPrimitive({DistancePrimitiveKind::Box,{2,0,4},{0.6,0.7,0.5},0,0.6});
    DeterministicRandom R(0x5a11,0x912); int Compared = 0, Disagreements = 0; double Worst = 0;
    for (int I = 0; I < 5000; ++I)
    {
        const auto D = Unit({3.0*(2*R.NextUnit()-1),2.0*(2*R.NextUnit()-1),3.0});
        const auto Exact = Mixed.IntersectAnalytic({0,0,0},D,100.0);
        const auto Marched = Mixed.Trace({0,0,0},D,100.0,1e-4,4096);
        if (Exact.Hit != Marched.Hit) { ++Disagreements; continue; }
        if (!Exact.Hit) continue;
        const auto N = Mixed.NormalAt({D.X*Exact.Distance,D.Y*Exact.Distance,D.Z*Exact.Distance});
        if (std::abs(N.X*D.X+N.Y*D.Y+N.Z*D.Z) < 0.1) continue;
        Worst = std::max(Worst,std::abs(Exact.Distance-Marched.Distance)); ++Compared;
    }
    Require(Compared > 1000 && Disagreements < 10 && Worst < 0.0011,
            "analytic intersections agree with independent SDF marching on non-grazing mixed-scene rays");
    std::printf("mixed-scene 5000 rays: compared %d, hit/miss disagreements %d, worst distance %.9g m\n",
                Compared,Disagreements,Worst);
}

void LunarBounceChecks(const std::shared_ptr<LunarAlbedoMap>& Map)
{
    const SpatialVector Position{0,0,0}, Normal{0,0,1}, Moon = Unit({-1,0,1});
    constexpr double Albedo = 0.8, Threshold = 1.0/1000.0;
    const double SupportedFraction = 0.5-(std::asin(Threshold)+Threshold*std::sqrt(1-Threshold*Threshold))/Pi;
    const double Expected = SupportedFraction*Albedo*Moon.Z;
    DistanceFieldScene Wall; Wall.AddPrimitive({DistancePrimitiveKind::HalfSpace,{1,0,0},{-1,0,0},0,Albedo});
    constexpr int Runs = 2400, Frames = 16; Moments Fresh, Final, Average;
    for (int Run = 0; Run < Runs; ++Run)
    {
        LunarBounceReservoir Previous; double Mean = 0;
        for (int Frame = 0; Frame < Frames; ++Frame)
        {
            DeterministicRandom R(0xb131ull+static_cast<uint64_t>(Run)*0x9e3779b97f4a7c15ull,0xa200ull+Frame);
            LunarBounceReservoir Current; Current.Stream(Wall,Position,Normal,Moon,1.0,1,R);
            if (Frame == 0) { auto Initial = Current; Initial.Finalize(); Fresh.Add(Initial.Irradiance()); }
            if (Frame > 0) Current.Reuse(Previous,Wall,20,R);
            Current.Finalize(); Mean += Current.Irradiance()/Frames; Previous = Current;
        }
        Final.Add(Previous.Irradiance()); Average.Add(Mean);
    }
    const auto Within = [&](const Moments& M) { return std::abs(M.Mean()-Expected) < 5*M.Error()+Expected*0.001; };
    Require(Within(Fresh) && Within(Final) && Within(Average),
            "fresh and same-receiver temporal lunar bounces agree with analytic infinite-wall irradiance");
    std::printf("wall analytic %.9g; fresh %.9g SE %.9g; temporal final %.9g SE %.9g; average %.9g SE %.9g\n",
                Expected,Fresh.Mean(),Fresh.Error(),Final.Mean(),Final.Error(),Average.Mean(),Average.Error());
    DeterministicRandom R(91,7); LunarBounceReservoir Prior, Moved, ChangedMoon, ChangedFlux;
    Prior.Stream(Wall,Position,Normal,Moon,1.0,8,R); Prior.Finalize();
    Moved.Stream(Wall,{0.01,0,0},Normal,Moon,1.0,1,R); Moved.Reuse(Prior,Wall,20,R);
    ChangedMoon.Stream(Wall,Position,Normal,{0,0,1},1.0,1,R); ChangedMoon.Reuse(Prior,Wall,20,R);
    ChangedFlux.Stream(Wall,Position,Normal,Moon,0.5,1,R); ChangedFlux.Reuse(Prior,Wall,20,R);
    Require(Moved.SampleCount() == 1 && ChangedMoon.SampleCount() == 1 && ChangedFlux.SampleCount() == 1,
            "lunar temporal reuse rejects a changed receiver, direction or irradiance");
    DistanceFieldScene Obscured = Wall;
    Obscured.AddPrimitive({DistancePrimitiveKind::HalfSpace,{0,0,2},{0,0,-1},0,0});
    LunarBounceReservoir Shadow; Shadow.Stream(Obscured,Position,Normal,Moon,1.0,128,R); Shadow.Finalize();
    Require(Shadow.SampleCount() == 128 && Shadow.Irradiance() == 0.0,
            "zero-weight lunar samples count and an SDF ceiling blocks the lunar bounce");

    auto Scene = GroundScene(); Scene.Distance = Wall; ReferenceRenderer Renderer; Renderer.SetScene(Scene);
    auto S = BasicSettings(); S.AtmosphereEnabled = false; S.Lunar.Albedo = Map; S.Lunar.Direction = Moon;
    S.SunDirection = {-Moon.X,-Moon.Y,-Moon.Z}; S.LunarIndirectLighting = true; S.IndirectSamples = 64;
    S.AccumulationFrames = 16; const auto Camera = CameraToward({0,0,2},{0,0,-1},0.6);
    const LunarDiscProjection Disc(S.Lunar,S.SunDirection,S.SunIrradiance);
    const double ExpectedRed = Disc.Irradiance().R * 0.5 * Albedo * Moon.Z / Pi;
    S.IndirectTemporalReuse = false; const auto FreshImage = Renderer.Render(Camera,S);
    S.IndirectTemporalReuse = true; const auto TemporalImage = Renderer.Render(Camera,S);
    S.LunarIndirectLighting = false; const auto Disabled = Renderer.Render(Camera,S);
    Require(Disabled.Pixels[0].R == 0.0 && std::abs(FreshImage.Pixels[0].R/ExpectedRed-1.0) < 0.08 &&
            std::abs(TemporalImage.Pixels[0].R/ExpectedRed-1.0) < 0.08,
            "reference renderer receives the lunar one-bounce path, with and without temporal reuse");
    Scene.Distance = Obscured; Renderer.SetScene(Scene); S.LunarIndirectLighting = true;
    Require(Renderer.Render(Camera,S).Pixels[0].R == 0.0, "renderer lunar bounce respects SDF occlusion");
    std::printf("renderer lunar bounce red expected %.12g, fresh %.12g, temporal %.12g, disabled %.12g\n",
                ExpectedRed,FreshImage.Pixels[0].R,TemporalImage.Pixels[0].R,Disabled.Pixels[0].R);
}

void Capture(const std::shared_ptr<LunarAlbedoMap>& Map, const std::string& Folder)
{
    constexpr double Latitude = -26.2, Longitude = 28.3, Height = 1640;
    auto CaptureAt = [&](double Julian, const char* Prefix, double Exposure, double Fov) {
        const auto Sun = SunDirectionLocal(SunPosition(Julian,Latitude,Longitude));
        const auto Moon = MoonTopocentric(MoonGeocentric(Julian+69.184/86400.0),Julian,Latitude,Longitude,Height);
        std::ifstream Csv("Exhibits/NightSky/Data/Hipparcos8.csv");
        auto Stars = std::make_shared<std::vector<StarRecord>>(DecodeHipparcosCatalogue(Csv,Julian,Latitude,Longitude));
        auto S = BasicSettings(); S.Width = S.Height = 384; S.AtmosphereEnabled = true; S.SunDirection = Sun;
        S.SunIrradiance = {20,20,20}; S.Lunar.Albedo = Map; S.Lunar.Direction = Moon.Direction;
        S.Lunar.AngularRadiusRad = Moon.AngularRadiusRad; S.Lunar.North = {0,0,1};
        S.LunarDiscEnabled = true; S.SiderealStarsEnabled = true; S.SiderealStars = Stars;
        const auto Camera = CameraToward({0,0,Height},Moon.Direction,Fov);
        ReferenceRenderer Renderer; Renderer.SetScene({}); const auto Image = Renderer.Render(Camera,S);
        SavePpm(Folder + "/" + Prefix + ".ppm",Image,Exposure);
        std::printf("capture %s sun elevation %.4f deg, moon elevation %.4f deg, apparent diameter %.2f arcsec\n",
            Prefix,std::asin(Sun.Z)/Rad,std::asin(Moon.Direction.Z)/Rad,2*Moon.AngularRadiusRad/Rad*3600);
    };
    CaptureAt(JulianDate(2026,10,18,13.0),"day-moon",1.0,3*Rad);
    CaptureAt(JulianDate(2026,10,26,18.5),"night-moon",2.0,3*Rad);
    const double Day = JulianDate(2026,10,18,13.0);
    const auto DayMoon = MoonTopocentric(MoonGeocentric(Day+69.184/86400.0),Day,Latitude,Longitude,Height);
    std::ifstream DayCsv("Exhibits/NightSky/Data/Hipparcos8.csv");
    auto DayStars = std::make_shared<std::vector<StarRecord>>(DecodeHipparcosCatalogue(DayCsv,Day,Latitude,Longitude));
    auto DaySettings = BasicSettings(); DaySettings.Width = 640; DaySettings.Height = 360;
    DaySettings.AtmosphereEnabled = true; DaySettings.SunIrradiance = {20,20,20};
    DaySettings.SunDirection = SunDirectionLocal(SunPosition(Day,Latitude,Longitude));
    DaySettings.Lunar.Albedo = Map; DaySettings.Lunar.Direction = DayMoon.Direction;
    DaySettings.Lunar.AngularRadiusRad = DayMoon.AngularRadiusRad; DaySettings.Lunar.North = {0,0,1};
    DaySettings.LunarDiscEnabled = true; DaySettings.SiderealStarsEnabled = true; DaySettings.SiderealStars = DayStars;
    const auto DayCamera = CameraToward({0,0,Height},DayMoon.Direction,60*Rad,16.0/9.0);
    ReferenceRenderer DayRenderer; DayRenderer.SetScene({});
    SavePpm(Folder + "/day-sky.ppm",DayRenderer.Render(DayCamera,DaySettings),1.0);
    DaySettings.StarBrightnessScale = 100000.0;
    SavePpm(Folder + "/day-stars-boosted.ppm",DayRenderer.Render(DayCamera,DaySettings),1.0);
    std::printf("capture day-sky and day-stars-boosted: identical view/exposure, latter uses nonphysical 100000x star gain\n");
    const double Night = JulianDate(2026,10,26,18.5);
    std::ifstream Csv("Exhibits/NightSky/Data/Hipparcos8.csv");
    auto Stars = std::make_shared<std::vector<StarRecord>>(DecodeHipparcosCatalogue(Csv,Night,Latitude,Longitude));
    auto S = BasicSettings(); S.Width = 640; S.Height = 360; S.AtmosphereEnabled = true;
    S.SunIrradiance = {20,20,20}; S.SunDirection = SunDirectionLocal(SunPosition(Night,Latitude,Longitude));
    S.SiderealStarsEnabled = true; S.SiderealStars = Stars;
    SpatialVector Aim{0,0,1}; double Brightest = 100;
    for (const auto& Star : *Stars)
        if (Star.Direction.Z > 0.35 && Star.Magnitude < Brightest) { Brightest = Star.Magnitude; Aim = Star.Direction; }
    ReferenceRenderer Renderer; Renderer.SetScene({});
    SavePpm(Folder + "/night-stars.ppm",Renderer.Render(CameraToward({0,0,Height},Aim,60*Rad,16.0/9.0),S),100000);
    std::printf("capture night-stars brightest centred catalogue magnitude %.3f; long illustrative exposure\n",Brightest);
}
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    const char* MapPath = ArgumentCount > 1 ? Arguments[1] : "EngineContent/CelestialMaps/MoonAlbedo2048.ppm";
    auto Map = std::make_shared<LunarAlbedoMap>();
    if (!Map->LoadPpm(MapPath)) { std::fprintf(stderr,"could not load actual demo Moon map %s\n",MapPath); return 2; }
    std::printf("actual demo Moon map %ux%u\n",Map->Width(),Map->Height());
    CatalogueChecks(); LunarModelChecks(Map); AtmosphereChecks(); BackgroundChecks(Map);
    LunarDirectChecks(Map); IntersectionChecks(); LunarBounceChecks(Map);
    if (ArgumentCount > 2) Capture(Map,Arguments[2]);
    std::printf("%s %d/%d checks; reference CPU subset only, not full ReSTIR GI or measured-sky validation\n",
                Failures ? "FAIL" : "PASS",Checks-Failures,Checks);
    return Failures ? 1 : 0;
}
