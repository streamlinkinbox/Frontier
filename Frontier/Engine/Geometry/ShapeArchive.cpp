/*==========================================================================================================================================
                                                             SHAPEARCHIVE.CPP
==========================================================================================================================================*/
// 🧩 Engine shapes: the Project Zero editor's construct primitives (cube, sphere, cylinder, cone, torus, plane) as
//    EngineContent/ShapeArchives/<Name>.toml, their facets tessellated from it and their signed distance volume baked to <Name>.sdfspace.

#include "Engine/Geometry/ShapeArchive.h"

#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <thread>

namespace Frontier
{

namespace
{

constexpr double   Pi                  = 3.14159265358979323846;
constexpr uint32_t MaximumVolumeTexels = 128;     // [texel] OccupantDistanceSpace's longest volume axis
constexpr uint32_t MinimumVolumeTexels = 8;       // [texel] and its shortest
constexpr double   PlaneThickness      = 0.01;    // [m] the plane slab
constexpr double   LayoutTolerance     = 1.0e-4;  // [m] a baked volume matches the archive's layout within this

using Vector = std::array<double, 3>;

Vector Subtract(const Vector& A, const Vector& B) { return {A[0] - B[0], A[1] - B[1], A[2] - B[2]}; }
Vector Add(const Vector& A, const Vector& B) { return {A[0] + B[0], A[1] + B[1], A[2] + B[2]}; }
Vector Scale(const Vector& A, double Factor) { return {A[0] * Factor, A[1] * Factor, A[2] * Factor}; }
double Dot(const Vector& A, const Vector& B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
Vector Cross(const Vector& A, const Vector& B) { return {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]}; }
double Length(const Vector& A) { return std::sqrt(Dot(A, A)); }
Vector Normalize(const Vector& A)
{
    const double Magnitude = Length(A);
    return Magnitude > 0.0 ? Scale(A, 1.0 / Magnitude) : Vector{0.0, 0.0, 1.0};
}

struct ShapeVertex
{
    Vector Position{};   // [m]
    Vector Normal{};     // [-]
};

// 📝 One triangle, wound so that its geometric normal agrees with its vertex normals (ConstructWorld.cpp Triangle); a facet with no
//    area (a pole of the latitude grid) is skipped.
void AppendTriangle(std::vector<ShapeFacet>& Facets, ShapeVertex A, ShapeVertex B, ShapeVertex C)
{
    const Vector Facing = Cross(Subtract(B.Position, A.Position), Subtract(C.Position, A.Position));
    if (Dot(Facing, Facing) < 1.0e-14) return;
    if (Dot(Facing, Add(Add(A.Normal, B.Normal), C.Normal)) < 0.0) std::swap(B, C);
    ShapeFacet Facet;
    Facet.Corners = {A.Position, B.Position, C.Position};
    Facet.Normals = {Normalize(A.Normal), Normalize(B.Normal), Normalize(C.Normal)};
    Facets.push_back(Facet);
}

void AppendQuad(std::vector<ShapeFacet>& Facets, const ShapeVertex& A, const ShapeVertex& B, const ShapeVertex& C, const ShapeVertex& D)
{
    AppendTriangle(Facets, A, B, C);
    AppendTriangle(Facets, A, C, D);
}

// 📝 Ericson, Real-Time Collision Detection §5.1.5: the closest point of a triangle by Voronoi region.
Vector ProjectClosestOnFacet(const Vector& Point, const Vector& A, const Vector& B, const Vector& C)
{
    const Vector AB = Subtract(B, A), AC = Subtract(C, A), AP = Subtract(Point, A);
    const double D1 = Dot(AB, AP), D2 = Dot(AC, AP);
    if (D1 <= 0.0 && D2 <= 0.0) return A;
    const Vector BP = Subtract(Point, B);
    const double D3 = Dot(AB, BP), D4 = Dot(AC, BP);
    if (D3 >= 0.0 && D4 <= D3) return B;
    const double VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0 && D1 >= 0.0 && D3 <= 0.0) return Add(A, Scale(AB, D1 / (D1 - D3)));
    const Vector CP = Subtract(Point, C);
    const double D5 = Dot(AB, CP), D6 = Dot(AC, CP);
    if (D6 >= 0.0 && D5 <= D6) return C;
    const double VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0 && D2 >= 0.0 && D6 <= 0.0) return Add(A, Scale(AC, D2 / (D2 - D6)));
    const double VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0) return Add(B, Scale(Subtract(C, B), (D4 - D3) / ((D4 - D3) + (D5 - D6))));
    const double Reciprocal = 1.0 / (VA + VB + VC);
    return Add(A, Add(Scale(AB, VB * Reciprocal), Scale(AC, VC * Reciprocal)));
}

// 📝 Van Oosterom and Strackee: the solid angle Ω a triangle subtends at the point, signed by its winding.
double ResolveSolidAngle(const Vector& Point, const Vector& A, const Vector& B, const Vector& C)
{
    const Vector RA = Subtract(A, Point), RB = Subtract(B, Point), RC = Subtract(C, Point);
    const double LA = Length(RA), LB = Length(RB), LC = Length(RC);
    const double Numerator   = Dot(RA, Cross(RB, RC));
    const double Denominator = LA * LB * LC + Dot(RA, RB) * LC + Dot(RA, RC) * LB + Dot(RB, RC) * LA;
    return 2.0 * std::atan2(Numerator, Denominator);
}

std::optional<ShapePrimitive> ResolvePrimitive(const std::string& Name)
{
    for (uint32_t Index = 0; Index < static_cast<uint32_t>(ShapePrimitive::Count); ++Index)
        if (Name == NameShapePrimitive(static_cast<ShapePrimitive>(Index))) return static_cast<ShapePrimitive>(Index);
    return std::nullopt;
}

template <typename View, size_t Count>
void ReadArray(const View& Node, std::array<double, Count>& Target)
{
    if (const auto* Values = Node.as_array())
        for (size_t Index = 0; Index < Count && Index < Values->size(); ++Index) Target[Index] = (*Values)[Index].value_or(Target[Index]);
}

bool ConfirmLayout(const ShapeDistanceVolume& Expected, const ShapeDistanceVolume& Baked)
{
    if (Expected.Resolution != Baked.Resolution || std::abs(Expected.VoxelExtent - Baked.VoxelExtent) > LayoutTolerance) return false;
    for (size_t Axis = 0; Axis < 3; ++Axis)
        if (std::abs(Expected.Minimum[Axis] - Baked.Minimum[Axis]) > LayoutTolerance) return false;
    return Baked.Distances.size() == static_cast<size_t>(Baked.Resolution[0]) * Baked.Resolution[1] * Baked.Resolution[2];
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    ARCHIVES
//------------------------------------------------------------------------------------------------------------------------

const char* NameShapePrimitive(ShapePrimitive Primitive) noexcept
{
    switch (Primitive)
    {
    case ShapePrimitive::Cube:     return "cube";
    case ShapePrimitive::Sphere:   return "sphere";
    case ShapePrimitive::Cylinder: return "cylinder";
    case ShapePrimitive::Cone:     return "cone";
    case ShapePrimitive::Torus:    return "torus";
    case ShapePrimitive::Plane:    return "plane";
    default:                       return "invalid";
    }
}

std::optional<ShapeArchive> ReadShapeArchive(const std::filesystem::path& Path, std::string* OutDiagnostic)
{
    toml::table Document;
    try
    {
        Document = toml::parse_file(Path.string());
    }
    catch (const toml::parse_error& Failure)
    {
        if (OutDiagnostic) *OutDiagnostic = Path.filename().string() + ": " + std::string(Failure.description());
        return std::nullopt;
    }
    ShapeArchive Archive;
    Archive.Source = Path;
    const auto Shape = Document["shape"];
    Archive.Name        = Shape["name"].value_or(Path.stem().string());
    Archive.Description = Shape["description"].value_or(std::string{});
    Archive.GlyphName   = Shape["glyph"].value_or(std::string{"Box"});
    const auto Primitive = ResolvePrimitive(Shape["primitive"].value_or(std::string{}));
    if (!Primitive)
    {
        if (OutDiagnostic) *OutDiagnostic = Path.filename().string() + ": primitive must be cube, sphere, cylinder, cone, torus or plane";
        return std::nullopt;
    }
    Archive.Primitive = *Primitive;
    ReadArray(Shape["size_m"], Archive.Size);
    for (double& Side : Archive.Size) Side = std::clamp(Side, 0.01, 1000.0);
    if (Archive.Primitive == ShapePrimitive::Plane) Archive.Size[2] = PlaneThickness;

    const auto Tessellation = Document["tessellation"];
    Archive.Segments  = std::clamp<uint32_t>(Tessellation["segments"].value_or(32u), 3u, 256u);
    Archive.Rings     = std::clamp<uint32_t>(Tessellation["rings"].value_or(16u), 3u, 256u);
    Archive.TubeRatio = std::clamp(Tessellation["tube_ratio"].value_or(0.3), 0.05, 0.95);

    // 📝 MaterialSpecification's twelve channels (Materials.bin): albedo rgb, roughness, incandescence rgb, metalness, coat weight,
    //    coat roughness, specular reflectance, transmission.
    const auto Surface = Document["surface"];
    Archive.SurfaceName = Surface["name"].value_or(Archive.Name + " surface");
    std::array<double, 3> Albedo{0.6, 0.6, 0.6}, Incandescence{0.0, 0.0, 0.0};
    ReadArray(Surface["albedo"], Albedo);
    ReadArray(Surface["incandescence"], Incandescence);
    Archive.Surface = {static_cast<float>(Albedo[0]), static_cast<float>(Albedo[1]), static_cast<float>(Albedo[2]),
                       static_cast<float>(Surface["roughness"].value_or(0.6)),
                       static_cast<float>(Incandescence[0]), static_cast<float>(Incandescence[1]), static_cast<float>(Incandescence[2]),
                       static_cast<float>(Surface["metalness"].value_or(0.0)), static_cast<float>(Surface["coat"].value_or(0.0)),
                       static_cast<float>(Surface["coat_roughness"].value_or(0.0)), static_cast<float>(Surface["specular"].value_or(0.04)),
                       static_cast<float>(Surface["transmission"].value_or(0.0))};

    const auto Distance = Document["distance"];
    Archive.VoxelExtent  = std::clamp(Distance["voxel_m"].value_or(0.03125), 0.005, 1.0);
    Archive.DistancePath = Path.parent_path() / Distance["archive"].value_or(Path.stem().string() + ".sdfspace");
    Archive.CardDensity  = std::clamp(Document["cards"]["density"].value_or(12.0), 1.0, 64.0);

    Archive.Facets = ConstructShapeFacets(Archive);
    std::string Diagnostic;
    if (auto Baked = ReadShapeDistance(Archive.DistancePath, &Diagnostic))
    {
        if (ConfirmLayout(ResolveShapeVolumeLayout(Archive), *Baked)) Archive.Distance = std::move(*Baked);
        else Archive.Diagnostic = Archive.DistancePath.filename().string() + " was baked for another layout; bake it again";
    }
    else
    {
        Archive.Diagnostic = Diagnostic;
    }
    return Archive;
}

std::vector<ShapeArchive> ReadShapeArchives(const std::filesystem::path& Folder, std::vector<std::string>* OutDiagnostics)
{
    std::vector<std::filesystem::path> Paths;
    std::error_code                    Failure;
    for (const auto& Entry : std::filesystem::directory_iterator(Folder, Failure))
        if (Entry.is_regular_file() && Entry.path().extension() == ".toml") Paths.push_back(Entry.path());
    std::sort(Paths.begin(), Paths.end());
    std::vector<ShapeArchive> Archives;
    for (const auto& Path : Paths)
    {
        std::string Diagnostic;
        if (auto Archive = ReadShapeArchive(Path, &Diagnostic)) Archives.push_back(std::move(*Archive));
        else if (OutDiagnostics) OutDiagnostics->push_back(Diagnostic);
    }
    return Archives;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TESSELLATION
//------------------------------------------------------------------------------------------------------------------------

std::vector<ShapeFacet> ConstructShapeFacets(const ShapeArchive& Archive)
{
    std::vector<ShapeFacet> Facets;
    const uint32_t          Around = Archive.Segments;
    switch (Archive.Primitive)
    {
    case ShapePrimitive::Cube:
    case ShapePrimitive::Plane:
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            for (const double Sign : {-1.0, 1.0})
            {
                ShapeVertex Corner[4];
                const int   U = (Axis + 1) % 3, W = (Axis + 2) % 3;
                for (int Index = 0; Index < 4; ++Index)
                {
                    Vector Position{}, Normal{};
                    Position[Axis] = Sign * 0.5;
                    Normal[Axis]   = Sign;
                    Position[U]    = (Index == 1 || Index == 2) ? 0.5 : -0.5;
                    Position[W]    = Index >= 2 ? 0.5 : -0.5;
                    Corner[Index]  = {Position, Normal};
                }
                AppendQuad(Facets, Corner[0], Corner[1], Corner[2], Corner[3]);
            }
        }
        break;
    }
    case ShapePrimitive::Sphere:
    case ShapePrimitive::Torus:
    {
        const bool     Sphere = Archive.Primitive == ShapePrimitive::Sphere;
        const uint32_t Rings  = Archive.Rings;
        // 📝 Torus: outer radius ½, tube radius r = ½ · TubeRatio, ring radius ½ − r; the box is then 1 × 1 × 2r before Size.
        const double Tube = 0.5 * Archive.TubeRatio, Ring = 0.5 - Tube;
        auto Sample = [&](uint32_t I, uint32_t J) {
            const double U = 2.0 * Pi * static_cast<double>(I % Around) / Around;
            const double V = (Sphere ? Pi : 2.0 * Pi) * static_cast<double>(J) / Rings;
            if (Sphere)
            {
                Vector Normal{std::sin(V) * std::cos(U), std::sin(V) * std::sin(U), std::cos(V)};
                if (J == 0 || J == Rings) Normal = {0.0, 0.0, J == 0 ? 1.0 : -1.0};
                return ShapeVertex{Scale(Normal, 0.5), Normal};
            }
            // 📝 In the unit box the tube spans z ±½ (stretched by ½ / r); its normal takes the inverse stretch.
            const Vector Normal{std::cos(V) * std::cos(U), std::cos(V) * std::sin(U), std::sin(V) * 2.0 * Tube};
            const double Radius = Ring + Tube * std::cos(V);
            return ShapeVertex{{Radius * std::cos(U), Radius * std::sin(U), 0.5 * std::sin(V)}, Normal};
        };
        for (uint32_t I = 0; I < Around; ++I)
            for (uint32_t J = 0; J < Rings; ++J) AppendQuad(Facets, Sample(I, J), Sample(I + 1, J), Sample(I + 1, J + 1), Sample(I, J + 1));
        break;
    }
    case ShapePrimitive::Cylinder:
    case ShapePrimitive::Cone:
    {
        const bool Cone = Archive.Primitive == ShapePrimitive::Cone;
        for (uint32_t I = 0; I < Around; ++I)
        {
            const double A = 2.0 * Pi * I / Around, B = 2.0 * Pi * (I + 1) / Around;
            const Vector PA{0.5 * std::cos(A), 0.5 * std::sin(A), -0.5}, PB{0.5 * std::cos(B), 0.5 * std::sin(B), -0.5};
            const double Slope = Cone ? 0.5 : 0.0;
            const Vector NA{std::cos(A), std::sin(A), Slope}, NB{std::cos(B), std::sin(B), Slope};
            AppendTriangle(Facets, {{0.0, 0.0, -0.5}, {0.0, 0.0, -1.0}}, {PA, {0.0, 0.0, -1.0}}, {PB, {0.0, 0.0, -1.0}});
            if (Cone)
            {
                AppendTriangle(Facets, {PA, NA}, {PB, NB}, {{0.0, 0.0, 0.5}, Normalize(Add(NA, NB))});
                continue;
            }
            const Vector TA{PA[0], PA[1], 0.5}, TB{PB[0], PB[1], 0.5};
            AppendQuad(Facets, {PA, NA}, {PB, NB}, {TB, NB}, {TA, NA});
            AppendTriangle(Facets, {{0.0, 0.0, 0.5}, {0.0, 0.0, 1.0}}, {TA, {0.0, 0.0, 1.0}}, {TB, {0.0, 0.0, 1.0}});
        }
        break;
    }
    default:
        break;
    }
    // 📝 The unit shape scaled to its box: positions by Size, normals by its inverse (then renormalized).
    for (ShapeFacet& Facet : Facets)
    {
        for (size_t Corner = 0; Corner < 3; ++Corner)
        {
            for (size_t Axis = 0; Axis < 3; ++Axis)
            {
                Facet.Corners[Corner][Axis] *= Archive.Size[Axis];
                Facet.Normals[Corner][Axis] /= Archive.Size[Axis];
            }
            Facet.Normals[Corner] = Normalize(Facet.Normals[Corner]);
        }
    }
    return Facets;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DISTANCE VOLUME
//------------------------------------------------------------------------------------------------------------------------

ShapeDistanceVolume ResolveShapeVolumeLayout(const ShapeArchive& Archive)
{
    ShapeDistanceVolume Volume;
    double              Voxel  = Archive.VoxelExtent;
    // 📝 As in OccupantDistanceSpace, the margin follows the voxel: a coarsened voxel widens it before the texels are counted.
    auto         Margin  = [&Voxel]() { return std::max(4.0 * Voxel, 0.12); };
    double       Longest = 0.0;
    for (size_t Axis = 0; Axis < 3; ++Axis) Longest = std::max(Longest, Archive.Size[Axis] + 2.0 * Margin());
    if (Longest / Voxel > MaximumVolumeTexels) Voxel = Longest / MaximumVolumeTexels;
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        const double Cells       = std::ceil((Archive.Size[Axis] + 2.0 * Margin()) / Voxel);
        Volume.Resolution[Axis] = static_cast<uint32_t>(std::clamp<double>(Cells, MinimumVolumeTexels, MaximumVolumeTexels));
        Volume.Minimum[Axis]    = -0.5 * Volume.Resolution[Axis] * Voxel;
    }
    Volume.VoxelExtent = Voxel;
    return Volume;
}

ShapeDistanceVolume BakeShapeDistance(const ShapeArchive& Archive)
{
    ShapeDistanceVolume Volume = ResolveShapeVolumeLayout(Archive);
    const auto&         Res    = Volume.Resolution;
    Volume.Distances.assign(static_cast<size_t>(Res[0]) * Res[1] * Res[2], 0.0f);
    const auto& Facets = Archive.Facets;
    auto BakeSlices = [&](uint32_t First, uint32_t Last) {
        for (uint32_t Z = First; Z < Last; ++Z)
            for (uint32_t Y = 0; Y < Res[1]; ++Y)
                for (uint32_t X = 0; X < Res[0]; ++X)
                {
                    const Vector Point{Volume.Minimum[0] + (X + 0.5) * Volume.VoxelExtent,
                                       Volume.Minimum[1] + (Y + 0.5) * Volume.VoxelExtent,
                                       Volume.Minimum[2] + (Z + 0.5) * Volume.VoxelExtent};
                    double Nearest = 1.0e30, Ω = 0.0;
                    for (const ShapeFacet& Facet : Facets)
                    {
                        const Vector Offset =
                            Subtract(Point, ProjectClosestOnFacet(Point, Facet.Corners[0], Facet.Corners[1], Facet.Corners[2]));
                        Nearest = std::min(Nearest, Dot(Offset, Offset));
                        Ω += ResolveSolidAngle(Point, Facet.Corners[0], Facet.Corners[1], Facet.Corners[2]);
                    }
                    const bool Inside = Ω / (4.0 * Pi) > 0.5;
                    const double Signed = Inside ? -std::sqrt(Nearest) : std::sqrt(Nearest);
                    Volume.Distances[(static_cast<size_t>(Z) * Res[1] + Y) * Res[0] + X] = static_cast<float>(Signed);
                }
    };
    const uint32_t           Workers = std::max(1u, std::min(Res[2], std::thread::hardware_concurrency()));
    std::vector<std::thread> Threads;
    for (uint32_t Worker = 0; Worker < Workers; ++Worker)
        Threads.emplace_back(BakeSlices, Res[2] * Worker / Workers, Res[2] * (Worker + 1) / Workers);
    for (auto& Thread : Threads) Thread.join();
    return Volume;
}

bool WriteShapeDistance(const ShapeDistanceVolume& Volume, const std::filesystem::path& Path, std::string* OutDiagnostic)
{
    VolumeDistanceField Field;
    Field.Dimensions = Volume.Resolution;
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        Field.Minimum[Axis] = static_cast<float>(Volume.Minimum[Axis]);
        Field.Maximum[Axis] = static_cast<float>(Volume.Minimum[Axis] + Volume.Resolution[Axis] * Volume.VoxelExtent);
        Field.Step[Axis]    = static_cast<float>(Volume.VoxelExtent);
    }
    Field.Distances       = Volume.Distances;
    Field.Signed          = true;
    Field.ClosedManifold  = true;
    Field.MinimumDistance = Volume.Distances.empty() ? 0.0f : *std::min_element(Volume.Distances.begin(), Volume.Distances.end());
    Field.MaximumDistance = Volume.Distances.empty() ? 0.0f : *std::max_element(Volume.Distances.begin(), Volume.Distances.end());
    return SaveVolumeDistanceSpace(Field, Path, OutDiagnostic);
}

std::optional<ShapeDistanceVolume> ReadShapeDistance(const std::filesystem::path& Path, std::string* OutDiagnostic)
{
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream)
    {
        if (OutDiagnostic) *OutDiagnostic = Path.filename().string() + " is missing; bake the shape archives";
        return std::nullopt;
    }
    const std::vector<uint8_t> Bytes((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
    VolumeDistanceField        Field;
    if (!LoadVolumeDistanceSpaceFromMemory(Bytes, Field, OutDiagnostic)) return std::nullopt;
    ShapeDistanceVolume Volume;
    Volume.Resolution  = Field.Dimensions;
    Volume.VoxelExtent = Field.Step[0];
    for (size_t Axis = 0; Axis < 3; ++Axis) Volume.Minimum[Axis] = Field.Minimum[Axis];
    Volume.Distances = std::move(Field.Distances);
    return Volume;
}

} // namespace Frontier
