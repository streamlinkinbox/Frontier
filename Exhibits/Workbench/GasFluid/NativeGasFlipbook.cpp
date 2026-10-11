//==============================================================================================================================================
//                                                         NATIVEGASFLIPBOOK.CPP
//==============================================================================================================================================
// 📦 Executed proof for the bottom rung: the sheet layout, the row flip, the straight alpha, the playback index, and one real bake of a camp fire.
//
// Dependency-free and windowless. The sheet below is produced by the shipped solver and the shipped volume
//    integration — nothing in this file paints smoke — so the capture is a bake, not an illustration.
//
//    ① THE LAYOUT IS THE BROWSER'S. A sheet baked in the page and a sheet baked here must address the same
//       way, or an artist's bake stops working the day it reaches the engine.
//    ② THE ROWS ARE FLIPPED EXACTLY ONCE. A device readback is bottom-up and a sheet is top-down; getting it
//       wrong gives a plume that falls, which every reviewer reads as a physics defect.
//    ③ THE ALPHA IS STRAIGHT, NOT PREMULTIPLIED. A premultiplied sheet grows a black fringe when sampled
//       bilinearly, and the fringe is blamed on the renderer for a week.
//    ④ PLAYBACK IS ONE FLOAT IN AND ONE INDEX OUT. That is the entire per-instance cost of this rung, and it
//       is why forty chimneys share one sheet.
//    ⑤ THE BAKE IS REPRODUCIBLE. Two bakes of the same preset are the same bytes, or the sheet cannot be
//       shipped as content.

#include "CoarseGasField.h"
#include "GasFlipbookSheet.h"
#include "GasPresetLibrary.h"
#include "GasQualityAllowance.h"
#include "VolumeRaymarch.h"

#include "PngWriteCounterpart.h"

#include <toml++/toml.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Claim);
    if (!Condition) throw std::runtime_error(Claim);
}

void Banner(const char* Title)
{
    std::printf("\n%s\n", Title);
}

// The seam from the integration to the solver's reading, as NativeGasRaymarch.cpp uses it.
VolumeSample ReadCoarseField(const void* Context, const float Coordinate[3])
{
    const CoarseGasField& Field = *static_cast<const CoarseGasField*>(Context);
    const float Lattice[3] = { Coordinate[0] * static_cast<float>(CoarseExtent) - 0.5f,
                               Coordinate[1] * static_cast<float>(CoarseExtent) - 0.5f,
                               Coordinate[2] * static_cast<float>(CoarseExtent) - 0.5f };
    VolumeSample Reading;
    Reading.Smoke       = ReadTrilinear(Field.Smoke,       Lattice[0], Lattice[1], Lattice[2]);
    Reading.Temperature = ReadTrilinear(Field.Temperature, Lattice[0], Lattice[1], Lattice[2]);
    return Reading;
}


// One tile, rendered the way a device hands it back: RGBA, premultiplied, BOTTOM-UP. The flip and the
//    unpremultiply are AssignTile's job and are deliberately not done here, so that the proof exercises them.
std::vector<uint8_t> RenderTile(const CoarseGasField& Field, const VolumeLighting& Lighting,
                                const float SunDirection[3], uint32_t Size, float DomainDiagonal)
{
    std::vector<uint8_t> Rendered(static_cast<std::size_t>(Size) * Size * 4u, 0u);

    const float Origin[3]  = { 0.5f + 1.9f, 0.5f, 0.72f };
    float Forward[3] = { 0.5f - Origin[0], 0.5f - Origin[1], 0.5f - Origin[2] };
    const float Reach = std::sqrt(Forward[0] * Forward[0] + Forward[1] * Forward[1] + Forward[2] * Forward[2]);
    for (int Axis = 0; Axis < 3; ++Axis) Forward[Axis] /= Reach;

    float Right[3] = { Forward[1], -Forward[0], 0.0f };
    const float RightReach = std::sqrt(Right[0] * Right[0] + Right[1] * Right[1]);
    for (int Axis = 0; Axis < 3; ++Axis) Right[Axis] /= RightReach;
    const float Up[3] = { Right[1] * Forward[2] - Right[2] * Forward[1],
                          Right[2] * Forward[0] - Right[0] * Forward[2],
                          Right[0] * Forward[1] - Right[1] * Forward[0] };

    const float HalfFieldOfView = 0.30f;   // tighter than the inspector preview: a tile is all plume, no room
    for (uint32_t Row = 0u; Row < Size; ++Row)
    for (uint32_t Column = 0u; Column < Size; ++Column)
    {
        // Row zero is the BOTTOM of the picture, which is what a readback gives and what the flip undoes.
        const float ScreenX = (static_cast<float>(Column) + 0.5f) / static_cast<float>(Size) * 2.0f - 1.0f;
        const float ScreenY = (static_cast<float>(Row) + 0.5f) / static_cast<float>(Size) * 2.0f - 1.0f;
        float Direction[3];
        for (int Axis = 0; Axis < 3; ++Axis)
            Direction[Axis] = Forward[Axis] + Right[Axis] * ScreenX * HalfFieldOfView
                                            + Up[Axis]    * ScreenY * HalfFieldOfView;
        const float Length = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1]
                                     + Direction[2] * Direction[2]);
        for (int Axis = 0; Axis < 3; ++Axis) Direction[Axis] /= Length;

        const MarchedRay Marched = MarchVolume(&ReadCoarseField, &Field, Origin, Direction, SunDirection,
                                               Lighting, DomainDiagonal);

        const float Coverage = 1.0f - Marched.Transmittance;
        const std::size_t Slot = (static_cast<std::size_t>(Row) * Size + Column) * 4u;
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Rendered[Slot + static_cast<std::size_t>(Channel)] =
                TransferToDisplay(Marched.Radiance[Channel], Lighting.Exposure);
        }
        Rendered[Slot + 3u] = static_cast<uint8_t>(ClampUnit(Coverage, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return Rendered;
}

}   // namespace


int main()
{
    std::printf("NATIVE GAS FLIPBOOK - the sheet the bottom rung plays, and one real bake of a camp fire\n");
    std::filesystem::create_directories("Exhibits/Gallery/GasFlipbook");

    //---------------------------------------------------------------------------------------------------------
    Banner("① The layout is the browser's, including what it refuses");
    {
        const GasFlipbookLayout Four = SpecifyFlipbook(4u, 64u, 12u, 0.0f);
        Check(Four.Admitted && Four.Columns == 2u && Four.Rows == 2u, "four tiles make a 2 by 2 sheet");
        Check(Four.Width == 128u && Four.Height == 128u, "of 128 pixels square, at the smallest tile size");

        const GasFlipbookLayout Many = SpecifyFlipbook(32u, 128u, 24u, 1.0f);
        Check(Many.Columns == 6u && Many.Rows == 6u,
              "thirty-two tiles make a 6 by 6 sheet with four unused - the ceiling of the square root, as the page does it");
        Check(Many.Width == 768u && Many.Height == 768u, "768 pixels square");
        Check(std::fabs(FlipbookDuration(Many) - 32.0f / 24.0f) < 1e-6f,
              "and it covers a second and a third of effect");
        Check(FlipbookSheetBytes(Many) == 768ull * 768ull * 4ull, "2.25 MB of RGBA, shared by every instance");

        Check(!SpecifyFlipbook(7u, 128u, 24u, 0.0f).Admitted, "seven tiles is refused rather than rounded");
        Check(!SpecifyFlipbook(16u, 100u, 24u, 0.0f).Admitted,
              "and so is a 100 pixel tile - a sheet that is not a power of two is padded behind one's back");
        Check(!SpecifyFlipbook(16u, 128u, 45u, 0.0f).Admitted, "and an unsupported rate");
        Check(!SpecifyFlipbook(16u, 128u, 24u, 4.0f).Admitted,
              "and a four second warm-up: past three, author a shorter effect");
        Check(!SpecifyFlipbook(16u, 128u, 24u, -1.0f).Admitted, "and a negative one");
        Check(FlipbookDuration(SpecifyFlipbook(7u, 128u, 24u, 0.0f)) == 0.0f,
              "a refused layout has no duration either, rather than a plausible-looking zero-sized one");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Where each tile sits, left to right then top to bottom");
    {
        const GasFlipbookLayout Layout = SpecifyFlipbook(16u, 64u, 24u, 0.0f);
        Check(Layout.Columns == 4u, "sixteen tiles is a tidy 4 by 4");

        const GasFlipbookTile First = TileAt(Layout, 0u);
        Check(First.X == 0u && First.Y == 0u, "the first tile is at the top left, because the sheet's origin is");

        const GasFlipbookTile Fifth = TileAt(Layout, 4u);
        Check(Fifth.X == 0u && Fifth.Y == 64u, "the fifth begins the second row");

        const GasFlipbookTile Last = TileAt(Layout, 15u);
        Check(Last.X == 192u && Last.Y == 192u, "and the last is at the bottom right");
        Check(std::fabs(Last.Uv[0] + Last.Uv[2] - 1.0f) < 1e-6f
              && std::fabs(Last.Uv[1] + Last.Uv[3] - 1.0f) < 1e-6f,
              "whose normalised coordinates end exactly at the edge of the sheet, not past it");

        const GasFlipbookTile Past = TileAt(Layout, 99u);
        Check(Past.X == 0u && Past.Y == 0u,
              "an index past the end answers the first tile - a quad sampling outside a sheet shows a neighbour");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("④ Playback is one float in and one index out");
    {
        const GasFlipbookLayout Layout = SpecifyFlipbook(16u, 64u, 24u, 0.0f);   // 16 tiles, 2/3 of a second

        Check(TileAtElapsed(Layout, 0.0f, true) == 0u, "nothing has elapsed, so nothing has advanced");
        Check(TileAtElapsed(Layout, -3.0f, true) == 0u, "and a negative time is the beginning, not the end");
        Check(TileAtElapsed(Layout, 1.0f / 24.0f + 0.001f, true) == 1u, "one tile period in is the second tile");
        Check(TileAtElapsed(Layout, 0.5f, true) == 12u, "half a second in is the thirteenth");

        Check(TileAtElapsed(Layout, 1.0f, true) == 8u, "a looping sheet wraps and keeps playing");
        Check(TileAtElapsed(Layout, 1.0f, false) == 15u, "a one-shot sheet holds its last tile and stops");
        Check(TileAtElapsed(Layout, 10000.0f, false) == 15u, "and keeps holding it, however long the level runs");
        Check(TileAtElapsed(Layout, 10000.0f, true) < Layout.Count,
              "while a looping one never names a tile the sheet does not have");

        const GasFlipbookLayout Refused = SpecifyFlipbook(7u, 64u, 24u, 0.0f);
        Check(TileAtElapsed(Refused, 4.0f, true) == 0u, "and a refused layout plays tile zero forever, harmlessly");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("② and ③ The rows flip exactly once, and the alpha comes out straight");
    {
        const GasFlipbookLayout Layout = SpecifyFlipbook(4u, 64u, 12u, 0.0f);
        std::vector<uint8_t> Sheet(static_cast<std::size_t>(FlipbookSheetBytes(Layout)), 0u);

        // A tile that is black everywhere except its BOTTOM row, which is a half-covered red. After the flip
        //    that row must be at the TOP of the tile in the sheet.
        std::vector<uint8_t> Rendered(64u * 64u * 4u, 0u);
        for (uint32_t Column = 0u; Column < 64u; ++Column)
        {
            const std::size_t Slot = static_cast<std::size_t>(Column) * 4u;   // row 0 = bottom
            Rendered[Slot + 0u] = 64u;    // premultiplied red: 128 straight, at half coverage
            Rendered[Slot + 3u] = 128u;
        }

        AssignTile(Sheet.data(), Layout, 0u, Rendered.data(), true);
        Check(Sheet[(63u * Layout.Width) * 4u + 3u] == 128u,
              "② row zero of a readback is the bottom of the picture, and lands at the bottom of the tile");
        Check(Sheet[3] == 0u, "while the tile's top row is the empty one - the flip happens exactly once");
        Check(Sheet[(63u * Layout.Width) * 4u] == 128u,
              "③ the colour is divided back out of its coverage: 64 premultiplied at half alpha is 128 straight");

        AssignTile(Sheet.data(), Layout, 1u, Rendered.data(), false);
        const std::size_t Opaque = (63u * static_cast<std::size_t>(Layout.Width)
                                    + TileAt(Layout, 1u).X) * 4u;
        Check(Sheet[Opaque + 3u] == 255u, "an opaque bake writes full alpha");
        Check(Sheet[Opaque + 0u] == 64u, "and leaves the colour exactly as it was rendered");

        // Nothing may be written outside the tile that was named.
        const GasFlipbookTile Third = TileAt(Layout, 2u);
        const std::size_t Below = (static_cast<std::size_t>(Third.Y) * Layout.Width + Third.X) * 4u;
        Check(Sheet[Below + 3u] == 0u, "a tile writes inside its own region and nowhere else");

        std::vector<uint8_t> Untouched = Sheet;
        AssignTile(Sheet.data(), Layout, 9u, Rendered.data(), true);
        Check(Sheet == Untouched, "and naming a tile the sheet does not have writes nothing at all");
        AssignTile(nullptr, Layout, 0u, Rendered.data(), true);
        Check(true, "a null sheet is declined rather than written through");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("A real bake: the camp fire preset, through the shipped solver and the shipped integration");

    const GasFlipbookLayout Layout = SpecifyFlipbook(16u, 128u, 12u, 1.5f);
    std::vector<uint8_t> Sheet(static_cast<std::size_t>(FlipbookSheetBytes(Layout)), 0u);
    std::vector<uint32_t> Coverage(Layout.Count, 0u);
    {
        const GasSettings Preset = ConstructPresetSettings("camp_fire");

        CoarseGasField Field;
        const float Origin[3] = { 0.0f, 0.0f, 0.0f };
        ResetField(Field, Origin, 2.0f);

        GasEmission Wood;
        Wood.Position[0] = 1.0f; Wood.Position[1] = 1.0f; Wood.Position[2] = 0.25f;
        Wood.Radius          = 0.22f;
        Wood.SmokeRate       = Preset.EmitterSmoke;
        Wood.TemperatureRate = Preset.EmitterTemperature;
        Wood.Velocity[2]     = Preset.EmitterUpwardVelocity * 0.25f;

        CoarseGasSettings Solver;
        Solver.BuoyancyLift = Preset.Buoyancy * 0.4f;
        Solver.SmokeWeight  = Preset.SmokeWeight * 0.45f;

        VolumeLighting Lighting;
        Lighting.DensityExtinction = Preset.DensityExtinction;
        Lighting.SmokeAlbedo       = Preset.SmokeAlbedo;
        Lighting.FireIntensity     = Preset.FireIntensity;
        Lighting.Exposure          = Preset.Exposure;
        const float Sun[3] = { 0.4f, 0.3f, 0.86f };

        // The warm-up, at the solver's own fixed step. No measured duration reaches it, here or anywhere.
        const uint32_t WarmupSteps = static_cast<uint32_t>(Layout.Warmup * GasMasterHertz + 0.5f);
        for (uint32_t Step = 0u; Step < WarmupSteps; ++Step)
        {
            InjectEmission(Field, Wood, CoarseStepInterval);
            AdvanceField(Field, Solver);
        }
        Check(TotalSmoke(Field) > 0.0f, "a second and a half of warm-up puts a fire in the box before the first tile");

        const uint32_t StepsPerTile = static_cast<uint32_t>(GasMasterHertz) / Layout.Rate;   // 60 / 24 is not whole
        Check(StepsPerTile == 5u, "a 12 per second sheet advances five solver steps a tile, at the solver's own fixed step");

        for (uint32_t Index = 0u; Index < Layout.Count; ++Index)
        {
            for (uint32_t Step = 0u; Step < StepsPerTile; ++Step)
            {
                InjectEmission(Field, Wood, CoarseStepInterval);
                AdvanceField(Field, Solver);
            }
            const std::vector<uint8_t> Rendered = RenderTile(Field, Lighting, Sun, Layout.Size, 2.0f * 1.732f);
            AssignTile(Sheet.data(), Layout, Index, Rendered.data(), true);

            for (std::size_t Pixel = 3u; Pixel < Rendered.size(); Pixel += 4u)
            {
                if (Rendered[Pixel] > 8u) ++Coverage[Index];
            }
        }

        for (uint32_t Index = 0u; Index < Layout.Count; ++Index)
        {
            Check(Coverage[Index] > 0u, Index == 0u ? "every tile has smoke in it, starting with the first"
                                                    : "...and so does the next");
        }
        Check(Coverage[Layout.Count - 1u] > Coverage[0u],
              "the fire grows across the sheet rather than sitting still - the solver really advanced between tiles");

        const GasFlipbookTile First = TileAt(Layout, 0u);
        const GasFlipbookTile Fifth = TileAt(Layout, 4u);
        bool Differs = false;
        for (uint32_t Row = 0u; Row < Layout.Size && !Differs; ++Row)
        {
            const std::size_t Left  = ((static_cast<std::size_t>(First.Y) + Row) * Layout.Width + First.X) * 4u;
            const std::size_t Right = ((static_cast<std::size_t>(Fifth.Y) + Row) * Layout.Width + Fifth.X) * 4u;
            Differs = std::memcmp(&Sheet[Left], &Sheet[Right], Layout.Size * 4u) != 0;
        }
        Check(Differs, "and two tiles a third of a second apart are not the same picture");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("⑤ The same bake twice is the same bytes");
    {
        const GasSettings Preset = ConstructPresetSettings("camp_fire");
        CoarseGasField Again;
        const float Origin[3] = { 0.0f, 0.0f, 0.0f };
        ResetField(Again, Origin, 2.0f);

        GasEmission Wood;
        Wood.Position[0] = 1.0f; Wood.Position[1] = 1.0f; Wood.Position[2] = 0.25f;
        Wood.Radius          = 0.22f;
        Wood.SmokeRate       = Preset.EmitterSmoke;
        Wood.TemperatureRate = Preset.EmitterTemperature;
        Wood.Velocity[2]     = Preset.EmitterUpwardVelocity * 0.25f;

        CoarseGasSettings Solver;
        Solver.BuoyancyLift = Preset.Buoyancy * 0.4f;
        Solver.SmokeWeight  = Preset.SmokeWeight * 0.45f;

        VolumeLighting Lighting;
        Lighting.DensityExtinction = Preset.DensityExtinction;
        Lighting.SmokeAlbedo       = Preset.SmokeAlbedo;
        Lighting.FireIntensity     = Preset.FireIntensity;
        Lighting.Exposure          = Preset.Exposure;
        const float Sun[3] = { 0.4f, 0.3f, 0.86f };

        const uint32_t WarmupSteps  = static_cast<uint32_t>(Layout.Warmup * GasMasterHertz + 0.5f);
        const uint32_t StepsPerTile = static_cast<uint32_t>(GasMasterHertz) / Layout.Rate;
        for (uint32_t Step = 0u; Step < WarmupSteps; ++Step)
        {
            InjectEmission(Again, Wood, CoarseStepInterval);
            AdvanceField(Again, Solver);
        }

        std::vector<uint8_t> Second(static_cast<std::size_t>(FlipbookSheetBytes(Layout)), 0u);
        for (uint32_t Index = 0u; Index < Layout.Count; ++Index)
        {
            for (uint32_t Step = 0u; Step < StepsPerTile; ++Step)
            {
                InjectEmission(Again, Wood, CoarseStepInterval);
                AdvanceField(Again, Solver);
            }
            const std::vector<uint8_t> Rendered = RenderTile(Again, Lighting, Sun, Layout.Size, 2.0f * 1.732f);
            AssignTile(Second.data(), Layout, Index, Rendered.data(), true);
        }
        Check(Second == Sheet, "a sheet is content: two bakes of the same preset are byte for byte the same file");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The descriptor beside it is TOML, and toml++ reads back what was written");
    {
        GasFlipbookDescriptor Descriptor;
        Descriptor.Name        = "camp fire";
        Descriptor.Image       = "CampFireSheet.png";
        Descriptor.Preset      = "camp_fire";
        Descriptor.Layout      = Layout;
        Descriptor.Transparent = true;

        const std::string Spelled = SpellFlipbookDescriptor(Descriptor);
        Check(!Spelled.empty(), "a descriptor is spelled for an admitted layout");
        Check(SpellFlipbookDescriptor(GasFlipbookDescriptor{}).empty(),
              "and none at all for a refused one - a descriptor for a sheet that does not exist is worse than none");

        // toml++ is built here with exceptions on, so a malformed descriptor throws rather than answering.
        bool Parses = true;
        toml::table Read;
        try { Read = toml::parse(Spelled); } catch (const toml::parse_error&) { Parses = false; }
        Check(Parses, "🔴 it parses as TOML, which is the only format the engine reads");
        Check(Read["format"].value_or(std::string_view{}) == GasFlipbookFormat, "the format key names the format");
        Check(Read["sheet"]["tile_count"].value_or(0) == 16, "sixteen tiles");
        Check(Read["sheet"]["tile_size"].value_or(0) == 128, "of 128 pixels");
        Check(Read["sheet"]["width"].value_or(0) == 512 && Read["sheet"]["height"].value_or(0) == 512,
              "on a 512 square sheet");
        Check(Read["sheet"]["alpha"].value_or(std::string_view{}) == "straight-coverage",
              "③ saying in the file itself that the alpha is straight, because the consumer cannot tell by looking");
        Check(Read["playback"]["tiles_per_second"].value_or(0) == 12, "played at 12 tiles a second");
        Check(std::fabs(Read["playback"]["duration"].value_or(0.0) - 16.0 / 12.0) < 1e-6,
              "for a second and a third, written as a float so it reads back as one");
        Check(std::fabs(Read["playback"]["warmup"].value_or(-1.0) - 1.5) < 1e-9, "after a second and a half of warm-up");

        const toml::array* Tiles = Read["tile"].as_array();
        Check(Tiles != nullptr && Tiles->size() == 16u, "every tile's place is written out, not left to be derived");
        const toml::table* Last = Tiles->back().as_table();
        Check(Last != nullptr && (*Last)["x"].value_or(-1) == 384 && (*Last)["y"].value_or(-1) == 384,
              "and the last of them agrees with TileAt() - a consumer that derives it differently finds out here");

        const std::string Path = "Exhibits/Gallery/GasFlipbook/CampFireSheet.toml";
        std::FILE* const File = std::fopen(Path.c_str(), "wb");
        Check(File != nullptr, "the descriptor is written beside the sheet");
        std::fwrite(Spelled.data(), 1u, Spelled.size(), File);
        std::fclose(File);
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Capture");
    {
        // Written RGBA, so the coverage survives the file. A viewer that shows it on white sees the plume; one
        //    that shows it on black sees what the engine will composite.
        const int Written = PngWriteCounterpart::WritePng("Exhibits/Gallery/GasFlipbook/CampFireSheet.png",
                                                          static_cast<int>(Layout.Width),
                                                          static_cast<int>(Layout.Height), 4,
                                                          Sheet.data(), static_cast<int>(Layout.Width) * 4);
        Check(Written != 0, "the baked sheet is written out, all sixteen tiles of it");
        std::printf("    wrote Exhibits/Gallery/GasFlipbook/CampFireSheet.png  (%ux%u, 16 tiles)\n",
                    Layout.Width, Layout.Height);
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("What the rung is worth");
    {
        const uint64_t Hero  = DeviceBytesFor(GasQuality::Hero);
        const uint64_t Far   = DeviceBytesFor(GasQuality::Far);
        const uint64_t Paper = FlipbookSheetBytes(Layout);
        Check(DeviceBytesFor(GasQuality::Flipbook) == 0ull,
              "the Flipbook rung holds no lattice at all, which is what the ladder promised");
        Check(Paper < Hero, "one sheet is smaller than one Hero domain");
        std::printf("    Hero %llu B · Far %llu B per domain · this sheet %llu B, shared by every instance\n",
                    static_cast<unsigned long long>(Hero), static_cast<unsigned long long>(Far),
                    static_cast<unsigned long long>(Paper));
        Check(Paper < Far * 40ull,
              "and forty distant chimneys on one sheet cost less than forty of the cheapest live domain");
    }

    std::printf("\nPASS %u\n", Checks);
    return 0;
}
