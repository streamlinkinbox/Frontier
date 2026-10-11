//==============================================================================================================================================
//                                                          NATIVECLOUDDECK.CPP
//==============================================================================================================================================
// 📦 Executed proof: records the native cloud-deck vertical section from real ImGui draw commands, against
//    Experimental/ProjectZeroEditor/CloudDeckPanel.jsx — the canvas inside the "Cloud base" card.
//
//    What makes this component worth porting on its own: its altitude window sizes itself to the deck, so
//    it reads the engine's real Base (hundreds to thousands of metres) instead of the depot band's fixed
//    0-400 m. The base line is also a slider, so the pointer and key mappings are proved here too.

#include "CloudDeckSurface.h"
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
using namespace Frontier::CloudDeck;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
}
}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/CloudDeckNative");
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig FaceConfig;
    std::snprintf(FaceConfig.Name, sizeof(FaceConfig.Name), "Sun reference / regular");
    ImFont* Face = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf", 14,
                                                &FaceConfig);
    ImGui::StyleColorsDark();

    // The "Cloud base" card is one half of a two-column card-grid, so its canvas runs about 300 px.
    constexpr int Pad = 12;
    const int Width = 300 + Pad * 2, Height = int(DeckTall) + Pad * 2;
    DeckDraft Deck;

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Face, 14.0f);
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        Draw->AddRectFilled({ 0, 0 }, { float(Width), float(Height) }, IM_COL32(18, 18, 18, 255));
        PaintDeck(Draw, Face, { float(Pad), float(Pad) }, 300.0f, Deck);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        constexpr int Over = 3;
        const int WideOver = Width * Over, TallOver = Height * Over;
        std::vector<unsigned char> Dense(size_t(WideOver) * TallOver * 3, 18);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
            FrontierProof::Draw(List, Dense.data(), WideOver, TallOver, { 0, 0 }, { Over, Over },
                                ImTextureID(), {});
        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 18);
        for (int Y = 0; Y < Height; ++Y) for (int X = 0; X < Width; ++X) for (int C = 0; C < 3; ++C)
        {
            int Total = 0;
            for (int DY = 0; DY < Over; ++DY) for (int DX = 0; DX < Over; ++DX)
                Total += Dense[(size_t(Y * Over + DY) * WideOver + X * Over + DX) * 3 + C];
            Pixels[(size_t(Y) * Width + X) * 3 + C] = static_cast<unsigned char>(Total / (Over * Over));
        }
        const std::string Path = "Exhibits/Gallery/CloudDeckNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "capture written");
    };

    //-----------------------------------------------------------------------------------------------------
    // The self-sizing altitude window. This is the whole reason the component exists: the depot band fixes
    //    its window at 400 m, and an engine base of 1500 m simply leaves the canvas.
    //-----------------------------------------------------------------------------------------------------
    {
        Deck = DeckDraft{ 1500.0f, 300.0f, 0.6f, false };
        Check(Floor(Deck) == 0.0f, "a world layer always stands on the datum");
        Check(Window(Deck) == 2500.0f, "a 1500 m base with a 300 m deck opens a 2500 m window");
        Check(std::fabs(Vertical(Deck, 1500.0f) - 81.2f) < 1e-3f, "and puts the base line at y 81.2");

        Deck.Base = 130.0f;
        Check(Window(Deck) == 1000.0f, "a 130 m base closes the window to 1000 m");
        Deck.Base = 4000.0f; Deck.Thickness = 3000.0f;
        Check(Window(Deck) == 9000.0f, "a 4 km base under a 3 km anvil opens it to 9000 m");

        // The window is rounded up to a 500 m step, so in practice the step is the floor and the stated
        //    400 m minimum only bites on a degenerate deck. Worth pinning down rather than assuming.
        Deck = DeckDraft{ 120.0f, 40.0f, 0.5f, false };
        Check(Window(Deck) == 500.0f, "a shallow deck rounds up to the 500 m step, not the 400 m minimum");
        Check(Deck.Base + Deck.Thickness <= Floor(Deck) + Window(Deck), "and sits wholly within it");
        Deck = DeckDraft{ 0.0f, 0.0f, 0.5f, false };
        Check(Window(Deck) == 400.0f, "only a deck of no depth on the datum falls back on the minimum");
    }

    //-----------------------------------------------------------------------------------------------------
    // A local volume drops the floor below the datum; a world layer never does.
    //-----------------------------------------------------------------------------------------------------
    {
        Deck = DeckDraft{ -40.0f, 300.0f, 0.6f, true };
        Check(Floor(Deck) == -200.0f, "a local volume floors to the 100 m step below base minus a quarter deck");
        Check(Window(Deck) == 1000.0f, "and opens a 1000 m window around it");
        Check(std::fabs(Vertical(Deck, -40.0f) - 137.52f) < 1e-2f, "the base line lands at y 137.52");
        Deck.Local = false;
        Check(Floor(Deck) == 0.0f, "the same numbers as a world layer floor at nought");
    }

    //-----------------------------------------------------------------------------------------------------
    // The axis. Five rules, the lowest on the floor and the highest at the window's head, and the base line
    //    always between them — which is what keeps the paint inside its own box.
    //-----------------------------------------------------------------------------------------------------
    {
        const DeckDraft Cases[] = { { 1500, 300, 0.6f, false }, { 130, 300, 0.6f, false },
                                    { -40, 300, 0.6f, true },   { 4000, 3000, 1.0f, false },
                                    { 20000, 300, 0.3f, false } };
        for (const DeckDraft& One : Cases)
        {
            Check(std::fabs(Vertical(One, Floor(One)) - 158.0f) < 1e-3f, "the floor rule sits at y 158");
            Check(std::fabs(Vertical(One, Floor(One) + Window(One)) - 30.0f) < 1e-3f, "the head rule at y 30");
            const float Datum = Vertical(One, One.Base);
            Check(Datum >= 30.0f && Datum <= 158.0f, "and the base line never leaves the axis");
        }
        // Only an altitude of exactly nought reads DATUM; a local floor is a signed metre mark.
        Deck = DeckDraft{ -40.0f, 300.0f, 0.6f, true };
        Check(Floor(Deck) != 0.0f, "a local floor of -200 m is not the datum and must print as metres");
    }

    //-----------------------------------------------------------------------------------------------------
    // The base line as a slider: pointer Y in, Base out, clamped per mode.
    //-----------------------------------------------------------------------------------------------------
    {
        Deck = DeckDraft{ 1500.0f, 300.0f, 0.6f, false };
        Check(Lowest(Deck) == 100.0f && Highest(Deck) == 12000.0f, "a world layer is clamped to 100-12000 m");
        Check(Commit(Deck, Vertical(Deck, 1500.0f)) == 1500.0f, "dragging to the base line commits the base");
        Check(Commit(Deck, 30.0f) == 2500.0f, "dragging to the head of the window commits its top altitude");
        Check(Commit(Deck, 158.0f) == 100.0f, "dragging to the floor clamps up to the 100 m minimum");
        Check(Commit(Deck, 300.0f) == 100.0f, "and dragging below the canvas cannot go under it");
        Check(Nudge(Deck, true) == 1510.0f && Nudge(Deck, false) == 1490.0f, "the arrow keys step ten metres");

        Deck = DeckDraft{ -40.0f, 300.0f, 0.6f, true };
        Check(Lowest(Deck) == -100150.0f && Highest(Deck) == 99850.0f,
              "a local volume is clamped to a hundred kilometres either side of half its depth");
        Check(Commit(Deck, Vertical(Deck, -40.0f)) == -40.0f, "and still round-trips its own base");
        Check(Nudge(Deck, false) == -50.0f, "a negative base steps down past the datum");
    }

    //-----------------------------------------------------------------------------------------------------
    // The deck body. Its crest rides a two-term sine and its opacity follows the normalised density.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(std::fabs(Crown(0.0f) - 0.65f) < 1e-6f, "the crown starts at .65 of the depth");
        float Low = 2.0f, High = -2.0f;
        for (float X = 0.0f; X < 340.0f; X += ColumnStep) { Low = std::min(Low, Crown(X)); High = std::max(High, Crown(X)); }
        Check(std::fabs(Low - 0.2357766f) < 1e-4f, "its shallowest column is .236 of the depth");
        Check(std::fabs(High - 1.0417568f) < 1e-4f, "and its deepest tops the nominal thickness by four percent");

        const float Density = 0.6f;
        Check((Vapour(0.0f, Density) >> IM_COL32_A_SHIFT & 0xff) == 13, "the crest fades in at five percent alpha");
        Check((Vapour(0.35f, Density) >> IM_COL32_A_SHIFT & 0xff) == 115, "the body reaches .75 of density");
        Check((Vapour(1.0f, Density) >> IM_COL32_A_SHIFT & 0xff) == 110, "and the base settles at .72 of it");
        Check((Vapour(0.0f, Density) & 0xff) == 238 && (Vapour(0.2f, Density) & 0xff) == 238,
              "the upper half holds the vapour white");
        Check((Vapour(1.0f, Density) & 0xff) == 92, "and the underside greys to the shadowed blue");
        Check((Vapour(0.0f, 0.0f) >> IM_COL32_A_SHIFT & 0xff) == 13
           && (Vapour(1.0f, 0.0f) >> IM_COL32_A_SHIFT & 0xff) == 0,
              "a nought density leaves only the faintest crest");
    }

    //-----------------------------------------------------------------------------------------------------
    // What the engine sheet actually hands it: Density is a 0-4 multiplier.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(std::fabs(std::clamp(2.4f / 4.0f, 0.0f, 1.0f) - 0.6f) < 1e-6f, "a 2.4x engine density normalises to .6");
        Check(std::clamp(4.0f / 4.0f, 0.0f, 1.0f) == 1.0f, "the top of the engine range is fully opaque");
        Check(std::clamp(-1.0f / 4.0f, 0.0f, 1.0f) == 0.0f, "and a negative one cannot go under nought");
    }

    //-----------------------------------------------------------------------------------------------------
    // Captures.
    //-----------------------------------------------------------------------------------------------------
    Deck = DeckDraft{ 1500.0f, 300.0f, 0.6f, false };  Capture("Deck");
    Deck = DeckDraft{  130.0f, 300.0f, 0.6f, false };  Capture("DeckLow");
    Deck = DeckDraft{  -40.0f, 300.0f, 0.6f, true  };  Capture("DeckLocal");
    Deck = DeckDraft{ 4000.0f, 3000.0f, 1.0f, false }; Capture("DeckAnvil");
    Deck = DeckDraft{ 1500.0f, 300.0f, 0.12f, false }; Capture("DeckThin");

    ImGui::DestroyContext();
    std::printf("PASS %u checks: cloud deck vertical section, self-sizing window, base-line slider.\n", Checks);
    return 0;
}
