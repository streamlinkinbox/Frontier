/*==========================================================================================================================================
                                                         FLUIDPLUMEPAGES.CPP
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor's pages on a FluidPlume record: InspectorPages.js BuildGas, card for card — the same controls in the same order with
//    the same captions, ranges, steps and units (read from the gas catalogue), the two instruments laid on the Simulation and Fire cards, and the
//    six presets as the prototype applies them (Object.assign(Gas, ConstructGasParameters(), Values)) — dealt out to the Outliner's records.

#include "Engine/Interface/FluidPlumeEditor/FluidPlumePages.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ActionRow.h"
#include "Engine/Interface/Controls/CaptionText.h"
#include "Engine/Interface/Controls/ChoiceRow.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/MetricTiles.h"
#include "Engine/Interface/Controls/PageHeading.h"
#include "Engine/Interface/Controls/ReadoutBlock.h"
#include "Engine/Interface/Controls/SectionRows.h"
#include "Engine/Interface/Controls/SelectRow.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/PhysicalDynamics/FluidDynamics/GasInstrumentProjection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace Frontier
{

namespace
{

using namespace FluidDynamics;

constexpr const char* MiddleDot = "\xC2\xB7";
constexpr const char* TimesSign = "\xC3\x97";

// 📝 InspectorPages.js IgnitionOptions: the tyre's ignition is the Vehicle record's setting (its thermal ignition, ignition_k and ignition_s), shown
//    here as the prototype shows it on this page.
constexpr const char* IgnitionOptions[3] = { "Authored (350 \xC2\xB0""C held 4 s)", "Diagnostic (160 \xC2\xB0""C held 1 s)", "Off" };

[[nodiscard]] const PlumeGrade& ResolveLevel(uint32_t Grade) noexcept
{
    const auto& Grades = QueryPlumeGrades();
    return Grades[std::min<size_t>(Grade, Grades.size() - 1)];
}

// 📝 Number.prototype.toFixed(0) of a positive figure: the nearest integer, a tie upward.
[[nodiscard]] long long RoundWhole(double Value) noexcept
{
    return static_cast<long long>(std::floor(Value + 0.5));
}

// 📝 A card with its heading, or without it when the page's own heading names the card.
void BeginCard(ControlColumn& Page, const FluidPlumeContext& Context, const char* Title, const char* Glyph, const char* Status, const char* Caption)
{
    SectionRows::BeginSection(Page, Context.Titled ? Title : nullptr, Context.Titled ? Glyph : nullptr, Context.Titled ? Status : nullptr, Caption);
}

// 📝 GasSlider: the catalogue's label, range, step and unit; the digits follow the step; the reading is the decimal the figure shows (the prototype's
//    range input commits that decimal), not the float the slider moved. Low and High are the range labels under the track.
bool ConstructGasSlider(ControlColumn& Page, FluidPlumeContext& Context, std::string_view Key, const char* Low = nullptr, const char* High = nullptr)
{
    const GasParameterEntry* Entry = FindGasParameter(Key);
    if (Entry == nullptr) return false;
    const std::string Id    = "PlumeGas" + std::string(Key);
    const std::string Label(Entry->Label), Unit(Entry->Unit);
    double&           Reading = Context.Plume.Gas.*(Entry->Field);
    const bool        Moved   = Low != nullptr || High != nullptr
                                    ? SectionRows::MeasureCaptioned(Page, Id.c_str(), Label.c_str(), Reading, Entry->Minimum, Entry->Maximum, Entry->Step, Unit.c_str(), Low, High)
                                    : SectionRows::Measure(Page, Id.c_str(), Label.c_str(), Reading, Entry->Minimum, Entry->Maximum, Entry->Step, Unit.c_str());
    if (!Moved) return false;
    const int    Digits = Entry->Step >= 1.0 ? 0 : Entry->Step >= 0.1 ? 1 : Entry->Step >= 0.01 ? 2 : 3;
    const double Place  = std::pow(10.0, Digits);
    Reading             = std::clamp(std::round(Reading * Place) / Place, Entry->Minimum, Entry->Maximum);
    Context.Plume.AdvanceRevision();
    return true;
}

constexpr ImU32 InstrumentFill   = IM_COL32(0x14, 0x14, 0x14, 255);   // [-] .instrument background #141414
constexpr ImU32 InstrumentBorder = IM_COL32(0x26, 0x26, 0x26, 255);   // [-] .instrument border #262626

[[nodiscard]] ImU32 ResolveInk(const InstrumentColour& Colour) noexcept
{
    return IM_COL32(Colour[0], Colour[1], Colour[2], 255);
}

// 📝 C.Instrument: a block 14 px below the card's last row and 4 px above the next, CssHeight tall with a 1 px border and 12 px corners; its canvas is
//    640 px wide and twice as tall, shown at the block's width and CssHeight, so x maps by (width / 640) and y by one half — as the browser stretches it.
struct InstrumentCanvas
{
    ImVec2 Origin{};   // [screen px] the canvas's top-left
    float  ScaleX = 1.0f;
    float  ScaleY = 1.0f;
    float  FontScale = 1.0f;
};

[[nodiscard]] InstrumentCanvas ConstructInstrumentCanvas(ControlColumn& Page, float CssHeight)
{
    const float  S      = Page.QueryScale();
    const ImVec2 Top    = Page.Space(CssHeight, 14.0f, 4.0f);
    const float  Width  = Page.QueryWidth();
    ImDrawList*  Ink    = Page.QueryDrawList();
    const ImVec2 Max(Top.x + Width, Top.y + CssHeight * S);
    Ink->AddRectFilled(Top, Max, InstrumentFill, 12.0f * S);
    Ink->AddRect(Top, Max, InstrumentBorder, 12.0f * S, ImDrawFlags_None, S);
    InstrumentCanvas Canvas;
    Canvas.Origin    = ImVec2(Top.x + S, Top.y + S);
    Canvas.ScaleX    = (Width - 2.0f * S) / static_cast<float>(InstrumentCanvasWidth);
    Canvas.ScaleY    = (CssHeight - 2.0f) * S / (CssHeight * 2.0f);
    Canvas.FontScale = Canvas.ScaleY;
    return Canvas;
}

void ConstructLabels(ControlColumn& Page, const InstrumentCanvas& Canvas, const std::vector<InstrumentLabel>& Labels)
{
    for (const InstrumentLabel& Label : Labels)
    {
        // 📝 The canvas font is 18 px: half a CSS pixel per canvas pixel.
        (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 18.0f * Canvas.FontScale,
                                  ImVec2(Canvas.Origin.x + static_cast<float>(Label.X) * Canvas.ScaleX, Canvas.Origin.y + static_cast<float>(Label.Y) * Canvas.ScaleY),
                                  ResolveInk(Label.Colour), Label.Text.c_str());
    }
}

void ConstructDecayCanvas(ControlColumn& Page, const GasParameters& Gas)
{
    constexpr float CssHeight = 86.0f;   // [px] Instrument(Simulation, 86, …)
    const InstrumentCanvas Canvas = ConstructInstrumentCanvas(Page, CssHeight);
    const DecayInstrument  Decay  = FluidDynamics::ConstructDecayInstrument(Gas, InstrumentCanvasWidth, CssHeight * 2.0);
    ImDrawList*            Ink    = Page.QueryDrawList();
    std::vector<ImVec2>    Path;
    for (const InstrumentPolyline& Stroke : Decay.Strokes)
    {
        Path.clear();
        for (const auto& Point : Stroke.Points)
            Path.emplace_back(Canvas.Origin.x + static_cast<float>(Point[0]) * Canvas.ScaleX, Canvas.Origin.y + static_cast<float>(Point[1]) * Canvas.ScaleY);
        Ink->AddPolyline(Path.data(), static_cast<int>(Path.size()), ResolveInk(Stroke.Colour), ImDrawFlags_None,
                         static_cast<float>(Stroke.Width) * 0.5f * (Canvas.ScaleX + Canvas.ScaleY));
    }
    ConstructLabels(Page, Canvas, Decay.Labels);
}

void ConstructBlackbodyCanvas(ControlColumn& Page, const GasParameters& Gas)
{
    constexpr float CssHeight = 54.0f;   // [px] Instrument(Fire, 54, …)
    const InstrumentCanvas    Canvas    = ConstructInstrumentCanvas(Page, CssHeight);
    const BlackbodyInstrument Blackbody = FluidDynamics::ConstructBlackbodyInstrument(Gas, InstrumentCanvasWidth, CssHeight * 2.0);
    ImDrawList*               Ink       = Page.QueryDrawList();
    for (const InstrumentBand& Band : Blackbody.Bands)
    {
        const ImVec2 Min(Canvas.Origin.x + static_cast<float>(Band.X) * Canvas.ScaleX, Canvas.Origin.y + static_cast<float>(Band.Y) * Canvas.ScaleY);
        const ImVec2 Max(Canvas.Origin.x + static_cast<float>(Band.X + Band.Width) * Canvas.ScaleX, Canvas.Origin.y + static_cast<float>(Band.Y + Band.Height) * Canvas.ScaleY);
        Ink->AddRectFilled(Min, Max, ResolveInk(Band.Colour));
    }
    ConstructLabels(Page, Canvas, Blackbody.Labels);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    READOUTS
//------------------------------------------------------------------------------------------------------------------------

uint32_t FluidPlumePages::ResolveGrade(const FluidPlumeContext& Context) noexcept
{
    return Context.Readout != nullptr ? Context.Readout->Grade : 2u;
}

std::string FluidPlumePages::FormatLatticeExtent(uint32_t Grade)
{
    const PlumeGrade& Level = ResolveLevel(Grade);
    return std::to_string(Level.Extent[0]) + TimesSign + std::to_string(Level.Extent[1]) + TimesSign + std::to_string(Level.Extent[2]);
}

std::string FluidPlumePages::FormatPlumeDescription(uint32_t Grade)
{
    const PlumeGrade& Level = ResolveLevel(Grade);
    return std::string("Slate 3c987f3 plume ") + MiddleDot + " " + std::string(Level.Name) + " " + MiddleDot + " " + FormatLatticeExtent(Grade) + " @ " +
           std::to_string(RoundWhole(Level.CellExtent * 100.0)) + " cm";
}

std::string FluidPlumePages::FormatPlumeCounters(const FluidPlumeReadout* Readout)
{
    if (Readout == nullptr) return "no solver attached";
    return "sources " + std::to_string(Readout->Sources) + " " + MiddleDot + " steps " + std::to_string(Readout->Ticks) + " " + MiddleDot + " " +
           (Readout->Resident ? "resident" : "idle");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EDITS
//------------------------------------------------------------------------------------------------------------------------

void FluidPlumePages::ApplyLook(FluidPlumeContext& Context, const FluidPlumeLook& Look)
{
    Context.Plume.Gas = Look.Values;
    Context.Plume.AdvanceRevision();
    Context.Requests.push_back("noted:look:" + Look.Identifier);
}

void FluidPlumePages::RestoreDefaults(FluidPlumeContext& Context)
{
    Context.Plume.Gas = ConstructGasParameters();
    Context.Plume.AdvanceRevision();
    Context.Requests.push_back("noted:defaults");
}

void FluidPlumePages::ToggleEnabled(FluidPlumeContext& Context)
{
    Context.Plume.Enabled = !Context.Plume.Enabled;
    Context.Plume.AdvanceRevision();
    Context.Requests.push_back(Context.Plume.Enabled ? "noted:enabled:on" : "noted:enabled:off");
}

void FluidPlumePages::ChooseEmitter(FluidPlumeContext& Context, uint32_t Emitter)
{
    // 📝 The emitter is the editor's own and is not saved, so choosing one does not touch the record's revision.
    Context.Plume.TestEmitter = std::min(Emitter, 2u);
    Context.Requests.push_back("noted:emitter:" + std::to_string(Context.Plume.TestEmitter));
}

void FluidPlumePages::ToggleEmitter(FluidPlumeContext& Context)
{
    ChooseEmitter(Context, Context.Plume.TestEmitter == 0 ? 1u : 0u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARDS
//------------------------------------------------------------------------------------------------------------------------

void FluidPlumePages::ConstructDomainCard(ControlColumn& Page, FluidPlumeContext& Context)
{
    const FluidPlumeReadout* Readout = Context.Readout;
    BeginCard(Page, Context, "Domain", "Box", Readout == nullptr ? "no solver" : Readout->Resident ? "resident" : "idle", "Toroidal lattice following the car");
    const std::string Extent = FormatLatticeExtent(ResolveGrade(Context));
    ReadoutBlock::Construct(Page, "Fine lattice", Extent.c_str(), "cells");
    CaptionText::Construct(Page, "The requested Slate plume: GPU coarse flow carrying fine smoke/heat/flame/soot, toroidal windows, 60 Hz transport and a coverage-clipped march. "
                                 "No passes run while idle.");
    const std::string Sources     = Readout != nullptr ? std::to_string(Readout->Sources) : "\xE2\x80\x93";
    const std::string Ticks       = Readout != nullptr ? std::to_string(Readout->Ticks) : "\xE2\x80\x93";
    const std::string Relocations = Readout != nullptr ? std::to_string(Readout->Relocations) : "\xE2\x80\x93";
    const MetricTile  Tiles[3]    = { { "Sources", Sources, "" }, { "Ticks", Ticks, "" }, { "Relocations", Relocations, "" } };
    MetricTiles::Construct(Page, Tiles, 3, MetricTiles::NoLead);
    CaptionText::Construct(Page, "Slate 3c987f3 plume: toroidal transport, vorticity, warm-started pressure, detailed multi-scattering; marched at the GI trace resolution before TAA.");
    const ActionEntry Actions[2] = { { "PlumeClearSmoke", "Clear smoke" }, { "PlumeDefaults", "Defaults" } };
    const int         Pressed    = ActionRow::Construct(Page, Actions);
    if (Pressed == 0) Context.Requests.push_back("clear-smoke");
    if (Pressed == 1) RestoreDefaults(Context);
    SectionRows::EndSection(Page);
}

void FluidPlumePages::ConstructEmitterCard(ControlColumn& Page, FluidPlumeContext& Context)
{
    BeginCard(Page, Context, "Test emitter", "Puff", Context.Plume.TestEmitter == 0 ? "off" : Context.Plume.TestEmitter == 1 ? "smoke" : "fire",
              "A steady smoke source, or a burning one");
    const char* const Emitters[3] = { "Off", "Smoke plume", "Fire" };
    int               Emitter     = static_cast<int>(std::min<uint32_t>(Context.Plume.TestEmitter, 2u));
    const std::string EmitterLabel = std::string("Test emitter ") + MiddleDot + " 3 m ahead of the car";
    if (ChoiceRow::Construct(Page, "PlumeTestEmitter", EmitterLabel.c_str(), Emitters, 3, Emitter)) ChooseEmitter(Context, static_cast<uint32_t>(Emitter));
    CaptionText::Construct(Page, "The prototype puts it 3 m ahead of the car; here it stands at the middle of the stage. It is the editor's own: it is not saved with the record.");
    SectionRows::EndSection(Page);
}

void FluidPlumePages::ConstructSourceCard(ControlColumn& Page, FluidPlumeContext& Context)
{
    BeginCard(Page, Context, "Source", "Droplets", nullptr, "Multipliers on the tyre model");
    for (const char* Key : { "Emission", "Heat", "Soot" })
        (void)ConstructGasSlider(Page, Context, Key);
    SectionRows::EndSection(Page);
}

void FluidPlumePages::ConstructSimulationCard(ControlColumn& Page, FluidPlumeContext& Context)
{
    BeginCard(Page, Context, "Simulation", "Wind", nullptr, nullptr);
    ConstructDecayCanvas(Page, Context.Plume.Gas);
    for (const char* Key : { "Buoyancy", "Dissipation", "Cooling", "Vorticity", "Turbulence", "TurbulenceScale" })
        (void)ConstructGasSlider(Page, Context, Key);
    (void)ConstructGasSlider(Page, Context, "WindSpeed", "Still", "Gale");
    (void)ConstructGasSlider(Page, Context, "WindHeading");
    (void)ConstructGasSlider(Page, Context, "TimeScale", "Slow motion", "Fast");
    (void)ConstructGasSlider(Page, Context, "PressureSweeps");
    SectionRows::EndSection(Page);
}

void FluidPlumePages::ConstructAppearanceCard(ControlColumn& Page, FluidPlumeContext& Context)
{
    BeginCard(Page, Context, "Appearance", "CloudFog", nullptr, nullptr);
    for (const char* Key : { "Density", "Albedo", "Phase", "MultiScatter", "Ambient", "Detail", "DetailScale" })
        (void)ConstructGasSlider(Page, Context, Key);
    SectionRows::EndSection(Page);
}

void FluidPlumePages::ConstructFireCard(ControlColumn& Page, FluidPlumeContext& Context)
{
    BeginCard(Page, Context, "Fire", "Flame", nullptr, "Combustion of the tyre");
    ConstructBlackbodyCanvas(Page, Context.Plume.Gas);
    int Ignition = 0;
    (void)SelectRow::Construct(Page, "PlumeIgnition", "Ignition", IgnitionOptions, 3, Ignition, false,
                               "The tyre's ignition is the Vehicle record's setting (thermal: ignition, ignition_k, ignition_s); edit it on the Vehicle page.");
    for (const char* Key : { "FlameIntensity", "FlameCore", "FlameTip", "FlameTemperature", "FlameLoss", "FireLight" })
        (void)ConstructGasSlider(Page, Context, Key);
    SectionRows::EndSection(Page);
}

void FluidPlumePages::ConstructLooksCard(ControlColumn& Page, FluidPlumeContext& Context)
{
    SectionRows::BeginSection(Page, "Looks", "Sparkles", nullptr, nullptr);
    // 📝 The look the 33 readings hold exactly, or how far they have been edited from the defaults.
    const FluidPlumeLook* Held = FluidPlumeLibrary::MatchLook(Context.Plume.Gas);
    char                  Now[96];
    if (Held != nullptr) std::snprintf(Now, sizeof(Now), "Now: %s", Held->Name.c_str());
    else std::snprintf(Now, sizeof(Now), "Now: edited %s %u of %zu readings differ from the defaults", MiddleDot,
                       FluidPlumeLibrary::CountDifferences(Context.Plume.Gas, ConstructGasParameters()), GasParameterCount);
    CaptionText::Construct(Page, Now, 0.0f, 8.0f);
    const auto&                             Presets = QueryGasPresets();
    std::array<ActionEntry, GasPresetCount> Actions{};
    std::array<std::string, GasPresetCount> Ids{};
    std::array<std::string, GasPresetCount> Captions{};
    for (size_t Index = 0; Index < Presets.size(); ++Index)
    {
        Ids[Index]      = "PlumePreset" + std::to_string(Index);
        Captions[Index] = std::string(Presets[Index].Name);
        Actions[Index]  = { Ids[Index].c_str(), Captions[Index].c_str() };
    }
    const int Pressed = ActionRow::Construct(Page, Actions);
    if (Pressed >= 0) ApplyLook(Context, FluidPlumeLibrary::QueryLooks()[static_cast<size_t>(Pressed)]);
    const ActionEntry Open[1] = { { "PlumeOpenLibraries", "Open the libraries" } };
    if (ActionRow::Construct(Page, Open) == 0) Context.Requests.push_back("library:on");
    const std::string Description = FormatPlumeDescription(ResolveGrade(Context));
    const std::string Counters    = FormatPlumeCounters(Context.Readout);
    CaptionText::Construct(Page, Description.c_str(), 0.0f, 2.0f);
    CaptionText::Construct(Page, Counters.c_str(), 0.0f, 8.0f);
    SectionRows::EndSection(Page);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGES
//------------------------------------------------------------------------------------------------------------------------

const char* FluidPlumePages::ResolveToken(FluidPlumePage Which) noexcept
{
    switch (Which)
    {
    case FluidPlumePage::Domain: return "Domain";
    case FluidPlumePage::Sources: return "Sources";
    case FluidPlumePage::Emitter: return "Emitter";
    case FluidPlumePage::Simulation: return "Simulation";
    case FluidPlumePage::Appearance: return "Appearance";
    case FluidPlumePage::Fire: return "Fire";
    }
    return "Domain";
}

bool FluidPlumePages::ResolvePage(std::string_view Token, FluidPlumePage& OutPage) noexcept
{
    for (size_t Index = 0; Index < FluidPlumePageCount; ++Index)
    {
        const FluidPlumePage Candidate = static_cast<FluidPlumePage>(Index);
        if (Token == ResolveToken(Candidate))
        {
            OutPage = Candidate;
            return true;
        }
    }
    return false;
}

void FluidPlumePages::ConstructWindowPage(ControlColumn& Page, FluidPlumeContext& Context, FluidPlumePage Which)
{
    const char* Eyebrow  = "";
    const char* Title    = "";
    const char* Subtitle = "";
    const char* Action   = nullptr;
    const char* Tooltip  = nullptr;
    switch (Which)
    {
    case FluidPlumePage::Domain:
        Eyebrow  = "SOLVER \xC2\xB7 FLUID PLUME";
        Title    = "Smoke & fire";
        Action   = Context.Plume.Enabled ? "\xE2\x97\x8F Enabled" : "\xE2\x97\x8B Disabled";
        Tooltip  = "Switch the plume on or off";
        Subtitle = "Eulerian smoke, combustion and tyre sources on a lattice that follows the car.";
        break;
    case FluidPlumePage::Sources:
        Eyebrow  = "SOURCES";
        Title    = "Tyre sources";
        Subtitle = "The smoke, heat and soot each tyre gives the plume.";
        break;
    case FluidPlumePage::Emitter:
        Eyebrow  = "SOURCES";
        Title    = "Test emitter";
        Action   = Context.Plume.TestEmitter != 0 ? "\xE2\x97\x8F On" : "\xE2\x97\x8B Off";
        Tooltip  = "Switch the test emitter on or off (E)";
        Subtitle = "A steady smoke source or a burning one, so every slider can be judged without driving.";
        break;
    case FluidPlumePage::Simulation:
        Eyebrow  = "GAS";
        Title    = "Simulation";
        Subtitle = "How the smoke lifts, cools, curls and drifts, and how fast the solver runs.";
        break;
    case FluidPlumePage::Appearance:
        Eyebrow  = "GAS";
        Title    = "Appearance";
        Subtitle = "How the smoke takes the light: its density and colour, its scattering and its detail.";
        break;
    case FluidPlumePage::Fire:
        Eyebrow  = "GAS";
        Title    = "Fire";
        Subtitle = "The flame's strength and temperature, and the light the fire throws.";
        break;
    }
    const std::string Id = std::string("Plume") + ResolveToken(Which) + "Heading";
    if (PageHeading::Construct(Page, Id.c_str(), Eyebrow, Title, Action, Subtitle, Tooltip))
    {
        if (Which == FluidPlumePage::Domain) ToggleEnabled(Context);
        if (Which == FluidPlumePage::Emitter) ToggleEmitter(Context);
    }
    const bool Titled = Context.Titled;
    Context.Titled    = false;
    switch (Which)
    {
    case FluidPlumePage::Domain:
        ConstructDomainCard(Page, Context);
        Context.Titled = true;
        ConstructLooksCard(Page, Context);
        break;
    case FluidPlumePage::Sources: ConstructSourceCard(Page, Context); break;
    case FluidPlumePage::Emitter: ConstructEmitterCard(Page, Context); break;
    case FluidPlumePage::Simulation: ConstructSimulationCard(Page, Context); break;
    case FluidPlumePage::Appearance: ConstructAppearanceCard(Page, Context); break;
    case FluidPlumePage::Fire: ConstructFireCard(Page, Context); break;
    }
    Context.Titled = Titled;
}

} // namespace Frontier

#endif
