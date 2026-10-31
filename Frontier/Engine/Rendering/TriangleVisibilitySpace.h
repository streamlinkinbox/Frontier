/*==========================================================================================================================================
                                                        TRIANGLEVISIBILITYSPACE.H
==========================================================================================================================================*/
// 🧩 Bounded double-precision triangle visibility over all scene facets,
// independent of primary-view culling.

#pragma once
#include "ClusterNesting.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

//------------------------------------------------------------------------------------------------------------------------
//                                                  TRIANGLE INTERVAL QUERIES
//------------------------------------------------------------------------------------------------------------------------

namespace Frontier
{
class TriangleVisibilitySpace
{
    struct Enclosure
    {
        std::array<double, 3> Minimum{}, Maximum{};
        uint32_t First = 0, Count = 0, Lower = 0, Upper = 0;
    };
    std::vector<std::array<SpatialVector, 3>> Facets;
    std::vector<uint32_t> Order;
    std::vector<Enclosure> Bounds;

    static double Coordinate(SpatialVector Point, uint32_t Axis)
    {
        return Axis == 0 ? Point.X : Axis == 1 ? Point.Y : Point.Z;
    }
    static bool Finite(SpatialVector Point)
    {
        return std::isfinite(Point.X) && std::isfinite(Point.Y) && std::isfinite(Point.Z);
    }
    uint32_t Partition(uint32_t First, uint32_t Count)
    {
        Enclosure Region;
        Region.First = First;
        Region.Count = Count;
        Region.Minimum.fill(std::numeric_limits<double>::infinity());
        Region.Maximum.fill(-std::numeric_limits<double>::infinity());
        for (uint32_t Slot = First; Slot < First + Count; ++Slot)
            for (auto Corner : Facets[Order[Slot]])
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    Region.Minimum[Axis] = std::min(Region.Minimum[Axis], Coordinate(Corner, Axis));
                    Region.Maximum[Axis] = std::max(Region.Maximum[Axis], Coordinate(Corner, Axis));
                }
        const uint32_t Index = uint32_t(Bounds.size());
        Bounds.push_back(Region);
        if (Count > 8)
        {
            uint32_t Axis = 0;
            for (uint32_t Other = 1; Other < 3; ++Other)
                if (Region.Maximum[Other] - Region.Minimum[Other] >
                    Region.Maximum[Axis] - Region.Minimum[Axis])
                    Axis = Other;
            const uint32_t Middle = First + Count / 2;
            auto Centre = [&](uint32_t Facet)
            {
                const auto& Triangle = Facets[Facet];
                return Coordinate(Triangle[0], Axis) / 3 + Coordinate(Triangle[1], Axis) / 3 +
                       Coordinate(Triangle[2], Axis) / 3;
            };
            std::nth_element(Order.begin() + First, Order.begin() + Middle, Order.begin() + First + Count,
                             [&](uint32_t A, uint32_t B) { return Centre(A) < Centre(B); });
            const uint32_t Lower = Partition(First, Middle - First);
            const uint32_t Upper = Partition(Middle, First + Count - Middle);
            Bounds[Index].Lower = Lower;
            Bounds[Index].Upper = Upper;
            Bounds[Index].Count = 0;
        }
        return Index;
    }

    static double BoxHalfArea(const std::array<double, 3>& Low, const std::array<double, 3>& High) noexcept
    {
        if (Low[0] > High[0] || Low[1] > High[1] || Low[2] > High[2])
            return 0.0;
        const double Dx = High[0] - Low[0], Dy = High[1] - Low[1], Dz = High[2] - Low[2];
        return Dx * Dy + Dy * Dz + Dz * Dx;
    }

    uint32_t PartitionSah(uint32_t First, uint32_t Count, uint32_t MaxLeafFacets, uint32_t Depth)
    {
        Enclosure Region;
        Region.First = First;
        Region.Count = Count;
        Region.Minimum.fill(std::numeric_limits<double>::infinity());
        Region.Maximum.fill(-std::numeric_limits<double>::infinity());
        std::array<double, 3> CentroidMin{}, CentroidMax{};
        CentroidMin.fill(std::numeric_limits<double>::infinity());
        CentroidMax.fill(-std::numeric_limits<double>::infinity());
        for (uint32_t Slot = First; Slot < First + Count; ++Slot)
        {
            const auto& Triangle = Facets[Order[Slot]];
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                const double C = Coordinate(Triangle[0], Axis) / 3.0 +
                                 Coordinate(Triangle[1], Axis) / 3.0 +
                                 Coordinate(Triangle[2], Axis) / 3.0;
                CentroidMin[Axis] = std::min(CentroidMin[Axis], C);
                CentroidMax[Axis] = std::max(CentroidMax[Axis], C);
                for (auto Corner : Triangle)
                {
                    Region.Minimum[Axis] = std::min(Region.Minimum[Axis], Coordinate(Corner, Axis));
                    Region.Maximum[Axis] = std::max(Region.Maximum[Axis], Coordinate(Corner, Axis));
                }
            }
        }
        const uint32_t Index = uint32_t(Bounds.size());
        Bounds.push_back(Region);
        if (Count <= MaxLeafFacets)
            return Index;

        constexpr uint32_t BinCount = 16u;
        const double EnclosureHalfArea = BoxHalfArea(Region.Minimum, Region.Maximum);
        double BestCost = std::numeric_limits<double>::infinity();
        uint32_t BestAxis = 0u, BestBin = 0u;
        bool FoundSahSplit = false;

        if (Depth < 24u && EnclosureHalfArea > 1e-20)
        {
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                const double Span = CentroidMax[Axis] - CentroidMin[Axis];
                if (Span <= 1e-12)
                    continue;
                struct SahBin
                {
                    uint32_t Count = 0u;
                    std::array<double, 3> Minimum{}, Maximum{};
                };
                std::array<SahBin, BinCount> Bins{};
                for (auto& Bin : Bins)
                {
                    Bin.Minimum.fill(std::numeric_limits<double>::infinity());
                    Bin.Maximum.fill(-std::numeric_limits<double>::infinity());
                }
                const double Scale = double(BinCount) / Span;
                for (uint32_t Slot = First; Slot < First + Count; ++Slot)
                {
                    const auto& Triangle = Facets[Order[Slot]];
                    const double C = Coordinate(Triangle[0], Axis) / 3.0 +
                                     Coordinate(Triangle[1], Axis) / 3.0 +
                                     Coordinate(Triangle[2], Axis) / 3.0;
                    const uint32_t Bucket = std::min<uint32_t>(
                        BinCount - 1u, uint32_t(std::max(0.0, (C - CentroidMin[Axis]) * Scale)));
                    ++Bins[Bucket].Count;
                    for (auto Corner : Triangle)
                        for (uint32_t Coord = 0; Coord < 3; ++Coord)
                        {
                            Bins[Bucket].Minimum[Coord] =
                                std::min(Bins[Bucket].Minimum[Coord], Coordinate(Corner, Coord));
                            Bins[Bucket].Maximum[Coord] =
                                std::max(Bins[Bucket].Maximum[Coord], Coordinate(Corner, Coord));
                        }
                }
                std::array<uint32_t, BinCount - 1u> LeftCounts{}, RightCounts{};
                std::array<double, BinCount - 1u> LeftAreas{}, RightAreas{};
                std::array<double, 3> RunMin{}, RunMax{};
                RunMin.fill(std::numeric_limits<double>::infinity());
                RunMax.fill(-std::numeric_limits<double>::infinity());
                uint32_t RunCount = 0u;
                for (uint32_t B = 0; B < BinCount - 1u; ++B)
                {
                    RunCount += Bins[B].Count;
                    for (uint32_t Coord = 0; Coord < 3; ++Coord)
                    {
                        RunMin[Coord] = std::min(RunMin[Coord], Bins[B].Minimum[Coord]);
                        RunMax[Coord] = std::max(RunMax[Coord], Bins[B].Maximum[Coord]);
                    }
                    LeftCounts[B] = RunCount;
                    LeftAreas[B] = BoxHalfArea(RunMin, RunMax);
                }
                RunMin.fill(std::numeric_limits<double>::infinity());
                RunMax.fill(-std::numeric_limits<double>::infinity());
                RunCount = 0u;
                for (uint32_t B = BinCount - 1u; B > 0u; --B)
                {
                    RunCount += Bins[B].Count;
                    for (uint32_t Coord = 0; Coord < 3; ++Coord)
                    {
                        RunMin[Coord] = std::min(RunMin[Coord], Bins[B].Minimum[Coord]);
                        RunMax[Coord] = std::max(RunMax[Coord], Bins[B].Maximum[Coord]);
                    }
                    RightCounts[B - 1u] = RunCount;
                    RightAreas[B - 1u] = BoxHalfArea(RunMin, RunMax);
                }
                for (uint32_t B = 0; B < BinCount - 1u; ++B)
                {
                    if (LeftCounts[B] == 0u || RightCounts[B] == 0u)
                        continue;
                    const double Cost = 1.0 + 1.5 * (double(LeftCounts[B]) * LeftAreas[B] +
                                                     double(RightCounts[B]) * RightAreas[B]) / EnclosureHalfArea;
                    if (Cost < BestCost)
                    {
                        BestCost = Cost;
                        BestAxis = Axis;
                        BestBin = B;
                        FoundSahSplit = true;
                    }
                }
            }
        }

        const double LeafCost = 1.5 * double(Count);
        if (FoundSahSplit && Count <= 4u && BestCost >= LeafCost)
            return Index;

        uint32_t Split = First + Count / 2u;
        if (FoundSahSplit)
        {
            const double Span = CentroidMax[BestAxis] - CentroidMin[BestAxis];
            const double Scale = double(BinCount) / Span;
            auto MiddleIter = std::stable_partition(
                Order.begin() + First, Order.begin() + First + Count,
                [&](uint32_t Facet)
                {
                    const auto& Triangle = Facets[Facet];
                    const double C = Coordinate(Triangle[0], BestAxis) / 3.0 +
                                     Coordinate(Triangle[1], BestAxis) / 3.0 +
                                     Coordinate(Triangle[2], BestAxis) / 3.0;
                    const uint32_t Bucket = std::min<uint32_t>(
                        BinCount - 1u, uint32_t(std::max(0.0, (C - CentroidMin[BestAxis]) * Scale)));
                    return Bucket <= BestBin;
                });
            const uint32_t CandidateSplit = uint32_t(MiddleIter - Order.begin());
            if (CandidateSplit > First && CandidateSplit < First + Count)
                Split = CandidateSplit;
            else
                FoundSahSplit = false;
        }
        if (!FoundSahSplit)
        {
            uint32_t Axis = 0u;
            for (uint32_t Other = 1u; Other < 3u; ++Other)
                if (Region.Maximum[Other] - Region.Minimum[Other] > Region.Maximum[Axis] - Region.Minimum[Axis])
                    Axis = Other;
            auto Centre = [&](uint32_t Facet)
            {
                const auto& Triangle = Facets[Facet];
                return Coordinate(Triangle[0], Axis) / 3.0 + Coordinate(Triangle[1], Axis) / 3.0 +
                       Coordinate(Triangle[2], Axis) / 3.0;
            };
            std::nth_element(Order.begin() + First, Order.begin() + Split, Order.begin() + First + Count,
                             [&](uint32_t A, uint32_t B) { return Centre(A) < Centre(B); });
        }
        const uint32_t Lower = PartitionSah(First, Split - First, MaxLeafFacets, Depth + 1u);
        const uint32_t Upper = PartitionSah(Split, First + Count - Split, MaxLeafFacets, Depth + 1u);
        Bounds[Index].Lower = Lower;
        Bounds[Index].Upper = Upper;
        Bounds[Index].Count = 0u;
        return Index;
    }

  public:
    struct FloatEnclosure
    {
        std::array<float, 4> Minimum{}, Maximum{};
        std::array<uint32_t, 4> Links{};
    };
    struct FloatFacet
    {
        std::array<float, 4> A{}, B{}, C{};
    };
    struct FloatAtlas
    {
        std::vector<FloatEnclosure> Enclosures;
        std::vector<FloatFacet> Corners;
        std::vector<uint32_t> OriginalFacets;
    };
    FloatAtlas PackFloat32() const
    {
        FloatAtlas Atlas;
        Atlas.OriginalFacets = Order;
        for (const auto& Region : Bounds)
        {
            FloatEnclosure Packed;
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                Packed.Minimum[Axis] =
                    std::nextafter(float(Region.Minimum[Axis]), -std::numeric_limits<float>::infinity());
                Packed.Maximum[Axis] =
                    std::nextafter(float(Region.Maximum[Axis]), std::numeric_limits<float>::infinity());
                if (!std::isfinite(Packed.Minimum[Axis]) || !std::isfinite(Packed.Maximum[Axis]))
                    throw std::runtime_error("Facet bounds exceed float32 range");
            }
            Packed.Links = {Region.Count ? Region.First : Region.Lower,
                            Region.Count ? Region.Count : Region.Upper, Region.Count ? 1u : 0u, 0u};
            Atlas.Enclosures.push_back(Packed);
        }
        for (auto Index : Order)
        {
            FloatFacet Packed;
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const auto P = Facets[Index][Corner];
                auto& Destination = Corner == 0 ? Packed.A : Corner == 1 ? Packed.B : Packed.C;
                Destination = {float(P.X), float(P.Y), float(P.Z), 0};
            }
            Atlas.Corners.push_back(Packed);
        }
        return Atlas;
    }

    struct TriangleStrike
    {
        bool Hit = false;
        double Travel = 0;
        uint32_t Facet = UINT32_MAX;
    };
    struct TriangleProximity
    {
        double Distance = std::numeric_limits<double>::infinity();
        uint32_t Facet = UINT32_MAX;
    };
    uint32_t FacetCount() const { return uint32_t(Facets.size()); }

    TriangleProximity Nearest(SpatialVector Point) const noexcept
    {
        TriangleProximity Result;
        if (Bounds.empty() || !Finite(Point))
            return Result;
        double BestSquared = std::numeric_limits<double>::infinity();
        std::array<uint32_t, 64> Pending{};
        uint32_t Count = 1;
        auto Subtract = [](SpatialVector A, SpatialVector B)
        { return SpatialVector{A.X - B.X, A.Y - B.Y, A.Z - B.Z}; };
        auto Dot = [](SpatialVector A, SpatialVector B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; };
        while (Count)
        {
            const auto& Region = Bounds[Pending[--Count]];
            double Lower = 0;
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                double Delta = std::max({Region.Minimum[Axis] - Coordinate(Point, Axis), 0.0,
                                         Coordinate(Point, Axis) - Region.Maximum[Axis]});
                Lower += Delta * Delta;
            }
            if (Lower > BestSquared)
                continue;
            if (!Region.Count)
            {
                Pending[Count++] = Region.Lower;
                Pending[Count++] = Region.Upper;
                continue;
            }
            for (uint32_t Slot = Region.First; Slot < Region.First + Region.Count; ++Slot)
            {
                const auto& Triangle = Facets[Order[Slot]];
                double Squared = std::numeric_limits<double>::infinity();
                for (uint32_t Edge = 0; Edge < 3; ++Edge)
                {
                    const auto A = Triangle[Edge], B = Triangle[(Edge + 1) % 3];
                    const auto E = Subtract(B, A), V = Subtract(Point, A);
                    const double LengthSquared = Dot(E, E);
                    const double Fraction =
                        LengthSquared > 0 ? std::clamp(Dot(V, E) / LengthSquared, 0.0, 1.0) : 0;
                    const SpatialVector Delta{V.X - Fraction * E.X, V.Y - Fraction * E.Y,
                                              V.Z - Fraction * E.Z};
                    Squared = std::min(Squared, Dot(Delta, Delta));
                }
                const auto E = Subtract(Triangle[1], Triangle[0]), F = Subtract(Triangle[2], Triangle[0]);
                const auto V = Subtract(Point, Triangle[0]);
                auto Cross = [](SpatialVector A, SpatialVector B) {
                    return SpatialVector{A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
                };
                const auto N = Cross(E, F);
                const double NormalSquared = Dot(N, N);
                if (NormalSquared > 0 && Dot(Cross(E, V), N) >= 0 &&
                    Dot(Cross(Subtract(Triangle[2], Triangle[1]), Subtract(Point, Triangle[1])), N) >= 0 &&
                    Dot(Cross(Subtract(Triangle[0], Triangle[2]), Subtract(Point, Triangle[2])), N) >= 0)
                {
                    const double Height = Dot(V, N);
                    Squared = std::min(Squared, Height * Height / NormalSquared);
                }
                if (Squared < BestSquared)
                {
                    BestSquared = Squared;
                    Result.Facet = Order[Slot];
                }
            }
        }
        Result.Distance = std::sqrt(BestSquared);
        return Result;
    }

    void Build(std::vector<std::array<SpatialVector, 3>> Geometry)
    {
        if (Geometry.size() > std::numeric_limits<uint32_t>::max() / 2)
            throw std::runtime_error("Triangle visibility capacity exceeded");
        for (const auto& Triangle : Geometry)
            for (auto Corner : Triangle)
                if (!Finite(Corner))
                    throw std::runtime_error("Nonfinite triangle visibility coordinate");
        Facets = std::move(Geometry);
        Order.resize(Facets.size());
        std::iota(Order.begin(), Order.end(), 0u);
        Bounds.clear();
        if (!Facets.empty())
            Partition(0, uint32_t(Facets.size()));
    }

    void BuildSurfaceAreaHierarchy(std::vector<std::array<SpatialVector, 3>> Geometry,
                                   uint32_t MaxLeafFacets = 2u)
    {
        if (Geometry.size() > std::numeric_limits<uint32_t>::max() / 2)
            throw std::runtime_error("Triangle visibility capacity exceeded");
        for (const auto& Triangle : Geometry)
            for (auto Corner : Triangle)
                if (!Finite(Corner))
                    throw std::runtime_error("Nonfinite triangle visibility coordinate");
        Facets = std::move(Geometry);
        Order.resize(Facets.size());
        std::iota(Order.begin(), Order.end(), 0u);
        Bounds.clear();
        if (!Facets.empty())
            PartitionSah(0u, uint32_t(Facets.size()), std::max<uint32_t>(1u, MaxLeafFacets), 0u);
    }

    TriangleStrike Trace(SpatialVector Origin, SpatialVector Direction, double Minimum, double Maximum,
                         bool Any = false) const noexcept
    {
        if (Bounds.empty() || !Finite(Origin) || !Finite(Direction) || !std::isfinite(Minimum) ||
            Minimum < 0 || !(Maximum > Minimum) || (Direction.X == 0 && Direction.Y == 0 && Direction.Z == 0))
            return {};
        TriangleStrike Result;
        std::array<uint32_t, 64> Pending{};
        uint32_t Count = 1;
        while (Count)
        {
            const auto& Region = Bounds[Pending[--Count]];
            double Near = Minimum, Far = Maximum;
            bool Intersects = true;
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                const double O = Coordinate(Origin, Axis), D = Coordinate(Direction, Axis);
                if (D == 0)
                {
                    if (O < Region.Minimum[Axis] || O > Region.Maximum[Axis])
                        Intersects = false;
                }
                else
                {
                    double A = (Region.Minimum[Axis] - O) / D, B = (Region.Maximum[Axis] - O) / D;
                    if (A > B)
                        std::swap(A, B);
                    Near = std::max(Near, A);
                    Far = std::min(Far, B);
                }
            }
            if (!Intersects || Near > Far)
                continue;
            if (!Region.Count)
            {
                // Balanced median splits and the uint32 capacity bound keep pending
                // depth below 32.
                Pending[Count++] = Region.Lower;
                Pending[Count++] = Region.Upper;
                continue;
            }
            for (uint32_t Slot = Region.First; Slot < Region.First + Region.Count; ++Slot)
            {
                const auto& Triangle = Facets[Order[Slot]];
                auto Subtract = [](SpatialVector A, SpatialVector B)
                { return SpatialVector{A.X - B.X, A.Y - B.Y, A.Z - B.Z}; };
                auto Cross = [](SpatialVector A, SpatialVector B) {
                    return SpatialVector{A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
                };
                auto Dot = [](SpatialVector A, SpatialVector B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; };
                const auto E1 = Subtract(Triangle[1], Triangle[0]), E2 = Subtract(Triangle[2], Triangle[0]);
                const auto P = Cross(Direction, E2);
                const double Determinant = Dot(E1, P);
                if (std::abs(Determinant) < 1e-14)
                    continue;
                const auto Offset = Subtract(Origin, Triangle[0]);
                const double U = Dot(Offset, P) / Determinant;
                if (U < 0 || U > 1)
                    continue;
                const auto Q = Cross(Offset, E1);
                const double V = Dot(Direction, Q) / Determinant;
                if (V < 0 || U + V > 1)
                    continue;
                const double Travel = Dot(E2, Q) / Determinant;
                if (Travel > Minimum && Travel < Maximum)
                {
                    Result = {true, Travel, Order[Slot]};
                    if (Any)
                        return Result;
                    Maximum = Travel;
                }
            }
        }
        return Result;
    }
    bool Occluded(SpatialVector Origin, SpatialVector Direction, double Minimum,
                  double Maximum) const noexcept
    {
        return Trace(Origin, Direction, Minimum, Maximum, true).Hit;
    }
};
} // namespace Frontier
