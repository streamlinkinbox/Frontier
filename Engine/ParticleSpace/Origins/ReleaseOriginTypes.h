/*============================================================================================================================================
                                                          RELEASEORIGINTYPES.H
============================================================================================================================================*/
// 🧩 Modular typed particle release-origin categories and std::variant descriptors (analytic, structural, surface anchor, firework patterns)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ReleaseOriginCategory : uint8_t
{
    Point           = 0u,  // [-] - Single spatial point origin
    SphereVolume    = 1u,  // [-] - Uniform volumetric or shell sphere
    Disc            = 2u,  // [-] - Horizontal or normal-oriented circular disc
    BoxVolume       = 3u,  // [-] - Axis-aligned box half-extents volume
    SurfaceLattice  = 4u,  // [-] - MoGraph halftone / height-extruded lattice grid
    DualElectrode   = 5u,  // [-] - Twin cathode/anode endpoints for beams and arcs
    SpiderwebWeave  = 6u,  // [-] - Radial spoke + capture spiral orb-weave or attic cobweb
    ClothGrid       = 7u,  // [-] - Anchored woven cloth / cable / bead / drape rows x segments
    BotanicalRoot   = 8u,  // [-] - Grounded grass, reeds, fern, kelp, or sea-anemone root anchors
    CanopySkeleton  = 9u,  // [-] - Branched tree trunk/bough anchor skeleton
    SurfaceAnchor   = 10u, // [-] - Area-weighted procedural (0..15) or PolygonNetwork (16) surface
    FireworkPattern = 11u, // [-] - Multi-shell pyrotechnic & New Year sky-text/object shells (0..9)
};

enum class SurfacePresetCategory : uint8_t
{
    FloorMatrixXZ          = 0u,  // [-] - XZ horizontal floor lattice
    WallMatrixXY           = 1u,  // [-] - XY vertical wall lattice
    ObsidianSphere         = 2u,  // [-] - 3D sphere manifold
    ObsidianHandChromeBall = 3u,  // [-] - Sculpted hand cradling chrome sphere
    TorusRing              = 4u,  // [-] - 3D torus ring
    CubicVolume            = 5u,  // [-] - 3D volumetric cubic grid
    UtahTeapot             = 6u,  // [-] - Classic parametric Utah teapot
    SportsCarSilhouette    = 7u,  // [-] - Sculpted aerodynamic sports car hull
    SculptedHeadBust       = 8u,  // [-] - Classic sculpted head/bust silhouette
    TorusKnot32            = 9u,  // [-] - (3, 2) trefoil torus knot tube
    PrecisionGearCog       = 10u, // [-] - Toothed mechanical gear
    QuartzCrystalCluster   = 11u, // [-] - Clustered hexagonal quartz spires
    ArchitecturalArchway   = 12u, // [-] - Classical vaulted archway
    BevelledMonolithCube   = 13u, // [-] - Chamfered architectural monolith
    FacetedSpireCone       = 14u, // [-] - Faceted crystalline spire
    StellatedStarPolyhedron= 15u, // [-] - Stellated dodecahedral star
    CustomPolygonNetwork   = 16u, // [-] - Imported external PolygonNetwork / OBJ / STL
    Count                  = 17u, // [count] - Total surface presets
};

enum class SurfaceSampleMode : uint8_t
{
    EvenSurface           = 0u, // [-] - Area-weighted uniform surface sampling
    SurfaceInteriorVolume = 1u, // [-] - Blended surface + interior volumetric anchors
    RidgeCreaseAccent     = 2u, // [-] - Perimeter/ridge-weighted crease emphasis
};

enum class FireworkShapeCategory : uint8_t
{
    Bouquet     = 0u, // [-] - Grand Bouquet multi-shell pyro
    SkyText     = 1u, // [-] - Sky Text custom UTF-8 message
    Heart       = 2u, // [-] - Glowing 3D heart shell
    Star        = 3u, // [-] - 5-point star shell
    Smiley      = 4u, // [-] - Hanabi smiley face shell
    Saturn      = 5u, // [-] - Saturn ringed planet shell
    Crown       = 6u, // [-] - Royal crown shell
    Butterfly   = 7u, // [-] - Iridescent butterfly shell
    Spiral      = 8u, // [-] - Spiral galaxy shell
    GrandFinale = 9u, // [-] - Cycling multi-shape New Year finale
    Count       = 10u,// [count] - Total firework shape patterns
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PointOriginSpec
{
    Mathematics::Axis3D OriginMeters { 0.0f, 0.0f, 0.5f }; // [m] - Release anchor position
};

struct SphereVolumeOriginSpec
{
    Mathematics::Axis3D OriginMeters { 0.0f, 0.0f, 1.2f }; // [m] - Sphere centre
    float               RadiusMeters = 0.5f;               // [m] - Release radius
    bool                ShellOnly    = false;              // [-] - True to sample surface shell only
};

struct DiscOriginSpec
{
    Mathematics::Axis3D OriginMeters { 0.0f, 0.0f, 0.2f }; // [m] - Disc centre
    Mathematics::Axis3D NormalAxis   { 0.0f, 0.0f, 1.0f }; // [-] - Unit plane normal (+Z Up)
    float               RadiusMeters = 1.0f;               // [m] - Disc radius
};

struct BoxVolumeOriginSpec
{
    Mathematics::Axis3D OriginMeters     { 0.0f, 0.0f, 1.0f }; // [m] - Box centre
    Mathematics::Axis3D HalfExtentsMeters{ 1.0f, 1.0f, 0.5f }; // [m] - Half extents
};

struct SurfaceLatticeOriginSpec
{
    Mathematics::Axis3D OriginMeters { 0.0f, 0.0f, 0.0f }; // [m]     - Lattice centre
    uint32_t            Columns      = 24u;                // [count] - Lattice column divisions
    uint32_t            Rows         = 24u;                // [count] - Lattice row divisions
    float               ExtentMeters = 3.0f;               // [m]     - Grid half-extent
};

struct DualElectrodeOriginSpec
{
    Mathematics::Axis3D CathodeMeters  { -2.0f, 0.0f, 1.2f }; // [m] - Start terminal
    Mathematics::Axis3D AnodeMeters    {  2.0f, 0.0f, 1.2f }; // [m] - Target terminal
    float               SpreadRadius   = 0.08f;              // [m] - Cathode jitter radius
    float               TargetRadius   = 0.25f;              // [m] - Anode / target sphere radius
    uint32_t            ArcMode        = 0u;                 // [-] - 0 off, 1 lightning arc, 2 plasma cage, 3 web
    bool                ShowElectrodes = false;              // [-] - Render cathode/anode spheres
};

struct SpiderwebOriginSpec
{
    Mathematics::Axis3D OriginMeters { 0.0f, 0.0f, 1.6f }; // [m]     - Web hub centre
    uint32_t            WebMode      = 1u;                 // [-]     - 1 Orb-Weave, 2 Attic Cobweb
    uint32_t            SpokeCount   = 18u;                // [count] - Radial spoke count
    uint32_t            RingCount    = 14u;                // [count] - Capture spiral turn count
    float               RadiusMeters = 2.2f;               // [m]     - Outer frame radius
};

struct ClothGridOriginSpec
{
    Mathematics::Axis3D OriginMeters  { 0.0f, 0.0f, 2.4f }; // [m]     - Top suspension centre
    uint32_t            RowCount      = 28u;                // [count] - Strand / strip row count
    uint32_t            SegmentCount  = 16u;                // [count] - Vertices per strand (2..20)
    float               WidthMeters   = 2.6f;               // [m]     - Total horizontal span
    float               HeightMeters  = 2.4f;               // [m]     - Drop or mast height
};

struct BotanicalRootOriginSpec
{
    Mathematics::Axis3D OriginMeters  { 0.0f, 0.0f, 0.0f }; // [m]     - Patch centre
    uint32_t            StemCount     = 28u;                // [count] - Stalk / frond / tentacle count
    uint32_t            SegmentCount  = 16u;                // [count] - Segments per stem
    float               PatchRadius   = 2.4f;               // [m]     - Ground patch radius
    float               HeightMeters  = 1.4f;               // [m]     - Nominal stalk height
};

struct CanopySkeletonOriginSpec
{
    Mathematics::Axis3D OriginMeters  { 0.0f, 0.0f, 0.0f }; // [m]     - Trunk base
    uint32_t            BranchCount   = 28u;                // [count] - Primary/secondary boughs
    uint32_t            SegmentCount  = 16u;                // [count] - Segments per branch
    float               CanopyHeight  = 2.8f;               // [m]     - Crown height
    float               CrownSpread   = 1.8f;               // [m]     - Canopy lateral radius
};

struct SurfaceAnchorOriginSpec
{
    SurfacePresetCategory SurfacePreset      = SurfacePresetCategory::FloorMatrixXZ; // [-]   - 0..16 surface selector
    SurfaceSampleMode     SampleMode         = SurfaceSampleMode::EvenSurface;       // [-]   - Sampling distribution
    Mathematics::Axis3D   OriginMeters       { 0.0f, 0.0f, 0.0f };                   // [m]   - World anchor offset
    Mathematics::Axis3D   EulerRotationDeg   { 0.0f, 0.0f, 0.0f };                   // [deg] - Surface Euler angles
    float                 ScaleMeters        = 1.5f;                                 // [m]   - Uniform surface scale
    uint32_t              Columns            = 24u;                                  // [count]
    uint32_t              Rows               = 24u;                                  // [count]
    std::string           CustomAssetIdentifier {};                                  // [-]   - Runtime custom asset ID
};

struct FireworkPatternOriginSpec
{
    FireworkShapeCategory Shape        = FireworkShapeCategory::Bouquet; // [-] - 0..9 shell pattern
    Mathematics::Axis3D   OriginMeters { 0.0f, 0.0f, 0.2f };             // [m] - Launch mortar pad
    std::string           SkyText      { "2026" };                       // [-] - UTF-8 message for SkyText/GrandFinale
    float                 PatternScale = 2.6f;                           // [m] - Burst pattern radius
    uint32_t              ShellMode    = 1u;                             // [-] - 1 multi-stage pyro, 2 spectacle
};

using ReleaseOriginSpec = std::variant<
    PointOriginSpec,
    SphereVolumeOriginSpec,
    DiscOriginSpec,
    BoxVolumeOriginSpec,
    SurfaceLatticeOriginSpec,
    DualElectrodeOriginSpec,
    SpiderwebOriginSpec,
    ClothGridOriginSpec,
    BotanicalRootOriginSpec,
    CanopySkeletonOriginSpec,
    SurfaceAnchorOriginSpec,
    FireworkPatternOriginSpec>;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr ReleaseOriginCategory QueryReleaseOriginCategory(const ReleaseOriginSpec& Origin) noexcept
{
    return static_cast<ReleaseOriginCategory>(Origin.index());
}

[[nodiscard]] std::string_view      QueryReleaseOriginCategoryToken(ReleaseOriginCategory Category) noexcept;
[[nodiscard]] ReleaseOriginCategory ParseReleaseOriginCategoryToken(std::string_view Token) noexcept;
[[nodiscard]] std::string_view      QuerySurfacePresetLabel(SurfacePresetCategory Preset) noexcept;
[[nodiscard]] std::string_view      QueryFireworkShapeLabel(FireworkShapeCategory Shape) noexcept;

} // namespace Frontier::ParticleSpace
