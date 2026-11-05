/*==========================================================================================================================================
                                                         XRAYSCANEDGES.CPP
==========================================================================================================================================*/

#include "XRayScanEdges.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>

namespace Frontier::XRayScan
{

namespace
{

using WeldedCorner = std::array<int64_t, 3>;
using EdgeKey      = std::array<int64_t, 6>;

// 📝 Math.round: the nearest integer, a tie toward +∞ (not the C round, which sends it away from zero).
int64_t RoundHalfUp(double Value)
{
    double Lower = std::floor(Value);
    if (Value - Lower >= 0.5)
    {
        Lower += 1.0;
    }
    return static_cast<int64_t>(Lower);
}

struct PendingEdge
{
    std::array<double, 3> From{};
    std::array<double, 3> To{};
    std::array<double, 3> Normal{};
};

} // namespace

double ResolveHypot(double X, double Y, double Z)
{
    const std::array<double, 3> Magnitudes = {std::abs(X), std::abs(Y), std::abs(Z)};
    double Largest = 0.0;
    for (double Magnitude : Magnitudes)
    {
        if (Magnitude > Largest) Largest = Magnitude;
    }
    if (Largest == std::numeric_limits<double>::infinity()) return Largest;
    if (Largest == 0.0) return 0.0;
    double Sum          = 0.0;
    double Compensation = 0.0;
    for (double Magnitude : Magnitudes)
    {
        const double Scaled      = Magnitude / Largest;
        const double Summand     = Scaled * Scaled - Compensation;
        const double Preliminary = Sum + Summand;
        Compensation = (Preliminary - Sum) - Summand;
        Sum          = Preliminary;
    }
    return std::sqrt(Sum) * Largest;
}

std::vector<double> ExtractFeatureEdges(std::span<const ScanFacet> Facets, double ThresholdDegrees)
{
    const double ThresholdDot = std::cos(ThresholdDegrees * 3.14159265358979323846 / 180.0);
    // 📝 JavaScript's Map keeps insertion order and a consumed edge stays in its place as null: Order holds the edges as they were first seen,
    //    Index finds an edge by its welded key, and an empty slot is a consumed (null) edge.
    std::map<EdgeKey, size_t>               Index;
    std::vector<std::optional<PendingEdge>> Order;
    std::vector<double>                     Segments;
    for (const ScanFacet& Facet : Facets)
    {
        const auto& A = Facet.Corners[0];
        const auto& B = Facet.Corners[1];
        const auto& C = Facet.Corners[2];
        // 📝 THREE.Triangle.getNormal: (c − b) × (a − b), normalised; a triangle of no area gets the zero vector, as there.
        const std::array<double, 3> U = {C[0] - B[0], C[1] - B[1], C[2] - B[2]};
        const std::array<double, 3> V = {A[0] - B[0], A[1] - B[1], A[2] - B[2]};
        std::array<double, 3> Normal = {U[1] * V[2] - U[2] * V[1], U[2] * V[0] - U[0] * V[2], U[0] * V[1] - U[1] * V[0]};
        const double Length = ResolveHypot(Normal[0], Normal[1], Normal[2]);
        if (Length > 0.0)
        {
            Normal[0] /= Length;
            Normal[1] /= Length;
            Normal[2] /= Length;
        }
        const std::array<const std::array<double, 3>*, 3> Corners = {&A, &B, &C};
        std::array<WeldedCorner, 3> Hashes{};
        for (size_t Corner = 0; Corner < 3; ++Corner)
        {
            const auto& P = *Corners[Corner];
            Hashes[Corner] = {RoundHalfUp(P[0] * WeldPrecision), RoundHalfUp(P[1] * WeldPrecision), RoundHalfUp(P[2] * WeldPrecision)};
        }
        // 📝 EdgesGeometry: "skip degenerate triangles" — two corners that weld to one point.
        if (Hashes[0] == Hashes[1] || Hashes[1] == Hashes[2] || Hashes[2] == Hashes[0])
        {
            continue;
        }
        for (size_t Corner = 0; Corner < 3; ++Corner)
        {
            const size_t Next = (Corner + 1) % 3;
            const EdgeKey Key = {Hashes[Corner][0], Hashes[Corner][1], Hashes[Corner][2], Hashes[Next][0], Hashes[Next][1], Hashes[Next][2]};
            const EdgeKey Reverse = {Hashes[Next][0], Hashes[Next][1], Hashes[Next][2], Hashes[Corner][0], Hashes[Corner][1], Hashes[Corner][2]};
            const auto Sibling = Index.find(Reverse);
            if (Sibling != Index.end() && Order[Sibling->second].has_value())
            {
                const PendingEdge& Other = *Order[Sibling->second];
                const double Dot = Normal[0] * Other.Normal[0] + Normal[1] * Other.Normal[1] + Normal[2] * Other.Normal[2];
                if (Dot <= ThresholdDot)
                {
                    Segments.insert(Segments.end(), {(*Corners[Corner])[0], (*Corners[Corner])[1], (*Corners[Corner])[2],
                                                     (*Corners[Next])[0], (*Corners[Next])[1], (*Corners[Next])[2]});
                }
                Order[Sibling->second].reset();
            }
            else if (Index.find(Key) == Index.end())
            {
                Index.emplace(Key, Order.size());
                Order.push_back(PendingEdge{*Corners[Corner], *Corners[Next], Normal});
            }
        }
    }
    for (const std::optional<PendingEdge>& Edge : Order)
    {
        if (Edge)
        {
            Segments.insert(Segments.end(), {Edge->From[0], Edge->From[1], Edge->From[2], Edge->To[0], Edge->To[1], Edge->To[2]});
        }
    }
    return Segments;
}

ScanEdgeRecords ConstructScanEdges(std::span<const ScanOccupantSource> Occupants, std::span<const std::span<const ScanFacet>> FacetSets)
{
    ScanEdgeRecords Result;
    std::map<std::pair<uint32_t, uint32_t>, std::vector<double>> Extracted;
    for (const ScanOccupantSource& Source : Occupants)
    {
        for (const ScanEdgeFamily& Entry : EdgeFamilies)
        {
            const std::pair<uint32_t, uint32_t> Key = {Source.FacetSet, Entry.Family};
            if (Extracted.find(Key) == Extracted.end())
            {
                std::vector<ScanFacet> Selected;
                for (const ScanFacet& Facet : FacetSets[Source.FacetSet])
                {
                    if (Facet.Family == Entry.Family) Selected.push_back(Facet);
                }
                Extracted.emplace(Key, ExtractFeatureEdges(Selected, Entry.Threshold));
            }
            const std::vector<double>& Segments = Extracted.at(Key);
            // 📝 The tag rides in the w of A as raw bits.
            const uint32_t TagBits = Source.Occupant | (Entry.Family << 8);
            float Tag = 0.0f;
            std::memcpy(&Tag, &TagBits, sizeof(Tag));
            for (size_t Offset = 0; Offset < Segments.size(); Offset += 6)
            {
                Result.Records.insert(Result.Records.end(),
                                      {static_cast<float>(Segments[Offset]), static_cast<float>(Segments[Offset + 1]), static_cast<float>(Segments[Offset + 2]), Tag,
                                       static_cast<float>(Segments[Offset + 3]), static_cast<float>(Segments[Offset + 4]), static_cast<float>(Segments[Offset + 5]), 0.0f});
            }
        }
    }
    Result.Count = static_cast<uint32_t>(Result.Records.size() / 8);
    return Result;
}

} // namespace Frontier::XRayScan
