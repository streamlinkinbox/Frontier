/*============================================================================================================================================
                                                          FACETAREAPACKET.H
============================================================================================================================================*/
// 🧩 Prepare explicit facet support and accumulate four fresh diffuse RIS or raw lanes without renderer activation.
#pragma once
#include "FacetAreaReservoir.h"
#include "FacetAreaTransport.h"
#include <cmath>
#include <utility>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                  ESTIMATOR CONTRACT
//------------------------------------------------------------------------------------------------------------------------

enum class FacetPacketEstimator : uint32_t
{
    Ris  = 0,
    Raw4 = 1,
    Raw8 = 2
};

struct FacetPacketEstimate
{
    bool Accepted = false;
    FacetAreaSurface Primary;
    std::array<float, 3> Rgb{}, OmitConnection{}, OmitSuffix{}, OmitBoth{};
    uint32_t CandidateCount = 0, PathCount = 0, VisibilityCount = 0;
};

class FacetAreaPacket
{
    FacetAreaTransport Transport;
    FacetAreaProposals Proposals;
    std::vector<FacetAreaShadingTriangle> Surfaces;
    std::vector<uint32_t> SourceOrdinals;
    bool Prepared = false;

  public:
    // Cold-path allocation;failure preserves the previous valid configuration. Ordinals address packed surfaces.
    bool Prepare(const std::vector<FacetAreaShadingTriangle> &ShadingSurfaces,
                 const TriangleVisibilitySpace::FloatAtlas &Packed, const std::vector<uint32_t> &SourceProfile)
    {
        if (SourceProfile.empty())
            return false;
        std::vector<FacetAreaTriangle> SourceTriangles;
        std::vector<bool> Included(ShadingSurfaces.size(), false);
        for (uint32_t Ordinal : SourceProfile)
        {
            if (Ordinal >= ShadingSurfaces.size() || Included[Ordinal])
                return false;
            Included[Ordinal] = true;
            SourceTriangles.push_back(ShadingSurfaces[Ordinal].Triangle);
        }
        FacetAreaTransport PreparedTransport;
        FacetAreaProposals PreparedProposals;
        if (!PreparedTransport.Prepare(ShadingSurfaces, Packed) || !PreparedProposals.Prepare(SourceTriangles))
            return false;
        // Allocate every owned copy before replacing any part of the live configuration.
        auto PreparedSurfaces = ShadingSurfaces;
        auto PreparedOrdinals = SourceProfile;
        Transport             = std::move(PreparedTransport);
        Proposals             = std::move(PreparedProposals);
        Surfaces              = std::move(PreparedSurfaces);
        SourceOrdinals        = std::move(PreparedOrdinals);
        Prepared              = true;
        return true;
    }

    // Hot path allocates nothing. Misses/invalid input/unrepresentable arithmetic return an empty rejected estimate.
    // SourceProfile defines the entire integration support;omitted scene surfaces are not implicitly sampled.
    [[nodiscard]] FacetPacketEstimate Evaluate(
        const std::array<float, 3> &Origin,
        const std::array<float, 3> &Direction,
        uint32_t Seed,
        FacetPacketEstimator Estimator   = FacetPacketEstimator::Ris,
        FacetAreaMeasureCategory Measure = FacetAreaMeasureCategory::UniformFacet) const noexcept
    {
        if (!Prepared || uint32_t(Estimator) > 2 || uint32_t(Measure) > 1)
            return {};
        FacetPacketEstimate Estimate;
        Estimate.Primary = Transport.Primary(Origin, Direction);
        if (!Estimate.Primary.Accepted)
            return {};
        for (uint32_t Lane = 0; Lane < 4; ++Lane)
        {
            const uint32_t CandidateCount = Estimator == FacetPacketEstimator::Raw4 ? 1 : 2;
            std::array<FacetAreaProposal, 2> Candidates{};
            std::array<FacetAreaScore, 2> Scores{};
            for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
            {
                auto &Proposal = Candidates[Ordinal];
                Proposal       = Proposals.Draw(Seed, 2 * Lane + Ordinal, Measure);
                if (!Proposal.Accepted || Proposal.Index >= SourceOrdinals.size())
                    return {};
                Proposal.Index    = SourceOrdinals[Proposal.Index];
                const auto Source = Transport.Source(Proposal);
                if (!Source.Accepted)
                    return {};
                FacetAreaScoreInput Input;
                Input.Triangle       = Surfaces[Proposal.Index].Triangle;
                Input.U              = Proposal.U;
                Input.V              = Proposal.V;
                Input.SourceNormal   = Source.Normal;
                Input.SourceAlbedo   = Source.Albedo;
                Input.Receiver       = Estimate.Primary.Position;
                Input.ReceiverNormal = Estimate.Primary.Normal;
                Input.ReceiverAlbedo = Estimate.Primary.Albedo;
                Scores[Ordinal]      = EvaluateFacetAreaScore(Input);
                if (!Scores[Ordinal].Accepted)
                    return {};
                ++Estimate.CandidateCount;
            }
            uint32_t Selected = 0;
            float Normalizer  = 0;
            if (Estimator == FacetPacketEstimator::Ris)
            {
                const auto Pair = SelectFacetAreaPair(
                    {{{Scores[0].Target, Candidates[0].Density}, {Scores[1].Target, Candidates[1].Density}}},
                    DrawFacetAreaSelectionUniform(Seed, Lane));
                if (!Pair.Accepted || Pair.Selected > 1)
                    return {};
                Selected   = Pair.Selected;
                Normalizer = float(Pair.Normalizer);
            }
            for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
            {
                if (Estimator == FacetPacketEstimator::Ris && Ordinal != Selected)
                    continue;
                const auto Path = Transport.Connect(Candidates[Ordinal], Estimate.Primary);
                if (!Path.Accepted)
                    return {};
                const float Weight =
                    (Estimator == FacetPacketEstimator::Ris ? Normalizer : 1 / Candidates[Ordinal].Density) *
                    (Estimator == FacetPacketEstimator::Raw8 ? .5f : 1.f) / 4;
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    Estimate.Rgb[Axis] += Path.Rgb[Axis] * Weight;
                    Estimate.OmitConnection[Axis] += Path.SuffixOnly[Axis] * Weight;
                    Estimate.OmitSuffix[Axis] += Path.ConnectionOnly[Axis] * Weight;
                    Estimate.OmitBoth[Axis] += Scores[Ordinal].ProxyRgb[Axis] * Weight;
                }
                ++Estimate.PathCount;
                Estimate.VisibilityCount += 2;
            }
        }
        for (const auto &Rgb : {Estimate.Rgb, Estimate.OmitConnection, Estimate.OmitSuffix, Estimate.OmitBoth})
            for (float Channel : Rgb)
                if (!std::isfinite(Channel) || Channel < 0)
                    return {};
        Estimate.Accepted = true;
        return Estimate;
    }
};
} // namespace Frontier
