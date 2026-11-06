/*==========================================================================================================================================
                                                        FLUIDEDITORPAGES.CPP
==========================================================================================================================================*/
// 🧩 The Fluid editor's pages on a FluidVolume record: FluidPanel.js ConstructInspector group for group (the same keys in the same order, the same
//    badges and hints, the same groups open and closed), and its AddObject, ToggleObject and LoadPreset as the edits the Outliner and the library share.
//    Every control is a row of the catalogue (label, range, step, unit, options) on the real Inspector control its form asks for.

#include "Engine/Interface/FluidEditor/FluidEditorPages.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ActionRow.h"
#include "Engine/Interface/Controls/CaptionText.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/DisclosureRow.h"
#include "Engine/Interface/Controls/MetricTiles.h"
#include "Engine/Interface/Controls/PageHeading.h"
#include "Engine/Interface/Controls/SectionRows.h"
#include "Engine/Interface/Controls/SelectRow.h"
#include "Engine/Interface/Controls/TabStrip.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

using namespace FluidEditor;

constexpr const char* MiddleDot = "\xC2\xB7";
constexpr const char* TimesSign = "\xC3\x97";
constexpr const char* Degrees   = "\xC2\xB0";

// 📝 One group of a page: FluidPanel.js AddGroup(Title, Keys, Badge, Open, Hint).
struct Group
{
    const char*                   Title = "";
    const char*                   Badge = "";
    bool                          Open  = true;
    const char*                   Hint  = "";
    std::vector<std::string_view> Keys;
};

Group ConstructEmission()
{
    return { "Emission", "SOURCE", true, "A continuous source at the domain\xE2\x80\x99s X/Z centre. Height and radius are live simulation parameters.",
             { "emitterEnabled", "emitterRate", "emitterRadius", "emitterHeight" } };
}

Group ConstructCollider(bool Open, const char* Hint)
{
    return { "Collider", "SDF", Open, Hint,
             { "obstacleType", "obstacleX", "obstacleY", "obstacleZ", "obstacleRadius", "colliderAutoMove", "colliderSpeed" } };
}

Group ConstructSunLight(bool Open)
{
    return { "Directional light", "LIGHT", Open, "", { "sunIntensity", "sunAzimuth", "sunElevation", "ambientIntensity" } };
}

// 📝 The groups of a part's tab, in the prototype's order.
std::vector<Group> ResolveGroups(FluidPart Part, FluidTab Tab)
{
    std::vector<Group> Groups;
    if (Tab == FluidTab::Source)
    {
        if (Part == FluidPart::Collider)
            Groups.push_back(ConstructCollider(true, "Position is normalized within the domain. The collider changes the simulated flow without restarting it."));
        else
        {
            Groups.push_back(ConstructEmission());
            Groups.push_back({ "Fuel & temperature", "INJECTION", true, "", { "emitterTemperature", "emitterFuel", "emitterSmoke" } });
            Groups.push_back({ "Velocity", "FLOW", true, "", { "emitterUpwardVelocity", "emitterSwirl" } });
        }
        Groups.push_back({ "Burst generator", "IMPULSE", false, "",
                           { "shrapnelEnabled", "blastStrength", "blastRadius", "blastTemperature", "blastFuel", "blastSmoke", "blastLobes" } });
        if (Part != FluidPart::Collider) Groups.push_back(ConstructCollider(false, ""));
    }
    else if (Tab == FluidTab::Simulation)
    {
        Groups.push_back({ "Domain", "3D", true, "", { "boundsWidth", "boundsHeight", "dynamicBounds", "dynamicBoundsMax" } });
        Groups.push_back({ "Voxel grid", "SOLVER", true, "Changing voxel resolution rebuilds the grid and restarts the simulation. Other properties update live.",
                           { "gridResolution", "pressureIterations", "macCormackAdvection", "enclosedBox", "timeScale" } });
        Groups.push_back({ "Forces & turbulence", "FLOW", false, "",
                           { "vorticityConfinement", "buoyancy", "smokeWeight", "turbulenceStrength", "turbulenceScale", "windX", "windZ", "velocityDamping" } });
        Groups.push_back({ "Combustion", "THERMAL", false, "",
                           { "burnRate", "burnHeat", "sootGeneration", "combustionExpansion", "coolingRate", "smokeDissipation" } });
    }
    else
    {
        if (Part == FluidPart::Sun) Groups.push_back(ConstructSunLight(true));
        Groups.push_back({ "Volume shading", "VOLUME", true, "", { "colorPalette", "fireIntensity", "densityExtinction", "smokeAlbedo", "exposure" } });
        if (Part != FluidPart::Sun) Groups.push_back(ConstructSunLight(false));
        Groups.push_back({ "Scattering & glow", "OPTICS", false, "",
                           { "bloomIntensity", "godRaysIntensity", "shockwaveStrength", "temperatureScale", "internalScattering", "shadowDensity", "phaseAnisotropy" } });
        Groups.push_back({ "Render quality", "GPU", false, "", { "raymarchSteps", "shadowSteps", "renderScale", "autoGpuGovernor", "voxelQuantization" } });
        Groups.push_back({ "Embers & ash", "PARTICLES", false, "", { "showEmbers", "emberCount", "emberSize", "emberIntensity", "emberLifetime", "emberAshiness" } });
        Groups.push_back({ "Overlays & diagnostics", "DEBUG", false, "",
                           { "showBoundingBox", "showVoxelGridLines", "showFloorGrid", "showActiveVoxelCells", "showAtlasMinimap", "atlasMinimapField", "renderChannel",
                             "sliceAxis", "slicePosition" } });
    }
    return Groups;
}

// 📝 A group's open flag: the person's, or the prototype's default for it. The window forgets the person's choices when the part or the tab changes, as the
//    prototype's details elements are built afresh then.
bool ResolveOpen(FluidEditorMemory& Memory, FluidPart Part, const Group& Entry)
{
    const std::string Name = std::string(FluidEditorPages::ResolveToken(Part)) + "/" + Entry.Title;
    for (const auto& Known : Memory.Groups)
    {
        if (Known.first == Name) return Known.second;
    }
    return Entry.Open;
}

void ConfigureOpen(FluidEditorMemory& Memory, FluidPart Part, const Group& Entry, bool Open)
{
    const std::string Name = std::string(FluidEditorPages::ResolveToken(Part)) + "/" + Entry.Title;
    for (auto& Known : Memory.Groups)
    {
        if (Known.first == Name)
        {
            Known.second = Open;
            return;
        }
    }
    Memory.Groups.emplace_back(Name, Open);
}

// 📝 The reading a slider commits is the decimal its figure shows (the prototype's range input commits that decimal), not the float the slider moved.
float RoundToStep(float Reading, float Step)
{
    const int   Digits = Step >= 1.0f ? 0 : Step >= 0.1f ? 1 : Step >= 0.01f ? 2 : 3;
    const float Place  = std::pow(10.0f, static_cast<float>(Digits));
    return std::round(Reading * Place) / Place;
}

// 📝 One setting as its control: a range as a slider, a toggle as a switch, a choice as a select. True when the person changed it.
bool ConstructSetting(ControlColumn& Page, FluidEditorContext& Context, std::string_view Key)
{
    const std::string Spelt(Key);
    const ControlRow* Row = ReadControl(Spelt.c_str());
    if (Row == nullptr) return false;
    GasSettings& Gas     = Context.Volume.Gas;
    float        Reading = 0.0f;
    if (!ReadSetting(Gas, Row->Key, Reading)) return false;
    const std::string Id = "FluidSetting" + Spelt;
    float             Next    = Reading;
    bool              Changed = false;
    if (Row->Form == ControlForm::Range)
    {
        float Quantity = std::clamp(Reading, Row->Low, Row->High);
        if (SectionRows::Amount(Page, Id.c_str(), Row->Label, Quantity, Row->Low, Row->High, Row->Step, Row->Unit))
        {
            Next    = RoundToStep(Quantity, Row->Step);
            Changed = true;
        }
    }
    else if (Row->Form == ControlForm::Toggle)
    {
        bool On = Reading >= 0.5f;
        if (SectionRows::Switch(Page, Id.c_str(), Row->Label, On))
        {
            Next    = On ? 1.0f : 0.0f;
            Changed = true;
        }
    }
    else
    {
        std::array<const char*, 16> Labels{};
        const int                   Count = static_cast<int>(std::min<uint32_t>(Row->ChoiceCount, static_cast<uint32_t>(Labels.size())));
        int                         Index = 0;
        for (int Option = 0; Option < Count; ++Option)
        {
            Labels[static_cast<size_t>(Option)] = Row->Choices[Option].Label;
            if (Row->Choices[Option].Ordinal == static_cast<int>(std::lround(Reading))) Index = Option;
        }
        if (SelectRow::Construct(Page, Id.c_str(), Row->Label, Labels.data(), Count, Index))
        {
            Next    = static_cast<float>(Row->Choices[Index].Ordinal);
            Changed = true;
        }
    }
    if (!Changed) return false;
    (void)WriteSetting(Gas, Row->Key, Next);
    float Written = Next;
    (void)ReadSetting(Gas, Row->Key, Written);
    if (Written == Reading) return false;
    Context.Volume.AdvanceRevision();
    // 📝 A new lattice edge rebuilds the grid and restarts the simulation (ApplyParameter gridResolution).
    if (Spelt == "gridResolution")
    {
        Context.Requests.push_back("restart");
        Context.Requests.push_back("noted:grid:" + std::to_string(Gas.LatticeResolution));
    }
    return true;
}

// 📝 A group as the prototype's details element: its summary (the title and the badge), then its card of controls when it is open.
bool ConstructGroup(ControlColumn& Page, FluidEditorContext& Context, FluidPart Part, const Group& Entry)
{
    bool        Open = ResolveOpen(Context.Memory, Part, Entry);
    std::string Caption = Entry.Title;
    if (Entry.Badge[0] != '\0') Caption += std::string("   ") + MiddleDot + "   " + Entry.Badge;
    const std::string Id = std::string("FluidGroup") + Entry.Title;
    const bool        Was = Open;
    (void)DisclosureRow::Construct(Page, Id.c_str(), Caption.c_str(), Open);
    if (Open != Was) ConfigureOpen(Context.Memory, Part, Entry, Open);
    if (!Open) return false;
    bool Moved = false;
    SectionRows::BeginSection(Page, nullptr, nullptr, nullptr, Entry.Hint);
    for (std::string_view Key : Entry.Keys) Moved = ConstructSetting(Page, Context, Key) || Moved;
    SectionRows::EndSection(Page);
    return Moved;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOKENS
//------------------------------------------------------------------------------------------------------------------------

const char* FluidEditorPages::ResolveToken(FluidPart Part) noexcept
{
    switch (Part)
    {
    case FluidPart::Domain: return "Domain";
    case FluidPart::Emitter: return "Emitter";
    case FluidPart::Collider: return "Collider";
    case FluidPart::Sun: return "Sun";
    }
    return "Domain";
}

bool FluidEditorPages::ResolvePart(std::string_view Token, FluidPart& OutPart) noexcept
{
    for (size_t Index = 0; Index < FluidPartCount; ++Index)
    {
        const FluidPart Candidate = static_cast<FluidPart>(Index);
        if (Token == ResolveToken(Candidate))
        {
            OutPart = Candidate;
            return true;
        }
    }
    return false;
}

const char* FluidEditorPages::ResolveTabToken(FluidTab Tab) noexcept
{
    switch (Tab)
    {
    case FluidTab::Source: return "source";
    case FluidTab::Simulation: return "simulation";
    case FluidTab::Rendering: return "rendering";
    }
    return "source";
}

bool FluidEditorPages::ResolveTab(std::string_view Token, FluidTab& OutTab) noexcept
{
    for (size_t Index = 0; Index < FluidTabCount; ++Index)
    {
        const FluidTab Candidate = static_cast<FluidTab>(Index);
        if (Token == ResolveTabToken(Candidate))
        {
            OutTab = Candidate;
            return true;
        }
    }
    return false;
}

FluidTab FluidEditorPages::ResolveOpeningTab(FluidPart Part) noexcept
{
    return Part == FluidPart::Domain ? FluidTab::Simulation : Part == FluidPart::Sun ? FluidTab::Rendering : FluidTab::Source;
}

std::vector<std::string_view> FluidEditorPages::ResolveVisibleKeys(FluidPart Part, FluidTab Tab)
{
    std::vector<std::string_view> Keys;
    for (const Group& Entry : ResolveGroups(Part, Tab)) Keys.insert(Keys.end(), Entry.Keys.begin(), Entry.Keys.end());
    return Keys;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    READINGS
//------------------------------------------------------------------------------------------------------------------------

const char* FluidEditorPages::ResolveColliderLabel(int32_t ObstacleType) noexcept
{
    // 📝 The prototype's OBSTACLE_TYPES labels, as the catalogue carries them; a collider that is off is named for the sphere it would come back as.
    const ControlRow* Row = ReadControl("obstacleType");
    if (Row == nullptr || Row->ChoiceCount < 2u) return "";
    for (uint32_t Option = 0; Option < Row->ChoiceCount; ++Option)
    {
        if (Row->Choices[Option].Ordinal == ObstacleType) return Row->Choices[Option].Label;
    }
    return Row->Choices[1].Label;
}

std::string FluidEditorPages::FormatLattice(int32_t Resolution)
{
    return std::to_string(Resolution) + "\xC2\xB3";
}

std::string FluidEditorPages::FormatVoxels(int32_t Resolution)
{
    const std::string Digits = std::to_string(static_cast<long long>(Resolution) * Resolution * Resolution);
    std::string       Spelt;
    for (size_t Index = 0; Index < Digits.size(); ++Index)
    {
        if (Index != 0 && (Digits.size() - Index) % 3 == 0) Spelt += ',';
        Spelt += Digits[Index];
    }
    return Spelt;
}

const char* FluidEditorPages::ResolveBurstLabel(std::string_view Pattern) noexcept
{
    if (Pattern == "shrapnel") return "Shrapnel";
    if (Pattern == "salvo") return "Cluster salvo";
    if (Pattern == "collide") return "Colliding waves";
    if (Pattern == "mushroom") return "Mushroom cloud";
    return "Single burst";
}

std::string FluidEditorPages::FormatTimecode(double Seconds)
{
    const long long Milliseconds = static_cast<long long>(std::floor(std::max(0.0, Seconds) * 1000.0 + 0.5));
    char            Text[32];
    std::snprintf(Text, sizeof(Text), "%02lld:%02lld.%03lld", Milliseconds / 60000, (Milliseconds / 1000) % 60, Milliseconds % 1000);
    return Text;
}

bool FluidEditorPages::QueryPartEnabled(const FluidVolumeConfiguration& Volume, FluidPart Part) noexcept
{
    switch (Part)
    {
    case FluidPart::Domain: return true;
    case FluidPart::Emitter: return Volume.Gas.EmitterEnabled;
    case FluidPart::Collider: return Volume.Gas.ObstacleType > 0;
    case FluidPart::Sun: return Volume.Gas.SunIntensity > 0.0f;
    }
    return true;
}

bool FluidEditorPages::QueryPartListed(const FluidVolumeConfiguration& Volume, FluidPart Part, FluidPart Selected) noexcept
{
    return Part != FluidPart::Collider || Volume.Gas.ObstacleType > 0 || Selected == FluidPart::Collider;
}

std::string FluidEditorPages::ResolvePartName(const FluidEditorMemory& Memory, FluidPart Part)
{
    switch (Part)
    {
    case FluidPart::Domain: return "Gas domain";
    case FluidPart::Emitter: return Memory.EmitterName;
    case FluidPart::Collider: return Memory.ColliderName;
    case FluidPart::Sun: return "Directional light";
    }
    return "Gas domain";
}

const char* FluidEditorPages::ResolvePartKind(FluidPart Part) noexcept
{
    switch (Part)
    {
    case FluidPart::Domain: return "EULERIAN VOLUME";
    case FluidPart::Emitter: return "CONTINUOUS SOURCE";
    case FluidPart::Collider: return "SIGNED DISTANCE COLLIDER";
    case FluidPart::Sun: return "DIRECTIONAL LIGHT";
    }
    return "";
}

const char* FluidEditorPages::ResolvePartGlyph(const FluidVolumeConfiguration& Volume, FluidPart Part) noexcept
{
    switch (Part)
    {
    case FluidPart::Domain: return "Box";
    case FluidPart::Emitter: return Volume.Gas.EmitterFuel == 0.0f ? "CloudFog" : "Flame";
    case FluidPart::Collider: return "Sphere";
    case FluidPart::Sun: return "Sun";
    }
    return "Box";
}

std::string FluidEditorPages::FormatPartFigures(const FluidVolumeConfiguration& Volume, FluidPart Part)
{
    const GasSettings& Gas = Volume.Gas;
    char               Text[128];
    switch (Part)
    {
    case FluidPart::Domain:
        std::snprintf(Text, sizeof(Text), "%s %s %.2f %s %.2f %s %.2f m", FormatLattice(Gas.LatticeResolution).c_str(), MiddleDot, Gas.BoundsWidth, TimesSign, Gas.BoundsHeight,
                      TimesSign, Gas.BoundsWidth);
        break;
    case FluidPart::Emitter:
        std::snprintf(Text, sizeof(Text), "%s %s rate %.2f%s %s temperature %.1f", Gas.EmitterFuel == 0.0f ? "Smoke" : "Fire", MiddleDot, Gas.EmitterRate, TimesSign, MiddleDot,
                      Gas.EmitterTemperature);
        break;
    case FluidPart::Collider:
        std::snprintf(Text, sizeof(Text), "%s %s radius %.2f m", ResolveColliderLabel(Gas.ObstacleType > 0 ? Gas.ObstacleType : 1), MiddleDot, Gas.ObstacleRadius);
        break;
    case FluidPart::Sun:
        std::snprintf(Text, sizeof(Text), "Intensity %.1f %s az %.0f%s el %.0f%s", Gas.SunIntensity, MiddleDot, Gas.SunAzimuth, Degrees, Gas.SunElevation, Degrees);
        break;
    }
    return Text;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EDITS
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorPages::ApplyPreset(FluidEditorContext& Context, std::string_view Identity)
{
    const std::string Spelt(Identity);
    const GasPreset*  Preset = FindGasPreset(Spelt.c_str());
    if (Preset == nullptr) return;
    // 📝 LoadPreset: the preset's readings on the editor's initial ones, the emitter and the collider named for what they are now, the camera framed on the
    //    new domain, the part the person most likely wants selected, and the burst some presets open on.
    Context.Volume.Gas    = ConstructPresetSettings(Preset->Identity);
    Context.Volume.Preset = Preset->Identity;
    const GasSettings& Gas = Context.Volume.Gas;
    Context.Memory.EmitterName  = Gas.EmitterFuel == 0.0f ? "Smoke emitter" : "Fire emitter";
    Context.Memory.ColliderName = std::string(ResolveColliderLabel(Gas.ObstacleType > 0 ? Gas.ObstacleType : 1)) + " collider";
    Context.Volume.AdvanceRevision();
    Context.Requests.push_back("noted:preset:" + Spelt);
    Context.Requests.push_back("restart");
    Context.Requests.push_back("camera:fit-settings");
    Context.Requests.push_back(Gas.EmitterEnabled ? "select:Emitter" : "select:Domain");
    if (Preset->DetonateOnLoad) Context.Requests.push_back("burst:burst");
}

void FluidEditorPages::RestoreTab(FluidEditorContext& Context, FluidPart Part, FluidTab Tab)
{
    // 📝 "Reset this inspector tab": each setting the tab shows goes back to the editor's initial reading (InitialParameters).
    const GasSettings Initial{};
    size_t            Restored = 0;
    for (std::string_view Key : ResolveVisibleKeys(Part, Tab))
    {
        const std::string Spelt(Key);
        float             Reading = 0.0f;
        if (ReadSetting(Initial, Spelt.c_str(), Reading) && WriteSetting(Context.Volume.Gas, Spelt.c_str(), Reading)) ++Restored;
    }
    Context.Volume.AdvanceRevision();
    Context.Requests.push_back("restart");
    Context.Requests.push_back("noted:defaults:" + std::to_string(Restored));
}

void FluidEditorPages::ToggleEnabled(FluidEditorContext& Context)
{
    Context.Volume.Enabled = !Context.Volume.Enabled;
    Context.Volume.AdvanceRevision();
    Context.Requests.push_back(Context.Volume.Enabled ? "noted:enabled:on" : "noted:enabled:off");
}

void FluidEditorPages::TogglePart(FluidEditorContext& Context, FluidPart Part)
{
    GasSettings& Gas = Context.Volume.Gas;
    if (Part == FluidPart::Emitter) Gas.EmitterEnabled = !Gas.EmitterEnabled;
    else if (Part == FluidPart::Collider)
    {
        if (Gas.ObstacleType != 0)
        {
            Context.Memory.SavedCollider = Gas.ObstacleType;
            Gas.ObstacleType             = 0;
        }
        else
            Gas.ObstacleType = Context.Memory.SavedCollider != 0 ? Context.Memory.SavedCollider : 1;
    }
    else if (Part == FluidPart::Sun)
    {
        if (Gas.SunIntensity != 0.0f)
        {
            Context.Memory.SavedSun = Gas.SunIntensity;
            Gas.SunIntensity        = 0.0f;
        }
        else
            Gas.SunIntensity = Context.Memory.SavedSun != 0.0f ? Context.Memory.SavedSun : 2.2f;
    }
    else
        return;
    Context.Volume.AdvanceRevision();
    Context.Requests.push_back(std::string("noted:part:") + ResolveToken(Part) + ":" + (QueryPartEnabled(Context.Volume, Part) ? "on" : "off"));
}

bool FluidEditorPages::AddPart(FluidEditorContext& Context, std::string_view Kind)
{
    GasSettings& Gas = Context.Volume.Gas;
    if (Kind == "fire" || Kind == "smoke")
    {
        const bool Fire             = Kind == "fire";
        Gas.EmitterEnabled          = true;
        Gas.EmitterFuel             = Fire ? 2.8f : 0.0f;
        Gas.EmitterTemperature      = Fire ? 3.5f : 0.8f;
        Gas.EmitterSmoke            = Fire ? 1.4f : 2.5f;
        Context.Memory.EmitterName  = Fire ? "Fire emitter" : "Smoke emitter";
        Context.Requests.push_back("select:Emitter");
    }
    else if (Kind == "sphere" || Kind == "cylinder" || Kind == "slab")
    {
        Gas.ObstacleType            = Kind == "sphere" ? 1 : Kind == "cylinder" ? 2 : 4;
        Gas.ObstacleY               = 0.35f;
        Gas.ColliderAutoMove        = false;
        Context.Memory.ColliderName = Kind == "sphere" ? "Sphere collider" : Kind == "cylinder" ? "Cylinder collider" : "Deflector slab";
        Context.Requests.push_back("select:Collider");
    }
    else
        return false;
    Context.Volume.AdvanceRevision();
    Context.Requests.push_back("noted:add:" + std::string(Kind));
    return true;
}

void FluidEditorPages::RemovePart(FluidEditorContext& Context, FluidPart Part)
{
    GasSettings& Gas = Context.Volume.Gas;
    if (Part == FluidPart::Emitter) Gas.EmitterEnabled = false;
    else if (Part == FluidPart::Collider)
    {
        Gas.ObstacleType = 0;
        Context.Requests.push_back("select:Domain");
    }
    else
        return;
    Context.Volume.AdvanceRevision();
    Context.Requests.push_back(std::string("noted:part:") + ResolveToken(Part) + ":off");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGES
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorPages::ConstructWindowPage(ControlColumn& Page, FluidEditorContext& Context, FluidPart Part, FluidTab& Tab)
{
    const char* Subtitle = "";
    switch (Part)
    {
    case FluidPart::Domain: Subtitle = "A box of fire, smoke or a blast, solved on a lattice that stays where it is put."; break;
    case FluidPart::Emitter: Subtitle = "Where the flame or the smoke comes from, and how hard it is fed."; break;
    case FluidPart::Collider: Subtitle = "A signed-distance body the flow is deflected around."; break;
    case FluidPart::Sun: Subtitle = "The light that shades the volume, and the ambient it sits in."; break;
    }
    const bool        On      = Part == FluidPart::Domain ? Context.Volume.Enabled : QueryPartEnabled(Context.Volume, Part);
    const char*       Action  = On ? "\xE2\x97\x8F Enabled" : "\xE2\x97\x8B Disabled";
    const std::string Title   = ResolvePartName(Context.Memory, Part);
    const std::string Heading = std::string("Fluid") + ResolveToken(Part) + "Heading";
    if (PageHeading::Construct(Page, Heading.c_str(), ResolvePartKind(Part), Title.c_str(), Action, Subtitle,
                               Part == FluidPart::Domain ? "Switch the volume on or off" : "Switch this part on or off"))
    {
        if (Part == FluidPart::Domain) ToggleEnabled(Context);
        else TogglePart(Context, Part);
    }

    static constexpr const char* Labels[FluidTabCount] = { "Source", "Simulation", "Rendering" };
    int                          Chosen                = static_cast<int>(Tab);
    if (TabStrip::Construct(Page, "FluidTabs", Labels, static_cast<int>(FluidTabCount), Chosen))
    {
        Tab = static_cast<FluidTab>(Chosen);
        Context.Memory.Groups.clear();
        Context.Requests.push_back(std::string("tab:") + ResolveTabToken(Tab));
    }

    // 📝 The solver's own figures lead the domain's Simulation tab: they are what the settings below act on.
    if (Part == FluidPart::Domain && Tab == FluidTab::Simulation)
    {
        const FluidVolumeReadout* Readout = Context.Readout;
        const GasSettings&        Gas     = Context.Volume.Gas;
        const std::string         Lattice = FormatLattice(Readout != nullptr && Readout->Resolution != 0 ? static_cast<int32_t>(Readout->Resolution) : Gas.LatticeResolution);
        const std::string         Steps   = Readout != nullptr ? std::to_string(Readout->Steps) : "\xE2\x80\x93";
        const std::string         Clock   = Readout != nullptr ? FormatTimecode(Readout->SimulationSeconds) : "\xE2\x80\x93";
        const MetricTile          Tiles[3] = { { "Lattice", Lattice, "" }, { "Steps", Steps, "" }, { "Time", Clock, "" } };
        MetricTiles::Construct(Page, Tiles, 3, MetricTiles::NoLead);
        char Surge[48] = "no solver attached";
        if (Readout != nullptr) std::snprintf(Surge, sizeof(Surge), "domain now %.2f m wide", Readout->BoundsWidth);
        const std::string Voxels = FormatVoxels(Gas.LatticeResolution) + " voxels " + MiddleDot + " " + Surge;
        CaptionText::Construct(Page, Voxels.c_str(), 0.0f, 12.0f);
    }

    for (const Group& Entry : ResolveGroups(Part, Tab)) (void)ConstructGroup(Page, Context, Part, Entry);

    // 📝 The foot of a Source tab: the way back to defaults, and Disable source / Remove collider (the prototype's #remove-object).
    std::array<ActionEntry, 2> Actions{ ActionEntry{ "FluidResetTab", "Reset this tab", true, "Each setting this tab shows goes back to the editor's initial reading" } };
    size_t                     Count = 1;
    if (Tab == FluidTab::Source && (Part == FluidPart::Emitter || Part == FluidPart::Collider))
    {
        Actions[Count++] = { "FluidRemovePart", Part == FluidPart::Emitter ? "Disable source" : "Remove collider", true, nullptr };
    }
    const int Pressed = ActionRow::Construct(Page, std::span<const ActionEntry>(Actions.data(), Count));
    if (Pressed == 0) RestoreTab(Context, Part, Tab);
    if (Pressed == 1) RemovePart(Context, Part);
}

} // namespace Frontier

#endif
