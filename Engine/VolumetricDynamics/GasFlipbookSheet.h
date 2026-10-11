//==============================================================================================================================================
//                                                         GASFLIPBOOKSHEET.H
//==============================================================================================================================================
// 📦 The bottom rung of the ladder: a gas effect advanced once into a sheet of tiles, and played back afterwards with no solver at all.
//
// GasQualityAllowance.h ends its ladder at Flipbook and says of it "no solver at all; a pre-advanced sheet
//    shared by every instance". This file is what that rung actually reads. One bake, done offline or once at
//    load, produces a square sheet of square tiles; at runtime a distant plume costs one textured quad and an
//    index computed from elapsed seconds.
//
//    | Rung     | Lattice  | Cost per instance                  |
//    |----------|----------|------------------------------------|
//    | Hero     | 128³     | ~94 MB and a solve every tick      |
//    | Far      | 32³      | ~1.5 MB and a solve every 4th tick |
//    | Flipbook | none     | one quad; the sheet is shared      |
//
// 🔴 THE SHEET IS SHARED AND THE PLAYBACK IS NOT. Forty distant chimneys are one sheet and forty elapsed
//    times. That is the entire reason the rung exists, and it is why nothing in this header holds pixels for
//    an instance — TileAtElapsed() takes a time and answers an index, and that is all an instance owns.
//
// 📐 THE LAYOUT IS THE BROWSER'S, DELIBERATELY NOT A BETTER ONE. Experimental/Fluid/src/FlipbookSequence.js
//    bakes the same four tile counts, the same four tile sizes, the same four rates and the same 0–3 s warm-up,
//    orders tiles left to right then top to bottom, and writes straight (unpremultiplied) alpha with the
//    coverage in the alpha channel. A native sheet and a browser sheet are therefore interchangeable, which is
//    what lets an artist bake in the page and ship the result without a conversion step that could differ.
//
// ⚠️ THE GL READBACK IS BOTTOM-UP AND A SHEET IS TOP-DOWN. AssignTile() flips. Getting that wrong produces a
//    plume that falls instead of rising, which reads as a physics defect rather than a row order one.
//
// 💾 A 64-tile sheet of 256 px tiles is 2048 × 2048 × 4 = 16 MB uncompressed, shared by every instance of the
//    effect in the level. The same effect at Far, for twelve instances, is 18 MB and twelve solves.

#pragma once

#include <cstdint>
#include <string>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE LAYOUT
//------------------------------------------------------------------------------------------------------------------------

constexpr std::string_view GasFlipbookFormat  = "frontier-fluid-flipbook";
constexpr int64_t          GasFlipbookVersion = 1;

// 📝 The four supported counts, sizes and rates are the browser's. They are a closed set rather than a range
//    because every one of them makes a square-ish sheet whose side is a power of two, and a sheet that is not
//    is a texture upload the device pads behind one's back.
constexpr uint32_t GasFlipbookCounts[4] = { 4u, 16u, 32u, 64u };
constexpr uint32_t GasFlipbookSizes[4]  = { 64u, 128u, 256u, 512u };
constexpr uint32_t GasFlipbookRates[4]  = { 12u, 24u, 30u, 60u };
constexpr float    GasFlipbookWarmupMost = 3.0f;   // [s] - longer than this, author a shorter effect


struct GasFlipbookLayout
{
    uint32_t Count    = 0u;      // [-]  - tiles in the sheet
    uint32_t Size     = 0u;      // [px] - edge of one square tile
    uint32_t Rate     = 0u;      // [1/s]- tiles per second of playback
    float    Warmup   = 0.0f;    // [s]  - advanced before the first tile is kept
    uint32_t Columns  = 0u;      // [-]
    uint32_t Rows     = 0u;      // [-]
    uint32_t Width    = 0u;      // [px] - Columns · Size
    uint32_t Height   = 0u;      // [px] - Rows · Size
    bool     Admitted = false;   // [-]  - false when the request was not one of the supported shapes
};


/// 📦 Whether a reading is one of a closed set.
/// in    Reading     [-]  the requested number
/// in    Supported   [-]  the four supported ones
/// out   bool        [-]  true when it is exactly one of them
/// cost  ✔️
/// tag   internal, nonallocating, nonthrowing
inline bool OneOfFour(uint32_t Reading, const uint32_t Supported[4]) noexcept
{
    return Reading == Supported[0] || Reading == Supported[1]
        || Reading == Supported[2] || Reading == Supported[3];
}


/// 📦 The sheet a request implies, or an unadmitted layout saying the request was not one we bake.
/// in    Count      [-]   tiles; 4, 16, 32 or 64
/// in    Size       [px]  tile edge; 64, 128, 256 or 512
/// in    Rate       [1/s] playback rate; 12, 24, 30 or 60
/// in    Warmup     [s]   advanced before the first tile is kept; 0 to 3
/// out   GasFlipbookLayout  [-]  Admitted false when anything was out of the set; every other member is then zero
/// err   refusing is the whole point: a sheet of 7 tiles of 100 px is a texture nothing can address cheaply,
///       and the browser refuses the same request with the same words
/// note  the column count is the ceiling of the square root, so 32 tiles is 6 × 6 with four empty — the empty
///       tiles cost nothing at runtime because TileAtElapsed() never names them
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasFlipbookLayout SpecifyFlipbook(uint32_t Count, uint32_t Size, uint32_t Rate, float Warmup) noexcept
{
    GasFlipbookLayout Layout;
    if (!OneOfFour(Count, GasFlipbookCounts) || !OneOfFour(Size, GasFlipbookSizes)
        || !OneOfFour(Rate, GasFlipbookRates) || !(Warmup >= 0.0f) || Warmup > GasFlipbookWarmupMost)
    {
        return Layout;
    }

    uint32_t Columns = 1u;
    while (Columns * Columns < Count) ++Columns;          // integer ceiling of the square root, exactly

    Layout.Count    = Count;
    Layout.Size     = Size;
    Layout.Rate     = Rate;
    Layout.Warmup   = Warmup;
    Layout.Columns  = Columns;
    Layout.Rows     = (Count + Columns - 1u) / Columns;
    Layout.Width    = Columns * Size;
    Layout.Height   = Layout.Rows * Size;
    Layout.Admitted = true;
    return Layout;
}


/// 📦 The seconds of effect a sheet covers.
/// in    Layout   [-]  an admitted layout
/// out   float    [s]  Count ÷ Rate; zero for an unadmitted layout
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float FlipbookDuration(const GasFlipbookLayout& Layout) noexcept
{
    return Layout.Admitted ? static_cast<float>(Layout.Count) / static_cast<float>(Layout.Rate) : 0.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHERE A TILE SITS
//------------------------------------------------------------------------------------------------------------------------

// One tile's place in the sheet, in pixels and in the normalised coordinates a quad samples with.
struct GasFlipbookTile
{
    uint32_t X = 0u, Y = 0u;                              // [px] - top-left, origin top-left
    uint32_t Size = 0u;                                   // [px]
    float    Uv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };          // [-]  - x, y, width, height, all normalised
};


/// 📦 Where tile Index lives, left to right then top to bottom.
/// in    Layout             [-]  an admitted layout
/// in    Index              [-]  tile number; an index past the end answers tile zero
/// out   GasFlipbookTile    [-]  pixels and normalised coordinates
/// err   an out-of-range index answers the first tile rather than a region outside the sheet, because a quad
///       sampling outside a sheet shows whatever the atlas neighbour was and nobody ever reads that as an error
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasFlipbookTile TileAt(const GasFlipbookLayout& Layout, uint32_t Index) noexcept
{
    GasFlipbookTile Tile;
    if (!Layout.Admitted) return Tile;
    if (Index >= Layout.Count) Index = 0u;

    Tile.X    = (Index % Layout.Columns) * Layout.Size;
    Tile.Y    = (Index / Layout.Columns) * Layout.Size;
    Tile.Size = Layout.Size;
    Tile.Uv[0] = static_cast<float>(Tile.X) / static_cast<float>(Layout.Width);
    Tile.Uv[1] = static_cast<float>(Tile.Y) / static_cast<float>(Layout.Height);
    Tile.Uv[2] = static_cast<float>(Layout.Size) / static_cast<float>(Layout.Width);
    Tile.Uv[3] = static_cast<float>(Layout.Size) / static_cast<float>(Layout.Height);
    return Tile;
}


/// 📦 Which tile an instance is showing after this many seconds.
/// in    Layout     [-]  an admitted layout
/// in    Elapsed    [s]  since the effect began; negative is treated as zero
/// in    Repeats    [-]  true for a loop, false to hold the last tile and stop
/// out   uint32_t   [-]  tile index
/// note  this is the whole of an instance's state at this rung: one float in, one index out, nothing retained
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline uint32_t TileAtElapsed(const GasFlipbookLayout& Layout, float Elapsed, bool Repeats) noexcept
{
    if (!Layout.Admitted || Layout.Count == 0u) return 0u;
    if (!(Elapsed > 0.0f)) return 0u;

    const float    Exact = Elapsed * static_cast<float>(Layout.Rate);
    const uint64_t Whole = static_cast<uint64_t>(Exact);
    if (Whole >= Layout.Count)
    {
        return Repeats ? static_cast<uint32_t>(Whole % Layout.Count) : Layout.Count - 1u;
    }
    return static_cast<uint32_t>(Whole);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FILLING A SHEET
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The bytes one sheet needs, RGBA.
/// in    Layout     [-]  an admitted layout
/// out   uint64_t   [B]  Width · Height · 4; zero when the layout was refused
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline uint64_t FlipbookSheetBytes(const GasFlipbookLayout& Layout) noexcept
{
    return static_cast<uint64_t>(Layout.Width) * Layout.Height * 4ull;
}


/// 📦 Copies one rendered tile into the sheet, flipping its rows and taking its alpha straight.
/// in    Sheet        [-]   destination, FlipbookSheetBytes() long, RGBA, origin top-left
/// in    Layout       [-]   an admitted layout
/// in    Index        [-]   which tile; out of range is ignored rather than written elsewhere
/// in    Rendered     [-]   Size · Size · 4 bytes as read back from the device: BOTTOM-UP, premultiplied
/// in    Transparent  [-]   true to keep coverage in alpha, false to write an opaque tile
/// out   -
/// err   a null sheet or an unadmitted layout writes nothing
/// note  the browser does exactly this in UnpackFrame(): the maximum of the four channels is the coverage, and
///       the colour is divided back out of it so that bilinear sampling between a lit tile and empty space does
///       not darken the edge of the plume — a premultiplied sheet sampled that way grows a black fringe
/// cost  ✔️  Size² pixels
/// tag   api, nonallocating, nonthrowing
inline void AssignTile(uint8_t* Sheet, const GasFlipbookLayout& Layout, uint32_t Index,
                       const uint8_t* Rendered, bool Transparent) noexcept
{
    if (Sheet == nullptr || Rendered == nullptr || !Layout.Admitted || Index >= Layout.Count) return;

    const GasFlipbookTile Tile = TileAt(Layout, Index);
    for (uint32_t Row = 0u; Row < Layout.Size; ++Row)
    {
        for (uint32_t Column = 0u; Column < Layout.Size; ++Column)
        {
            const uint32_t Source = ((Layout.Size - 1u - Row) * Layout.Size + Column) * 4u;   // bottom-up
            const uint32_t Target = ((Tile.Y + Row) * Layout.Width + Tile.X + Column) * 4u;

            uint32_t Coverage = 255u;
            if (Transparent)
            {
                Coverage = Rendered[Source + 3u];
                for (uint32_t Channel = 0u; Channel < 3u; ++Channel)
                {
                    if (Rendered[Source + Channel] > Coverage) Coverage = Rendered[Source + Channel];
                }
            }

            for (uint32_t Channel = 0u; Channel < 3u; ++Channel)
            {
                const uint32_t Straight = Coverage != 0u
                    ? (static_cast<uint32_t>(Rendered[Source + Channel]) * 255u + Coverage / 2u) / Coverage
                    : 0u;
                Sheet[Target + Channel] = static_cast<uint8_t>(Straight > 255u ? 255u : Straight);
            }
            Sheet[Target + 3u] = static_cast<uint8_t>(Coverage);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DESCRIPTOR
//------------------------------------------------------------------------------------------------------------------------

// What travels beside the sheet. The browser writes the same fields as JSON because a web page already has a
//    JSON parser; this writes TOML because the engine already has toml++ and no JSON parser at all, which is
//    the same argument GasSceneCodec.h makes at greater length.
struct GasFlipbookDescriptor
{
    std::string       Name  = "scene";           // [-] - what the sheet is of
    std::string       Image = "scene.flipbook.png";
    std::string       Preset;                    // [-] - the preset key it was baked from, if any
    GasFlipbookLayout Layout;
    bool              Transparent = true;        // [-] - straight coverage in alpha, or an opaque tile
    bool              StartBurst  = false;       // [-] - the bake began with the preset's detonation
};


/// 📦 Spells a decimal with a fractional part, so that TOML reads it back as a float and not as an integer.
/// in    Reading   [-]  any finite float
/// out   string    [-]  always containing a point
/// note  the same rule GasSceneCodec.h follows, and for the same reason: toml++ hands back exactly the type
///       that was written, and a descriptor whose warmup is "0" refuses to load as a float
/// cost  ✔️
/// tag   internal, allocating, nonthrowing
inline std::string SpellFlipbookFloat(float Reading) noexcept
{
    std::string Spelled = std::to_string(static_cast<double>(Reading));
    while (Spelled.size() > 1u && Spelled.back() == '0' && Spelled[Spelled.size() - 2u] != '.')
    {
        Spelled.pop_back();
    }
    return Spelled;
}


/// 📦 The descriptor as TOML, ready to sit beside the .png.
/// in    Descriptor   [-]  what was baked
/// out   string       [-]  the whole file, newline-terminated; empty when the layout was never admitted
/// err   an unadmitted layout spells nothing, because a descriptor for a sheet that does not exist is worse
///       than no descriptor at all
/// note  every tile's normalised coordinates are written out rather than left to be recomputed. They are
///       derivable, and writing them is how a consumer that disagrees about the derivation finds out
/// cost  ✔️
/// tag   api, allocating, nonthrowing
inline std::string SpellFlipbookDescriptor(const GasFlipbookDescriptor& Descriptor) noexcept
{
    if (!Descriptor.Layout.Admitted) return std::string();
    const GasFlipbookLayout& Layout = Descriptor.Layout;

    std::string Out;
    Out += "format = \"";  Out += GasFlipbookFormat;  Out += "\"\n";
    Out += "version = " + std::to_string(GasFlipbookVersion) + "\n";
    Out += "name = \"" + Descriptor.Name + "\"\n";
    Out += "image = \"" + Descriptor.Image + "\"\n";
    if (!Descriptor.Preset.empty()) Out += "preset = \"" + Descriptor.Preset + "\"\n";
    Out += "\n[sheet]\n";
    Out += "width = "  + std::to_string(Layout.Width)  + "\n";
    Out += "height = " + std::to_string(Layout.Height) + "\n";
    Out += "columns = " + std::to_string(Layout.Columns) + "\n";
    Out += "rows = "    + std::to_string(Layout.Rows)    + "\n";
    Out += "tile_size = " + std::to_string(Layout.Size)  + "\n";
    Out += "tile_count = " + std::to_string(Layout.Count) + "\n";
    Out += "origin = \"top-left\"\n";
    Out += "tile_order = \"left-to-right, top-to-bottom\"\n";
    Out += "colour_space = \"srgb-tonemapped\"\n";
    Out += "alpha = \"";
    Out += Descriptor.Transparent ? "straight-coverage" : "opaque";
    Out += "\"\n";

    Out += "\n[playback]\n";
    Out += "tiles_per_second = " + std::to_string(Layout.Rate) + "\n";
    Out += "duration = " + SpellFlipbookFloat(FlipbookDuration(Layout)) + "\n";
    Out += "warmup = " + SpellFlipbookFloat(Layout.Warmup) + "\n";
    Out += std::string("start_burst = ") + (Descriptor.StartBurst ? "true" : "false") + "\n";

    Out += "\n[[tile]]\n";
    for (uint32_t Index = 0u; Index < Layout.Count; ++Index)
    {
        const GasFlipbookTile Tile = TileAt(Layout, Index);
        if (Index > 0u) Out += "\n[[tile]]\n";
        Out += "index = " + std::to_string(Index) + "\n";
        Out += "x = " + std::to_string(Tile.X) + "\n";
        Out += "y = " + std::to_string(Tile.Y) + "\n";
        Out += "uv = [" + SpellFlipbookFloat(Tile.Uv[0]) + ", " + SpellFlipbookFloat(Tile.Uv[1]) + ", "
                        + SpellFlipbookFloat(Tile.Uv[2]) + ", " + SpellFlipbookFloat(Tile.Uv[3]) + "]\n";
    }
    return Out;
}

}   // namespace Frontier
