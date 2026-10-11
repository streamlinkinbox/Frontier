//==============================================================================================================================================
//                                                        NATIVEFRACTURECARD.CPP
//==============================================================================================================================================
// 📦 Executed proof: records the per-object fracture card from real ImGui draw commands, against
//    Experimental/ProjectZeroEditor/FracturePanel.jsx and FractureEditor/FractureProjection.js.
//
//    Two things are proved here that a screenshot cannot. The diagram is a real Voronoi diagram, so its
//    nine cells must tile the perimeter exactly — the areas are summed and compared with the 36-gon's own.
//    And the card is a CSS block flow, so the checks pin the collapsed margins: BAKE sits 18 px under the
//    diagram, not the 26 that adding the two margins would give.

#include "FractureCardSurface.h"
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
using namespace Frontier::Fracture;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
}
double Area(const Polygon& Shape)
{
    double Twice = 0.0;
    for (size_t I = 0; I < Shape.size(); ++I)
    {
        const Point& A = Shape[I];
        const Point& B = Shape[(I + 1) % Shape.size()];
        Twice += A.X * B.Y - B.X * A.Y;
    }
    return std::fabs(Twice) * 0.5;
}
}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/FractureNative");
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

    constexpr int   Margin = 20;
    constexpr float CardWide = 300.0f;
    const int Width = int(CardWide) + Margin * 2;
    int Height = 700;
    Settings S;
    Owner Who;

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Light, 14.0f);
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        Draw->AddRectFilled({ 0, 0 }, { float(Width), float(Height) }, IM_COL32(9, 9, 9, 255));
        PaintCard(Draw, Light, Regular, { float(Margin), float(Margin) }, CardWide, S, Who);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        Height = int(std::ceil(CardHeight(Light, Regular, CardWide, S, Who))) + Margin * 2;
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        constexpr int Over = 3;
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
        const std::string Path = "Exhibits/Gallery/FractureNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "capture written");
    };

    //-----------------------------------------------------------------------------------------------------
    // FractureSpecification.js: Normalize clamps, rounds two keys and falls back on the enumerations.
    //-----------------------------------------------------------------------------------------------------
    {
        Settings D;
        Check(!D.Enabled && D.Run == Mode::Dynamic && !D.PieceSdf && D.SdfResolution == 64,
              "the defaults are disabled, dynamic, no per-piece SDF at 64 cubed");
        Check(D.Energy == 2500.0f && D.Seed == 42 && D.Ceiling == 48 && std::fabs(D.MinimumSize - 0.045f) < 1e-7f,
              "and 2500 J, seed 42, a 48 piece ceiling and a 45 mm floor");

        Settings Wild;
        Wild.Energy = 90000.0f; Wild.Seed = -4; Wild.Ceiling = 900; Wild.MinimumSize = 9.0f;
        Wild.X = -5000.0f; Wild.Y = 5000.0f; Wild.Z = 12.5f; Wild.SdfResolution = 48;
        const Settings Tame = Normalise(Wild);
        Check(Tame.Energy == 50000.0f, "energy clamps to 50 kJ");
        Check(Tame.Seed == 1, "the seed cannot fall under one");
        Check(Tame.Ceiling == 160, "the ceiling clamps to 160 pieces");
        Check(std::fabs(Tame.MinimumSize - 0.3f) < 1e-7f, "the minimum size clamps to 300 mm");
        Check(Tame.X == -1000.0f && Tame.Y == 1000.0f && Tame.Z == 12.5f, "and the offsets to a kilometre");
        Check(Tame.SdfResolution == 64, "an SDF resolution off the list falls back to 64 rather than clamping");
        for (int R : { 32, 64, 128 })
        {
            Settings One; One.SdfResolution = R;
            Check(Normalise(One).SdfResolution == R, "the three offered resolutions survive normalisation");
        }
        Check(Supported("cube") && Supported("sphere") && Supported("cylinder") && Supported("cone"),
              "four primitives carry a convex preview");
        Check(!Supported("torus") && !Supported("mesh"), "a torus and an imported mesh do not");
    }

    //-----------------------------------------------------------------------------------------------------
    // The status line the component assembles. Dynamic says one thing; baked adds a second clause that
    //    depends on whether the stored signature still matches.
    //-----------------------------------------------------------------------------------------------------
    {
        char Text[256];
        Settings One;
        StatusLine(One, Text, sizeof(Text));
        Check(std::strcmp(Text, "Generate fragments on demand.") == 0, "dynamic has no second clause");
        One.Run = Mode::Baked;
        StatusLine(One, Text, sizeof(Text));
        Check(std::strcmp(Text, "Reuse stored geometry for this object. Open the editor to bake or refresh geometry.") == 0,
              "a stale bake sends you to the editor");
        One.Ready = true;
        StatusLine(One, Text, sizeof(Text));
        Check(std::strcmp(Text, "Reuse stored geometry for this object. Browser bake ready.") == 0,
              "a matching bake is ready");
        One.PieceSdf = true;
        StatusLine(One, Text, sizeof(Text));
        Check(std::strcmp(Text, "Reuse stored geometry for this object. Browser geometry ready. SDF pending.") == 0,
              "and with per-piece SDF asked for, the SDF itself is still pending");

        Owner Torus { "Ring", "torus" };
        UnsupportedLine(Torus, Text, sizeof(Text));
        Check(std::strcmp(Text, "Fracture settings are available. Concave preview needs decomposition.") == 0,
              "a torus is called out as concave");
        Owner Mesh { "Rock", "mesh" };
        UnsupportedLine(Mesh, Text, sizeof(Text));
        Check(std::strcmp(Text, "Fracture settings are available. Source geometry preview is pending.") == 0,
              "anything else is simply pending");
    }

    //-----------------------------------------------------------------------------------------------------
    // The diagram is a real Voronoi diagram: nine cells that tile the 36-gon exactly.
    //-----------------------------------------------------------------------------------------------------
    {
        const Polygon Ring = GlyphPerimeter();
        Check(Ring.size() == 36, "the perimeter is a 36-gon");
        Check(std::fabs(std::hypot(Ring[0].X, Ring[0].Y) - 58.0) < 1e-9, "of radius 58");
        const std::vector<Polygon> Pieces = GlyphPieces();
        Check(Pieces.size() == 9, "nine seeds give nine cells");
        double Total = 0.0;
        for (const Polygon& One : Pieces)
        {
            Check(One.size() >= 3, "every cell is a real polygon");
            Total += Area(One);
        }
        Check(std::fabs(Total - Area(Ring)) < 1e-6, "and together they tile the perimeter exactly");
        Check(std::fabs(Area(Ring) - 10514.7445) < 1e-3, "whose area is 10514.74");
        const int Counts[9] = { 10, 9, 6, 9, 8, 8, 9, 7, 10 };
        for (int I = 0; I < 9; ++I)
            Check(int(Pieces[size_t(I)].size()) == Counts[I], "each cell has the vertex count the clipping gives it");
        // Sorted by the first vertex's Y, which is the painter's order back to front.
        for (size_t I = 1; I < Pieces.size(); ++I)
            Check(Pieces[I - 1].front().Y <= Pieces[I].front().Y, "the cells are sorted into painter's order");
        Check(std::fabs(Pieces.front().front().Y + 53.1167) < 1e-3, "the furthest cell starts at y -53.12");

        // Shading: a tone from the cell's own centroid, clamped, with every seventh facet tinted green.
        Check(Shade(400.0) == IM_COL32(210, 212, 211, 255), "the tone clamps at 210");
        Check(Shade(-50.0) == IM_COL32(35, 37, 36, 255), "and at 35");
        Check(Shade(120.0, true) == IM_COL32(108, 123, 116, 255), "the green tint shifts the channels apart");
        const Point Centre = Centroid(Pieces.front());
        Check(std::fabs(Centre.X - 2.5693) < 1e-3 && std::fabs(Centre.Y + 48.6561) < 1e-3,
              "the furthest cell's centroid is where the clipping puts it");
        const double Tone = 133.0 - Centre.X * 0.36 - Centre.Y * 0.35;
        Check(std::fabs(Tone - 149.105) < 1e-3, "its tone is 149.1");
        Check(Shade(Tone * 0.42) == IM_COL32(63, 65, 64, 255), "so its side walls are a near-black 63");
        // The lift maps the flat cells onto a sphere of the perimeter's radius.
        Check(std::fabs(SolidProject({ 0, 0 }, { 0, 0 }).Y - (61.0 - 58.0 * 0.12)) < 1e-9,
              "a cell at the centre is lifted by the full sphere height");
        Check(std::fabs(SolidProject({ 0, 0 }, { 58, 0 }).Y - 61.0) < 1e-9,
              "and one at the rim is not lifted at all");
        Check(std::fabs(SolidProject({ 0, 0 }, { 58, 0 }).X - 150.44) < 1e-9,
              "the rim cell is pushed out sideways by its own parallax");
        Check(std::fabs(SolidProject({ 0, 0 }, { 0, 58 }).Y - 70.28) < 1e-9,
              "and a near cell drops towards the viewer");
    }

    //-----------------------------------------------------------------------------------------------------
    // The card is a CSS block flow. These are the collapsed margins, not the sums.
    //-----------------------------------------------------------------------------------------------------
    {
        Owner Cube { "Cube", "cube" };
        Settings Off;
        const Plan Shut = Measure(Light, Regular, CardWide, Off, Cube);
        Check(std::fabs(Shut.Inner - 272.0f) < 1e-4f, "the content column is the card less 14 px either side");
        Check(std::fabs(Shut.HeadTall - 36.248f) < 1e-3f,
              "the header is the title and eyebrow stacked, 36.25 px, taller than its 30 px button");
        Check(std::fabs(Shut.HeadTop - 18.0f) < 1e-4f, "it starts at the card's own padding");
        Check(std::fabs(Shut.EnableTop - 74.248f) < 1e-3f, "the switch follows a 20 px margin");
        Check(std::fabs(Shut.Tall - 136.248f) < 1e-3f, "and a disabled card is 136.25 px tall");
        Check(!Shut.HasSdf && !Shut.HasField && !Shut.HasPending, "with nothing else drawn");

        Settings On; On.Enabled = true;
        const Plan Live = Measure(Light, Regular, CardWide, On, Cube);
        Check(std::fabs(Live.DiagramTop - 118.248f) < 1e-3f, "enabling it opens the diagram under the switch");
        Check(std::fabs(Live.BakeTop - Live.DiagramTop - 134.0f - 18.0f) < 1e-3f,
              "BAKE sits 18 px under the diagram: the 8 px below it collapses into the 18 above BAKE");
        Check(std::fabs(Live.ModeTop - 293.268f) < 1e-3f, "the mode pair follows BAKE's 10 px margin");
        Check(std::fabs(Live.StatusTop - 345.268f) < 1e-3f, "and the status line the track's 14");
        Check(NoteLines(Light, Live.Inner, "Generate fragments on demand.") == 1,
              "the dynamic status fits one line in the column");
        Check(std::fabs(Live.OpenTop - 376.268f) < 1e-3f, "the expand button follows at 376.27");
        Check(std::fabs(Live.Tall - 442.59f) < 1e-3f, "an enabled dynamic card is 442.59 px tall");
        Check(!Live.HasPending, "and a cube needs no apology");

        Settings Baked; Baked.Enabled = true; Baked.Run = Mode::Baked; Baked.PieceSdf = true; Baked.Ready = true;
        const Plan Deep = Measure(Light, Regular, CardWide, Baked, Cube);
        Check(Deep.HasSdf && Deep.HasField, "baked with per-piece SDF grows both extra blocks");
        char Deepest[256];
        StatusLine(Normalise(Baked), Deepest, sizeof(Deepest));
        Check(NoteLines(Light, Deep.Inner, Deepest) == 2,
              "the longest status wraps onto a second line, pushing everything under it down by 17");
        Check(std::fabs(Deep.SdfTop - 395.268f) < 1e-3f,
              "the SDF rule clears the status by 16, the larger of its own margin and the status's 14");
        Check(std::fabs(Deep.SdfSwitchTop - Deep.SdfTop - 25.0f) < 1e-3f,
              "its border and padding break the collapse, so the switch keeps its full 20 px margin");
        Check(std::fabs(Deep.SelectTop - Deep.FieldTop - 22.322f) < 1e-3f,
              "the select sits a label and 8 px under the field's top");
        Check(std::fabs(Deep.FieldTop - Deep.SdfSwitchTop - 44.0f) < 1e-3f,
              "the field's 20 px margin collapses into the switch's own 20, not onto it");
        Check(std::fabs(Deep.Tall - 635.912f) < 1e-3f, "the deepest card is 635.91 px tall");

        Settings Shallow = Baked; Shallow.PieceSdf = false;
        const Plan Mid = Measure(Light, Regular, CardWide, Shallow, Cube);
        Check(Mid.HasSdf && !Mid.HasField, "turning per-piece SDF off drops the resolution field");
        Check(std::fabs(Mid.Tall - 513.59f) < 1e-3f,
              "which shortens the card to 513.59 - and its status fits one line again");

        Owner Torus { "Ring", "torus" };
        const Plan Odd = Measure(Light, Regular, CardWide, On, Torus);
        Check(Odd.HasPending, "an unsupported primitive adds its note");
        Check(std::fabs(Odd.Tall - Live.Tall - 48.0f) < 1e-3f,
              "whose two lines and collapsed 14 px margin add 48 px over the same card on a cube");
    }

    //-----------------------------------------------------------------------------------------------------
    // Captures.
    //-----------------------------------------------------------------------------------------------------
    Who = Owner{ "Cube", "cube" };
    S = Settings{};                                             Capture("CardClosed");
    S.Enabled = true;                                           Capture("CardDynamic");
    S.Run = Mode::Baked; S.Ready = true;                        Capture("CardBaked");
    S.PieceSdf = true; S.SdfResolution = 128;                   Capture("CardBakedSdf");
    Who = Owner{ "Ring", "torus" };
    S = Settings{}; S.Enabled = true;                           Capture("CardTorus");

    ImGui::DestroyContext();
    std::printf("PASS %u checks: per-object fracture card, Voronoi solid, collapsed block flow.\n", Checks);
    return 0;
}
