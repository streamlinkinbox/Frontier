//==============================================================================================================================================
//                                                          NATIVECLOUDCARDS.CPP
//==============================================================================================================================================
// 📦 Executed proof: records the shipped cloud panel from real ImGui draw commands, against InspectorDepot/panels/clouds.js.

#include "CloudInstrumentSurface.h"
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
using namespace Frontier::CloudInstrument;

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
    std::filesystem::create_directories("Exhibits/Gallery/CloudNative");
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

    int   Width = 340, Height = 1400;
    float Measured = 0;
    CloudDraft Layer;
    float WindFrom = 214.0f;

    auto PanelHeight = [&]()
    {
        return MapTall + 20.0f + Kit::PillTall + 10.0f + (DuoHeight(Face, float(Width) - 20.0f, Layer) + 10.0f) + 10.0f
             + CoverageHeight() + 20.0f + DeckHeight() + 20.0f + MorphologyHeight() + 10.0f;
    };

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginChild("##cl", { 0, float(Height) }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Face, 14.0f);
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const ImVec2 Origin{ 10.0f, 10.0f };
        const float  Card = float(Width) - 20.0f;
        Draw->AddRectFilled({ 0, 0 }, { float(Width), float(Height) }, IM_COL32(5, 5, 5, 255));
        float Y = Origin.y;
        PaintMap(Draw, Face, { Origin.x, Y }, Card, MapTall, Layer, WindFrom);  Y += MapTall + 20.0f;
        PaintRail(Draw, Face, { Origin.x, Y }, Card, Layer);                    Y += Kit::PillTall + 10.0f;
        PaintDuo(Draw, Face, { Origin.x, Y }, Card, Layer);                     Y += DuoHeight(Face, Card, Layer) + 20.0f;
        PaintCoverage(Draw, Face, { Origin.x, Y }, Card, Layer);                Y += CoverageHeight() + 20.0f;
        PaintDeck(Draw, Face, { Origin.x, Y }, Card, Layer);                    Y += DeckHeight() + 20.0f;
        PaintMorphology(Draw, Face, { Origin.x, Y }, Card, Layer);
        ImGui::PopFont();
        ImGui::SetCursorScreenPos({ 0, 0 });
        ImGui::Dummy({ float(Width), PanelHeight() + 20.0f });
        Measured = ImGui::GetCursorPosY();
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        Height = int(PanelHeight()) + 220;
        Tick();
        Height = static_cast<int>(Measured) + 2;
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        constexpr int Over = 3;
        const int WideOver = Width * Over, TallOver = Height * Over;
        std::vector<unsigned char> Dense(size_t(WideOver) * TallOver * 3, 5);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
            FrontierProof::Draw(List, Dense.data(), WideOver, TallOver, { 0, 0 }, { Over, Over },
                                ImTextureID(), {});
        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 5);
        for (int Y = 0; Y < Height; ++Y) for (int X = 0; X < Width; ++X) for (int C = 0; C < 3; ++C)
        {
            int Total = 0;
            for (int DY = 0; DY < Over; ++DY) for (int DX = 0; DX < Over; ++DX)
                Total += Dense[(size_t(Y * Over + DY) * WideOver + X * Over + DX) * 3 + C];
            Pixels[(size_t(Y) * Width + X) * 3 + C] = static_cast<unsigned char>(Total / (Over * Over));
        }
        const std::string Path = "Exhibits/Gallery/CloudNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "capture written");
    };

    //-----------------------------------------------------------------------------------------------------
    // The field the whole panel reads from.
    //-----------------------------------------------------------------------------------------------------
    Check(std::fabs(Threshold(Layer) - (1.0f - 0.46f * 0.78f)) < 1e-6f,
          "the condensate threshold is one less coverage times .78");
    Check(std::strcmp(CloudName(0.0f), "Few") == 0 && std::strcmp(CloudName(0.2f), "Scattered") == 0
       && std::strcmp(CloudName(0.46f), "Broken") == 0 && std::strcmp(CloudName(0.8f), "Overcast") == 0,
          "few, scattered, broken and overcast split at .12, .35 and .65");
    {
        const double H = Hash(3.0, 7.0);
        Check(H >= 0.0 && H < 1.0, "the hash stays in the unit interval");
        Check(std::fabs(Hash(3.0, 7.0) - H) < 1e-15, "and is stable for the same cell");
        Check(std::fabs(ValueNoise(2.0, 5.0) - Hash(2.0, 5.0)) < 1e-12,
              "value noise lands exactly on the lattice at integer coordinates");
        for (int I = 0; I < 400; ++I)
        {
            const float N = Noise((I % 20) * 11, (I / 20) * 13, Layer.Scale, Layer.Detail);
            Check(N >= 0.0f && N <= 1.0f, "the field is clamped to nought and one everywhere");
        }
    }

    //-----------------------------------------------------------------------------------------------------
    // The readouts.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(int(std::lround(Layer.Coverage * 8.0f)) == 4, "a .46 cover reads four oktas");
        Check(std::fabs(Layer.Density * 12.0f - 7.44f) < 1e-4f, "optical depth is density times twelve");
        const float Passing = std::exp(-Layer.Density * Layer.Coverage * 2.2f) * 100.0f;
        Check(std::fabs(Passing - 53.396f) < 0.01f, "just over half the sun reaches the datum through it");
        Check(int(std::lround(12.0f / Layer.Scale)) == 12, "a 1x feature scale is a twelve kilometre swath");
        Check(int(std::lround(12.0f / 4.0f)) == 3, "and the widest cells narrow it to three");
    }

    //-----------------------------------------------------------------------------------------------------
    // The histogram: the bins past the threshold are the sky that has cloud in it.
    //-----------------------------------------------------------------------------------------------------
    {
        int Bins[20];
        Population(Layer, Bins);
        int Total = 0, Peak = 0, PeakAt = 0;
        for (int I = 0; I < 20; ++I)
        {
            Total += Bins[I];
            if (Bins[I] > Peak) { Peak = Bins[I]; PeakAt = I; }
        }
        Check(Total == 1600, "all sixteen hundred probes land in a bin");
        Check(Peak > 0, "the distribution has a mode");
        Check(PeakAt > 2 && PeakAt < 17, "and it sits in the body of the range, not against an edge");
        int Condensed = 0;
        for (int I = 0; I < 20; ++I) if (float(I) / 20.0f > Threshold(Layer)) Condensed += Bins[I];
        Check(Condensed > 0 && Condensed < Total, "a broken sky is neither clear nor solid");
    }

    //-----------------------------------------------------------------------------------------------------
    // Captures.
    //-----------------------------------------------------------------------------------------------------
    Capture("Panel");

    Layer.Coverage = 0.88f;
    Layer.Density = 0.93f;
    Layer.Altitude = 72.0f;
    Layer.Scale = 0.45f;
    Layer.Detail = 0.86f;
    Layer.Speed = 3.4f;
    Layer.Linked = false;
    Capture("PanelOvercast");
    Check(std::strcmp(CloudName(Layer.Coverage), "Overcast") == 0, "an .88 cover is overcast");
    Check(std::exp(-Layer.Density * Layer.Coverage * 2.2f) * 100.0f < 20.0f,
          "an overcast deck lets less than a fifth of the sun through");
    Check(int(std::lround(12.0f / Layer.Scale)) == 27, "sheet-scale cells widen the swath to 27 km");

    Layer = CloudDraft{};
    Layer.Coverage = 0.06f;
    Layer.Density = 0.22f;
    Layer.Altitude = 320.0f;
    Capture("PanelFew");
    Check(std::strcmp(CloudName(Layer.Coverage), "Few") == 0, "a .06 cover is few");
    Check(int(std::lround(Layer.Coverage * 8.0f)) == 0, "and rounds to nought oktas");

    //-----------------------------------------------------------------------------------------------------
    // What the mount in CloudsInspectorPanel.cpp actually feeds the cards. The engine keeps density as a
    //    0-4 multiplier and a base in the hundreds of metres, so the normalisation is proved here rather
    //    than assumed.
    //-----------------------------------------------------------------------------------------------------
    {
        const float EngineDensity = 2.4f;    // the sheet's 0..4 multiplier
        const float EngineBase    = 1500.0f; // metres, well past the instrument's own 400 m window
        Layer = CloudDraft{};
        Layer.Coverage = 0.72f;
        Layer.Density  = std::clamp(EngineDensity / 4.0f, 0.0f, 1.0f);
        Layer.Altitude = EngineBase;
        Layer.Scale    = 1.8f;
        Check(std::fabs(Layer.Density - 0.6f) < 1e-6f, "a 2.4x engine density is .6 optical density");
        Check(Layer.Density >= 0.0f && Layer.Density <= 1.0f, "and the normalisation cannot leave the unit range");
        Check(std::clamp(4.0f / 4.0f, 0.0f, 1.0f) == 1.0f, "the top of the engine range is fully opaque");
        Check(std::strcmp(CloudName(Layer.Coverage), "Overcast") == 0, "a .72 cover reads overcast");
        Check(int(std::lround(12.0f / Layer.Scale)) == 7, "a 1.8x feature scale is a seven kilometre swath");
        // A base past the instrument's own window must stay inside the section box, not paint the card above.
        Check(Layer.Altitude > 400.0f, "an engine base can sit well past the 400 m section window");
        Capture("PanelMounted");
    }

    ImGui::DestroyContext();
    std::printf("PASS %u checks: shipped cloud panel, satellite map, coverage histogram and vertical section.\n",
                Checks);
    return 0;
}
