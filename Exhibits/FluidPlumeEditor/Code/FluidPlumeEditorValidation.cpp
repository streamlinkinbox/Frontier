/*==========================================================================================================================================
                                                   FLUIDPLUMEEDITORVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor, checked as a user meets it:
//      ① the FluidPlume record: its defaults, the 33 snake_case keys of its TOML, a save and reload bit for bit, range clamping, non-finite and missing
//        readings, and the legacy block of Simulation.levelspace;
//      ② every card driven through native input on a real ImGui context: each of the 26 sliders by arrow, Shift-arrow, Home and End, the six presets,
//        Defaults, Clear smoke and the Test emitter — each must change exactly the readings it names, to the decimal the prototype's range input commits;
//      ③ the window in the Everest workspace, opened from a FluidPlume record the way the Inspector's button opens it and driven there too (a preset,
//        the test emitter, a slider, Clear smoke, Defaults, Close), with the host's readout taken from the C++ plume's own CPU half (SmokeSequence on
//        the burnout script), captured at 1920 × 1080 and on a narrower window. The two instruments are looked for in what the window submits.
//
//        FluidPlumeEditorValidation <EngineContent> <Projects/Everest> <output folder>
//
// 📝 Run from the repository root: the legacy fixture is Exhibits/EverestInterface/Content/Simulation.levelspace.

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumePages.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/Inspectors/FluidPlumeInspector.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/PhysicalDynamics/FluidDynamics/GasInstrumentProjection.h"
#include "Engine/PhysicalDynamics/FluidDynamics/GasParameterCatalogue.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/WorldSpace.h"
#include "EverestWorkspace.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "Exhibits/PlumeSimulation/Code/PlumeScenario.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui_internal.h>
#include <stb_image_write.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace Frontier;
using namespace Frontier::FluidDynamics;

namespace
{

std::ofstream Report;
int           Assertions = 0;
int           Failures   = 0;

void Verify(bool Condition, const std::string& Caption)
{
    ++Assertions;
    Report << (Condition ? "PASS" : "FAIL") << '\t' << Caption << '\n';
    Report.flush();   // [-] a crash must not lose the lines before it
    if (!Condition)
    {
        ++Failures;
        std::cerr << "FAIL: " << Caption << '\n';
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE READINGS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The 26 readings the prototype's page puts a slider on, in page order: Source (3), Simulation (10), Appearance (7), Fire (6). The other seven
//    (Expansion, Momentum, TreadEjection, Erosion, Warp, Powder, FlameFlicker) only the presets set.
constexpr std::array<std::string_view, 26> SliderKeys = {
    "Emission", "Heat", "Soot",
    "Buoyancy", "Dissipation", "Cooling", "Vorticity", "Turbulence", "TurbulenceScale", "WindSpeed", "WindHeading", "TimeScale", "PressureSweeps",
    "Density", "Albedo", "Phase", "MultiScatter", "Ambient", "Detail", "DetailScale",
    "FlameIntensity", "FlameCore", "FlameTip", "FlameTemperature", "FlameLoss", "FireLight" };

// 📝 The project file's spelling of every reading, written out here and not derived: the record's keys are a format.
constexpr std::array<std::pair<std::string_view, std::string_view>, 33> FileKeys = {{
    { "Emission", "emission" }, { "Heat", "heat" }, { "Expansion", "expansion" }, { "Soot", "soot" }, { "Momentum", "momentum" },
    { "TreadEjection", "tread_ejection" }, { "Buoyancy", "buoyancy" }, { "Dissipation", "dissipation" }, { "Cooling", "cooling" },
    { "Vorticity", "vorticity" }, { "Turbulence", "turbulence" }, { "TurbulenceScale", "turbulence_scale" }, { "WindSpeed", "wind_speed" },
    { "WindHeading", "wind_heading" }, { "TimeScale", "time_scale" }, { "PressureSweeps", "pressure_sweeps" }, { "Density", "density" },
    { "Albedo", "albedo" }, { "Phase", "phase" }, { "MultiScatter", "multi_scatter" }, { "Ambient", "ambient" }, { "Detail", "detail" },
    { "DetailScale", "detail_scale" }, { "Erosion", "erosion" }, { "Warp", "warp" }, { "Powder", "powder" }, { "FlameIntensity", "flame_intensity" },
    { "FlameTemperature", "flame_temperature" }, { "FlameCore", "flame_core" }, { "FlameTip", "flame_tip" }, { "FlameFlicker", "flame_flicker" },
    { "FlameLoss", "flame_loss" }, { "FireLight", "fire_light" } }};

std::vector<double> Flatten(const GasParameters& Gas)
{
    std::vector<double> Values;
    for (const GasParameterEntry& Entry : QueryGasParameterCatalogue()) Values.push_back(Gas.*(Entry.Field));
    return Values;
}

size_t IndexOf(std::string_view Key)
{
    size_t Index = 0;
    for (const GasParameterEntry& Entry : QueryGasParameterCatalogue())
    {
        if (Entry.Key == Key) return Index;
        ++Index;
    }
    throw std::runtime_error("no catalogue entry " + std::string(Key));
}

size_t CountDifferences(const std::vector<double>& A, const std::vector<double>& B)
{
    size_t Count = 0;
    for (size_t Index = 0; Index < A.size(); ++Index) Count += A[Index] != B[Index] ? 1 : 0;
    return Count;
}

const GasPreset& PresetNamed(std::string_view Name)
{
    for (const GasPreset& Preset : QueryGasPresets())
    {
        if (Preset.Name == Name) return Preset;
    }
    throw std::runtime_error("no preset " + std::string(Name));
}

// 📝 The prototype's SliderDigits: the larger count of decimals among the range's minimum and its step (shortest form of each).
int DecimalPlaces(double Value)
{
    char Text[64];
    std::snprintf(Text, sizeof(Text), "%.12g", Value);
    const char* Point = std::strchr(Text, '.');
    return Point == nullptr ? 0 : static_cast<int>(std::strlen(Point + 1));
}

// 📝 A figure as the prototype's range input commits it: on the grid min + k·step, the decimal the figure shows, parsed back.
double Committed(double Value, const GasParameterEntry& Entry)
{
    char Text[64];
    std::snprintf(Text, sizeof(Text), "%.*f", std::max(DecimalPlaces(Entry.Minimum), DecimalPlaces(Entry.Step)), Value);
    return std::strtod(Text, nullptr);
}

FluidPlumeRecord& AccessPlume(WorldSpace& World, RecordToken Token)
{
    return *dynamic_cast<FluidPlumeRecord*>(World.FindRecord(Token));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ① THE RECORD AND ITS TOML
//------------------------------------------------------------------------------------------------------------------------

void ValidateRecord(const std::filesystem::path& Scratch, const std::filesystem::path& Fixture)
{
    const GasParameters Defaults = ConstructGasParameters();

    FluidPlumeConfiguration Fresh;
    Verify(Fresh.Enabled && Fresh.TestEmitter == 0, "A new FluidPlume record is enabled with the test emitter off");
    Verify(Flatten(Fresh.Gas) == Flatten(Defaults), "A new record holds the catalogue's defaults, all 33 readings");
    Verify(QueryGasParameterCatalogue().size() == FileKeys.size(), "The file format names every one of the 33 catalogue readings");

    // 📝 A level with one FluidPlume record whose every reading is somewhere inside its range, off the defaults.
    WorldSpace        World;
    std::string       Diagnostic;
    const RecordToken Token = World.ConstructRecord("FluidPlume", "Smoke and fire");
    Verify(Token.IsValid() && World.FindRecord(Token) != nullptr, "A FluidPlume record is constructed by the world");
    FluidPlumeConfiguration& Plume = AccessPlume(World, Token).AccessConfiguration();
    Verify(Flatten(Plume.Gas) == Flatten(Defaults) && Plume.Enabled, "The constructed record holds the defaults");
    size_t Offset = 0;
    for (const GasParameterEntry& Entry : QueryGasParameterCatalogue())
    {
        const double Fraction = 0.13 + 0.07 * static_cast<double>(Offset % 11);
        Plume.Gas.*(Entry.Field) = Committed(Entry.Minimum + (Entry.Maximum - Entry.Minimum) * Fraction, Entry);
        ++Offset;
    }
    Plume.Enabled     = false;
    Plume.TestEmitter = 2;
    const std::vector<double> Authored = Flatten(Plume.Gas);
    Verify(CountDifferences(Authored, Flatten(Defaults)) > 25, "The authored readings differ from the defaults");

    toml::table Saved;
    AccessPlume(World, Token).SerializeConfiguration(Saved);
    size_t Spelled = 0;
    for (const auto& [Key, Name] : FileKeys)
    {
        const auto Entry   = Saved[std::string(Name)];
        const bool Present = Entry.is_floating_point();
        Verify(Present, "The file carries " + std::string(Name) + " as a floating point number");
        if (Present) Spelled += Entry.value<double>().value_or(std::nan("")) == Authored[IndexOf(Key)] ? 1 : 0;
    }
    Verify(Spelled == FileKeys.size(), "Every key holds its reading exactly");
    Verify(Saved.size() == FileKeys.size() + 1 && Saved["enabled"].is_boolean() && Saved["enabled"].value<bool>() == false,
           "The file is `enabled` and the 33 readings, nothing else");
    Verify(!Saved.contains("test_emitter") && !Saved.contains("grade"), "The test emitter and the lattice grade are not saved");

    const std::filesystem::path Level = Scratch / "SmokeAndFire.levelspace";
    Verify(World.SaveLevelSpace(Level, &Diagnostic), "The level saves: " + Diagnostic);
    WorldSpace Reloaded;
    Verify(Reloaded.LoadLevelSpace(Level, &Diagnostic), "The level reloads: " + Diagnostic);
    auto* Loaded = dynamic_cast<FluidPlumeRecord*>(Reloaded.FindFirstByType("FluidPlume"));
    Verify(Loaded != nullptr, "The reloaded level has its FluidPlume record");
    if (Loaded != nullptr)
    {
        const FluidPlumeConfiguration& Again = Loaded->QueryConfiguration();
        Verify(Flatten(Again.Gas) == Authored, "A save and reload returns all 33 readings bit for bit");
        Verify(!Again.Enabled && Again.TestEmitter == 0, "Disabled survives the file; the test emitter does not");
        Verify(Again.Revision > 0, "Loading advances the record's revision");
    }

    // 📝 A reading outside its range is held to the range, a number the parser rejects keeps the prior reading, an integer reads as a number, and a
    //    file that names only some readings leaves the rest at the defaults.
    {
        WorldSpace        Other;
        const RecordToken Plain  = Other.ConstructRecord("FluidPlume", "Plain");
        FluidPlumeRecord& Record = AccessPlume(Other, Plain);
        toml::table       Wild{ { "emission", 99.0 }, { "dissipation", -2.0 }, { "wind_heading", std::numeric_limits<double>::quiet_NaN() },
                                { "soot", std::numeric_limits<double>::infinity() }, { "pressure_sweeps", 20 }, { "flame_core", 5000.0 },
                                { "flame_tip", 100.0 }, { "albedo", "bright" } };
        Record.DeserializeConfiguration(Wild);
        const GasParameters& Gas = Record.QueryConfiguration().Gas;
        Verify(Gas.Emission == FindGasParameter("Emission")->Maximum, "Emission above its range is held to its maximum");
        Verify(Gas.Dissipation == FindGasParameter("Dissipation")->Minimum, "Dissipation below its range is held to its minimum");
        Verify(Gas.WindHeading == Defaults.WindHeading, "A NaN wind heading keeps the prior reading");
        Verify(Gas.Soot == Defaults.Soot, "An infinite soot keeps the prior reading");
        Verify(Gas.PressureSweeps == 20.0, "An integer in the file reads as a number");
        Verify(Gas.FlameCore == FindGasParameter("FlameCore")->Maximum && Gas.FlameTip == FindGasParameter("FlameTip")->Minimum,
               "The flame's core and tip are held to the ends of their ranges");
        Verify(Gas.Albedo == Defaults.Albedo, "A string where a number belongs keeps the prior reading");
        Verify(CountDifferences(Flatten(Gas), Flatten(Defaults)) == 5, "Only the five readings the file named moved (emission, dissipation, pressure sweeps, flame core and tip)");
        Verify(Record.QueryConfiguration().Enabled, "A file without `enabled` leaves the record enabled");
    }

    // 📝 The block the Simulation fixture was written with, before the record carried the catalogue: three keys, one of which (vorticity) the
    //    catalogue shares; the others are ignored and nothing else moves.
    {
        WorldSpace Legacy;
        Verify(Legacy.LoadLevelSpace(Fixture, &Diagnostic), "The Simulation fixture loads: " + Diagnostic);
        auto* Old = dynamic_cast<FluidPlumeRecord*>(Legacy.FindFirstByType("FluidPlume"));
        Verify(Old != nullptr, "The Simulation fixture's FluidPlume record is read");
        if (Old != nullptr)
        {
            const GasParameters& Gas = Old->QueryConfiguration().Gas;
            Verify(Gas.Vorticity == 1.85, "Its vorticity (1.85) is read as the catalogue's Vorticity");
            Verify(CountDifferences(Flatten(Gas), Flatten(Defaults)) == 1, "Its other keys (buoyancy_lift, blackbody_k) leave every other reading at the default");
            Verify(Old->QueryConfiguration().Enabled, "Its enabled flag is read");
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ② THE CARDS, DRIVEN BY NATIVE INPUT
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the window submits is searched for the instruments' ink. ImGui fringes an anti-aliased fill with translucent vertices of the same colour,
//    so a band or a stroke is a run of vertices of one colour; an instrument is a run of such runs, in order, unbroken. Consecutive items of one
//    colour are one run. Left to Bottom are the screen extent of the instrument's runs.
struct InkReading
{
    bool  Found  = false;
    float Left   = 0.0f;
    float Right  = 0.0f;
    float Top    = 0.0f;
    float Bottom = 0.0f;
};

constexpr ImU32 InkOf(const InstrumentColour& Colour)
{
    return IM_COL32(Colour[0], Colour[1], Colour[2], 0) & 0x00FFFFFFu;
}

InkReading FindInk(const ImDrawData* Submitted, const std::vector<ImU32>& Wanted)
{
    std::vector<ImU32> Sequence;
    for (const ImU32 Ink : Wanted)
    {
        if (Sequence.empty() || Sequence.back() != Ink) Sequence.push_back(Ink);
    }
    for (const ImDrawList* List : Submitted->CmdLists)
    {
        std::vector<ImU32> Runs;
        std::vector<ImVec2> Minima, Maxima;
        for (const ImDrawVert& Vertex : List->VtxBuffer)
        {
            const ImU32 Ink = Vertex.col & 0x00FFFFFFu;
            if (Runs.empty() || Runs.back() != Ink)
            {
                Runs.push_back(Ink);
                Minima.push_back(Vertex.pos);
                Maxima.push_back(Vertex.pos);
            }
            else
            {
                Minima.back() = ImVec2(std::min(Minima.back().x, Vertex.pos.x), std::min(Minima.back().y, Vertex.pos.y));
                Maxima.back() = ImVec2(std::max(Maxima.back().x, Vertex.pos.x), std::max(Maxima.back().y, Vertex.pos.y));
            }
        }
        for (size_t Start = 0; Start + Sequence.size() <= Runs.size(); ++Start)
        {
            if (!std::equal(Sequence.begin(), Sequence.end(), Runs.begin() + static_cast<std::ptrdiff_t>(Start))) continue;
            InkReading Reading{ true, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX };
            for (size_t Index = Start; Index < Start + Sequence.size(); ++Index)
            {
                Reading.Left   = std::min(Reading.Left, Minima[Index].x);
                Reading.Right  = std::max(Reading.Right, Maxima[Index].x);
                Reading.Top    = std::min(Reading.Top, Minima[Index].y);
                Reading.Bottom = std::max(Reading.Bottom, Maxima[Index].y);
            }
            return Reading;
        }
    }
    return {};
}

InkReading FindStrip(const ImDrawData* Submitted, const GasParameters& Gas)
{
    std::vector<ImU32> Wanted;
    for (const InstrumentBand& Band : ConstructBlackbodyInstrument(Gas, InstrumentCanvasWidth, 108.0).Bands) Wanted.push_back(InkOf(Band.Colour));
    return FindInk(Submitted, Wanted);
}

InkReading FindCurves(const ImDrawData* Submitted, const GasParameters& Gas)
{
    std::vector<ImU32> Wanted;
    for (const InstrumentPolyline& Stroke : ConstructDecayInstrument(Gas, InstrumentCanvasWidth, 172.0).Strokes) Wanted.push_back(InkOf(Stroke.Colour));
    return FindInk(Submitted, Wanted);
}

class InterfaceSession
{
  public:
    explicit InterfaceSession(ControlCentrePanel& InCentre) : Centre(InCentre) {}

    int     Width         = 440;   // [px] one column of the editor, as its page lays it
    int     Height        = 6000;
    float   ContentHeight = 0.0f;
    ImGuiID WindowId      = 0;

    [[nodiscard]] ImGuiID ResolveId(std::initializer_list<const char*> Parts) const
    {
        ImGuiID Id = WindowId;
        for (const char* Part : Parts) Id = ImHashStr(Part, 0, Id);
        return Id;
    }

    // 📝 The id of a control inside a ChoiceRow: PushID(row), PushID(segment), "##Choice".
    [[nodiscard]] ImGuiID ResolveSegment(const char* Row, int Segment) const
    {
        ImGuiID Id = ImHashStr(Row, 0, WindowId);
        Id         = ImHashData(&Segment, sizeof(Segment), Id);
        return ImHashStr("##Choice", 0, Id);
    }

    void Construct(const std::function<void()>& ConstructPage, ImGuiID Focus = 0, bool Activate = false)
    {
        auto& Io       = ImGui::GetIO();
        Io.DisplaySize = { static_cast<float>(Width), static_cast<float>(Height) };
        Centre.PrepareInterface();
        ImGui::NewFrame();
        (void)Centre.Integrate(1.0 / 60.0);
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(Io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
        ImGui::Begin("FluidPlumeSession", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        ImGuiWindow* Window = ImGui::GetCurrentWindow();
        WindowId            = Window->ID;
        if (Focus != 0)
        {
            ImGui::FocusWindow(Window);
            ImGui::SetFocusID(Focus, Window);
            if (Activate)
            {
                GImGui->NavActivateId = GImGui->NavActivateDownId = GImGui->NavActivatePressedId = Focus;
                GImGui->NavInputSource                                                           = ImGuiInputSource_Keyboard;
            }
        }
        ConstructPage();
        ContentHeight = ImGui::GetCursorScreenPos().y;
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::Render();
        FrontierExhibit::AcknowledgeTextures();
    }

    void Press(const std::function<void()>& ConstructPage, ImGuiID Focus, ImGuiKey Key, bool Shift = false)
    {
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, Shift);
        ImGui::GetIO().AddKeyEvent(Key, true);
        Construct(ConstructPage, Focus);
        ImGui::GetIO().AddKeyEvent(Key, false);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
        Construct(ConstructPage, Focus);
    }

    // 📝 A control activated as the keyboard would: focused, then the activation held for one refresh.
    void Activate(const std::function<void()>& ConstructPage, ImGuiID Focus)
    {
        Construct(ConstructPage, Focus, true);
        Construct(ConstructPage, Focus);
    }

    // 📝 The page rasterized on the CPU to a PNG, sized to its content.
    void Capture(const std::function<void()>& ConstructPage, const std::filesystem::path& Filename)
    {
        Height = 6000;
        for (int Tick = 0; Tick < 3; ++Tick) Construct(ConstructPage);
        Height = std::clamp(static_cast<int>(std::ceil(ContentHeight)) + 8, 380, 6000);
        Construct(ConstructPage);
        Construct(ConstructPage);
        const ImDrawData*    Submitted = ImGui::GetDrawData();
        std::vector<unsigned char> Pixels(static_cast<size_t>(Width) * Height * 3u, 14u);
        for (const ImDrawList* List : Submitted->CmdLists)
        {
            for (const ImDrawVert& Vertex : List->VtxBuffer)
            {
                if (!std::isfinite(Vertex.pos.x) || !std::isfinite(Vertex.pos.y)) throw std::runtime_error("a non-finite UI vertex");
            }
            FrontierExhibit::RasterizeCommands(List, Pixels.data(), Width, Height, Submitted->DisplayPos, Submitted->FramebufferScale);
        }
        Verify(stbi_write_png(Filename.string().c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "Rendered " + Filename.filename().string());
        Height = 6000;
    }

  private:
    ControlCentrePanel& Centre;
};

using CardBuilder = void (*)(ControlColumn&, FluidPlumeContext&);

// 📝 The card a slider sits on, by the slider's place in SliderKeys.
CardBuilder ResolveCard(size_t SliderIndex)
{
    if (SliderIndex < 3) return FluidPlumePages::ConstructSourceCard;
    if (SliderIndex < 13) return FluidPlumePages::ConstructSimulationCard;
    if (SliderIndex < 20) return FluidPlumePages::ConstructAppearanceCard;
    return FluidPlumePages::ConstructFireCard;
}

void ValidateReadouts()
{
    // 📝 The grades the Render settings' quality tier selects, and the lines the Presets card prints (the prototype's wording: PlumeSpace.QueryDescription
    //    and SmokeSequence.QueryReadout).
    Verify(FluidPlumePages::FormatLatticeExtent(0) == "64\xC3\x97" "64\xC3\x97" "20", "Low reads 64 × 64 × 20");
    Verify(FluidPlumePages::FormatLatticeExtent(1) == "96\xC3\x97" "96\xC3\x97" "24", "Medium reads 96 × 96 × 24");
    Verify(FluidPlumePages::FormatLatticeExtent(2) == "128\xC3\x97" "128\xC3\x97" "32", "High reads 128 × 128 × 32");
    Verify(FluidPlumePages::FormatPlumeDescription(0) == "Slate 3c987f3 plume \xC2\xB7 Low \xC2\xB7 64\xC3\x97" "64\xC3\x97" "20 @ 40 cm", "The Low description line");
    Verify(FluidPlumePages::FormatPlumeDescription(1) == "Slate 3c987f3 plume \xC2\xB7 Medium \xC2\xB7 96\xC3\x97" "96\xC3\x97" "24 @ 30 cm", "The Medium description line");
    Verify(FluidPlumePages::FormatPlumeDescription(2) == "Slate 3c987f3 plume \xC2\xB7 High \xC2\xB7 128\xC3\x97" "128\xC3\x97" "32 @ 25 cm", "The High description line");
    Verify(FluidPlumePages::FormatPlumeCounters(nullptr) == "no solver attached", "Without a host the counters read `no solver attached`");
    FluidPlumeReadout Readout;
    Readout.Resident = true;
    Readout.Sources  = 6;
    Readout.Ticks    = 177;
    Verify(FluidPlumePages::FormatPlumeCounters(&Readout) == "sources 6 \xC2\xB7 steps 177 \xC2\xB7 resident", "A resident readout reads `sources 6 · steps 177 · resident`");
    Readout.Resident = false;
    Verify(FluidPlumePages::FormatPlumeCounters(&Readout) == "sources 6 \xC2\xB7 steps 177 \xC2\xB7 idle", "An idle readout reads `… · idle`");
}

void ValidateCards(InterfaceSession& Session)
{
    const GasParameters      Defaults = ConstructGasParameters();
    FluidPlumeConfiguration  Plume;
    std::vector<std::string> Requests;
    FluidPlumeContext        Context{ Plume, nullptr, Requests };
    const auto               PageOf = [&](CardBuilder Card) {
        return std::function<void()>([&, Card] {
            const ControlTheme    Theme = ControlTheme::ResolveNative();
            const EditorTypefaces Faces = EditorTypefaces::Resolve();
            ControlColumn         Column(Theme, Faces, 1.0f);
            Card(Column, Context);
            Column.Finalize();
        });
    };
    for (int Tick = 0; Tick < 3; ++Tick) Session.Construct(PageOf(FluidPlumePages::ConstructSourceCard));

    {
        // 📝 The layout the window plans on: the four columns of the plan at scale 1 on a 449 px column (one of four on 1920 px), their whole height
        //    with the page's spacing. Simulation, alone, is the tallest and sets TallestColumnHeight, which the window fits its page to.
        struct Column
        {
            const char*              Name;
            std::vector<CardBuilder> Cards;
        };
        const std::array<Column, 4> Plan = {{ { "Domain with Source", { FluidPlumePages::ConstructDomainCard, FluidPlumePages::ConstructSourceCard } },
                                              { "Simulation", { FluidPlumePages::ConstructSimulationCard } },
                                              { "Appearance", { FluidPlumePages::ConstructAppearanceCard } },
                                              { "Fire with Presets", { FluidPlumePages::ConstructFireCard, FluidPlumePages::ConstructPresetsCard } } }};
        float                       Heights[4] = {};
        Session.Width                          = 449;
        for (size_t Index = 0; Index < Plan.size(); ++Index)
        {
            const auto Page = [&] {
                const ControlTheme    Theme = ControlTheme::ResolveNative();
                const EditorTypefaces Faces = EditorTypefaces::Resolve();
                ControlColumn         Page(Theme, Faces, 1.0f);
                for (const CardBuilder Card : Plan[Index].Cards) Card(Page, Context);
                Page.Finalize();
            };
            Session.Construct(Page);
            Session.Construct(Page);
            Heights[Index] = Session.ContentHeight;
            Verify(Heights[Index] > 400.0f && Heights[Index] <= FluidPlumePages::TallestColumnHeight,
                   std::string(Plan[Index].Name) + " column is " + std::to_string(Heights[Index]) + " px at scale 1, within TallestColumnHeight");
        }
        Session.Width = 440;
        Verify(Heights[1] >= FluidPlumePages::TallestColumnHeight - 16.0f && Heights[1] == *std::max_element(Heights, Heights + 4),
               "Simulation is the tallest column, and TallestColumnHeight is tight to it");
    }

    // 📝 Every slider: Right moves one step, Left returns, Shift-Right moves ten, End reaches the maximum and Home the minimum — each to the decimal the
    //    prototype commits, and touching no other reading.
    size_t Sliders = 0;
    for (size_t Place = 0; Place < SliderKeys.size(); ++Place)
    {
        const std::string_view   Key   = SliderKeys[Place];
        const GasParameterEntry* Entry = FindGasParameter(Key);
        Verify(Entry != nullptr, "The catalogue has " + std::string(Key));
        if (Entry == nullptr) continue;
        const std::function<void()> Page = PageOf(ResolveCard(Place));
        const std::string           Id   = "PlumeGas" + std::string(Key);
        Plume.Gas                        = Defaults;
        Session.Construct(Page);
        const ImGuiID             Track = Session.ResolveId({ Id.c_str(), "##Track" });
        const std::vector<double> Was   = Flatten(Defaults);
        const size_t              Own   = IndexOf(Key);
        const auto OnlyOwnMoved = [&] {
            const std::vector<double> Now = Flatten(Plume.Gas);
            for (size_t Index = 0; Index < Now.size(); ++Index)
            {
                if (Index != Own && Now[Index] != Was[Index]) return false;
            }
            return true;
        };
        const double   Start    = Defaults.*(Entry->Field);
        const uint64_t Revision = Plume.Revision;

        Session.Press(Page, Track, ImGuiKey_RightArrow);
        const double Up = Plume.Gas.*(Entry->Field);
        Verify(Up == Committed(std::min(Start + Entry->Step, Entry->Maximum), *Entry) && Up != Start, std::string(Key) + ": Right moves one step to the decimal the figure shows");
        Verify(OnlyOwnMoved() && Plume.Revision > Revision, std::string(Key) + ": only its own reading moved, and the record's revision advanced");
        Session.Press(Page, Track, ImGuiKey_LeftArrow);
        Verify(Plume.Gas.*(Entry->Field) == Start, std::string(Key) + ": Left returns to the default exactly");
        Session.Press(Page, Track, ImGuiKey_RightArrow, true);
        Verify(Plume.Gas.*(Entry->Field) == Committed(std::min(Start + 10.0 * Entry->Step, Entry->Maximum), *Entry), std::string(Key) + ": Shift-Right moves ten steps");
        Session.Press(Page, Track, ImGuiKey_End);
        Verify(Plume.Gas.*(Entry->Field) == Entry->Maximum && OnlyOwnMoved(), std::string(Key) + ": End reaches the maximum (" + std::to_string(Entry->Maximum) + ")");
        Session.Press(Page, Track, ImGuiKey_Home);
        Verify(Plume.Gas.*(Entry->Field) == Entry->Minimum && OnlyOwnMoved(), std::string(Key) + ": Home reaches the minimum (" + std::to_string(Entry->Minimum) + ")");
        ++Sliders;
    }
    Verify(Sliders == 26, "Twenty-six sliders were driven");

    // 📝 The presets, Defaults and Clear smoke. Every reading is first set to its maximum, so that a preset's readings that are not overrides must
    //    come back to the defaults (the prototype assigns over ConstructGasParameters()).
    const auto&                 Presets = QueryGasPresets();
    const std::function<void()> DomainPage = PageOf(FluidPlumePages::ConstructDomainCard), PresetPage = PageOf(FluidPlumePages::ConstructPresetsCard);
    for (size_t Index = 0; Index < Presets.size(); ++Index)
    {
        for (const GasParameterEntry& Entry : QueryGasParameterCatalogue()) Plume.Gas.*(Entry.Field) = Entry.Maximum;
        Session.Construct(PresetPage);
        const uint64_t    Revision = Plume.Revision;
        const std::string Button   = "PlumePreset" + std::to_string(Index);
        Session.Activate(PresetPage, Session.ResolveId({ Button.c_str() }));
        Verify(Flatten(Plume.Gas) == Flatten(ApplyGasPreset(Presets[Index])), "Preset " + std::string(Presets[Index].Name) + " sets exactly the prototype's gas");
        Verify(Plume.Revision > Revision, "Preset " + std::string(Presets[Index].Name) + " advances the record's revision");
    }
    {
        for (const GasParameterEntry& Entry : QueryGasParameterCatalogue()) Plume.Gas.*(Entry.Field) = Entry.Minimum;
        Session.Construct(DomainPage);
        Session.Activate(DomainPage, Session.ResolveId({ "PlumeDefaults" }));
        Verify(Flatten(Plume.Gas) == Flatten(Defaults), "Defaults restores all 33 readings");
        Verify(Requests.empty(), "Defaults raises no host request");
        Session.Activate(DomainPage, Session.ResolveId({ "PlumeClearSmoke" }));
        Verify(Requests.size() == 1 && Requests[0] == "clear-smoke", "Clear smoke raises the host's clear-smoke request");
        Verify(Flatten(Plume.Gas) == Flatten(Defaults), "Clear smoke leaves the readings alone");

        const uint64_t Revision = Plume.Revision;
        Session.Activate(DomainPage, Session.ResolveSegment("PlumeTestEmitter", 1));
        Verify(Plume.TestEmitter == 1, "Test emitter: Smoke plume");
        Session.Activate(DomainPage, Session.ResolveSegment("PlumeTestEmitter", 2));
        Verify(Plume.TestEmitter == 2, "Test emitter: Fire");
        Session.Activate(DomainPage, Session.ResolveSegment("PlumeTestEmitter", 0));
        Verify(Plume.TestEmitter == 0, "Test emitter: Off");
        Verify(Plume.Revision == Revision, "The test emitter is not an authored reading: the revision stays");
    }
    {
        // 📝 The Ignition row belongs to the Vehicle record; here it is shown disabled and must not open or change anything.
        const std::function<void()> FirePage = PageOf(FluidPlumePages::ConstructFireCard);
        Session.Construct(FirePage);
        const std::vector<double> Before = Flatten(Plume.Gas);
        Session.Activate(FirePage, Session.ResolveId({ "PlumeIgnition", "##Select" }));
        Verify(GImGui->OpenPopupStack.Size == 0 && Flatten(Plume.Gas) == Before, "The Ignition select is inert: no popup opens, no reading moves");
    }

    // 📝 The two instruments in what a card submits: the decay curves on the Simulation card and the blackbody strip on the Fire card, for a preset
    //    that moves both (Inferno: faster cooling, a hotter flame).
    Plume.Gas = ApplyGasPreset(PresetNamed("Inferno"));
    Session.Construct(PageOf(FluidPlumePages::ConstructSimulationCard));
    Verify(FindCurves(ImGui::GetDrawData(), Plume.Gas).Found, "The Simulation card submits the decay instrument's grid and both curves, in the prototype's colours and order");
    Session.Construct(PageOf(FluidPlumePages::ConstructFireCard));
    const InkReading Strip = FindStrip(ImGui::GetDrawData(), Plume.Gas);
    Verify(Strip.Found && Strip.Right - Strip.Left > 300.0f, "The Fire card submits the blackbody strip's bands in order across the card");
    Plume.Gas = Defaults;
    Session.Construct(PageOf(FluidPlumePages::ConstructFireCard));
    Verify(!FindStrip(ImGui::GetDrawData(), ApplyGasPreset(PresetNamed("Inferno"))).Found, "The strip is not found for another flame: its colours follow the readings");
    std::printf("[FluidPlumeEditorValidation] cards: %zu sliders, %zu presets, Defaults, Clear smoke, Test emitter, Ignition, both instruments\n", Sliders, Presets.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                          ③ THE WINDOW IN THE EVEREST WORKSPACE
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the host's solver reads at a refresh of the burnout script: the C++ plume's CPU half run from the start, as the host's SmokeSequence.
FluidPlumeReadout ReadSolver(int Refreshes, const char* Grade, uint32_t GradeIndex)
{
    using namespace PlumeScenario;
    Frontier::PlumeSimulation::SmokeSequence Smoke(Grade);
    Mulberry32                               Noise{ 0x51ED270Bu };
    for (int Step = 0; Step < Refreshes; ++Step)
    {
        std::array<double, 8> Random{};
        for (double& Value : Random) Value = Noise();
        const BurnoutRefresh                         Refresh = ConstructBurnout(Step, Random);
        Frontier::PlumeSimulation::SmokeAdvanceInput Input;
        Input.Enabled        = Refresh.Enabled;
        Input.Vehicle        = &Refresh.Vehicle;
        Input.LightDirection = LightDirection;
        Input.Gas            = &Refresh.Gas;
        Input.Readings       = &Refresh.Readings;
        Input.Thermal        = &Refresh.Thermal;
        Input.Emissions      = &Refresh.Emissions;
        Smoke.Advance(Refresh.Seconds, Input);
        // 📝 The device half runs a pending step and completes it; here nothing runs, but the schedule's count must be the host's.
        if (Smoke.QuerySchedule().QueryStepPending()) Smoke.AccessSchedule().CompleteStep();
    }
    FluidPlumeReadout Readout;
    Readout.Grade       = GradeIndex;
    Readout.Resident    = Smoke.QuerySchedule().QueryResident();
    Readout.Sources     = Smoke.QueryMetrics().Sources;
    Readout.Ticks       = Smoke.QueryMetrics().Ticks;
    Readout.Relocations = Smoke.QueryMetrics().Relocations;
    return Readout;
}

struct WindowSession
{
    ControlCentrePanel*        Centre    = nullptr;
    Everest::EverestWorkspace* Workspace = nullptr;
    int                        Width     = 1920;
    int                        Height    = 1080;
    std::function<void()>      AfterNewFrame;   // [-] the keyboard's part of a refresh, between NewFrame and the workspace
    ImVec2                     Pointer{ -FLT_MAX, -FLT_MAX };   // [px] where the pointer rests; off the window unless a wheel turns

    void Step()
    {
        ImGuiIO& Io    = ImGui::GetIO();
        Io.DisplaySize = ImVec2(static_cast<float>(Width), static_cast<float>(Height));
        Io.MousePos    = Pointer;
        Centre->PrepareInterface();
        ImGui::NewFrame();
        if (AfterNewFrame) AfterNewFrame();
        (void)Centre->Integrate(1.0 / 30.0);
        (void)Workspace->ConfigurePlatform();
        Workspace->ConstructInterface(1.0 / 30.0);
        Centre->ConstructInterface();
        ImGui::Render();
        FrontierExhibit::AcknowledgeTextures();
    }

    void Tick(int Count)
    {
        for (int Index = 0; Index < Count; ++Index) Step();
    }

    // 📝 ImGui names a column's child "<popup>/<name>_<id>"; its id roots the ids of the controls inside.
    [[nodiscard]] ImGuiWindow* FindColumn(int Column) const
    {
        char Prefix[96];
        std::snprintf(Prefix, sizeof(Prefix), "##FluidPlumeEditorWindow/##FluidPlumeColumn%d_", Column);
        for (ImGuiWindow* Window : GImGui->Windows)
        {
            if ((Window->Active || Window->WasActive) && std::strncmp(Window->Name, Prefix, std::strlen(Prefix)) == 0) return Window;
        }
        return nullptr;
    }

    [[nodiscard]] ImGuiID ResolveItem(int Column, std::initializer_list<const char*> Parts) const
    {
        ImGuiWindow* Owner = FindColumn(Column);
        if (Owner == nullptr) throw std::runtime_error("the editor has no column " + std::to_string(Column));
        ImGuiID Id = Owner->ID;
        for (const char* Part : Parts) Id = ImHashStr(Part, 0, Id);
        return Id;
    }

    [[nodiscard]] ImGuiID ResolveSegment(int Column, const char* Row, int Segment) const
    {
        ImGuiWindow* Owner = FindColumn(Column);
        if (Owner == nullptr) throw std::runtime_error("the editor has no column " + std::to_string(Column));
        ImGuiID Id = ImHashStr(Row, 0, Owner->ID);
        Id         = ImHashData(&Segment, sizeof(Segment), Id);
        return ImHashStr("##Choice", 0, Id);
    }

    // 📝 A control used as the keyboard would use it, inside the open window: focused on a refresh in which its key is held (or, for a button, its
    //    activation), released on the next, then a refresh to settle.
    void Use(int Column, ImGuiID Item, ImGuiKey Key = ImGuiKey_None)
    {
        for (int Phase = 0; Phase < 3; ++Phase)
        {
            if (Key != ImGuiKey_None && Phase < 2) ImGui::GetIO().AddKeyEvent(Key, Phase == 0);
            AfterNewFrame = [this, Column, Item, Key, Phase] {
                ImGuiWindow* Owner = FindColumn(Column);
                if (Owner == nullptr || Phase == 2) return;
                ImGui::FocusWindow(Owner);
                ImGui::SetFocusID(Item, Owner);
                if (Key == ImGuiKey_None && Phase == 0)
                {
                    GImGui->NavActivateId = GImGui->NavActivateDownId = GImGui->NavActivatePressedId = Item;
                    GImGui->NavInputSource                                                           = ImGuiInputSource_Keyboard;
                }
            };
            Step();
        }
        AfterNewFrame = nullptr;
    }

    // 📝 The wheel turned Notches clicks over a column: the pointer rests on it, the column scrolls, the pointer leaves again.
    void Scroll(int Column, int Notches)
    {
        ImGuiWindow* Owner = FindColumn(Column);
        if (Owner == nullptr) throw std::runtime_error("the editor has no column " + std::to_string(Column));
        Pointer = ImVec2(Owner->Pos.x + 0.5f * Owner->Size.x, Owner->Pos.y + 0.5f * Owner->Size.y);
        Step();
        for (int Index = 0; Index < Notches; ++Index)
        {
            ImGui::GetIO().AddMouseWheelEvent(0.0f, Notches > 0 ? -1.0f : 1.0f);
            Step();
        }
        Pointer = ImVec2(-FLT_MAX, -FLT_MAX);
        Tick(14);   // [-] the column eases its offset back to its own end after the last click
    }

    // 📝 Whether an instrument lies wholly inside one column's visible rectangle, as a user would see it.
    [[nodiscard]] bool QueryVisible(const InkReading& Ink) const
    {
        for (int Column = 0; Column < 4; ++Column)
        {
            const ImGuiWindow* Owner = FindColumn(Column);
            if (Owner == nullptr) continue;
            if (Ink.Left >= Owner->Pos.x && Ink.Right <= Owner->Pos.x + Owner->Size.x && Ink.Top >= Owner->Pos.y && Ink.Bottom <= Owner->Pos.y + Owner->Size.y) return true;
        }
        return false;
    }

    // 📝 A capture is of the window at rest: the keyboard focus ring of the last control used is hidden, as a mouse click hides it.
    void Settle()
    {
        AfterNewFrame = [] { ImGui::SetNavCursorVisible(false); };
        Tick(2);
        AfterNewFrame = nullptr;
    }

    // 📝 The popup's own button (Close Editor).
    void UseWindowControl(const char* Name)
    {
        ImGuiWindow* Popup = ImGui::FindWindowByName("##FluidPlumeEditorWindow");
        if (Popup == nullptr) throw std::runtime_error("the editor window is not open");
        const ImGuiID Item = ImHashStr(Name, 0, Popup->ID);
        for (int Phase = 0; Phase < 2; ++Phase)
        {
            AfterNewFrame = [this, Popup, Item, Phase] {
                if (Phase != 0) return;
                ImGui::FocusWindow(Popup);
                ImGui::SetFocusID(Item, Popup);
                GImGui->NavActivateId = GImGui->NavActivateDownId = GImGui->NavActivatePressedId = Item;
                GImGui->NavInputSource                                                           = ImGuiInputSource_Keyboard;
            };
            Step();
        }
        AfterNewFrame = nullptr;
    }

    [[nodiscard]] std::vector<unsigned char> Rasterize() const
    {
        const ImDrawData*          Submitted = ImGui::GetDrawData();
        std::vector<unsigned char> Pixels(static_cast<size_t>(Width) * Height * 3u, 14u);
        for (const ImDrawList* List : Submitted->CmdLists)
        {
            for (const ImDrawVert& Vertex : List->VtxBuffer)
            {
                if (!std::isfinite(Vertex.pos.x) || !std::isfinite(Vertex.pos.y)) throw std::runtime_error("a non-finite UI vertex");
            }
            FrontierExhibit::RasterizeCommands(List, Pixels.data(), Width, Height, Submitted->DisplayPos, Submitted->FramebufferScale);
        }
        return Pixels;
    }
};

void ValidateWindow(ControlCentrePanel& Centre, const std::filesystem::path& Everest, const std::filesystem::path& Output)
{
    Everest::EverestWorkspace Workspace(Centre, Everest);
    WindowSession             Session;
    Session.Centre    = &Centre;
    Session.Workspace = &Workspace;

    // 📝 A FluidPlume record as the Construct dialog adds one, selected, and its editor opened as the Inspector's Plume Editor button opens it.
    WorldSpace&       World = Workspace.QueryWorldSpace();
    const RecordToken Token = World.ConstructRecord("FluidPlume", "Smoke and fire");
    Verify(Token.IsValid(), "The workspace constructs a FluidPlume record");
    Workspace.QueryOutliner().Select(Token);
    FluidPlumeConfiguration& Plume = AccessPlume(World, Token).AccessConfiguration();
    FluidPlumeEditor::RevokeReadout();
    Verify(InspectorRuntime::Instance().RequestEditorOpen(Token), "The FluidPlume record has an editor");
    Session.Tick(3);
    Verify(InspectorRuntime::Instance().QueryActiveEditorToken() == Token, "The Smoke & fire editor is the active editor");
    Verify(ImGui::FindWindowByName("##FluidPlumeEditorWindow") != nullptr && Session.FindColumn(3) != nullptr, "The editor is a modal window with four columns on 1920 px");

    const auto WriteWindow = [&](const std::string& Name, const char* Claim) {
        Session.Settle();
        const std::vector<unsigned char> Pixels = Session.Rasterize();
        const std::filesystem::path      Target = Output / (Name + ".png");
        Verify(stbi_write_png(Target.string().c_str(), Session.Width, Session.Height, 3, Pixels.data(), Session.Width * 3) != 0, std::string(Claim) + " (" + Target.filename().string() + ")");
        return Pixels;
    };
    // 📝 Both instruments in what the window submits, bands and strokes in order; Visible says whether each must lie within a column's visible rectangle
    //    (on the wide window every card is in view; on the narrow one the Fire card is below the fold until the column is scrolled).
    const auto ExpectInstruments = [&](const std::string& Name, bool StripVisible = true) {
        const InkReading Strip  = FindStrip(ImGui::GetDrawData(), Plume.Gas);
        const InkReading Curves = FindCurves(ImGui::GetDrawData(), Plume.Gas);
        Verify(Strip.Found && Strip.Right - Strip.Left > 250.0f, Name + ": the Fire card's blackbody strip is submitted, bands in order");
        Verify(Strip.Found && Session.QueryVisible(Strip) == StripVisible, Name + ": the strip is " + (StripVisible ? "in view" : "below the fold"));
        Verify(Curves.Found && Session.QueryVisible(Curves), Name + ": the Simulation card's decay curves are submitted, grid then density then heat, and in view");
    };

    // ① The window as it opens: the defaults, no solver attached; four columns on 1920 px.
    Session.Tick(3);
    const std::vector<unsigned char> Opening = WriteWindow("SmokeAndFireEditor-Opening", "The editor as it opens, no solver attached");
    ExpectInstruments("Opening");
    for (int Column = 0; Column < 4; ++Column)
        Verify(Session.FindColumn(Column) != nullptr && Session.FindColumn(Column)->ScrollMax.y <= 0.5f, "Opening: column " + std::to_string(Column) + " holds its cards without scrolling");
    Verify(FluidPlumeEditor::QueryReadout() == nullptr, "Opening: no readout is attached, so the cards say `no solver`");
    Verify(Flatten(Plume.Gas) == Flatten(ConstructGasParameters()), "Opening: the record holds the defaults");

    // ② The Inferno preset by its button inside the window, the Fire test emitter by its segment, and the live plume's readout (the C++ plume's CPU
    //    half at refresh 230 of the burnout, the Inferno burst).
    const FluidPlumeReadout Inferno = ReadSolver(230, "high", 2);
    Verify(Inferno.Resident && Inferno.Sources > 0 && Inferno.Ticks > 0,
           "The plume's CPU half is resident with sources and steps at refresh 230 (resident " + std::to_string(Inferno.Resident) + ", sources " + std::to_string(Inferno.Sources) +
               ", steps " + std::to_string(Inferno.Ticks) + ", relocations " + std::to_string(Inferno.Relocations) + ")");
    FluidPlumeEditor::AssignReadout(Inferno);
    Verify(FluidPlumeEditor::QueryReadout() != nullptr && FluidPlumeEditor::QueryReadout()->Ticks == Inferno.Ticks, "The host's readout reaches the editor");
    Session.Use(3, Session.ResolveItem(3, { "PlumePreset3" }));
    Verify(Flatten(Plume.Gas) == Flatten(ApplyGasPreset(PresetNamed("Inferno"))), "The Inferno button, pressed in the window, sets the prototype's Inferno gas");
    Session.Use(0, Session.ResolveSegment(0, "PlumeTestEmitter", 2));
    Verify(Plume.TestEmitter == 2, "The Fire segment of the Test emitter, pressed in the window, selects the fire");
    Session.Tick(3);
    const std::vector<unsigned char> InfernoWindow = WriteWindow("SmokeAndFireEditor-Inferno-live", "The Inferno preset with the live plume's readout");
    ExpectInstruments("Inferno");
    Verify(InfernoWindow != Opening, "The Inferno window differs from the opening window");

    // ③ A slider in the window: Emission, one step right, to the decimal the figure shows.
    const GasParameterEntry& Emission = *FindGasParameter("Emission");
    const double             Before   = Plume.Gas.Emission;
    Session.Use(0, Session.ResolveItem(0, { "PlumeGasEmission", "##Track" }), ImGuiKey_RightArrow);
    Verify(Plume.Gas.Emission == Committed(std::min(Before + Emission.Step, Emission.Maximum), Emission) && Plume.Gas.Emission != Before,
           "Emission, moved one step in the window, commits the decimal the figure shows");

    // ④ The Crosswind preset on the readout of refresh 300.
    FluidPlumeEditor::AssignReadout(ReadSolver(300, "high", 2));
    Session.Use(3, Session.ResolveItem(3, { "PlumePreset4" }));
    Verify(Flatten(Plume.Gas) == Flatten(ApplyGasPreset(PresetNamed("Crosswind"))), "The Crosswind button sets the prototype's Crosswind gas");
    Session.Use(0, Session.ResolveSegment(0, "PlumeTestEmitter", 0));
    Verify(Plume.TestEmitter == 0, "The Off segment ends the test emitter");
    Session.Tick(3);
    WriteWindow("SmokeAndFireEditor-Crosswind-live", "The Crosswind preset with the live plume's readout");
    ExpectInstruments("Crosswind");

    // ⑤ Clear smoke reaches the host as a request, once.
    Verify(FluidPlumeEditor::TakeRequests().empty(), "No request is pending before Clear smoke");
    Session.Use(0, Session.ResolveItem(0, { "PlumeClearSmoke" }));
    const std::vector<std::string> Requests = FluidPlumeEditor::TakeRequests();
    Verify(Requests.size() == 1 && Requests[0] == "clear-smoke", "Clear smoke in the window reaches the host as `clear-smoke`");
    Verify(FluidPlumeEditor::TakeRequests().empty(), "The host takes a request once");

    // ⑥ Defaults, then a narrower window (three columns) on the Medium grade: every card still has its place.
    Session.Use(0, Session.ResolveItem(0, { "PlumeDefaults" }));
    Verify(Flatten(Plume.Gas) == Flatten(ConstructGasParameters()), "Defaults, pressed in the window, restores the 33 readings");
    FluidPlumeEditor::AssignReadout(ReadSolver(120, "medium", 1));
    Session.Width  = 1366;
    Session.Height = 800;
    Session.Tick(4);
    Verify(Session.FindColumn(2) != nullptr && Session.FindColumn(3) == nullptr, "On 1366 px the editor has three columns");
    WriteWindow("SmokeAndFireEditor-Narrow-medium", "The editor on a 1366 px window at the Medium grade");
    ExpectInstruments("Narrow", false);

    // 📝 The third column (Appearance, then Fire) outgrows its window: it starts at the top and the wheel scrolls it to the end, which brings the Fire
    //    card and its strip into view.
    {
        const ImGuiWindow* Last = Session.FindColumn(2);
        Verify(Last != nullptr && Last->ScrollMax.y > 100.0f && Last->Scroll.y == 0.0f, "Narrow: the third column is taller than its window and rests at its top");
        Session.Scroll(2, 60);
        Last = Session.FindColumn(2);
        Verify(Last != nullptr && Last->Scroll.y > 100.0f && Last->Scroll.y >= Last->ScrollMax.y - 1.0f,
               "Narrow: the wheel scrolls it to its end (" + std::to_string(Last != nullptr ? Last->Scroll.y : -1.0f) + " of " + std::to_string(Last != nullptr ? Last->ScrollMax.y : -1.0f) + " px)");
        WriteWindow("SmokeAndFireEditor-Narrow-medium-scrolled", "The third column scrolled to its end: the Fire card and Presets");
        ExpectInstruments("Narrow, scrolled");
    }

    // ⑦ Close Editor returns to the workspace, which shows the record's own page.
    Session.Width  = 1920;
    Session.Height = 1080;
    Plume.Gas      = ApplyGasPreset(PresetNamed("Inferno"));
    Plume.AdvanceRevision();
    FluidPlumeEditor::AssignReadout(Inferno);
    Session.Tick(2);
    Session.UseWindowControl("##CloseFluidPlumeEditor");
    Session.Tick(2);
    Verify(!InspectorRuntime::Instance().QueryActiveEditorToken().IsValid(), "Close Editor returns to the workspace");
    Verify(Session.FindColumn(0) == nullptr && GImGui->OpenPopupStack.Size == 0, "The editor window is gone: no column is submitted and no popup is open");
    const std::vector<unsigned char> Workspace1080 = WriteWindow("Workspace-FluidPlume-selected", "The workspace with the FluidPlume record selected: its page");
    Verify(Workspace1080 != Opening, "The workspace with the record selected differs from the open editor");
    FluidPlumeEditor::RevokeReadout();
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    try
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        if (ArgumentCount != 4) throw std::runtime_error("usage: FluidPlumeEditorValidation <EngineContent> <Projects/Everest> <output folder>");
        const std::filesystem::path Content = std::filesystem::absolute(Arguments[1]);
        const std::filesystem::path Everest = std::filesystem::absolute(Arguments[2]);
        const std::filesystem::path Output  = std::filesystem::absolute(Arguments[3]);
        const std::filesystem::path Scratch = Output / "Scratch";
        std::filesystem::create_directories(Scratch);
        Report.open(Output / "Validation.tsv");
        const std::filesystem::path Fixture = std::filesystem::current_path() / "Exhibits/EverestInterface/Content/Simulation.levelspace";
        if (!std::filesystem::exists(Fixture)) throw std::runtime_error("run from the repository root: " + Fixture.string() + " is missing");

        ValidateRecord(Scratch, Fixture);
        ValidateReadouts();

        ImGui::CreateContext();
        ImGuiIO& Io    = ImGui::GetIO();
        Io.IniFilename = nullptr;
        Io.DeltaTime   = 1.0f / 30.0f;
        Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(Content / "Icons");
        {
            const std::filesystem::path Preferences = Scratch / "Personal.projectspace";
            std::filesystem::remove(Preferences);
            ActivationOwnership Ownership(new ControlCentrePanel(Content / "FontArchives", Preferences, Everest / "Everest.projectspace"));
            auto&               Centre = *static_cast<ControlCentrePanel*>(Ownership.get());
            if (!Centre.Initialize() || !Centre.Activate()) throw std::runtime_error("Control Centre initialization failed");
            {
                InterfaceSession Session(Centre);
                for (int Tick = 0; Tick < 3; ++Tick) Session.Construct([] {});
                ValidateCards(Session);

                // 📝 The compact page of the record's inspector, as a rendered image: the figures at a glance.
                FluidPlumeConfiguration Plume;
                Plume.Gas = ApplyGasPreset(PresetNamed("Inferno"));
                FluidPlumeInspector Inspector(Plume);
                FluidPlumeEditor::AssignReadout(ReadSolver(230, "high", 2));
                Session.Capture([&] { Inspector.ConstructInspector(); }, Output / "FluidPlumeInspector-Panel.png");
                FluidPlumeEditor::RevokeReadout();
            }
            ValidateWindow(Centre, Everest, Output);
        }
        ImGui::DestroyContext();
        Report << "SUMMARY\t" << Assertions << " assertions\t" << Failures << " failures\n";
        std::cout << Assertions << " assertions; " << Failures << " failures\n";
        return Failures != 0 ? 1 : 0;
    }
    catch (const std::exception& Failure)
    {
        std::cerr << "[FluidPlumeEditorValidation] " << Failure.what() << '\n';
        Report << "ERROR\t" << Failure.what() << '\n';
        return 2;
    }
}
