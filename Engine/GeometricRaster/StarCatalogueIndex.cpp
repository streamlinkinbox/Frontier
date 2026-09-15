//============================================================================================================================================
//                                                    STARCATALOGUEINDEX.CPP
//============================================================================================================================================

#include "StarCatalogueIndex.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace Frontier {

namespace {

constexpr uint32_t kMagic   = 0x54414C53u;   // 'SLAT'
constexpr uint32_t kVersion = 1u;

// The angular radius a star is drawn at. Stars are point sources, but a point cannot be rasterised — this is
//    the glow radius, and it is what decides how far into neighbouring cells a star must be duplicated.
constexpr float kStarAngularRadius = 0.0025f;   // [rad] ≈ 0.14°, a little over half the moon's radius

// Octahedral projection: a direction on the sphere → a point on the unit square. Equal-area enough that cells
//    hold comparable numbers of stars, and unlike a latitude/longitude grid it does not collapse at the poles
//    into cells holding hundreds of stars each.
void DirectionToSquare(float X, float Y, float Z, float& U, float& V) noexcept
{
    const float Norm = std::fabs(X) + std::fabs(Y) + std::fabs(Z);
    if (Norm < 1e-9f) { U = 0.5f; V = 0.5f; return; }

    float Px = X / Norm;
    float Py = Y / Norm;
    if (Z < 0.0f)
    {
        const float Ax = (1.0f - std::fabs(Py)) * (Px >= 0.0f ? 1.0f : -1.0f);
        const float Ay = (1.0f - std::fabs(Px)) * (Py >= 0.0f ? 1.0f : -1.0f);
        Px = Ax; Py = Ay;
    }
    U = Px * 0.5f + 0.5f;
    V = Py * 0.5f + 0.5f;
}

} // namespace

uint32_t StarCatalogueIndex::CellForDirection(float X, float Y, float Z) noexcept
{
    float U = 0.0f, V = 0.0f;
    DirectionToSquare(X, Y, Z, U, V);
    const uint32_t Column = std::min(static_cast<uint32_t>(U * kGridResolution), kGridResolution - 1u);
    const uint32_t Row    = std::min(static_cast<uint32_t>(V * kGridResolution), kGridResolution - 1u);
    return Row * kGridResolution + Column;
}

bool StarCatalogueIndex::Load(const std::string& Path) noexcept
{
    BinnedStars.clear();
    Cells.clear();
    SourceCount = 0u;

    std::ifstream File(Path, std::ios::binary);
    if (!File) return false;

    uint32_t Header[4]{};
    File.read(reinterpret_cast<char*>(Header), sizeof(Header));
    if (!File || Header[0] != kMagic || Header[1] != kVersion) return false;

    const uint32_t Count = Header[2];
    // A malformed count would otherwise reserve gigabytes before the read fails.
    if (Count == 0u || Count > 200000u) return false;

    struct SourceStar { float X, Y, Z, Luminance, Red, Green, Blue; };
    std::vector<SourceStar> Source(Count);
    File.read(reinterpret_cast<char*>(Source.data()), static_cast<std::streamsize>(Count * sizeof(SourceStar)));
    if (!File) return false;

    SourceCount = Count;

    // ── Bin ──────────────────────────────────────────────────────────────────────────────────────────────────
    // Two passes: count per cell, then fill. One pass with per-cell vectors would allocate 1 024 times and
    //    fragment; this allocates twice and leaves the stars contiguous, which is what the GPU wants.
    std::vector<std::vector<uint32_t>> Membership(kCellCount);

    for (uint32_t I = 0u; I < Count; ++I)
    {
        const SourceStar& S = Source[I];

        // ⚠️ A star is placed in every cell its glow touches, not just the cell its centre lands in. Sampling
        //    the disc's rim is a cheap way to find those: a star exactly on a boundary must be found from both
        //    sides or it winks out as the camera pans across the seam.
        uint32_t Touched[8]{};
        uint32_t TouchedCount = 0u;

        const auto Remember = [&](uint32_t Cell)
        {
            for (uint32_t J = 0u; J < TouchedCount; ++J) if (Touched[J] == Cell) return;
            if (TouchedCount < 8u) Touched[TouchedCount++] = Cell;
        };

        Remember(CellForDirection(S.X, S.Y, S.Z));

        // Any two perpendicular directions will do; the disc is symmetric about the star.
        const float Ax = std::fabs(S.X), Ay = std::fabs(S.Y), Az = std::fabs(S.Z);
        float Ux = 0.0f, Uy = 0.0f, Uz = 0.0f;
        if (Ax <= Ay && Ax <= Az) { Ux = 0.0f; Uy = -S.Z; Uz = S.Y; }
        else if (Ay <= Az)        { Ux = -S.Z; Uy = 0.0f; Uz = S.X; }
        else                      { Ux = -S.Y; Uy = S.X;  Uz = 0.0f; }
        const float UNorm = std::sqrt(Ux * Ux + Uy * Uy + Uz * Uz);
        if (UNorm > 1e-9f) { Ux /= UNorm; Uy /= UNorm; Uz /= UNorm; }

        const float Vx = S.Y * Uz - S.Z * Uy;
        const float Vy = S.Z * Ux - S.X * Uz;
        const float Vz = S.X * Uy - S.Y * Ux;

        for (uint32_t Step = 0u; Step < 4u; ++Step)
        {
            const float Angle = 1.57079633f * static_cast<float>(Step);
            const float Cos = std::cos(Angle) * kStarAngularRadius;
            const float Sin = std::sin(Angle) * kStarAngularRadius;
            Remember(CellForDirection(S.X + Ux * Cos + Vx * Sin,
                                      S.Y + Uy * Cos + Vy * Sin,
                                      S.Z + Uz * Cos + Vz * Sin));
        }

        for (uint32_t J = 0u; J < TouchedCount; ++J) Membership[Touched[J]].push_back(I);
    }

    Cells.resize(kCellCount);
    uint32_t Running = 0u;
    for (uint32_t Cell = 0u; Cell < kCellCount; ++Cell)
    {
        Cells[Cell].First = Running;
        Cells[Cell].Count = static_cast<uint32_t>(Membership[Cell].size());
        Running += Cells[Cell].Count;
    }

    BinnedStars.reserve(Running);
    for (uint32_t Cell = 0u; Cell < kCellCount; ++Cell)
        for (uint32_t Index : Membership[Cell])
        {
            const SourceStar& S = Source[Index];
            StarRecord Record{};
            Record.DirectionX = S.X; Record.DirectionY = S.Y; Record.DirectionZ = S.Z;
            Record.Luminance  = S.Luminance;
            Record.ColourRed  = S.Red; Record.ColourGreen = S.Green; Record.ColourBlue = S.Blue;
            BinnedStars.push_back(Record);
        }

    return true;
}

} // namespace Frontier
