/*==========================================================================================================================================
                                                              SHAPEARCHIVE.H
==========================================================================================================================================*/
// 🧩 Engine shapes: the Project Zero editor's construct primitives (cube, sphere, cylinder, cone, torus, plane) as
//    EngineContent/ShapeArchives/<Name>.toml, their facets tessellated from it and their signed distance volume baked to <Name>.sdfspace.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The base geometries of the Project Zero editor's Construct › Geometry (ConstructWorld.cpp ConstructKind), unit sized and
//    centred on the origin (z up). The plane is a 1 cm slab, so its distance volume has an inside like every other shape.
enum class ShapePrimitive : uint8_t
{
    Cube,
    Sphere,
    Cylinder,
    Cone,
    Torus,
    Plane,
    Count
};

// 📝 One triangle in the shape's local frame with its vertex normals (smooth on curved faces, flat on caps and planar faces).
struct ShapeFacet
{
    std::array<std::array<double, 3>, 3> Corners{};   // [m]
    std::array<std::array<double, 3>, 3> Normals{};   // [-] unit
};

// 📝 A signed distance volume in the shape's local frame: texel (i, j, k) is centred at Minimum + (i + ½, j + ½, k + ½) ·
//    VoxelExtent, x fastest; negative inside. The layout is the one OccupantDistanceSpace chooses for a facet set (voxel, margin,
//    8–128 texels).
struct ShapeDistanceVolume
{
    std::array<uint32_t, 3> Resolution{};        // [texel]
    std::array<double, 3>   Minimum{};           // [m] local corner of texel (0, 0, 0)
    double                  VoxelExtent = 0.0;   // [m]
    std::vector<float>      Distances{};         // [m]
};

// 📝 One EngineContent/ShapeArchives/<Name>.toml: the shape, its tessellation, its surface (MaterialSpecification channels), the
//    distance volume's voxel and the surface cards' density; Facets is generated from it, Distance read from the baked .sdfspace.
struct ShapeArchive
{
    std::string             Name{};                       // [-] "Cube"
    std::string             Description{};
    std::string             GlyphName{};                  // [-] GlyphSymbol name for the Outliner and the Construct tiles
    ShapePrimitive          Primitive = ShapePrimitive::Cube;
    std::array<double, 3>   Size{1.0, 1.0, 1.0};          // [m] bounding box of the shape
    uint32_t                Segments  = 32;               // [-] around the axis (sphere, cylinder, cone, torus)
    uint32_t                Rings     = 16;               // [-] pole to pole (sphere), around the tube (torus)
    double                  TubeRatio = 0.3;              // [-] torus tube radius over the torus outer radius
    std::string             SurfaceName{};                // [-] the material family's name
    // 📝 MaterialSpecification channels: albedo rgb, roughness, incandescence rgb, metalness, coat, coat roughness, specular,
    //    transmission.
    std::array<float, 12>   Surface{};
    double                  VoxelExtent  = 0.03125;       // [m] distance volume voxel
    double                  CardDensity  = 12.0;          // [texel/m] surface cards
    std::filesystem::path   Source{};                     // [-] the .toml
    std::filesystem::path   DistancePath{};               // [-] the baked .sdfspace beside it
    std::vector<ShapeFacet> Facets{};
    std::optional<ShapeDistanceVolume> Distance{};        // [-] unset: missing or stale bake (the device transfers the volume)
    std::string             Diagnostic{};                 // [-] why Distance is unset
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ARCHIVES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Reads one shape archive and tessellates it; the baked volume is attached when it matches the layout the archive implies.
[[nodiscard]] std::optional<ShapeArchive> ReadShapeArchive(const std::filesystem::path& Path, std::string* OutDiagnostic = nullptr);

// 📝 Every *.toml of a folder, in name order; unreadable archives are reported and skipped.
[[nodiscard]] std::vector<ShapeArchive> ReadShapeArchives(const std::filesystem::path& Folder,
                                                          std::vector<std::string>*    OutDiagnostics = nullptr);

[[nodiscard]] const char* NameShapePrimitive(ShapePrimitive Primitive) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TESSELLATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The Project Zero editor's tessellation (ConstructWorld.cpp Build) scaled to Size: a unit cube of 12 triangles, a 32 × 16 UV sphere,
//    32-sided cylinder and cone, a 32 × 12 torus and the plane slab.
[[nodiscard]] std::vector<ShapeFacet> ConstructShapeFacets(const ShapeArchive& Archive);

//------------------------------------------------------------------------------------------------------------------------
//                                                    DISTANCE VOLUME
//------------------------------------------------------------------------------------------------------------------------

// 📝 The volume layout of OccupantDistanceSpace::ConstructVolumes for these bounds: voxel, a margin of max(4 voxels, 0.12 m) on every
//    side, 8–128 texels per axis (a longer shape takes a coarser voxel).
[[nodiscard]] ShapeDistanceVolume ResolveShapeVolumeLayout(const ShapeArchive& Archive);

// 📝 Exact distance to the facets at every texel centre; the sign from the generalized winding number (inside above ½), exact for these
//    closed surfaces.
[[nodiscard]] ShapeDistanceVolume BakeShapeDistance(const ShapeArchive& Archive);

bool WriteShapeDistance(const ShapeDistanceVolume& Volume, const std::filesystem::path& Path, std::string* OutDiagnostic = nullptr);
[[nodiscard]] std::optional<ShapeDistanceVolume> ReadShapeDistance(const std::filesystem::path& Path,
                                                                   std::string*                 OutDiagnostic = nullptr);

} // namespace Frontier
