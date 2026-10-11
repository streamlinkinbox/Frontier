//============================================================================================================================================
//                                                           TYREMESHSTRUCTURE.CPP
//============================================================================================================================================
// 📦 Welding, triangle recording and the watertightness ledger behind TyreMeshStructure.

#include "TyreMeshStructure.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        WELDING
//------------------------------------------------------------------------------------------------------------------------

uint64_t TyreMeshStructure::CellKey(int64_t Cx, int64_t Cy, int64_t Cz) noexcept
{
    // 📝 Three odd primes mixed into one 64-bit key. Collisions are tolerated: a bucket holds candidates
    //    and every candidate is distance-checked, so a collision costs a comparison, never a wrong weld.
    const uint64_t Hx = static_cast<uint64_t>(Cx) * 0x9E3779B97F4A7C15ull;
    const uint64_t Hy = static_cast<uint64_t>(Cy) * 0xC2B2AE3D27D4EB4Full;
    const uint64_t Hz = static_cast<uint64_t>(Cz) * 0x165667B19E3779F9ull;
    return Hx ^ (Hy + 0x9E3779B97F4A7C15ull + (Hx << 6) + (Hx >> 2))
              ^ (Hz + 0x9E3779B97F4A7C15ull + (Hy << 6) + (Hy >> 2));
}

uint32_t TyreMeshStructure::WeldPosition(float X, float Y, float Z) noexcept
{
    const float   Inverse = 1.0f / WeldTolerance;
    const int64_t Cx      = static_cast<int64_t>(std::floor(X * Inverse));
    const int64_t Cy      = static_cast<int64_t>(std::floor(Y * Inverse));
    const int64_t Cz      = static_cast<int64_t>(std::floor(Z * Inverse));

    // 📝 ① Probe the 3×3×3 neighbourhood. A point just across a cell edge is still within tolerance of
    //    one already stored, and must weld to it rather than open a crack along the cell lattice.
    const float Squared = WeldTolerance * WeldTolerance;
    for (int64_t Dz = -1; Dz <= 1; ++Dz)
    {
        for (int64_t Dy = -1; Dy <= 1; ++Dy)
        {
            for (int64_t Dx = -1; Dx <= 1; ++Dx)
            {
                const auto Found = CellIndex.find(CellKey(Cx + Dx, Cy + Dy, Cz + Dz));
                if (Found == CellIndex.end())
                    continue;

                for (const uint32_t Candidate : Found->second)
                {
                    const TyrePositionRecord& Point = Positions[Candidate];
                    const float Ex = Point.X - X;
                    const float Ey = Point.Y - Y;
                    const float Ez = Point.Z - Z;
                    if (Ex * Ex + Ey * Ey + Ez * Ez <= Squared)
                        return Candidate;
                }
            }
        }
    }

    // 📝 ② Nothing within tolerance, so this is a new point. It is filed only in its own cell; the
    //    27-cell probe above is what makes a one-cell filing sufficient.
    const uint32_t Created = static_cast<uint32_t>(Positions.size());
    Positions.push_back(TyrePositionRecord{ X, Y, Z });
    CellIndex[CellKey(Cx, Cy, Cz)].push_back(Created);
    return Created;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   TRIANGLE RECORDING
//------------------------------------------------------------------------------------------------------------------------

void TyreMeshStructure::AddTriangle(uint32_t                A,
                                    uint32_t                B,
                                    uint32_t                C,
                                    const TyreCornerRecord& CornerA,
                                    const TyreCornerRecord& CornerB,
                                    const TyreCornerRecord& CornerC) noexcept
{
    Indices.push_back(A);
    Indices.push_back(B);
    Indices.push_back(C);
    Corners.push_back(CornerA);
    Corners.push_back(CornerB);
    Corners.push_back(CornerC);
}

void TyreMeshStructure::AddQuad(uint32_t                A,
                                uint32_t                B,
                                uint32_t                C,
                                uint32_t                D,
                                const TyreCornerRecord& CornerA,
                                const TyreCornerRecord& CornerB,
                                const TyreCornerRecord& CornerC,
                                const TyreCornerRecord& CornerD) noexcept
{
    AddTriangle(A, B, C, CornerA, CornerB, CornerC);
    AddTriangle(A, C, D, CornerA, CornerC, CornerD);
    ++QuadTally;
}

void TyreMeshStructure::Reserve(uint32_t PositionCount, uint32_t TriangleCount) noexcept
{
    Positions.reserve(PositionCount);
    Indices.reserve(static_cast<size_t>(TriangleCount) * 3u);
    Corners.reserve(static_cast<size_t>(TriangleCount) * 3u);
}

void TyreMeshStructure::Clear() noexcept
{
    Positions.clear();
    Corners.clear();
    Indices.clear();
    CellIndex.clear();
    QuadTally = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   T-JUNCTION REPAIR
//------------------------------------------------------------------------------------------------------------------------

uint32_t TyreMeshStructure::RepairJunctions() noexcept
{
    if (Indices.size() < 3u || Positions.empty())
        return 0u;

    // 📝 ① A coarse grid, not the weld grid. Welding buckets at one micrometre, and walking a six
    //    millimetre edge through micrometre cells would visit six thousand of them. This index exists to
    //    answer "which points are near this edge", so its cell is sized to the edges being asked about.
    constexpr float CoarseCell = 1.0f;   // [mm]  - lookup cell for the near-edge query

    std::unordered_map<uint64_t, std::vector<uint32_t>> Coarse;
    Coarse.reserve(Positions.size());
    const float Inverse = 1.0f / CoarseCell;
    for (uint32_t Point = 0; Point < Positions.size(); ++Point)
    {
        const TyrePositionRecord& Record = Positions[Point];
        Coarse[CellKey(static_cast<int64_t>(std::floor(Record.X * Inverse)),
                       static_cast<int64_t>(std::floor(Record.Y * Inverse)),
                       static_cast<int64_t>(std::floor(Record.Z * Inverse)))].push_back(Point);
    }

    const float Tolerance = WeldTolerance;
    const float Squared   = Tolerance * Tolerance;

    std::vector<uint32_t>         Rebuilt;
    std::vector<TyreCornerRecord> RebuiltCorners;
    Rebuilt.reserve(Indices.size());
    RebuiltCorners.reserve(Corners.size());
    uint32_t Splits = 0u;

    std::vector<std::pair<float, uint32_t>> OnEdge;
    std::vector<uint32_t>                   Ring;
    std::vector<TyreCornerRecord>           RingCorner;

    for (size_t Face = 0; Face + 2u < Indices.size(); Face += 3u)
    {
        Ring.clear();
        RingCorner.clear();

        for (uint32_t Side = 0; Side < 3u; ++Side)
        {
            const uint32_t Start = Indices[Face + Side];
            const uint32_t End   = Indices[Face + (Side + 1u) % 3u];
            Ring.push_back(Start);
            RingCorner.push_back(Corners[Face + Side]);

            const TyrePositionRecord& A = Positions[Start];
            const TyrePositionRecord& B = Positions[End];
            const float Dx = B.X - A.X, Dy = B.Y - A.Y, Dz = B.Z - A.Z;
            const float Length = Dx * Dx + Dy * Dy + Dz * Dz;
            if (Length <= Squared)
                continue;

            // 📝 ② Sweep the coarse cells the edge's extent touches and keep any point that lies on it.
            OnEdge.clear();
            const int64_t X0 = static_cast<int64_t>(std::floor(std::fmin(A.X, B.X) * Inverse)) - 1;
            const int64_t X1 = static_cast<int64_t>(std::floor(std::fmax(A.X, B.X) * Inverse)) + 1;
            const int64_t Y0 = static_cast<int64_t>(std::floor(std::fmin(A.Y, B.Y) * Inverse)) - 1;
            const int64_t Y1 = static_cast<int64_t>(std::floor(std::fmax(A.Y, B.Y) * Inverse)) + 1;
            const int64_t Z0 = static_cast<int64_t>(std::floor(std::fmin(A.Z, B.Z) * Inverse)) - 1;
            const int64_t Z1 = static_cast<int64_t>(std::floor(std::fmax(A.Z, B.Z) * Inverse)) + 1;

            for (int64_t Cz = Z0; Cz <= Z1; ++Cz)
            for (int64_t Cy = Y0; Cy <= Y1; ++Cy)
            for (int64_t Cx = X0; Cx <= X1; ++Cx)
            {
                const auto Bucket = Coarse.find(CellKey(Cx, Cy, Cz));
                if (Bucket == Coarse.end())
                    continue;

                for (const uint32_t Candidate : Bucket->second)
                {
                    if (Candidate == Start || Candidate == End)
                        continue;

                    const TyrePositionRecord& P = Positions[Candidate];
                    const float Along = ((P.X - A.X) * Dx + (P.Y - A.Y) * Dy + (P.Z - A.Z) * Dz) / Length;
                    if (Along <= 0.0f || Along >= 1.0f)
                        continue;

                    const float Ex = A.X + Dx * Along - P.X;
                    const float Ey = A.Y + Dy * Along - P.Y;
                    const float Ez = A.Z + Dz * Along - P.Z;
                    if (Ex * Ex + Ey * Ey + Ez * Ez > Squared)
                        continue;

                    OnEdge.emplace_back(Along, Candidate);
                }
            }

            if (OnEdge.empty())
                continue;

            std::sort(OnEdge.begin(), OnEdge.end(),
                      [](const auto& L, const auto& R) { return L.first < R.first; });
            OnEdge.erase(std::unique(OnEdge.begin(), OnEdge.end(),
                                     [](const auto& L, const auto& R) { return L.second == R.second; }),
                         OnEdge.end());

            const TyreCornerRecord& Head = Corners[Face + Side];
            const TyreCornerRecord& Tail = Corners[Face + (Side + 1u) % 3u];
            for (const auto& Insert : OnEdge)
            {
                // 📝 📐 The inserted corner is interpolated, not copied. It sits on the edge, so its
                //    attributes are the edge's attributes at that parameter; copying either end would
                //    kink the normal and show as a facet seam along an otherwise flat floor.
                const float Mix = Insert.first;
                TyreCornerRecord Blend;
                Blend.NormalX = Head.NormalX + (Tail.NormalX - Head.NormalX) * Mix;
                Blend.NormalY = Head.NormalY + (Tail.NormalY - Head.NormalY) * Mix;
                Blend.NormalZ = Head.NormalZ + (Tail.NormalZ - Head.NormalZ) * Mix;
                Blend.U       = Head.U + (Tail.U - Head.U) * Mix;
                Blend.V       = Head.V + (Tail.V - Head.V) * Mix;
                Blend.Group   = Head.Group;
                Ring.push_back(Insert.second);
                RingCorner.push_back(Blend);
                ++Splits;
            }
        }

        // 📝 ③ Fan to a centre point, never to a ring vertex.
        //    ⚠️ A fan from Ring[0] recreates the very edge it just split. Splitting A–B at P gives the
        //    ring A,P,B,C, and the fan's first triangle is A,P,B — which closes through B–A. The
        //    neighbour across that edge has already been split into A–P and P–B, so the recreated A–B
        //    has one user, and the T-junction comes back as a non-manifold edge instead. That is exactly
        //    what the repair's rising non-manifold count was reporting.
        //    A centre point cannot do this: every edge it creates ends at the centre, and every ring edge
        //    is used by exactly one fan triangle.
        if (Ring.size() == 3u)
        {
            Rebuilt.push_back(Ring[0]);
            Rebuilt.push_back(Ring[1]);
            Rebuilt.push_back(Ring[2]);
            RebuiltCorners.push_back(RingCorner[0]);
            RebuiltCorners.push_back(RingCorner[1]);
            RebuiltCorners.push_back(RingCorner[2]);
            continue;
        }

        float Cx = 0.0f, Cy = 0.0f, Cz = 0.0f;
        TyreCornerRecord Middle = RingCorner[0];
        float Mnx = 0.0f, Mny = 0.0f, Mnz = 0.0f, Mu = 0.0f, Mv = 0.0f;
        for (size_t Step = 0; Step < Ring.size(); ++Step)
        {
            const TyrePositionRecord& Point = Positions[Ring[Step]];
            Cx += Point.X;  Cy += Point.Y;  Cz += Point.Z;
            Mnx += RingCorner[Step].NormalX;
            Mny += RingCorner[Step].NormalY;
            Mnz += RingCorner[Step].NormalZ;
            Mu  += RingCorner[Step].U;
            Mv  += RingCorner[Step].V;
        }
        const float Share = 1.0f / static_cast<float>(Ring.size());
        Middle.NormalX = Mnx * Share;
        Middle.NormalY = Mny * Share;
        Middle.NormalZ = Mnz * Share;
        Middle.U       = Mu * Share;
        Middle.V       = Mv * Share;

        const uint32_t Centre = WeldPosition(Cx * Share, Cy * Share, Cz * Share);

        for (size_t Step = 0; Step < Ring.size(); ++Step)
        {
            const size_t Next = (Step + 1u) % Ring.size();
            Rebuilt.push_back(Ring[Step]);
            Rebuilt.push_back(Ring[Next]);
            Rebuilt.push_back(Centre);
            RebuiltCorners.push_back(RingCorner[Step]);
            RebuiltCorners.push_back(RingCorner[Next]);
            RebuiltCorners.push_back(Middle);
        }
    }

    Indices.swap(Rebuilt);
    Corners.swap(RebuiltCorners);
    return Splits;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                 WATERTIGHTNESS LEDGER
//------------------------------------------------------------------------------------------------------------------------

TyreMeshMetrics TyreMeshStructure::QueryMetrics() const noexcept
{
    TyreMeshMetrics Metrics;
    Metrics.PositionCount = static_cast<uint32_t>(Positions.size());
    Metrics.TriangleCount = static_cast<uint32_t>(Indices.size() / 3u);
    Metrics.QuadCount     = QuadTally;
    Metrics.LooseTriangle = Metrics.TriangleCount - QuadTally * 2u;

    // 📝 ① Count how many triangles use each undirected edge. An edge is keyed by its two position
    //    indices in ascending order, so the two triangles that share it agree on the key by construction.
    std::unordered_map<uint64_t, uint32_t> EdgeUse;
    EdgeUse.reserve(Indices.size());

    std::unordered_map<uint64_t, uint32_t> FaceUse;
    FaceUse.reserve(Indices.size() / 3u);

    for (size_t Offset = 0; Offset + 2u < Indices.size(); Offset += 3u)
    {
        const uint32_t A = Indices[Offset];
        const uint32_t B = Indices[Offset + 1u];
        const uint32_t C = Indices[Offset + 2u];

        // 📝 ② A repeated index is degenerate by inspection; so is a vanishing cross product. Both are
        //    counted before the edges, because a degenerate triangle's edges would corrupt the ledger.
        if (A == B || B == C || A == C)
        {
            ++Metrics.DegenerateCount;
            continue;
        }

        const TyrePositionRecord& Pa = Positions[A];
        const TyrePositionRecord& Pb = Positions[B];
        const TyrePositionRecord& Pc = Positions[C];

        const float Ux = Pb.X - Pa.X, Uy = Pb.Y - Pa.Y, Uz = Pb.Z - Pa.Z;
        const float Vx = Pc.X - Pa.X, Vy = Pc.Y - Pa.Y, Vz = Pc.Z - Pa.Z;
        const float Cx = Uy * Vz - Uz * Vy;
        const float Cy = Uz * Vx - Ux * Vz;
        const float Cz = Ux * Vy - Uy * Vx;

        // 📐 Twice the area. Compared against the square of the weld tolerance so the threshold for
        //    "no area" is consistent with the threshold for "same point".
        constexpr float AreaFloor = WeldTolerance * WeldTolerance;
        if (Cx * Cx + Cy * Cy + Cz * Cz <= AreaFloor * AreaFloor)
        {
            ++Metrics.DegenerateCount;
            continue;
        }

        uint32_t Sorted[3] = { A, B, C };
        for (int Outer = 0; Outer < 2; ++Outer)
            for (int Inner = 0; Inner < 2 - Outer; ++Inner)
                if (Sorted[Inner] > Sorted[Inner + 1])
                {
                    const uint32_t Swap = Sorted[Inner];
                    Sorted[Inner]       = Sorted[Inner + 1];
                    Sorted[Inner + 1]   = Swap;
                }

        const uint64_t FaceKey = (static_cast<uint64_t>(Sorted[0]) * 0x9E3779B1ull + Sorted[1])
                               * 0x9E3779B1ull + Sorted[2];
        if (++FaceUse[FaceKey] > 1u)
            ++Metrics.DuplicateCount;

        const uint32_t Pairs[3][2] = { { A, B }, { B, C }, { C, A } };
        for (const auto& Pair : Pairs)
        {
            const uint32_t Low  = Pair[0] < Pair[1] ? Pair[0] : Pair[1];
            const uint32_t High = Pair[0] < Pair[1] ? Pair[1] : Pair[0];
            ++EdgeUse[(static_cast<uint64_t>(Low) << 32) | static_cast<uint64_t>(High)];
        }
    }

    // 📝 ③ One use is an open edge, three or more is a non-manifold junction. Exactly two is closed.
    for (const auto& Entry : EdgeUse)
    {
        if (Entry.second == 1u)
            ++Metrics.BoundaryEdge;
        else if (Entry.second > 2u)
            ++Metrics.NonManifoldEdge;
    }

    return Metrics;
}

}   // namespace Frontier
