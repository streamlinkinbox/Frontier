/*============================================================================================================================================
                                                          ANALYTICORIGINS.CPP
============================================================================================================================================*/
// 🧩 Implementation of deterministic analytic release-origin samplers (Point, SphereVolume, Disc, BoxVolume, SurfaceLattice, DualElectrode)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

[[nodiscard]] uint32_t MixHash32(uint32_t Key) noexcept
{
    Key ^= Key >> 16u;
    Key *= 0x7FEB352Du;
    Key ^= Key >> 15u;
    Key *= 0x846CA68Bu;
    Key ^= Key >> 16u;
    return Key;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  TOKEN & ORIGIN SAMPLING
//------------------------------------------------------------------------------------------------------------------------

std::string_view QueryReleaseOriginCategoryToken(ReleaseOriginCategory Category) noexcept
{
    switch (Category)
    {
    case ReleaseOriginCategory::Point:           return "Point";
    case ReleaseOriginCategory::SphereVolume:    return "SphereVolume";
    case ReleaseOriginCategory::Disc:            return "Disc";
    case ReleaseOriginCategory::BoxVolume:       return "BoxVolume";
    case ReleaseOriginCategory::SurfaceLattice:  return "SurfaceLattice";
    case ReleaseOriginCategory::DualElectrode:   return "DualElectrode";
    case ReleaseOriginCategory::SpiderwebWeave:  return "SpiderwebWeave";
    case ReleaseOriginCategory::ClothGrid:       return "ClothGrid";
    case ReleaseOriginCategory::BotanicalRoot:   return "BotanicalRoot";
    case ReleaseOriginCategory::CanopySkeleton:  return "CanopySkeleton";
    case ReleaseOriginCategory::SurfaceAnchor:   return "SurfaceAnchor";
    case ReleaseOriginCategory::FireworkPattern: return "FireworkPattern";
    default:                                     return "Point";
    }
}

ReleaseOriginCategory ParseReleaseOriginCategoryToken(std::string_view Token) noexcept
{
    if (Token == "SphereVolume")    return ReleaseOriginCategory::SphereVolume;
    if (Token == "Disc")            return ReleaseOriginCategory::Disc;
    if (Token == "BoxVolume")       return ReleaseOriginCategory::BoxVolume;
    if (Token == "SurfaceLattice")  return ReleaseOriginCategory::SurfaceLattice;
    if (Token == "DualElectrode")   return ReleaseOriginCategory::DualElectrode;
    if (Token == "SpiderwebWeave")  return ReleaseOriginCategory::SpiderwebWeave;
    if (Token == "ClothGrid")       return ReleaseOriginCategory::ClothGrid;
    if (Token == "BotanicalRoot")   return ReleaseOriginCategory::BotanicalRoot;
    if (Token == "CanopySkeleton")  return ReleaseOriginCategory::CanopySkeleton;
    if (Token == "SurfaceAnchor")   return ReleaseOriginCategory::SurfaceAnchor;
    if (Token == "FireworkPattern") return ReleaseOriginCategory::FireworkPattern;
    return ReleaseOriginCategory::Point;
}

std::string_view QuerySurfacePresetLabel(SurfacePresetCategory Preset) noexcept
{
    switch (Preset)
    {
    case SurfacePresetCategory::FloorMatrixXZ:           return "XZ Floor Matrix";
    case SurfacePresetCategory::WallMatrixXY:            return "XY Wall Matrix";
    case SurfacePresetCategory::ObsidianSphere:          return "3D Obsidian Sphere";
    case SurfacePresetCategory::ObsidianHandChromeBall:  return "3D Obsidian Hand + Chrome Ball";
    case SurfacePresetCategory::TorusRing:               return "3D Torus Ring";
    case SurfacePresetCategory::CubicVolume:             return "3D Cubic Volume";
    case SurfacePresetCategory::UtahTeapot:              return "Utah Teapot";
    case SurfacePresetCategory::SportsCarSilhouette:     return "Sports Car Silhouette";
    case SurfacePresetCategory::SculptedHeadBust:        return "Sculpted Head / Bust";
    case SurfacePresetCategory::TorusKnot32:             return "Torus Knot (3,2)";
    case SurfacePresetCategory::PrecisionGearCog:        return "Precision Gear / Cog";
    case SurfacePresetCategory::QuartzCrystalCluster:    return "Quartz Crystal Cluster";
    case SurfacePresetCategory::ArchitecturalArchway:    return "Architectural Archway";
    case SurfacePresetCategory::BevelledMonolithCube:    return "Bevelled Monolith Cube";
    case SurfacePresetCategory::FacetedSpireCone:        return "Faceted Spire / Cone";
    case SurfacePresetCategory::StellatedStarPolyhedron: return "Stellated Star Polyhedron";
    case SurfacePresetCategory::CustomPolygonNetwork:    return "Custom 3D PolygonNetwork";
    default:                                             return "XZ Floor Matrix";
    }
}

std::string_view QueryFireworkShapeLabel(FireworkShapeCategory Shape) noexcept
{
    switch (Shape)
    {
    case FireworkShapeCategory::Bouquet:     return "Grand Bouquet (Multi-Shell Pyro)";
    case FireworkShapeCategory::SkyText:     return "Sky Text (Custom Message)";
    case FireworkShapeCategory::Heart:       return "Glowing Heart";
    case FireworkShapeCategory::Star:        return "5-Point Star";
    case FireworkShapeCategory::Smiley:      return "Hanabi Smiley Face";
    case FireworkShapeCategory::Saturn:      return "Saturn Ringed Planet";
    case FireworkShapeCategory::Crown:       return "Royal Crown";
    case FireworkShapeCategory::Butterfly:   return "Iridescent Butterfly";
    case FireworkShapeCategory::Spiral:      return "Spiral Galaxy";
    case FireworkShapeCategory::GrandFinale: return "New Year's Grand Finale";
    default:                                 return "Grand Bouquet (Multi-Shell Pyro)";
    }
}

float HashOriginScalar(uint32_t SampleIndex, uint32_t Salt) noexcept
{
    const uint32_t Mixed = MixHash32(SampleIndex * 2654435761u ^ Salt * 2246822519u ^ 0x9E3779B9u);
    return static_cast<float>(Mixed & 0x00FFFFFFu) / 16777216.0f;
}

OriginSamplePoint SampleAnalyticOrigin(
    const ReleaseOriginSpec& Origin,
    uint32_t                 SampleIndex,
    uint32_t                 TotalCount,
    uint32_t                 Seed) noexcept
{
    const uint32_t SafeTotal = std::max(1u, TotalCount);
    const float    U         = HashOriginScalar(SampleIndex, Seed + 11u);
    const float    V         = HashOriginScalar(SampleIndex, Seed + 37u);
    const float    W         = HashOriginScalar(SampleIndex, Seed + 73u);

    if (const auto* Point = std::get_if<PointOriginSpec>(&Origin))
    {
        return { Point->OriginMeters, { 0.0f, 0.0f, 1.0f }, U, V };
    }

    if (const auto* Sphere = std::get_if<SphereVolumeOriginSpec>(&Origin))
    {
        const float ZComp   = 2.0f * U - 1.0f;
        const float Azimuth = Constants::TauFloat * V;
        const float PlanarR = std::sqrt(std::max(0.0f, 1.0f - ZComp * ZComp));
        const Mathematics::Axis3D Dir { PlanarR * std::cos(Azimuth), PlanarR * std::sin(Azimuth), ZComp };
        const float RadialScale = Sphere->ShellOnly ? 1.0f : std::cbrt(std::max(0.0f, W));
        const float R           = Sphere->RadiusMeters * RadialScale;
        return {
            { Sphere->OriginMeters.X + Dir.X * R, Sphere->OriginMeters.Y + Dir.Y * R, Sphere->OriginMeters.Z + Dir.Z * R },
            Dir,
            U,
            V,
        };
    }

    if (const auto* Disc = std::get_if<DiscOriginSpec>(&Origin))
    {
        const Mathematics::Axis3D N    = Mathematics::NormalizeAxis(Disc->NormalAxis, { 0.0f, 0.0f, 1.0f });
        const Mathematics::Axis3D Ref  = (std::abs(N.Z) < 0.95f) ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f } : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D TanU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Ref, N), { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D TanV = Mathematics::EvaluateCross(N, TanU);
        const float               R    = Disc->RadiusMeters * std::sqrt(std::max(0.0f, U));
        const float               Phi  = Constants::TauFloat * V;
        const float               Cp   = std::cos(Phi) * R;
        const float               Sp   = std::sin(Phi) * R;
        return {
            {
                Disc->OriginMeters.X + TanU.X * Cp + TanV.X * Sp,
                Disc->OriginMeters.Y + TanU.Y * Cp + TanV.Y * Sp,
                Disc->OriginMeters.Z + TanU.Z * Cp + TanV.Z * Sp,
            },
            N,
            U,
            V,
        };
    }

    if (const auto* Box = std::get_if<BoxVolumeOriginSpec>(&Origin))
    {
        return {
            {
                Box->OriginMeters.X + (2.0f * U - 1.0f) * Box->HalfExtentsMeters.X,
                Box->OriginMeters.Y + (2.0f * V - 1.0f) * Box->HalfExtentsMeters.Y,
                Box->OriginMeters.Z + (2.0f * W - 1.0f) * Box->HalfExtentsMeters.Z,
            },
            { 0.0f, 0.0f, 1.0f },
            U,
            V,
        };
    }

    if (const auto* Lattice = std::get_if<SurfaceLatticeOriginSpec>(&Origin))
    {
        const uint32_t Cols = std::max(1u, Lattice->Columns);
        const uint32_t Rows = std::max(1u, Lattice->Rows);
        const uint32_t Cell = SampleIndex % (Cols * Rows);
        const uint32_t Col  = Cell % Cols;
        const uint32_t Row  = Cell / Cols;
        const float    Cu   = (Cols > 1u) ? (static_cast<float>(Col) / static_cast<float>(Cols - 1u)) : 0.5f;
        const float    Cv   = (Rows > 1u) ? (static_cast<float>(Row) / static_cast<float>(Rows - 1u)) : 0.5f;
        return {
            {
                Lattice->OriginMeters.X + (Cu * 2.0f - 1.0f) * Lattice->ExtentMeters,
                Lattice->OriginMeters.Y + (Cv * 2.0f - 1.0f) * Lattice->ExtentMeters,
                Lattice->OriginMeters.Z,
            },
            { 0.0f, 0.0f, 1.0f },
            Cu,
            Cv,
        };
    }

    if (const auto* Dual = std::get_if<DualElectrodeOriginSpec>(&Origin))
    {
        const float T = static_cast<float>(SampleIndex) / static_cast<float>(std::max(1u, SafeTotal - 1u));
        return {
            {
                Dual->CathodeMeters.X + (Dual->AnodeMeters.X - Dual->CathodeMeters.X) * T,
                Dual->CathodeMeters.Y + (Dual->AnodeMeters.Y - Dual->CathodeMeters.Y) * T,
                Dual->CathodeMeters.Z + (Dual->AnodeMeters.Z - Dual->CathodeMeters.Z) * T,
            },
            { 0.0f, 0.0f, 1.0f },
            T,
            0.5f,
        };
    }

    return { { 0.0f, 0.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, U, V };
}

} // namespace Frontier::ParticleSpace
