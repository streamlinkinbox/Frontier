/*============================================================================================================================================
                                                    PARTICLEFOUNDATIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase 1B validation & visual contact sheets for Engine/ParticleSpace (16B structs, 15 scalar drivers, 17 surfaces, 10 fireworks)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"
#include "Engine/ParticleSpace/Origins/FireworkPatternSampler.h"
#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"
#include "Engine/ParticleSpace/Origins/StructuralOrigins.h"
#include "Engine/ParticleSpace/Origins/SurfaceAnchorSampler.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleExtension.h"
#include "Engine/ParticleSpace/ParticleRig.h"
#include "Engine/ParticleSpace/ParticleTintDriver.h"
#include "Engine/ParticleSpace/VisualCarriers/VisualCarrierTypes.h"
#include "Engine/Workspaces/WorkspaceExchange.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

using namespace Frontier::ParticleSpace;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ParticleFoundationValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

bool NearEqual(float A, float B, float Tolerance = 1.0e-3f) noexcept
{
    return std::fabs(A - B) <= Tolerance;
}

RgbaColor8 LinearToRgba8(const Frontier::ColourSpace::LinearRgba& C) noexcept
{
    auto Encode = [](float V) noexcept -> uint8_t {
        const float Clamped = std::clamp(V, 0.0f, 1.0f);
        const float Srgb    = (Clamped <= 0.0031308f) ? (12.92f * Clamped) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
    };
    return RgbaColor8{ Encode(C.Red), Encode(C.Green), Encode(C.Blue), 255u };
}

class PixelPlotter
{
public:
    PixelPlotter(uint32_t W, uint32_t H, RgbaColor8 ClearCol)
        : WidthPx(W)
        , HeightPx(H)
        , Pixels(static_cast<std::size_t>(W) * H * 4u, 0u)
    {
        for (std::size_t I = 0u; I + 3u < Pixels.size(); I += 4u)
        {
            Pixels[I + 0u] = ClearCol.Red;
            Pixels[I + 1u] = ClearCol.Green;
            Pixels[I + 2u] = ClearCol.Blue;
            Pixels[I + 3u] = ClearCol.Alpha;
        }
    }

    void BlendPixel(uint32_t X, uint32_t Y, RgbaColor8 C) noexcept
    {
        if (X >= WidthPx || Y >= HeightPx)
        {
            return;
        }
        const std::size_t Off = (static_cast<std::size_t>(Y) * WidthPx + X) * 4u;
        const float       A   = static_cast<float>(C.Alpha) / 255.0f;
        const float       Inv = 1.0f - A;
        Pixels[Off + 0u]      = static_cast<uint8_t>(std::clamp(static_cast<float>(Pixels[Off + 0u]) * Inv + static_cast<float>(C.Red) * A, 0.0f, 255.0f));
        Pixels[Off + 1u]      = static_cast<uint8_t>(std::clamp(static_cast<float>(Pixels[Off + 1u]) * Inv + static_cast<float>(C.Green) * A, 0.0f, 255.0f));
        Pixels[Off + 2u]      = static_cast<uint8_t>(std::clamp(static_cast<float>(Pixels[Off + 2u]) * Inv + static_cast<float>(C.Blue) * A, 0.0f, 255.0f));
        Pixels[Off + 3u]      = 255u;
    }

    void DrawDisc(float Cx, float Cy, float Radius, RgbaColor8 C) noexcept
    {
        const int MinX = std::max(0, static_cast<int>(std::floor(Cx - Radius)));
        const int MaxX = std::min(static_cast<int>(WidthPx) - 1, static_cast<int>(std::ceil(Cx + Radius)));
        const int MinY = std::max(0, static_cast<int>(std::floor(Cy - Radius)));
        const int MaxY = std::min(static_cast<int>(HeightPx) - 1, static_cast<int>(std::ceil(Cy + Radius)));
        const float R2 = Radius * Radius;
        for (int Y = MinY; Y <= MaxY; ++Y)
        {
            for (int X = MinX; X <= MaxX; ++X)
            {
                const float Dx = static_cast<float>(X) + 0.5f - Cx;
                const float Dy = static_cast<float>(Y) + 0.5f - Cy;
                if (Dx * Dx + Dy * Dy <= R2)
                {
                    BlendPixel(static_cast<uint32_t>(X), static_cast<uint32_t>(Y), C);
                }
            }
        }
    }

    void DrawBoxOutline(uint32_t X0, uint32_t Y0, uint32_t W, uint32_t H, RgbaColor8 C) noexcept
    {
        for (uint32_t X = X0; X < X0 + W; ++X)
        {
            BlendPixel(X, Y0, C);
            BlendPixel(X, Y0 + H - 1u, C);
        }
        for (uint32_t Y = Y0; Y < Y0 + H; ++Y)
        {
            BlendPixel(X0, Y, C);
            BlendPixel(X0 + W - 1u, Y, C);
        }
    }

    bool WritePng(const std::filesystem::path& OutPath) const
    {
        SoftwareRasterCanvas Canvas(WidthPx, HeightPx, RgbaColor8{ 0u, 0u, 0u, 255u });
        Canvas.BlitRgbaRegion(0u, 0u, WidthPx, HeightPx, Pixels.data());
        std::filesystem::create_directories(OutPath.parent_path());
        return Canvas.WritePngFile(OutPath);
    }

private:
    uint32_t             WidthPx;
    uint32_t             HeightPx;
    std::vector<uint8_t> Pixels;
};

void RenderSurfaceContactSheet(const std::filesystem::path& OutPath)
{
    constexpr uint32_t Cols  = 6u;
    constexpr uint32_t Rows  = 3u;
    constexpr uint32_t CellW = 240u;
    constexpr uint32_t CellH = 220u;
    PixelPlotter       Plot(Cols * CellW, Rows * CellH, RgbaColor8{ 14u, 17u, 24u, 255u });

    Frontier::ColourSpace::ColourPresetRepository Presets;
    const auto* AuroraPreset = Presets.FindRampPreset("aurora");
    Frontier::ColourSpace::ColourSourceProfile AuroraProfile =
        AuroraPreset ? Frontier::ColourSpace::ConstructProfileFromRampPreset(*AuroraPreset)
                     : Frontier::ColourSpace::ColourSourceProfile{};

    for (uint32_t Idx = 0u; Idx < 17u; ++Idx)
    {
        const uint32_t Col = Idx % Cols;
        const uint32_t Row = Idx / Cols;
        const float    Ox  = static_cast<float>(Col * CellW);
        const float    Oy  = static_cast<float>(Row * CellH);

        Plot.DrawBoxOutline(Col * CellW + 4u, Row * CellH + 4u, CellW - 8u, CellH - 8u, RgbaColor8{ 42u, 52u, 74u, 255u });

        SurfaceAnchorOriginSpec Spec{};
        Spec.SurfacePreset = static_cast<SurfacePresetCategory>(Idx);
        Spec.SampleMode    = SurfaceSampleMode::EvenSurface;

        SurfaceAnchorSet Set;
        if (Idx < 16u)
        {
            Set = SampleSurfaceAnchors(Spec, 900u, nullptr, Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);
        }
        else
        {
            Spec.EulerRotationDeg = { 18.0f, 0.0f, 25.0f };
            const auto SampleAsset = Frontier::GeometrySpace::ConstructSamplePolygonNetwork(Frontier::GeometrySpace::SampleAssetPreset::TacticalCrate);
            Set = SampleSurfaceAnchors(Spec, 900u, &SampleAsset.Network, Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);
        }

        const float Yaw   = 0.61f;
        const float Pitch = 0.42f;
        const float Cy = std::cos(Yaw), Sy = std::sin(Yaw);
        const float Cp = std::cos(Pitch), Sp = std::sin(Pitch);

        float MaxExtent = 1.0e-3f;
        for (const SurfaceAnchorPoint& S : Set.Anchors)
        {
            MaxExtent = std::max(MaxExtent, std::max({ std::fabs(S.PositionMeters.X), std::fabs(S.PositionMeters.Y), std::fabs(S.PositionMeters.Z) }));
        }
        const float Scale = 82.0f / std::max(1.2f, MaxExtent);

        for (const SurfaceAnchorPoint& S : Set.Anchors)
        {
            const float Rx   = S.PositionMeters.X * Cy - S.PositionMeters.Y * Sy;
            const float Ry   = S.PositionMeters.X * Sy + S.PositionMeters.Y * Cy;
            const float Rz   = S.PositionMeters.Z;
            const float Py   = Rz * Cp + Ry * Sp;
            const float Sx   = Ox + 120.0f + Rx * Scale;
            const float SyPx = Oy + 114.0f - Py * Scale;
            const float T    = std::clamp(0.5f + 0.5f * S.NormalAxis.Z, 0.0f, 1.0f);
            Frontier::ColourSpace::SpatialColourSampleInput ColIn{};
            ColIn.NormalizedCoordinate = T;
            RgbaColor8 ColPx = LinearToRgba8(Frontier::ColourSpace::SampleColourSource(AuroraProfile, ColIn));
            ColPx.Alpha = 215u;
            Plot.DrawDisc(Sx, SyPx, 1.6f, ColPx);
        }
    }

    (void)Plot.WritePng(OutPath);
}

void RenderFireworkContactSheet(const std::filesystem::path& OutPath)
{
    constexpr uint32_t Cols  = 5u;
    constexpr uint32_t Rows  = 2u;
    constexpr uint32_t CellW = 280u;
    constexpr uint32_t CellH = 240u;
    PixelPlotter       Plot(Cols * CellW, Rows * CellH, RgbaColor8{ 10u, 11u, 18u, 255u });

    Frontier::ColourSpace::ColourPresetRepository Presets;
    const auto* RainbowPreset = Presets.FindRampPreset("rainbow");
    Frontier::ColourSpace::ColourSourceProfile RainbowProfile =
        RainbowPreset ? Frontier::ColourSpace::ConstructProfileFromRampPreset(*RainbowPreset)
                      : Frontier::ColourSpace::ColourSourceProfile{};

    for (uint32_t Idx = 0u; Idx < 10u; ++Idx)
    {
        const uint32_t Col = Idx % Cols;
        const uint32_t Row = Idx / Cols;
        const float    Ox  = static_cast<float>(Col * CellW);
        const float    Oy  = static_cast<float>(Row * CellH);

        Plot.DrawBoxOutline(Col * CellW + 4u, Row * CellH + 4u, CellW - 8u, CellH - 8u, RgbaColor8{ 42u, 48u, 68u, 255u });

        const FireworkShellSample Pattern = SampleFireworkShell(
            static_cast<FireworkShapeCategory>(Idx),
            "FRONTIER",
            520u,
            0,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);

        float MaxR = 0.5f;
        for (const FireworkStarPoint& P : Pattern.Points)
        {
            MaxR = std::max(MaxR, std::max({ std::fabs(P.OffsetX), std::fabs(P.OffsetY), std::fabs(P.OffsetZ) }));
        }
        const float Scale = 95.0f / MaxR;

        for (std::size_t K = 0u; K < Pattern.Points.size(); ++K)
        {
            const FireworkStarPoint& P = Pattern.Points[K];
            const float Sx = Ox + 140.0f + P.OffsetX * Scale;
            const float Sy = Oy + 120.0f - P.OffsetZ * Scale;
            const float T = static_cast<float>(K) / static_cast<float>(std::max<std::size_t>(1u, Pattern.Points.size() - 1u));
            RgbaColor8 ColPx{
                static_cast<uint8_t>(std::clamp(P.Red * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(P.Green * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(P.Blue * 255.0f, 0.0f, 255.0f)),
                230u,
            };
            if (Idx == 0u && RainbowPreset != nullptr)
            {
                Frontier::ColourSpace::SpatialColourSampleInput ColIn{};
                ColIn.NormalizedCoordinate = T;
                ColPx = LinearToRgba8(Frontier::ColourSpace::SampleColourSource(RainbowProfile, ColIn));
                ColPx.Alpha = 230u;
            }
            Plot.DrawDisc(Sx, Sy, 1.8f, ColPx);
        }
    }

    (void)Plot.WritePng(OutPath);
}

} // namespace

int main()
{
    bool AllPassed = true;

    // 1. Verify exact 16-byte aligned GPU struct sizes matching Slang/WGSL layouts
    AllPassed &= VerifyCheck(sizeof(ParticleSlot) == 80u, "ParticleSlot must be 80 bytes");
    AllPassed &= VerifyCheck(sizeof(StrandVertex) == 16u, "StrandVertex must be 16 bytes");
    AllPassed &= VerifyCheck(sizeof(GlobalParticleUniform) == 240u, "GlobalParticleUniform must be 240 bytes");
    AllPassed &= VerifyCheck(sizeof(RigParticleUniform) == 352u, "RigParticleUniform must be 352 bytes");
    AllPassed &= VerifyCheck(sizeof(ParticleTelemetryCounters) == 32u, "ParticleTelemetryCounters must be 32 bytes");

    // 2. Verify all 15 ParticleScalarChannel modes evaluate through ColourSpace::SampleColourSource
    Frontier::ColourSpace::ColourPresetRepository Presets;
    ParticleTintBinding Binding{};
    if (const auto* ThermalPreset = Presets.FindRampPreset("thermal"))
    {
        Binding.SourceProfile = Frontier::ColourSpace::ConstructProfileFromRampPreset(*ThermalPreset);
    }

    ParticleChannelSampleInput SampleIn{};
    SampleIn.ModeDefaultCoordinate = 0.5f;
    SampleIn.PositionX             = 1.2f;
    SampleIn.PositionY             = -0.8f;
    SampleIn.PositionZ             = 2.4f;
    SampleIn.OriginZ               = 1.0f;
    SampleIn.VelocityX             = 2.5f;
    SampleIn.VelocityY             = 0.4f;
    SampleIn.VelocityZ             = 1.6f;
    SampleIn.CameraY               = -6.0f;
    SampleIn.CameraZ               = 2.0f;
    SampleIn.AgeSeconds            = 0.7f;
    SampleIn.LifespanSeconds       = 2.0f;
    SampleIn.SizeMeters            = 0.08f;
    SampleIn.OrientationRadians    = 1.1f;
    SampleIn.RandomSeed            = 0.42f;
    SampleIn.ElapsedSeconds        = 0.35f;

    for (uint8_t Ch = 0u; Ch < static_cast<uint8_t>(ParticleScalarChannel::Count); ++Ch)
    {
        const auto Channel = static_cast<ParticleScalarChannel>(Ch);
        const float Scalar = ExtractParticleDriverScalar(Channel, SampleIn);
        AllPassed &= VerifyCheck(std::isfinite(Scalar), "Scalar channel returned non-finite value");
        const auto Rgba = EvaluateParticleTint(Binding.SourceProfile, Channel, SampleIn);
        AllPassed &= VerifyCheck(
            std::isfinite(Rgba.Red) && std::isfinite(Rgba.Green) && std::isfinite(Rgba.Blue) && std::isfinite(Rgba.Alpha),
            "Evaluated particle RGBA is finite");
    }
    const auto Lut64 = Frontier::ColourSpace::PackColourRampLut64(Binding.SourceProfile);
    AllPassed &= VerifyCheck(Lut64.Samples.size() == 64u, "Baked LUT must contain 64 samples");

    // 3. Verify analytic & structural origins + coordinate conversion
    const auto YUpPos = Frontier::Mathematics::Axis3D{ 1.0f, 2.5f, -3.0f };
    const auto ZUpPos = Frontier::Mathematics::ConvertVectorBetweenBases(
        YUpPos,
        Frontier::Mathematics::WorldCoordinateBasis::RightHandedYUp,
        Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);
    AllPassed &= VerifyCheck(NearEqual(ZUpPos.X, 1.0f) && NearEqual(ZUpPos.Y, 3.0f) && NearEqual(ZUpPos.Z, 2.5f), "HTML Y-Up -> RightHandZUp conversion mismatch");

    PointOriginSpec Pt{ { 0.0f, 0.0f, 1.5f } };
    const auto SamplePt = SampleAnalyticOrigin(Pt, 0u, 100u, 42u);
    AllPassed &= VerifyCheck(NearEqual(SamplePt.PositionMeters.Z, 1.5f), "PointOriginSpec Z mismatch");

    ClothGridOriginSpec ClothGrid{ { 0.0f, 0.0f, 2.4f }, 28u, 16u, 2.6f, 2.4f };
    const auto ClothAnchors = BuildClothGridAnchors(ClothGrid);
    AllPassed &= VerifyCheck(ClothAnchors.size() == 28u, "ClothGrid strand count mismatch");

    SpiderwebOriginSpec WebSpec{ { 0.0f, 0.0f, 1.6f }, 1u, 18u, 14u, 2.2f };
    std::vector<Frontier::Mathematics::Axis3D> WebPositions;
    const auto WebBonds = BuildSpiderwebAnchors(WebSpec, WebPositions);
    AllPassed &= VerifyCheck(!WebPositions.empty() && !WebBonds.empty(), "Spiderweb anchor builder returned empty");

    // 4. Verify all 17 surface presets (0..16) & PolygonNetwork sampling
    for (uint32_t PresetIdx = 0u; PresetIdx < 16u; ++PresetIdx)
    {
        SurfaceAnchorOriginSpec Spec{};
        Spec.SurfacePreset    = static_cast<SurfacePresetCategory>(PresetIdx);
        Spec.SampleMode       = SurfaceSampleMode::EvenSurface;
        Spec.EulerRotationDeg = { 10.0f, -5.0f, 15.0f };
        const SurfaceAnchorSet Set = SampleSurfaceAnchors(Spec, 512u, nullptr, Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);
        AllPassed &= VerifyCheck(Set.Anchors.size() == 512u && Set.PackedSnapSlots.size() == 512u, "Procedural surface anchor sample count mismatch");
    }
    {
        SurfaceAnchorOriginSpec CustomSpec{};
        CustomSpec.SurfacePreset = SurfacePresetCategory::CustomPolygonNetwork;
        CustomSpec.SampleMode    = SurfaceSampleMode::RidgeCreaseAccent;
        const auto SampleAsset = Frontier::GeometrySpace::ConstructSamplePolygonNetwork(Frontier::GeometrySpace::SampleAssetPreset::TacticalCrate);
        const SurfaceAnchorSet MeshSet = SampleSurfaceAnchors(CustomSpec, 512u, &SampleAsset.Network, Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);
        AllPassed &= VerifyCheck(MeshSet.Anchors.size() == 512u, "Custom PolygonNetwork anchor sample count mismatch");
    }

    // 5. Verify all 10 firework shapes (0..9) + SkyText UTF-8
    for (uint32_t ShapeIdx = 0u; ShapeIdx < 10u; ++ShapeIdx)
    {
        const FireworkShellSample Shell = SampleFireworkShell(
            static_cast<FireworkShapeCategory>(ShapeIdx),
            "FRONTIER",
            480u,
            0,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);
        AllPassed &= VerifyCheck(!Shell.Points.empty() && !Shell.PackedSnap.empty(), "Firework shape produced zero target offsets");
    }

    // 6. Verify ParticleRig, ParticleDocument & ParticleExtension lifecycle
    Frontier::Workspaces::WorkspaceExchange ExchangeBus;
    Frontier::ColourSpace::ColourExtension  ColourCoord;
    AllPassed &= VerifyCheck(ColourCoord.Initialize(&ExchangeBus, { 1u, 1u }), "ColourExtension initialize failed");

    ParticleExtension ParticleCoord;
    AllPassed &= VerifyCheck(ParticleCoord.Initialize(&ColourCoord, &ExchangeBus, { 2u, 1u }), "ParticleExtension initialize failed");
    AllPassed &= VerifyCheck(ParticleCoord.QueryActiveDocument().Rigs.size() == 1u, "Default Sparks rig missing");

    const ParticleRig& DefaultRig = ParticleCoord.QueryActiveDocument().Rigs.front();
    const RigParticleUniform PackedUniform = DefaultRig.PackGpuUniform(0u, DefaultRig.Sizing.CapacitySlots);
    AllPassed &= VerifyCheck(NearEqual(PackedUniform.OriginRadius[2], 1.5f) && PackedUniform.ReleaseBudget[0] > 100.0f, "RigParticleUniform packing mismatch");

    ParticleCoord.Tick(0.016);
    const ParticleExtensionMetrics Metrics = ParticleCoord.InspectMetrics();
    AllPassed &= VerifyCheck(Metrics.Initialized && Metrics.ActiveRigCount == 1u && Metrics.TickCounter == 1ull, "ParticleExtension metrics mismatch");
    ParticleCoord.Finalize();
    ColourCoord.Finalize();

    // 7. Emit Phase 1B visual contact sheets
    const std::filesystem::path RepoRoot = ResolveRepositoryRootPath();
    RenderSurfaceContactSheet(RepoRoot / "Exhibits/Images/Particles/01-SurfaceAnchorsContactSheet.png");
    RenderFireworkContactSheet(RepoRoot / "Exhibits/Images/Particles/02-FireworkPatternsContactSheet.png");

    if (!AllPassed)
    {
        return 1;
    }
    std::printf("[ParticleFoundationValidation] All Phase 1B checks passed.\n");
    return 0;
}
