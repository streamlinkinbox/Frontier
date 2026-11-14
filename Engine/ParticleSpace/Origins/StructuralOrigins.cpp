/*============================================================================================================================================
                                                         STRUCTURALORIGINS.CPP
============================================================================================================================================*/
// 🧩 Implementation of structural release-origin builders for spiderwebs, cloth grids, botanical stems, and canopy trees

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/StructuralOrigins.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

std::vector<StructuralStrandAnchor> BuildClothGridAnchors(const ClothGridOriginSpec& Spec)
{
    const uint32_t Rows     = std::max(1u, Spec.RowCount);
    const uint32_t Segments = std::clamp(Spec.SegmentCount, 2u, 20u);
    const float    StepLen  = Spec.HeightMeters / static_cast<float>(Segments - 1u);

    std::vector<StructuralStrandAnchor> Strands {};
    Strands.reserve(Rows);

    for (uint32_t Row = 0u; Row < Rows; ++Row)
    {
        const float U = (Rows > 1u) ? (static_cast<float>(Row) / static_cast<float>(Rows - 1u)) : 0.5f;
        StructuralStrandAnchor Entry {};
        Entry.StrandIndex       = Row;
        Entry.VertexCount       = Segments;
        Entry.RestSegmentLength = StepLen;
        Entry.RootPosition      = {
            Spec.OriginMeters.X + (U - 0.5f) * Spec.WidthMeters,
            Spec.OriginMeters.Y,
            Spec.OriginMeters.Z,
        };
        Entry.TipDirection = { 0.0f, 0.0f, -1.0f };
        Entry.RootPinned   = true;
        Entry.TipPinned    = false;
        Strands.push_back(Entry);
    }
    return Strands;
}

std::vector<StructuralStrandAnchor> BuildBotanicalAnchors(const BotanicalRootOriginSpec& Spec)
{
    const uint32_t Stems    = std::max(1u, Spec.StemCount);
    const uint32_t Segments = std::clamp(Spec.SegmentCount, 2u, 20u);
    const float    StepLen  = Spec.HeightMeters / static_cast<float>(Segments - 1u);

    std::vector<StructuralStrandAnchor> Strands {};
    Strands.reserve(Stems);

    for (uint32_t Index = 0u; Index < Stems; ++Index)
    {
        const float R   = Spec.PatchRadius * std::sqrt(HashOriginScalar(Index, 101u));
        const float Phi = Constants::TauFloat * HashOriginScalar(Index, 203u);

        StructuralStrandAnchor Entry {};
        Entry.StrandIndex       = Index;
        Entry.VertexCount       = Segments;
        Entry.RestSegmentLength = StepLen * (0.75f + 0.5f * HashOriginScalar(Index, 307u));
        Entry.RootPosition      = {
            Spec.OriginMeters.X + R * std::cos(Phi),
            Spec.OriginMeters.Y + R * std::sin(Phi),
            Spec.OriginMeters.Z,
        };
        Entry.TipDirection = {
            0.12f * (HashOriginScalar(Index, 401u) - 0.5f),
            0.12f * (HashOriginScalar(Index, 503u) - 0.5f),
            1.0f,
        };
        Entry.TipDirection = Mathematics::NormalizeAxis(Entry.TipDirection, { 0.0f, 0.0f, 1.0f });
        Entry.RootPinned   = true;
        Entry.TipPinned    = false;
        Strands.push_back(Entry);
    }
    return Strands;
}

std::vector<StructuralStrandAnchor> BuildCanopySkeletonAnchors(const CanopySkeletonOriginSpec& Spec)
{
    const uint32_t Branches = std::max(1u, Spec.BranchCount);
    const uint32_t Segments = std::clamp(Spec.SegmentCount, 2u, 20u);

    std::vector<StructuralStrandAnchor> Strands {};
    Strands.reserve(Branches);

    for (uint32_t Index = 0u; Index < Branches; ++Index)
    {
        const float Azimuth  = Constants::TauFloat * (static_cast<float>(Index) / static_cast<float>(Branches));
        const float LiftZ    = 0.45f + 0.5f * HashOriginScalar(Index, 601u);
        const float BranchL  = Spec.CrownSpread * (0.65f + 0.5f * HashOriginScalar(Index, 709u));

        StructuralStrandAnchor Entry {};
        Entry.StrandIndex       = Index;
        Entry.VertexCount       = Segments;
        Entry.RestSegmentLength = BranchL / static_cast<float>(Segments - 1u);
        Entry.RootPosition      = {
            Spec.OriginMeters.X,
            Spec.OriginMeters.Y,
            Spec.OriginMeters.Z + Spec.CanopyHeight * (0.35f + 0.35f * HashOriginScalar(Index, 809u)),
        };
        Entry.TipDirection = Mathematics::NormalizeAxis(
            { std::cos(Azimuth), std::sin(Azimuth), LiftZ },
            { 0.0f, 0.0f, 1.0f });
        Entry.RootPinned = true;
        Entry.TipPinned  = false;
        Strands.push_back(Entry);
    }
    return Strands;
}

std::vector<StructuralBondLink> BuildSpiderwebAnchors(
    const SpiderwebOriginSpec&        Spec,
    std::vector<Mathematics::Axis3D>& OutPositions)
{
    OutPositions.clear();
    std::vector<StructuralBondLink> Bonds {};

    const uint32_t Spokes = std::max(4u, Spec.SpokeCount);
    const uint32_t Rings  = std::max(2u, Spec.RingCount);

    if (Spec.WebMode == 2u)
    {
        // Messy attic cobweb anchor mesh between radial/perimeter points
        const uint32_t AnchorCount = Spokes * Rings;
        OutPositions.reserve(AnchorCount);
        for (uint32_t Index = 0u; Index < AnchorCount; ++Index)
        {
            const float U = HashOriginScalar(Index, 17u) * 2.0f - 1.0f;
            const float V = HashOriginScalar(Index, 53u) * 2.0f - 1.0f;
            const float W = HashOriginScalar(Index, 97u) * 2.0f - 1.0f;
            OutPositions.push_back({
                Spec.OriginMeters.X + U * Spec.RadiusMeters,
                Spec.OriginMeters.Y + V * Spec.RadiusMeters * 0.45f,
                Spec.OriginMeters.Z + W * Spec.RadiusMeters * 0.85f,
            });
        }
        for (uint32_t Index = 0u; Index + 1u < AnchorCount; ++Index)
        {
            const uint32_t Partner = (Index + 1u + static_cast<uint32_t>(HashOriginScalar(Index, 131u) * 5.0f)) % AnchorCount;
            if (Partner != Index)
            {
                const float Dx = OutPositions[Partner].X - OutPositions[Index].X;
                const float Dy = OutPositions[Partner].Y - OutPositions[Index].Y;
                const float Dz = OutPositions[Partner].Z - OutPositions[Index].Z;
                const float Len = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
                if (Len < Spec.RadiusMeters * 1.15f)
                {
                    Bonds.push_back({ Index, Partner, Len, HashOriginScalar(Index, 211u) });
                }
            }
        }
        return Bonds;
    }

    // Orb-Weave radial spokes + capture spiral (+Z Up vertical XZ web plane)
    OutPositions.push_back(Spec.OriginMeters);
    for (uint32_t Ring = 1u; Ring <= Rings; ++Ring)
    {
        const float RadialT = static_cast<float>(Ring) / static_cast<float>(Rings);
        for (uint32_t Spoke = 0u; Spoke < Spokes; ++Spoke)
        {
            const float Angle = Constants::TauFloat * (static_cast<float>(Spoke) / static_cast<float>(Spokes));
            const float R     = Spec.RadiusMeters * (0.14f + 0.86f * RadialT);
            const float Droop = -0.14f * (1.0f - RadialT) * RadialT * Spec.RadiusMeters;
            OutPositions.push_back({
                Spec.OriginMeters.X + std::cos(Angle) * R,
                Spec.OriginMeters.Y + 0.03f * std::sin(Angle * 2.0f),
                Spec.OriginMeters.Z + std::sin(Angle) * R + Droop,
            });
        }
    }

    auto ComputeLen = [&](uint32_t A, uint32_t B) noexcept -> float {
        const float Dx = OutPositions[B].X - OutPositions[A].X;
        const float Dy = OutPositions[B].Y - OutPositions[A].Y;
        const float Dz = OutPositions[B].Z - OutPositions[A].Z;
        return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    };

    // Radial spoke bonds
    for (uint32_t Spoke = 0u; Spoke < Spokes; ++Spoke)
    {
        const uint32_t FirstRingVertex = 1u + Spoke;
        Bonds.push_back({ 0u, FirstRingVertex, ComputeLen(0u, FirstRingVertex), 0.35f });
        for (uint32_t Ring = 1u; Ring < Rings; ++Ring)
        {
            const uint32_t Curr = 1u + (Ring - 1u) * Spokes + Spoke;
            const uint32_t Next = 1u + Ring * Spokes + Spoke;
            Bonds.push_back({ Curr, Next, ComputeLen(Curr, Next), 0.45f });
        }
    }

    // Concentric capture spiral bonds
    for (uint32_t Ring = 1u; Ring <= Rings; ++Ring)
    {
        const float Dew = static_cast<float>(Ring) / static_cast<float>(Rings);
        for (uint32_t Spoke = 0u; Spoke < Spokes; ++Spoke)
        {
            const uint32_t Curr = 1u + (Ring - 1u) * Spokes + Spoke;
            const uint32_t NextSpoke = (Spoke + 1u) % Spokes;
            const uint32_t NextRing  = (NextSpoke == 0u && Ring < Rings) ? (Ring + 1u) : Ring;
            const uint32_t Next      = 1u + (NextRing - 1u) * Spokes + NextSpoke;
            if (Next < OutPositions.size())
            {
                Bonds.push_back({ Curr, Next, ComputeLen(Curr, Next), Dew });
            }
        }
    }

    return Bonds;
}

} // namespace Frontier::ParticleSpace
