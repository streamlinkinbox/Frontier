/*==========================================================================================================================================
                                                         BOUNDINGSTRUCTURE.CPP
==========================================================================================================================================*/
// 🧩 Binned-SAH bounding volume structure over one facet set, flattened for the distance transfer (BoundingStructure.js).

#include "BoundingStructure.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace Frontier::GlobalIllumination
{

namespace
{

constexpr uint32_t LeafCapacity = 4;    // [-] facets per leaf before a split is attempted
constexpr int      BinCount     = 12;   // [-] SAH bins along the split axis

struct BoundingEntry
{
    std::array<double, 3> Minimum{0.0, 0.0, 0.0};
    std::array<double, 3> Maximum{0.0, 0.0, 0.0};
    uint32_t              LeftOrFirst = 0;
    uint32_t              Count       = 0;
};

struct Bin
{
    uint32_t              Count = 0;
    std::array<double, 3> Minimum{INFINITY, INFINITY, INFINITY};
    std::array<double, 3> Maximum{-INFINITY, -INFINITY, -INFINITY};
};

double SurfaceArea(const std::array<double, 3>& Minimum, const std::array<double, 3>& Maximum)
{
    const double X = std::max(0.0, Maximum[0] - Minimum[0]);
    const double Y = std::max(0.0, Maximum[1] - Minimum[1]);
    const double Z = std::max(0.0, Maximum[2] - Minimum[2]);
    return 2.0 * (X * Y + Y * Z + Z * X);
}

// 📝 JS Math.min/Math.max over doubles; NaN never occurs for finite facet corners.
double LesserOf(double Left, double Right) { return Right < Left ? Right : Left; }
double GreaterOf(double Left, double Right) { return Right > Left ? Right : Left; }

class StructureAssembly
{
  public:
    explicit StructureAssembly(const std::vector<float>& Corners) : Corners_(Corners)
    {
        FacetCount_ = static_cast<uint32_t>(Corners.size() / 9);
        Centroids_.resize(static_cast<size_t>(FacetCount_) * 3);
        FacetMinimum_.resize(Centroids_.size());
        FacetMaximum_.resize(Centroids_.size());
        for (uint32_t Facet = 0; Facet < FacetCount_; ++Facet)
        {
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const double A = Corners[Facet * 9 + Axis];
                const double B = Corners[Facet * 9 + 3 + Axis];
                const double C = Corners[Facet * 9 + 6 + Axis];
                FacetMinimum_[Facet * 3 + Axis] = static_cast<float>(LesserOf(LesserOf(A, B), C));
                FacetMaximum_[Facet * 3 + Axis] = static_cast<float>(GreaterOf(GreaterOf(A, B), C));
                Centroids_[Facet * 3 + Axis]    = static_cast<float>((A + B + C) / 3.0);
            }
        }
        Order_.resize(FacetCount_);
        for (uint32_t Facet = 0; Facet < FacetCount_; ++Facet) Order_[Facet] = Facet;
    }

    BoundingStructureArchive Assemble()
    {
        const uint32_t Root = ConstructEntry();
        Subdivide(Root, 0, FacetCount_);
        BoundingStructureArchive Archive;
        Archive.EntryCount = static_cast<uint32_t>(Entries_.size());
        Archive.EntryBytes.resize(Entries_.size() * 32);
        for (size_t Index = 0; Index < Entries_.size(); ++Index)
        {
            const BoundingEntry& Entry = Entries_[Index];
            float Scalars[8];
            uint32_t Words[8];
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Scalars[Axis]     = static_cast<float>(Entry.Minimum[Axis]);
                Scalars[4 + Axis] = static_cast<float>(Entry.Maximum[Axis]);
            }
            std::memcpy(Words, Scalars, sizeof(Scalars));
            Words[3] = Entry.LeftOrFirst;
            Words[7] = Entry.Count;
            std::memcpy(Archive.EntryBytes.data() + Index * 32, Words, 32);
        }
        Archive.FacetCount = FacetCount_;
        Archive.FacetCorners.assign(static_cast<size_t>(FacetCount_) * 12, 0.0f);
        for (uint32_t Index = 0; Index < FacetCount_; ++Index)
        {
            const uint32_t Facet = Order_[Index];
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Archive.FacetCorners[Index * 12 + Corner * 4 + Axis] = Corners_[Facet * 9 + Corner * 3 + Axis];
                }
            }
        }
        return Archive;
    }

  private:
    uint32_t ConstructEntry()
    {
        Entries_.push_back(BoundingEntry{});
        return static_cast<uint32_t>(Entries_.size() - 1);
    }

    void FitBounds(uint32_t Entry, uint32_t First, uint32_t Count)
    {
        std::array<double, 3> Minimum{INFINITY, INFINITY, INFINITY};
        std::array<double, 3> Maximum{-INFINITY, -INFINITY, -INFINITY};
        for (uint32_t Index = First; Index < First + Count; ++Index)
        {
            const uint32_t Facet = Order_[Index];
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Minimum[Axis] = LesserOf(Minimum[Axis], FacetMinimum_[Facet * 3 + Axis]);
                Maximum[Axis] = GreaterOf(Maximum[Axis], FacetMaximum_[Facet * 3 + Axis]);
            }
        }
        Entries_[Entry].Minimum = Minimum;
        Entries_[Entry].Maximum = Maximum;
    }

    // 📝 Binned SAH: cost(split) = A_left·N_left + A_right·N_right, compared against the leaf cost A·N.
    void Subdivide(uint32_t Entry, uint32_t First, uint32_t Count)
    {
        FitBounds(Entry, First, Count);
        if (Count <= LeafCapacity)
        {
            Entries_[Entry].LeftOrFirst = First;
            Entries_[Entry].Count       = Count;
            return;
        }
        int    BestAxis  = -1;
        double BestSplit = 0.0;
        double BestCost  = INFINITY;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            double Low  = INFINITY;
            double High = -INFINITY;
            for (uint32_t Index = First; Index < First + Count; ++Index)
            {
                const double Centroid = Centroids_[Order_[Index] * 3 + Axis];
                Low  = LesserOf(Low, Centroid);
                High = GreaterOf(High, Centroid);
            }
            if (High - Low < 1.0e-9) continue;
            std::array<Bin, BinCount> Bins{};
            const double Scale = BinCount / (High - Low);
            for (uint32_t Index = First; Index < First + Count; ++Index)
            {
                const uint32_t Facet = Order_[Index];
                const double   Raw   = std::floor((static_cast<double>(Centroids_[Facet * 3 + Axis]) - Low) * Scale);
                const int      Slot  = static_cast<int>(LesserOf(static_cast<double>(BinCount - 1), Raw));
                Bins[Slot].Count++;
                for (int Component = 0; Component < 3; ++Component)
                {
                    Bins[Slot].Minimum[Component] = LesserOf(Bins[Slot].Minimum[Component], FacetMinimum_[Facet * 3 + Component]);
                    Bins[Slot].Maximum[Component] = GreaterOf(Bins[Slot].Maximum[Component], FacetMaximum_[Facet * 3 + Component]);
                }
            }
            for (int Split = 1; Split < BinCount; ++Split)
            {
                uint32_t LeftCount  = 0;
                uint32_t RightCount = 0;
                std::array<double, 3> LeftMinimum{INFINITY, INFINITY, INFINITY}, LeftMaximum{-INFINITY, -INFINITY, -INFINITY};
                std::array<double, 3> RightMinimum{INFINITY, INFINITY, INFINITY}, RightMaximum{-INFINITY, -INFINITY, -INFINITY};
                for (int Slot = 0; Slot < BinCount; ++Slot)
                {
                    if (Bins[Slot].Count == 0) continue;
                    const bool Left = Slot < Split;
                    auto& TargetMinimum = Left ? LeftMinimum : RightMinimum;
                    auto& TargetMaximum = Left ? LeftMaximum : RightMaximum;
                    for (int Component = 0; Component < 3; ++Component)
                    {
                        TargetMinimum[Component] = LesserOf(TargetMinimum[Component], Bins[Slot].Minimum[Component]);
                        TargetMaximum[Component] = GreaterOf(TargetMaximum[Component], Bins[Slot].Maximum[Component]);
                    }
                    if (Left) LeftCount += Bins[Slot].Count;
                    else RightCount += Bins[Slot].Count;
                }
                if (LeftCount == 0 || RightCount == 0) continue;
                const double Cost = SurfaceArea(LeftMinimum, LeftMaximum) * LeftCount + SurfaceArea(RightMinimum, RightMaximum) * RightCount;
                if (Cost < BestCost)
                {
                    BestCost  = Cost;
                    BestAxis  = Axis;
                    BestSplit = Low + Split / Scale;
                }
            }
        }
        const double LeafCost = SurfaceArea(Entries_[Entry].Minimum, Entries_[Entry].Maximum) * Count;
        uint32_t Middle = First;
        if (BestAxis >= 0 && BestCost < LeafCost)
        {
            int64_t Cursor = First;
            int64_t Tail   = static_cast<int64_t>(First) + Count - 1;
            while (Cursor <= Tail)
            {
                if (static_cast<double>(Centroids_[Order_[Cursor] * 3 + BestAxis]) < BestSplit)
                {
                    ++Cursor;
                }
                else
                {
                    std::swap(Order_[Cursor], Order_[Tail]);
                    --Tail;
                }
            }
            Middle = static_cast<uint32_t>(Cursor);
        }
        if (Middle == First || Middle == First + Count)
        {
            if (Count <= LeafCapacity * 4)
            {
                Entries_[Entry].LeftOrFirst = First;
                Entries_[Entry].Count       = Count;
                return;
            }
            Middle = First + (Count >> 1);
        }
        const uint32_t Left = ConstructEntry();
        ConstructEntry();
        Entries_[Entry].LeftOrFirst = Left;
        Entries_[Entry].Count       = 0;
        Subdivide(Left, First, Middle - First);
        Subdivide(Left + 1, Middle, First + Count - Middle);
    }

    const std::vector<float>&  Corners_;
    uint32_t                   FacetCount_ = 0;
    std::vector<float>         Centroids_{};
    std::vector<float>         FacetMinimum_{};
    std::vector<float>         FacetMaximum_{};
    std::vector<uint32_t>      Order_{};
    std::vector<BoundingEntry> Entries_{};
};

} // namespace

BoundingStructureArchive ConstructBoundingStructure(const std::vector<float>& Corners)
{
    StructureAssembly Assembly(Corners);
    return Assembly.Assemble();
}

} // namespace Frontier::GlobalIllumination
