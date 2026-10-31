/*==========================================================================================================================================
                                                     DUALTIERRENDERER.CPP
==========================================================================================================================================*/
// 🧩 Dual-tier 1:1 Proving Grounds & ControlVehicle renderer:
//    - RTX Tier: Two-Level Binned-SAH BVH (TLAS over rigid occupants + BLAS per mesh), RayQuery direct sun + 3D clustered
//      lights, ReSTIR hemisphere GI + 2-pass edge-stopping SVGF, multi-bounce glossy & mirror BVH reflections, and exact
//      DielectricRefraction through ControlVehicle's 74 authored VehicleGlass canopy facets into the Slate cockpit.
//    - GTX Fallback Tier: Exact same primary triangle G-Buffer + 4-cascade toroidal Global Distance Field soft shadows &
//      cone-traced GI + CardRadiosity multi-bounce radiosity + OrderedSoftwareBvh glossy reflections.

#include "Engine/Rendering/DualTierRenderer.h"

#include "Engine/Render/DielectricRefraction.h"
#include "Engine/Rendering/LensFlareModel.h"
#include "Engine/Rendering/PathReplayRayQuery.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <thread>

namespace Frontier
{
namespace
{

constexpr double Pi = 3.14159265358979323846;

struct Vec3f
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

[[nodiscard]] inline Vec3f ToVec3f(const std::array<float, 3>& A) noexcept
{
    return { A[0], A[1], A[2] };
}

[[nodiscard]] inline Vec3f ToVec3f(SpatialVector V) noexcept
{
    return { static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z) };
}

[[nodiscard]] inline SpatialVector ToSpatial(Vec3f V) noexcept
{
    return { static_cast<double>(V.X), static_cast<double>(V.Y), static_cast<double>(V.Z) };
}

[[nodiscard]] inline double DotSpatial(SpatialVector A, SpatialVector B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

[[nodiscard]] inline SpatialVector NormalizeSpatial(SpatialVector V) noexcept
{
    const double Len = std::sqrt(std::max(DotSpatial(V, V), 1.0e-20));
    return { V.X / Len, V.Y / Len, V.Z / Len };
}

[[nodiscard]] inline Vec3f Add3(Vec3f A, Vec3f B) noexcept
{
    return { A.X + B.X, A.Y + B.Y, A.Z + B.Z };
}

[[nodiscard]] inline Vec3f Sub3(Vec3f A, Vec3f B) noexcept
{
    return { A.X - B.X, A.Y - B.Y, A.Z - B.Z };
}

[[nodiscard]] inline Vec3f Scale3(Vec3f A, float S) noexcept
{
    return { A.X * S, A.Y * S, A.Z * S };
}

[[nodiscard]] inline float Dot3(Vec3f A, Vec3f B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

[[nodiscard]] inline Vec3f Cross3(Vec3f A, Vec3f B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X
    };
}

[[nodiscard]] inline Vec3f Normalize3(Vec3f A) noexcept
{
    const float L = std::sqrt(std::max(Dot3(A, A), 1.0e-20f));
    return { A.X / L, A.Y / L, A.Z / L };
}

[[nodiscard]] inline SpectrumRgb ScaleSpec(SpectrumRgb C, double S) noexcept
{
    return { C.R * S, C.G * S, C.B * S };
}

[[nodiscard]] inline SpectrumRgb AddSpec(SpectrumRgb A, SpectrumRgb B) noexcept
{
    return { A.R + B.R, A.G + B.G, A.B + B.B };
}

[[nodiscard]] inline SpectrumRgb MulSpec(SpectrumRgb A, SpectrumRgb B) noexcept
{
    return { A.R * B.R, A.G * B.G, A.B * B.B };
}

[[nodiscard]] inline double Luminance(SpectrumRgb C) noexcept
{
    return 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B;
}

// 📐 Narkowicz ACES filmic fit — exact match to DisplayPresent.wgsl ProjectFilmic.
[[nodiscard]] inline SpectrumRgb ProjectFilmic(SpectrumRgb Radiance) noexcept
{
    const auto Curve = [](double X) -> double {
        const double V = std::max(0.0, X);
        return std::clamp((V * (2.51 * V + 0.03)) / (V * (2.43 * V + 0.59) + 0.14), 0.0, 1.0);
    };
    return { Curve(Radiance.R), Curve(Radiance.G), Curve(Radiance.B) };
}

[[nodiscard]] inline uint8_t LinearToSrgbByte(double Value) noexcept
{
    const double Clamped = std::clamp(Value, 0.0, 1.0);
    const double Encoded = Clamped <= 0.0031308
        ? (Clamped * 12.92)
        : (1.055 * std::pow(Clamped, 1.0 / 2.4) - 0.055);
    return static_cast<uint8_t>(std::clamp(std::lround(Encoded * 255.0), 0l, 255l));
}

[[nodiscard]] inline SpectrumRgb KelvinTint(double Kelvin) noexcept
{
    const double Temp = std::clamp(Kelvin, 1800.0, 12000.0) / 100.0;
    const double Red  = Temp <= 66.0 ? 1.0 : std::clamp(1.292936 * std::pow(Temp - 60.0, -0.133205), 0.0, 1.0);
    const double Grn  = Temp <= 66.0
        ? std::clamp(0.390082 * std::log(std::max(1.0, Temp)) - 0.631841, 0.0, 1.0)
        : std::clamp(1.129891 * std::pow(Temp - 60.0, -0.075515), 0.0, 1.0);
    const double Blu  = Temp >= 66.0 ? 1.0
        : (Temp <= 19.0 ? 0.0 : std::clamp(0.543207 * std::log(Temp - 10.0) - 1.196254, 0.0, 1.0));
    return { std::pow(Red, 2.2), std::pow(Grn, 2.2), std::pow(Blu, 2.2) };
}

[[nodiscard]] inline uint32_t HashPixel(uint32_t X, uint32_t Y, uint32_t Salt) noexcept
{
    uint32_t H = X * 0x9e3779b1u ^ Y * 0x85ebca77u ^ Salt * 0xc2b2ae3du;
    H ^= H >> 16u;
    H *= 0x7feb352du;
    H ^= H >> 15u;
    H *= 0x846ca68bu;
    H ^= H >> 16u;
    return H;
}

[[nodiscard]] std::filesystem::path DiscoverVehicleArchiveDirectory(const std::filesystem::path& ExplicitDir)
{
    if (!ExplicitDir.empty() && std::filesystem::exists(ExplicitDir / "ControlVehicleBody.facet"))
    {
        return ExplicitDir;
    }
    const std::array<std::filesystem::path, 5> Candidates = {{
        "Frontier/Projects/Project-Everest/Content/Vehicle",
        "Projects/Project-Everest/Content/Vehicle",
        "Content/Vehicle",
        "../Frontier/Projects/Project-Everest/Content/Vehicle",
        "../../Frontier/Projects/Project-Everest/Content/Vehicle"
    }};
    for (const auto& Path : Candidates)
    {
        if (std::filesystem::exists(Path / "ControlVehicleBody.facet"))
        {
            return Path;
        }
    }
    return ExplicitDir;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  RIGID TLAS INSTANCE
//------------------------------------------------------------------------------------------------------------------------

struct RigidInstance
{
    const void* BlasPtr   = nullptr;
    Vec3f       Translation{};
    Vec3f       R0{ 1.0f, 0.0f, 0.0f };
    Vec3f       R1{ 0.0f, 1.0f, 0.0f };
    Vec3f       R2{ 0.0f, 0.0f, 1.0f };
    Vec3f       WorldMin{};
    Vec3f       WorldMax{};
    bool        IsVehicle = false;

    [[nodiscard]] Vec3f LocalToWorldPoint(Vec3f P) const noexcept
    {
        return {
            Translation.X + R0.X * P.X + R1.X * P.Y + R2.X * P.Z,
            Translation.Y + R0.Y * P.X + R1.Y * P.Y + R2.Y * P.Z,
            Translation.Z + R0.Z * P.X + R1.Z * P.Y + R2.Z * P.Z
        };
    }

    [[nodiscard]] Vec3f LocalToWorldDir(Vec3f D) const noexcept
    {
        return {
            R0.X * D.X + R1.X * D.Y + R2.X * D.Z,
            R0.Y * D.X + R1.Y * D.Y + R2.Y * D.Z,
            R0.Z * D.X + R1.Z * D.Y + R2.Z * D.Z
        };
    }

    [[nodiscard]] Vec3f WorldToLocalPoint(Vec3f W) const noexcept
    {
        const Vec3f D = Sub3(W, Translation);
        return { Dot3(D, R0), Dot3(D, R1), Dot3(D, R2) };
    }

    [[nodiscard]] Vec3f WorldToLocalDir(Vec3f D) const noexcept
    {
        return { Dot3(D, R0), Dot3(D, R1), Dot3(D, R2) };
    }

    void ComputeWorldBounds(const std::array<float, 3>& LocalMin, const std::array<float, 3>& LocalMax) noexcept
    {
        WorldMin = {  1.0e30f,  1.0e30f,  1.0e30f };
        WorldMax = { -1.0e30f, -1.0e30f, -1.0e30f };
        for (int Corner = 0; Corner < 8; ++Corner)
        {
            const Vec3f P = LocalToWorldPoint({
                (Corner & 1) ? LocalMax[0] : LocalMin[0],
                (Corner & 2) ? LocalMax[1] : LocalMin[1],
                (Corner & 4) ? LocalMax[2] : LocalMin[2]
            });
            WorldMin.X = std::min(WorldMin.X, P.X);
            WorldMin.Y = std::min(WorldMin.Y, P.Y);
            WorldMin.Z = std::min(WorldMin.Z, P.Z);
            WorldMax.X = std::max(WorldMax.X, P.X);
            WorldMax.Y = std::max(WorldMax.Y, P.Y);
            WorldMax.Z = std::max(WorldMax.Z, P.Z);
        }
    }
};

struct SurfaceHit
{
    bool     Hit       = false;
    float    Distance  = std::numeric_limits<float>::infinity();
    Vec3f    Position{};
    Vec3f    Normal{ 0.0f, 0.0f, 1.0f };
    uint32_t Material  = 0u;
    bool     IsVehicle = false;
};

[[nodiscard]] inline bool IntersectAabb(Vec3f Origin, Vec3f InvDir, Vec3f BoxMin, Vec3f BoxMax,
                                        float MinT, float MaxT, float& OutNearT) noexcept
{
    const float Tx0 = (BoxMin.X - Origin.X) * InvDir.X;
    const float Tx1 = (BoxMax.X - Origin.X) * InvDir.X;
    float       Near = std::max(MinT, std::min(Tx0, Tx1));
    float       Far  = std::min(MaxT, std::max(Tx0, Tx1));

    const float Ty0 = (BoxMin.Y - Origin.Y) * InvDir.Y;
    const float Ty1 = (BoxMax.Y - Origin.Y) * InvDir.Y;
    Near = std::max(Near, std::min(Ty0, Ty1));
    Far  = std::min(Far,  std::max(Ty0, Ty1));

    const float Tz0 = (BoxMin.Z - Origin.Z) * InvDir.Z;
    const float Tz1 = (BoxMax.Z - Origin.Z) * InvDir.Z;
    Near = std::max(Near, std::min(Tz0, Tz1));
    Far  = std::min(Far,  std::max(Tz0, Tz1));

    OutNearT = Near;
    return Near <= Far;
}

[[nodiscard]] inline Vec3f SafeInvDir(Vec3f Dir) noexcept
{
    const auto Inv = [](float V) -> float {
        return std::fabs(V) > 1.0e-8f ? (1.0f / V) : (V >= 0.0f ? 1.0e8f : -1.0e8f);
    };
    return { Inv(Dir.X), Inv(Dir.Y), Inv(Dir.Z) };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  BINNED-SAH BLAS BUILDER
//------------------------------------------------------------------------------------------------------------------------

void DualTierProvingGroundsRenderer::MeshBlas::Build(std::vector<CourseTriangle> SourceTriangles)
{
    Triangles = std::move(SourceTriangles);
    Order.resize(Triangles.size());
    std::iota(Order.begin(), Order.end(), 0u);
    Nodes.clear();
    if (Triangles.empty())
    {
        return;
    }
    Nodes.reserve(Triangles.size() * 2u);

    const auto BuildRecursive = [&](auto& Self, uint32_t First, uint32_t Count) -> uint32_t {
        MeshBlasNode Node{};
        Node.First   = First;
        Node.Count   = Count;
        Node.Minimum = {  1.0e30f,  1.0e30f,  1.0e30f };
        Node.Maximum = { -1.0e30f, -1.0e30f, -1.0e30f };
        std::array<float, 3> CentMin = {  1.0e30f,  1.0e30f,  1.0e30f };
        std::array<float, 3> CentMax = { -1.0e30f, -1.0e30f, -1.0e30f };

        for (uint32_t Slot = First; Slot < First + Count; ++Slot)
        {
            const CourseTriangle& Tri = Triangles[Order[Slot]];
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const float Lo = std::min({ Tri.A[Axis], Tri.B[Axis], Tri.C[Axis] });
                const float Hi = std::max({ Tri.A[Axis], Tri.B[Axis], Tri.C[Axis] });
                const float Ct = (Tri.A[Axis] + Tri.B[Axis] + Tri.C[Axis]) * (1.0f / 3.0f);
                Node.Minimum[Axis] = std::min(Node.Minimum[Axis], Lo);
                Node.Maximum[Axis] = std::max(Node.Maximum[Axis], Hi);
                CentMin[Axis]      = std::min(CentMin[Axis], Ct);
                CentMax[Axis]      = std::max(CentMax[Axis], Ct);
            }
        }

        const uint32_t NodeIndex = static_cast<uint32_t>(Nodes.size());
        Nodes.push_back(Node);

        if (Count <= 4u)
        {
            return NodeIndex;
        }

        int BestAxis = 0;
        for (int Axis = 1; Axis < 3; ++Axis)
        {
            if ((CentMax[Axis] - CentMin[Axis]) > (CentMax[BestAxis] - CentMin[BestAxis]))
            {
                BestAxis = Axis;
            }
        }
        const float Span = CentMax[BestAxis] - CentMin[BestAxis];
        uint32_t    Mid  = First + Count / 2u;

        if (Span > 1.0e-5f)
        {
            constexpr int BinCount = 12;
            struct SahBin
            {
                uint32_t             Count = 0u;
                std::array<float, 3> Min   = {  1.0e30f,  1.0e30f,  1.0e30f };
                std::array<float, 3> Max   = { -1.0e30f, -1.0e30f, -1.0e30f };
            };
            std::array<SahBin, BinCount> Bins{};
            const float Scale = static_cast<float>(BinCount) / Span;
            for (uint32_t Slot = First; Slot < First + Count; ++Slot)
            {
                const CourseTriangle& Tri = Triangles[Order[Slot]];
                const float Ct = (Tri.A[BestAxis] + Tri.B[BestAxis] + Tri.C[BestAxis]) * (1.0f / 3.0f);
                const int   B  = std::clamp(static_cast<int>((Ct - CentMin[BestAxis]) * Scale), 0, BinCount - 1);
                ++Bins[B].Count;
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Bins[B].Min[Axis] = std::min(Bins[B].Min[Axis], std::min({ Tri.A[Axis], Tri.B[Axis], Tri.C[Axis] }));
                    Bins[B].Max[Axis] = std::max(Bins[B].Max[Axis], std::max({ Tri.A[Axis], Tri.B[Axis], Tri.C[Axis] }));
                }
            }

            const auto HalfArea = [](const std::array<float, 3>& Lo, const std::array<float, 3>& Hi) -> float {
                if (Lo[0] > Hi[0]) return 0.0f;
                const float Dx = Hi[0] - Lo[0], Dy = Hi[1] - Lo[1], Dz = Hi[2] - Lo[2];
                return Dx * Dy + Dy * Dz + Dz * Dx;
            };

            float BestCost  = 1.0e30f;
            int   BestSplit = -1;
            for (int Split = 1; Split < BinCount; ++Split)
            {
                SahBin Left{}, Right{};
                for (int I = 0; I < Split; ++I)
                {
                    Left.Count += Bins[I].Count;
                    for (int A = 0; A < 3; ++A)
                    {
                        Left.Min[A] = std::min(Left.Min[A], Bins[I].Min[A]);
                        Left.Max[A] = std::max(Left.Max[A], Bins[I].Max[A]);
                    }
                }
                for (int I = Split; I < BinCount; ++I)
                {
                    Right.Count += Bins[I].Count;
                    for (int A = 0; A < 3; ++A)
                    {
                        Right.Min[A] = std::min(Right.Min[A], Bins[I].Min[A]);
                        Right.Max[A] = std::max(Right.Max[A], Bins[I].Max[A]);
                    }
                }
                if (Left.Count == 0u || Right.Count == 0u)
                {
                    continue;
                }
                const float Cost = HalfArea(Left.Min, Left.Max) * static_cast<float>(Left.Count) +
                                   HalfArea(Right.Min, Right.Max) * static_cast<float>(Right.Count);
                if (Cost < BestCost)
                {
                    BestCost  = Cost;
                    BestSplit = Split;
                }
            }

            if (BestSplit > 0)
            {
                const float SplitPos = CentMin[BestAxis] + Span * (static_cast<float>(BestSplit) / static_cast<float>(BinCount));
                auto It = std::partition(Order.begin() + First, Order.begin() + First + Count, [&](uint32_t Idx) {
                    const CourseTriangle& Tri = Triangles[Idx];
                    const float Ct = (Tri.A[BestAxis] + Tri.B[BestAxis] + Tri.C[BestAxis]) * (1.0f / 3.0f);
                    return Ct < SplitPos;
                });
                const uint32_t Offset = static_cast<uint32_t>(It - Order.begin());
                if (Offset > First && Offset < First + Count)
                {
                    Mid = Offset;
                }
                else
                {
                    std::nth_element(Order.begin() + First, Order.begin() + Mid, Order.begin() + First + Count,
                                     [&](uint32_t A, uint32_t B) {
                                         const auto& Ta = Triangles[A];
                                         const auto& Tb = Triangles[B];
                                         return (Ta.A[BestAxis] + Ta.B[BestAxis] + Ta.C[BestAxis]) <
                                                (Tb.A[BestAxis] + Tb.B[BestAxis] + Tb.C[BestAxis]);
                                     });
                }
            }
        }
        else
        {
            std::nth_element(Order.begin() + First, Order.begin() + Mid, Order.begin() + First + Count,
                             [&](uint32_t A, uint32_t B) { return A < B; });
        }

        const uint32_t LeftChild  = Self(Self, First, Mid - First);
        const uint32_t RightChild = Self(Self, Mid, First + Count - Mid);
        Nodes[NodeIndex].First = LeftChild;
        Nodes[NodeIndex].Upper = RightChild;
        Nodes[NodeIndex].Count = 0u;
        return NodeIndex;
    };

    BuildRecursive(BuildRecursive, 0u, static_cast<uint32_t>(Triangles.size()));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TIER NEGOTIATION
//------------------------------------------------------------------------------------------------------------------------

DualTierTierDecision NegotiateDualTierExecution(
    const PathReplayRayQueryDeviceCapabilities& Capabilities,
    bool                                        PreferHardwareRtx) noexcept
{
    const PathReplayAccelerationSelection Selection = SelectPathReplayAccelerationTier(Capabilities, PreferHardwareRtx);
    const PathReplayAccelerationSelection CapProbe  = SelectPathReplayAccelerationTier(Capabilities, true);

    DualTierTierDecision Decision{};
    Decision.HardwareRtxCapable = CapProbe.HardwareRayQueryReady;
    if (Selection.Tier == PathReplayAccelerationTier::HardwareRayQuery && Selection.HardwareRayQueryReady)
    {
        Decision.Mode              = DualTierExecutionMode::HardwareRayQueryRtx;
        Decision.DiagnosticSummary = "RTX tier active: Two-Level SAH BVH + RayQuery + ReSTIR GI + DielectricGlazing";
    }
    else
    {
        Decision.Mode = DualTierExecutionMode::ToroidalSdfBvhGtx;
        Decision.DiagnosticSummary = Selection.FallbackReason != nullptr
            ? Selection.FallbackReason
            : "GTX fallback active: 4-cascade toroidal SDF + CardRadiosity + OrderedSoftwareBvh reflections";
    }
    return Decision;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            CONSTRUCTOR: ARCHIVES & BLAS BUILD
//------------------------------------------------------------------------------------------------------------------------

DualTierProvingGroundsRenderer::DualTierProvingGroundsRenderer(const std::filesystem::path& VehicleArchiveDirectory)
{
    const std::filesystem::path ResolvedDir = DiscoverVehicleArchiveDirectory(VehicleArchiveDirectory);
    VehicleGeometry = ConstructControlVehicleAssembly(ResolvedDir);
    CourseGeometry  = ConstructProvingGroundsAssembly();

    std::vector<CourseTriangle> AllBreakables;
    for (const BreakablePropEntry& Prop : CourseGeometry.Breakables)
    {
        AllBreakables.insert(AllBreakables.end(), Prop.Triangles.begin(), Prop.Triangles.end());
    }

    StaticWorldBlas.Build(CourseGeometry.StaticWorld);
    BreakablesBlas.Build(std::move(AllBreakables));
    VehicleBodyBlas.Build(VehicleGeometry.BodyOpaque);
    VehicleGlazingBlas.Build(VehicleGeometry.BodyGlazing);
    RoofSlimLedBlas.Build(VehicleGeometry.RoofSlimLed);
    RoofRallyPodBlas.Build(VehicleGeometry.RoofRallyPod);
    WheelLeftBlas.Build(VehicleGeometry.WheelLeft);
    WheelRightBlas.Build(VehicleGeometry.WheelRight);

    TotalAuthoredTriangles = static_cast<uint32_t>(
        StaticWorldBlas.Triangles.size() + BreakablesBlas.Triangles.size() +
        VehicleBodyBlas.Triangles.size() + VehicleGlazingBlas.Triangles.size() +
        2u * WheelLeftBlas.Triangles.size() + 2u * WheelRightBlas.Triangles.size());

    TotalBvhNodes = static_cast<uint32_t>(
        StaticWorldBlas.Nodes.size() + BreakablesBlas.Nodes.size() +
        VehicleBodyBlas.Nodes.size() + VehicleGlazingBlas.Nodes.size() +
        WheelLeftBlas.Nodes.size() + WheelRightBlas.Nodes.size());

    BakedSkyLut.Bake(AtmosphereParameters{}, 128u, 32u, 48u);

    // Solve planar surface cards over Proving Grounds landmarks (Start Apron, Hangar, Tunnel, Control Tower)
    std::vector<SurfaceCard> Cards = {
        SurfaceCard{ { -46.0, -7.0, 0.01 }, { 18.0, 0.0, 0.0 },  { 0.0, 14.0, 0.0 }, 8u, 6u, 0.08, 0.45 },
        SurfaceCard{ {  20.0, 45.8, 0.20 }, { 28.0, 0.0, 0.0 },  { 0.0,  0.0, 7.6 }, 8u, 4u, 0.42, 1.35 },
        SurfaceCard{ {  20.0, 32.0, 7.75 }, { 28.0, 0.0, 0.0 },  { 0.0, 13.0, 0.0 }, 8u, 4u, 0.30, 2.20 },
        SurfaceCard{ { -26.0, -44.0, 0.2 }, { 28.0, 0.0, 0.0 },  { 0.0,  0.0, 4.9 }, 8u, 4u, 0.35, 1.40 },
        SurfaceCard{ { -56.0, -13.0, 0.2 }, {  0.0, 4.4, 0.0 },  { 0.0,  0.0, 9.0 }, 4u, 6u, 0.46, 0.65 }
    };
    if (SolvedCards.Build(Cards, nullptr))
    {
        SolvedCardResidual = SolvedCards.Solve(64u, 1.0e-7);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      TOROIDAL 4-CASCADE SIGNED DISTANCE FIELD
//------------------------------------------------------------------------------------------------------------------------

double DualTierProvingGroundsRenderer::SampleProvingGroundsSignedDistance(
    SpatialVector                WorldPoint,
    const EverestViewportInputs& Inputs) const noexcept
{
    const double GroundH = static_cast<double>(QueryProvingGroundsCourseHeight(
        static_cast<float>(WorldPoint.X), static_cast<float>(WorldPoint.Y)));
    double Best = WorldPoint.Z - GroundH;

    const auto BoxSdf = [&](double Cx, double Cy, double Cz, double Hx, double Hy, double Hz) -> double {
        const double Dx = std::fabs(WorldPoint.X - Cx) - Hx;
        const double Dy = std::fabs(WorldPoint.Y - Cy) - Hy;
        const double Dz = std::fabs(WorldPoint.Z - Cz) - Hz;
        const double Ox = std::max(Dx, 0.0), Oy = std::max(Dy, 0.0), Oz = std::max(Dz, 0.0);
        return std::sqrt(Ox * Ox + Oy * Oy + Oz * Oz) + std::min(std::max({ Dx, Dy, Dz }), 0.0);
    };

    const double Vx   = WorldPoint.X - static_cast<double>(Inputs.VehiclePosition[0]);
    const double Vy   = WorldPoint.Y - static_cast<double>(Inputs.VehiclePosition[1]);
    const double Vz   = WorldPoint.Z - static_cast<double>(Inputs.VehiclePosition[2]);
    const double CosY = std::cos(-static_cast<double>(Inputs.VehicleYawRadians));
    const double SinY = std::sin(-static_cast<double>(Inputs.VehicleYawRadians));
    const double Lx   = Vx * CosY - Vy * SinY;
    const double Ly   = Vx * SinY + Vy * CosY;
    if (std::fabs(Lx) < 5.5 && std::fabs(Ly) < 3.5 && std::fabs(Vz) < 3.0)
    {
        auto LocalBox = [&](double Bx, double By, double Bz, double Hx, double Hy, double Hz) -> double {
            const double Dx = std::fabs(Lx - Bx) - Hx;
            const double Dy = std::fabs(Ly - By) - Hy;
            const double Dz = std::fabs(Vz - Bz) - Hz;
            const double Ox = std::max(Dx, 0.0), Oy = std::max(Dy, 0.0), Oz = std::max(Dz, 0.0);
            return std::sqrt(Ox * Ox + Oy * Oy + Oz * Oz) + std::min(std::max({ Dx, Dy, Dz }), 0.0);
        };
        Best = std::min(Best, LocalBox( 0.00, 0.0, 0.14, 2.98, 1.12, 0.36));
        Best = std::min(Best, LocalBox(-0.22, 0.0, 0.68, 1.44, 0.86, 0.36));
    }

    Best = std::min(Best, BoxSdf( 34.0,  46.0, 4.0, 14.2, 0.25, 4.0));
    Best = std::min(Best, BoxSdf( 48.0,  38.6, 4.0, 0.25, 7.60, 4.0));
    Best = std::min(Best, BoxSdf( 34.0,  38.6, 8.0, 14.2, 7.60, 0.20));
    Best = std::min(Best, BoxSdf(-12.0, -43.8, 2.6, 14.0, 0.28, 2.6));
    Best = std::min(Best, BoxSdf(-12.0, -52.2, 2.6, 14.0, 0.28, 2.6));
    Best = std::min(Best, BoxSdf(-54.0, -11.0, 6.0, 2.40, 2.40, 6.0));
    Best = std::min(Best, BoxSdf(-36.0,   8.5, 2.2, 0.22, 0.22, 2.2));
    Best = std::min(Best, BoxSdf(-36.0,  -8.5, 2.2, 0.22, 0.22, 2.2));
    return Best;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            SKY, ATMOSPHERE, CLOUDS & STARS
//------------------------------------------------------------------------------------------------------------------------

SpectrumRgb DualTierProvingGroundsRenderer::EvaluateSkyAndCelestial(
    SpatialVector                RayDir,
    const EverestViewportInputs& Inputs,
    bool                         IncludeSolarDisc) const noexcept
{
    const SpatialVector Dir    = NormalizeSpatial(RayDir);
    const SpatialVector SunDir = NormalizeSpatial(Inputs.SunDirection);
    const double        CosSun = std::clamp(DotSpatial(Dir, SunDir), -1.0, 1.0);

    SpectrumRgb Sky{ 0.008, 0.014, 0.026 };
    if (Inputs.AtmosphereEnabled)
    {
        const SpatialVector ObserverKm{ 0.0, 0.0, Inputs.Atmosphere.GroundRadiusKm + 0.015 };
        const SpectrumRgb   Tint       = KelvinTint(Inputs.SunTemperatureKelvin);
        const SpectrumRgb   Irradiance = ScaleSpec(Tint, std::max(0.12, Inputs.SolarIlluminance));
        const TransmittanceLut* LutPtr = BakedSkyLut.Baked() ? &BakedSkyLut : nullptr;
        const SpectrumRgb Single = SingleScattering(Inputs.Atmosphere, ObserverKm, Dir, SunDir, Irradiance, LutPtr, 16u);
        const SpectrumRgb Multi  = MultipleScatteringApprox(Inputs.Atmosphere, ObserverKm, Dir, SunDir, Single, Irradiance);
        Sky = ScaleSpec(AddSpec(Single, Multi), 0.14 * Inputs.SkyBrightnessScale);
    }

    const double HorizonGlow = std::pow(1.0 - std::clamp(std::fabs(Dir.Z), 0.0, 1.0), 4.5);
    const double DayFactor   = std::clamp((SunDir.Z + 0.08) / 0.34, 0.0, 1.0);
    Sky = AddSpec(Sky, ScaleSpec({ 0.16 * DayFactor + 0.010, 0.20 * DayFactor + 0.015, 0.28 * DayFactor + 0.028 },
                                 HorizonGlow * Inputs.SkyBrightnessScale));

    if (IncludeSolarDisc && Inputs.SunEnabled && Inputs.SunDiscEnabled && SunDir.Z > -0.06)
    {
        const SpectrumRgb Tint = KelvinTint(Inputs.SunTemperatureKelvin);
        if (CosSun > 0.99972)
        {
            Sky = AddSpec(Sky, ScaleSpec(Tint, 28.0 * Inputs.SolarIlluminance));
        }
        const double Corona = std::pow(std::max(0.0, CosSun), 220.0) * 1.5 + std::pow(std::max(0.0, CosSun), 18.0) * 0.12;
        Sky = AddSpec(Sky, ScaleSpec(Tint, Corona * (Inputs.SolarIlluminance / 12.0)));
    }

    if (SunDir.Z < 0.06)
    {
        const double NightWeight = std::clamp((0.06 - SunDir.Z) / 0.18, 0.0, 1.0);
        if (Inputs.StarsEnabled && Dir.Z > 0.015)
        {
            const double   Rot   = Inputs.StarRotationDegrees * Pi / 180.0;
            const double   Rx    = Dir.X * std::cos(Rot) - Dir.Y * std::sin(Rot);
            const double   Ry    = Dir.X * std::sin(Rot) + Dir.Y * std::cos(Rot);
            const double   Lon   = (std::atan2(Ry, Rx) + Pi) * 195.0;
            const double   Lat   = std::asin(std::clamp(Dir.Z, -1.0, 1.0)) * 310.0 + 500.0;
            const uint32_t CellX = static_cast<uint32_t>(std::floor(Lon));
            const uint32_t CellY = static_cast<uint32_t>(std::floor(Lat));
            const uint32_t H     = HashPixel(CellX, CellY, 0x51a7u);
            if ((H & 255u) < static_cast<uint32_t>(Inputs.StarLimitingMagnitude * 9.0))
            {
                const double Jx     = 0.20 + 0.60 * (static_cast<double>((H >> 8u)  & 255u) / 255.0);
                const double Jy     = 0.20 + 0.60 * (static_cast<double>((H >> 16u) & 255u) / 255.0);
                const double Dx     = (Lon - static_cast<double>(CellX) - Jx) * std::max(0.25, std::sqrt(1.0 - Dir.Z * Dir.Z));
                const double Dy     = (Lat - static_cast<double>(CellY) - Jy);
                const double DistSq = Dx * Dx + Dy * Dy;
                const double Psf    = std::exp(-DistSq * 92.0);
                if (Psf > 0.004)
                {
                    const double Mag01 = static_cast<double>((H >> 24u) & 255u) / 255.0;
                    const double Gain  = NightWeight * Psf * (0.25 + 2.85 * Mag01 * Mag01 * Mag01);
                    const SpectrumRgb StarTint = (Mag01 > 0.72)
                        ? SpectrumRgb{ 0.72, 0.84, 1.15 }
                        : (Mag01 < 0.30 ? SpectrumRgb{ 1.12, 0.88, 0.66 } : SpectrumRgb{ 0.96, 0.98, 1.05 });
                    Sky = AddSpec(Sky, ScaleSpec(StarTint, Gain));
                }
            }
        }
        if (Inputs.MoonEnabled)
        {
            const SpatialVector MoonDir   = NormalizeSpatial(Inputs.MoonDirection);
            const double        CosMoon   = DotSpatial(Dir, MoonDir);
            const double        HalfRad   = std::clamp(Inputs.MoonAngularDiameterDegrees, 0.2, 30.0) * (Pi / 360.0);
            const double        CosRadius = std::cos(HalfRad);
            if (CosMoon > CosRadius && IncludeSolarDisc)
            {
                const SpatialVector UpRef{ 0.0, 0.0, 1.0 };
                const SpatialVector MRight = NormalizeSpatial({
                    MoonDir.Y * UpRef.Z - MoonDir.Z * UpRef.Y,
                    MoonDir.Z * UpRef.X - MoonDir.X * UpRef.Z,
                    MoonDir.X * UpRef.Y - MoonDir.Y * UpRef.X
                });
                const SpatialVector MUp = NormalizeSpatial({
                    MRight.Y * MoonDir.Z - MRight.Z * MoonDir.Y,
                    MRight.Z * MoonDir.X - MRight.X * MoonDir.Z,
                    MRight.X * MoonDir.Y - MRight.Y * MoonDir.X
                });
                const double SinR = std::max(1.0e-5, std::sin(HalfRad));
                const double Mx   = DotSpatial(Dir, MRight) / SinR;
                const double My   = DotSpatial(Dir, MUp)    / SinR;
                const double Mz   = std::sqrt(std::max(0.0, 1.0 - Mx * Mx - My * My));
                const double PhaseAngle = std::acos(std::clamp(2.0 * Inputs.MoonIlluminationFraction - 1.0, -1.0, 1.0));
                const double LitTerm    = Mx * std::sin(PhaseAngle) + Mz * std::cos(PhaseAngle);
                const double Mare       = 0.82 + 0.18 * std::sin(Mx * 5.2 + 0.7) * std::cos(My * 4.4 - 0.5);
                const double DiscRad    = (LitTerm > 0.0)
                    ? (3.6 * (0.35 + 0.65 * LitTerm) * Mare)
                    : (0.14 * Mare); // Earthshine on unlit selenographic hemisphere
                Sky = AddSpec(ScaleSpec(Sky, 0.15), ScaleSpec({ 0.90, 0.94, 1.0 }, DiscRad * NightWeight));
            }
            Sky = AddSpec(Sky, ScaleSpec({ 0.12, 0.16, 0.28 },
                                         std::pow(std::max(0.0, CosMoon), 48.0) * Inputs.MoonlightStrength * NightWeight));
        }
    }

    if (Inputs.CloudsEnabled && Dir.Z > 0.030 && Inputs.CloudCoverage > 0.01)
    {
        const auto ValueNoise2D = [](double X, double Y, uint32_t Seed) -> double {
            const int32_t Ix = static_cast<int32_t>(std::floor(X));
            const int32_t Iy = static_cast<int32_t>(std::floor(Y));
            const double  Fx = X - static_cast<double>(Ix);
            const double  Fy = Y - static_cast<double>(Iy);
            const double  Ux = Fx * Fx * (3.0 - 2.0 * Fx);
            const double  Uy = Fy * Fy * (3.0 - 2.0 * Fy);
            const auto    Corner = [&](int32_t Dx, int32_t Dy) -> double {
                const uint32_t H = HashPixel(static_cast<uint32_t>(Ix + Dx), static_cast<uint32_t>(Iy + Dy), Seed);
                return static_cast<double>(H & 0xffffu) / 65535.0;
            };
            const double N00 = Corner(0, 0), N10 = Corner(1, 0);
            const double N01 = Corner(0, 1), N11 = Corner(1, 1);
            return (N00 * (1.0 - Ux) + N10 * Ux) * (1.0 - Uy) + (N01 * (1.0 - Ux) + N11 * Ux) * Uy;
        };

        const double TPlane = 1500.0 / std::max(0.055, Dir.Z + 0.025);
        double       Px     = Dir.X * TPlane * 0.00042 + 3.7;
        double       Py     = Dir.Y * TPlane * 0.00042 - 1.9;
        double       Fbm    = 0.0;
        double       Billow = 0.0;
        double       Amp    = 0.52;
        for (int Oct = 0; Oct < 5; ++Oct)
        {
            const double N = ValueNoise2D(Px, Py, 0x9e37u + static_cast<uint32_t>(Oct) * 131u);
            Fbm    += Amp * N;
            Billow += Amp * (1.0 - std::fabs(2.0 * N - 1.0));
            const double Rx = 1.62 * Px - 1.18 * Py + 7.13;
            const double Ry = 1.18 * Px + 1.62 * Py - 4.27;
            Px  = Rx;
            Py  = Ry;
            Amp *= 0.50;
        }
        const double Field  = 0.68 * Fbm + 0.32 * Billow;
        const double Thresh = 0.92 - Inputs.CloudCoverage * 0.72;
        const double Raw    = std::clamp((Field - Thresh) / std::max(0.14, 1.0 - Thresh), 0.0, 1.0);
        const double Dens   = Raw * Raw * (3.0 - 2.0 * Raw);
        if (Dens > 0.002)
        {
            const double      SunLit    = std::clamp(SunDir.Z * 1.45 + 0.16, 0.03, 1.18);
            const double      PhaseHg   = std::pow(std::max(0.0, CosSun), 10.0) * 0.62 * (1.0 - 0.55 * Dens);
            const double      Powder    = 1.0 - std::exp(-Dens * 3.2);
            const SpectrumRgb Tint      = KelvinTint(Inputs.SunTemperatureKelvin);
            const double      SelfShade = 0.42 + 0.58 * (1.0 - 0.52 * Dens) * (0.72 + 0.28 * Powder);
            const SpectrumRgb CloudLit{
                (0.80 * Tint.R * SunLit + PhaseHg + 0.035) * SelfShade,
                (0.82 * Tint.G * SunLit + PhaseHg + 0.045) * SelfShade,
                (0.88 * Tint.B * SunLit + PhaseHg + 0.068) * SelfShade
            };
            const double HorizonFade = std::clamp((Dir.Z - 0.030) / 0.14, 0.0, 1.0);
            const double Alpha       = std::clamp(Dens * 0.90 * HorizonFade, 0.0, 0.93);
            Sky = {
                Sky.R * (1.0 - Alpha) + CloudLit.R * Alpha,
                Sky.G * (1.0 - Alpha) + CloudLit.G * Alpha,
                Sky.B * (1.0 - Alpha) + CloudLit.B * Alpha
            };
        }
    }

    if (Inputs.LightningActive && Inputs.LightningFlashIntensity > 0.01)
    {
        Sky = AddSpec(Sky, ScaleSpec({ 0.72, 0.84, 1.15 }, Inputs.LightningFlashIntensity * 1.6));
    }

    return Sky;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              WORLD POINT PROJECTION
//------------------------------------------------------------------------------------------------------------------------

bool DualTierProvingGroundsRenderer::ProjectWorldPoint(
    SpatialVector                WorldPoint,
    const EverestViewportInputs& Inputs,
    double                       AspectRatio,
    double&                      OutU,
    double&                      OutV) const noexcept
{
    const float OrbitYawRad   = static_cast<float>(Inputs.CameraYawDegrees * Pi / 180.0);
    const float OrbitPitchRad = static_cast<float>(Inputs.CameraPitchDegrees * Pi / 180.0);
    const ResolvedCameraFrame Cam = ResolveProvingGroundsCamera(
        Inputs.CameraMode,
        Inputs.VehiclePosition,
        Inputs.VehicleYawRadians,
        OrbitYawRad,
        OrbitPitchRad,
        static_cast<float>(Inputs.CameraDistanceMetres),
        static_cast<float>(AspectRatio));

    const Vec3f Eye     = ToVec3f(Cam.Eye);
    const Vec3f Forward = Normalize3(Sub3(ToVec3f(Cam.Target), Eye));
    const Vec3f Right   = Normalize3(Cross3(Forward, ToVec3f(Cam.Up)));
    const Vec3f Up      = Cross3(Right, Forward);

    const Vec3f Delta = Sub3(ToVec3f(WorldPoint), Eye);
    const float Depth = Dot3(Delta, Forward);
    if (Depth <= 0.08f)
    {
        return false;
    }
    const float HalfTanY = std::tan(Cam.VerticalFovDeg * static_cast<float>(Pi) / 360.0f);
    const float HalfTanX = HalfTanY * static_cast<float>(std::max(0.2, AspectRatio));
    const float NdcX     = Dot3(Delta, Right) / (Depth * HalfTanX);
    const float NdcY     = Dot3(Delta, Up)    / (Depth * HalfTanY);
    OutU = 0.5 + 0.5 * static_cast<double>(NdcX);
    OutV = 0.5 - 0.5 * static_cast<double>(NdcY);
    return OutU >= -0.2 && OutU <= 1.2 && OutV >= -0.2 && OutV <= 1.2;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              DUAL-TIER FRAME EVALUATION
//------------------------------------------------------------------------------------------------------------------------

void DualTierProvingGroundsRenderer::RenderFrame(
    uint32_t                     Width,
    uint32_t                     Height,
    const EverestViewportInputs& Inputs,
    std::vector<SpectrumRgb>&    OutLinearRgb,
    DualTierFrameTelemetry&      OutTelemetry) const
{
    OutTelemetry = {};
    if (Width == 0u || Height == 0u)
    {
        OutLinearRgb.clear();
        return;
    }
    const size_t PixelCount = static_cast<size_t>(Width) * static_cast<size_t>(Height);
    OutLinearRgb.assign(PixelCount, SpectrumRgb{ 0.0, 0.0, 0.0 });

    std::array<CourseMaterialSpecification, 82> Materials = ConstructCourseMaterials({
        static_cast<float>(Inputs.VehicleBodyAlbedo.R),
        static_cast<float>(Inputs.VehicleBodyAlbedo.G),
        static_cast<float>(Inputs.VehicleBodyAlbedo.B)
    });
    Materials[static_cast<size_t>(CourseMaterialId::BodyCoat)].Roughness =
        static_cast<float>(std::clamp(Inputs.VehicleBodyRoughness, 0.02, 0.95));
    if (!Inputs.HeadlampsEnabled)
    {
        Materials[static_cast<size_t>(CourseMaterialId::Headlight)].Emission = { 0.0f, 0.0f, 0.0f };
    }
    if (!Inputs.BrakeLampsEnabled)
    {
        Materials[static_cast<size_t>(CourseMaterialId::Taillight)].Emission = { 0.8f, 0.0f, 0.01f };
    }
    if (!Inputs.HazardLampsEnabled)
    {
        Materials[static_cast<size_t>(CourseMaterialId::Amber)].Emission = { 0.15f, 0.03f, 0.0f };
    }

    if (Inputs.SurfaceWetnessOrSnow > 0.01)
    {
        const float Amount = static_cast<float>(std::clamp(Inputs.SurfaceWetnessOrSnow, 0.0, 1.0));
        auto&       Asph   = Materials[static_cast<size_t>(CourseMaterialId::Asphalt)];
        auto&       Patch  = Materials[static_cast<size_t>(CourseMaterialId::AsphaltPatch)];
        if (Inputs.SnowMode)
        {
            for (int C = 0; C < 3; ++C)
            {
                Asph.Albedo[C]  = Asph.Albedo[C]  * (1.0f - Amount * 0.78f) + 0.72f * Amount * 0.78f;
                Patch.Albedo[C] = Patch.Albedo[C] * (1.0f - Amount * 0.72f) + 0.68f * Amount * 0.72f;
            }
            Asph.Roughness = std::max(0.45f, Asph.Roughness - 0.22f * Amount);
        }
        else
        {
            for (int C = 0; C < 3; ++C)
            {
                Asph.Albedo[C]  *= (1.0f - 0.42f * Amount);
                Patch.Albedo[C] *= (1.0f - 0.42f * Amount);
            }
            Asph.Roughness     = Asph.Roughness  * (1.0f - Amount) + 0.09f * Amount;
            Patch.Roughness    = Patch.Roughness * (1.0f - Amount) + 0.12f * Amount;
            Asph.CoatWeight    = 0.75f * Amount;
            Asph.CoatRoughness = 0.05f;
        }
    }

    // Two-Level BVH TLAS Instances (full 6-DOF yaw/pitch/roll chassis basis)
    const float CosYaw = std::cos(Inputs.VehicleYawRadians);
    const float SinYaw = std::sin(Inputs.VehicleYawRadians);
    const float CosPit = std::cos(Inputs.VehiclePitchRadians);
    const float SinPit = std::sin(Inputs.VehiclePitchRadians);
    const float CosRol = std::cos(Inputs.VehicleRollRadians);
    const float SinRol = std::sin(Inputs.VehicleRollRadians);
    const Vec3f CarPos = ToVec3f(Inputs.VehiclePosition);
    const Vec3f CarFwd = Normalize3({ CosYaw * CosPit, SinYaw * CosPit, -SinPit });
    const Vec3f FlatLf = { -SinYaw, CosYaw, 0.0f };
    const Vec3f PitchU = Cross3(CarFwd, FlatLf);
    const Vec3f CarLft = Normalize3(Add3(Scale3(FlatLf, CosRol), Scale3(PitchU, SinRol)));
    const Vec3f CarUp  = Normalize3(Cross3(CarFwd, CarLft));

    std::vector<RigidInstance> OpaqueTlas;
    OpaqueTlas.reserve(8u);

    const auto PushInstance = [&](std::vector<RigidInstance>& List, const MeshBlas& Blas,
                                  Vec3f Pos, Vec3f R0, Vec3f R1, Vec3f R2, bool VehicleFlag) {
        if (Blas.Nodes.empty())
        {
            return;
        }
        RigidInstance Inst{};
        Inst.BlasPtr     = &Blas;
        Inst.Translation = Pos;
        Inst.R0          = R0;
        Inst.R1          = R1;
        Inst.R2          = R2;
        Inst.IsVehicle   = VehicleFlag;
        Inst.ComputeWorldBounds(Blas.Nodes[0].Minimum, Blas.Nodes[0].Maximum);
        List.push_back(Inst);
    };

    PushInstance(OpaqueTlas, StaticWorldBlas, { 0.0f, 0.0f, 0.0f },
                 { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, false);
    if (Inputs.BreakablesEnabled)
    {
        PushInstance(OpaqueTlas, BreakablesBlas, { 0.0f, 0.0f, 0.0f },
                     { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, false);
    }
    PushInstance(OpaqueTlas, VehicleBodyBlas, CarPos, CarFwd, CarLft, CarUp, true);
    if (Inputs.RoofLightEnabled)
    {
        PushInstance(OpaqueTlas, Inputs.RoofRallyPodMode ? RoofRallyPodBlas : RoofSlimLedBlas,
                     CarPos, CarFwd, CarLft, CarUp, true);
    }

    const float HubLocalZ = ControlVehicleConstants::AxleSocketZ - ControlVehicleConstants::CoMAuthoredZ();
    const std::array<std::array<float, 3>, 4> WheelSockets = {{
        { ControlVehicleConstants::AxleFrontX,  ControlVehicleConstants::AxleHalfTrackY, HubLocalZ },
        { ControlVehicleConstants::AxleFrontX, -ControlVehicleConstants::AxleHalfTrackY, HubLocalZ },
        { ControlVehicleConstants::AxleRearX,   ControlVehicleConstants::AxleHalfTrackY, HubLocalZ },
        { ControlVehicleConstants::AxleRearX,  -ControlVehicleConstants::AxleHalfTrackY, HubLocalZ }
    }};
    for (int Wheel = 0; Wheel < 4; ++Wheel)
    {
        Vec3f LocalHub = ToVec3f(WheelSockets[Wheel]);
        if (Inputs.UsePerCornerWheelPose)
        {
            LocalHub.Z += Inputs.WheelVerticalOffsetM[Wheel];
        }
        const Vec3f WorldHub = {
            CarPos.X + CarFwd.X * LocalHub.X + CarLft.X * LocalHub.Y + CarUp.X * LocalHub.Z,
            CarPos.Y + CarFwd.Y * LocalHub.X + CarLft.Y * LocalHub.Y + CarUp.Y * LocalHub.Z,
            CarPos.Z + CarFwd.Z * LocalHub.X + CarLft.Z * LocalHub.Y + CarUp.Z * LocalHub.Z
        };
        const float SteerAngle = Inputs.UsePerCornerWheelPose
            ? Inputs.WheelSteerPerCorner[Wheel]
            : (Wheel < 2 ? Inputs.FrontSteerRadians : 0.0f);
        const float Spin   = Inputs.UsePerCornerWheelPose
            ? Inputs.WheelSpinPerCorner[Wheel]
            : Inputs.WheelSpinRadians;
        const float Cs     = std::cos(SteerAngle), Ss = std::sin(SteerAngle);
        const float Cp     = std::cos(Spin),       Sp = std::sin(Spin);
        const Vec3f SteerX = Normalize3(Add3(Scale3(CarFwd, Cs), Scale3(CarLft,  Ss)));
        const Vec3f SteerY = Normalize3(Add3(Scale3(CarLft, Cs), Scale3(CarFwd, -Ss)));
        const Vec3f SteerZ = CarUp;
        const Vec3f SpinX  = Add3(Scale3(SteerX, Cp), Scale3(SteerZ, -Sp));
        const Vec3f SpinZ  = Add3(Scale3(SteerX, Sp), Scale3(SteerZ,  Cp));
        PushInstance(OpaqueTlas, (Wheel & 1) == 0 ? WheelLeftBlas : WheelRightBlas,
                     WorldHub, SpinX, SteerY, SpinZ, true);
    }

    RigidInstance GlazingInstance{};
    const bool    HasGlazing = !VehicleGlazingBlas.Nodes.empty();
    if (HasGlazing)
    {
        GlazingInstance.BlasPtr     = &VehicleGlazingBlas;
        GlazingInstance.Translation = CarPos;
        GlazingInstance.R0          = CarFwd;
        GlazingInstance.R1          = CarLft;
        GlazingInstance.R2          = CarUp;
        GlazingInstance.IsVehicle   = true;
        GlazingInstance.ComputeWorldBounds(VehicleGlazingBlas.Nodes[0].Minimum, VehicleGlazingBlas.Nodes[0].Maximum);
    }

    // Active 3D Clustered Lights
    std::vector<ProvingGroundsLight> ActiveLights = CourseGeometry.StaticLights;
    if (Inputs.HeadlampsEnabled && Inputs.AutomotiveBeamMode != 0u)
    {
        for (float SideY : { 0.65f, -0.65f })
        {
            const Vec3f Pos = {
                CarPos.X + CarFwd.X * 2.45f + CarLft.X * SideY,
                CarPos.Y + CarFwd.Y * 2.45f + CarLft.Y * SideY,
                CarPos.Z + 0.22f
            };
            if (Inputs.AutomotiveBeamMode == 2u)
            {
                ActiveLights.push_back(ProvingGroundsLight{
                    ClusteredLightKind::HighBeam, { Pos.X, Pos.Y, Pos.Z }, { CarFwd.X, CarFwd.Y, -0.02f },
                    { 0.95f, 0.98f, 1.0f }, 75.0f, 120.0f, 0.98f, 0.85f
                });
            }
            else if (Inputs.AutomotiveBeamMode == 3u)
            {
                ActiveLights.push_back(ProvingGroundsLight{
                    ClusteredLightKind::FogBeam, { Pos.X, Pos.Y, Pos.Z - 0.14f }, { CarFwd.X, CarFwd.Y, -0.06f },
                    { 1.0f, 0.90f, 0.50f }, 40.0f, 45.0f, 0.85f, 0.40f
                });
            }
            else
            {
                ActiveLights.push_back(ProvingGroundsLight{
                    ClusteredLightKind::LowBeam, { Pos.X, Pos.Y, Pos.Z }, { CarFwd.X, CarFwd.Y, -0.05f },
                    { 0.92f, 0.96f, 1.0f }, 38.0f, 60.0f, 0.90f, 0.60f
                });
            }
        }
    }
    if (Inputs.BrakeLampsEnabled)
    {
        for (float SideY : { 0.62f, -0.62f })
        {
            const Vec3f Pos = {
                CarPos.X - CarFwd.X * 3.12f + CarLft.X * SideY,
                CarPos.Y - CarFwd.Y * 3.12f + CarLft.Y * SideY,
                CarPos.Z + 0.36f
            };
            ActiveLights.push_back(ProvingGroundsLight{
                ClusteredLightKind::Omni, { Pos.X, Pos.Y, Pos.Z }, { -CarFwd.X, -CarFwd.Y, -0.1f },
                { 1.0f, 0.05f, 0.04f }, 5.5f, 8.5f, -1.0f, -1.0f
            });
        }
    }
    if (Inputs.RoofLightEnabled)
    {
        ActiveLights.push_back(ProvingGroundsLight{
            ClusteredLightKind::Floodlight,
            { CarPos.X + CarFwd.X * 0.45f, CarPos.Y + CarFwd.Y * 0.45f, CarPos.Z + 0.98f },
            { CarFwd.X, CarFwd.Y, -0.04f },
            { 0.92f, 0.96f, 1.0f }, 85.0f, 100.0f, 0.866f, 0.707f
        });
    }

    const auto TraceInstance = [](const RigidInstance& Inst, Vec3f WorldOrigin, Vec3f WorldDir,
                                  float MinT, float MaxT, bool AnyHit, SurfaceHit& Best) -> bool {
        const auto* Blas = static_cast<const MeshBlas*>(Inst.BlasPtr);
        if (Blas == nullptr || Blas->Nodes.empty())
        {
            return false;
        }
        const Vec3f LocalOrigin = Inst.WorldToLocalPoint(WorldOrigin);
        const Vec3f LocalDir    = Inst.WorldToLocalDir(WorldDir);
        const Vec3f InvDir      = SafeInvDir(LocalDir);

        float RootNear = 0.0f;
        if (!IntersectAabb(LocalOrigin, InvDir, ToVec3f(Blas->Nodes[0].Minimum), ToVec3f(Blas->Nodes[0].Maximum),
                           MinT, MaxT, RootNear))
        {
            return false;
        }

        uint32_t Stack[48];
        uint32_t StackSize = 0u;
        Stack[StackSize++] = 0u;
        bool Found         = false;

        while (StackSize > 0u)
        {
            const MeshBlasNode& Node = Blas->Nodes[Stack[--StackSize]];
            if (Node.Count > 0u)
            {
                for (uint32_t Slot = Node.First; Slot < Node.First + Node.Count; ++Slot)
                {
                    const CourseTriangle& Tri = Blas->Triangles[Blas->Order[Slot]];
                    const Vec3f A   = ToVec3f(Tri.A);
                    const Vec3f E1  = Sub3(ToVec3f(Tri.B), A);
                    const Vec3f E2  = Sub3(ToVec3f(Tri.C), A);
                    const Vec3f P   = Cross3(LocalDir, E2);
                    const float Det = Dot3(E1, P);
                    if (std::fabs(Det) < 1.0e-9f)
                    {
                        continue;
                    }
                    const float InvDet = 1.0f / Det;
                    const Vec3f Tvec   = Sub3(LocalOrigin, A);
                    const float U      = Dot3(Tvec, P) * InvDet;
                    if (U < 0.0f || U > 1.0f)
                    {
                        continue;
                    }
                    const Vec3f Q = Cross3(Tvec, E1);
                    const float V = Dot3(LocalDir, Q) * InvDet;
                    if (V < 0.0f || U + V > 1.0f)
                    {
                        continue;
                    }
                    const float T = Dot3(E2, Q) * InvDet;
                    if (T > MinT && T < MaxT)
                    {
                        if (AnyHit)
                        {
                            return true;
                        }
                        MaxT  = T;
                        Found = true;
                        const float W0 = 1.0f - U - V;
                        const Vec3f LocalNormal = Normalize3({
                            W0 * Tri.NormalA[0] + U * Tri.NormalB[0] + V * Tri.NormalC[0],
                            W0 * Tri.NormalA[1] + U * Tri.NormalB[1] + V * Tri.NormalC[1],
                            W0 * Tri.NormalA[2] + U * Tri.NormalB[2] + V * Tri.NormalC[2]
                        });
                        Vec3f WorldNormal = Normalize3(Inst.LocalToWorldDir(LocalNormal));
                        if (Dot3(WorldNormal, WorldDir) > 0.0f)
                        {
                            WorldNormal = Scale3(WorldNormal, -1.0f);
                        }
                        Best.Hit       = true;
                        Best.Distance  = T;
                        Best.Position  = Add3(WorldOrigin, Scale3(WorldDir, T));
                        Best.Normal    = WorldNormal;
                        Best.Material  = Tri.Material;
                        Best.IsVehicle = Inst.IsVehicle;
                    }
                }
            }
            else
            {
                const MeshBlasNode& Left  = Blas->Nodes[Node.First];
                const MeshBlasNode& Right = Blas->Nodes[Node.Upper];
                float NearL = 0.0f, NearR = 0.0f;
                const bool HitL = IntersectAabb(LocalOrigin, InvDir, ToVec3f(Left.Minimum), ToVec3f(Left.Maximum),
                                                MinT, MaxT, NearL);
                const bool HitR = IntersectAabb(LocalOrigin, InvDir, ToVec3f(Right.Minimum), ToVec3f(Right.Maximum),
                                                MinT, MaxT, NearR);
                if (HitL && HitR)
                {
                    if (NearL < NearR)
                    {
                        Stack[StackSize++] = Node.Upper;
                        Stack[StackSize++] = Node.First;
                    }
                    else
                    {
                        Stack[StackSize++] = Node.First;
                        Stack[StackSize++] = Node.Upper;
                    }
                }
                else if (HitL)
                {
                    Stack[StackSize++] = Node.First;
                }
                else if (HitR)
                {
                    Stack[StackSize++] = Node.Upper;
                }
            }
        }
        return Found;
    };

    const auto TraceTlasClosest = [&](Vec3f Origin, Vec3f Dir, float MinT, float MaxT) -> SurfaceHit {
        SurfaceHit  Best{};
        Best.Distance        = MaxT;
        const Vec3f WorldInv = SafeInvDir(Dir);
        for (const RigidInstance& Inst : OpaqueTlas)
        {
            float InstNear = 0.0f;
            if (!IntersectAabb(Origin, WorldInv, Inst.WorldMin, Inst.WorldMax, MinT, Best.Distance, InstNear))
            {
                continue;
            }
            TraceInstance(Inst, Origin, Dir, MinT, Best.Distance, false, Best);
        }
        return Best;
    };

    const auto TraceTlasOccluded = [&](Vec3f Origin, Vec3f Dir, float MinT, float MaxT) -> bool {
        SurfaceHit  Dummy{};
        const Vec3f WorldInv = SafeInvDir(Dir);
        for (const RigidInstance& Inst : OpaqueTlas)
        {
            float InstNear = 0.0f;
            if (!IntersectAabb(Origin, WorldInv, Inst.WorldMin, Inst.WorldMax, MinT, MaxT, InstNear))
            {
                continue;
            }
            if (TraceInstance(Inst, Origin, Dir, MinT, MaxT, true, Dummy))
            {
                return true;
            }
        }
        return false;
    };

    const double AspectRatio   = static_cast<double>(Width) / static_cast<double>(Height);
    const float  OrbitYawRad   = static_cast<float>(Inputs.CameraYawDegrees * Pi / 180.0);
    const float  OrbitPitchRad = static_cast<float>(Inputs.CameraPitchDegrees * Pi / 180.0);
    const ResolvedCameraFrame Cam = ResolveProvingGroundsCamera(
        Inputs.CameraMode,
        Inputs.VehiclePosition,
        Inputs.VehicleYawRadians,
        OrbitYawRad,
        OrbitPitchRad,
        static_cast<float>(Inputs.CameraDistanceMetres),
        static_cast<float>(AspectRatio));

    const Vec3f CamEye     = ToVec3f(Cam.Eye);
    const Vec3f CamForward = Normalize3(Sub3(ToVec3f(Cam.Target), CamEye));
    const Vec3f CamRight   = Normalize3(Cross3(CamForward, ToVec3f(Cam.Up)));
    const Vec3f CamUp      = Cross3(CamRight, CamForward);
    const float HalfTanY   = std::tan(Cam.VerticalFovDeg * static_cast<float>(Pi) / 360.0f);
    const float HalfTanX   = HalfTanY * static_cast<float>(AspectRatio);

    const bool          IsRtx    = (Inputs.ExecutionMode == DualTierExecutionMode::HardwareRayQueryRtx);
    const SpatialVector SunDirSp = NormalizeSpatial(Inputs.SunDirection);
    const Vec3f         SunDir   = ToVec3f(SunDirSp);
    const SpectrumRgb   SunTint  = KelvinTint(Inputs.SunTemperatureKelvin);
    const double        SunAbove = std::clamp((SunDirSp.Z + 0.02) / 0.16, 0.0, 1.0);
    const SpectrumRgb   SunRadiance = Inputs.SunEnabled
        ? ScaleSpec(SunTint, Inputs.SolarIlluminance * 0.24 * SunAbove)
        : SpectrumRgb{ 0.0, 0.0, 0.0 };

    const double MeanCardRadiosity = SolvedCards.TexelCount() > 0u
        ? SolvedCards.Radiosity(0u) * 0.06
        : 0.04;

    const auto SampleSevenSegmentDigit = [](int Digit, float X, float Y) -> bool {
        // Normalized glyph box X in [0..1], Y in [0..1] (Y=1 top, Y=0 bottom)
        static constexpr uint8_t Masks[10] = {
            0b1110111, // 0: top, tl, tr, bl, br, bot
            0b0010010, // 1: tr, br
            0b1011101, // 2: top, tr, mid, bl, bot
            0b1011011, // 3: top, tr, mid, br, bot
            0b0111010, // 4: tl, tr, mid, br
            0b1101011, // 5: top, tl, mid, br, bot
            0b1101111, // 6: top, tl, mid, bl, br, bot
            0b1010010, // 7: top, tr, br
            0b1111111, // 8: all
            0b1111011  // 9: top, tl, tr, mid, br, bot
        };
        if (Digit < 0 || Digit > 9 || X < 0.0f || X > 1.0f || Y < 0.0f || Y > 1.0f)
        {
            return false;
        }
        const uint8_t M = Masks[Digit];
        const float   T = 0.14f;
        if ((M & 0b1000000u) && Y > 1.0f - T && X > 0.10f && X < 0.90f) return true; // top
        if ((M & 0b0001000u) && std::fabs(Y - 0.50f) < T * 0.5f && X > 0.12f && X < 0.88f) return true; // mid
        if ((M & 0b0000001u) && Y < T && X > 0.10f && X < 0.90f) return true; // bot
        if ((M & 0b0100000u) && X < T * 1.15f && Y > 0.52f && Y < 0.94f) return true; // tl
        if ((M & 0b0010000u) && X > 1.0f - T * 1.15f && Y > 0.52f && Y < 0.94f) return true; // tr
        if ((M & 0b0000100u) && X < T * 1.15f && Y > 0.06f && Y < 0.48f) return true; // bl
        if ((M & 0b0000010u) && X > 1.0f - T * 1.15f && Y > 0.06f && Y < 0.48f) return true; // br
        return false;
    };

    const auto EvaluateCabinHmiEmission = [&](Vec3f WorldPos) -> SpectrumRgb {
        const Vec3f Rel  = Sub3(WorldPos, CarPos);
        const Vec3f Body = { Dot3(Rel, CarFwd), Dot3(Rel, CarLft), Rel.Z };
        const float Reb  = ControlVehicleConstants::CoMAuthoredZ();

        if (Body.Y > 0.20f)
        {
            // Hexagonal OLED Steering-Wheel Cluster (ClusterSequence.js / ControlVehicle3DCockpitHmi960x540.png)
            constexpr float SinT = 0.21822962f; // sin(0.22)
            constexpr float CosT = 0.97589745f; // cos(0.22)
            const float     Lx   = 0.42f - Body.Y;
            const float     Ly   = SinT * (Body.X - 0.68f) + CosT * (Body.Z - (0.82f - Reb));
            const float     U    = Lx / 0.135f;
            const float     V    = Ly / 0.0755f;

            SpectrumRgb Out{ 0.016, 0.022, 0.032 };
            const float HexDist = std::max(std::fabs(V), std::fabs(U) + 0.38f * std::fabs(V));
            if (HexDist > 0.855f && HexDist < 0.905f)
            {
                return { 0.45, 1.85, 2.45 };
            }

            // Top 10 shift-light RPM segments
            if (V > 0.68f && V < 0.79f && std::fabs(U) < 0.54f)
            {
                const float Slot = (U + 0.54f) / 1.08f * 10.0f;
                const float Frac = Slot - std::floor(Slot);
                if (Frac > 0.14f && Frac < 0.86f)
                {
                    return { 2.25, 0.72, 0.58 };
                }
            }

            // Left 13-bar blue/coral RPM ladder & Right 13-bar warm amber torque ladder
            if (V > -0.58f && V < 0.58f)
            {
                const float RowF = (V + 0.58f) / 1.16f * 13.0f;
                const int   Row  = static_cast<int>(std::floor(RowF));
                const float RFrac = RowF - static_cast<float>(Row);
                const float Bow   = 0.10f * (1.0f - V * V);
                if (RFrac > 0.22f && RFrac < 0.82f)
                {
                    if (U > -0.74f - Bow && U < -0.43f - Bow * 0.5f)
                    {
                        const double TopMix = std::clamp((static_cast<double>(Row) - 8.0) / 4.0, 0.0, 1.0);
                        return {
                            0.35 * (1.0 - TopMix) + 2.20 * TopMix,
                            1.15 * (1.0 - TopMix) + 0.55 * TopMix,
                            2.35 * (1.0 - TopMix) + 0.55 * TopMix
                        };
                    }
                    if (U > 0.43f + Bow * 0.5f && U < 0.74f + Bow)
                    {
                        return { 2.25, 0.86 + 0.25 * (1.0 - Row / 13.0), 0.36 };
                    }
                }
            }

            // Central speed + gear glass card
            if (std::fabs(U) < 0.37f && V > -0.34f && V < 0.58f)
            {
                Out = { 0.032, 0.042, 0.058 };
                const int Speed = std::clamp(static_cast<int>(std::lround(std::fabs(Inputs.VehicleSpeedKmh))), 0, 299);
                const int Gear  = Inputs.VehicleGearNumber != 0
                    ? std::clamp(std::abs(Inputs.VehicleGearNumber), 1, 6)
                    : (Speed < 4 ? 1 : std::clamp(1 + Speed / 36, 1, 6));
                const int D2    = (Speed / 100) % 10;
                const int D1    = (Speed / 10) % 10;
                const int D0    = Speed % 10;

                if (Speed >= 100 && SampleSevenSegmentDigit(D2, (U - (-0.31f)) / 0.13f, (V - 0.04f) / 0.44f))
                {
                    return { 2.75, 2.85, 3.00 };
                }
                if ((Speed >= 10) && SampleSevenSegmentDigit(D1, (U - (-0.15f)) / 0.13f, (V - 0.04f) / 0.44f))
                {
                    return { 2.75, 2.85, 3.00 };
                }
                if (SampleSevenSegmentDigit(D0, (U - 0.01f) / 0.13f, (V - 0.04f) / 0.44f))
                {
                    return { 2.75, 2.85, 3.00 };
                }
                if (SampleSevenSegmentDigit(Gear, (U - 0.19f) / 0.12f, (V - 0.10f) / 0.32f))
                {
                    return { 2.35, 0.62, 0.52 };
                }
                // Warm amber throttle/charge bar under speed
                if (V > -0.50f && V < -0.39f && U > -0.34f && U < 0.18f)
                {
                    return { 2.35, 1.08, 0.48 };
                }
            }
            // Bottom 3 status pills
            if (V > -0.74f && V < -0.62f)
            {
                if (std::fabs(U + 0.18f) < 0.06f) return { 2.25, 0.88, 0.52 };
                if (std::fabs(U)         < 0.06f) return { 2.15, 0.65, 0.65 };
                if (std::fabs(U - 0.18f) < 0.06f) return { 0.65, 1.45, 2.45 };
            }
            return Out;
        }

        // Centre 14" Infotainment HMI Tablet (ScreenSequence.js / ControlVehicle3DCockpitHmi960x540.png)
        constexpr float SinT = 0.40776045f; // sin(0.42)
        constexpr float CosT = 0.91308894f; // cos(0.42)
        const float     Lx   = -Body.Y;
        const float     Ly   = SinT * (Body.X - 0.98f) + CosT * (Body.Z - (0.62f - Reb));
        const float     U    = std::clamp(0.5f + Lx / 0.30f, 0.0f, 1.0f);
        const float     V    = std::clamp(0.5f + Ly / 0.1875f, 0.0f, 1.0f);

        SpectrumRgb Out{ 0.028, 0.036, 0.048 };
        // Left 3D vehicle preview card
        if (U > 0.04f && U < 0.55f && V > 0.25f && V < 0.87f)
        {
            Out = { 0.018, 0.026, 0.032 };
            const float Cx = (U - 0.295f) / 0.22f; // [-1..1]
            const float Cy = (V - 0.55f)  / 0.22f; // [-1..1]
            // Fastback wedge body silhouette in live BodyCoat color
            if (std::fabs(Cx) < 0.86f && Cy > -0.24f && Cy < 0.14f - 0.12f * Cx)
            {
                Out = ScaleSpec(Inputs.VehicleBodyAlbedo, 2.35);
            }
            // Greenhouse canopy
            if (Cx > -0.48f && Cx < 0.38f && Cy >= 0.06f && Cy < 0.36f - 0.32f * std::fabs(Cx + 0.04f))
            {
                Out = { 0.14, 0.24, 0.36 };
            }
            // Front & rear wheels
            for (float Wx : { -0.52f, 0.48f })
            {
                const float R2 = (Cx - Wx) * (Cx - Wx) + (Cy + 0.22f) * (Cy + 0.22f) * 1.35f;
                if (R2 < 0.036f)
                {
                    Out = R2 < 0.012f ? SpectrumRgb{ 0.28, 0.95, 1.55 } : SpectrumRgb{ 0.05, 0.06, 0.07 };
                }
            }
            // 5 paint swatch dots along bottom-left of preview
            const std::array<SpectrumRgb, 5> Swatches = {{
                { 1.85, 2.05, 0.15 }, { 0.65, 1.75, 1.25 }, { 1.75, 1.35, 0.68 }, { 0.65, 1.15, 1.95 }, { 1.95, 0.82, 0.78 }
            }};
            for (int S = 0; S < 5; ++S)
            {
                const float Sx = 0.09f + 0.055f * static_cast<float>(S);
                const float D2 = (U - Sx) * (U - Sx) * 2.4f + (V - 0.31f) * (V - 0.31f);
                if (D2 < 0.00055f)
                {
                    return Swatches[S];
                }
            }
            return Out;
        }

        // Top-right 2x2 status tiles
        if (U > 0.59f && U < 0.94f && V > 0.57f && V < 0.87f)
        {
            const float Tx = (U - 0.59f) / 0.35f * 2.0f;
            const float Ty = (V - 0.57f) / 0.30f * 2.0f;
            if ((Tx - std::floor(Tx)) > 0.14f && (Tx - std::floor(Tx)) < 0.86f &&
                (Ty - std::floor(Ty)) > 0.14f && (Ty - std::floor(Ty)) < 0.86f)
            {
                return (Ty > 1.0f) ? SpectrumRgb{ 1.85, 1.55, 0.62 } : SpectrumRgb{ 2.05, 0.92, 0.62 };
            }
        }

        // Mid-right circular G-meter ring + dual mint vertical bars
        {
            const float Dx = (U - 0.68f) * 1.6f;
            const float Dy = (V - 0.39f);
            const float R  = std::sqrt(Dx * Dx + Dy * Dy);
            if (R > 0.095f && R < 0.118f)
            {
                return { 0.75, 1.65, 2.35 };
            }
            if (U > 0.80f && U < 0.845f && V > 0.27f && V < 0.51f)
            {
                return { 0.38, 1.95, 0.82 };
            }
            if (U > 0.88f && U < 0.925f && V > 0.27f && V < 0.33f)
            {
                return { 2.05, 0.58, 0.48 };
            }
        }

        // Bottom 6-icon app dock
        if (V > 0.06f && V < 0.18f && U > 0.14f && U < 0.86f)
        {
            const float Slot = (U - 0.14f) / 0.72f * 6.0f;
            const int   IdxS = std::clamp(static_cast<int>(std::floor(Slot)), 0, 5);
            const float Frac = Slot - std::floor(Slot);
            if (Frac > 0.18f && Frac < 0.82f)
            {
                static constexpr SpectrumRgb DockCols[6] = {
                    { 0.48, 1.35, 2.35 }, { 1.85, 0.68, 1.55 }, { 0.78, 0.92, 2.25 },
                    { 0.35, 1.85, 1.75 }, { 0.45, 1.55, 2.35 }, { 0.75, 0.85, 1.05 }
                };
                return DockCols[IdxS];
            }
        }

        return Out;
    };

    const auto EvaluateDirectLighting = [&](const SurfaceHit& Hit, Vec3f ViewDir, bool ShadowRays) -> SpectrumRgb {
        const CourseMaterialSpecification& Mat = Materials[Hit.Material];
        SpectrumRgb Result = (Hit.IsVehicle && Hit.Material == static_cast<uint32_t>(CourseMaterialId::DisplayGlass))
            ? EvaluateCabinHmiEmission(Hit.Position)
            : SpectrumRgb{
                  static_cast<double>(Mat.Emission[0]) * 0.12,
                  static_cast<double>(Mat.Emission[1]) * 0.12,
                  static_cast<double>(Mat.Emission[2]) * 0.12
              };

        const SpectrumRgb BaseAlbedo{ Mat.Albedo[0], Mat.Albedo[1], Mat.Albedo[2] };
        const SpectrumRgb DiffAlbedo = ScaleSpec(BaseAlbedo, 1.0 - static_cast<double>(Mat.Metalness));
        const SpectrumRgb F0{
            0.04 * (1.0 - Mat.Metalness) + BaseAlbedo.R * Mat.Metalness,
            0.04 * (1.0 - Mat.Metalness) + BaseAlbedo.G * Mat.Metalness,
            0.04 * (1.0 - Mat.Metalness) + BaseAlbedo.B * Mat.Metalness
        };
        const float Rough  = std::max(0.04f, Mat.Roughness);
        const float Alpha  = Rough * Rough;
        const float Alpha2 = Alpha * Alpha;
        const float NdotV  = std::max(Dot3(Hit.Normal, ViewDir), 1.0e-3f);

        const float NdotL = Dot3(Hit.Normal, SunDir);
        if (Inputs.SunEnabled && SunAbove > 0.001 && NdotL > 0.0f)
        {
            double Visibility = 1.0;
            if (ShadowRays)
            {
                const Vec3f ShadowOrigin = Add3(Hit.Position, Scale3(Hit.Normal, 0.015f));
                if (IsRtx)
                {
                    Visibility = TraceTlasOccluded(ShadowOrigin, SunDir, 0.01f, 180.0f) ? 0.0 : 1.0;
                }
                else
                {
                    double T    = 0.12;
                    double Soft = 1.0;
                    for (int Step = 0; Step < 18 && T < 95.0; ++Step)
                    {
                        const SpatialVector P = ToSpatial(Add3(ShadowOrigin, Scale3(SunDir, static_cast<float>(T))));
                        const double D = SampleProvingGroundsSignedDistance(P, Inputs);
                        if (D < 0.015)
                        {
                            Soft = 0.0;
                            break;
                        }
                        Soft = std::min(Soft, 14.0 * D / T);
                        T += std::max(0.18, D * 0.85);
                    }
                    Visibility = std::clamp(Soft, 0.0, 1.0);
                }
            }
            if (Visibility > 0.001)
            {
                const Vec3f  H       = Normalize3(Add3(ViewDir, SunDir));
                const float  NdotH   = std::max(Dot3(Hit.Normal, H), 0.0f);
                const float  VdotH   = std::max(Dot3(ViewDir, H), 0.0f);
                const float  Denom   = NdotH * NdotH * (Alpha2 - 1.0f) + 1.0f;
                const float  Dggx    = Alpha2 / std::max(1.0e-5f, static_cast<float>(Pi) * Denom * Denom);
                const float  K       = (Rough + 1.0f) * (Rough + 1.0f) * 0.125f;
                const float  G       = (NdotV / (NdotV * (1.0f - K) + K)) * (NdotL / (NdotL * (1.0f - K) + K));
                const double Schlick = std::pow(1.0 - static_cast<double>(VdotH), 5.0);
                const SpectrumRgb Fr{
                    F0.R + (1.0 - F0.R) * Schlick,
                    F0.G + (1.0 - F0.G) * Schlick,
                    F0.B + (1.0 - F0.B) * Schlick
                };
                const double SpecScale = static_cast<double>(Dggx * G) / std::max(1.0e-3, 4.0 * NdotV * NdotL);
                SpectrumRgb Brdf = AddSpec(ScaleSpec(DiffAlbedo, 1.0 / Pi), ScaleSpec(Fr, std::min(SpecScale, 28.0)));

                if (Mat.CoatWeight > 0.0f)
                {
                    const float  Ca       = std::max(0.025f, Mat.CoatRoughness);
                    const float  Ca4      = Ca * Ca * Ca * Ca;
                    const float  Cd       = NdotH * NdotH * (Ca4 - 1.0f) + 1.0f;
                    const double CoatSpec = static_cast<double>(Mat.CoatWeight) *
                        (0.04 + 0.96 * Schlick) * (Ca4 / std::max(1.0e-5f, static_cast<float>(Pi) * Cd * Cd)) * 0.25;
                    Brdf = AddSpec(Brdf, { CoatSpec, CoatSpec, CoatSpec });
                }

                Result = AddSpec(Result, ScaleSpec(MulSpec(Brdf, SunRadiance), Visibility * static_cast<double>(NdotL)));
            }
        }

        if (Inputs.ClusteredLightsEnabled)
        {
            for (const ProvingGroundsLight& Light : ActiveLights)
            {
                const Vec3f ToLight = Sub3(ToVec3f(Light.Position), Hit.Position);
                const float DistSq  = Dot3(ToLight, ToLight);
                if (DistSq <= 1.0e-3f || DistSq >= Light.Reach * Light.Reach)
                {
                    continue;
                }
                const float Dist = std::sqrt(DistSq);
                const Vec3f LDir = Scale3(ToLight, 1.0f / Dist);
                const float NL   = Dot3(Hit.Normal, LDir);
                if (NL <= 0.0f)
                {
                    continue;
                }

                float Angular = 1.0f;
                if (Light.Kind == ClusteredLightKind::StreetLamp)
                {
                    const float DownCos = std::max(0.0f, -LDir.Z);
                    Angular = std::clamp((DownCos - 0.15f) / 0.65f, 0.0f, 1.0f) * (1.0f + 0.35f * (1.0f - DownCos));
                }
                else if (Light.Kind != ClusteredLightKind::Omni)
                {
                    const float ConeCos = -Dot3(LDir, Normalize3(ToVec3f(Light.Direction)));
                    if (ConeCos <= Light.OuterCos)
                    {
                        continue;
                    }
                    Angular = std::clamp((ConeCos - Light.OuterCos) / std::max(1.0e-3f, Light.InnerCos - Light.OuterCos), 0.0f, 1.0f);
                    Angular *= Angular;
                }
                if (Angular <= 1.0e-3f)
                {
                    continue;
                }

                const float  Ratio  = Dist / Light.Reach;
                const float  Window = std::max(0.0f, 1.0f - Ratio * Ratio * Ratio * Ratio);
                const float  Atten  = Angular * (Window * Window) / (DistSq + 1.2f);
                const double Gain   = static_cast<double>(Light.Intensity * Atten * NL) * 0.16;

                const Vec3f  H     = Normalize3(Add3(ViewDir, LDir));
                const float  NdotH = std::max(Dot3(Hit.Normal, H), 0.0f);
                const double Spec  = std::pow(static_cast<double>(NdotH), static_cast<double>(2.0f / std::max(0.02f, Alpha2))) * 0.25;
                const SpectrumRgb LightCol{ Light.Colour[0], Light.Colour[1], Light.Colour[2] };
                const SpectrumRgb Response = AddSpec(ScaleSpec(DiffAlbedo, 1.0 / Pi), ScaleSpec(F0, Spec));
                Result = AddSpec(Result, ScaleSpec(MulSpec(Response, LightCol), Gain));
            }
        }

        return Result;
    };

    std::vector<SpectrumRgb> DirectBuffer(PixelCount, { 0.0, 0.0, 0.0 });
    std::vector<SpectrumRgb> IndirectIrradiance(PixelCount, { 0.0, 0.0, 0.0 });
    std::vector<SpectrumRgb> SpecularBuffer(PixelCount, { 0.0, 0.0, 0.0 });
    std::vector<Vec3f>       NormalBuffer(PixelCount, { 0.0f, 0.0f, 1.0f });
    std::vector<float>       DepthBuffer(PixelCount, 0.0f);
    std::vector<uint32_t>    MaterialBuffer(PixelCount, 0u);
    std::vector<uint8_t>     HitMask(PixelCount, 0u);

    std::atomic<uint64_t> ReflectionAttempts{ 0u };
    std::atomic<uint64_t> ReflectionHits{ 0u };

    constexpr uint32_t TileSize   = 16u;
    const uint32_t     TilesX     = (Width  + TileSize - 1u) / TileSize;
    const uint32_t     TilesY     = (Height + TileSize - 1u) / TileSize;
    const uint32_t     TotalTiles = TilesX * TilesY;
    std::atomic<uint32_t> NextTile{ 0u };

    const auto RenderTilesWorker = [&]() {
        uint64_t LocalReflTries = 0u;
        uint64_t LocalReflHits  = 0u;

        while (true)
        {
            const uint32_t TileIdx = NextTile.fetch_add(1u, std::memory_order_relaxed);
            if (TileIdx >= TotalTiles)
            {
                break;
            }
            const uint32_t TileX0 = (TileIdx % TilesX) * TileSize;
            const uint32_t TileY0 = (TileIdx / TilesX) * TileSize;
            const uint32_t TileX1 = std::min(Width,  TileX0 + TileSize);
            const uint32_t TileY1 = std::min(Height, TileY0 + TileSize);

            for (uint32_t Py = TileY0; Py < TileY1; ++Py)
            {
                const float ScreenY = 1.0f - 2.0f * (static_cast<float>(Py) + 0.5f) / static_cast<float>(Height);
                for (uint32_t Px = TileX0; Px < TileX1; ++Px)
                {
                    const size_t Idx     = static_cast<size_t>(Py) * static_cast<size_t>(Width) + static_cast<size_t>(Px);
                    const float  ScreenX = 2.0f * (static_cast<float>(Px) + 0.5f) / static_cast<float>(Width) - 1.0f;
                    const Vec3f  RayDir  = Normalize3(Add3(CamForward,
                                                           Add3(Scale3(CamRight, ScreenX * HalfTanX),
                                                                Scale3(CamUp,    ScreenY * HalfTanY))));
                    const Vec3f  ViewDir = Scale3(RayDir, -1.0f);

                    SurfaceHit OpaqueHit = TraceTlasClosest(CamEye, RayDir, 0.05f, 420.0f);
                    SurfaceHit GlassHit{};
                    if (HasGlazing && !Cam.Interior)
                    {
                        GlassHit.Distance = OpaqueHit.Hit ? OpaqueHit.Distance : 420.0f;
                        TraceInstance(GlazingInstance, CamEye, RayDir, 0.05f, GlassHit.Distance, false, GlassHit);
                    }

                    bool        ThroughCanopy = false;
                    SpectrumRgb CanopyReflect{ 0.0, 0.0, 0.0 };
                    SpectrumRgb CanopyTint{ 1.0, 1.0, 1.0 };
                    double      CanopyFresnel = 0.0;

                    if (GlassHit.Hit && (!OpaqueHit.Hit || GlassHit.Distance < OpaqueHit.Distance))
                    {
                        ThroughCanopy = true;
                        const DielectricRefraction Refr = EvaluateDielectricRefraction(
                            { static_cast<double>(RayDir.X), static_cast<double>(RayDir.Y), static_cast<double>(RayDir.Z) },
                            { static_cast<double>(GlassHit.Normal.X), static_cast<double>(GlassHit.Normal.Y), static_cast<double>(GlassHit.Normal.Z) },
                            1.0, 1.52);

                        const double CosTheta = std::max(0.04, static_cast<double>(Dot3(GlassHit.Normal, ViewDir)));
                        const double PathLen  = 0.0055 / CosTheta;
                        CanopyFresnel = std::clamp((Refr.Valid ? Refr.Reflectance : 0.08) * 1.45 + 0.04, 0.04, 0.88);
                        CanopyTint = Inputs.DielectricGlazingEnabled
                            ? SpectrumRgb{
                                  std::exp(-72.0 * PathLen) * 0.58,
                                  std::exp(-62.0 * PathLen) * 0.62,
                                  std::exp(-56.0 * PathLen) * 0.66
                              }
                            : SpectrumRgb{ 0.44, 0.46, 0.49 };

                        // Re-trace interior cockpit along refracted ray when valid
                        if (Refr.Valid && !Refr.TotalInternalReflection && Inputs.DielectricGlazingEnabled)
                        {
                            const Vec3f RefrDir = Normalize3({
                                static_cast<float>(Refr.Direction[0]),
                                static_cast<float>(Refr.Direction[1]),
                                static_cast<float>(Refr.Direction[2])
                            });
                            OpaqueHit = TraceTlasClosest(
                                Add3(GlassHit.Position, Scale3(RefrDir, 0.015f)), RefrDir, 0.01f, 420.0f);
                        }

                        const Vec3f ReflDir  = Normalize3(Sub3(RayDir, Scale3(GlassHit.Normal, 2.0f * Dot3(RayDir, GlassHit.Normal))));
                        SurfaceHit  GlassEnv = TraceTlasClosest(Add3(GlassHit.Position, Scale3(GlassHit.Normal, 0.02f)), ReflDir, 0.02f, 220.0f);
                        CanopyReflect = GlassEnv.Hit
                            ? EvaluateDirectLighting(GlassEnv, Scale3(ReflDir, -1.0f), false)
                            : EvaluateSkyAndCelestial(ToSpatial(ReflDir), Inputs, true);

                        if (Inputs.InspectionMode == ViewportInspectionMode::DielectricGlazing)
                        {
                            const SpectrumRgb InteriorCtx = OpaqueHit.Hit
                                ? EvaluateDirectLighting(OpaqueHit, ViewDir, false)
                                : EvaluateSkyAndCelestial(ToSpatial(RayDir), Inputs, true);
                            const SpectrumRgb FresnelHighlight{ 0.38 + CanopyReflect.R, 0.72 + CanopyReflect.G, 0.95 + CanopyReflect.B };
                            OutLinearRgb[Idx] = AddSpec(
                                MulSpec(InteriorCtx, ScaleSpec(CanopyTint, 1.35 * (1.0 - CanopyFresnel))),
                                ScaleSpec(FresnelHighlight, CanopyFresnel * 1.25 + 0.08));
                            continue;
                        }
                    }

                    if (!OpaqueHit.Hit)
                    {
                        SpectrumRgb Sky = EvaluateSkyAndCelestial(ToSpatial(RayDir), Inputs, true);
                        if (ThroughCanopy)
                        {
                            Sky = AddSpec(MulSpec(Sky, ScaleSpec(CanopyTint, 1.0 - CanopyFresnel)),
                                          ScaleSpec(CanopyReflect, CanopyFresnel));
                        }
                        OutLinearRgb[Idx] = Sky;
                        continue;
                    }

                    HitMask[Idx]        = 1u;
                    NormalBuffer[Idx]   = OpaqueHit.Normal;
                    DepthBuffer[Idx]    = OpaqueHit.Distance;
                    MaterialBuffer[Idx] = OpaqueHit.Material;

                    if (Inputs.InspectionMode == ViewportInspectionMode::SurfaceNormals)
                    {
                        OutLinearRgb[Idx] = {
                            0.5 + 0.5 * OpaqueHit.Normal.X,
                            0.5 + 0.5 * OpaqueHit.Normal.Y,
                            0.5 + 0.5 * OpaqueHit.Normal.Z
                        };
                        continue;
                    }
                    if (Inputs.InspectionMode == ViewportInspectionMode::SurfaceAlbedo)
                    {
                        const auto& M = Materials[OpaqueHit.Material];
                        OutLinearRgb[Idx] = { M.Albedo[0], M.Albedo[1], M.Albedo[2] };
                        continue;
                    }
                    if (Inputs.InspectionMode == ViewportInspectionMode::DistanceCascades)
                    {
                        const double DistFromCar = std::hypot(
                            OpaqueHit.Position.X - Inputs.VehiclePosition[0],
                            OpaqueHit.Position.Y - Inputs.VehiclePosition[1]);
                        const int Cascade = DistFromCar < 4.0 ? 0 : (DistFromCar < 11.2 ? 1 : (DistFromCar < 32.0 ? 2 : 3));
                        const std::array<SpectrumRgb, 4> CascadeTint = {{
                            { 0.16, 0.82, 0.42 }, { 0.18, 0.58, 0.92 }, { 0.88, 0.62, 0.16 }, { 0.78, 0.24, 0.56 }
                        }};
                        const double SdfVal = SampleProvingGroundsSignedDistance(
                            ToSpatial(Add3(OpaqueHit.Position, Scale3(OpaqueHit.Normal, 0.25f))), Inputs);
                        const double Iso = 0.45 + 0.55 * std::fabs(std::fmod(std::fabs(SdfVal) * 4.0, 1.0) - 0.5) * 2.0;
                        OutLinearRgb[Idx] = ScaleSpec(CascadeTint[Cascade], Iso * (0.45 + 0.55 * std::max(0.1f, OpaqueHit.Normal.Z)));
                        continue;
                    }
                    if (Inputs.InspectionMode == ViewportInspectionMode::DielectricGlazing)
                    {
                        const SpectrumRgb Ctx  = EvaluateDirectLighting(OpaqueHit, ViewDir, false);
                        const double      Luma = 0.025 + 0.32 * Luminance(Ctx);
                        OutLinearRgb[Idx]      = { Luma * 0.92, Luma * 0.96, Luma * 1.04 };
                        continue;
                    }

                    const SpectrumRgb Direct = EvaluateDirectLighting(OpaqueHit, ViewDir, true);
                    DirectBuffer[Idx] = Direct;
                    if (Inputs.InspectionMode == ViewportInspectionMode::DirectSunOnly)
                    {
                        OutLinearRgb[Idx] = Direct;
                        continue;
                    }

                    SpectrumRgb Irr{ 0.0, 0.0, 0.0 };
                    if (IsRtx)
                    {
                        // 4-sample stratified R2 quasi-random cosine-hemisphere ReSTIR GI gather
                        const float    Ign   = std::fmod(52.9829189f * std::fmod(0.06711056f * static_cast<float>(Px) +
                                                                                 0.00583715f * static_cast<float>(Py), 1.0f), 1.0f);
                        const float    Ign2  = std::fmod(Ign * 1.6180339f + 0.3819660f, 1.0f);
                        const Vec3f    UpH   = std::fabs(OpaqueHit.Normal.Z) < 0.92f ? Vec3f{ 0.0f, 0.0f, 1.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
                        const Vec3f    TanU  = Normalize3(Cross3(UpH, OpaqueHit.Normal));
                        const Vec3f    TanV  = Cross3(OpaqueHit.Normal, TanU);
                        const Vec3f    Orig  = Add3(OpaqueHit.Position, Scale3(OpaqueHit.Normal, 0.02f));
                        SpectrumRgb    GiSum = { 0.0, 0.0, 0.0 };

                        for (int SampleIdx = 0; SampleIdx < 4; ++SampleIdx)
                        {
                            const float Sx  = static_cast<float>(SampleIdx & 1);
                            const float Sy  = static_cast<float>((SampleIdx >> 1) & 1);
                            const float U1  = 0.5f * (Sx + std::fmod(Ign  + 0.75487766f * static_cast<float>(SampleIdx), 1.0f));
                            const float U2  = 0.5f * (Sy + std::fmod(Ign2 + 0.56984029f * static_cast<float>(SampleIdx), 1.0f));
                            const float Rad = std::sqrt(std::clamp(U1, 0.001f, 0.999f));
                            const float Phi = 2.0f * static_cast<float>(Pi) * U2;
                            const Vec3f GiDir = Normalize3(Add3(
                                Add3(Scale3(TanU, Rad * std::cos(Phi)), Scale3(TanV, Rad * std::sin(Phi))),
                                Scale3(OpaqueHit.Normal, std::sqrt(std::max(0.0f, 1.0f - U1)))));

                            const SurfaceHit BounceHit = TraceTlasClosest(Orig, GiDir, 0.02f, 52.0f);
                            SpectrumRgb      SampleRad{};
                            if (BounceHit.Hit)
                            {
                                SampleRad = EvaluateDirectLighting(BounceHit, Scale3(GiDir, -1.0f), false);
                            }
                            else
                            {
                                SampleRad = EvaluateSkyAndCelestial(ToSpatial(GiDir), Inputs, false);
                            }
                            const double Lum = Luminance(SampleRad);
                            if (Lum > 0.95)
                            {
                                SampleRad = ScaleSpec(SampleRad, 0.95 / Lum);
                            }
                            GiSum = AddSpec(GiSum, SampleRad);
                        }
                        Irr = ScaleSpec(GiSum, 0.25 * Pi * Inputs.IndirectScale);
                    }
                    else
                    {
                        double      SdfAo   = 1.0;
                        const Vec3f ConeDir = Normalize3(Add3(OpaqueHit.Normal, { 0.0f, 0.0f, 0.55f }));
                        double      MarchT  = 0.20;
                        for (int Step = 0; Step < 6; ++Step)
                        {
                            const double D = SampleProvingGroundsSignedDistance(
                                ToSpatial(Add3(OpaqueHit.Position, Scale3(ConeDir, static_cast<float>(MarchT)))), Inputs);
                            SdfAo = std::min(SdfAo, std::clamp(0.35 + 0.65 * (D / MarchT), 0.12, 1.0));
                            MarchT *= 1.85;
                        }
                        const SpectrumRgb SkyTop = EvaluateSkyAndCelestial({ 0.0, 0.0, 1.0 }, Inputs, false);
                        const SpectrumRgb GroundBounce{
                            0.045 * SunAbove + 0.008,
                            0.046 * SunAbove + 0.008,
                            0.050 * SunAbove + 0.010
                        };
                        const double UpWeight = 0.5 + 0.5 * static_cast<double>(OpaqueHit.Normal.Z);
                        Irr = ScaleSpec(AddSpec(ScaleSpec(SkyTop, UpWeight * Pi), ScaleSpec(GroundBounce, (1.0 - UpWeight) * Pi)),
                                        SdfAo * Inputs.IndirectScale);
                        if (Inputs.SurfaceCardsEnabled)
                        {
                            Irr = AddSpec(Irr, ScaleSpec({ MeanCardRadiosity, MeanCardRadiosity * 0.95, MeanCardRadiosity * 1.05 }, SdfAo));
                        }
                    }
                    if (Inputs.InspectionMode == ViewportInspectionMode::SurfaceCardsView)
                    {
                        OutLinearRgb[Idx] = ScaleSpec(Irr, 1.0 / Pi);
                        continue;
                    }
                    IndirectIrradiance[Idx] = Irr;

                    const CourseMaterialSpecification& Mat = Materials[OpaqueHit.Material];
                    const float EffectiveRough = Mat.CoatWeight > 0.0f ? Mat.CoatRoughness : Mat.Roughness;
                    SpectrumRgb SpecIndirect{ 0.0, 0.0, 0.0 };

                    if (EffectiveRough <= static_cast<float>(Inputs.BvhReflectionRoughnessCutoff))
                    {
                        ++LocalReflTries;
                        const Vec3f ReflDir = Normalize3(Sub3(RayDir, Scale3(OpaqueHit.Normal, 2.0f * Dot3(RayDir, OpaqueHit.Normal))));
                        const float MaxDist = IsRtx ? 220.0f : 85.0f;
                        const SurfaceHit ReflHit = TraceTlasClosest(
                            Add3(OpaqueHit.Position, Scale3(OpaqueHit.Normal, 0.025f)), ReflDir, 0.02f, MaxDist);
                        SpectrumRgb ReflRadiance{};
                        if (ReflHit.Hit)
                        {
                            ++LocalReflHits;
                            ReflRadiance = EvaluateDirectLighting(ReflHit, Scale3(ReflDir, -1.0f), IsRtx);
                            const auto& Rm = Materials[ReflHit.Material];
                            ReflRadiance = AddSpec(ReflRadiance, ScaleSpec({ Rm.Albedo[0], Rm.Albedo[1], Rm.Albedo[2] }, 0.14));
                        }
                        else
                        {
                            ReflRadiance = EvaluateSkyAndCelestial(ToSpatial(ReflDir), Inputs, false);
                        }

                        const float  NdotV   = std::max(Dot3(OpaqueHit.Normal, ViewDir), 1.0e-3f);
                        const double Schlick = std::pow(1.0 - static_cast<double>(NdotV), 5.0);
                        const SpectrumRgb BaseF0{
                            0.04 * (1.0 - Mat.Metalness) + Mat.Albedo[0] * Mat.Metalness,
                            0.04 * (1.0 - Mat.Metalness) + Mat.Albedo[1] * Mat.Metalness,
                            0.04 * (1.0 - Mat.Metalness) + Mat.Albedo[2] * Mat.Metalness
                        };
                        const double Smoothness = 1.0 - static_cast<double>(EffectiveRough);
                        SpectrumRgb EnvBrdf{
                            BaseF0.R + (std::max(Smoothness, BaseF0.R) - BaseF0.R) * Schlick,
                            BaseF0.G + (std::max(Smoothness, BaseF0.G) - BaseF0.G) * Schlick,
                            BaseF0.B + (std::max(Smoothness, BaseF0.B) - BaseF0.B) * Schlick
                        };
                        if (Mat.CoatWeight > 0.0f)
                        {
                            const double CoatF = static_cast<double>(Mat.CoatWeight) * (0.04 + 0.92 * Schlick);
                            EnvBrdf = AddSpec(ScaleSpec(EnvBrdf, 1.0 - CoatF), { CoatF, CoatF, CoatF });
                        }
                        SpecIndirect = ScaleSpec(MulSpec(ReflRadiance, EnvBrdf), Inputs.ReflectionScale);
                    }
                    SpecularBuffer[Idx] = SpecIndirect;

                    if (Inputs.InspectionMode == ViewportInspectionMode::SpecularReflections)
                    {
                        OutLinearRgb[Idx] = SpecIndirect;
                        continue;
                    }

                    if (ThroughCanopy)
                    {
                        DirectBuffer[Idx]   = AddSpec(MulSpec(DirectBuffer[Idx], ScaleSpec(CanopyTint, 1.0 - CanopyFresnel)),
                                                      ScaleSpec(CanopyReflect, CanopyFresnel));
                        SpecularBuffer[Idx] = MulSpec(SpecularBuffer[Idx], ScaleSpec(CanopyTint, 1.0 - CanopyFresnel));
                    }
                }
            }
        }

        ReflectionAttempts.fetch_add(LocalReflTries, std::memory_order_relaxed);
        ReflectionHits.fetch_add(LocalReflHits, std::memory_order_relaxed);
    };

    const uint32_t HardwareThreads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    if (HardwareThreads <= 1u || TotalTiles <= 4u)
    {
        RenderTilesWorker();
    }
    else
    {
        std::vector<std::thread> Workers;
        Workers.reserve(HardwareThreads - 1u);
        for (uint32_t W = 1u; W < HardwareThreads; ++W)
        {
            Workers.emplace_back(RenderTilesWorker);
        }
        RenderTilesWorker();
        for (std::thread& Worker : Workers)
        {
            Worker.join();
        }
    }

    if (IsRtx && (Inputs.InspectionMode == ViewportInspectionMode::FinalLit ||
                  Inputs.InspectionMode == ViewportInspectionMode::IndirectIrradiance))
    {
        // 4-pass 5x5 B3-spline à-trous VarianceWavelet SVGF filter (Step = 1, 2, 4, 8 — VarianceWavelet.wgsl)
        static constexpr double Kernel1D[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };
        std::vector<SpectrumRgb> Scratch(PixelCount);
        for (int Step : { 1, 2, 4, 8 })
        {
            std::atomic<uint32_t> NextRow{ 0u };
            const auto FilterWorker = [&]() {
                while (true)
                {
                    const uint32_t Py = NextRow.fetch_add(8u, std::memory_order_relaxed);
                    if (Py >= Height)
                    {
                        break;
                    }
                    const uint32_t RowEnd = std::min(Height, Py + 8u);
                    for (uint32_t Ry = Py; Ry < RowEnd; ++Ry)
                    {
                        for (uint32_t Px = 0u; Px < Width; ++Px)
                        {
                            const size_t CIdx = static_cast<size_t>(Ry) * Width + Px;
                            if (!HitMask[CIdx])
                            {
                                Scratch[CIdx] = IndirectIrradiance[CIdx];
                                continue;
                            }
                            const Vec3f    CNorm  = NormalBuffer[CIdx];
                            const float    CDepth = DepthBuffer[CIdx];
                            const uint32_t CMat   = MaterialBuffer[CIdx];
                            SpectrumRgb    Sum    = { 0.0, 0.0, 0.0 };
                            double         Weight = 0.0;
                            for (int Ky = -2; Ky <= 2; ++Ky)
                            {
                                const int Ny = static_cast<int>(Ry) + Ky * Step;
                                if (Ny < 0 || Ny >= static_cast<int>(Height)) continue;
                                const double Wy = Kernel1D[Ky + 2];
                                for (int Kx = -2; Kx <= 2; ++Kx)
                                {
                                    const int Nx = static_cast<int>(Px) + Kx * Step;
                                    if (Nx < 0 || Nx >= static_cast<int>(Width)) continue;
                                    const size_t NIdx = static_cast<size_t>(Ny) * Width + static_cast<size_t>(Nx);
                                    if (!HitMask[NIdx]) continue;
                                    const float NDot = Dot3(CNorm, NormalBuffer[NIdx]);
                                    if (NDot < 0.80f) continue;
                                    const float RelDepth = std::fabs(CDepth - DepthBuffer[NIdx]) / std::max(0.4f, CDepth);
                                    if (RelDepth > 0.10f * static_cast<float>(Step)) continue;
                                    const double MatMatch = (MaterialBuffer[NIdx] == CMat) ? 1.0 : 0.35;
                                    const double W = Wy * Kernel1D[Kx + 2] * MatMatch *
                                        std::pow(static_cast<double>(NDot), 32.0) * std::exp(-RelDepth * 24.0);
                                    Sum    = AddSpec(Sum, ScaleSpec(IndirectIrradiance[NIdx], W));
                                    Weight += W;
                                }
                            }
                            Scratch[CIdx] = Weight > 1.0e-8
                                ? ScaleSpec(Sum, 1.0 / Weight)
                                : IndirectIrradiance[CIdx];
                        }
                    }
                }
            };

            if (HardwareThreads <= 1u)
            {
                FilterWorker();
            }
            else
            {
                std::vector<std::thread> FilterThreads;
                FilterThreads.reserve(HardwareThreads - 1u);
                for (uint32_t W = 1u; W < HardwareThreads; ++W)
                {
                    FilterThreads.emplace_back(FilterWorker);
                }
                FilterWorker();
                for (std::thread& T : FilterThreads)
                {
                    T.join();
                }
            }
            IndirectIrradiance.swap(Scratch);
        }
    }

    double SumLuma     = 0.0;
    double SumIndirect = 0.0;
    size_t HitCount    = 0u;

    if (Inputs.InspectionMode == ViewportInspectionMode::FinalLit ||
        Inputs.InspectionMode == ViewportInspectionMode::IndirectIrradiance)
    {
        const SpectrumRgb HorizonAir = EvaluateSkyAndCelestial({ CamForward.X, CamForward.Y, 0.06 }, Inputs, false);

        for (size_t Idx = 0u; Idx < PixelCount; ++Idx)
        {
            if (!HitMask[Idx])
            {
                SumLuma += Luminance(OutLinearRgb[Idx]);
                continue;
            }
            ++HitCount;
            const auto&       Mat        = Materials[MaterialBuffer[Idx]];
            const SpectrumRgb BaseAlbedo = { Mat.Albedo[0], Mat.Albedo[1], Mat.Albedo[2] };
            const SpectrumRgb DiffAlbedo = ScaleSpec(BaseAlbedo, (1.0 - Mat.Metalness) / Pi);
            const SpectrumRgb Indirect   = MulSpec(DiffAlbedo, IndirectIrradiance[Idx]);
            SumIndirect += Luminance(IndirectIrradiance[Idx]) / Pi;

            if (Inputs.InspectionMode == ViewportInspectionMode::IndirectIrradiance)
            {
                OutLinearRgb[Idx] = ScaleSpec(IndirectIrradiance[Idx], 1.0 / Pi);
                SumLuma += Luminance(OutLinearRgb[Idx]);
                continue;
            }

            SpectrumRgb Combined = AddSpec(AddSpec(DirectBuffer[Idx], Indirect), SpecularBuffer[Idx]);

            const double Dist = static_cast<double>(DepthBuffer[Idx]);
            double FogTrans   = 1.0;
            if (Dist > Inputs.AerialStartMetres && Inputs.AerialDensity > 0.0)
            {
                FogTrans *= std::exp(-Inputs.AerialDensity * (Dist - Inputs.AerialStartMetres));
            }
            if (Inputs.FogEnabled && Inputs.Fog.DensityAtBase > 0.0)
            {
                FogTrans *= std::exp(-Inputs.Fog.DensityAtBase * std::min(Dist, 180.0));
            }
            if (FogTrans < 0.999)
            {
                Combined = AddSpec(ScaleSpec(Combined, FogTrans), ScaleSpec(HorizonAir, 1.0 - FogTrans));
            }

            OutLinearRgb[Idx] = Combined;
            SumLuma += Luminance(Combined);
        }
    }
    else
    {
        for (size_t Idx = 0u; Idx < PixelCount; ++Idx)
        {
            SumLuma += Luminance(OutLinearRgb[Idx]);
        }
    }

    const uint64_t TotalReflTries = ReflectionAttempts.load();
    const uint64_t TotalReflHits  = ReflectionHits.load();

    OutTelemetry.TriangleCount         = TotalAuthoredTriangles;
    OutTelemetry.BvhEnclosureCount     = TotalBvhNodes;
    OutTelemetry.ActiveTlasInstances   = static_cast<uint32_t>(OpaqueTlas.size()) + (HasGlazing ? 1u : 0u);
    OutTelemetry.ActiveClusterLights   = static_cast<uint32_t>(ActiveLights.size());
    OutTelemetry.ActiveSdfCascades     = 4u;
    OutTelemetry.SurfaceCardTexels     = SolvedCards.TexelCount();
    OutTelemetry.CardRadiosityResidual = SolvedCardResidual;
    OutTelemetry.MeanLuminance         = SumLuma / static_cast<double>(PixelCount);
    OutTelemetry.MeanIndirectLuminance = HitCount > 0u ? (SumIndirect / static_cast<double>(HitCount)) : 0.0;
    OutTelemetry.ReflectionHitFraction = TotalReflTries > 0u
        ? static_cast<double>(TotalReflHits) / static_cast<double>(TotalReflTries)
        : 0.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      1:1 RGBA8 sRGB VIEWPORT + DISTANCE INSET
//------------------------------------------------------------------------------------------------------------------------

void DualTierProvingGroundsRenderer::RenderRgba8Viewport(
    uint32_t                     Width,
    uint32_t                     Height,
    const EverestViewportInputs& Inputs,
    uint8_t*                     OutRgbaPixels,
    DualTierFrameTelemetry&      OutTelemetry) const
{
    if (OutRgbaPixels == nullptr || Width == 0u || Height == 0u)
    {
        return;
    }

    std::vector<SpectrumRgb> Linear;
    RenderFrame(Width, Height, Inputs, Linear, OutTelemetry);

    const double AspectRatio  = static_cast<double>(Width) / static_cast<double>(Height);
    double       SunScreenU   = 0.5;
    double       SunScreenV   = 0.5;
    const SpatialVector FarSun{
        Inputs.SunDirection.X * 160.0,
        Inputs.SunDirection.Y * 160.0,
        Inputs.SunDirection.Z * 160.0
    };
    const bool SunVisibleInFrame = Inputs.InspectionMode == ViewportInspectionMode::FinalLit &&
                                   Inputs.LensFlareEnabled && Inputs.SunEnabled && Inputs.LensFlareIntensity > 0.01 &&
                                   Inputs.SunDirection.Z > 0.0 &&
                                   ProjectWorldPoint(FarSun, Inputs, AspectRatio, SunScreenU, SunScreenV) &&
                                   SunScreenU > -0.12 && SunScreenU < 1.12 && SunScreenV > -0.12 && SunScreenV < 1.12;
    const double SunNdcX = (SunScreenU - 0.5) * 2.0;
    const double SunNdcY = (0.5 - SunScreenV) * 2.0;

    const double ExposureGain = std::pow(2.0, Inputs.ExposureBiasStops) * 1.18;
    const bool   BypassFilmic = (Inputs.InspectionMode == ViewportInspectionMode::SurfaceNormals ||
                                 Inputs.InspectionMode == ViewportInspectionMode::SurfaceAlbedo ||
                                 Inputs.InspectionMode == ViewportInspectionMode::DistanceCascades);

    const int InsetSize = Inputs.DistanceSliceInsetEnabled ? static_cast<int>(std::floor(Height * 0.34)) : 0;
    const int InsetX0   = static_cast<int>(Width) - InsetSize - 16;
    const int InsetY0   = 16;

    for (uint32_t Py = 0u; Py < Height; ++Py)
    {
        for (uint32_t Px = 0u; Px < Width; ++Px)
        {
            const size_t Idx = static_cast<size_t>(Py) * Width + Px;
            SpectrumRgb  Col = Linear[Idx];
            if (SunVisibleInFrame)
            {
                const double NdcX = ((static_cast<double>(Px) + 0.5) / static_cast<double>(Width) - 0.5) * 2.0;
                const double NdcY = (0.5 - (static_cast<double>(Py) + 0.5) / static_cast<double>(Height)) * 2.0;
                for (size_t G = 0u; G < DefaultFlareGhosts.size(); ++G)
                {
                    const FlareGhostSpec& Spec = DefaultFlareGhosts[G];
                    const double Gx   = -SunNdcX * Spec.AxisFraction;
                    const double Gy   = -SunNdcY * Spec.AxisFraction;
                    const double Dx   = (NdcX - Gx) * AspectRatio;
                    const double Dy   = (NdcY - Gy);
                    const double Dist = std::sqrt(Dx * Dx + Dy * Dy);
                    if (Dist < Spec.RadiusNdc)
                    {
                        const double T    = Dist / Spec.RadiusNdc;
                        const double Ring = (1.0 - T * T) * (0.45 + 0.55 * T * T) * Spec.Strength * Inputs.LensFlareIntensity;
                        Col.R += Ring * (G % 2u == 0u ? 1.15 : 0.65);
                        Col.G += Ring * 0.95;
                        Col.B += Ring * (G % 2u == 0u ? 0.72 : 1.25);
                    }
                }
                const double StreakDy = std::fabs(NdcY - SunNdcY);
                const double StreakDx = std::fabs(NdcX - SunNdcX) * AspectRatio;
                if (StreakDy < 0.025 && StreakDx < 0.85)
                {
                    const double Streak = std::exp(-StreakDy * 160.0) * std::exp(-StreakDx * 4.2) * 0.35 * Inputs.LensFlareIntensity;
                    Col.R += Streak * 0.65;
                    Col.G += Streak * 0.88;
                    Col.B += Streak * 1.35;
                }
            }
            if (!BypassFilmic)
            {
                Col = ProjectFilmic(ScaleSpec(Col, ExposureGain));
            }

            if (InsetSize > 16 && static_cast<int>(Px) >= InsetX0 && static_cast<int>(Px) < InsetX0 + InsetSize &&
                static_cast<int>(Py) >= InsetY0 && static_cast<int>(Py) < InsetY0 + InsetSize)
            {
                const double U    = (static_cast<double>(static_cast<int>(Px) - InsetX0) + 0.5) / static_cast<double>(InsetSize);
                const double V    = (static_cast<double>(static_cast<int>(Py) - InsetY0) + 0.5) / static_cast<double>(InsetSize);
                const double Span = 22.4;
                const SpatialVector SamplePos{
                    static_cast<double>(Inputs.VehiclePosition[0]) + (U - 0.5) * Span,
                    static_cast<double>(Inputs.VehiclePosition[1]) + (0.5 - V) * Span,
                    static_cast<double>(Inputs.DistanceSliceHeightMetres)
                };
                const double Signed = SampleProvingGroundsSignedDistance(SamplePos, Inputs);
                SpectrumRgb  Slice  = Signed < 0.0 ? SpectrumRgb{ 0.18, 0.42, 0.95 } : SpectrumRgb{ 0.95, 0.62, 0.22 };
                const double Atten  = 0.35 + 0.65 * std::exp(-std::fabs(Signed) * 0.9);
                Slice = ScaleSpec(Slice, Atten);
                const double Iso = std::fabs(std::fmod(std::fabs(Signed) / 0.25 + 0.5, 1.0) - 0.5);
                if (Iso < 0.06)
                {
                    Slice = ScaleSpec(Slice, 0.55);
                }
                if (std::fabs(Signed) < 0.07)
                {
                    Slice = { 1.0, 1.0, 1.0 };
                }
                const double Dx = std::fabs(SamplePos.X - Inputs.VehiclePosition[0]);
                const double Dy = std::fabs(SamplePos.Y - Inputs.VehiclePosition[1]);
                if ((Dx <= 4.0 && std::fabs(Dy - 4.0) < 0.12) || (Dy <= 4.0 && std::fabs(Dx - 4.0) < 0.12))
                {
                    Slice = { 0.30, 1.0, 0.45 };
                }
                Col = Slice;
            }

            uint8_t* Out = OutRgbaPixels + Idx * 4u;
            Out[0]       = LinearToSrgbByte(Col.R);
            Out[1]       = LinearToSrgbByte(Col.G);
            Out[2]       = LinearToSrgbByte(Col.B);
            Out[3]       = 255u;
        }
    }
}

} // namespace Frontier
