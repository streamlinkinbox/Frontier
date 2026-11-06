/*==========================================================================================================================================
                                                   FLUIDPLUMEEDITORVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor, checked as a user meets it:
//      ① the FluidPlume record: its defaults, the 33 snake_case keys of its TOML, a save and reload bit for bit, range clamping, non-finite and missing
//        readings, and the legacy block of Simulation.levelspace;
//      ② the libraries (the six looks and what each changes, the groups, which look a gas holds) and every card driven through native input on a real
//        ImGui context: each of the 26 sliders by arrow, Shift-arrow, Home and End, the six looks, Defaults, Clear smoke, the Test emitter and the way
//        to the libraries — each must change exactly the readings it names, to the decimal the prototype's range input commits;
//      ③ the window in the Everest workspace, opened from a FluidPlume record the way the Inspector's button opens it and driven there with the
//        pointer and the keyboard: the Outliner's six rows and its eyes, the Inspector page each row shows, the library sheet (tabs, chips, search,
//        a look applied, Escape, a click outside), the viewport's bar and its orbit, and what the editor asks of a host's presenter (the gas by
//        pointer, the emitter, the camera, the clear, the stage's pixels). The host's readout comes from the C++ plume's own CPU half (SmokeSequence on
//        the burnout script). Captured at 1920 × 1080 and on a 1366 × 800 window; the two instruments are looked for in what the window submits.
//
//        FluidPlumeEditorValidation <EngineContent> <Projects/Everest> <output folder>
//
// 📝 Run from the repository root: the legacy fixture is Exhibits/EverestInterface/Content/Simulation.levelspace. The presenter here is a stand-in
//    that returns a flat picture: the plume's own pixels come from the editor application's presenter (Exhibits/FluidPlumeEditor/Code/FluidPlumeEditorCapture.cpp).

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumeEditorWorkspace.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumeLibrary.h"
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
#include <chrono>
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

const FluidDynamics::GasPreset& PresetNamed(std::string_view Name)
{
    for (const FluidDynamics::GasPreset& Preset : QueryGasPresets())
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

// 📝 The libraries as the window's sheet lists them. The expected figures are written out here from the prototype's GasPresets, not derived from the
//    code under test: a look's readings are its overrides, less any that equal the default.
void ValidateLibrary()
{
    const GasParameters Defaults = ConstructGasParameters();
    const auto&         Looks    = FluidPlumeLibrary::QueryLooks();
    Verify(Looks.size() == 6, "The library lists the prototype's six looks");

    struct Expected
    {
        const char* Name;
        const char* Identifier;
        const char* Group;
        uint32_t    Differences;   // [-] readings that differ from the defaults
    };
    const std::array<Expected, 6> Table = {{ { "Tyre smoke", "tyre-smoke", "Smoke", 0 },
                                             { "Thick burnout", "thick-burnout", "Smoke", 7 },
                                             { "Light haze", "light-haze", "Smoke", 6 },
                                             { "Inferno", "inferno", "Fire", 7 },
                                             { "Crosswind", "crosswind", "Wind", 4 },
                                             { "Legacy billows", "legacy-billows", "Smoke", 13 } }};
    for (size_t Index = 0; Index < Table.size() && Index < Looks.size(); ++Index)
    {
        const FluidPlumeLook& Look = Looks[Index];
        const std::string     Name = Table[Index].Name;
        Verify(Look.Name == Name && Look.Identifier == Table[Index].Identifier, Name + ": the prototype's key and the verb's identifier `" + Table[Index].Identifier + "`");
        Verify(Look.Group == Table[Index].Group, Name + ": it lies in the " + Table[Index].Group + " group");
        Verify(Flatten(Look.Values) == Flatten(ApplyGasPreset(PresetNamed(Name))), Name + ": the values are the prototype's, applied over the defaults");
        Verify(Look.Differences == Table[Index].Differences, Name + ": " + std::to_string(Table[Index].Differences) + " readings differ from the defaults (observed " + std::to_string(Look.Differences) + ")");
        Verify(Look.Differences == CountDifferences(Flatten(Look.Values), Flatten(Defaults)), Name + ": its count agrees with a comparison of the 33 readings");
        Verify(Look.Changes.empty() == (Look.Differences == 0), Name + ": it lists what it changes exactly when it changes something");
        Verify(!Look.Lead.empty() && FluidPlumeLibrary::FindLook(Table[Index].Identifier) == &Look, Name + ": it has its first sentence and is found by its identifier");
        Verify(FluidPlumeLibrary::MatchLook(Look.Values) == &Look, Name + ": a gas holding its readings matches it");
    }
    Verify(FluidPlumeLibrary::QueryGroups() == std::vector<std::string>({ "Smoke", "Fire", "Wind" }), "The groups, in order of first appearance, are Smoke, Fire, Wind");
    Verify(FluidPlumeLibrary::FindLook("no-such-look") == nullptr, "An unknown identifier finds no look");
    Verify(Looks.size() >= 5 && Looks[4].Changes == "Dissipation 0.28 1/s \xC2\xB7 Turbulence 0.70 m/s \xC2\xB7 Wind speed 5.5 m/s \xC2\xB7 Wind heading 95\xC2\xB0",
           "Crosswind lists its four readings in the catalogue's order, as their sliders show them");
    Verify(Looks.size() >= 2 && Looks[1].Changes.rfind("Emission rate 2.20 \xC3\x97 \xC2\xB7 Volume expansion 1.40 \xC3\x97", 0) == 0,
           "Thick burnout lists its readings by their catalogue labels, the multipliers with their sign");

    // 📝 Which look a gas holds: the defaults hold Tyre smoke, and a gas with any one reading moved by a step holds none.
    Verify(FluidPlumeLibrary::MatchLook(Defaults) == &Looks[0], "The defaults hold the Tyre smoke look");
    GasParameters Moved = ApplyGasPreset(PresetNamed("Inferno"));
    Moved.FireLight += 0.05;
    Verify(FluidPlumeLibrary::MatchLook(Moved) == nullptr, "Inferno with one reading moved by one step holds no look");
    Verify(FluidPlumeLibrary::CountDifferences(Moved, ApplyGasPreset(PresetNamed("Inferno"))) == 1, "That gas differs from Inferno in exactly one of the 33 readings");
    Verify(FluidPlumeLibrary::MatchLook(ApplyGasPreset(PresetNamed("Inferno"))) != nullptr, "Inferno held again, bit for bit, is matched");

    // 📝 One reading as its slider shows it.
    const auto Reading = [](const char* Key, double Value) { return FluidPlumeLibrary::FormatReading(*FindGasParameter(Key), Value); };
    Verify(Reading("Emission", 2.2) == "2.20 \xC3\x97", "Emission 2.2 reads `2.20 ×`");
    Verify(Reading("WindSpeed", 5.5) == "5.5 m/s", "Wind speed 5.5 reads `5.5 m/s`");
    Verify(Reading("WindHeading", 95.0) == "95\xC2\xB0", "Wind heading 95 reads `95°`");
    Verify(Reading("PressureSweeps", 16.0) == "16", "Pressure iterations, which have no unit, read `16`");
    Verify(Reading("FlameCore", 1800.0) == "1800 K", "Flame core 1800 reads `1800 K`");
    Verify(Reading("FlameLoss", 0.96) == "0.960 1/s", "Flame decay, whose step is 0.005, reads three decimals: `0.960 1/s`");
    std::printf("[FluidPlumeEditorValidation] library: %zu looks, %zu groups\n", Looks.size(), FluidPlumeLibrary::QueryGroups().size());
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
        // 📝 The six pages as the Inspector lays them: a column 402 px wide at scale 1 (the window's 404 px less its border). None is empty, and the Titled
        //    flag leaves a card's own heading out where the page's heading already names it.
        Session.Width = 402;
        for (size_t Index = 0; Index < FluidPlumePageCount; ++Index)
        {
            const FluidPlumePage Which = static_cast<FluidPlumePage>(Index);
            const auto           Page  = [&] {
                const ControlTheme    Theme = ControlTheme::ResolveNative();
                const EditorTypefaces Faces = EditorTypefaces::Resolve();
                ControlColumn         Column(Theme, Faces, 1.0f);
                FluidPlumeContext     PageContext{ Plume, nullptr, Requests };
                FluidPlumePages::ConstructWindowPage(Column, PageContext, Which);
                Verify(PageContext.Titled, std::string(FluidPlumePages::ResolveToken(Which)) + ": the page leaves the context's Titled flag as it found it");
                Column.Finalize();
            };
            Session.Construct(Page);
            Session.Construct(Page);
            Verify(Session.ContentHeight > 200.0f, std::string(FluidPlumePages::ResolveToken(Which)) + " page is " + std::to_string(Session.ContentHeight) + " px tall at scale 1");
            FluidPlumePage Round = FluidPlumePage::Fire;
            Verify(FluidPlumePages::ResolvePage(FluidPlumePages::ResolveToken(Which), Round) && Round == Which, std::string(FluidPlumePages::ResolveToken(Which)) + ": its token names it");
        }
        FluidPlumePage Unknown = FluidPlumePage::Domain;
        Verify(!FluidPlumePages::ResolvePage("Nothing", Unknown), "A token that names no page resolves to none");
        const auto Height = [&](bool Titled) {
            const auto Page = [&] {
                const ControlTheme    Theme = ControlTheme::ResolveNative();
                const EditorTypefaces Faces = EditorTypefaces::Resolve();
                ControlColumn         Column(Theme, Faces, 1.0f);
                FluidPlumeContext     PageContext{ Plume, nullptr, Requests };
                PageContext.Titled = Titled;
                FluidPlumePages::ConstructSourceCard(Column, PageContext);
                Column.Finalize();
            };
            Session.Construct(Page);
            Session.Construct(Page);
            return Session.ContentHeight;
        };
        const float Titled = Height(true), Untitled = Height(false);
        Verify(Titled > Untitled + 18.0f, "The Source card is " + std::to_string(Titled - Untitled) + " px taller with its own heading than without it");
        Session.Width = 440;
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

    // 📝 The looks, Defaults, Clear smoke, the Test emitter and the way to the libraries. Every reading is first set to its maximum, so that a look's
    //    readings that are not overrides must come back to the defaults (the prototype assigns over ConstructGasParameters()). Each edit leaves its notice
    //    for the Log Stream.
    const auto&                 Presets    = QueryGasPresets();
    const std::function<void()> DomainPage = PageOf(FluidPlumePages::ConstructDomainCard), LooksPage = PageOf(FluidPlumePages::ConstructLooksCard),
                                EmitterPage = PageOf(FluidPlumePages::ConstructEmitterCard);
    for (size_t Index = 0; Index < Presets.size(); ++Index)
    {
        for (const GasParameterEntry& Entry : QueryGasParameterCatalogue()) Plume.Gas.*(Entry.Field) = Entry.Maximum;
        Session.Construct(LooksPage);
        Requests.clear();
        const uint64_t    Revision = Plume.Revision;
        const std::string Button   = "PlumePreset" + std::to_string(Index);
        Session.Activate(LooksPage, Session.ResolveId({ Button.c_str() }));
        Verify(Flatten(Plume.Gas) == Flatten(ApplyGasPreset(Presets[Index])), "Look " + std::string(Presets[Index].Name) + " sets exactly the prototype's gas");
        Verify(Plume.Revision > Revision, "Look " + std::string(Presets[Index].Name) + " advances the record's revision");
        Verify(Requests.size() == 1 && Requests[0] == "noted:look:" + FluidPlumeLibrary::QueryLooks()[Index].Identifier,
               "Look " + std::string(Presets[Index].Name) + " leaves its notice `noted:look:" + FluidPlumeLibrary::QueryLooks()[Index].Identifier + "`");
        Verify(FluidPlumeLibrary::MatchLook(Plume.Gas) == &FluidPlumeLibrary::QueryLooks()[Index], "Look " + std::string(Presets[Index].Name) + " is the look the gas holds afterwards");
    }
    {
        Requests.clear();
        Session.Construct(LooksPage);
        Session.Activate(LooksPage, Session.ResolveId({ "PlumeOpenLibraries" }));
        Verify(Requests.size() == 1 && Requests[0] == "library:on", "Open the libraries raises `library:on`");

        for (const GasParameterEntry& Entry : QueryGasParameterCatalogue()) Plume.Gas.*(Entry.Field) = Entry.Minimum;
        Requests.clear();
        Session.Construct(DomainPage);
        Session.Activate(DomainPage, Session.ResolveId({ "PlumeDefaults" }));
        Verify(Flatten(Plume.Gas) == Flatten(Defaults), "Defaults restores all 33 readings");
        Verify(Requests.size() == 1 && Requests[0] == "noted:defaults", "Defaults leaves its notice and raises no host request");
        Requests.clear();
        Session.Activate(DomainPage, Session.ResolveId({ "PlumeClearSmoke" }));
        Verify(Requests.size() == 1 && Requests[0] == "clear-smoke", "Clear smoke raises the host's clear-smoke request");
        Verify(Flatten(Plume.Gas) == Flatten(Defaults), "Clear smoke leaves the readings alone");

        const uint64_t Revision = Plume.Revision;
        for (uint32_t Emitter : { 1u, 2u, 0u })
        {
            Requests.clear();
            Session.Activate(EmitterPage, Session.ResolveSegment("PlumeTestEmitter", static_cast<int>(Emitter)));
            Verify(Plume.TestEmitter == Emitter, std::string("Test emitter: ") + (Emitter == 0 ? "Off" : Emitter == 1 ? "Smoke plume" : "Fire"));
            Verify(Requests.size() == 1 && Requests[0] == "noted:emitter:" + std::to_string(Emitter), "The Test emitter's choice leaves its notice");
        }
        Verify(Plume.Revision == Revision, "The test emitter is not an authored reading: the revision stays");
    }
    {
        // 📝 The heading's switch on the Domain page turns the plume off and on, and the Test emitter page's turns the emitter; each also leaves a notice.
        const auto DomainWindow = [&] {
            const ControlTheme    Theme = ControlTheme::ResolveNative();
            const EditorTypefaces Faces = EditorTypefaces::Resolve();
            ControlColumn         Column(Theme, Faces, 1.0f);
            FluidPlumePages::ConstructWindowPage(Column, Context, FluidPlumePage::Domain);
            Column.Finalize();
        };
        const auto EmitterWindow = [&] {
            const ControlTheme    Theme = ControlTheme::ResolveNative();
            const EditorTypefaces Faces = EditorTypefaces::Resolve();
            ControlColumn         Column(Theme, Faces, 1.0f);
            FluidPlumePages::ConstructWindowPage(Column, Context, FluidPlumePage::Emitter);
            Column.Finalize();
        };
        Session.Construct(DomainWindow);
        Requests.clear();
        const uint64_t Revision = Plume.Revision;
        Session.Activate(DomainWindow, Session.ResolveId({ "PlumeDomainHeading", "##TitleAction" }));
        Verify(!Plume.Enabled && Plume.Revision > Revision && Requests.size() == 1 && Requests[0] == "noted:enabled:off", "The Domain page's switch disables the plume, advancing the revision");
        Session.Activate(DomainWindow, Session.ResolveId({ "PlumeDomainHeading", "##TitleAction" }));
        Verify(Plume.Enabled && Requests.back() == "noted:enabled:on", "The Domain page's switch enables it again");
        Session.Construct(EmitterWindow);
        Session.Activate(EmitterWindow, Session.ResolveId({ "PlumeEmitterHeading", "##TitleAction" }));
        Verify(Plume.TestEmitter == 1 && Requests.back() == "noted:emitter:1", "The Test emitter page's switch starts the smoke plume");
        Session.Activate(EmitterWindow, Session.ResolveId({ "PlumeEmitterHeading", "##TitleAction" }));
        Verify(Plume.TestEmitter == 0 && Requests.back() == "noted:emitter:0", "and stops it");
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
    std::printf("[FluidPlumeEditorValidation] cards: %zu sliders, %zu looks, Defaults, Clear smoke, Test emitter, the libraries' button, both heading switches, Ignition, both instruments\n", Sliders, Presets.size());
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

// 📝 The picture the stand-in presenter returns: a flat dark gradient with a faint grid. It is not a plume and is not meant to look like one; the
//    plume's own pixels come from the editor application's presenter.
constexpr uint64_t StandInTexture = 0x5EED0001u;
constexpr int      StandInWidth   = 480;
constexpr int      StandInHeight  = 270;

std::vector<unsigned char> ConstructStandIn()
{
    std::vector<unsigned char> Pixels(static_cast<size_t>(StandInWidth) * StandInHeight * 4u);
    for (int Y = 0; Y < StandInHeight; ++Y)
    {
        for (int X = 0; X < StandInWidth; ++X)
        {
            const float         T    = static_cast<float>(Y) / static_cast<float>(StandInHeight);
            const int           Grid = (X % 40 == 0 || Y % 40 == 0) ? 12 : 0;
            unsigned char*      Texel = &Pixels[(static_cast<size_t>(Y) * StandInWidth + X) * 4u];
            Texel[0] = static_cast<unsigned char>(22.0f + 30.0f * T + Grid);
            Texel[1] = static_cast<unsigned char>(26.0f + 34.0f * T + Grid);
            Texel[2] = static_cast<unsigned char>(38.0f + 40.0f * T + Grid);
            Texel[3] = 255;
        }
    }
    return Pixels;
}

// 📝 What a host's presenter does, as far as the editor can tell: it records every request it is given, advances its clock by the seconds the request
//    carries and answers with an image, an adapter and the readout of the C++ plume's CPU half.
struct StandInPresenter
{
    std::vector<PlumeViewportRequest> Requests;
    FluidPlumeReadout                 Readout;
    double                            Clock  = 0.0;   // [s] the interface seconds it was given
    uint32_t                          Clears = 0;     // [-] requests that asked to empty the lattices

    PlumeViewportImage operator()(const PlumeViewportRequest& Request)
    {
        const auto Begin = std::chrono::steady_clock::now();
        Requests.push_back(Request);
        Clock += Request.Seconds;
        if (Request.Clear) ++Clears;
        PlumeViewportImage Image;
        Image.Texture             = StandInTexture;
        Image.Extent              = Request.Extent;
        Image.Adapter             = "Validation stand-in presenter";
        Image.Readout             = Readout;
        Image.Solving             = true;
        Image.RefreshMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begin).count();
        return Image;
    }
};

struct WindowSession
{
    ControlCentrePanel*               Centre    = nullptr;
    Everest::EverestWorkspace*        Workspace = nullptr;
    int                               Width     = 1920;
    int                               Height    = 1080;
    std::function<void()>             AfterNewFrame;                      // [-] the keyboard's part of a refresh, between NewFrame and the workspace
    ImVec2                            Pointer{ -FLT_MAX, -FLT_MAX };      // [px] where the pointer rests; off the window unless it is being used
    const std::vector<unsigned char>* StandIn = nullptr;                  // [-] the pixels the stand-in presenter's texture stands for

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

    // 📝 ImGui names a child "<popup>/<name>_<id>"; its id roots the ids of the controls inside.
    [[nodiscard]] ImGuiWindow* FindChild(const char* Prefix) const
    {
        for (ImGuiWindow* Window : GImGui->Windows)
        {
            if ((Window->Active || Window->WasActive) && std::strncmp(Window->Name, Prefix, std::strlen(Prefix)) == 0) return Window;
        }
        return nullptr;
    }

    [[nodiscard]] ImGuiWindow* FindInspector() const { return FindChild("##FluidPlumeEditorWindow/##PlumeInspector_"); }

    [[nodiscard]] ImGuiID ResolveItem(std::initializer_list<const char*> Parts) const
    {
        ImGuiWindow* Owner = FindInspector();
        if (Owner == nullptr) throw std::runtime_error("the editor has no Inspector");
        ImGuiID Id = Owner->ID;
        for (const char* Part : Parts) Id = ImHashStr(Part, 0, Id);
        return Id;
    }

    [[nodiscard]] ImGuiID ResolveSegment(const char* Row, int Segment) const
    {
        ImGuiWindow* Owner = FindInspector();
        if (Owner == nullptr) throw std::runtime_error("the editor has no Inspector");
        ImGuiID Id = ImHashStr(Row, 0, Owner->ID);
        Id         = ImHashData(&Segment, sizeof(Segment), Id);
        return ImHashStr("##Choice", 0, Id);
    }

    // 📝 A control of the Inspector used as the keyboard would use it: focused on a refresh in which its key is held (or, for a button, its activation),
    //    released on the next, then a refresh to settle.
    void Use(ImGuiID Item, ImGuiKey Key = ImGuiKey_None)
    {
        for (int Phase = 0; Phase < 3; ++Phase)
        {
            if (Key != ImGuiKey_None && Phase < 2) ImGui::GetIO().AddKeyEvent(Key, Phase == 0);
            AfterNewFrame = [this, Item, Key, Phase] {
                ImGuiWindow* Owner = FindInspector();
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

    // 📝 The pointer used on the window: it arrives and hovers (a refresh), presses (a refresh), releases (a refresh) and leaves.
    void Click(ImVec2 At)
    {
        Pointer = At;
        Step();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        Step();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        Step();
        Pointer = ImVec2(-FLT_MAX, -FLT_MAX);
        Step();
    }

    [[nodiscard]] static ImVec2 ResolveMiddle(const FluidPlumeControlRectangle& Control)
    {
        return ImVec2((Control.Minimum.x + Control.Maximum.x) * 0.5f, (Control.Minimum.y + Control.Maximum.y) * 0.5f);
    }

    // 📝 A control the editor laid out, pressed where it lies on the screen.
    void Click(std::string_view Name)
    {
        const FluidPlumeControlRectangle* Control = FluidPlumeEditor::AccessWorkspace()->FindControl(Name);
        if (Control == nullptr) throw std::runtime_error("the editor has no control `" + std::string(Name) + "`");
        Click(ResolveMiddle(*Control));
    }

    // 📝 The pointer pressed at From, moved by Delta while held, and released.
    void Drag(ImVec2 From, ImVec2 Delta)
    {
        Pointer = From;
        Step();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        Step();
        Pointer = ImVec2(From.x + Delta.x, From.y + Delta.y);
        Step();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        Step();
        Pointer = ImVec2(-FLT_MAX, -FLT_MAX);
        Step();
    }

    // 📝 The wheel turned Notches clicks (positive: away from the user) with the pointer resting at At.
    void Wheel(ImVec2 At, int Notches)
    {
        Pointer = At;
        Step();
        for (int Index = 0; Index < std::abs(Notches); ++Index)
        {
            ImGui::GetIO().AddMouseWheelEvent(0.0f, Notches > 0 ? 1.0f : -1.0f);
            Step();
        }
        Pointer = ImVec2(-FLT_MAX, -FLT_MAX);
        Step();
    }

    // 📝 A key pressed and released, with Control held when asked.
    void Key(ImGuiKey Pressed, bool Control = false)
    {
        if (Control) ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
        ImGui::GetIO().AddKeyEvent(Pressed, true);
        Step();
        ImGui::GetIO().AddKeyEvent(Pressed, false);
        if (Control) ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
        Step();
    }

    void Type(const char* Text)
    {
        ImGui::GetIO().AddInputCharactersUTF8(Text);
        Step();
        Step();
    }

    // 📝 A capture is of the window at rest: the keyboard focus ring of the last control used is hidden, as a mouse click hides it.
    void Settle()
    {
        AfterNewFrame = [] { ImGui::SetNavCursorVisible(false); };
        Tick(2);
        AfterNewFrame = nullptr;
    }

    // 📝 A popup's own button as the keyboard uses it: the editor window's Close Editor, or the Construct dialog's Add to scene.
    void UseWindowControl(const char* Name, const char* PopupName = "##FluidPlumeEditorWindow")
    {
        ImGuiWindow* Popup = ImGui::FindWindowByName(PopupName);
        if (Popup == nullptr) throw std::runtime_error(std::string("the popup ") + PopupName + " is not open");
        const ImGuiID Item = ImHashStr(Name, 0, Popup->ID);
        for (int Phase = 0; Phase < 2; ++Phase)
        {
            AfterNewFrame = [Popup, Item, Phase] {
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
        const std::function<FrontierExhibit::Image(ImTextureID)> Textures = [this](ImTextureID Id) -> FrontierExhibit::Image {
            if (Id == StandInTexture && StandIn != nullptr) return { StandIn->data(), StandInWidth, StandInHeight, 4 };
            return {};
        };
        const ImDrawData*          Submitted = ImGui::GetDrawData();
        std::vector<unsigned char> Pixels(static_cast<size_t>(Width) * Height * 3u, 14u);
        for (const ImDrawList* List : Submitted->CmdLists)
        {
            for (const ImDrawVert& Vertex : List->VtxBuffer)
            {
                if (!std::isfinite(Vertex.pos.x) || !std::isfinite(Vertex.pos.y)) throw std::runtime_error("a non-finite UI vertex");
            }
            FrontierExhibit::RasterizeCommands(List, Pixels.data(), Width, Height, Submitted->DisplayPos, Submitted->FramebufferScale, nullptr, &Textures);
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
    const std::vector<unsigned char> StandIn = ConstructStandIn();
    Session.StandIn                          = &StandIn;
    InspectorRuntime& Runtime                = InspectorRuntime::Instance();
    Runtime.RegisterPlumeViewportPresenter({});
    FluidPlumeEditor::RevokeReadout();

    const auto WriteWindow = [&](const std::string& Name, const char* Claim) {
        Session.Settle();
        const std::vector<unsigned char> Pixels = Session.Rasterize();
        const std::filesystem::path      Target = Output / (Name + ".png");
        Verify(stbi_write_png(Target.string().c_str(), Session.Width, Session.Height, 3, Pixels.data(), Session.Width * 3) != 0, std::string(Claim) + " (" + Target.filename().string() + ")");
        return Pixels;
    };

    // 📝 The way in, taken as a person takes it and with nothing of this test's own: the level holds no FluidPlume, Ctrl+A opens the Construct dialog, the
    //    search for "plume" leaves one tile, the tile and "Add to scene" add the record, and the Outliner lists it, selected. Without this the Plume Editor
    //    button could never be pressed. No presenter is registered yet: the window opens as it does where there is no Vulkan device.
    WorldSpace&     World  = Workspace.QueryWorldSpace();
    ConstructPanel& Dialog = Workspace.QueryConstructDialog();
    const float     Scale  = Runtime.ResolveInterfaceScale();
    Session.Tick(3);
    Verify(World.FindFirstByType("FluidPlume") == nullptr, "The level holds no FluidPlume record to begin with");
    {
        size_t Tiles = 0;
        for (const ConstructCatalogueEntry& Entry : Dialog.QueryCatalogue())
        {
            if (Entry.Identifier != "FluidPlume") continue;
            ++Tiles;
            Verify(Entry.Caption == "Fluid Plume" && Entry.Group == "Weather" && Entry.Glyph == "Flame" && !Entry.Shape, "The Construct dialog's tile: Fluid Plume, a record type of the Weather group with the Flame glyph");
        }
        Verify(Tiles == 1, "The Construct dialog offers a Fluid Plume to add, once");
    }
    Verify(!Dialog.QueryOpen(), "The Construct dialog is shut");
    Session.Key(ImGuiKey_A, true);
    Session.Tick(3);
    Verify(Dialog.QueryOpen(), "Ctrl+A opens the Construct dialog");
    Session.Type("plume");
    Session.Tick(2);
    const ImGuiWindow* Matrix = Session.FindChild("##ConstructDialog/##ConstructResults");
    Verify(Matrix != nullptr, "The dialog lays out its matrix of tiles");
    if (Matrix == nullptr) throw std::runtime_error("the Construct dialog has no matrix");
    WriteWindow("SmokeAndFireEditor-Construct", "The Construct dialog with `plume` in its search: the Fluid Plume tile");
    Session.Click(ImVec2(Matrix->Pos.x + 40.0f * Scale, Matrix->Pos.y + 40.0f * Scale));
    Session.Tick(2);
    Session.UseWindowControl("##ConstructAdd", "##ConstructDialog");
    Session.Tick(4);
    Verify(!Dialog.QueryOpen(), "Add to scene closes the dialog");
    auto* Made = dynamic_cast<FluidPlumeRecord*>(World.FindFirstByType("FluidPlume"));
    Verify(Made != nullptr, "Add to scene adds a FluidPlume record to the level");
    if (Made == nullptr) throw std::runtime_error("no FluidPlume record was added");
    const RecordToken Token = Made->QueryToken();
    Verify(Made->QueryRecordName() == "Fluid Plume" && Made->QueryConfiguration().Enabled && Flatten(Made->QueryConfiguration().Gas) == Flatten(ConstructGasParameters()),
           "The record is named Fluid Plume, enabled, with every reading at its default");
    Verify(Workspace.QueryOutliner().QuerySelected() == Token, "The Outliner selects the new record");
    {
        bool Listed = false;
        for (const auto& Row : Workspace.QueryOutliner().QueryRows()) Listed = Listed || Row.Token == Token;
        Verify(Listed, "The Outliner lists the new record");
    }
    FluidPlumeConfiguration& Plume = AccessPlume(World, Token).AccessConfiguration();
    // 📝 The Inspector's Plume Editor button is `RequestEditorOpen` on the selected record (InspectorRuntime.cpp), which refuses a record without an editor.
    Verify(Runtime.RequestEditorOpen(Token), "The FluidPlume record has an editor");
    Session.Tick(4);
    Verify(Runtime.QueryActiveEditorToken() == Token, "The Smoke & fire editor is the active editor");
    FluidPlumeEditorWorkspace* Editor = FluidPlumeEditor::AccessWorkspace();
    Verify(Editor != nullptr && Editor->QueryRecord() == &Plume, "The window's workspace edits the record's own configuration, not a copy");
    if (Editor == nullptr) throw std::runtime_error("the Smoke & fire editor has no workspace");
    const auto LogHas = [&](std::string_view Category, std::string_view Words) {
        for (const FluidPlumeLogLine& Line : Editor->QueryLog())
        {
            if (Line.Category == Category && (Line.Text + " " + Line.Detail).find(Words) != std::string::npos) return true;
        }
        return false;
    };
    // 📝 The instruments in what the window submits, bands and strokes in order, each within the Inspector's visible rectangle: the decay curves belong to
    //    the Simulation page and the blackbody strip to the Fire page.
    const auto InsideInspector = [&](const InkReading& Ink) {
        const ImGuiWindow* Inspector = Session.FindInspector();
        return Inspector != nullptr && Ink.Left >= Inspector->Pos.x && Ink.Right <= Inspector->Pos.x + Inspector->Size.x && Ink.Top >= Inspector->Pos.y && Ink.Bottom <= Inspector->Pos.y + Inspector->Size.y;
    };
    const auto ExpectCurves = [&](const std::string& Name) {
        const InkReading Curves = FindCurves(ImGui::GetDrawData(), Plume.Gas);
        Verify(Curves.Found && InsideInspector(Curves), Name + ": the Simulation page's decay curves are submitted, grid then density then heat, in view");
    };
    const auto ExpectStrip = [&](const std::string& Name) {
        const InkReading Strip = FindStrip(ImGui::GetDrawData(), Plume.Gas);
        Verify(Strip.Found && Strip.Right - Strip.Left > 250.0f && InsideInspector(Strip), Name + ": the Fire page's blackbody strip is submitted, bands in order, in view");
    };

    // ① The window as it opens: Domain selected, the defaults, no solver and no device.
    constexpr const char* Tokens[FluidPlumePageCount] = { "Domain", "Sources", "Emitter", "Simulation", "Appearance", "Fire" };
    Verify(Runtime.QueryPlumeViewportPresenter() == nullptr, "Opening: no presenter is registered");
    ImGuiWindow* Popup = ImGui::FindWindowByName("##FluidPlumeEditorWindow");
    Verify(Popup != nullptr && Session.FindInspector() != nullptr && Session.FindChild("##FluidPlumeEditorWindow/##PlumeOutlinerList_") != nullptr,
           "Opening: the editor is a modal window with an Outliner list and an Inspector child");
    for (const char* Row : Tokens) Verify(Editor->FindControl(std::string("row:") + Row) != nullptr, std::string("Opening: the Outliner lists ") + Row);
    for (const char* Name : { "library", "library-add", "pause", "clear", "emitter:0", "emitter:1", "emitter:2", "orbit", "eye:Domain", "eye:Emitter" })
        Verify(Editor->FindControl(Name) != nullptr, std::string("Opening: the window lays out `") + Name + "`");
    Verify(Editor->QuerySelected() == "Domain" && Editor->QueryLibraryShown() == false, "Opening: the Domain is selected and the libraries are shut");
    {
        // 📝 The Particle editor's proportions: the Outliner 292 px, the Inspector 404 px, the stage between them.
        const FluidPlumeControlRectangle* Row       = Editor->FindControl("row:Domain");
        const ImGuiWindow*                Inspector = Session.FindInspector();
        const auto                        Stage     = Editor->QueryStage();
        Verify(Row != nullptr && std::fabs((Row->Maximum.x - Row->Minimum.x) - 268.0f * Scale) < 2.0f, "Opening: an Outliner row is the 292 px column less its margins, 268 px");
        Verify(Inspector != nullptr && std::fabs(Inspector->Size.x - (404.0f * Scale - 2.0f)) < 1.5f, "Opening: the Inspector is 404 px wide (within its border)");
        Verify(Row != nullptr && Inspector != nullptr && Stage[0].x > Row->Maximum.x && Stage[1].x < Inspector->Pos.x, "Opening: the stage lies between the Outliner and the Inspector");
        Verify(Stage[1].x - Stage[0].x > 500.0f * Scale && Stage[1].y - Stage[0].y > 400.0f * Scale, "Opening: the stage is " + std::to_string(Stage[1].x - Stage[0].x) + " × " + std::to_string(Stage[1].y - Stage[0].y) + " px");
    }
    Verify(Editor->QueryImage().Texture == 0 && Editor->QueryImage().Failure == "The plume viewport needs the editor's Vulkan device.", "Opening: with no presenter the stage says why it is empty");
    Verify(Plume.TestEmitter == 0 && FluidPlumeEditor::QueryReadout() == nullptr, "Opening: with no presenter the emitter stays off and no solver is claimed");
    Verify(LogHas("PLUME", "Smoke & fire editor") && LogHas("RECORD", "Editing Fluid Plume"), "Opening: the Log Stream opens with the editor and the record it edits");
    Verify(Flatten(Plume.Gas) == Flatten(ConstructGasParameters()), "Opening: the record holds the defaults");
    const std::vector<unsigned char> Opening = WriteWindow("SmokeAndFireEditor-Opening", "The editor as it opens, no device and no solver");

    // ② The Outliner drives the Inspector: each row, pressed with the pointer, shows its page, and a control only that page has answers there and not
    //    on another page.
    const GasParameterEntry& Emission = *FindGasParameter("Emission");
    const auto               Step1    = [&](const char* Key, ImGuiKey Arrow) {
        const GasParameterEntry& Entry = *FindGasParameter(Key);
        const double             Was   = Plume.Gas.*(Entry.Field);
        const std::string        Id    = std::string("PlumeGas") + Key;
        Session.Use(Session.ResolveItem({ Id.c_str(), "##Track" }), Arrow);
        return std::pair<double, double>(Was, Plume.Gas.*(Entry.Field));
    };
    Session.Click("row:Sources");
    Verify(Editor->QuerySelected() == "Sources", "Sources row: pressed, it is the selected record");
    {
        const auto [Was, Now] = Step1("Emission", ImGuiKey_RightArrow);
        Verify(Now == Committed(std::min(Was + Emission.Step, Emission.Maximum), Emission) && Now != Was, "Sources page: Emission, moved one step in the window, commits the decimal its figure shows");
        const auto [FireWas, FireNow] = Step1("FlameIntensity", ImGuiKey_RightArrow);
        Verify(FireNow == FireWas, "Sources page: a Fire slider is not on it, so the key moves nothing");
        Plume.Gas = ConstructGasParameters();
    }
    Session.Click("row:Emitter");
    Verify(Editor->QuerySelected() == "Emitter", "Test emitter row: pressed, it is the selected record");
    Session.Use(Session.ResolveSegment("PlumeTestEmitter", 1));
    Verify(Plume.TestEmitter == 1, "Test emitter page: the Smoke plume segment, pressed in the window, starts the smoke");
    Session.Use(Session.ResolveSegment("PlumeTestEmitter", 0));
    Verify(Plume.TestEmitter == 0, "Test emitter page: Off stops it");
    Session.Click("row:Simulation");
    Verify(Editor->QuerySelected() == "Simulation", "Simulation row: pressed, it is the selected record");
    {
        const auto [Was, Now] = Step1("Buoyancy", ImGuiKey_RightArrow);
        Verify(Now > Was, "Simulation page: Buoyancy moves up by a step");
        const auto [EmissionWas, EmissionNow] = Step1("Emission", ImGuiKey_RightArrow);
        Verify(EmissionNow == EmissionWas, "Simulation page: Emission is not on it, so the key moves nothing");
        Plume.Gas = ConstructGasParameters();
    }
    Session.Settle();
    ExpectCurves("Simulation");
    Session.Click("row:Appearance");
    Verify(Editor->QuerySelected() == "Appearance", "Appearance row: pressed, it is the selected record");
    {
        const auto [Was, Now] = Step1("Density", ImGuiKey_RightArrow);
        Verify(Now > Was, "Appearance page: Density moves up by a step");
        Plume.Gas = ConstructGasParameters();
    }
    Session.Click("row:Fire");
    Verify(Editor->QuerySelected() == "Fire", "Fire row: pressed, it is the selected record");
    {
        const auto [Was, Now] = Step1("FlameIntensity", ImGuiKey_RightArrow);
        Verify(Now > Was, "Fire page: Flame intensity moves up by a step");
        const auto [BuoyancyWas, BuoyancyNow] = Step1("Buoyancy", ImGuiKey_RightArrow);
        Verify(BuoyancyNow == BuoyancyWas, "Fire page: a Simulation slider is not on it, so the key moves nothing");
        Plume.Gas = ConstructGasParameters();
    }
    ExpectStrip("Fire page");
    Session.Click("row:Domain");
    Verify(Editor->QuerySelected() == "Domain", "Domain row: pressed, it is the selected record");
    Plume.Gas = ApplyGasPreset(PresetNamed("Crosswind"));
    Session.Tick(2);
    Session.Use(Session.ResolveItem({ "PlumeDefaults" }));
    Verify(Flatten(Plume.Gas) == Flatten(ConstructGasParameters()), "Domain page: Defaults, pressed in the window, restores the 33 readings");
    Verify(LogHas("RECORD", "Defaults restored"), "Domain page: Defaults is in the Log Stream");
    Verify(!Editor->Perform("select:Nothing") && Editor->QuerySelected() == "Domain", "A verb that names no record is refused and the selection stays");

    // ③ The eyes in the Outliner: the plume's, and the test emitter's.
    {
        const uint64_t Revision = Plume.Revision;
        Session.Click("eye:Domain");
        Verify(!Plume.Enabled && Plume.Revision > Revision, "Domain eye: pressed, the plume is disabled and the record's revision advances");
        Verify(LogHas("RECORD", "Plume disabled"), "Domain eye: the Log Stream says so");
        Session.Click("eye:Domain");
        Verify(Plume.Enabled, "Domain eye: pressed again, the plume is enabled");
        Session.Click("eye:Emitter");
        Verify(Plume.TestEmitter == 1, "Test emitter eye: pressed, the smoke plume starts");
        Session.Click("eye:Emitter");
        Verify(Plume.TestEmitter == 0, "Test emitter eye: pressed again, it stops");
    }

    // ④ The libraries, with the pointer: opened from the Outliner's pill, from the card on the Domain page and from the Outliner's last row; the Looks
    //    tab with its chips and search; a look applied by its card; Escape and a click outside.
    const auto& Looks = FluidPlumeLibrary::QueryLooks();
    Session.Click("library");
    Verify(Editor->QueryLibraryShown() && Editor->QueryLibraryTab() == "looks" && Editor->QueryLibraryCards() == 6, "Libraries pill: the sheet opens on the Looks tab with six cards");
    for (const char* Name : { "library-sheet", "library-close", "library-tab:looks", "library-tab:grades", "library-group:All", "library-group:Smoke", "library-group:Fire", "library-group:Wind", "library-search" })
        Verify(Editor->FindControl(Name) != nullptr, std::string("Libraries: the sheet lays out `") + Name + "`");
    for (const FluidPlumeLook& Look : Looks) Verify(Editor->FindControl("library-card:" + Look.Identifier) != nullptr, "Libraries: " + Look.Name + " has its card");
    Verify(Editor->QueryLibraryMarked() == "tyre-smoke", "Libraries: with the defaults held, the Tyre smoke card is the one marked Applied");
    Session.Click("library-group:Fire");
    Verify(Editor->QueryLibraryGroup() == "Fire" && Editor->QueryLibraryCards() == 1 && Editor->FindControl("library-card:inferno") != nullptr, "Fire chip: one card, Inferno");
    Session.Click("library-group:Wind");
    Verify(Editor->QueryLibraryGroup() == "Wind" && Editor->QueryLibraryCards() == 1 && Editor->FindControl("library-card:crosswind") != nullptr, "Wind chip: one card, Crosswind");
    Session.Click("library-group:Smoke");
    Verify(Editor->QueryLibraryCards() == 4, "Smoke chip: four cards");
    Session.Click("library-group:All");
    Verify(Editor->QueryLibraryCards() == 6, "All chip: six cards again");
    Session.Click("library-search");
    Session.Type("haze");
    Verify(Editor->QueryLibraryCards() == 1 && Editor->FindControl("library-card:light-haze") != nullptr, "Search `haze`, typed into the field: one card, Light haze");
    Session.Key(ImGuiKey_A, true);
    Session.Key(ImGuiKey_Backspace);
    Verify(Editor->QueryLibraryCards() == 6, "Search emptied with Control-A and Backspace: six cards again");
    Session.Type("wind");
    Verify(Editor->QueryLibraryCards() == 2 && Editor->FindControl("library-card:crosswind") != nullptr && Editor->FindControl("library-card:legacy-billows") != nullptr,
           "Search `wind` finds Crosswind by its name and Legacy billows by the wind speed it lists (" + std::to_string(Editor->QueryLibraryCards()) + " cards)");
    Session.Type("zz");
    Verify(Editor->QueryLibraryCards() == 0, "Search `windzz` finds nothing");
    Session.Key(ImGuiKey_A, true);
    Session.Key(ImGuiKey_Backspace);
    Verify(Editor->QueryLibraryCards() == 6, "Search emptied again: six cards");
    Session.Key(ImGuiKey_Escape);   // [-] the first Escape leaves the field
    Verify(Editor->QueryLibraryShown(), "Escape in the search field leaves the field and keeps the sheet");
    {
        // 📝 A click inside the sheet but on no control changes nothing; a click on the veil outside the sheet closes it.
        const FluidPlumeControlRectangle* Sheet = Editor->FindControl("library-sheet");
        Session.Click(ImVec2(Sheet->Minimum.x + 8.0f, Sheet->Minimum.y + 8.0f));
        Verify(Editor->QueryLibraryShown(), "A click inside the sheet, on no control, keeps it open");
        const FluidPlumeControlRectangle* Veil = Editor->FindControl("library-veil");
        Session.Click(ImVec2(Veil->Minimum.x + 6.0f, Veil->Maximum.y - 6.0f));
        Verify(!Editor->QueryLibraryShown(), "A click on the veil outside the sheet closes it");
    }
    Session.Click("library-add");
    Verify(Editor->QueryLibraryShown(), "The Outliner's `Apply a look from the library` row opens the sheet");
    Session.Click("library-close");
    Verify(!Editor->QueryLibraryShown(), "Close (Esc) closes the sheet");
    Session.Use(Session.ResolveItem({ "PlumeOpenLibraries" }));
    Verify(Editor->QueryLibraryShown(), "The Domain page's `Open the libraries` opens the sheet");
    {
        const uint64_t Revision = Plume.Revision;
        Session.Click("library-card:inferno");
        Verify(Flatten(Plume.Gas) == Flatten(ApplyGasPreset(PresetNamed("Inferno"))), "Inferno's card, pressed, sets the prototype's Inferno gas");
        Verify(Plume.Revision > Revision && !Editor->QueryLibraryShown(), "The look advances the record's revision and the sheet closes");
        Verify(LogHas("LOOK", "Applied Inferno"), "The Log Stream says `Applied Inferno`");
    }
    Session.Key(ImGuiKey_L);
    Verify(Editor->QueryLibraryShown() && Editor->QueryLibraryMarked() == "inferno", "L opens the sheet again, and the card marked Applied is Inferno's");
    Session.Click("library-tab:grades");
    Verify(Editor->QueryLibraryTab() == "grades" && Editor->QueryLibraryCards() == 4, "Grades tab: four cards");
    for (const char* Grade : { "Low", "Medium", "High", "Ultra" }) Verify(Editor->FindControl(std::string("library-grade:") + Grade) != nullptr, std::string("Grades: ") + Grade + " has its card");
    Verify(Editor->QueryLibraryMarked().empty(), "Grades: with no solver attached, no grade is marked in use");
    {
        const std::vector<double> Before = Flatten(Plume.Gas);
        Session.Click("library-grade:Low");
        Verify(Editor->QueryLibraryShown() && Editor->QueryLibraryTab() == "grades" && Flatten(Plume.Gas) == Before, "A grade card is read-only: the sheet stays and no reading moves");
    }
    const FluidPlumeReadout Inferno = ReadSolver(230, "high", 2);
    Verify(Inferno.Resident && Inferno.Sources > 0 && Inferno.Ticks > 0,
           "The plume's CPU half is resident with sources and steps at refresh 230 (resident " + std::to_string(Inferno.Resident) + ", sources " + std::to_string(Inferno.Sources) +
               ", steps " + std::to_string(Inferno.Ticks) + ", relocations " + std::to_string(Inferno.Relocations) + ")");
    FluidPlumeEditor::AssignReadout(Inferno);
    Session.Tick(2);
    Verify(FluidPlumeEditor::QueryReadout() != nullptr && FluidPlumeEditor::QueryReadout()->Ticks == Inferno.Ticks, "A host's readout, assigned without a presenter, reaches the editor");
    Verify(Editor->QueryLibraryMarked() == "High", "Grades: with the readout at grade 2, the High card is marked in use");
    Session.Click("library-search");
    Session.Type("ultra");
    Verify(Editor->QueryLibraryCards() == 1 && Editor->FindControl("library-grade:Ultra") != nullptr, "Search `ultra` on the Grades tab: one card");
    Session.Key(ImGuiKey_A, true);
    Session.Key(ImGuiKey_Backspace);
    Session.Key(ImGuiKey_Escape);
    Verify(Editor->QueryLibraryCards() == 4, "Search emptied: four grade cards");
    const std::vector<unsigned char> GradesSheet = WriteWindow("SmokeAndFireEditor-Libraries-grades", "The libraries, Grades tab, with a host's readout: High marked in use");
    Session.Click("library-tab:looks");
    Verify(Editor->QueryLibraryTab() == "looks" && Editor->QueryLibraryCards() == 6 && Editor->QueryLibraryMarked() == "inferno", "Looks tab again: six cards, Inferno marked");
    const std::vector<unsigned char> LooksSheet = WriteWindow("SmokeAndFireEditor-Libraries-looks", "The libraries, Looks tab, with Inferno applied");
    Verify(LooksSheet != GradesSheet && LooksSheet != Opening, "The two sheets differ from each other and from the opening window");
    Session.Key(ImGuiKey_Escape);
    Verify(!Editor->QueryLibraryShown(), "Escape closes the sheet");
    Verify(Editor->Perform("library:on") && Editor->QueryLibraryShown() && Editor->Perform("library:off") && !Editor->QueryLibraryShown(), "The library verbs open and shut the sheet");
    Verify(!Editor->Perform("library-tab:nothing") && !Editor->Perform("look:nothing") && !Editor->Perform("nonsense"), "Verbs that name nothing are refused");

    // ⑤ The keyboard on the window.
    Session.Click("row:Fire");
    Plume.TestEmitter = 0;
    Session.Key(ImGuiKey_E);
    Verify(Plume.TestEmitter == 1, "E turns the test emitter on (smoke)");
    Session.Key(ImGuiKey_E);
    Verify(Plume.TestEmitter == 2, "E again: fire");
    Session.Key(ImGuiKey_E);
    Verify(Plume.TestEmitter == 0, "E again: off");
    Verify(LogHas("EMITTER", "Test emitter: Fire"), "The Log Stream names the emitter's states");
    Session.Key(ImGuiKey_Space);
    Verify(!Editor->QueryPlaying(), "Space pauses the stage");
    Session.Key(ImGuiKey_Space);
    Verify(Editor->QueryPlaying(), "Space plays it again");
    {
        Verify(FluidPlumeEditor::TakeRequests().empty(), "No request is pending before Clear smoke");
        Session.Key(ImGuiKey_C);
        const std::vector<std::string> Taken = FluidPlumeEditor::TakeRequests();
        Verify(Taken.size() == 1 && Taken[0] == "clear-smoke", "C raises `clear-smoke` for a host without a presenter");
        Verify(FluidPlumeEditor::TakeRequests().empty(), "The host takes a request once");
        Session.Click("clear");
        Verify(FluidPlumeEditor::TakeRequests().size() == 1 && LogHas("SOLVER", "Smoke cleared"), "The bar's Clear smoke does the same, and the Log Stream says so");
    }

    // ⑥ A presenter: the stand-in is registered, the window closed and opened again. The editor passes it the record's own gas, the emitter, the
    //    camera and the stage's pixels, and shows what it returns.
    const uint32_t EmitterBefore = Plume.TestEmitter;
    Session.UseWindowControl("##CloseFluidPlumeEditor");
    Session.Tick(2);
    Verify(!Runtime.QueryActiveEditorToken().IsValid() && Editor->QueryRecord() == nullptr, "Close Editor closes the window and lets the record go");
    Verify(Plume.TestEmitter == EmitterBefore, "The record leaves as it came: the test emitter is what it was when the window opened");
    StandInPresenter Presenter;
    Presenter.Readout = Inferno;
    Runtime.RegisterPlumeViewportPresenter([&Presenter](const PlumeViewportRequest& Request) { return Presenter(Request); });
    FluidPlumeEditor::RevokeReadout();
    Verify(Runtime.RequestEditorOpen(Token), "The editor opens again");
    Session.Tick(4);
    Verify(Editor->QueryRecord() == &Plume && Plume.TestEmitter == 1, "With a presenter the window opens with the smoke plume running, so the readings can be judged as they are edited");
    Verify(LogHas("EMITTER", "on while the editor is open"), "The Log Stream says the emitter was switched on for the window");
    Verify(Presenter.Requests.size() >= 3, "The presenter is asked on every refresh (" + std::to_string(Presenter.Requests.size()) + " requests in four refreshes)");
    {
        const PlumeViewportRequest& Last  = Presenter.Requests.back();
        const auto                  Stage = Editor->QueryStage();
        Verify(Last.Gas == &Plume.Gas, "The request carries the record's own gas, by pointer: an edit reaches the host on the next refresh");
        Verify(Last.Enabled == Plume.Enabled && Last.TestEmitter == 1, "The request carries the plume's switch and the emitter");
        Verify(std::fabs(static_cast<double>(Last.Extent[0]) - (Stage[1].x - Stage[0].x)) <= 1.0 && std::fabs(static_cast<double>(Last.Extent[1]) - (Stage[1].y - Stage[0].y)) <= 1.0,
               "The request asks for the stage's pixels: " + std::to_string(Last.Extent[0]) + " × " + std::to_string(Last.Extent[1]));
        Verify(std::fabs(Last.Seconds - 1.0 / 30.0) < 1.0e-6 && !Last.Clear, "The request carries the interface's 1/30 s (ImGui's float delta, " + std::to_string(Last.Seconds) + ") and no clear");
        const auto Camera = Editor->QueryCamera();
        Verify(Last.Yaw == Camera[0] && Last.Pitch == Camera[1] && Last.Distance == Camera[2], "The request carries the camera");
        Verify(Editor->QueryImage().Texture == StandInTexture && Editor->QueryImage().Adapter == "Validation stand-in presenter", "The window shows the presenter's image and names its adapter");
        Verify(FluidPlumeEditor::QueryReadout() != nullptr && FluidPlumeEditor::QueryReadout()->Ticks == Inferno.Ticks && FluidPlumeEditor::QueryReadout()->Sources == Inferno.Sources,
               "The presenter's readout is the editor's readout: the Outliner, the HUD and the header read the solver it names");
        Verify(LogHas("PLUME", "Vulkan ready"), "The Log Stream says the device is ready, once");
    }
    {
        // 📝 Pause: the presenter is still asked (the picture follows the camera and the readings) but is given no time. Play gives it time again.
        const double Before = Presenter.Clock;
        Session.Click("pause");
        Verify(!Editor->QueryPlaying(), "Pause: the stage is paused");
        Session.Tick(3);
        Verify(Presenter.Requests.back().Seconds == 0.0 && Presenter.Clock >= Before, "Paused: the request carries no seconds");
        const double Held = Presenter.Clock;
        Session.Tick(3);
        Verify(Presenter.Clock == Held, "Paused: the presenter's clock stands still over three refreshes");
        Session.Click("pause");
        Verify(Editor->QueryPlaying(), "Play: the stage runs");
        Session.Tick(3);
        Verify(Presenter.Clock > Held + 0.05, "Playing: the presenter's clock advances again");
    }
    {
        // 📝 Clear smoke reaches the presenter exactly once, on the next refresh, and does not queue for a host without one.
        const uint32_t Before = Presenter.Clears;
        const size_t   Asked  = Presenter.Requests.size();
        Session.Click("clear");
        Session.Tick(3);
        Verify(Presenter.Clears == Before + 1, "Clear smoke: the presenter is asked to clear once (" + std::to_string(Presenter.Clears - Before) + ")");
        size_t Cleared = 0;
        for (size_t Index = Asked; Index < Presenter.Requests.size(); ++Index) Cleared += Presenter.Requests[Index].Clear ? 1u : 0u;
        Verify(Cleared == 1 && !Presenter.Requests.back().Clear, "Clear smoke: exactly one request carries the clear, and later refreshes do not clear again");
        Session.Key(ImGuiKey_C);
        Session.Tick(2);
        Verify(Presenter.Clears == Before + 2, "C clears through the presenter too");
        Verify(FluidPlumeEditor::TakeRequests().empty(), "With a presenter the clear goes through it: nothing is queued for a host to take twice");
    }
    {
        // 📝 The bar's emitter pills and the eye reach the next request.
        Session.Click("emitter:2");
        Session.Tick(2);
        Verify(Plume.TestEmitter == 2 && Presenter.Requests.back().TestEmitter == 2, "The Fire pill: the record and the next request say fire");
        Session.Click("emitter:0");
        Session.Tick(2);
        Verify(Plume.TestEmitter == 0 && Presenter.Requests.back().TestEmitter == 0, "The Off pill: the record and the next request say off");
        Session.Click("emitter:1");
        Session.Tick(2);
        Verify(Plume.TestEmitter == 1 && Presenter.Requests.back().TestEmitter == 1, "The Smoke pill: the record and the next request say smoke");
        Session.Click("eye:Domain");
        Session.Tick(2);
        Verify(!Plume.Enabled && !Presenter.Requests.back().Enabled, "The Domain eye: the next request says the plume is disabled");
        Session.Click("eye:Domain");
        Session.Tick(2);
        Verify(Plume.Enabled && Presenter.Requests.back().Enabled, "The Domain eye: and enabled again");
    }
    {
        // 📝 Orbit and zoom: a drag over the stage turns the camera (yaw 0.006 rad per pixel, pitch likewise), the wheel zooms by a tenth per notch.
        const auto   Start  = Editor->QueryCamera();
        const auto   Stage  = Editor->QueryStage();
        const ImVec2 Middle((Stage[0].x + Stage[1].x) * 0.5f, (Stage[0].y + Stage[1].y) * 0.5f);
        Session.Drag(Middle, ImVec2(60.0f, 20.0f));
        const auto Turned = Editor->QueryCamera();
        Verify(std::fabs((Turned[0] - Start[0]) - (-60.0 * 0.006)) < 1.0e-6 && std::fabs((Turned[1] - Start[1]) - (20.0 * 0.006)) < 1.0e-6,
               "Drag by (60, 20) px: yaw " + std::to_string(Turned[0] - Start[0]) + " rad and pitch " + std::to_string(Turned[1] - Start[1]) + " rad, as the bar's hint says");
        Verify(Turned[2] == Start[2], "Drag: the distance is unchanged");
        Session.Tick(1);
        Verify(Presenter.Requests.back().Yaw == Turned[0] && Presenter.Requests.back().Pitch == Turned[1], "The next request carries the turned camera");
        Session.Wheel(Middle, 3);
        const auto Zoomed = Editor->QueryCamera();
        Verify(std::fabs(Zoomed[2] - Turned[2] * std::exp(-0.3)) < 1.0e-6, "Three wheel notches: the distance is " + std::to_string(Zoomed[2]) + " m (" + std::to_string(Turned[2]) + " × e^-0.3)");
        Verify(Editor->Perform("camera:0.5,0.3,20") && Editor->QueryCamera()[0] == 0.5 && Editor->QueryCamera()[1] == 0.3 && Editor->QueryCamera()[2] == 20.0, "The camera verb sets yaw, pitch and distance");
        Verify(Editor->Perform("camera:0,5,500") && Editor->QueryCamera()[1] == 1.5 && Editor->QueryCamera()[2] == 40.0, "The camera verb clamps pitch to 1.5 rad and distance to 40 m");
        Verify(Editor->Perform("camera:0.3,0.22,11"), "The camera returns to its opening view");
    }
    {
        // 📝 An edit made in the window reaches the host on the next refresh without any copying: Inferno, by its card, then a slider.
        Session.Click("library");
        Session.Click("library-card:inferno");
        Session.Tick(2);
        Verify(Presenter.Requests.back().Gas == &Plume.Gas && Flatten(*Presenter.Requests.back().Gas) == Flatten(ApplyGasPreset(PresetNamed("Inferno"))), "Inferno applied: the next request's gas is the Inferno gas");
        Session.Click("row:Fire");
        const auto [Was, Now] = Step1("FireLight", ImGuiKey_RightArrow);
        Session.Tick(1);
        Verify(Now > Was && Presenter.Requests.back().Gas->FireLight == Now, "A Fire slider moved in the window: the next request reads the new Fire light");
    }
    Session.Click("emitter:2");
    Session.Click("row:Fire");
    Session.Tick(3);
    ExpectStrip("Live");
    const std::vector<unsigned char> Live = WriteWindow("SmokeAndFireEditor-Live-standin", "The editor with a presenter, Inferno applied and the fire emitter: a stand-in picture on the stage");
    Verify(Live != Opening, "The live window differs from the opening window");

    // ⑦ A 1366 × 800 window at the Medium grade: the same layout, the stage smaller, everything inside the window.
    FluidPlumeEditor::RevokeReadout();
    Presenter.Readout = ReadSolver(120, "medium", 1);
    Session.Width     = 1366;
    Session.Height    = 800;
    Session.Tick(4);
    Session.Click("row:Simulation");
    Session.Tick(3);
    {
        const auto         Stage     = Editor->QueryStage();
        const ImGuiWindow* Inspector = Session.FindInspector();
        const FluidPlumeControlRectangle* Last = Editor->FindControl("row:Fire");
        Verify(Inspector != nullptr && Inspector->Pos.x + Inspector->Size.x <= 1366.0f && Stage[1].x < Inspector->Pos.x && Stage[1].y - Stage[0].y > 300.0f,
               "1366 px: the Inspector is inside the window and the stage keeps " + std::to_string(Stage[1].x - Stage[0].x) + " × " + std::to_string(Stage[1].y - Stage[0].y) + " px");
        Verify(Last != nullptr && Last->Maximum.y < 800.0f, "1366 px: the Outliner's last row is inside the window");
        const PlumeViewportRequest& Request = Presenter.Requests.back();
        Verify(std::fabs(static_cast<double>(Request.Extent[0]) - (Stage[1].x - Stage[0].x)) <= 1.0, "1366 px: the request follows the stage's new width");
        Verify(Editor->QueryReadout() != nullptr && Editor->QueryReadout()->Grade == 1, "1366 px: the readout is the Medium grade's");
    }
    ExpectCurves("Narrow");
    {
        // 📝 The bar's controls all lie inside the viewport's column: where it is narrow the bar gives up its words (Pause, Clear smoke, the pill, the title).
        const auto Stage = Editor->QueryStage();
        for (const char* Name : { "pause", "clear", "emitter:0", "emitter:1", "emitter:2" })
        {
            const FluidPlumeControlRectangle* Control = Editor->FindControl(Name);
            Verify(Control != nullptr && Control->Minimum.x >= Stage[0].x - 8.0f && Control->Maximum.x <= Stage[1].x, std::string("1366 px: the bar's `") + Name + "` lies inside the viewport's column");
        }
    }
    WriteWindow("SmokeAndFireEditor-Narrow-medium", "The editor on a 1366 px window at the Medium grade, Simulation selected");

    // 📝 The smallest window the editor's own minimum (1100 × 640) allows on a display of 1156 × 720: the centre column is 360 px wide and the bar is all icons.
    Session.Width  = 1156;
    Session.Height = 720;
    Session.Tick(4);
    {
        const auto         Stage     = Editor->QueryStage();
        const ImGuiWindow* Inspector = Session.FindInspector();
        Verify(Inspector != nullptr && Stage[1].x < Inspector->Pos.x && Stage[1].x - Stage[0].x > 300.0f && Stage[1].x - Stage[0].x < 400.0f,
               "1156 px: the stage keeps " + std::to_string(Stage[1].x - Stage[0].x) + " × " + std::to_string(Stage[1].y - Stage[0].y) + " px between the Outliner and the Inspector");
        for (const char* Name : { "pause", "clear", "emitter:0", "emitter:1", "emitter:2" })
        {
            const FluidPlumeControlRectangle* Control = Editor->FindControl(Name);
            Verify(Control != nullptr && Control->Minimum.x >= Stage[0].x - 8.0f && Control->Maximum.x <= Stage[1].x, std::string("1156 px: the bar's `") + Name + "` lies inside the viewport's column");
        }
        const FluidPlumeControlRectangle* Last = Editor->FindControl("row:Fire");
        const ImGuiWindow*                List = Session.FindChild("##FluidPlumeEditorWindow/##PlumeOutlinerList_");
        Verify(Last != nullptr && List != nullptr && (Last->Maximum.y < 720.0f || List->ScrollMax.y > 0.0f), "1156 px: the Outliner's last row is inside the window, or its list scrolls to it");
    }
    WriteWindow("SmokeAndFireEditor-Smallest", "The editor on the smallest window its minimum allows: 1156 × 720, the bar all icons");

    // ⑧ Close Editor returns to the workspace, which shows the record's own page; the record leaves as it came.
    Session.Width  = 1920;
    Session.Height = 1080;
    Presenter.Readout = Inferno;
    Session.Tick(2);
    const uint32_t EmitterOpen = Plume.TestEmitter;
    Verify(EmitterOpen == 2, "Before closing, the emitter is the fire the pills chose");
    Session.UseWindowControl("##CloseFluidPlumeEditor");
    Session.Tick(2);
    Verify(!Runtime.QueryActiveEditorToken().IsValid(), "Close Editor returns to the workspace");
    Verify(Session.FindInspector() == nullptr && GImGui->OpenPopupStack.Size == 0, "The editor window is gone: no Inspector is submitted and no popup is open");
    Verify(Plume.TestEmitter == 0, "The record leaves as it came: the test emitter, which the editor turned on when it opened, is off again");
    Verify(Flatten(Plume.Gas) != Flatten(ConstructGasParameters()), "The edits stay: the gas is still Inferno with the Fire light moved");
    Verify(!Editor->Perform("defaults") && !Editor->Perform("emitter:fire") && Editor->Perform("pause") && Editor->QueryRecord() == nullptr,
           "With the window closed, a verb that edits the record is refused and the stage's own verbs still answer");
    // 📝 The Outliner lists the new record last: scrolled to the end of its list the row shows, selected.
    Session.Wheel(ImVec2(180.0f * Scale, 600.0f * Scale), -40);
    Session.Tick(2);
    const std::vector<unsigned char> Workspace1080 = WriteWindow("Workspace-FluidPlume-selected", "The workspace with the FluidPlume record selected: its row in the Outliner and its page");
    Verify(Workspace1080 != Opening, "The workspace with the record selected differs from the open editor");
    Runtime.RegisterPlumeViewportPresenter({});
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
        ValidateLibrary();

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
