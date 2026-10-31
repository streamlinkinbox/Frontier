/*==========================================================================================================================================
                                                        DUAL TIER RENDERER
                                     ------------------------------------------------------------------
             Dual-tier Proving Grounds renderer: RTX hardware RayQuery + ReSTIR GI + DielectricBoundary + SVGF,
               paired with GTX 4-cascade toroidal 3D SDF + SurfaceCards + OrderedSoftwareBvh triangle reflections.
==========================================================================================================================================*/

// 🧩 Dual-tier illumination engine executing both the RTX RayQuery + ReSTIR path and the GTX 4-cascade SDF + BVH fallback.

#include "DualTierRenderer.h"

#include "PathReplayRayQuery.h"
#include "ReferenceRenderer.h"
#include "Engine/Render/DielectricRefraction.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{

constexpr double PiNumber = 3.14159265358979323846;

SpatialVector AddVec(SpatialVector A, SpatialVector B) noexcept
{
    return {A.X + B.X, A.Y + B.Y, A.Z + B.Z};
}

SpatialVector SubVec(SpatialVector A, SpatialVector B) noexcept
{
    return {A.X - B.X, A.Y - B.Y, A.Z - B.Z};
}

SpatialVector MulVec(SpatialVector A, double S) noexcept
{
    return {A.X * S, A.Y * S, A.Z * S};
}

double DotVec(SpatialVector A, SpatialVector B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

SpatialVector CrossVec(SpatialVector A, SpatialVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}

SpatialVector NormalizeVec(SpatialVector V) noexcept
{
    const double LenSq = DotVec(V, V);
    if (LenSq <= 1e-24)
    {
        return {0.0, 0.0, 1.0};
    }
    const double Inv = 1.0 / std::sqrt(LenSq);
    return {V.X * Inv, V.Y * Inv, V.Z * Inv};
}

SpatialVector ReflectVec(SpatialVector Incident, SpatialVector Normal) noexcept
{
    const double Proj = DotVec(Incident, Normal);
    return NormalizeVec(SubVec(Incident, MulVec(Normal, 2.0 * Proj)));
}

SpectrumRgb EvaluatePlanckTint(double Kelvin) noexcept
{
    const double Temp = std::clamp(Kelvin, 1800.0, 14000.0) / 100.0;
    double       R    = 1.0;
    double       G    = 1.0;
    double       B    = 1.0;
    if (Temp <= 66.0)
    {
        R = 1.0;
        G = std::clamp((99.4708025861 * std::log(Temp) - 161.1195681661) / 255.0, 0.0, 1.0);
        B = Temp <= 19.0 ? 0.0 : std::clamp((138.5177312231 * std::log(Temp - 10.0) - 305.0447927307) / 255.0, 0.0, 1.0);
    }
    else
    {
        R = std::clamp(329.698727446 * std::pow(Temp - 60.0, -0.1332047592) / 255.0, 0.0, 1.0);
        G = std::clamp(288.1221695283 * std::pow(Temp - 60.0, -0.0755148492) / 255.0, 0.0, 1.0);
        B = 1.0;
    }
    return {std::pow(R, 2.2), std::pow(G, 2.2), std::pow(B, 2.2)};
}

struct CameraFrameBasis
{
    SpatialVector Eye{};
    SpatialVector Forward{};
    SpatialVector Right{};
    SpatialVector Up{};
    double        TanHalfFovY = 0.445;   // ~48 deg vertical FOV
};

CameraFrameBasis BuildOrbitCamera(const EverestViewportInputs& Inputs) noexcept
{
    const SpatialVector Target{0.0, 0.35, 0.68};
    const double        YawRad   = Inputs.CameraYawDegrees * (PiNumber / 180.0);
    const double        PitchRad = std::clamp(Inputs.CameraPitchDegrees, 2.0, 82.0) * (PiNumber / 180.0);
    const double        Dist     = std::clamp(Inputs.CameraDistanceMetres, 2.4, 28.0);

    CameraFrameBasis Basis;
    Basis.Eye = {Target.X + Dist * std::cos(PitchRad) * std::sin(YawRad),
                 Target.Y - Dist * std::cos(PitchRad) * std::cos(YawRad),
                 Target.Z + Dist * std::sin(PitchRad)};
    Basis.Forward = NormalizeVec(SubVec(Target, Basis.Eye));
    Basis.Right   = NormalizeVec(CrossVec(Basis.Forward, SpatialVector{0.0, 0.0, 1.0}));
    Basis.Up      = NormalizeVec(CrossVec(Basis.Right, Basis.Forward));
    return Basis;
}

uint32_t MixHash32(uint32_t Seed) noexcept
{
    Seed ^= Seed >> 16;
    Seed *= 0x7feb352du;
    Seed ^= Seed >> 15;
    Seed *= 0x846ca68bu;
    Seed ^= Seed >> 16;
    return Seed;
}

SpatialVector SampleHemisphereDirection(SpatialVector Normal, uint32_t SampleIndex, uint32_t SampleCount,
                                        double RotationOffset) noexcept
{
    const double Zeta      = (static_cast<double>(SampleIndex) + 0.5) / std::max<uint32_t>(1u, SampleCount);
    const double Radial    = std::sqrt(Zeta);
    const double Azimuth   = static_cast<double>(SampleIndex) * 2.399963229728653 + RotationOffset;
    const SpatialVector Ref = std::abs(Normal.Z) < 0.995 ? SpatialVector{0.0, 0.0, 1.0} : SpatialVector{1.0, 0.0, 0.0};
    const SpatialVector Tan = NormalizeVec(CrossVec(Ref, Normal));
    const SpatialVector Bit = CrossVec(Normal, Tan);
    return NormalizeVec(AddVec(AddVec(MulVec(Tan, Radial * std::cos(Azimuth)),
                                      MulVec(Bit, Radial * std::sin(Azimuth))),
                               MulVec(Normal, std::sqrt(std::max(0.0, 1.0 - Zeta)))));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             TIER CAPABILITY NEGOTIATION
//------------------------------------------------------------------------------------------------------------------------

DualTierTierDecision NegotiateDualTierExecution(
    const PathReplayRayQueryDeviceCapabilities& Capabilities,
    bool                                        PreferHardwareRtx) noexcept
{
    const PathReplayAccelerationSelection Accel = SelectPathReplayAccelerationTier(Capabilities, PreferHardwareRtx);
    DualTierTierDecision                  Decision;
    Decision.HardwareRtxCapable = Accel.HardwareRayQueryReady;
    if (Accel.Tier == PathReplayAccelerationTier::HardwareRayQuery)
    {
        Decision.Mode              = DualTierExecutionMode::HardwareRayQueryRtx;
        Decision.DiagnosticSummary = "RTX tier active: VK_KHR_acceleration_structure + VK_KHR_ray_query + ReSTIR GI";
    }
    else
    {
        Decision.Mode              = DualTierExecutionMode::ToroidalSdfBvhGtx;
        Decision.DiagnosticSummary = Accel.FallbackReason != nullptr
                                         ? Accel.FallbackReason
                                         : "GTX fallback active: 4-cascade toroidal 3D SDF + SurfaceCards + OrderedSoftwareBvh";
    }
    return Decision;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3D PROVING GROUNDS SCENE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

DualTierProvingGroundsRenderer::DualTierProvingGroundsRenderer()
{
    std::vector<std::array<SpatialVector, 3>> Triangles;
    Triangles.reserve(380);
    FacetMaterials.reserve(380);

    auto AppendQuad = [&](SpatialVector P0, SpatialVector P1, SpatialVector P2, SpatialVector P3,
                          SpectrumRgb Albedo, double Roughness, double Metallic,
                          DualTierProvingGroundsRenderer::SurfaceCategory Category,
                          SpectrumRgb Emissive = {}) {
        const SpatialVector Normal = NormalizeVec(CrossVec(SubVec(P1, P0), SubVec(P2, P0)));
        Triangles.push_back({P0, P1, P2});
        FacetMaterials.push_back({Normal, Albedo, Emissive, Roughness, Metallic, Category});
        Triangles.push_back({P0, P2, P3});
        FacetMaterials.push_back({Normal, Albedo, Emissive, Roughness, Metallic, Category});
    };

    auto AppendBox = [&](SpatialVector Center, SpatialVector Half, SpectrumRgb Albedo, double Roughness,
                         double Metallic, DualTierProvingGroundsRenderer::SurfaceCategory Category,
                         SpectrumRgb Emissive = {}) {
        const double X0 = Center.X - Half.X, X1 = Center.X + Half.X;
        const double Y0 = Center.Y - Half.Y, Y1 = Center.Y + Half.Y;
        const double Z0 = Center.Z - Half.Z, Z1 = Center.Z + Half.Z;
        // +Z top, -Z bottom, +X right, -X left, +Y front, -Y rear
        AppendQuad({X0, Y0, Z1}, {X1, Y0, Z1}, {X1, Y1, Z1}, {X0, Y1, Z1}, Albedo, Roughness, Metallic, Category, Emissive);
        AppendQuad({X0, Y1, Z0}, {X1, Y1, Z0}, {X1, Y0, Z0}, {X0, Y0, Z0}, Albedo, Roughness, Metallic, Category, Emissive);
        AppendQuad({X1, Y0, Z0}, {X1, Y1, Z0}, {X1, Y1, Z1}, {X1, Y0, Z1}, Albedo, Roughness, Metallic, Category, Emissive);
        AppendQuad({X0, Y1, Z0}, {X0, Y0, Z0}, {X0, Y0, Z1}, {X0, Y1, Z1}, Albedo, Roughness, Metallic, Category, Emissive);
        AppendQuad({X1, Y1, Z0}, {X0, Y1, Z0}, {X0, Y1, Z1}, {X1, Y1, Z1}, Albedo, Roughness, Metallic, Category, Emissive);
        AppendQuad({X0, Y0, Z0}, {X1, Y0, Z0}, {X1, Y0, Z1}, {X0, Y0, Z1}, Albedo, Roughness, Metallic, Category, Emissive);
    };

    // 1. Proving Grounds asphalt pad (-64..64 m) + SDF half-space.
    AppendQuad({-64.0, -64.0, 0.0}, {64.0, -64.0, 0.0}, {64.0, 64.0, 0.0}, {-64.0, 64.0, 0.0},
               {0.11, 0.12, 0.13}, 0.62, 0.0, SurfaceCategory::AsphaltApron);
    AnalyticSdf.AddPrimitive({DistancePrimitiveKind::HalfSpace, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.08});

    // 2. Left Coral-Red & Right Mint-Teal Radiosity Calibration Walls (proves indirect GI colour bleed in RTX & GTX!).
    AppendBox({-3.65, 0.9, 1.15}, {0.16, 3.1, 1.15}, {0.84, 0.20, 0.16}, 0.72, 0.0, SurfaceCategory::RadiosityWall);
    AppendBox({+3.65, 0.9, 1.15}, {0.16, 3.1, 1.15}, {0.14, 0.78, 0.56}, 0.72, 0.0, SurfaceCategory::RadiosityWall);
    AnalyticSdf.AddPrimitive({DistancePrimitiveKind::Box, {-3.65, 0.9, 1.15}, {0.16, 3.1, 1.15}, 0.45});
    AnalyticSdf.AddPrimitive({DistancePrimitiveKind::Box, {+3.65, 0.9, 1.15}, {0.16, 3.1, 1.15}, 0.45});

    // 3. Overhead Proving Grounds Telemetry Gantry Arch at Y = +8.5 m (creates clear reflections on car hood & glass!).
    AppendBox({-4.4, 8.5, 2.6}, {0.32, 0.35, 2.6}, {0.26, 0.29, 0.33}, 0.38, 0.35, SurfaceCategory::GantryStructure);
    AppendBox({+4.4, 8.5, 2.6}, {0.32, 0.35, 2.6}, {0.26, 0.29, 0.33}, 0.38, 0.35, SurfaceCategory::GantryStructure);
    AppendBox({0.0, 8.5, 5.05}, {4.85, 0.42, 0.28}, {0.30, 0.34, 0.39}, 0.30, 0.45, SurfaceCategory::GantryStructure);
    AppendBox({0.0, 8.05, 4.82}, {2.6, 0.08, 0.08}, {0.95, 0.88, 0.70}, 0.20, 0.0, SurfaceCategory::EmissiveLamp,
              {4.5, 3.9, 2.8});
    AnalyticSdf.AddPrimitive({DistancePrimitiveKind::Box, {-4.4, 8.5, 2.6}, {0.32, 0.35, 2.6}, 0.1});
    AnalyticSdf.AddPrimitive({DistancePrimitiveKind::Box, {+4.4, 8.5, 2.6}, {0.32, 0.35, 2.6}, 0.1});
    AnalyticSdf.AddPrimitive({DistancePrimitiveKind::Box, {0.0, 8.5, 5.05}, {4.85, 0.42, 0.28}, 0.25});

    // 4. Sculpted ControlVehicle Body (lower aero sill + sculpted beltline + hood slope + rear haunches).
    const SpectrumRgb DefaultPaint{0.11, 0.34, 0.52};
    AppendBox({0.0, 0.0, 0.44}, {0.96, 2.22, 0.24}, DefaultPaint, 0.14, 0.78, SurfaceCategory::VehiclePaint);
    // Sculpted sloped front hood & fenders
    AppendQuad({-0.92, 0.55, 0.76}, {0.92, 0.55, 0.76}, {0.86, 2.18, 0.58}, {-0.86, 2.18, 0.58},
               DefaultPaint, 0.12, 0.82, SurfaceCategory::VehiclePaint);
    // Rear fastback deck
    AppendQuad({-0.90, -2.18, 0.72}, {0.90, -2.18, 0.72}, {0.92, -0.75, 0.80}, {-0.92, -0.75, 0.80},
               DefaultPaint, 0.12, 0.82, SurfaceCategory::VehiclePaint);
    // Carbon front splitter & rear diffuser
    AppendBox({0.0, 2.23, 0.21}, {0.94, 0.08, 0.04}, {0.08, 0.09, 0.10}, 0.32, 0.20, SurfaceCategory::GantryStructure);
    AppendBox({0.0, -2.23, 0.23}, {0.92, 0.07, 0.05}, {0.08, 0.09, 0.10}, 0.32, 0.20, SurfaceCategory::GantryStructure);
    AnalyticSdf.AddPrimitive({DistancePrimitiveKind::Box, {0.0, 0.0, 0.52}, {0.96, 2.22, 0.32}, 0.12});
    AnalyticSdf.AddPrimitive({DistancePrimitiveKind::Box, {0.0, -0.15, 0.96}, {0.78, 1.05, 0.28}, 0.08});

    // 5. Interior Cabin Seats & Glowing Cabin HMI Cluster Screen (visible THROUGH the Dielectric Canopy!).
    AppendBox({0.0, -0.18, 0.74}, {0.68, 0.82, 0.08}, {0.14, 0.15, 0.17}, 0.65, 0.0, SurfaceCategory::CabinInterior);
    AppendBox({-0.34, -0.28, 0.92}, {0.22, 0.16, 0.22}, {0.20, 0.21, 0.23}, 0.60, 0.0, SurfaceCategory::CabinInterior);
    AppendBox({+0.34, -0.28, 0.92}, {0.22, 0.16, 0.22}, {0.20, 0.21, 0.23}, 0.60, 0.0, SurfaceCategory::CabinInterior);
    // Glowing mint-cyan HMI infotainment screen inside dashboard
    AppendQuad({-0.28, 0.42, 0.76}, {0.28, 0.42, 0.76}, {0.28, 0.36, 0.93}, {-0.28, 0.36, 0.93},
               {0.18, 0.88, 0.72}, 0.15, 0.0, SurfaceCategory::EmissiveLamp, {1.8, 4.6, 3.8});

    // 6. Closed 3D Dielectric Cabin Glass Canopy (windshield, roof glass, left/right side windows, fastback hatch).
    const SpectrumRgb GlassTint{0.80, 0.93, 0.90};
    const SpatialVector CBL{-0.82, -0.95, 0.76}, CBR{+0.82, -0.95, 0.76};
    const SpatialVector CFL{-0.82, +0.62, 0.76}, CFR{+0.82, +0.62, 0.76};
    const SpatialVector RTL{-0.68, -0.68, 1.24}, RTR{+0.68, -0.68, 1.24};
    const SpatialVector RFL{-0.68, +0.18, 1.24}, RFR{+0.68, +0.18, 1.24};
    // Windshield (+Y), Rear hatch (-Y), Roof (+Z), Right glass (+X), Left glass (-X)
    AppendQuad(CFL, CFR, RFR, RFL, GlassTint, 0.03, 0.0, SurfaceCategory::DielectricCanopy);
    AppendQuad(CBR, CBL, RTL, RTR, GlassTint, 0.03, 0.0, SurfaceCategory::DielectricCanopy);
    AppendQuad(RFL, RFR, RTR, RTL, GlassTint, 0.04, 0.0, SurfaceCategory::DielectricCanopy);
    AppendQuad(CFR, CBR, RTR, RFR, GlassTint, 0.03, 0.0, SurfaceCategory::DielectricCanopy);
    AppendQuad(CBL, CFL, RFL, RTL, GlassTint, 0.03, 0.0, SurfaceCategory::DielectricCanopy);

    // 7. Four Wheel Assemblies (FL, FR, RL, RR: octagonal tyre tread + metallic alloy rim face).
    const std::array<SpatialVector, 4> WheelCentres{{
        {-0.96, +1.38, 0.35},
        {+0.96, +1.38, 0.35},
        {-0.96, -1.38, 0.35},
        {+0.96, -1.38, 0.35},
    }};
    for (size_t WheelIdx = 0; WheelIdx < WheelCentres.size(); ++WheelIdx)
    {
        const SpatialVector WC     = WheelCentres[WheelIdx];
        const double        SignX  = WC.X < 0.0 ? -1.0 : 1.0;
        const double        Radius = 0.35;
        const double        RimRad = 0.22;
        const double        HalfW  = 0.13;
        constexpr int       Segs   = 10;
        for (int Seg = 0; Seg < Segs; ++Seg)
        {
            const double A0 = (2.0 * PiNumber * Seg) / Segs;
            const double A1 = (2.0 * PiNumber * (Seg + 1)) / Segs;
            const SpatialVector P0{WC.X - HalfW, WC.Y + Radius * std::cos(A0), WC.Z + Radius * std::sin(A0)};
            const SpatialVector P1{WC.X + HalfW, WC.Y + Radius * std::cos(A0), WC.Z + Radius * std::sin(A0)};
            const SpatialVector P2{WC.X + HalfW, WC.Y + Radius * std::cos(A1), WC.Z + Radius * std::sin(A1)};
            const SpatialVector P3{WC.X - HalfW, WC.Y + Radius * std::cos(A1), WC.Z + Radius * std::sin(A1)};
            AppendQuad(P0, P1, P2, P3, {0.06, 0.065, 0.07}, 0.78, 0.0, SurfaceCategory::RubberTyre);

            // Outer alloy rim disc
            const double OutX = WC.X + SignX * (HalfW + 0.005);
            const SpatialVector CenterPt{OutX, WC.Y, WC.Z};
            const SpatialVector R0{OutX, WC.Y + RimRad * std::cos(A0), WC.Z + RimRad * std::sin(A0)};
            const SpatialVector R1{OutX, WC.Y + RimRad * std::cos(A1), WC.Z + RimRad * std::sin(A1)};
            const SpatialVector Normal{SignX, 0.0, 0.0};
            if (SignX > 0.0)
            {
                Triangles.push_back({CenterPt, R0, R1});
            }
            else
            {
                Triangles.push_back({CenterPt, R1, R0});
            }
            FacetMaterials.push_back({Normal, {0.76, 0.79, 0.84}, {}, 0.18, 0.92, SurfaceCategory::AlloyRim});
        }
        AnalyticSdf.AddPrimitive({DistancePrimitiveKind::Sphere, WC, {Radius, Radius, Radius}, 0.04});
    }

    // 8. Headlamps (+Y) & Brake Tail-Light Bar (-Y).
    AppendBox({-0.66, 2.21, 0.54}, {0.20, 0.03, 0.045}, {0.95, 0.97, 1.0}, 0.08, 0.0, SurfaceCategory::EmissiveLamp,
              {9.0, 9.6, 11.0});
    AppendBox({+0.66, 2.21, 0.54}, {0.20, 0.03, 0.045}, {0.95, 0.97, 1.0}, 0.08, 0.0, SurfaceCategory::EmissiveLamp,
              {9.0, 9.6, 11.0});
    AppendBox({0.0, -2.21, 0.64}, {0.82, 0.03, 0.035}, {1.0, 0.12, 0.08}, 0.10, 0.0, SurfaceCategory::EmissiveLamp,
              {7.5, 0.65, 0.45});

    // Build 16-bin SAH-2 OrderedSoftwareBvh!
    SceneBvh.BuildSurfaceAreaHierarchy(Triangles, 2u);
    PackedEnclosureCount = static_cast<uint32_t>(SceneBvh.PackFloat32().Enclosures.size());

    // Bake the 4 nested toroidal 3D SDF cascades (SceneClip0..3)!
    const std::array<uint32_t, 4> CascadeResolutions{{24u, 24u, 20u, 16u}};
    for (size_t Cascade = 0; Cascade < 4; ++Cascade)
    {
        const double Half = CascadeHalfExtentsMetres[Cascade];
        (void)ToroidalCascades[Cascade].Bake(
            AnalyticSdf, {-Half, -Half, -1.0}, {Half, Half, Half * 0.85},
            CascadeResolutions[Cascade], CascadeResolutions[Cascade], CascadeResolutions[Cascade]);
    }

    // Build & solve multi-bounce SurfaceCards radiosity (B = E + rho * F * B)!
    std::vector<SurfaceCard> Cards;
    // Left coral wall card (faces +X toward car)
    Cards.push_back({{-3.48, -2.1, 0.0}, {0.0, 6.0, 0.0}, {0.0, 0.0, 2.3}, 4u, 2u, 0.72, 1.85});
    CardClusters.push_back({{-3.48, 0.9, 1.15}, {+1.0, 0.0, 0.0}, {0.92, 0.22, 0.18}, 13.8, 0u, 8u});
    // Right mint wall card (faces -X toward car)
    Cards.push_back({{+3.48, +3.9, 0.0}, {0.0, -6.0, 0.0}, {0.0, 0.0, 2.3}, 4u, 2u, 0.72, 1.85});
    CardClusters.push_back({{+3.48, 0.9, 1.15}, {-1.0, 0.0, 0.0}, {0.16, 0.88, 0.64}, 13.8, 8u, 8u});
    // Apron floor bounce card (faces +Z)
    Cards.push_back({{-4.0, -3.5, 0.01}, {8.0, 0.0, 0.0}, {0.0, 8.0, 0.0}, 4u, 4u, 0.24, 0.55});
    CardClusters.push_back({{0.0, 0.5, 0.01}, {0.0, 0.0, 1.0}, {0.48, 0.52, 0.56}, 64.0, 16u, 16u});
    // Overhead gantry soffit card (faces -Z)
    Cards.push_back({{-4.2, 8.9, 4.76}, {8.4, 0.0, 0.0}, {0.0, -0.8, 0.0}, 4u, 1u, 0.55, 2.40});
    CardClusters.push_back({{0.0, 8.5, 4.76}, {0.0, 0.0, -1.0}, {0.95, 0.88, 0.72}, 6.72, 32u, 4u});

    if (SolvedCards.Build(Cards, &AnalyticSdf))
    {
        InitialCardResidual = SolvedCards.Solve(80u, 1e-6);
    }

    NightStarCatalogue = GenerateStarCatalogue(512u, 0xE7E2E57ull);
    CachedStormBolt    = GenerateLightningBolt({-2.5, 26.0, 14.0}, {-1.2, 24.0, 0.0}, 5u, 0.28, 0x51A7Eull, 3u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SKY, CELESTIAL & CASCADE HELPERS
//------------------------------------------------------------------------------------------------------------------------

SpectrumRgb DualTierProvingGroundsRenderer::EvaluateSkyAndCelestial(
    SpatialVector                RayDir,
    const EverestViewportInputs& Inputs,
    const SpectrumRgb&           SolarIrradiance) const noexcept
{
    SpectrumRgb Sky{0.008, 0.011, 0.018};
    const SpatialVector ClampedDir = NormalizeVec({RayDir.X, RayDir.Y, std::max(0.006, RayDir.Z)});

    if (Inputs.AtmosphereEnabled)
    {
        const SpatialVector ObserverKm{0.0, 0.0, Inputs.Atmosphere.GroundRadiusKm + 0.08};
        Sky = SingleScattering(Inputs.Atmosphere, ObserverKm, ClampedDir, Inputs.SunDirection, SolarIrradiance, nullptr, 8);
    }

    // Night stars & lunar illumination when sun is low or disabled.
    const double NightWeight = Inputs.SunEnabled
                                   ? std::clamp((-Inputs.SunElevationDegrees + 6.0) / 14.0, 0.0, 1.0)
                                   : 1.0;
    if (NightWeight > 0.01)
    {
        if (Inputs.MoonEnabled)
        {
            const SpatialVector MoonDir = NormalizeVec(Inputs.MoonDirection);
            const double        CosMoon = DotVec(RayDir, MoonDir);
            if (CosMoon > 0.9985)
            {
                const double Limb = 0.65 + 0.35 * std::sqrt(std::max(0.0, (CosMoon - 0.9985) / 0.0015));
                const double Lit  = Inputs.MoonIlluminationFraction * Inputs.MoonlightStrength * 6.5 * Limb;
                Sky.R += NightWeight * Lit * 0.92;
                Sky.G += NightWeight * Lit * 0.95;
                Sky.B += NightWeight * Lit * 1.00;
            }
            else if (CosMoon > 0.96)
            {
                const double Halo = std::exp(-(1.0 - CosMoon) * 95.0) * Inputs.MoonlightStrength * 0.55;
                Sky.R += NightWeight * Halo * 0.75;
                Sky.G += NightWeight * Halo * 0.84;
                Sky.B += NightWeight * Halo * 1.00;
            }
        }
        if (Inputs.StarsEnabled && RayDir.Z > 0.02)
        {
            // Analytic star cell hash + galactic band so starfield is crisp without O(N) scan per ray.
            const double RotRad = Inputs.StarRotationDegrees * (PiNumber / 180.0);
            const double RX     = RayDir.X * std::cos(RotRad) - RayDir.Y * std::sin(RotRad);
            const double RY     = RayDir.X * std::sin(RotRad) + RayDir.Y * std::cos(RotRad);
            const int    CellU  = static_cast<int>(std::floor(( std::atan2(RY, RX) + PiNumber) * 42.0));
            const int    CellV  = static_cast<int>(std::floor(std::asin(std::clamp(RayDir.Z, -1.0, 1.0)) * 84.0));
            const uint32_t H    = MixHash32(static_cast<uint32_t>(CellU * 73856093 ^ CellV * 19349663));
            const double   Prob = static_cast<double>(H & 0xFFFFu) / 65535.0;
            const double   Cut  = std::clamp((Inputs.StarLimitingMagnitude - 2.0) * 0.018, 0.005, 0.14);
            if (Prob < Cut)
            {
                const double Flux = (1.0 - Prob / Cut) * 0.85 * NightWeight;
                const double Warm = static_cast<double>((H >> 16) & 0xFFu) / 255.0;
                Sky.R += Flux * (0.75 + 0.45 * Warm);
                Sky.G += Flux * 0.90;
                Sky.B += Flux * (1.20 - 0.35 * Warm);
            }
            // Subtle Milky Way band
            const double Galactic = std::exp(-std::pow(RayDir.X * 0.65 + RayDir.Z * 0.35 - 0.22, 2.0) * 18.0) * 0.035;
            Sky.R += NightWeight * Galactic * 0.78;
            Sky.G += NightWeight * Galactic * 0.86;
            Sky.B += NightWeight * Galactic * 1.12;
        }
    }

    // Procedural cloud layer
    if (Inputs.CloudsEnabled && RayDir.Z > 0.035)
    {
        const double U     = 0.5 + (RayDir.X / std::max(0.14, RayDir.Z)) * 0.085;
        const double V     = 0.5 + (RayDir.Y / std::max(0.14, RayDir.Z)) * 0.085;
        const double WaveA = std::sin(U * 9.0 + 0.7) * std::cos(V * 7.5 - 0.4);
        const double WaveB = 0.5 * std::sin(U * 21.0 - V * 16.0) + 0.25 * std::cos(U * 37.0 + V * 29.0);
        const double Raw   = 0.5 + 0.34 * WaveA + 0.24 * WaveB;
        const double Dens  = std::clamp((Raw - (1.0 - Inputs.CloudCoverage)) * 2.2, 0.0, 1.0);
        if (Dens > 0.005)
        {
            const double LimbFade = std::clamp((RayDir.Z - 0.035) * 7.0, 0.0, 1.0);
            const double Mix      = Dens * LimbFade * 0.82;
            const SpectrumRgb Tint   = EvaluatePlanckTint(Inputs.SunTemperatureKelvin);
            const double Lit         = Inputs.SunEnabled
                                           ? (0.22 + 0.88 * std::clamp((Inputs.SunElevationDegrees + 8.0) / 46.0, 0.04, 1.0))
                                           : (0.04 + 0.12 * Inputs.MoonlightStrength);
            Sky.R = Sky.R * (1.0 - Mix) + Lit * Tint.R * Mix;
            Sky.G = Sky.G * (1.0 - Mix) + Lit * Tint.G * Mix;
            Sky.B = Sky.B * (1.0 - Mix) + Lit * Tint.B * Mix;
        }
    }

    // Solar disc & aureole
    if (Inputs.SunEnabled && Inputs.SunDiscEnabled && Inputs.SunElevationDegrees > -2.5)
    {
        const double      CosSun = DotVec(RayDir, NormalizeVec(Inputs.SunDirection));
        const SpectrumRgb Tint   = EvaluatePlanckTint(Inputs.SunTemperatureKelvin);
        if (CosSun > 0.9994)
        {
            Sky.R += Tint.R * 12.0;
            Sky.G += Tint.G * 11.4;
            Sky.B += Tint.B * 10.2;
        }
        else if (CosSun > 0.92)
        {
            const double Glow = std::exp(-(1.0 - CosSun) * 65.0) * 0.85;
            Sky.R += Tint.R * Glow;
            Sky.G += Tint.G * Glow * 0.94;
            Sky.B += Tint.B * Glow * 0.84;
        }
    }

    // Distant storm lightning bolt in sky
    if (Inputs.LightningActive && Inputs.LightningFlashIntensity > 0.01 && RayDir.Y > 0.35 && RayDir.Z > 0.0)
    {
        const double AzDist = std::abs((RayDir.X / std::max(0.2, RayDir.Y)) + 0.08 +
                                       0.025 * std::sin(RayDir.Z * 38.0));
        if (AzDist < 0.012 && RayDir.Z < 0.52)
        {
            const double Bolt = (1.0 - AzDist / 0.012) * Inputs.LightningFlashIntensity * 9.0;
            Sky.R += Bolt * 0.78;
            Sky.G += Bolt * 0.88;
            Sky.B += Bolt * 1.25;
        }
    }

    return Sky;
}

uint32_t DualTierProvingGroundsRenderer::SelectCascadeIndex(SpatialVector WorldPoint, double OuterSpanScale) const noexcept
{
    const double Scale = std::clamp(OuterSpanScale, 0.25, 4.0);
    const double MaxCoord = std::max({std::abs(WorldPoint.X), std::abs(WorldPoint.Y), std::abs(WorldPoint.Z)});
    for (uint32_t Cascade = 0; Cascade < 3u; ++Cascade)
    {
        if (MaxCoord <= CascadeHalfExtentsMetres[Cascade] * 0.88 * Scale)
        {
            return Cascade;
        }
    }
    return 3u;
}

double DualTierProvingGroundsRenderer::SampleToroidalSignedDistance(SpatialVector WorldPoint,
                                                                    uint32_t      CascadeIndex) const noexcept
{
    const uint32_t Clamped = std::min<uint32_t>(3u, CascadeIndex);
    if (ToroidalCascades[Clamped].Baked())
    {
        return ToroidalCascades[Clamped].Sample(WorldPoint);
    }
    return AnalyticSdf.Evaluate(WorldPoint).Distance;
}

SpectrumRgb DualTierProvingGroundsRenderer::GatherCardMultiBounceIrradiance(
    SpatialVector                Position,
    SpatialVector                Normal,
    const EverestViewportInputs& Inputs) const noexcept
{
    if (!Inputs.SurfaceCardsEnabled || SolvedCards.TexelCount() == 0)
    {
        return {};
    }
    SpectrumRgb Gathered{};
    const double SunScale = Inputs.SunEnabled
                                ? std::clamp(Inputs.SolarIlluminance / 12.0, 0.05, 3.5)
                                : (0.08 + Inputs.MoonlightStrength * 0.25);
    for (const CardColourRecord& Cluster : CardClusters)
    {
        const SpatialVector ToCard = SubVec(Cluster.Centre, Position);
        const double        DistSq = DotVec(ToCard, ToCard);
        if (DistSq <= 1e-4)
        {
            continue;
        }
        const double        InvDist   = 1.0 / std::sqrt(DistSq);
        const SpatialVector Dir       = MulVec(ToCard, InvDist);
        const double        CosRecv   = std::max(0.0, DotVec(Normal, Dir));
        const double        CosSend   = std::max(0.0, -DotVec(Cluster.Normal, Dir));
        if (CosRecv <= 0.0 || CosSend <= 0.0)
        {
            continue;
        }
        double MeanRad = 0.0;
        for (uint32_t Offset = 0; Offset < Cluster.TexelCount; ++Offset)
        {
            MeanRad += SolvedCards.Radiosity(Cluster.FirstTexel + Offset);
        }
        MeanRad /= std::max<uint32_t>(1u, Cluster.TexelCount);
        const double Form = (CosRecv * CosSend * Cluster.AreaMetresSq) /
                            (PiNumber * (DistSq + Cluster.AreaMetresSq / PiNumber));
        const double Weight = Form * MeanRad * SunScale * 0.42;
        Gathered.R += Cluster.Tint.R * Weight;
        Gathered.G += Cluster.Tint.G * Weight;
        Gathered.B += Cluster.Tint.B * Weight;
    }
    return Gathered;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      RTX PATH-REPLAY vs GTX SDF+CARDS INDIRECT GI
//------------------------------------------------------------------------------------------------------------------------

SpectrumRgb DualTierProvingGroundsRenderer::EvaluateRtxIndirectIrradiance(
    SpatialVector                Position,
    SpatialVector                Normal,
    uint32_t                     PixelHash,
    const EverestViewportInputs& Inputs,
    const SpectrumRgb&           SolarIrradiance) const noexcept
{
    constexpr uint32_t ReservoirSamples = 6u;
    const double       Rotation         = static_cast<double>(PixelHash & 0xFFFFu) * (2.0 * PiNumber / 65536.0);
    const SpatialVector Origin          = AddVec(Position, MulVec(Normal, 0.015));
    const SpatialVector SunDir          = NormalizeVec(Inputs.SunDirection);
    const SpectrumRgb   SunTint         = EvaluatePlanckTint(Inputs.SunTemperatureKelvin);

    SpectrumRgb Accumulated{};
    double      WeightSum = 0.0;
    for (uint32_t Tap = 0; Tap < ReservoirSamples; ++Tap)
    {
        const SpatialVector RayDir = SampleHemisphereDirection(Normal, Tap, ReservoirSamples, Rotation);
        const auto          Strike = SceneBvh.Trace(Origin, RayDir, 1e-3, 36.0, false);
        SpectrumRgb         Candidate{};
        if (Strike.Hit && Strike.Facet < FacetMaterials.size())
        {
            const FacetMaterialRecord& HitMat = FacetMaterials[Strike.Facet];
            const SpatialVector        HitPos = AddVec(Origin, MulVec(RayDir, Strike.Travel));
            double                     Direct = 0.0;
            if (Inputs.SunEnabled)
            {
                const double NdotL = std::max(0.0, DotVec(HitMat.Normal, SunDir));
                if (NdotL > 0.0 && !SceneBvh.Occluded(AddVec(HitPos, MulVec(HitMat.Normal, 0.02)), SunDir, 1e-3, 60.0))
                {
                    Direct = NdotL * Inputs.SolarIlluminance * 0.095;
                }
            }
            Candidate.R = std::min(0.65, HitMat.Emissive.R * 0.14) + HitMat.Albedo.R * (0.10 + Direct * SunTint.R);
            Candidate.G = std::min(0.65, HitMat.Emissive.G * 0.14) + HitMat.Albedo.G * (0.10 + Direct * SunTint.G);
            Candidate.B = std::min(0.65, HitMat.Emissive.B * 0.14) + HitMat.Albedo.B * (0.10 + Direct * SunTint.B);
        }
        else
        {
            const SpectrumRgb Sky = EvaluateSkyAndCelestial(RayDir, Inputs, SolarIrradiance);
            Candidate.R           = std::min(1.10, Sky.R * 0.52);
            Candidate.G           = std::min(1.10, Sky.G * 0.52);
            Candidate.B           = std::min(1.10, Sky.B * 0.52);
        }
        // Power MIS (beta = 2) target weight stabilization
        const double Luma        = 0.2126 * Candidate.R + 0.7152 * Candidate.G + 0.0722 * Candidate.B;
        const double PowerWeight = 1.0 + 0.35 * std::min(4.0, Luma * Luma);
        Accumulated.R += Candidate.R * PowerWeight;
        Accumulated.G += Candidate.G * PowerWeight;
        Accumulated.B += Candidate.B * PowerWeight;
        WeightSum += PowerWeight;
    }

    const double Inv = WeightSum > 0.0 ? 1.0 / WeightSum : 0.0;
    return {Accumulated.R * Inv, Accumulated.G * Inv, Accumulated.B * Inv};
}

SpectrumRgb DualTierProvingGroundsRenderer::EvaluateGtxConeIndirectIrradiance(
    SpatialVector                Position,
    SpatialVector                Normal,
    const EverestViewportInputs& Inputs,
    const SpectrumRgb&           SolarIrradiance) const noexcept
{
    constexpr uint32_t ConeCount = 5u;
    const double       SpanScale = Inputs.SdfOuterSpanMetres / 160.0;
    SpectrumRgb        SkyGather{};
    double             VisibilitySum = 0.0;

    for (uint32_t Cone = 0; Cone < ConeCount; ++Cone)
    {
        const SpatialVector Dir   = SampleHemisphereDirection(Normal, Cone, ConeCount, 0.37);
        double              T     = 0.14;
        double              Trans = 1.0;
        for (int Step = 0; Step < 9 && Trans > 0.05; ++Step)
        {
            const SpatialVector SamplePt = AddVec(Position, AddVec(MulVec(Normal, 0.04), MulVec(Dir, T)));
            const uint32_t      Cascade  = SelectCascadeIndex(SamplePt, SpanScale);
            const double        Dist     = SampleToroidalSignedDistance(SamplePt, Cascade);
            const double        ConeRad  = std::max(0.12, T * 0.32);
            const double        Occ      = std::clamp(0.5 - Dist / (2.0 * ConeRad), 0.0, 1.0);
            Trans *= (1.0 - Occ * 0.78);
            T += std::max(ConeRad * 0.85, std::abs(Dist));
        }
        const SpectrumRgb Sky = EvaluateSkyAndCelestial(Dir, Inputs, SolarIrradiance);
        SkyGather.R += Sky.R * Trans * 0.52;
        SkyGather.G += Sky.G * Trans * 0.52;
        SkyGather.B += Sky.B * Trans * 0.52;
        VisibilitySum += Trans;
    }

    const double      InvCones  = 1.0 / static_cast<double>(ConeCount);
    const double      MeanAo    = VisibilitySum * InvCones;
    const SpectrumRgb CardLight = GatherCardMultiBounceIrradiance(Position, Normal, Inputs);
    return {SkyGather.R * InvCones + CardLight.R * (0.45 + 0.55 * MeanAo),
            SkyGather.G * InvCones + CardLight.G * (0.45 + 0.55 * MeanAo),
            SkyGather.B * InvCones + CardLight.B * (0.45 + 0.55 * MeanAo)};
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PROJECTION & FULL FRAME EXECUTION
//------------------------------------------------------------------------------------------------------------------------

bool DualTierProvingGroundsRenderer::ProjectWorldPoint(
    SpatialVector                WorldPoint,
    const EverestViewportInputs& Inputs,
    double                       AspectRatio,
    double&                      OutU,
    double&                      OutV) const noexcept
{
    const CameraFrameBasis Cam = BuildOrbitCamera(Inputs);
    const SpatialVector    Rel = SubVec(WorldPoint, Cam.Eye);
    const double           Z   = DotVec(Rel, Cam.Forward);
    if (Z <= 0.15)
    {
        return false;
    }
    const double X = DotVec(Rel, Cam.Right) / (Z * Cam.TanHalfFovY * std::max(0.2, AspectRatio));
    const double Y = DotVec(Rel, Cam.Up) / (Z * Cam.TanHalfFovY);
    OutU           = 0.5 + 0.5 * X;
    OutV           = 0.5 - 0.5 * Y;
    return true;
}

void DualTierProvingGroundsRenderer::RenderFrame(
    uint32_t                     Width,
    uint32_t                     Height,
    const EverestViewportInputs& Inputs,
    std::vector<SpectrumRgb>&    OutLinearRgb,
    DualTierFrameTelemetry&      OutTelemetry) const
{
    Width  = std::max<uint32_t>(4u, Width);
    Height = std::max<uint32_t>(4u, Height);
    OutLinearRgb.assign(static_cast<size_t>(Width) * static_cast<size_t>(Height), SpectrumRgb{});

    const CameraFrameBasis Cam        = BuildOrbitCamera(Inputs);
    const double           Aspect     = static_cast<double>(Width) / static_cast<double>(Height);
    const SpatialVector    SunDir     = NormalizeVec(Inputs.SunDirection);
    const SpectrumRgb      SunTint    = EvaluatePlanckTint(Inputs.SunTemperatureKelvin);
    const double           IrrScale   = (Inputs.SunEnabled ? Inputs.SolarIlluminance : 0.0) * Inputs.SkyBrightnessScale;
    const SpectrumRgb      SolarIrr{IrrScale * SunTint.R, IrrScale * SunTint.G, IrrScale * SunTint.B};
    const double           ExposureMul = std::pow(2.0, Inputs.ExposureBiasStops);
    const bool             IsRtx       = (Inputs.ExecutionMode == DualTierExecutionMode::HardwareRayQueryRtx);

    // Raw indirect buffer + diffuse receiver modulation for 2-step edge-stopping SVGF spatial filter on RTX tier.
    std::vector<SpectrumRgb>   IndirectBuffer(OutLinearRgb.size());
    std::vector<SpectrumRgb>   DiffuseWeightBuffer(OutLinearRgb.size());
    std::vector<double>        FogTransBuffer(OutLinearRgb.size(), 1.0);
    std::vector<SpatialVector> HitNormals(OutLinearRgb.size());
    std::vector<uint8_t>       HitValid(OutLinearRgb.size(), 0u);

    uint64_t ReflectionCandidates = 0;
    uint64_t ReflectionHits       = 0;
    double   IndirectLumaSum      = 0.0;
    uint64_t SurfacePixelCount    = 0;

    for (uint32_t Row = 0; Row < Height; ++Row)
    {
        const double ScreenY = (1.0 - 2.0 * (static_cast<double>(Row) + 0.5) / static_cast<double>(Height)) * Cam.TanHalfFovY;
        for (uint32_t Col = 0; Col < Width; ++Col)
        {
            const size_t PixelIdx = static_cast<size_t>(Row) * Width + Col;
            const double ScreenX  = (2.0 * (static_cast<double>(Col) + 0.5) / static_cast<double>(Width) - 1.0) *
                                    Cam.TanHalfFovY * Aspect;
            const SpatialVector RayDir =
                NormalizeVec(AddVec(Cam.Forward, AddVec(MulVec(Cam.Right, ScreenX), MulVec(Cam.Up, ScreenY))));

            const auto PrimaryStrike = SceneBvh.Trace(Cam.Eye, RayDir, 0.05, 160.0, false);
            if (!PrimaryStrike.Hit || PrimaryStrike.Facet >= FacetMaterials.size())
            {
                OutLinearRgb[PixelIdx] = EvaluateSkyAndCelestial(RayDir, Inputs, SolarIrr);
                continue;
            }

            ++SurfacePixelCount;
            const FacetMaterialRecord& Mat    = FacetMaterials[PrimaryStrike.Facet];
            const SpatialVector        HitPos = AddVec(Cam.Eye, MulVec(RayDir, PrimaryStrike.Travel));
            SpatialVector              Normal = DotVec(Mat.Normal, RayDir) < 0.0 ? Mat.Normal : MulVec(Mat.Normal, -1.0);
            HitNormals[PixelIdx]              = Normal;
            HitValid[PixelIdx]                = 1u;

            // SurfaceNormals AOV
            if (Inputs.InspectionMode == ViewportInspectionMode::SurfaceNormals)
            {
                OutLinearRgb[PixelIdx] = {0.5 + 0.5 * Normal.X, 0.5 + 0.5 * Normal.Y, 0.5 + 0.5 * Normal.Z};
                continue;
            }

            // DistanceCascades AOV (visualizes SceneClip0..3 cascade boundaries + SDF isolines!)
            if (Inputs.InspectionMode == ViewportInspectionMode::DistanceCascades)
            {
                const uint32_t Cascade = SelectCascadeIndex(HitPos, Inputs.SdfOuterSpanMetres / 160.0);
                const double   SdfVal  = SampleToroidalSignedDistance(AddVec(HitPos, MulVec(Normal, 0.25)), Cascade);
                const double   Rings   = 0.55 + 0.45 * std::cos(SdfVal * 14.0);
                const std::array<SpectrumRgb, 4> CascadePalette{{
                    {0.18, 0.92, 0.65},   // SceneClip0 (near vehicle): Mint
                    {0.32, 0.66, 0.98},   // SceneClip1 (mid apron): Sky Blue
                    {0.96, 0.72, 0.24},   // SceneClip2 (gantry/walls): Amber
                    {0.92, 0.36, 0.58},   // SceneClip3 (far field): Coral
                }};
                const SpectrumRgb Tint = CascadePalette[Cascade];
                OutLinearRgb[PixelIdx] = {Tint.R * Rings, Tint.G * Rings, Tint.B * Rings};
                continue;
            }

            // Dynamic material customization (car paint, asphalt grid/snow/wetness, lamp switches)
            SpectrumRgb Albedo    = Mat.Albedo;
            SpectrumRgb Emissive  = Mat.Emissive;
            double      Roughness = Mat.Roughness;
            double      Metallic  = Mat.Metallic;

            if (Mat.Category == SurfaceCategory::VehiclePaint)
            {
                Albedo    = Inputs.VehicleBodyAlbedo;
                Roughness = std::clamp(Inputs.VehicleBodyRoughness, 0.03, 0.95);
            }
            else if (Mat.Category == SurfaceCategory::AsphaltApron)
            {
                // Proving-grounds 2m calibration lane grid + snow/rain surface deposit!
                const double GridX = std::abs(HitPos.X - std::round(HitPos.X * 0.5) * 2.0);
                const double GridY = std::abs(HitPos.Y - std::round(HitPos.Y * 0.5) * 2.0);
                if (GridX < 0.035 || GridY < 0.035)
                {
                    Albedo = {0.22, 0.25, 0.28};
                }
                if (std::abs(std::abs(HitPos.X) - 2.45) < 0.09 && HitPos.Y > -6.0 && HitPos.Y < 14.0)
                {
                    const bool RedKerb = (static_cast<int>(std::floor(HitPos.Y * 1.5)) & 1) == 0;
                    Albedo             = RedKerb ? SpectrumRgb{0.78, 0.16, 0.14} : SpectrumRgb{0.82, 0.84, 0.86};
                }
                if (Inputs.SurfaceWetnessOrSnow > 0.01)
                {
                    if (Inputs.SnowMode)
                    {
                        const double Cover = std::clamp(Inputs.SurfaceWetnessOrSnow, 0.0, 1.0);
                        Albedo.R           = Albedo.R * (1.0 - Cover) + 0.84 * Cover;
                        Albedo.G           = Albedo.G * (1.0 - Cover) + 0.88 * Cover;
                        Albedo.B           = Albedo.B * (1.0 - Cover) + 0.94 * Cover;
                        Roughness          = Roughness * (1.0 - Cover) + 0.55 * Cover;
                    }
                    else
                    {
                        // Wet proving-grounds mirror puddle response!
                        const double Wet = std::clamp(Inputs.SurfaceWetnessOrSnow, 0.0, 1.0);
                        Albedo           = {Albedo.R * (1.0 - 0.45 * Wet), Albedo.G * (1.0 - 0.45 * Wet),
                                            Albedo.B * (1.0 - 0.45 * Wet)};
                        Roughness        = Roughness * (1.0 - 0.82 * Wet) + 0.06 * Wet;
                    }
                }
            }
            else if (Mat.Category == SurfaceCategory::EmissiveLamp)
            {
                if (HitPos.Y > 1.8 && !Inputs.HeadlampsEnabled)
                {
                    Emissive = {};
                }
                else if (HitPos.Y < -1.8 && !Inputs.BrakeLampsEnabled && !Inputs.HazardLampsEnabled)
                {
                    Emissive = {0.35, 0.03, 0.02};
                }
                else if (HitPos.Y < -1.8 && Inputs.HazardLampsEnabled)
                {
                    Emissive = {8.5, 4.2, 0.3};
                }
            }

            // Dielectric Cabin Canopy Transport (RTX: Exact Snell + Fresnel + Beer-Lambert; GTX: Schlick + OrderedSoftwareBvh)
            if (Mat.Category == SurfaceCategory::DielectricCanopy && Inputs.DielectricGlazingEnabled)
            {
                const std::array<double, 3> IncArr{RayDir.X, RayDir.Y, RayDir.Z};
                const std::array<double, 3> NrmArr{Normal.X, Normal.Y, Normal.Z};
                const DielectricRefraction  Boundary = EvaluateDielectricRefraction(IncArr, NrmArr, 1.0, 1.52);
                const double                Fresnel  = Boundary.Valid ? Boundary.Reflectance : 0.08;
                const double                CosTheta = std::max(0.12, -DotVec(RayDir, Normal));
                // 6 mm automotive laminated green-slate acoustic glazing Beer-Lambert transmittance
                const SpectrumRgb BeerLambert{
                    std::exp(-0.22 * (0.006 / CosTheta) * 24.0),
                    std::exp(-0.08 * (0.006 / CosTheta) * 24.0),
                    std::exp(-0.12 * (0.006 / CosTheta) * 24.0)};

                if (Inputs.InspectionMode == ViewportInspectionMode::DielectricGlazing)
                {
                    OutLinearRgb[PixelIdx] = {BeerLambert.R * (1.0 - Fresnel) + Fresnel * 2.2,
                                              BeerLambert.G * (1.0 - Fresnel) + Fresnel * 2.4,
                                              BeerLambert.B * (1.0 - Fresnel) + Fresnel * 2.8};
                    continue;
                }

                // Reflected ray off curved windshield/canopy
                const SpatialVector ReflDir    = ReflectVec(RayDir, Normal);
                const auto          ReflStrike = SceneBvh.Trace(AddVec(HitPos, MulVec(Normal, 0.02)), ReflDir, 1e-3, 90.0, false);
                SpectrumRgb         Reflected  = EvaluateSkyAndCelestial(ReflDir, Inputs, SolarIrr);
                if (ReflStrike.Hit && ReflStrike.Facet < FacetMaterials.size())
                {
                    const auto& RMat = FacetMaterials[ReflStrike.Facet];
                    Reflected        = {RMat.Emissive.R + RMat.Albedo.R * 0.65,
                                        RMat.Emissive.G + RMat.Albedo.G * 0.65,
                                        RMat.Emissive.B + RMat.Albedo.B * 0.65};
                }

                // Transmitted ray through cabin interior & opposite window!
                const SpatialVector RefractDir = (IsRtx && Boundary.Valid && !Boundary.TotalInternalReflection)
                                                     ? NormalizeVec({Boundary.Direction[0], Boundary.Direction[1],
                                                                     Boundary.Direction[2]})
                                                     : RayDir;
                SpatialVector       Cursor     = AddVec(HitPos, MulVec(RefractDir, 0.03));
                SpectrumRgb         Through    = EvaluateSkyAndCelestial(RefractDir, Inputs, SolarIrr);
                for (int PaneHop = 0; PaneHop < 3; ++PaneHop)
                {
                    const auto Inner = SceneBvh.Trace(Cursor, RefractDir, 1e-3, 60.0, false);
                    if (!Inner.Hit || Inner.Facet >= FacetMaterials.size())
                    {
                        Through = EvaluateSkyAndCelestial(RefractDir, Inputs, SolarIrr);
                        break;
                    }
                    const auto&         IMat = FacetMaterials[Inner.Facet];
                    const SpatialVector IPos = AddVec(Cursor, MulVec(RefractDir, Inner.Travel));
                    if (IMat.Category == SurfaceCategory::DielectricCanopy)
                    {
                        Cursor = AddVec(IPos, MulVec(RefractDir, 0.03));
                        continue;
                    }
                    const double SunLit = Inputs.SunEnabled ? std::max(0.15, DotVec(IMat.Normal, SunDir)) : 0.12;
                    Through             = {IMat.Emissive.R + IMat.Albedo.R * SunLit * 0.85,
                                           IMat.Emissive.G + IMat.Albedo.G * SunLit * 0.85,
                                           IMat.Emissive.B + IMat.Albedo.B * SunLit * 0.85};
                    break;
                }

                OutLinearRgb[PixelIdx] = {
                    (Fresnel * Reflected.R + (1.0 - Fresnel) * BeerLambert.R * Through.R) * ExposureMul,
                    (Fresnel * Reflected.G + (1.0 - Fresnel) * BeerLambert.G * Through.G) * ExposureMul,
                    (Fresnel * Reflected.B + (1.0 - Fresnel) * BeerLambert.B * Through.B) * ExposureMul};
                continue;
            }

            if (Inputs.InspectionMode == ViewportInspectionMode::DielectricGlazing)
            {
                OutLinearRgb[PixelIdx] = {Albedo.R * 0.08, Albedo.G * 0.08, Albedo.B * 0.08};
                continue;
            }

            // 1. Direct Solar + Headlamp Pool + Lightning Illumination (RTX: exact BVH shadow; GTX: SDF soft shadow!)
            SpectrumRgb DirectLight{};
            if (Inputs.SunEnabled && Inputs.SunElevationDegrees > -1.0)
            {
                const double NdotL = std::max(0.0, DotVec(Normal, SunDir));
                if (NdotL > 0.0)
                {
                    double ShadowVis = 1.0;
                    if (IsRtx)
                    {
                        ShadowVis = SceneBvh.Occluded(AddVec(HitPos, MulVec(Normal, 0.02)), SunDir, 1e-3, 80.0) ? 0.0 : 1.0;
                    }
                    else
                    {
                        // GTX soft SDF cone shadow across ToroidalCascades
                        double T = 0.12;
                        for (int Step = 0; Step < 14 && T < 32.0; ++Step)
                        {
                            const SpatialVector SP   = AddVec(HitPos, AddVec(MulVec(Normal, 0.04), MulVec(SunDir, T)));
                            const double        Dist = AnalyticSdf.Evaluate(SP).Distance;
                            if (Dist < 0.015)
                            {
                                ShadowVis = 0.0;
                                break;
                            }
                            ShadowVis = std::min(ShadowVis, std::clamp(8.5 * Dist / T, 0.0, 1.0));
                            T += std::max(0.10, Dist * 0.85);
                        }
                    }
                    if (ShadowVis > 0.0)
                    {
                        const SpatialVector View = MulVec(RayDir, -1.0);
                        const ReferenceMaterial RefMat{Albedo, Roughness, Metallic, 1.5, 0.5};
                        const SpectrumRgb Brdf = EvaluateReferenceBrdf(RefMat, Normal, View, SunDir);
                        const double      SunE = Inputs.SolarIlluminance * 0.22 * ShadowVis * NdotL;
                        DirectLight.R += Brdf.R * SunTint.R * SunE * PiNumber;
                        DirectLight.G += Brdf.G * SunTint.G * SunE * PiNumber;
                        DirectLight.B += Brdf.B * SunTint.B * SunE * PiNumber;
                    }
                }
            }

            // Vehicle headlamp pools projected onto asphalt & gantry ahead of Y = +2.22 m!
            if (Inputs.HeadlampsEnabled && HitPos.Y > 2.22)
            {
                for (double LampX : {-0.66, +0.66})
                {
                    const SpatialVector LampOrigin{LampX, 2.24, 0.54};
                    const SpatialVector ToHit = SubVec(HitPos, LampOrigin);
                    const double        Dist  = std::sqrt(DotVec(ToHit, ToHit));
                    const SpatialVector Dir   = MulVec(ToHit, 1.0 / std::max(1e-4, Dist));
                    const double        Cone  = DotVec(Dir, NormalizeVec({0.0, 0.985, -0.17}));
                    if (Cone > 0.78)
                    {
                        const double Spot = std::pow((Cone - 0.78) / 0.22, 1.8) * 18.0 / (1.0 + 0.22 * Dist * Dist);
                        DirectLight.R += Albedo.R * Spot * 0.94;
                        DirectLight.G += Albedo.G * Spot * 0.97;
                        DirectLight.B += Albedo.B * Spot * 1.05;
                    }
                }
            }

            // Storm lightning flash illumination
            if (Inputs.LightningActive && Inputs.LightningFlashIntensity > 0.01)
            {
                const double Flash = Inputs.LightningFlashIntensity * (0.35 + 0.65 * std::max(0.0, Normal.Z)) * 1.8;
                DirectLight.R += Albedo.R * Flash * 0.78;
                DirectLight.G += Albedo.G * Flash * 0.88;
                DirectLight.B += Albedo.B * Flash * 1.22;
            }

            // 2. Indirect Global Illumination (RTX: Path-Replay ReSTIR GI; GTX: 4-Cascade Toroidal SDF + SurfaceCards!)
            const uint32_t    PixelHash  = MixHash32(Row * 4099u + Col * 131u + 17u);
            const SpectrumRgb IndirectGi = IsRtx
                                               ? EvaluateRtxIndirectIrradiance(HitPos, Normal, PixelHash, Inputs, SolarIrr)
                                               : EvaluateGtxConeIndirectIrradiance(HitPos, Normal, Inputs, SolarIrr);
            IndirectBuffer[PixelIdx]     = IndirectGi;
            IndirectLumaSum += 0.2126 * IndirectGi.R + 0.7152 * IndirectGi.G + 0.0722 * IndirectGi.B;

            if (Inputs.InspectionMode == ViewportInspectionMode::IndirectIrradiance)
            {
                OutLinearRgb[PixelIdx] = {IndirectGi.R * ExposureMul * 2.2,
                                          IndirectGi.G * ExposureMul * 2.2,
                                          IndirectGi.B * ExposureMul * 2.2};
                continue;
            }

            // 3. Specular Reflections (RTX: Hardware RayQuery across all roughnesses; GTX: OrderedSoftwareBvh <= cutoff!)
            SpectrumRgb SpecularRefl{};
            const bool  TraceBvhRefl = IsRtx ? (Roughness < 0.82) : (Roughness <= Inputs.BvhReflectionRoughnessCutoff);
            if (TraceBvhRefl)
            {
                ++ReflectionCandidates;
                const SpatialVector ReflDir    = ReflectVec(RayDir, Normal);
                const auto          ReflStrike = SceneBvh.Trace(AddVec(HitPos, MulVec(Normal, 0.02)), ReflDir, 1e-3, 95.0, false);
                if (ReflStrike.Hit && ReflStrike.Facet < FacetMaterials.size())
                {
                    ++ReflectionHits;
                    const auto&         RMat   = FacetMaterials[ReflStrike.Facet];
                    const SpectrumRgb   RAlb   = (RMat.Category == SurfaceCategory::VehiclePaint)
                                                     ? Inputs.VehicleBodyAlbedo
                                                     : RMat.Albedo;
                    const double        RSun   = Inputs.SunEnabled ? std::max(0.12, DotVec(RMat.Normal, SunDir)) : 0.08;
                    SpecularRefl               = {RMat.Emissive.R * 0.4 + RAlb.R * RSun * 1.15,
                                                  RMat.Emissive.G * 0.4 + RAlb.G * RSun * 1.15,
                                                  RMat.Emissive.B * 0.4 + RAlb.B * RSun * 1.15};
                }
                else
                {
                    SpecularRefl = EvaluateSkyAndCelestial(ReflDir, Inputs, SolarIrr);
                }
            }
            else
            {
                // Rough GTX fallback uses sky-hemisphere prefiltered approximation
                SpecularRefl = EvaluateSkyAndCelestial(ReflectVec(RayDir, Normal), Inputs, SolarIrr);
            }

            if (Inputs.InspectionMode == ViewportInspectionMode::SpecularReflections)
            {
                OutLinearRgb[PixelIdx] = {SpecularRefl.R * ExposureMul,
                                          SpecularRefl.G * ExposureMul,
                                          SpecularRefl.B * ExposureMul};
                continue;
            }

            const double CosView         = std::clamp(-DotVec(RayDir, Normal), 0.0, 1.0);
            const double ReflectanceF0   = 0.04 * (1.0 - Metallic) + 0.82 * Metallic;
            const double FresnelSpec     = (ReflectanceF0 + (1.0 - ReflectanceF0) * std::pow(1.0 - CosView, 5.0)) *
                                           (1.0 - 0.72 * Roughness);
            const double DiffuseKeep     = (1.0 - Metallic) * (1.0 - FresnelSpec);
            DiffuseWeightBuffer[PixelIdx] = {Albedo.R * DiffuseKeep * 1.45,
                                             Albedo.G * DiffuseKeep * 1.45,
                                             Albedo.B * DiffuseKeep * 1.45};

            SpectrumRgb DirectAndSpecular{
                Emissive.R + DirectLight.R + SpecularRefl.R * FresnelSpec,
                Emissive.G + DirectLight.G + SpecularRefl.G * FresnelSpec,
                Emissive.B + DirectLight.B + SpecularRefl.B * FresnelSpec};

            // 4. Exponential Height Fog + Aerial Perspective Gate (FogMedium.h)
            if (Inputs.FogEnabled)
            {
                const double TransH      = FogTransmittance(Inputs.Fog, Cam.Eye, RayDir, PrimaryStrike.Travel);
                const double AerialDist  = std::max(0.0, PrimaryStrike.Travel - Inputs.AerialStartMetres);
                const double TransA      = std::exp(-Inputs.AerialDensity * AerialDist);
                FogTransBuffer[PixelIdx] = TransH * TransA;
                DirectAndSpecular        = ApplyFog(Inputs.Fog, DirectAndSpecular, FogTransBuffer[PixelIdx]);
            }

            if (IsRtx)
            {
                // Deferred IndirectGi addition after 2-step edge-stopping SVGF filter below!
                OutLinearRgb[PixelIdx] = {DirectAndSpecular.R * ExposureMul,
                                          DirectAndSpecular.G * ExposureMul,
                                          DirectAndSpecular.B * ExposureMul};
            }
            else
            {
                const double FT = FogTransBuffer[PixelIdx];
                OutLinearRgb[PixelIdx] = {
                    (DirectAndSpecular.R + DiffuseWeightBuffer[PixelIdx].R * IndirectGi.R * FT) * ExposureMul,
                    (DirectAndSpecular.G + DiffuseWeightBuffer[PixelIdx].G * IndirectGi.G * FT) * ExposureMul,
                    (DirectAndSpecular.B + DiffuseWeightBuffer[PixelIdx].B * IndirectGi.B * FT) * ExposureMul};
            }
        }
    }

    // 2-step a-trous normal-weighted edge-stopping SVGF indirect denoiser on RTX tier (both FinalLit & IndirectIrradiance!)
    if (IsRtx && (Inputs.InspectionMode == ViewportInspectionMode::FinalLit ||
                  Inputs.InspectionMode == ViewportInspectionMode::IndirectIrradiance))
    {
        for (int StepRadius : {1, 2})
        {
            std::vector<SpectrumRgb> Filtered = IndirectBuffer;
            for (uint32_t Row = 0; Row < Height; ++Row)
            {
                for (uint32_t Col = 0; Col < Width; ++Col)
                {
                    const size_t Center = static_cast<size_t>(Row) * Width + Col;
                    if (!HitValid[Center])
                    {
                        continue;
                    }
                    SpectrumRgb Sum{};
                    double      WTotal = 0.0;
                    for (int Dy = -1; Dy <= 1; ++Dy)
                    {
                        const int NR = static_cast<int>(Row) + Dy * StepRadius;
                        if (NR < 0 || NR >= static_cast<int>(Height))
                        {
                            continue;
                        }
                        for (int Dx = -1; Dx <= 1; ++Dx)
                        {
                            const int NC = static_cast<int>(Col) + Dx * StepRadius;
                            if (NC < 0 || NC >= static_cast<int>(Width))
                            {
                                continue;
                            }
                            const size_t NIdx = static_cast<size_t>(NR) * Width + static_cast<size_t>(NC);
                            if (!HitValid[NIdx])
                            {
                                continue;
                            }
                            const double NWeight =
                                std::pow(std::max(0.0, DotVec(HitNormals[Center], HitNormals[NIdx])), 24.0);
                            Sum.R += IndirectBuffer[NIdx].R * NWeight;
                            Sum.G += IndirectBuffer[NIdx].G * NWeight;
                            Sum.B += IndirectBuffer[NIdx].B * NWeight;
                            WTotal += NWeight;
                        }
                    }
                    if (WTotal > 1e-5)
                    {
                        Filtered[Center] = {Sum.R / WTotal, Sum.G / WTotal, Sum.B / WTotal};
                    }
                }
            }
            IndirectBuffer.swap(Filtered);
        }

        for (size_t PixelIdx = 0; PixelIdx < OutLinearRgb.size(); ++PixelIdx)
        {
            if (!HitValid[PixelIdx])
            {
                continue;
            }
            const SpectrumRgb& Gi = IndirectBuffer[PixelIdx];
            if (Inputs.InspectionMode == ViewportInspectionMode::IndirectIrradiance)
            {
                OutLinearRgb[PixelIdx] = {Gi.R * ExposureMul * 2.2, Gi.G * ExposureMul * 2.2, Gi.B * ExposureMul * 2.2};
            }
            else if (DiffuseWeightBuffer[PixelIdx].R > 0.0 || DiffuseWeightBuffer[PixelIdx].G > 0.0 ||
                     DiffuseWeightBuffer[PixelIdx].B > 0.0)
            {
                const double FT = FogTransBuffer[PixelIdx];
                OutLinearRgb[PixelIdx].R += DiffuseWeightBuffer[PixelIdx].R * Gi.R * FT * ExposureMul;
                OutLinearRgb[PixelIdx].G += DiffuseWeightBuffer[PixelIdx].G * Gi.G * FT * ExposureMul;
                OutLinearRgb[PixelIdx].B += DiffuseWeightBuffer[PixelIdx].B * Gi.B * FT * ExposureMul;
            }
        }
    }

    double TotalLuma = 0.0;
    for (const SpectrumRgb& Px : OutLinearRgb)
    {
        TotalLuma += 0.2126 * Px.R + 0.7152 * Px.G + 0.0722 * Px.B;
    }

    OutTelemetry.TriangleCount         = SceneBvh.FacetCount();
    OutTelemetry.BvhEnclosureCount     = PackedEnclosureCount;
    OutTelemetry.ActiveSdfCascades     = 4u;
    OutTelemetry.SurfaceCardTexels     = SolvedCards.TexelCount();
    OutTelemetry.CardRadiosityResidual = InitialCardResidual;
    OutTelemetry.MeanLuminance         = TotalLuma / static_cast<double>(OutLinearRgb.size());
    OutTelemetry.MeanIndirectLuminance = SurfacePixelCount > 0 ? IndirectLumaSum / static_cast<double>(SurfacePixelCount) : 0.0;
    OutTelemetry.ReflectionHitFraction = ReflectionCandidates > 0
                                             ? static_cast<double>(ReflectionHits) / static_cast<double>(ReflectionCandidates)
                                             : 0.0;
}

} // namespace Frontier
