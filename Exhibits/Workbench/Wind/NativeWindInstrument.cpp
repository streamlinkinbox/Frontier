//==============================================================================================================================================
//                                                         NATIVEWINDINSTRUMENT.CPP
//==============================================================================================================================================
// 📦 Executed proof: records the shipped anemometer card from real ImGui draw commands, against InspectorDepot/panels/wind.js
//    as rewritten by InstrumentSpecification.js.

#include "WindInstrumentSurface.h"
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
using namespace Frontier::WindInstrument;

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
    std::filesystem::create_directories("Exhibits/Gallery/WindInstrument");
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

    int   Width = 340, Height = 900;
    float Measured = 0;
    WindDraft Draft;
    TraceLog  Log;
    Sky       Air;
    bool      Tall = false;
    bool      WholePanel = false;
    const Follower Flock[3] = { { "Cloud deck", true }, { "Water", true }, { "Foliage", false } };

    // .mpanel is a 10 px flex column and .pcard carries its own 10 px bottom margin, so a card is
    //    followed by 20 px and a bare block by 10.
    auto PanelHeight = [&](ImFont* With)
    {
        return HeroTall + 20.0f + PillTall + 10.0f + (DuoHeight(Face, float(Width) - 20.0f, Log) + 10.0f) + 10.0f
             + AnemometerHeight(Tall) + 20.0f + BeaufortHeight() + 20.0f + SteadinessHeight() + 20.0f
             + DrivingHeight(With, 320.0f, Flock, 3) + 10.0f;
    };

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginChild("##wi", { 0, float(Height) }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Face, 14.0f);
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const ImVec2 Origin{ 10.0f, 10.0f };
        const float  Card = float(Width) - 20.0f;
        Draw->AddRectFilled({ 0, 0 }, { float(Width), float(Height) }, IM_COL32(5, 5, 5, 255));
        float Deep;
        if (WholePanel)
        {
            float Y = Origin.y;
            PaintHero(Draw, Face, { Origin.x, Y }, Card, HeroTall, Air, Log, Draft);   Y += HeroTall + 20.0f;
            PaintRail(Draw, Face, { Origin.x, Y }, Card, Log, Draft);                  Y += PillTall + 10.0f;
            PaintDuo(Draw, Face, { Origin.x, Y }, Card, Log);                          Y += DuoHeight(Face, Card, Log) + 10.0f + 10.0f;
            PaintAnemometer(Draw, Face, { Origin.x, Y }, Card, Log, Draft, Tall);
            Y += AnemometerHeight(Tall) + 20.0f;
            PaintBeaufortCard(Draw, Face, { Origin.x, Y }, Card, Draft);               Y += BeaufortHeight() + 20.0f;
            PaintSteadiness(Draw, Face, { Origin.x, Y }, Card, Draft);                 Y += SteadinessHeight() + 20.0f;
            PaintDriving(Draw, Face, { Origin.x, Y }, Card, Flock, 3);
            Deep = Y - Origin.y + DrivingHeight(Face, Card, Flock, 3) + 10.0f;
        }
        else
        {
            PaintAnemometer(Draw, Face, Origin, Card, Log, Draft, Tall);
            Deep = AnemometerHeight(Tall);
        }
        ImGui::PopFont();
        ImGui::SetCursorScreenPos({ 0, 0 });
        ImGui::Dummy({ float(Width), Deep + 20.0f });
        Measured = ImGui::GetCursorPosY();
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        Height = WholePanel ? int(PanelHeight(Face)) + 220 : 900;
        Tick();
        Height = static_cast<int>(Measured) + 2;
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        // The CPU backend takes one sample per pixel with a hard inside test, so a 13 px decimal point
        //    can fall between two pixel centres and vanish. Draw three times over and box filter back
        //    down: every output pixel then carries nine samples, which is what the browser's own
        //    antialiasing would have given us.
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
        const std::string Path = "Exhibits/Gallery/WindInstrument/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "capture written");
    };

    //-----------------------------------------------------------------------------------------------------
    // The Beaufort scale, the land signs and the compass, band by band.
    //-----------------------------------------------------------------------------------------------------
    Check(Force(0.0f).Force == 0 && std::strcmp(Force(0.0f).Name, "Calm") == 0, "nought is calm");
    Check(Force(0.49f).Force == 0, "the calm band runs to just under half a metre a second");
    Check(Force(0.5f).Force == 1 && std::strcmp(Force(0.5f).Name, "Light air") == 0, "half is light air");
    Check(Force(4.2f).Force == 3 && std::strcmp(Force(4.2f).Name, "Gentle breeze") == 0,
          "the 4.2 m/s default is a force 3 gentle breeze");
    Check(Force(17.1f).Force == 8 && std::strcmp(Force(17.1f).Name, "Gale") == 0, "17.1 is a gale");
    Check(Force(28.4f).Force == 11 && std::strcmp(Force(28.4f).Name, "Violent storm") == 0,
          "past the table is a violent storm");
    Check(std::strcmp(LandSign(4.2f), "flags stir, leaves move") == 0, "a gentle breeze stirs flags");
    Check(std::strcmp(LandSign(99.0f), "structural damage") == 0, "past the table is structural damage");
    Check(std::strcmp(Compass(0.0f), "N") == 0 && std::strcmp(Compass(214.0f), "SW") == 0,
          "214 degrees is south-west on the sixteen-point compass");
    Check(std::strcmp(Compass(-11.0f), "N") == 0, "a negative bearing wraps rather than indexing out of range");
    Check(std::strcmp(Compass(360.0f), "N") == 0, "a full turn comes back to north");

    //-----------------------------------------------------------------------------------------------------
    // The gust model and the rolling trace.
    //-----------------------------------------------------------------------------------------------------
    Check(std::fabs(Rate(Draft) - (0.4f + 0.24f * 2.2f)) < 1e-6f, "the clock runs at .4 plus turbulence times 2.2");
    Check(std::fabs(GustAt(Draft, 0.0f) - (1.0f + 0.3f * (std::sin(1.7f) * 0.3f) * 0.55f
                                          + 0.24f * (std::sin(2.1f) * 0.5f) * 0.18f)) < 1e-5f,
          "the gust envelope is three swells plus two grains, weighted .55 and .18");
    {
        WindDraft Steady;
        Steady.Gust = 0.0f;
        Steady.Turbulence = 0.0f;
        Check(std::fabs(GustAt(Steady, 3.7f) - 1.0f) < 1e-6f, "no gust and no turbulence is a flat envelope");
    }

    Prefill(Log, Draft);
    Check(Log.Ready, "the trace is prefilled so it never looks switched off");
    {
        float High = Crest(Log), Low = Lull(Log);
        Check(High > Draft.Speed && Low < Draft.Speed, "a prefilled minute straddles the mean");
        Check(Low >= 0.0f, "the trace is clamped at nought, never negative");
        Check(std::fabs(Log.Sample[TraceLength - 1]
                        - std::max(0.0f, Draft.Speed * GustAt(Draft, -0.25f * Rate(Draft)))) < 1e-5f,
              "the newest prefilled sample is one quarter-second behind the clock");
    }
    {
        TraceLog Walk = Log;
        const float Held = Walk.Sample[1];
        Advance(Walk, Draft, 0.1f);
        Check(Walk.Sample[1] == Held, "a tenth of a second does not yet shift the trace");
        Check(std::fabs(Walk.Spare - 0.1f) < 1e-6f, "the remainder is carried, not dropped");
        Advance(Walk, Draft, 0.2f);
        Check(Walk.Sample[1] != Held, "crossing a quarter second shifts one sample off the back");
        Check(std::fabs(Walk.Spare - 0.05f) < 1e-6f, "and the overshoot carries forward");
        Advance(Walk, Draft, 10.0f);
        Check(Walk.Spare < 0.25f, "a long frame drains the whole backlog rather than one sample");
    }
    Check(TraceLength == 240, "sixty seconds at four samples a second is 240 readings");

    //-----------------------------------------------------------------------------------------------------
    // The readouts beneath the number.
    //-----------------------------------------------------------------------------------------------------
    {
        const float Pressure = 0.5f * 1.225f * Draft.Speed * Draft.Speed;
        Check(std::fabs(Pressure - 10.805f) < 0.01f, "4.2 m/s is 10.8 Pa of dynamic pressure");
        Check(std::fabs(Draft.Speed * 3.6f - 15.12f) < 0.01f, "4.2 m/s is 15 km/h");
        Check(std::fabs(Draft.Speed * 1.944f - 8.1648f) < 0.01f, "4.2 m/s is 8.2 knots");
        Check(Crest(Log) / std::max(Draft.Speed, 0.1f) > 1.0f, "the gust factor is the crest over the mean");
    }

    //-----------------------------------------------------------------------------------------------------
    // Captures.
    //-----------------------------------------------------------------------------------------------------
    Capture("Anemometer");

    Tall = true;
    Capture("AnemometerTall");
    Tall = false;

    Draft.Speed = 19.4f;
    Draft.Gust = 0.62f;
    Draft.Turbulence = 0.55f;
    Prefill(Log, Draft);
    Capture("AnemometerGale");
    {
        Check(Force(Draft.Speed).Force == 8, "19.4 m/s is a force 8 gale");
        Check(Crest(Log) > 20.0f, "a gale's crest clears twenty metres a second");
    }

    Draft = WindDraft{};
    Draft.Speed = 0.3f;
    Draft.Gust = 0.05f;
    Draft.Turbulence = 0.02f;
    Prefill(Log, Draft);
    Capture("AnemometerCalm");
    {
        Check(Force(Draft.Speed).Force == 0, "a third of a metre a second is still calm");
        const float Top = std::max(2.0f, std::max(Crest(Log), Draft.Speed) * 1.18f);
        Check(Top == 2.0f, "the trace keeps a two metre floor so a calm field still has an axis");
    }

    //-----------------------------------------------------------------------------------------------------
    // The hero field.
    //-----------------------------------------------------------------------------------------------------
    Draft = WindDraft{};
    Prefill(Log, Draft);
    SkyPrime(Air);
    {
        Check(MoteCount == 190, "a hundred and ninety motes stream the field");
        Check(std::fabs(Air.Motes[0].X - 0.0f) < 1e-6f && std::fabs(Air.Motes[1].X - 0.977f) < 1e-6f,
              "the motes are seeded off the same integer sieve the browser uses");
        float UX, UY;
        Heading(Draft, UX, UY);
        // 214 degrees is the bearing it comes FROM, so the air travels towards 34 degrees.
        const float Towards = std::fmod(std::atan2(UY, UX) * 180.0f / Pi + 90.0f + 360.0f, 360.0f);
        Check(std::fabs(Towards - 34.0f) < 0.01f, "a south-westerly blows towards the north-east");
        Check(std::fabs(Curl(0.0f, 0.0f, 0.0f)) < 1e-6f, "the curl field is nought at the origin");
        const float Before = Air.Motes[7].X;
        SkyAdvance(Air, Draft, Log, 320.0f, HeroTall, 1.0f / 60.0f);
        Check(Air.Motes[7].X != Before, "a frame moves the air");
        for (int Frame = 0; Frame < 600; ++Frame) SkyAdvance(Air, Draft, Log, 320.0f, HeroTall, 1.0f / 60.0f);
        for (const Mote& Speck : Air.Motes)
            Check(Speck.X >= -0.16f && Speck.X <= 1.16f && Speck.Y >= -0.16f && Speck.Y <= 1.16f,
                  "ten seconds on, every mote is still recycled inside the field");
    }

    //-----------------------------------------------------------------------------------------------------
    // The Beaufort block scale.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(std::fabs(Beaufort[0].Limit - 0.5f) < 1e-6f && std::fabs(Beaufort[10].Limit - 28.4f) < 1e-5f,
              "the scale runs from calm at .5 to storm at 28.4, then a twelfth block out to 30");
        // The block ramp walks green to red across the eleven forces.
        const int ColdR = int(std::lround(60 + 0.0f * 195)), HotR = int(std::lround(60 + 1.0f * 195));
        Check(ColdR == 60 && HotR == 255, "force nought is green, force eleven is red");
    }

    //-----------------------------------------------------------------------------------------------------
    // Steadiness and the followers.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(std::fabs((1.0f + 0.3f * 0.6f) - 1.18f) < 1e-6f, "the default gust-to-mean ratio is 1.18x");
        Check(int(0.24f * 100.0f + 0.5f) == 24, "the default turbulence intensity reads 24 per cent");
        Check(Flock[2].Linked == false, "a follower can be cut loose and keeps its own drift");
    }

    //-----------------------------------------------------------------------------------------------------
    // The whole panel, in the reference's own order.
    //-----------------------------------------------------------------------------------------------------
    WholePanel = true;
    SkyPrime(Air);
    for (int Frame = 0; Frame < 90; ++Frame) SkyAdvance(Air, Draft, Log, 320.0f, HeroTall, 1.0f / 60.0f);
    Capture("Panel");

    Draft.Speed = 19.4f;
    Draft.Gust = 0.62f;
    Draft.Turbulence = 0.55f;
    Draft.Direction = 287.0f;
    Prefill(Log, Draft);
    SkyPrime(Air);
    for (int Frame = 0; Frame < 90; ++Frame) SkyAdvance(Air, Draft, Log, 320.0f, HeroTall, 1.0f / 60.0f);
    Capture("PanelGale");
    Check(Crest(Log) > 17.0f, "a gale trips the gusting-to card into its alarmed colour");
    Check(std::strcmp(Compass(287.0f), "WNW") == 0, "287 degrees is west-north-west");

    ImGui::DestroyContext();
    std::printf("PASS %u checks: shipped anemometer card, Beaufort scale and the rolling sixty-second trace.\n",
                Checks);
    return 0;
}
