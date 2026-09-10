//============================================================================================================================================
//                                                     STARCATALOGUEINDEX.H
//============================================================================================================================================
// 🧩 Loads a star catalogue and bins it by direction so a shader can find the few stars near a ray.
//
//    🔴 The binning is the entire reason this exists. Testing every star per pixel is 9 100 dot products at
//    1080p over a visible sky — around ten billion a frame, which is not a performance problem so much as an
//    impossibility. Binning into an octahedral grid at load time reduces that to the handful sharing the ray's
//    own cell: roughly a thousandfold, and the difference between a real catalogue being affordable and not.
//
//    ⚠️ A star near a cell boundary is placed in every cell its angular radius touches. Without that it winks
//    out as the camera pans across the seam — a defect that looks like flickering geometry rather than a
//    lookup bug, and one that only appears in motion.
//
//    Named …Index because it is a spatial lookup structure (CLAUDE.md §2.15). It builds a table and answers
//    "what is near this direction"; it does not integrate, solve or decode anything.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        RECORDS
//------------------------------------------------------------------------------------------------------------------------

// One star, as the shader consumes it. Direction rather than the two angles: the conversion is done once at
//    load rather than per pixel, and a unit vector is what a dot product wants anyway.
struct StarRecord
{
    float DirectionX = 0.0f, DirectionY = 0.0f, DirectionZ = 0.0f;   // [-]      unit, equatorial J2000
    float Luminance  = 0.0f;                                          // [-]      relative; magnitude 0 is 1.0
    float ColourRed  = 1.0f, ColourGreen = 1.0f, ColourBlue = 1.0f;   // [-]      linear, from B−V
    float Padding    = 0.0f;                                          // [-]      std430 wants 8 floats, not 7
};
static_assert(sizeof(StarRecord) == 32u, "StarRecord must be 32 bytes so std430 needs no per-element padding");

// Where one cell's stars live in the flat array. Two uints rather than a pointer so the whole thing uploads as
//    a buffer with no fix-up on the GPU side.
struct StarCellRecord
{
    uint32_t First = 0u;   // [idx] into the binned star array
    uint32_t Count = 0u;   // [cnt]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        INDEX
//------------------------------------------------------------------------------------------------------------------------

class StarCatalogueIndex
{
public:
    // 32 × 32 octahedral cells = 1 024. With the full 9 100-star catalogue that averages nine stars per cell,
    //    which is small enough to walk per pixel and large enough that the tables stay tiny.
    static constexpr uint32_t kGridResolution = 32u;
    static constexpr uint32_t kCellCount      = kGridResolution * kGridResolution;

    // Reads the binary produced by Tools/StarCatalogue/ConvertHygCatalogue.py. Returns false and leaves the
    //    index empty when the file is absent or malformed — a missing catalogue must degrade to "no stars",
    //    never to a crash or to garbage directions.
    [[nodiscard]] bool Load(const std::string& Path) noexcept;

    [[nodiscard]] bool Empty() const noexcept { return BinnedStars.empty(); }
    [[nodiscard]] const std::vector<StarRecord>&     QueryStars() const noexcept { return BinnedStars; }
    [[nodiscard]] const std::vector<StarCellRecord>& QueryCells() const noexcept { return Cells; }

    // How many stars were read before binning. Binning duplicates boundary stars, so QueryStars().size() is
    //    larger — reporting both makes the duplication visible rather than looking like a parse error.
    [[nodiscard]] uint32_t QuerySourceCount() const noexcept { return SourceCount; }

    // Octahedral mapping, shared with the shader. A direction becomes a cell; the same arithmetic must exist on
    //    both sides or a pixel looks in the wrong cell and the sky is simply empty.
    [[nodiscard]] static uint32_t CellForDirection(float X, float Y, float Z) noexcept;

private:
    std::vector<StarRecord>     BinnedStars;   // stars grouped by cell, cells contiguous
    std::vector<StarCellRecord> Cells;
    uint32_t                    SourceCount = 0u;
};

} // namespace Frontier
