//==============================================================================================================================================
//                                                          NATIVEGASCARD.CPP
//==============================================================================================================================================
// 📦 Executed proof: the native gas domain inspector, recorded from real ImGui draw commands, against the
//    HTML it was ported from — Experimental/ProjectZeroEditor/GasPanel.jsx and GasSpecification.js — and
//    the lifecycle it drives, Engine/VolumetricDynamics/GasDomainResidency.h.
//
//    Three kinds of claim are checked here, and only the first could be seen in a screenshot:
//
//    ① THE LAYOUT. The card stack is a CSS block flow, so the heights are pinned: a slider row is the
//       11 px label, its 8 px margin and a 30 px pill, and nothing in a gas card may disagree with what a
//       fog card does with the same stylesheet.
//    ② THE SHEET. Eleven inline readings, each one naming a parameter the simulator actually has, with the
//       lattice deliberately absent — the quality ladder owns the extent, and a control the budget
//       overrules every frame is a lie with a handle on it.
//    ③ THE LIFECYCLE. 🔴 Most gas is a one-shot. Fired, it burns for its lifetime, fades until the smoke is
//       under the faintest visible reading, and is then released — its bytes back in the budget on that
//       tick. A capture cannot show that; the arithmetic can.

#include "GasCardSurface.h"
#include "GasDomainResidency.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::GasCards;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
    std::printf("  PASS  %s\n", Claim);
}
void Banner(const char* Title) { std::printf("\n%s\n", Title); }
bool Near(float A, float B, float Tolerance = 0.01f) { return std::fabs(A - B) <= Tolerance; }
}   // namespace

int main()
{
    std::printf("NativeGasCard - the ported gas inspector\n");
    std::filesystem::create_directories("Exhibits/Gallery/GasCardNative");

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

    constexpr int   Margin   = 20;
    constexpr float CardWide = 300.0f;                       // [px] the shipped inspector column
    const int Width = int(CardWide) + Margin * 2;
    int Height = 1400;
    GasCardSubject Subject;

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
        Draw->AddRectFilled({ 0, 0 }, { float(Width), float(Height) }, PageFill);
        PaintGasCard(Draw, Light, Regular, { float(Margin), float(Margin) }, CardWide, Subject);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        Height = int(std::ceil(ResolveStack(Light, CardWide, Subject).Total)) + Margin * 2;
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        constexpr int Over = 3;                              // 3x supersample, as the other card proofs use
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
        const std::string Path = "Exhibits/Gallery/GasCardNative/" + std::string(Name) + ".png";
        Check(PngWriteCounterpart::WritePng(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0,
              (std::string("capture written: ") + Name).c_str());
    };

    //---------------------------------------------------------------------------------------------------------
    // ② The sheet, against GasSpecification.js.
    //---------------------------------------------------------------------------------------------------------
    Banner("The inline sheet is the browser's sheet");
    {
        uint32_t Count = 0u;
        const GasField* Fields = GasSheet(Count);
        Check(Count == 11u, "eleven readings are inline; the other seventy-four are one button away");

        bool Lattice = false;
        for (uint32_t Index = 0u; Index < Count; ++Index)
            if (std::strcmp(Fields[Index].Key, "gridResolution") == 0) Lattice = true;
        Check(!Lattice, "the lattice is NOT one of them - the quality ladder owns the extent at runtime");

        for (uint32_t Index = 0u; Index < Count; ++Index)
        {
            Check(Fields[Index].Key[0] != '\0' && Fields[Index].Group[0] != '\0',
                  "every field names a simulator parameter and the card it lands in");
            if (Fields[Index].Control == GasControl::Slider)
                Check(Fields[Index].Maximum > Fields[Index].Minimum &&
                      Fields[Index].Default >= Fields[Index].Minimum &&
                      Fields[Index].Default <= Fields[Index].Maximum,
                      "and a slider's default sits inside its own range");
        }

        uint32_t EmitterCount = 0u;
        GasEmitterSheet(EmitterCount);
        Check(EmitterCount == 7u, "an emitter carries its own seven, because it is an entity and not a group of settings");
    }

    //---------------------------------------------------------------------------------------------------------
    // ① The layout, against Editor.css and MaterialPanel.css.
    //---------------------------------------------------------------------------------------------------------
    Banner("The measurements are the stylesheet's, not new ones");
    {
        uint32_t Count = 0u;
        const GasField* Fields = GasSheet(Count);
        Check(Near(FieldHeight(Fields[1]), LabelSize + LabelFoot + PillTall),
              "a slider row is the 11 px label, its 8 px margin and the 30 px pill - the same row a fog slider is");
        Check(Near(FieldHeight(Fields[0]), SwitchTall), "a switch row is the 24 px toggle, with no label line above it");
        Check(Near(SplitWide, 92.0f) && Near(SplitNumber, 58.0f),
              "the split value is 92 px with a 58 px number, leaving 34 for the unit");
        Check(Near(TrackTall, 26.0f) && Near(ThumbWide, 24.0f), "the track is 26 px with a 24 px thumb");
        Check(Near(CardRound, 22.0f) && Near(CardPadX, 24.0f),
              "a property card is 22 px round with 24 px side padding");

        const float Expected = PlainPadY + 14.0f + 18.0f + 20.0f + 3.0f * (RowTall + RowPad * 2.0f)
                             + 14.0f + PlainPadY;
        Check(Near(TransformHeight(), Expected), "the transform is a three-row table at 29 px a row inside a generic card");

        const GasStackLayout Layout = ResolveStack(Light, 300.0f, Subject);
        Check(Layout.Total > 1000.0f, "the whole stack is taller than the column, which is why the inspector scrolls");
        Check(Layout.Transform > 0.0f && Layout.Caption > 0.0f,
              "① the transform and its caption are bands of the stack, not an afterthought under the settings");
        const float Summed = Layout.Tiles + Layout.Transform + Layout.Caption + Layout.Domain + Layout.Simulation
                           + Layout.Budget + Layout.Source + Layout.Appearance + Layout.Hierarchy + Layout.Open;
        Check(Near(Summed, Layout.Total, 0.05f), "and the bands sum to the total the painter returns");

        GasCardSubject WithChild = Subject;
        WithChild.ChildCount = 1u;
        const GasStackLayout Childed = ResolveStack(Light, 300.0f, WithChild);
        Check(Childed.Source < Layout.Source,
              "② a domain with a child emitter shows no Source sliders - the child owns them, and two places to set one reading is one too many");
        Check(Childed.Hierarchy > Layout.Hierarchy, "and its hierarchy card grows by the row");
    }

    //---------------------------------------------------------------------------------------------------------
    // The three decisions the card is built on.
    //---------------------------------------------------------------------------------------------------------
    Banner("A 3D entity: a transform, children, and what is deliberately not a child");
    {
        Check(Near(Subject.Bounds[0], 1.85f) && Near(Subject.Bounds[1], 2.1f) && Near(Subject.Bounds[2], 1.85f),
              "① bounds are three metres on the transform, where a mesh has Scale");
        Check(Near(Subject.Bounds[0], Subject.Bounds[2]),
              "X and Z stay equal: the solver uses one width for both horizontal axes");
        Check(std::strstr(RotationCaption(), "NOT THE LATTICE") != nullptr,
              "⚠️ the card itself admits that rotation never reaches the axis-aligned lattice");
        Check(std::strstr(HierarchyNote(), "survive the deletion") != nullptr,
              "③ and that a collider is not a child, so deleting the domain cannot delete the crate");
        Check(!Subject.Coupled, "🔴 two-way coupling is off on a new domain, as GasWindContribution.h has it");
    }

    //---------------------------------------------------------------------------------------------------------
    // ③ The lifecycle: GasDomainResidency.h.
    //---------------------------------------------------------------------------------------------------------
    Banner("🔴 A one-shot runs once, is destroyed, and hands its fields back");
    {
        GasResidencyConsent Consent;
        Consent.Policy   = GasRunPolicy::Triggered;
        Consent.Lifetime = 2.0f;
        Consent.Retire   = true;

        GasDomainResidency One;
        Check(One.Phase == GasPhase::Dormant && One.HeldBytes == 0ull,
              "a placed domain is dormant and holds nothing - the row exists, the solver does not");
        Check(Advance(One, Consent, 1.0f / 60, 0.0f, 0.0f) == GasPhase::Dormant,
              "and advancing it changes nothing, because nothing asked it to run");

        Check(Ignite(One, GasQuality::Near), "firing it charges the rung it was granted");
        Check(One.HeldBytes == DeviceBytesFor(GasQuality::Near), "96 cubed at 47 bytes a voxel, from the allowance header");
        Check(!Ignite(One, GasQuality::Near),
              "a second trigger on the same frame is a no-op, not a restart - gameplay does that, and restarting would cancel the first");
        Check(Emitting(One) && Simulating(One), "while burning it both emits and advances");

        // 125 steps rather than 120: a float sum of sixtieths lands either side of two seconds, and a
        //    check that depends on which side is a check that fails on somebody else's compiler.
        for (int Step = 0; Step < 125; ++Step) Advance(One, Consent, 1.0f / 60, 1.0f, 0.0f);
        Check(One.Phase == GasPhase::Fading, "at the end of its lifetime emission stops");
        Check(!Emitting(One) && Simulating(One),
              "but the solver keeps advancing - cutting here would make a smoke column vanish in mid-air");
        Check(One.HeldBytes == DeviceBytesFor(GasQuality::Near), "and a fading domain is charged in full, because it is live");

        Advance(One, Consent, 1.0f / 60, 0.9f, 0.0f);
        Check(One.Phase == GasPhase::Fading, "thick smoke keeps it fading");
        Advance(One, Consent, 1.0f / 60, 0.001f, 0.0f);
        Check(One.Phase == GasPhase::Released, "and it is released on the measurement, not on a second clock");
        Check(One.HeldBytes == 0ull && Retired(One), "🔴 its bytes are back in the budget on that tick");
        Check(!Simulating(One), "nothing advances a released domain");

        GasResidencyConsent Kept = Consent;
        Kept.Retire = false;
        GasDomainResidency Two;
        Ignite(Two, GasQuality::Near);
        for (int Step = 0; Step < 200; ++Step) Advance(Two, Kept, 1.0f / 60, Step < 125 ? 1.0f : 0.0f, 0.0f);
        Check(Two.Phase == GasPhase::Dormant && Two.HeldBytes > 0ull,
              "a domain told to stay resident returns to dormant still holding its fields - that is the whole difference, and the card makes you choose it");

        GasDomainResidency Stuck;
        Ignite(Stuck, GasQuality::Mid);
        for (int Step = 0; Step < 60 * 60; ++Step) Advance(Stuck, Consent, 1.0f / 60, 1.0f, 0.0f);
        Check(Stuck.Phase == GasPhase::Released,
              "a field that never settles is released at the fade ceiling - holding its storage while that bug is found helps nobody");

        GasResidencyConsent Far;
        Far.Policy = GasRunPolicy::Proximity;
        GasDomainResidency Walked;
        Advance(Walked, Far, 1.0f / 60, 0.0f, 40.0f);
        Check(Simulating(Walked), "a proximity domain starts simulating when the viewer is inside the band");
        Advance(Walked, Far, 1.0f / 60, 0.5f, 400.0f);
        Check(Walked.Phase == GasPhase::Released && Walked.HeldBytes == 0ull,
              "and gives its storage back when they leave, which is a release like any other");
    }

    Banner("The budget sees only what is live");
    {
        GasBudgetCeiling Ceiling;
        GasDomainResidency Domains[4];
        Ignite(Domains[0], GasQuality::Hero);
        Ignite(Domains[1], GasQuality::Near);
        Domains[1].Phase = GasPhase::Fading;
        Ignite(Domains[2], GasQuality::Mid);
        // Domains[3] is left dormant.
        const GasOccupancy Occupancy = ResolveOccupancy(Domains, 4u, Ceiling);
        Check(Occupancy.LiveDomains == 3u, "three live, and the dormant one is not charged");
        Check(Occupancy.HeldBytes == DeviceBytesFor(GasQuality::Hero) + DeviceBytesFor(GasQuality::Near)
                                   + DeviceBytesFor(GasQuality::Mid),
              "the charge is the sum of the rungs they hold");
        Check(Occupancy.Retiring == 1u && Occupancy.ReturningSoon == DeviceBytesFor(GasQuality::Near),
              "and the fading one is counted separately, because its storage is about to come back");
        Check(!Occupancy.OverCeiling, "well inside 256 MB over twelve domains");
        Check(Admits(Occupancy, GasQuality::Hero, Ceiling), "another hero fits");

        GasOccupancy Crowded;
        Crowded.LiveDomains = 12u;
        Crowded.HeldBytes   = 200ull * 1024ull * 1024ull;
        Check(!Admits(Crowded, GasQuality::Far, Ceiling), "the thirteenth live domain is refused on the count alone");
        Check(Admits(Crowded, GasQuality::Flipbook, Ceiling),
              "but a flipbook is always admitted - it costs no live storage, which is the point of the rung");

        GasOccupancy Heavy;
        Heavy.LiveDomains = 2u;
        Heavy.HeldBytes   = 250ull * 1024ull * 1024ull;
        Check(!Admits(Heavy, GasQuality::Hero, Ceiling), "and a hero is refused on bytes with room in the count");
    }

    //---------------------------------------------------------------------------------------------------------
    // The preview tells the truth about whether anything is running.
    //---------------------------------------------------------------------------------------------------------
    Banner("🔴 The preview refuses to animate a domain the game would not run");
    {
        GasCardSubject Dormant;
        Dormant.Policy = 0u;
        Check(!GasCardRunning(Dormant), "a dormant domain is not running");
        Dormant.Fired = true;
        Check(GasCardRunning(Dormant), "and is once something fires it");

        GasCardSubject Always;
        Always.Policy = 3u;
        Check(GasCardRunning(Always), "an always-on domain runs with nothing firing it");
        Always.Hidden = true;
        Check(!GasCardRunning(Always), "a hidden domain simulates nothing, whatever its policy says");

        char Words[128];
        GasCardSubject Shot;
        Shot.Policy = 1u; Shot.Retire = true; Shot.Lifetime = 4.0f;
        StateWords(Shot, Words, sizeof(Words));
        Check(std::strstr(Words, "destroyed") != nullptr,
              "the card says a retiring one-shot is destroyed, in words rather than only in a colour");
        Shot.Retire = false;
        StateWords(Shot, Words, sizeof(Words));
        Check(std::strstr(Words, "holding its fields") != nullptr, "and says what the other choice costs");

        Check(std::strstr(GasPhaseReadout(GasPhase::Fading), "charged to the budget") != nullptr,
              "the runtime spells the fade the same way, so the card and the engine share one vocabulary");
    }

    //---------------------------------------------------------------------------------------------------------
    // Captures.
    //---------------------------------------------------------------------------------------------------------
    Banner("Captures");
    {
        Subject = GasCardSubject{};
        Subject.Policy = 1u;
        std::snprintf(Subject.Cost, sizeof(Subject.Cost), "40 MB");
        Capture("CardOneShotDormant");

        Subject.Fired = true;
        Capture("CardOneShotFiring");

        Subject.Retire = false;
        Subject.Departures = 3u;
        Capture("CardKeptResident");

        Subject = GasCardSubject{};
        Subject.Policy = 3u;
        Subject.PresetName = "Camp fire";
        Subject.ChildCount = 2u;
        Subject.Children[0] = GasChild{ "Flame Source", "1.00x - 3.5 K", true };
        Subject.Children[1] = GasChild{ "Ember Vent", "0.40x - 1.2 K", false };
        Subject.Readings[9] = 9.0f;
        Capture("CardAlwaysWithEmitters");

        // ③ The same card drawn for an emitter row: the Open band is the only thing that changes, because
        //    the expanded editor behind it is the Particle Editor rather than the Fluid one.
        Subject = GasCardSubject{};
        Subject.Emitter = true;
        Subject.Name = "Flame Source";
        Subject.PresetName = "Hearth embers";
        Capture("CardEmitterOpensParticleEditor");

        Subject = GasCardSubject{};
        Subject.Policy = 2u;
        Subject.Distance = 6.0f;
        Subject.QualityPin = "Hero - 128 cubed";
        Subject.TierReadout = "Hero tier - 128 cubed lattice - 24 sweeps - 60 Hz";
        std::snprintf(Subject.Cost, sizeof(Subject.Cost), "94 MB");
        Subject.OverAmber = true;
        Subject.Bounds[1] = 4.2f;
        Capture("CardHeroOverAmber");
    }

    ImGui::DestroyContext();
    std::printf("\nPASS %u\n", Checks);
    return 0;
}
