//==============================================================================================================================================
//                                                     NATIVEOUTLINERMETADATA.CPP
//==============================================================================================================================================
// 📦 Executed proof: the small live figure printed under every outliner row's name.
//
//    Editor.jsx OutlinerMetadata() answers for nineteen panels, and the shipped bundle carries every one
//    of the strings below verbatim. The native header is held to them character for character — the
//    separator is U+00B7, the multiplication sign U+00D7, the degree sign U+00B0 — because the figure is
//    the only place the outliner shows a live reading, and a drifted unit reads as a wrong scene.
//
//    The number formatting is proved alongside, since every branch reaches through it: thousands
//    grouping, trailing-zero trim, half-away-from-zero rounding, and the em dash a non-finite gives.
//
//    The capture draws one row per panel the way OutlinerPanel.cpp draws it — the name in 13 px, the
//    figure in 10 px beneath — so a reviewer reads the same two lines the browser shows.

#include "OutlinerMetadata.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

using namespace Frontier;

namespace
{
unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
}

// The figure a panel prints for a given reading, as a comparable string.
std::string Figure(RowPanel Panel, const RowReading& Reading = RowReading{})
{
    char Out[96];
    OutlinerMetadata(Panel, Reading, Out, sizeof(Out));
    return std::string(Out);
}

std::string Grouped(double Reading, int Digits)
{
    char Out[64];
    CompactNumber(Reading, Digits, Out, sizeof(Out));
    return std::string(Out);
}

void Same(const std::string& Found, const char* Wanted, const char* Claim)
{
    ++Checks;
    if (Found != Wanted)
    {
        std::fprintf(stderr, "  wanted [%s]\n  found  [%s]\n", Wanted, Found.c_str());
        throw std::runtime_error(Claim);
    }
}
}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/OutlinerMetadataNative");

    //-----------------------------------------------------------------------------------------------------
    // CompactNumber: Number.prototype.toLocaleString("en-US", { maximumFractionDigits: Digits }).
    //-----------------------------------------------------------------------------------------------------
    Same(Grouped(1250.0, 0), "1,250", "thousands are grouped");
    Same(Grouped(1250000.0, 0), "1,250,000", "and so are millions");
    Same(Grouped(-12.45, 1), "-12.5", "a half rounds away from zero, not to even");
    Same(Grouped(0.5, 0), "1", "and so does a bare half");
    Same(Grouped(2.50, 1), "2.5", "a trailing zero is dropped");
    Same(Grouped(3.0, 2), "3", "and so is the orphaned point");
    Same(Grouped(0.0, 1), "0", "zero is plain");
    Same(Grouped(-0.04, 1), "0", "a magnitude that rounds away loses its sign, as Intl does");
    Same(Grouped(1.0 / 0.0, 1), "\xe2\x80\x94", "a non-finite reading answers with an em dash");

    {
        const float Place[3] = { -12.4f, 0.0f, 3.0f };
        char Out[64];
        CompactPosition(Place, Out, sizeof(Out));
        Same(std::string(Out), "-12.4,0,3", "a position joins three figures with bare commas");
    }

    //-----------------------------------------------------------------------------------------------------
    // PanelOfRow: the browser's Row.Panel string, recovered from the glyph and the category.
    //-----------------------------------------------------------------------------------------------------
    Check(PanelOfRow(EditorGlyph::Auto, EditorInstanceCategory::Folder) == RowPanel::Group,
          "a folder with no glyph of its own is a group");
    Check(PanelOfRow(EditorGlyph::Auto, EditorInstanceCategory::Geometry) == RowPanel::Geometry,
          "and an unglyphed row is geometry");
    Check(PanelOfRow(EditorGlyph::Bulb, EditorInstanceCategory::Light) == RowPanel::Light,
          "a bulb is a light");
    Check(PanelOfRow(EditorGlyph::Camera, EditorInstanceCategory::Camera, true) == RowPanel::EditorCamera,
          "the permanent camera answers before the glyph is read");
    Check(PanelOfRow(EditorGlyph::VolumeFog, EditorInstanceCategory::Geometry) == RowPanel::HeightFog,
          "the fog glyphs separate height fog from aerial and local");
    Check(PanelOfRow(EditorGlyph::AerialFog, EditorInstanceCategory::Geometry) == RowPanel::AerialFog,
          "aerial fog keeps its own panel");
    Check(PanelOfRow(EditorGlyph::Fog, EditorInstanceCategory::Geometry) == RowPanel::LocalFog,
          "and so does local fog");

    //-----------------------------------------------------------------------------------------------------
    // FolderInventory.mjs CollectionTypes: the plural the collection inspector lists a panel under.
    //-----------------------------------------------------------------------------------------------------
    Same(CollectionTypeName(RowPanel::Group), "Folders", "a group is listed as Folders");
    Same(CollectionTypeName(RowPanel::Light), "Lights", "a light as Lights");
    Same(CollectionTypeName(RowPanel::LocalCloud), "Local clouds", "a local cloud keeps its lower case");
    Same(CollectionTypeName(RowPanel::Flare), "Lens flares", "a flare is listed as Lens flares");
    Same(CollectionTypeName(RowPanel::Post), "Post processing", "and post as Post processing");

    //-----------------------------------------------------------------------------------------------------
    // The nineteen branches, each at the browser's own defaults unless the reading says otherwise.
    //-----------------------------------------------------------------------------------------------------
    Same(Figure(RowPanel::EditorCamera), "Editor \xc2\xb7 permanent",
         "the editor camera answers before anything else");

    {
        RowReading Lone; Lone.Enclosed = 1u;
        RowReading Many; Many.Enclosed = 12u;
        RowReading None;
        Same(Figure(RowPanel::Group, Lone), "1 item", "one child is an item");
        Same(Figure(RowPanel::Group, Many), "12 items", "more than one are items");
        Same(Figure(RowPanel::Group, None), "0 items", "and none are items too");
    }

    {
        RowReading Placed; Placed.Position[0] = -12.4f; Placed.Position[2] = 3.0f;
        Same(Figure(RowPanel::Geometry, Placed), "Position -12.4,0,3 m",
             "geometry prints where it stands");
    }

    Same(Figure(RowPanel::Camera), "35 mm \xc2\xb7 f/2.8", "a camera prints focal length and aperture");
    Same(Figure(RowPanel::Post), "EV +0", "post prints a signed exposure compensation");
    {
        RowReading Dark; Dark.Exposure = -1.5f;
        Same(Figure(RowPanel::Post, Dark), "EV -1.5", "and carries its own minus when below zero");
    }

    Same(Figure(RowPanel::Atmosphere), "Mie 1\xc3\x97 \xc2\xb7 ozone 1", "atmosphere prints Mie and ozone");
    Same(Figure(RowPanel::Sun), "1\xc3\x97 \xc2\xb7 0.53\xc2\xb0", "the sun prints intensity and disc");
    Same(Figure(RowPanel::Flare), "1\xc3\x97 \xc2\xb7 6 ghosts", "a flare counts its ghosts");
    Same(Figure(RowPanel::Moon), "Lunar phase 180\xc2\xb0 \xc2\xb7 50%",
         "a moon prints its phase twice over");
    Same(Figure(RowPanel::Stars), "6 mag \xc2\xb7 1\xc3\x97", "stars print the limiting magnitude");
    Same(Figure(RowPanel::Wind), "7 m/s \xc2\xb7 45\xc2\xb0", "wind prints speed and bearing");
    Same(Figure(RowPanel::HeightFog), "1\xc3\x97 \xc2\xb7 120 m", "height fog prints its falloff height");
    Same(Figure(RowPanel::AerialFog), "1\xc3\x97 \xc2\xb7 starts 500 m", "aerial fog prints where it begins");
    Same(Figure(RowPanel::LocalFog), "1\xc3\x97 \xc2\xb7 50%", "local fog prints its coverage");
    Same(Figure(RowPanel::Clouds), "50% \xc2\xb7 1\xc3\x97", "clouds lead with coverage");
    Same(Figure(RowPanel::LocalCloud), "50% \xc2\xb7 1\xc3\x97", "and a local cloud reads the same way");
    Same(Figure(RowPanel::Precipitation), "Rain \xc2\xb7 1 mm/h", "precipitation names itself first");
    {
        RowReading Snow; Snow.Precipitate = 3u; Snow.Intensity = 2.5f;
        Same(Figure(RowPanel::Precipitation, Snow), "Snow \xc2\xb7 2.5 mm/h",
             "and it answers for each of the five kinds");
    }
    Same(Figure(RowPanel::Rainbow), "1\xc3\x97 \xc2\xb7 100 m", "a rainbow prints its minimum path");

    //-----------------------------------------------------------------------------------------------------
    // The light branch, which reads the reference luminaire the row points at.
    //-----------------------------------------------------------------------------------------------------
    {
        RowReading Spot;
        Spot.Referenced = true;
        Spot.Shape = LuminaireShape::Spot;
        Spot.Output = 1250.0f;
        Spot.Position[0] = -12.4f;
        Spot.Position[2] = 3.0f;
        Same(Figure(RowPanel::Light, Spot), "1,250 cd \xc2\xb7 Spot \xc2\xb7 -12.4,0,3",
             "a spot reads in candela");

        RowReading Strip = Spot;
        Strip.Shape = LuminaireShape::Strip;
        Strip.Output = 2400.0f;
        Same(Figure(RowPanel::Light, Strip), "2,400 lm \xc2\xb7 Strip \xc2\xb7 -12.4,0,3",
             "every shape that is not point or spot reads in lumens");

        RowReading Bare;
        Same(Figure(RowPanel::Light, Bare), "1 lx \xc2\xb7 Area \xc2\xb7 0,0,0",
             "a light with no reference luminaire falls back to plain illuminance");
    }

    Check(std::strcmp(LuminaireName(LuminaireShape::Ies), "IES") == 0, "IES keeps its capitals");
    Check(std::strcmp(LuminaireName(LuminaireShape::Led), "LED") == 0, "and so does LED");
    Check(std::strcmp(PrecipitateName(4u), "Sleet") == 0, "the fifth precipitate is sleet");

    //-----------------------------------------------------------------------------------------------------
    // Capture: one row per panel, drawn the way OutlinerPanel.cpp stacks the name over the figure.
    //-----------------------------------------------------------------------------------------------------
    struct Line { RowPanel Panel; const char* Name; RowReading Reading; };
    std::vector<Line> Sheet;
    {
        RowReading Placed; Placed.Position[0] = -12.4f; Placed.Position[2] = 3.0f;
        RowReading Group;  Group.Enclosed = 12u;
        RowReading Spot;   Spot.Referenced = true; Spot.Shape = LuminaireShape::Spot;
        Spot.Output = 1250.0f; Spot.Position[0] = -12.4f; Spot.Position[2] = 3.0f;
        Sheet = {
            { RowPanel::EditorCamera,  "Editor camera",   RowReading{} },
            { RowPanel::Group,         "Showcase",        Group },
            { RowPanel::Geometry,      "Cube",            Placed },
            { RowPanel::Camera,        "Cine camera",     RowReading{} },
            { RowPanel::Light,         "Key spot",        Spot },
            { RowPanel::Post,          "Post process",    RowReading{} },
            { RowPanel::Atmosphere,    "Atmosphere",      RowReading{} },
            { RowPanel::Sun,           "Sun",             RowReading{} },
            { RowPanel::Flare,         "Lens flare",      RowReading{} },
            { RowPanel::Moon,          "Moon A",          RowReading{} },
            { RowPanel::Stars,         "Stars",           RowReading{} },
            { RowPanel::Wind,          "Wind",            RowReading{} },
            { RowPanel::HeightFog,     "Height fog",      RowReading{} },
            { RowPanel::AerialFog,     "Atmospheric fog", RowReading{} },
            { RowPanel::LocalFog,      "Local fog",       RowReading{} },
            { RowPanel::Clouds,        "Volume clouds",   RowReading{} },
            { RowPanel::LocalCloud,    "Local cloud",     RowReading{} },
            { RowPanel::Precipitation, "Precipitation",   RowReading{} },
            { RowPanel::Rainbow,       "Rainbow",         RowReading{} },
        };
    }

    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig UiConfig;
    std::snprintf(UiConfig.Name, sizeof(UiConfig.Name), "Sun reference / regular");
    ImFont* Ui = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf", 14,
                                              &UiConfig);
    ImGui::StyleColorsDark();

    constexpr int Width = 420;
    const int Height = 28 + int(Sheet.size()) * 40;

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        float Y = 16;
        for (const Line& Row : Sheet)
        {
            char Meta[96];
            OutlinerMetadata(Row.Panel, Row.Reading, Meta, sizeof(Meta));
            Draw->AddRectFilled({ 10, Y - 4 }, { float(Width) - 10, Y + 32 }, IM_COL32(26, 28, 27, 255), 8);
            Draw->AddText(Ui, 13, { 24, Y }, IM_COL32(222, 226, 224, 255), Row.Name);
            Draw->AddText(Ui, 10, { 24, Y + 18 }, IM_COL32(128, 134, 131, 255), Meta);
            Y += 40;
        }
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    for (int Frame = 0; Frame < 3; ++Frame) Tick();
    std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 18);
    for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
        FrontierProof::Draw(List, Pixels.data(), Width, Height, { 0, 0 }, { 1, 1 }, ImTextureID(), {});
    Check(stbi_write_png("Exhibits/Gallery/OutlinerMetadataNative/Row-Metadata.png", Width, Height, 3,
                         Pixels.data(), Width * 3) != 0,
          "the metadata sheet is written");

    std::printf("PASS %u checks: outliner row metadata, panel recovery, compact number formatting.\n",
                Checks);
    return 0;
}
