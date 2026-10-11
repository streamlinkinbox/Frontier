//==============================================================================================================================================
//                                                        NATIVEFRACTUREEDITOR.CPP
//==============================================================================================================================================
// 📦 Executed proof: the whole fracture editor page, recorded from real ImGui draw commands against
//    Experimental/FractureEditor/index.html — FracturePanel.html, FracturePanel.css, FracturePanel.js,
//    the material table in SourceDepot/Fragmentation/src/fracture/materials.ts and the quality
//    illustration in FractureProjection.js.
//
//    Four things are proved that a screenshot cannot. The three-column grid resolves to the stylesheet's
//    own widths at every breakpoint. The material table carries the real Gc and density figures, grouped
//    the way toLocaleString groups them. Every derived string — the execution eyebrow, the viewport
//    caption, the mode description, the bake status, detail and button labels — is assembled exactly as
//    Refresh() assembles it, for each of the states that change it. And the quality illustration is a
//    genuine Voronoi diagram whose cells tile their 228 x 72 frame to within 1e-6 of its area.

#include "FractureEditorSurface.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Frontier;
using namespace Frontier::FractureEditor;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
}
bool Near(float A, float B, float Slack = 1e-3f) { return std::fabs(A - B) <= Slack; }
double Area(const Outline& Shape)
{
    double Twice = 0.0;
    for (size_t I = 0; I < Shape.size(); ++I)
    {
        const Spot2& A = Shape[I];
        const Spot2& B = Shape[(I + 1) % Shape.size()];
        Twice += A.X * B.Y - B.X * A.Y;
    }
    return std::fabs(Twice) * 0.5;
}
}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/FractureEditorNative");
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig LightConfig;
    std::snprintf(LightConfig.Name, sizeof(LightConfig.Name), "Sun reference / light");
    ImFont* Light = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Light.ttf", 14,
                                                 &LightConfig);
    ImFontConfig RegularConfig;
    std::snprintf(RegularConfig.Name, sizeof(RegularConfig.Name), "Sun reference / regular");
    ImFont* Regular = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf", 14,
                                                   &RegularConfig);
    ImGui::StyleColorsDark();

    //-----------------------------------------------------------------------------------------------------
    // The five bands and the three columns. .fracture-app is a flex column; .workspace is the only
    //    flexible band, and its grid-template-columns changes at 1600, 1180 and 900.
    //-----------------------------------------------------------------------------------------------------
    {
        const Layout Wide = Measure(1440.0f, 900.0f);
        Check(Near(Wide.BarTop, 39.0f), "the title bar is 39 px");
        Check(Near(Wide.WorkTop, 79.0f), "and the workspace bar another 40");
        Check(Near(Wide.StatusTop, 874.0f), "the status bar is 26 px off the bottom");
        Check(Near(Wide.WorkTall, 795.0f), "so the workspace absorbs 795");
        Check(Near(Wide.LeftW, 240.0f) && Near(Wide.RightW, 330.0f), "240 / 1fr / 330 by default");
        Check(Near(Wide.CentreX, 241.0f), "the 1 px grid gap sits between them");
        Check(Near(Wide.CentreW, 868.0f), "and the centre takes what is left");

        const Layout Big = Measure(1680.0f, 1050.0f);
        Check(Near(Big.LeftW, 270.0f) && Near(Big.RightW, 350.0f),
              "at 1600 and up the asides widen to 270 and 350");

        const Layout Narrow = Measure(1100.0f, 800.0f);
        Check(Near(Narrow.LeftW, 190.0f) && Near(Narrow.RightW, 300.0f),
              "at 1180 and under they narrow to 190 and 300");
        Check(Near(Narrow.TargetPad, 12.0f), "and .target-content drops to 12 px of padding");

        const Layout Small = Measure(860.0f, 700.0f);
        Check(!Small.ShowTarget, "under 900 the object pane is display:none");
        Check(Near(Small.CentreX, 0.0f) && Near(Small.RightW, 310.0f),
              "leaving a two-column grid of 1fr and 310");
    }

    //-----------------------------------------------------------------------------------------------------
    // MATERIALS and MaterialNames, in iteration order, with toLocaleString grouping.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(int(Material::Count) == 6, "six materials are offered");
        const char* Keys[]  = { "concrete", "rock", "wood", "glass", "tempered", "plastic" };
        const char* Names[] = { "Concrete", "Stone", "Wood", "Glass", "Tempered glass", "ABS plastic" };
        const int Gc[]      = { 140, 95, 320, 7, 7, 460 };
        const int Density[] = { 2350, 2700, 520, 2500, 2500, 1050 };
        for (int I = 0; I < 6; ++I)
        {
            Check(std::strcmp(Materials[I].Key, Keys[I]) == 0, "the keys are MaterialNames' own order");
            Check(std::strcmp(Materials[I].Name, Names[I]) == 0, "with the labels it gives them");
            Check(Materials[I].Toughness == Gc[I], "the crack resistance is MATERIALS.Gc");
            Check(Materials[I].Density == Density[I], "and the density is MATERIALS.density");
        }
        Check(Materials[0].Swatch == IM_COL32(0x9d, 0x9d, 0x97, 255), "concrete's swatch is 0x9d9d97");
        Check(Materials[5].Swatch == IM_COL32(0xd8, 0x55, 0x2f, 255), "and ABS plastic's is 0xd8552f");

        char Text[24];
        Grouped(2350, Text, sizeof(Text));
        Check(std::strcmp(Text, "2,350") == 0, "toLocaleString groups thousands");
        Grouped(7, Text, sizeof(Text));
        Check(std::strcmp(Text, "7") == 0, "and leaves a single digit alone");
        Grouped(999999, Text, sizeof(Text));
        Check(std::strcmp(Text, "999,999") == 0, "six digits take one separator");
        Grouped(1234567, Text, sizeof(Text));
        Check(std::strcmp(Text, "1,234,567") == 0, "seven take two");
    }

    //-----------------------------------------------------------------------------------------------------
    // Refresh(): every derived string, in every state that changes it.
    //-----------------------------------------------------------------------------------------------------
    {
        Subject Specimen;
        char Text[192];

        ExecutionLabel(Specimen, Text, sizeof(Text));
        Check(std::strcmp(Text, "DYNAMIC / GEOMETRY") == 0, "the eyebrow names the execution mode");
        Specimen.Settings.Run = Mode::Baked;
        ExecutionLabel(Specimen, Text, sizeof(Text));
        Check(std::strcmp(Text, "BAKED / GEOMETRY") == 0, "uppercased, as Refresh uppercases it");
        Specimen.Settings.Enabled = false;
        ExecutionLabel(Specimen, Text, sizeof(Text));
        Check(std::strcmp(Text, "DISABLED / GEOMETRY") == 0, "and says DISABLED when the card's switch is off");

        ViewportCaption(Specimen, Text, sizeof(Text));
        Check(std::strcmp(Text, "Fracture disabled in the object inspector") == 0,
              "the caption sends you back to the inspector");
        Specimen.Settings.Enabled = true;
        ViewportCaption(Specimen, Text, sizeof(Text));
        Check(std::strcmp(Text, "Source geometry \xc2\xb7 Concrete response") == 0,
              "otherwise it names the material's response");
        Specimen.ShowingFragments = true; Specimen.Pieces = 37;
        ViewportCaption(Specimen, Text, sizeof(Text));
        Check(std::strcmp(Text, "37 closed fragments \xc2\xb7 Concrete") == 0,
              "and counts the fragments once they exist");

        Specimen.Settings.Run = Mode::Dynamic;
        Check(std::strcmp(ModeDescription(Specimen),
              "Generate geometry on demand from this object\xe2\x80\x99s recipe.") == 0,
              "dynamic generates on demand, with the bundle's own right single quote");
        Specimen.Settings.Run = Mode::Baked;
        Check(std::strcmp(ModeDescription(Specimen),
              "Use the stored fragment geometry\xe2\x80\x94no fracture generation during replay.") == 0,
              "baked replays, with an em dash and no spaces round it");

        Check(std::strcmp(FractureAction(Specimen), "Show baked fracture") == 0,
              "the primary button renames itself in baked mode");
        Specimen.Settings.Run = Mode::Dynamic;
        Check(std::strcmp(FractureAction(Specimen), "Fracture object") == 0, "and back again in dynamic");

        Check(std::strcmp(StoredStatus(Specimen), "Not baked") == 0, "no stored geometry reads Not baked");
        Check(StoredDot(Specimen) == DotIdle, "with a grey dot");
        Check(std::strcmp(StoredAction(Specimen), "Bake object") == 0, "and the button offers a first bake");
        StoredDetail(Specimen, Text, sizeof(Text));
        Check(std::strcmp(Text, "No stored fragment geometry") == 0, "the detail says so plainly");

        Specimen.Stored = Freshness::Stale; Specimen.StoredParts = 41; Specimen.StoredKiB = 128.75f;
        Check(std::strcmp(StoredStatus(Specimen), "Bake is stale") == 0, "a signature mismatch is stale");
        Check(StoredDot(Specimen) == DotStale, "with an amber dot");
        Check(std::strcmp(StoredAction(Specimen), "Rebake object") == 0, "and the button offers a rebake");
        StoredDetail(Specimen, Text, sizeof(Text));
        Check(std::strcmp(Text, "41 closed pieces \xc2\xb7 128.8 KiB \xc2\xb7 geometry / recipe changed") == 0,
              "the detail rounds the size to one decimal and says why it is stale");

        Specimen.Stored = Freshness::Ready;
        Check(std::strcmp(StoredStatus(Specimen), "Baked \xc2\xb7 ready") == 0, "a matching signature is ready");
        Check(StoredDot(Specimen) == DotReady, "with a green dot");
        StoredDetail(Specimen, Text, sizeof(Text));
        Check(std::strcmp(Text, "41 closed pieces \xc2\xb7 128.8 KiB \xc2\xb7 per object") == 0,
              "and the detail says per object");
        Specimen.Settings.PieceSdf = true;
        Check(std::strcmp(StoredStatus(Specimen), "Geometry ready \xc2\xb7 SDF pending") == 0,
              "asking for a per-piece SDF leaves the SDF itself pending");
        Check(std::strcmp(StoredAction(Specimen), "Bake geometry") == 0, "and renames the bake button again");

        Subject Cone;
        Cone.Shape = Primitive::Cone;
        SourceLabel(Cone, Text, sizeof(Text));
        Check(std::strcmp(Text, "Analytical cone") == 0, "the source names the analytical primitive");
        Cone.Scale[0] = 2.5f; Cone.Scale[1] = 0.25f; Cone.Scale[2] = 1.0f;
        ScaleLabel(Cone, Text, sizeof(Text));
        Check(std::strcmp(Text, "2.50 \xc3\x97 0.25 \xc3\x97 1.00") == 0,
              "and the scale is three two-decimal figures joined by multiplication signs");
    }

    //-----------------------------------------------------------------------------------------------------
    // QualityGlyph(Ceiling, MinimumSize): the site count, and a genuine tiling.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(QualitySiteCount(48, 0.045f) == 16, "the default recipe lays down sixteen sites");
        Check(QualitySiteCount(2, 0.3f) == 6, "the floor is six");
        Check(QualitySiteCount(160, 0.002f) == 26, "160 pieces at the finest span gives twenty-six");
        Check(QualitySiteCount(100000, 0.002f) == 28, "and the ceiling is twenty-eight");

        const std::vector<Outline> Cells = QualityCells(48, 0.045f);
        Check(Cells.size() == 16, "sixteen sites give sixteen cells");
        double Total = 0.0;
        for (const Outline& One : Cells)
        {
            Check(One.size() >= 3, "every cell is a real polygon");
            Total += Area(One);
        }
        Check(std::fabs(Total - 228.0 * 72.0) < 1e-6, "and together they tile the 228 x 72 frame exactly");

        // The lattice is the golden ratio against sqrt(2) - 1, so no two sites share a column.
        const std::vector<Spot2> Sites = QualitySites(16);
        Check(std::fabs(Sites[0].X - (0.1 * 226.0 - 113.0)) < 1e-9, "the first site is at 10 % of the span");
        for (size_t I = 1; I < Sites.size(); ++I)
            Check(std::fabs(Sites[I].X - Sites[I - 1].X) > 1e-6, "and no two sites share an abscissa");

        Check(QualityShade(500.0, false) == IM_COL32(210, 212, 211, 255), "the shade clamps at 210");
        Check(QualityShade(0.0, false) == IM_COL32(35, 37, 36, 255), "and at 35");
        Check(QualityShade(100.0, true) == IM_COL32(88, 103, 96, 255), "every seventh facet is tinted green");
    }

    //-----------------------------------------------------------------------------------------------------
    // The viewport stand-in borrows CheckerViewport's marker, and the two rosters are in different orders.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(MarkerPrimitive(Primitive::Cube) == BaseMesh::Solid::Cube, "cube to cube");
        Check(MarkerPrimitive(Primitive::Sphere) == BaseMesh::Solid::Sphere, "sphere to sphere");
        Check(MarkerPrimitive(Primitive::Cylinder) == BaseMesh::Solid::Cylinder, "cylinder to cylinder");
        Check(MarkerPrimitive(Primitive::Cone) == BaseMesh::Solid::Cone,
              "cone to cone, which a cast would not give: the editor lists it fourth and the roster fifth");
        Check(MarkerPrimitive(Primitive::Torus) == BaseMesh::Solid::Torus, "and torus to torus for the same reason");
        Check(int(Primitive::Cone) != int(BaseMesh::Solid::Cone), "the two orders really do disagree");
    }

    //-----------------------------------------------------------------------------------------------------
    // Captures.
    //-----------------------------------------------------------------------------------------------------
    int Width = 1440, Height = 900;
    Subject Subject;
    Subject.OwnerName = "Cube";
    Subject.OwnerId = "cube";
    Subject.Shape = Primitive::Cube;
    Subject.Dimensions = "1.000 \xc3\x97 1.000 \xc3\x97 1.000 m";
    Subject.SourceVolume = "1.0000 m\xc2\xb3";

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Fracture", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Light, 14.0f);
        Paint(ImGui::GetWindowDrawList(), Light, Regular, Measure(float(Width), float(Height)), Subject);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        // The page is drawn at 1440 x 900; a x2 supersample of that is 2.6 Specimen pixels per channel, which is
        //    as much as this harness wants to hold.
        constexpr int Over = 2;
        const int WideOver = Width * Over, TallOver = Height * Over;
        std::vector<unsigned char> Dense(size_t(WideOver) * TallOver * 3, 9);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
            FrontierProof::Draw(List, Dense.data(), WideOver, TallOver, { 0, 0 }, { Over, Over },
                                ImTextureID(), {});
        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 9);
        for (int Y = 0; Y < Height; ++Y) for (int X = 0; X < Width; ++X) for (int C = 0; C < 3; ++C)
        {
            int Total = 0;
            for (int DY = 0; DY < Over; ++DY) for (int DX = 0; DX < Over; ++DX)
                Total += Dense[(size_t(Y * Over + DY) * WideOver + X * Over + DX) * 3 + C];
            Pixels[(size_t(Y) * Width + X) * 3 + C] = static_cast<unsigned char>(Total / (Over * Over));
        }
        const std::string Path = "Exhibits/Gallery/FractureEditorNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0,
              "capture written");
    };

    Capture("Dynamic");

    Subject.Settings.Run = Mode::Baked;
    Subject.Settings.PieceSdf = true;
    Subject.Settings.SdfResolution = 128;
    Subject.Stored = Freshness::Ready;
    Subject.StoredParts = 41;
    Subject.StoredKiB = 128.75f;
    Subject.Status = "Baked 41 closed pieces.";
    Capture("BakedWithSdf");

    Subject.Settings.Run = Mode::Dynamic;
    Subject.Settings.PieceSdf = false;
    Subject.Stored = Freshness::None;
    Subject.Settings.Stock = Material::Tempered;
    Subject.Settings.Energy = 18000.0f;
    Subject.Settings.Ceiling = 120;
    Subject.Settings.MinimumSize = 0.008f;
    Subject.Settings.Seed = 7331;
    Subject.Shape = Primitive::Sphere;
    Subject.OwnerName = "Sphere";
    Subject.OwnerId = "sphere";
    Subject.ShowingFragments = true;
    Subject.Pieces = 94;
    Subject.Occupied = 99.998f;
    Subject.Quality = "0.214";
    Subject.Triangles = "12,884";
    Subject.Refused = "31";
    Subject.VolumeError = "1.84e-5";
    Subject.Generation = "42.6 ms";
    Subject.Status = "94 fragments in 42.6 ms.";
    Capture("TemperedFragments");

    Subject.Settings.Enabled = false;
    Subject.ShowingFragments = false;
    Subject.Status = "Fracture is disabled for this object.";
    Capture("Disabled");

    // 1180 and under: the asides narrow and the object pane's padding drops.
    Width = 1120; Height = 760;
    Subject.Settings.Enabled = true;
    Subject.Settings.Stock = Material::Wood;
    Subject.Shape = Primitive::Cylinder;
    Subject.OwnerName = "Cylinder";
    Subject.OwnerId = "cylinder";
    Subject.Status = "Ready";
    Capture("NarrowBreakpoint");

    // One tall frame so the whole inspector column is on the page at once rather than under its scroll.
    Width = 1280; Height = 2080;
    Subject.Settings.Stock = Material::Glass;
    Subject.Settings.Run = Mode::Baked;
    Subject.Settings.PieceSdf = true;
    Subject.Stored = Freshness::Stale;
    Subject.StoredParts = 41;
    Subject.StoredKiB = 128.75f;
    Subject.Shape = Primitive::Torus;
    Subject.OwnerName = "Torus";
    Subject.OwnerId = "torus";
    Subject.Status = "Bake is stale.";
    Capture("InspectorColumn");

    std::printf("PASS %u checks: fracture editor page, breakpoints, material table, derived text, "
                "quality tiling.\n", Checks);
    return 0;
}
