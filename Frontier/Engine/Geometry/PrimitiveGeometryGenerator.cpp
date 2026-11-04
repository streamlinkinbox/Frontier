/*==========================================================================================================================================
                                               PRIMITIVE GEOMETRY GENERATOR DEFINITION
==========================================================================================================================================*/
// 🧩 Binary FCT1 archive decoder, ControlVehicle multi-part assembly, and watertight analytic 3D primitive geometry builders.

#include "Engine/Geometry/PrimitiveGeometryGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace Frontier
{
namespace
{

constexpr double π    = 3.14159265358979323846;
constexpr double Twoπ = 6.28318530717958647692;

FacetPoint3D TransformLocalPoint(FacetPoint3D Local, const PrimitivePlacement& Placement) noexcept
{
    const double ScaledX = Local.X * Placement.UniformScale;
    const double ScaledY = Local.Y * Placement.UniformScale;
    const double ScaledZ = Local.Z * Placement.UniformScale;
    const double cosψ    = std::cos(Placement.YawRadians);
    const double sinψ    = std::sin(Placement.YawRadians);
    return {
        Placement.TranslationMeters.X + (ScaledX * cosψ + ScaledZ * sinψ),
        Placement.TranslationMeters.Y + ScaledY,
        Placement.TranslationMeters.Z + (-ScaledX * sinψ + ScaledZ * cosψ),
    };
}

void EmitTriangle(FacetField&           Out,
                  FacetPoint3D          P0,
                  FacetPoint3D          P1,
                  FacetPoint3D          P2,
                  std::array<double, 2> Uv0,
                  std::array<double, 2> Uv1,
                  std::array<double, 2> Uv2,
                  const PrimitivePlacement& Placement,
                  uint8_t               SharpBits = 0u)
{
    if (!Out.Corners.empty() && Out.CornerUVs.size() < Out.Corners.size())
    {
        Out.CornerUVs.resize(Out.Corners.size(), {0.0, 0.0});
    }
    Out.Corners.push_back(TransformLocalPoint(P0, Placement));
    Out.Corners.push_back(TransformLocalPoint(P1, Placement));
    Out.Corners.push_back(TransformLocalPoint(P2, Placement));
    Out.CornerUVs.push_back({Uv0[0] * Placement.UvScale, Uv0[1] * Placement.UvScale});
    Out.CornerUVs.push_back({Uv1[0] * Placement.UvScale, Uv1[1] * Placement.UvScale});
    Out.CornerUVs.push_back({Uv2[0] * Placement.UvScale, Uv2[1] * Placement.UvScale});
    Out.Classification.push_back(Placement.ClassificationId);
    Out.SharpMask.push_back(SharpBits);
}

void EmitQuad(FacetField&           Out,
              FacetPoint3D          P00,
              FacetPoint3D          P10,
              FacetPoint3D          P11,
              FacetPoint3D          P01,
              std::array<double, 2> Uv00,
              std::array<double, 2> Uv10,
              std::array<double, 2> Uv11,
              std::array<double, 2> Uv01,
              const PrimitivePlacement& Placement)
{
    EmitTriangle(Out, P00, P10, P11, Uv00, Uv10, Uv11, Placement);
    EmitTriangle(Out, P00, P11, P01, Uv00, Uv11, Uv01, Placement);
}

std::filesystem::path ResolveFacetArchivePath(const std::string& PathOrRel)
{
    namespace fs = std::filesystem;
    const fs::path Direct(PathOrRel);
    std::error_code Error;
    if (fs::is_regular_file(Direct, Error))
    {
        return Direct;
    }

    // Check active working directory & ancestor Content / EngineContent roots (supports standalone staged Content/)
    std::vector<fs::path> SearchPrefixes;
    if (const char* ProjectOverride = std::getenv("FRONTIER_PROJECT_CONTENT_DIR"); ProjectOverride && *ProjectOverride)
    {
        SearchPrefixes.emplace_back(ProjectOverride);
    }
    if (const char* EngineOverride = std::getenv("FRONTIER_ENGINE_CONTENT_DIR"); EngineOverride && *EngineOverride)
    {
        SearchPrefixes.emplace_back(EngineOverride);
    }

    fs::path Cursor = fs::current_path(Error);
    for (int Depth = 0; Depth < 6 && !Cursor.empty(); ++Depth)
    {
        SearchPrefixes.push_back(Cursor / "Content");
        SearchPrefixes.push_back(Cursor / "Projects" / "Everest" / "Content");
        SearchPrefixes.push_back(Cursor / "EngineContent");
        const fs::path Parent = Cursor.parent_path();
        if (Parent == Cursor)
        {
            break;
        }
        Cursor = Parent;
    }

    std::string Stripped = PathOrRel;
    if (Stripped.rfind("EngineContent/", 0) == 0)
    {
        Stripped = Stripped.substr(std::strlen("EngineContent/"));
    }
    else if (Stripped.rfind("Content/", 0) == 0)
    {
        Stripped = Stripped.substr(std::strlen("Content/"));
    }

    for (const fs::path& Prefix : SearchPrefixes)
    {
        const fs::path Candidate = Prefix / Stripped;
        if (fs::is_regular_file(Candidate, Error))
        {
            return Candidate;
        }
    }
    return Direct;
}

uint32_t ReadUint32Le(const uint8_t* Ptr) noexcept
{
    uint32_t Word = 0u;
    std::memcpy(&Word, Ptr, sizeof(uint32_t));
    return Word;
}

float ReadFloat32Le(const uint8_t* Ptr) noexcept
{
    float Value = 0.0f;
    std::memcpy(&Value, Ptr, sizeof(float));
    return Value;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                           1. BINARY FCT1 FACET ARCHIVE CODEC & CONTROL VEHICLE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

bool LoadFacetStreamFromMemory(std::span<const uint8_t> Bytes,
                               FacetField&              OutFacets,
                               const PrimitivePlacement&    Placement,
                               bool                     ConvertBlenderZToY,
                               int32_t                  OverrideClassId)
{
    if (Bytes.size() < 48u || std::memcmp(Bytes.data(), "FCT1", 4u) != 0)
    {
        return false;
    }

    const uint32_t VertexCount   = ReadUint32Le(Bytes.data() + 4u);
    const uint32_t TriangleCount = ReadUint32Le(Bytes.data() + 8u);
    const uint32_t Flags         = ReadUint32Le(Bytes.data() + 12u);
    if (VertexCount == 0u || TriangleCount == 0u)
    {
        return false;
    }

    const size_t PositionsBytes = static_cast<size_t>(VertexCount) * 12u;
    const size_t IndicesBytes   = static_cast<size_t>(TriangleCount) * 12u;
    const size_t ClassesBytes   = static_cast<size_t>(TriangleCount) * 4u;
    const size_t NormalsBytes   = (Flags & 1u) != 0u ? (static_cast<size_t>(TriangleCount) * 36u) : 0u;
    const size_t ExpectedTotal  = 48u + PositionsBytes + IndicesBytes + ClassesBytes + NormalsBytes;
    if (Bytes.size() < ExpectedTotal)
    {
        return false;
    }

    const uint8_t* PosPtr = Bytes.data() + 48u;
    const uint8_t* IdxPtr = PosPtr + PositionsBytes;
    const uint8_t* ClsPtr = IdxPtr + IndicesBytes;

    std::vector<FacetPoint3D> Vertices(VertexCount);
    for (uint32_t VIdx = 0u; VIdx < VertexCount; ++VIdx)
    {
        const float RawX = ReadFloat32Le(PosPtr + VIdx * 12u + 0u);
        const float RawY = ReadFloat32Le(PosPtr + VIdx * 12u + 4u);
        const float RawZ = ReadFloat32Le(PosPtr + VIdx * 12u + 8u);
        if (ConvertBlenderZToY)
        {
            // Blender vehicle convention (+X forward, +Y left, +Z up) -> Frontier Y-up (+X right, +Y up, +Z forward).
            // Reflection count = 2 (negate Y -> right, swap Y/Z -> even permutation), so CCW winding stays outward!
            Vertices[VIdx] = {static_cast<double>(-RawY), static_cast<double>(RawZ), static_cast<double>(RawX)};
        }
        else
        {
            Vertices[VIdx] = {static_cast<double>(RawX), static_cast<double>(RawY), static_cast<double>(RawZ)};
        }
    }

    if (!OutFacets.Corners.empty() && OutFacets.CornerUVs.size() < OutFacets.Corners.size())
    {
        OutFacets.CornerUVs.resize(OutFacets.Corners.size(), {0.0, 0.0});
    }

    OutFacets.Corners.reserve(OutFacets.Corners.size() + static_cast<size_t>(TriangleCount) * 3u);
    OutFacets.CornerUVs.reserve(OutFacets.CornerUVs.size() + static_cast<size_t>(TriangleCount) * 3u);
    OutFacets.Classification.reserve(OutFacets.Classification.size() + TriangleCount);
    OutFacets.SharpMask.reserve(OutFacets.SharpMask.size() + TriangleCount);

    for (uint32_t TIdx = 0u; TIdx < TriangleCount; ++TIdx)
    {
        const uint32_t I0 = ReadUint32Le(IdxPtr + TIdx * 12u + 0u);
        const uint32_t I1 = ReadUint32Le(IdxPtr + TIdx * 12u + 4u);
        const uint32_t I2 = ReadUint32Le(IdxPtr + TIdx * 12u + 8u);
        if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
        {
            return false;
        }
        const uint32_t RawClass = ReadUint32Le(ClsPtr + TIdx * 4u);
        const uint32_t FinalCls = (OverrideClassId >= 0) ? static_cast<uint32_t>(OverrideClassId) : RawClass;

        const FacetPoint3D P0 = Vertices[I0];
        const FacetPoint3D P1 = Vertices[I1];
        const FacetPoint3D P2 = Vertices[I2];

        // Dominant-axis triplanar UV projection per facet so UV-checker & virtualized material LODs stay distortion-free
        const FacetPoint3D E1 = {P1.X - P0.X, P1.Y - P0.Y, P1.Z - P0.Z};
        const FacetPoint3D E2 = {P2.X - P0.X, P2.Y - P0.Y, P2.Z - P0.Z};
        const double       Nx = std::abs(E1.Y * E2.Z - E1.Z * E2.Y);
        const double       Ny = std::abs(E1.Z * E2.X - E1.X * E2.Z);
        const double       Nz = std::abs(E1.X * E2.Y - E1.Y * E2.X);

        auto ProjectTriplanarUv = [&](FacetPoint3D Pt) -> std::array<double, 2> {
            if (Ny >= Nx && Ny >= Nz)
            {
                return {Pt.X * Placement.UvScale, Pt.Z * Placement.UvScale};
            }
            if (Nx >= Nz)
            {
                return {Pt.Z * Placement.UvScale, Pt.Y * Placement.UvScale};
            }
            return {Pt.X * Placement.UvScale, Pt.Y * Placement.UvScale};
        };

        PrimitivePlacement TriPlace   = Placement;
        TriPlace.ClassificationId = FinalCls;
        TriPlace.UvScale          = 1.0;
        EmitTriangle(OutFacets,
                     P0,
                     P1,
                     P2,
                     ProjectTriplanarUv(P0),
                     ProjectTriplanarUv(P1),
                     ProjectTriplanarUv(P2),
                     TriPlace);
    }

    return true;
}

bool LoadFacetStreamFromFile(const std::string&    PathOrContentRel,
                             FacetField&           OutFacets,
                             const PrimitivePlacement& Placement,
                             bool                  ConvertBlenderZToY,
                             int32_t               OverrideClassId)
{
    const std::filesystem::path Resolved = ResolveFacetArchivePath(PathOrContentRel);
    std::ifstream               Input(Resolved, std::ios::binary | std::ios::ate);
    if (!Input)
    {
        return false;
    }
    const auto ByteCount = Input.tellg();
    if (ByteCount <= 48)
    {
        return false;
    }
    std::vector<uint8_t> Bytes(static_cast<size_t>(ByteCount));
    Input.seekg(0, std::ios::beg);
    if (!Input.read(reinterpret_cast<char*>(Bytes.data()), ByteCount))
    {
        return false;
    }
    return LoadFacetStreamFromMemory(Bytes, OutFacets, Placement, ConvertBlenderZToY, OverrideClassId);
}

bool AssembleControlVehicle(FacetField&                    OutFacets,
                            const ControlVehiclePlacement& Placement,
                            std::string*                   OutDiagnostic)
{
    constexpr double kTyreOuterRadiusM = 0.50855;
    constexpr double kFrontAxleZM      = 1.665;
    constexpr double kRearAxleZM       = -1.705;
    constexpr double kTrackHalfXM      = 1.045;

    const double cosH = std::cos(Placement.HeadingYawRadians);
    const double sinH = std::sin(Placement.HeadingYawRadians);
    auto LocalToWorld = [&](double LocalX, double LocalY, double LocalZ) -> FacetPoint3D {
        return {
            Placement.WorldOriginMeters.X + (LocalX * cosH + LocalZ * sinH),
            Placement.WorldOriginMeters.Y + LocalY,
            Placement.WorldOriginMeters.Z + (-LocalX * sinH + LocalZ * cosH),
        };
    };

    const std::string BodyPath = Placement.ArchiveDirectory + "/ControlVehicleBody.facet";
    const std::string RimPath  = Placement.ArchiveDirectory + "/ControlVehicleRim.facet";
    const std::string TyrePath = Placement.ArchiveDirectory + "/ControlVehicleTyre.facet";

    PrimitivePlacement BodyPlace{};
    BodyPlace.TranslationMeters = LocalToWorld(0.0, kTyreOuterRadiusM, 0.0);
    BodyPlace.YawRadians        = Placement.HeadingYawRadians;
    BodyPlace.UniformScale      = 1.0;
    BodyPlace.UvScale           = 1.5;

    if (!LoadFacetStreamFromFile(BodyPath, OutFacets, BodyPlace, true, -1))
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Failed to load ControlVehicleBody.facet from " + BodyPath;
        }
        return false;
    }

    struct WheelMount
    {
        double LateralXMeters;
        double AxleZMeters;
        bool   FrontSteered;
        bool   RightSide;
    };
    const std::array<WheelMount, 4> Wheels{{
        {-kTrackHalfXM, kFrontAxleZM, true, false},
        {+kTrackHalfXM, kFrontAxleZM, true, true},
        {-kTrackHalfXM, kRearAxleZM, false, false},
        {+kTrackHalfXM, kRearAxleZM, false, true},
    }};

    for (const WheelMount& Wheel : Wheels)
    {
        PrimitivePlacement WheelPlace{};
        WheelPlace.TranslationMeters = LocalToWorld(Wheel.LateralXMeters, kTyreOuterRadiusM, Wheel.AxleZMeters);
        WheelPlace.YawRadians        = Placement.HeadingYawRadians +
                                       (Wheel.FrontSteered ? Placement.FrontSteerRadians : 0.0) +
                                       (Wheel.RightSide ? π : 0.0);
        WheelPlace.UniformScale = 1.0;
        WheelPlace.UvScale      = 3.0;

        if (!LoadFacetStreamFromFile(RimPath, OutFacets, WheelPlace, true, -1))
        {
            if (OutDiagnostic)
            {
                *OutDiagnostic = "Failed to load ControlVehicleRim.facet from " + RimPath;
            }
            return false;
        }
        if (!LoadFacetStreamFromFile(TyrePath, OutFacets, WheelPlace, true, 11))
        {
            if (OutDiagnostic)
            {
                *OutDiagnostic = "Failed to load ControlVehicleTyre.facet from " + TyrePath;
            }
            return false;
        }
    }

    // Add interior cabin seats & dashboard console behind the Class-7 laminated windshield so two-layer
    // glass peel & opaque-behind-glass Hi-Z survival are physically exercised on ControlVehicle.
    if (Placement.IncludeInteriorCabin)
    {
        for (double SeatX : {-0.42, 0.42})
        {
            PrimitivePlacement SeatBack{};
            SeatBack.TranslationMeters = LocalToWorld(SeatX, kTyreOuterRadiusM + 0.78, -0.08);
            SeatBack.YawRadians        = Placement.HeadingYawRadians;
            SeatBack.ClassificationId  = 16u;
            SeatBack.UvScale           = 4.0;
            AppendProceduralBox(OutFacets, {0.24, 0.34, 0.09}, SeatBack);

            PrimitivePlacement Headrest{};
            Headrest.TranslationMeters = LocalToWorld(SeatX, kTyreOuterRadiusM + 1.16, -0.10);
            Headrest.YawRadians        = Placement.HeadingYawRadians;
            Headrest.ClassificationId  = 16u;
            Headrest.UvScale           = 4.0;
            AppendProceduralBox(OutFacets, {0.13, 0.10, 0.07}, Headrest);
        }

        PrimitivePlacement DashConsole{};
        DashConsole.TranslationMeters = LocalToWorld(0.0, kTyreOuterRadiusM + 0.74, 0.72);
        DashConsole.YawRadians        = Placement.HeadingYawRadians;
        DashConsole.ClassificationId  = 16u;
        DashConsole.UvScale           = 3.0;
        AppendProceduralBox(OutFacets, {0.78, 0.14, 0.22}, DashConsole);
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  2. CORE WATERTIGHT PROCEDURAL PRIMITIVE GEOMETRY GENERATORS
//------------------------------------------------------------------------------------------------------------------------

void AppendProceduralBox(FacetField& InOutFacets, FacetPoint3D Half, const PrimitivePlacement& Placement)
{
    const double Hx = std::max(1.0e-4, Half.X);
    const double Hy = std::max(1.0e-4, Half.Y);
    const double Hz = std::max(1.0e-4, Half.Z);

    // +Z front, -Z back, +X right, -X left, +Y top, -Y bottom (all outward CCW in right-handed Y-up)
    EmitQuad(InOutFacets, {-Hx, -Hy, +Hz}, {+Hx, -Hy, +Hz}, {+Hx, +Hy, +Hz}, {-Hx, +Hy, +Hz},
             {0, 0}, {1, 0}, {1, 1}, {0, 1}, Placement);
    EmitQuad(InOutFacets, {+Hx, -Hy, -Hz}, {-Hx, -Hy, -Hz}, {-Hx, +Hy, -Hz}, {+Hx, +Hy, -Hz},
             {0, 0}, {1, 0}, {1, 1}, {0, 1}, Placement);
    EmitQuad(InOutFacets, {+Hx, -Hy, +Hz}, {+Hx, -Hy, -Hz}, {+Hx, +Hy, -Hz}, {+Hx, +Hy, +Hz},
             {0, 0}, {1, 0}, {1, 1}, {0, 1}, Placement);
    EmitQuad(InOutFacets, {-Hx, -Hy, -Hz}, {-Hx, -Hy, +Hz}, {-Hx, +Hy, +Hz}, {-Hx, +Hy, -Hz},
             {0, 0}, {1, 0}, {1, 1}, {0, 1}, Placement);
    EmitQuad(InOutFacets, {-Hx, +Hy, +Hz}, {+Hx, +Hy, +Hz}, {+Hx, +Hy, -Hz}, {-Hx, +Hy, -Hz},
             {0, 0}, {1, 0}, {1, 1}, {0, 1}, Placement);
    EmitQuad(InOutFacets, {-Hx, -Hy, -Hz}, {+Hx, -Hy, -Hz}, {+Hx, -Hy, +Hz}, {-Hx, -Hy, +Hz},
             {0, 0}, {1, 0}, {1, 1}, {0, 1}, Placement);
}

void AppendProceduralUvSphere(FacetField&           InOutFacets,
                              double                RadiusMeters,
                              uint32_t              LongitudeSegments,
                              uint32_t              LatitudeRings,
                              const PrimitivePlacement& Placement)
{
    const double   R     = std::max(1.0e-4, RadiusMeters);
    const uint32_t Segs  = std::max(6u, LongitudeSegments);
    const uint32_t Rings = std::max(4u, LatitudeRings);

    auto SpherePt = [&](uint32_t Ring, uint32_t Seg) -> FacetPoint3D {
        if (Ring == 0u)
        {
            return {0.0, -R, 0.0};
        }
        if (Ring == Rings)
        {
            return {0.0, +R, 0.0};
        }
        const double φ = -0.5 * π + π * (static_cast<double>(Ring) / static_cast<double>(Rings));
        const double θ = Twoπ * (static_cast<double>(Seg % Segs) / static_cast<double>(Segs));
        return {R * std::cos(φ) * std::cos(θ), R * std::sin(φ), R * std::cos(φ) * std::sin(θ)};
    };

    for (uint32_t Ring = 0u; Ring < Rings; ++Ring)
    {
        const double V0 = static_cast<double>(Ring) / static_cast<double>(Rings);
        const double V1 = static_cast<double>(Ring + 1u) / static_cast<double>(Rings);
        for (uint32_t Seg = 0u; Seg < Segs; ++Seg)
        {
            const double U0  = static_cast<double>(Seg) / static_cast<double>(Segs);
            const double U1  = static_cast<double>(Seg + 1u) / static_cast<double>(Segs);
            const auto   P00 = SpherePt(Ring, Seg);
            const auto   P10 = SpherePt(Ring, Seg + 1u);
            const auto   P11 = SpherePt(Ring + 1u, Seg + 1u);
            const auto   P01 = SpherePt(Ring + 1u, Seg);

            if (Ring == 0u)
            {
                EmitTriangle(InOutFacets, P00, P01, P11, {0.5 * (U0 + U1), V0}, {U0, V1}, {U1, V1}, Placement);
            }
            else if (Ring + 1u == Rings)
            {
                EmitTriangle(InOutFacets, P00, P01, P10, {U0, V0}, {0.5 * (U0 + U1), V1}, {U1, V0}, Placement);
            }
            else
            {
                EmitQuad(InOutFacets, P00, P01, P11, P10, {U0, V0}, {U0, V1}, {U1, V1}, {U1, V0}, Placement);
            }
        }
    }
}

void AppendProceduralGeodesicSphere(FacetField&           InOutFacets,
                                    double                RadiusMeters,
                                    uint32_t              Subdivisions,
                                    const PrimitivePlacement& Placement)
{
    const double R   = std::max(1.0e-4, RadiusMeters);
    const double Phi = 0.5 * (1.0 + std::sqrt(5.0));
    auto NormR = [&](FacetPoint3D P) -> FacetPoint3D {
        const double Len = std::sqrt(P.X * P.X + P.Y * P.Y + P.Z * P.Z);
        return (Len > 1.0e-12) ? FacetPoint3D{P.X * R / Len, P.Y * R / Len, P.Z * R / Len} : FacetPoint3D{0.0, R, 0.0};
    };

    std::vector<FacetPoint3D> Verts = {
        NormR({-1, Phi, 0}),  NormR({1, Phi, 0}),   NormR({-1, -Phi, 0}), NormR({1, -Phi, 0}),
        NormR({0, -1, Phi}),  NormR({0, 1, Phi}),   NormR({0, -1, -Phi}), NormR({0, 1, -Phi}),
        NormR({Phi, 0, -1}),  NormR({Phi, 0, 1}),   NormR({-Phi, 0, -1}), NormR({-Phi, 0, 1}),
    };
    std::vector<std::array<uint32_t, 3>> Faces = {
        {0, 11, 5},  {0, 5, 1},   {0, 1, 7},   {0, 7, 10},  {0, 10, 11},
        {1, 5, 9},   {5, 11, 4},  {11, 10, 2}, {10, 7, 6},  {7, 1, 8},
        {3, 9, 4},   {3, 4, 2},   {3, 2, 6},   {3, 6, 8},   {3, 8, 9},
        {4, 9, 5},   {2, 4, 11},  {6, 2, 10},  {8, 6, 7},   {9, 8, 1},
    };

    const uint32_t Subdiv = std::min(5u, Subdivisions);
    for (uint32_t Level = 0u; Level < Subdiv; ++Level)
    {
        std::map<uint64_t, uint32_t>         MidpointIndex;
        std::vector<std::array<uint32_t, 3>> NextFaces;
        NextFaces.reserve(Faces.size() * 4u);

        auto Midpoint = [&](uint32_t A, uint32_t B) -> uint32_t {
            const uint64_t Key = (static_cast<uint64_t>(std::min(A, B)) << 32u) | static_cast<uint64_t>(std::max(A, B));
            auto           It  = MidpointIndex.find(Key);
            if (It != MidpointIndex.end())
            {
                return It->second;
            }
            const FacetPoint3D Mid = NormR({0.5 * (Verts[A].X + Verts[B].X),
                                            0.5 * (Verts[A].Y + Verts[B].Y),
                                            0.5 * (Verts[A].Z + Verts[B].Z)});
            const auto         Idx = static_cast<uint32_t>(Verts.size());
            Verts.push_back(Mid);
            MidpointIndex.emplace(Key, Idx);
            return Idx;
        };

        for (const auto& Tri : Faces)
        {
            const uint32_t Mab = Midpoint(Tri[0], Tri[1]);
            const uint32_t Mbc = Midpoint(Tri[1], Tri[2]);
            const uint32_t Mca = Midpoint(Tri[2], Tri[0]);
            NextFaces.push_back({Tri[0], Mab, Mca});
            NextFaces.push_back({Tri[1], Mbc, Mab});
            NextFaces.push_back({Tri[2], Mca, Mbc});
            NextFaces.push_back({Mab, Mbc, Mca});
        }
        Faces = std::move(NextFaces);
    }

    auto SphericalUv = [&](FacetPoint3D Pt) -> std::array<double, 2> {
        const double U = 0.5 + std::atan2(Pt.Z, Pt.X) / Twoπ;
        const double V = 0.5 + std::asin(std::clamp(Pt.Y / R, -1.0, 1.0)) / π;
        return {U, V};
    };

    for (const auto& Tri : Faces)
    {
        EmitTriangle(InOutFacets,
                     Verts[Tri[0]],
                     Verts[Tri[1]],
                     Verts[Tri[2]],
                     SphericalUv(Verts[Tri[0]]),
                     SphericalUv(Verts[Tri[1]]),
                     SphericalUv(Verts[Tri[2]]),
                     Placement);
    }
}

void AppendProceduralCylinder(FacetField&           InOutFacets,
                              double                RadiusMeters,
                              double                HeightMeters,
                              uint32_t              RadialSegments,
                              const PrimitivePlacement& Placement)
{
    const double   R    = std::max(1.0e-4, RadiusMeters);
    const double   Half = 0.5 * std::max(1.0e-4, HeightMeters);
    const uint32_t Segs = std::max(6u, RadialSegments);

    for (uint32_t Seg = 0u; Seg < Segs; ++Seg)
    {
        const double θ0 = Twoπ * (static_cast<double>(Seg) / static_cast<double>(Segs));
        const double θ1 = Twoπ * (static_cast<double>((Seg + 1u) % Segs) / static_cast<double>(Segs));
        const double X0 = R * std::cos(θ0), Z0 = R * std::sin(θ0);
        const double X1 = R * std::cos(θ1), Z1 = R * std::sin(θ1);
        const double U0 = static_cast<double>(Seg) / static_cast<double>(Segs);
        const double U1 = static_cast<double>(Seg + 1u) / static_cast<double>(Segs);

        EmitQuad(InOutFacets, {X0, -Half, Z0}, {X0, +Half, Z0}, {X1, +Half, Z1}, {X1, -Half, Z1},
                 {U0, 0}, {U0, 1}, {U1, 1}, {U1, 0}, Placement);
        EmitTriangle(InOutFacets, {0, +Half, 0}, {X1, +Half, Z1}, {X0, +Half, Z0},
                     {0.5, 0.5}, {0.5 + 0.5 * std::cos(θ1), 0.5 + 0.5 * std::sin(θ1)},
                     {0.5 + 0.5 * std::cos(θ0), 0.5 + 0.5 * std::sin(θ0)}, Placement);
        EmitTriangle(InOutFacets, {0, -Half, 0}, {X0, -Half, Z0}, {X1, -Half, Z1},
                     {0.5, 0.5}, {0.5 + 0.5 * std::cos(θ0), 0.5 + 0.5 * std::sin(θ0)},
                     {0.5 + 0.5 * std::cos(θ1), 0.5 + 0.5 * std::sin(θ1)}, Placement);
    }
}

void AppendProceduralCapsule(FacetField&           InOutFacets,
                             double                RadiusMeters,
                             double                CylinderSpanMeters,
                             uint32_t              RadialSegments,
                             uint32_t              CapRings,
                             const PrimitivePlacement& Placement)
{
    const double   R     = std::max(1.0e-4, RadiusMeters);
    const double   HalfH = 0.5 * std::max(0.0, CylinderSpanMeters);
    const uint32_t Segs  = std::max(6u, RadialSegments);
    const uint32_t CapN  = std::max(2u, CapRings);
    const uint32_t Rings = 2u * CapN + 1u;

    auto RingProfile = [&](uint32_t RingIdx, double& OutY, double& OutRingR) {
        if (RingIdx <= CapN)
        {
            const double φ = -0.5 * π + (0.5 * π) * (static_cast<double>(RingIdx) / static_cast<double>(CapN));
            OutY           = -HalfH + R * std::sin(φ);
            OutRingR       = R * std::cos(φ);
        }
        else
        {
            const double φ = (0.5 * π) * (static_cast<double>(RingIdx - CapN - 1u) / static_cast<double>(CapN));
            OutY           = +HalfH + R * std::sin(φ);
            OutRingR       = R * std::cos(φ);
        }
    };

    for (uint32_t Ring = 0u; Ring < Rings; ++Ring)
    {
        double Y0 = 0.0, R0 = 0.0, Y1 = 0.0, R1 = 0.0;
        RingProfile(Ring, Y0, R0);
        RingProfile(Ring + 1u, Y1, R1);
        const double V0 = static_cast<double>(Ring) / static_cast<double>(Rings);
        const double V1 = static_cast<double>(Ring + 1u) / static_cast<double>(Rings);

        for (uint32_t Seg = 0u; Seg < Segs; ++Seg)
        {
            const double θ0 = Twoπ * (static_cast<double>(Seg) / static_cast<double>(Segs));
            const double θ1 = Twoπ * (static_cast<double>((Seg + 1u) % Segs) / static_cast<double>(Segs));
            const double U0 = static_cast<double>(Seg) / static_cast<double>(Segs);
            const double U1 = static_cast<double>(Seg + 1u) / static_cast<double>(Segs);

            const FacetPoint3D P00 = (Ring == 0u) ? FacetPoint3D{0.0, -HalfH - R, 0.0} : FacetPoint3D{R0 * std::cos(θ0), Y0, R0 * std::sin(θ0)};
            const FacetPoint3D P10 = (Ring == 0u) ? FacetPoint3D{0.0, -HalfH - R, 0.0} : FacetPoint3D{R0 * std::cos(θ1), Y0, R0 * std::sin(θ1)};
            const FacetPoint3D P11 = (Ring + 1u == Rings) ? FacetPoint3D{0.0, +HalfH + R, 0.0} : FacetPoint3D{R1 * std::cos(θ1), Y1, R1 * std::sin(θ1)};
            const FacetPoint3D P01 = (Ring + 1u == Rings) ? FacetPoint3D{0.0, +HalfH + R, 0.0} : FacetPoint3D{R1 * std::cos(θ0), Y1, R1 * std::sin(θ0)};

            if (Ring == 0u)
            {
                EmitTriangle(InOutFacets, P00, P01, P11, {0.5 * (U0 + U1), V0}, {U0, V1}, {U1, V1}, Placement);
            }
            else if (Ring + 1u == Rings)
            {
                EmitTriangle(InOutFacets, P00, P01, P10, {U0, V0}, {0.5 * (U0 + U1), V1}, {U1, V0}, Placement);
            }
            else
            {
                EmitQuad(InOutFacets, P00, P01, P11, P10, {U0, V0}, {U0, V1}, {U1, V1}, {U1, V0}, Placement);
            }
        }
    }
}

void AppendProceduralTorus(FacetField&           InOutFacets,
                           double                MajorRadiusMeters,
                           double                MinorRadiusMeters,
                           uint32_t              MajorSegments,
                           uint32_t              MinorSegments,
                           const PrimitivePlacement& Placement)
{
    const double   MajR = std::max(1.0e-3, MajorRadiusMeters);
    const double   MinR = std::max(1.0e-4, MinorRadiusMeters);
    const uint32_t MajN = std::max(8u, MajorSegments);
    const uint32_t MinN = std::max(6u, MinorSegments);

    auto TorusPt = [&](uint32_t MajIdx, uint32_t MinIdx) -> FacetPoint3D {
        const double θ = Twoπ * (static_cast<double>(MajIdx % MajN) / static_cast<double>(MajN));
        const double φ = Twoπ * (static_cast<double>(MinIdx % MinN) / static_cast<double>(MinN));
        const double TubeR = MajR + MinR * std::cos(φ);
        return {TubeR * std::cos(θ), MinR * std::sin(φ), TubeR * std::sin(θ)};
    };

    for (uint32_t Maj = 0u; Maj < MajN; ++Maj)
    {
        const double U0 = static_cast<double>(Maj) / static_cast<double>(MajN);
        const double U1 = static_cast<double>(Maj + 1u) / static_cast<double>(MajN);
        for (uint32_t Min = 0u; Min < MinN; ++Min)
        {
            const double V0 = static_cast<double>(Min) / static_cast<double>(MinN);
            const double V1 = static_cast<double>(Min + 1u) / static_cast<double>(MinN);
            EmitQuad(InOutFacets,
                     TorusPt(Maj, Min),
                     TorusPt(Maj, Min + 1u),
                     TorusPt(Maj + 1u, Min + 1u),
                     TorusPt(Maj + 1u, Min),
                     {U0, V0},
                     {U0, V1},
                     {U1, V1},
                     {U1, V0},
                     Placement);
        }
    }
}

void AppendProceduralCone(FacetField&           InOutFacets,
                          double                BaseRadiusMeters,
                          double                HeightMeters,
                          uint32_t              RadialSegments,
                          const PrimitivePlacement& Placement)
{
    const double   R    = std::max(1.0e-4, BaseRadiusMeters);
    const double   H    = std::max(1.0e-4, HeightMeters);
    const uint32_t Segs = std::max(6u, RadialSegments);

    for (uint32_t Seg = 0u; Seg < Segs; ++Seg)
    {
        const double θ0 = Twoπ * (static_cast<double>(Seg) / static_cast<double>(Segs));
        const double θ1 = Twoπ * (static_cast<double>((Seg + 1u) % Segs) / static_cast<double>(Segs));
        const double X0 = R * std::cos(θ0), Z0 = R * std::sin(θ0);
        const double X1 = R * std::cos(θ1), Z1 = R * std::sin(θ1);
        const double U0 = static_cast<double>(Seg) / static_cast<double>(Segs);
        const double U1 = static_cast<double>(Seg + 1u) / static_cast<double>(Segs);

        EmitTriangle(InOutFacets, {X0, 0.0, Z0}, {0.0, H, 0.0}, {X1, 0.0, Z1},
                     {U0, 0.0}, {0.5 * (U0 + U1), 1.0}, {U1, 0.0}, Placement);
        EmitTriangle(InOutFacets, {0.0, 0.0, 0.0}, {X0, 0.0, Z0}, {X1, 0.0, Z1},
                     {0.5, 0.5}, {0.5 + 0.5 * std::cos(θ0), 0.5 + 0.5 * std::sin(θ0)},
                     {0.5 + 0.5 * std::cos(θ1), 0.5 + 0.5 * std::sin(θ1)}, Placement);
    }
}

void AppendProceduralArchTunnel(FacetField&           InOutFacets,
                                double                InnerRadiusMeters,
                                double                WallThicknessMeters,
                                double                LengthMeters,
                                uint32_t              ArcSegments,
                                const PrimitivePlacement& Placement)
{
    const double   Ri    = std::max(0.2, InnerRadiusMeters);
    const double   Ro    = Ri + std::max(0.05, WallThicknessMeters);
    const double   HalfL = 0.5 * std::max(0.2, LengthMeters);
    const uint32_t Segs  = std::max(6u, ArcSegments);

    for (uint32_t Seg = 0u; Seg < Segs; ++Seg)
    {
        const double θ0  = π * (static_cast<double>(Seg) / static_cast<double>(Segs));
        const double θ1  = π * (static_cast<double>(Seg + 1u) / static_cast<double>(Segs));
        const double U0  = static_cast<double>(Seg) / static_cast<double>(Segs);
        const double U1  = static_cast<double>(Seg + 1u) / static_cast<double>(Segs);
        const double Xi0 = Ri * std::cos(θ0), Yi0 = Ri * std::sin(θ0);
        const double Xi1 = Ri * std::cos(θ1), Yi1 = Ri * std::sin(θ1);
        const double Xo0 = Ro * std::cos(θ0), Yo0 = Ro * std::sin(θ0);
        const double Xo1 = Ro * std::cos(θ1), Yo1 = Ro * std::sin(θ1);

        // Outer arch vault, inner ceiling vault, front portal face, back portal face
        EmitQuad(InOutFacets, {Xo0, Yo0, -HalfL}, {Xo0, Yo0, +HalfL}, {Xo1, Yo1, +HalfL}, {Xo1, Yo1, -HalfL},
                 {U0, 0}, {U0, 1}, {U1, 1}, {U1, 0}, Placement);
        EmitQuad(InOutFacets, {Xi1, Yi1, -HalfL}, {Xi1, Yi1, +HalfL}, {Xi0, Yi0, +HalfL}, {Xi0, Yi0, -HalfL},
                 {U1, 0}, {U1, 1}, {U0, 1}, {U0, 0}, Placement);
        EmitQuad(InOutFacets, {Xi0, Yi0, +HalfL}, {Xi1, Yi1, +HalfL}, {Xo1, Yo1, +HalfL}, {Xo0, Yo0, +HalfL},
                 {U0, 0}, {U1, 0}, {U1, 1}, {U0, 1}, Placement);
        EmitQuad(InOutFacets, {Xo0, Yo0, -HalfL}, {Xo1, Yo1, -HalfL}, {Xi1, Yi1, -HalfL}, {Xi0, Yi0, -HalfL},
                 {U0, 1}, {U1, 1}, {U1, 0}, {U0, 0}, Placement);
    }
    // Base footing caps at θ = 0 and θ = π
    EmitQuad(InOutFacets, {Ri, 0, -HalfL}, {Ri, 0, +HalfL}, {Ro, 0, +HalfL}, {Ro, 0, -HalfL},
             {0, 0}, {0, 1}, {1, 1}, {1, 0}, Placement);
    EmitQuad(InOutFacets, {-Ro, 0, -HalfL}, {-Ro, 0, +HalfL}, {-Ri, 0, +HalfL}, {-Ri, 0, -HalfL},
             {0, 0}, {0, 1}, {1, 1}, {1, 0}, Placement);
}

void AppendProceduralGroundGrid(FacetField&           InOutFacets,
                                double                HalfExtentXMeters,
                                double                HalfExtentZMeters,
                                uint32_t              SubdivisionsX,
                                uint32_t              SubdivisionsZ,
                                const PrimitivePlacement& Placement)
{
    const double   Hx = std::max(0.5, HalfExtentXMeters);
    const double   Hz = std::max(0.5, HalfExtentZMeters);
    const uint32_t Nx = std::max(1u, SubdivisionsX);
    const uint32_t Nz = std::max(1u, SubdivisionsZ);

    for (uint32_t Iz = 0u; Iz < Nz; ++Iz)
    {
        const double V0 = static_cast<double>(Iz) / static_cast<double>(Nz);
        const double V1 = static_cast<double>(Iz + 1u) / static_cast<double>(Nz);
        const double Z0 = -Hz + 2.0 * Hz * V0;
        const double Z1 = -Hz + 2.0 * Hz * V1;
        for (uint32_t Ix = 0u; Ix < Nx; ++Ix)
        {
            const double U0 = static_cast<double>(Ix) / static_cast<double>(Nx);
            const double U1 = static_cast<double>(Ix + 1u) / static_cast<double>(Nx);
            const double X0 = -Hx + 2.0 * Hx * U0;
            const double X1 = -Hx + 2.0 * Hx * U1;
            EmitQuad(InOutFacets, {X0, 0.0, Z1}, {X1, 0.0, Z1}, {X1, 0.0, Z0}, {X0, 0.0, Z0},
                     {U0, V1}, {U1, V1}, {U1, V0}, {U0, V0}, Placement);
        }
    }
}

} // namespace Frontier
