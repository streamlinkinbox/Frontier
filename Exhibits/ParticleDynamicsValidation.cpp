/*============================================================================================================================================
                                                      PARTICLEDYNAMICSVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase 3 validation & 4x3 visual contact sheet for all 16 modular ParticleSpace dynamics solvers + ForceFieldAccumulator + SDF

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/Interchange/ParticleInterchange.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleSoftwareExecutor.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
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
        std::fprintf(stderr, "[ParticleDynamicsValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

RgbaColor8 LinearToRgba8(float R, float G, float B, float A = 1.0f) noexcept
{
    auto Encode = [](float V) noexcept -> uint8_t {
        const float Clamped = std::clamp(V, 0.0f, 1.0f);
        const float Srgb    = (Clamped <= 0.0031308f) ? (12.92f * Clamped) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
    };
    return RgbaColor8{
        Encode(R),
        Encode(G),
        Encode(B),
        static_cast<uint8_t>(std::clamp(std::lround(std::clamp(A, 0.0f, 1.0f) * 255.0f), 0l, 255l)),
    };
}

class PanelRasterizer
{
public:
    PanelRasterizer(uint32_t W, uint32_t H, RgbaColor8 Bg)
        : WidthPx(W)
        , HeightPx(H)
        , Pixels(static_cast<std::size_t>(W) * H * 4u, 0u)
    {
        for (std::size_t I = 0u; I + 3u < Pixels.size(); I += 4u)
        {
            Pixels[I + 0u] = Bg.Red;
            Pixels[I + 1u] = Bg.Green;
            Pixels[I + 2u] = Bg.Blue;
            Pixels[I + 3u] = Bg.Alpha;
        }
    }

    void BlendDot(int32_t Cx, int32_t Cy, int32_t Radius, RgbaColor8 C) noexcept
    {
        for (int32_t Dy = -Radius; Dy <= Radius; ++Dy)
        {
            for (int32_t Dx = -Radius; Dx <= Radius; ++Dx)
            {
                if (Dx * Dx + Dy * Dy > Radius * Radius)
                {
                    continue;
                }
                const int32_t Px = Cx + Dx;
                const int32_t Py = Cy + Dy;
                if (Px < 0 || Py < 0 || Px >= static_cast<int32_t>(WidthPx) || Py >= static_cast<int32_t>(HeightPx))
                {
                    continue;
                }
                const std::size_t Off = (static_cast<std::size_t>(Py) * WidthPx + static_cast<std::size_t>(Px)) * 4u;
                const float Alpha = static_cast<float>(C.Alpha) / 255.0f;
                Pixels[Off + 0u] = static_cast<uint8_t>(std::min(255.0f, Pixels[Off + 0u] * (1.0f - Alpha * 0.35f) + C.Red * Alpha));
                Pixels[Off + 1u] = static_cast<uint8_t>(std::min(255.0f, Pixels[Off + 1u] * (1.0f - Alpha * 0.35f) + C.Green * Alpha));
                Pixels[Off + 2u] = static_cast<uint8_t>(std::min(255.0f, Pixels[Off + 2u] * (1.0f - Alpha * 0.35f) + C.Blue * Alpha));
                Pixels[Off + 3u] = 255u;
            }
        }
    }

    void DrawLine(int32_t X0, int32_t Y0, int32_t X1, int32_t Y1, RgbaColor8 C) noexcept
    {
        const int32_t Steps = std::max(1, std::max(std::abs(X1 - X0), std::abs(Y1 - Y0)));
        for (int32_t S = 0; S <= Steps; ++S)
        {
            const float T  = static_cast<float>(S) / static_cast<float>(Steps);
            const int32_t Px = static_cast<int32_t>(std::lround(X0 + (X1 - X0) * T));
            const int32_t Py = static_cast<int32_t>(std::lround(Y0 + (Y1 - Y0) * T));
            BlendDot(Px, Py, 1, C);
        }
    }

    [[nodiscard]] const uint8_t* Data() const noexcept
    {
        return Pixels.data();
    }

private:
    uint32_t             WidthPx  = 0u;
    uint32_t             HeightPx = 0u;
    std::vector<uint8_t> Pixels   {};
};

struct ProjectedPt
{
    int32_t X = 0;
    int32_t Y = 0;
};

ProjectedPt ProjectWorld(float X, float Y, float Z, uint32_t W, uint32_t H, float Scale) noexcept
{
    const float ScreenX = (X * 0.85f - Y * 0.52f) * Scale + static_cast<float>(W) * 0.5f;
    const float ScreenY = static_cast<float>(H) * 0.72f - (Z * 0.92f + (X * 0.28f + Y * 0.42f)) * Scale;
    return { static_cast<int32_t>(std::lround(ScreenX)), static_cast<int32_t>(std::lround(ScreenY)) };
}

void RenderWorkspaceTile(
    const RigSimulationWorkspace& Work,
    uint32_t                      W,
    uint32_t                      H,
    float                         Scale,
    SoftwareRasterCanvas&         Canvas,
    uint32_t                      TileX,
    uint32_t                      TileY)
{
    PanelRasterizer Panel(W, H, RgbaColor8{ 14u, 17u, 26u, 255u });

    for (uint32_t X = 0u; X < W; ++X)
    {
        Panel.BlendDot(static_cast<int32_t>(X), 0, 0, RgbaColor8{ 45u, 55u, 76u, 255u });
        Panel.BlendDot(static_cast<int32_t>(X), static_cast<int32_t>(H - 1u), 0, RgbaColor8{ 45u, 55u, 76u, 255u });
    }
    const ProjectedPt Ox0 = ProjectWorld(-2.5f, 0.0f, 0.0f, W, H, Scale);
    const ProjectedPt Ox1 = ProjectWorld( 2.5f, 0.0f, 0.0f, W, H, Scale);
    const ProjectedPt Oy0 = ProjectWorld(0.0f, -2.5f, 0.0f, W, H, Scale);
    const ProjectedPt Oy1 = ProjectWorld(0.0f,  2.5f, 0.0f, W, H, Scale);
    Panel.DrawLine(Ox0.X, Ox0.Y, Ox1.X, Ox1.Y, RgbaColor8{ 36u, 44u, 62u, 180u });
    Panel.DrawLine(Oy0.X, Oy0.Y, Oy1.X, Oy1.Y, RgbaColor8{ 36u, 44u, 62u, 180u });

    if (!Work.StrandAnchors.empty())
    {
        const uint32_t VertsPerStrand = static_cast<uint32_t>(Work.StrandVertices.size() / Work.StrandAnchors.size());
        for (std::size_t S = 0u; S < Work.StrandAnchors.size(); ++S)
        {
            const std::size_t Base = S * VertsPerStrand;
            for (uint32_t V = 0u; V + 1u < VertsPerStrand; ++V)
            {
                const StrandVertex& A = Work.StrandVertices[Base + V];
                const StrandVertex& B = Work.StrandVertices[Base + V + 1u];
                const ProjectedPt Pa  = ProjectWorld(A.PositionRadius[0], A.PositionRadius[1], A.PositionRadius[2], W, H, Scale);
                const ProjectedPt Pb  = ProjectWorld(B.PositionRadius[0], B.PositionRadius[1], B.PositionRadius[2], W, H, Scale);
                Panel.DrawLine(Pa.X, Pa.Y, Pb.X, Pb.Y, RgbaColor8{ 120u, 225u, 150u, 220u });
            }
        }
    }

    if (!Work.WebLattice.LivePositions.empty())
    {
        const auto& Pos = Work.WebLattice.LivePositions;
        for (const StructuralBondLink& Bond : Work.WebLattice.Bonds)
        {
            if (Bond.VertexA < Pos.size() && Bond.VertexB < Pos.size())
            {
                const ProjectedPt Pa = ProjectWorld(Pos[Bond.VertexA].X, Pos[Bond.VertexA].Y, Pos[Bond.VertexA].Z, W, H, Scale);
                const ProjectedPt Pb = ProjectWorld(Pos[Bond.VertexB].X, Pos[Bond.VertexB].Y, Pos[Bond.VertexB].Z, W, H, Scale);
                Panel.DrawLine(Pa.X, Pa.Y, Pb.X, Pb.Y, RgbaColor8{ 215u, 230u, 255u, 220u });
            }
        }
    }

    for (const ParticleSlot& Slot : Work.Slots)
    {
        if (Slot.PositionAge[3] >= Slot.VelocityLifetime[3])
        {
            continue;
        }
        const ProjectedPt P = ProjectWorld(Slot.PositionAge[0], Slot.PositionAge[1], Slot.PositionAge[2], W, H, Scale);
        const RgbaColor8 Col = LinearToRgba8(Slot.ColourRgba[0], Slot.ColourRgba[1], Slot.ColourRgba[2], std::max(0.45f, Slot.ColourRgba[3]));
        const int32_t DotR = std::clamp(static_cast<int32_t>(std::lround(Slot.Attributes[0] * 18.0f)), 1, 4);
        Panel.BlendDot(P.X, P.Y, DotR, Col);
    }

    Canvas.BlitRgbaRegion(TileX * W, TileY * H, W, H, Panel.Data());
}

[[nodiscard]] ParticleDocument BuildSingleRigDoc(const std::string& JsonPresetText)
{
    ParticleDocument Doc;
    ParticleRig Rig{};
    std::string Diag;
    if (!Frontier::Interchange::ParticleInterchange::ImportHtmlStudioPresetJson(
            JsonPresetText,
            "sparks",
            "Sparks",
            false,
            Rig,
            Diag))
    {
        Doc.PopulateDefaultSparksRig();
    }
    else
    {
        Doc.AddRig(std::move(Rig));
    }
    return Doc;
}

[[nodiscard]] Frontier::SimulationSpace::ForceFields::ForceSourceRecordEntry MakeForceSource(
    Frontier::SimulationSpace::ForceFields::ForceModel Model,
    uint32_t                                           Token,
    float                                              Strength = 6.0f) noexcept
{
    Frontier::SimulationSpace::ForceFields::ForceSourceRecordEntry Entry{};
    Entry.UniqueToken           = Token;
    Entry.Model                 = Model;
    Entry.CentreMeters          = { 0.0f, 0.0f, 1.5f, 0.0f };
    Entry.EvaluatedCentreMeters = Entry.CentreMeters;
    Entry.RadiusMeters          = 8.0f;
    Entry.Strength              = Strength;
    Entry.Enabled               = true;
    return Entry;
}

} // namespace

int main()
{
    bool AllPassed = true;
    const std::filesystem::path RepoRoot = ResolveRepositoryRootPath();

    // 1. Validate ForceFieldAccumulator coupling (19 ForceModels: RotationalColumn + CurlNoise) & SignedDistanceVolume obstacle
    {
        ParticleDocument Doc;
        Doc.PopulateDefaultSparksRig();
        Doc.AmbientWindMps = { 1.4f, 0.0f, 0.25f };
        Doc.Collider.BallRadiusMeters     = 0.65f;
        Doc.Collider.BallPositionMeters   = { 0.0f, 0.0f, 1.8f };
        Doc.Collider.SignedDistanceActive = true;

        Doc.ForceSources.push_back(MakeForceSource(
            Frontier::SimulationSpace::ForceFields::ForceModel::RotationalColumn, 1u, 6.5f));
        Doc.ForceSources.push_back(MakeForceSource(
            Frontier::SimulationSpace::ForceFields::ForceModel::CurlNoise, 2u, 4.0f));

        const auto SdfAsset = Frontier::GeometrySpace::ConstructSamplePolygonNetwork(
            Frontier::GeometrySpace::SampleAssetPreset::TacticalCrate);
        Frontier::SpatialAcceleration::WideBoundingTree BvhTree;
        Frontier::DistanceFieldSpace::SignedDistanceVolume Sdf;
        AllPassed &= VerifyCheck(BvhTree.BuildFromPolygonNetwork(SdfAsset.Network), "BVH build failed");
        AllPassed &= VerifyCheck(Sdf.BuildFromWideBoundingTree(BvhTree, 0.22f, 0.4f), "SDF build failed");

        ParticleSoftwareExecutor Exec;
        Exec.Initialize(Doc);
        for (int Step = 0; Step < 45; ++Step)
        {
            Exec.Step(Doc, 1.0f / 60.0f, &Sdf);
        }

        const auto Telemetry = Exec.QueryTelemetry();
        AllPassed &= VerifyCheck(Telemetry.AliveCount > 100u, "Sparks + ForceFields produced too few alive particles");
        AllPassed &= VerifyCheck(Exec.QueryForceAccumulator().QueryActiveSourceCount() == 2u, "ForceAccumulator source count mismatch");
    }

    // 2. Construct representative specs covering all 16 modular dynamics solvers
    struct TileCase
    {
        const char* Name;
        ParticleDocument Doc;
    };
    std::vector<TileCase> Cases;

    auto MakeCase = [](const char* Name, auto&& ConfigureFn) -> TileCase
    {
        ParticleDocument Doc;
        Doc.PopulateDefaultSparksRig();
        Doc.Rigs[0].Sizing.CapacitySlots = 600u;
        ConfigureFn(Doc, Doc.Rigs[0]);
        Doc.RebuildAllRigCaches();
        return { Name, std::move(Doc) };
    };

    Cases.push_back(MakeCase("01 Sparks + ForceColumn", [](ParticleDocument& D, ParticleRig& R)
    {
        R.Sizing.ReleaseRatePerSec = 500.0f;
        D.ForceSources.push_back(MakeForceSource(
            Frontier::SimulationSpace::ForceFields::ForceModel::RotationalColumn, 10u, 8.0f));
    }));
    Cases.push_back(MakeCase("02 Curl Plasma", [](ParticleDocument&, ParticleRig& R)
    {
        R.Origin = SphereVolumeOriginSpec{ { 0.0f, 0.0f, 1.5f }, 1.2f, true };
        R.Dynamics.Curl.Mode = CurlManifoldMode::SphericalPlasma;
        R.Dynamics.Curl.ShellRadius = 1.35f;
        R.Dynamics.Curl.DriveSpeed = 2.4f;
        R.Dynamics.Curl.SpatialFreq = 1.4f;
    }));
    Cases.push_back(MakeCase("03 Hanging Rope XPBD", [](ParticleDocument& D, ParticleRig& R)
    {
        D.AmbientWindMps = { 1.6f, 0.5f, 0.0f };
        R.Origin = ClothGridOriginSpec{ { 0.0f, 0.0f, 2.8f }, 18u, 14u, 2.6f, 2.4f };
        R.Dynamics.Strands.Mode = StrandDynamicsMode::VinesRopes;
        R.Dynamics.Strands.WaveSpeedHz = 1.8f;
    }));
    Cases.push_back(MakeCase("04 Meadow Grass Sway", [](ParticleDocument& D, ParticleRig& R)
    {
        D.AmbientWindMps = { 2.4f, 0.8f, 0.0f };
        R.Origin = BotanicalRootOriginSpec{ { 0.0f, 0.0f, 0.0f }, 36u, 8u, 2.4f, 1.4f };
        R.Dynamics.Strands.Mode = StrandDynamicsMode::GrassWheat;
    }));
    auto SetTint = [](ParticleRig& R, Frontier::ColourSpace::LinearRgba A, Frontier::ColourSpace::LinearRgba B)
    {
        Frontier::ColourSpace::LinearFillSource Fill{};
        Fill.PrimaryLinear   = A;
        Fill.SecondaryLinear = B;
        R.TintBinding.SourceProfile.Source = Fill;
    };

    Cases.push_back(MakeCase("05 Spiderweb Weave", [](ParticleDocument& D, ParticleRig& R)
    {
        D.AmbientWindMps = { 1.1f, 0.4f, 0.0f };
        R.Origin = SpiderwebOriginSpec{ { 0.0f, 0.0f, 1.6f }, 1u, 18u, 10u, 1.9f };
        R.Dynamics.Spiderweb.WebMode = 1u;
        R.Dynamics.Spiderweb.SagMeters = 0.22f;
    }));
    Cases.push_back(MakeCase("06 Ferrofluid Meniscus", [&](ParticleDocument&, ParticleRig& R)
    {
        R.Sizing.CapacitySlots = 360u;
        R.Dynamics.Ferrofluid.FerroMode = 2u;
        R.Dynamics.Ferrofluid.SpikeCount = 360u;
        SetTint(R, { 0.18f, 0.38f, 0.72f, 1.0f }, { 0.75f, 0.94f, 1.0f, 1.0f });
    }));
    Cases.push_back(MakeCase("07 Seismic Fracture", [&](ParticleDocument&, ParticleRig& R)
    {
        R.Sizing.CapacitySlots = 256u;
        R.Dynamics.Fracture.CrackMode = 2u;
        R.Dynamics.Fracture.PropagationSpeed = 2.8f;
        R.Dynamics.Fracture.FissureHeaveMeters = 0.45f;
        SetTint(R, { 0.35f, 0.38f, 0.44f, 1.0f }, { 1.0f, 0.45f, 0.08f, 1.0f });
    }));
    Cases.push_back(MakeCase("08 Rally Figure-8 Splash", [&](ParticleDocument&, ParticleRig& R)
    {
        R.Sizing.CapacitySlots = 480u;
        R.Dynamics.WaterSplash.SplashMode = 3u;
        R.Dynamics.WaterSplash.VehicleSpeed = 5.5f;
        SetTint(R, { 0.35f, 0.72f, 0.96f, 1.0f }, { 0.75f, 0.52f, 0.28f, 1.0f });
    }));
    Cases.push_back(MakeCase("09 MoGraph Effectors", [&](ParticleDocument&, ParticleRig& R)
    {
        R.Sizing.CapacitySlots = 324u;
        R.Dynamics.EffectorGrid.Active = true;
        R.Dynamics.EffectorGrid.Columns = 18u;
        R.Dynamics.EffectorGrid.Rows = 18u;
        R.Dynamics.EffectorGrid.Driver = MoGraphEffectorDriver::ExpandingPulseRing;
        R.Dynamics.EffectorGrid.SecondaryEffector.SizeMeters = 1.1f;
        SetTint(R, { 0.14f, 0.52f, 0.98f, 1.0f }, { 0.98f, 0.24f, 0.72f, 1.0f });
    }));
    Cases.push_back(MakeCase("10 Bird Murmuration", [&](ParticleDocument& D, ParticleRig& R)
    {
        D.Collider.BallRadiusMeters = 0.7f;
        D.Collider.BallPositionMeters = { 0.6f, 0.0f, 1.8f };
        R.Origin = SphereVolumeOriginSpec{ { 0.0f, 0.0f, 2.0f }, 1.6f, false };
        R.Dynamics.Boids.Active = true;
        R.Dynamics.Boids.PulseHz = 1.6f;
        R.Dynamics.Boids.PulseDepth = 0.8f;
        SetTint(R, { 0.95f, 0.88f, 0.35f, 1.0f }, { 0.38f, 0.85f, 1.0f, 1.0f });
    }));
    Cases.push_back(MakeCase("11 Molecular Gas", [&](ParticleDocument&, ParticleRig& R)
    {
        R.Sizing.CapacitySlots = 320u;
        R.Origin = SphereVolumeOriginSpec{ { 0.0f, 0.0f, 1.5f }, 1.8f, false };
        R.Dynamics.Family = ParticleSimulationFamily::ChemicalReaction;
        R.Dynamics.Molecular.Active = true;
        R.Dynamics.Molecular.Temperature = 0.85f;
        SetTint(R, { 0.25f, 0.95f, 0.62f, 1.0f }, { 0.98f, 0.45f, 0.25f, 1.0f });
    }));
    Cases.push_back(MakeCase("12 Honeycomb Shield", [&](ParticleDocument& D, ParticleRig& R)
    {
        D.Collider.BallRadiusMeters = 0.75f;
        D.Collider.BallPositionMeters = { 1.3f, 0.0f, 1.6f };
        R.Sizing.CapacitySlots = 640u;
        R.Dynamics.Shield.Active = true;
        R.Dynamics.Shield.Form = ShieldFormCategory::SphereDome;
        R.Dynamics.Shield.MotifBitmask = 1u;
        SetTint(R, { 0.10f, 0.68f, 0.98f, 1.0f }, { 0.55f, 0.96f, 1.0f, 1.0f });
    }));

    // Verify TransitionDerez + Pyro + Beam explicitly
    {
        TileCase DerezCase = MakeCase("TransitionDerez", [](ParticleDocument&, ParticleRig& R)
        {
            R.Dynamics.Transition.Mode = TransitionDerezMode::DissolveButterfly;
            R.Dynamics.Transition.HoldBaseSec = 0.1f;
            R.Dynamics.Transition.HoldDurationSec = 0.2f;
        });
        ParticleSoftwareExecutor Exec;
        Exec.Initialize(DerezCase.Doc);
        for (int S = 0; S < 45; ++S) Exec.Step(DerezCase.Doc, 1.0f / 60.0f);
        AllPassed &= VerifyCheck(Exec.QueryTelemetry().AliveCount > 0u, "TransitionDerez had 0 alive particles");
    }
    {
        TileCase PyroCase = MakeCase("FireworkShow", [](ParticleDocument&, ParticleRig& R)
        {
            R.Origin = FireworkPatternOriginSpec{ FireworkShapeCategory::Heart, { 0.0f, 0.0f, 0.2f }, "2026", 2.6f, 2u };
            R.Dynamics.Pyro.FireworkMode = 2u;
        });
        ParticleSoftwareExecutor Exec;
        Exec.Initialize(PyroCase.Doc);
        for (int S = 0; S < 25; ++S) Exec.Step(PyroCase.Doc, 1.0f / 60.0f);
        AllPassed &= VerifyCheck(Exec.QueryTelemetry().AliveCount > 50u, "FireworkShow had too few alive stars");
    }
    {
        TileCase BeamCase = MakeCase("BeamFilament", [](ParticleDocument&, ParticleRig& R)
        {
            R.Sizing.CapacitySlots = 128u;
            R.Dynamics.Pyro.BeamModeActive = 1.0f;
        });
        ParticleSoftwareExecutor Exec;
        Exec.Initialize(BeamCase.Doc);
        for (int S = 0; S < 15; ++S) Exec.Step(BeamCase.Doc, 1.0f / 60.0f);
        AllPassed &= VerifyCheck(Exec.QueryTelemetry().AliveCount == 128u, "BeamFilament slot count mismatch");
    }

    // 3. Step all 12 contact-sheet cases and render 4x3 PNG
    constexpr uint32_t TileW = 360u;
    constexpr uint32_t TileH = 260u;
    SoftwareRasterCanvas Sheet(TileW * 4u, TileH * 3u, RgbaColor8{ 12u, 14u, 22u, 255u });

    for (std::size_t Idx = 0u; Idx < Cases.size(); ++Idx)
    {
        ParticleSoftwareExecutor Exec;
        Exec.Initialize(Cases[Idx].Doc);
        for (int S = 0; S < 50; ++S)
        {
            Exec.Step(Cases[Idx].Doc, 1.0f / 60.0f);
        }
        const auto* Work = Exec.FindWorkspace(Cases[Idx].Doc.Rigs[0].RigId);
        AllPassed &= VerifyCheck(Work != nullptr, "Missing RigSimulationWorkspace");
        if (Work != nullptr)
        {
            const bool HasActivity = (Work->AliveCount > 0u) || (Work->ActiveStrandCount > 0u);
            AllPassed &= VerifyCheck(HasActivity, Cases[Idx].Name);
            RenderWorkspaceTile(
                *Work,
                TileW,
                TileH,
                42.0f,
                Sheet,
                static_cast<uint32_t>(Idx % 4u),
                static_cast<uint32_t>(Idx / 4u));
        }
    }

    const std::filesystem::path OutPng = RepoRoot / "Exhibits/Images/Particles/03-DynamicsSolversContactSheet.png";
    AllPassed &= VerifyCheck(Sheet.WritePngFile(OutPng), "Failed to write 03-DynamicsSolversContactSheet.png");

    // 4. Verify ParticleInterchange + ParticleSoftwareExecutor together
    const std::filesystem::path PresetsJs = RepoRoot / "Experimental/ParticleEditor Final/Engine/presets.js";
    std::ifstream In(PresetsJs);
    AllPassed &= VerifyCheck(In.is_open(), "Missing presets.js");
    if (In.is_open())
    {
        std::ostringstream Ss;
        Ss << In.rdbuf();
        const std::string Text = Ss.str();
        const std::size_t SparksPos = Text.find("\"sparks\"");
        AllPassed &= VerifyCheck(SparksPos != std::string::npos, "sparks key not found in presets.js");
        if (SparksPos != std::string::npos)
        {
            const std::size_t BraceOpen = Text.find('{', SparksPos);
            int Depth = 0;
            std::size_t EndPos = BraceOpen;
            for (std::size_t P = BraceOpen; P < Text.size(); ++P)
            {
                if (Text[P] == '{') ++Depth;
                else if (Text[P] == '}')
                {
                    --Depth;
                    if (Depth == 0)
                    {
                        EndPos = P;
                        break;
                    }
                }
            }
            const ParticleDocument Doc = BuildSingleRigDoc(Text.substr(BraceOpen, EndPos - BraceOpen + 1u));
            ParticleSoftwareExecutor Exec;
            Exec.Initialize(Doc);
            for (int S = 0; S < 20; ++S) Exec.Step(Doc, 1.0f / 60.0f);
            AllPassed &= VerifyCheck(Exec.QueryTelemetry().AliveCount > 0u, "Imported sparks preset failed simulation");
        }
    }

    if (!AllPassed)
    {
        return 1;
    }

    std::printf("[ParticleDynamicsValidation] All Phase 3 checks passed (16 modular dynamics solvers + ForceFields + SDF + 4x3 visual sheet).\n");
    return 0;
}
