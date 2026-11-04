/*==========================================================================================================================================
                                                             WINDINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 The Wind page: the hero over the wind speed now (10 m above the ground at the camera, on the Force Field's clock), the
//    bearings, shear and veer, the quick switches and the five sections (Inspectors/ForceField), as the prototype's Wind page.

#include "Engine/Interface/Inspectors/WindInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/ForceFieldVisualization/ForceFieldVisualization.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"
#include "Engine/Interface/Inspectors/ForceField/ForceFieldPieces.h"
#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"
#include "Engine/PhysicalDynamics/AtmosphericFlow.h"
#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

constexpr const char* SectionLabels[WindInspector::SectionCount] = {"Flow", "Gusts", "Altitude", "Eddies", "Mesoscale"};
constexpr int         UpperTabs                                  = 3;   // [-] Flow · Gusts · Altitude, then Eddies · Mesoscale

// 📝 The wind alone (no localized force) 10 m above the ground at a scene point, on the Force Field's clock.
double ResolveWindSpeed(const WindConfiguration& Wind, const ForceFieldConfiguration* Forces, double East, double North, double Seconds)
{
    ForceFieldSpace         Space;
    ForceFieldConfiguration Controls = Forces != nullptr ? *Forces : ForceFieldConfiguration{};
    WindConfiguration       Alone    = Wind;
    Alone.Enabled                    = true;
    ConfigureAtmosphericFlow(Space, Controls, Alone, Seconds);
    const auto Velocity = EvaluateWorldFlow(Space, {East, North, Wind.GroundHeightMetres + 10.0}, Controls.IncludeTurbulence);
    return std::hypot(Velocity[0], Velocity[1], Velocity[2]);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    HERO
//------------------------------------------------------------------------------------------------------------------------

// 📝 The speed now and over the last twelve seconds (120 evaluations a tenth of a second apart), under the dashed base speed.
void WindInspector::ConstructHero(ControlColumn& Page)
{
    const EnvironmentEditAccess&   Access  = InspectorRuntime::Instance().QueryEnvironmentAccess();
    const ForceFieldConfiguration* Forces  = Access.Forces;
    const double                   Seconds = Forces != nullptr ? Forces->TimelineSeconds : 0.0;
    const double                   East    = Access.CameraReady ? Access.CameraEye[0] : 0.0;
    const double                   North   = Access.CameraReady ? Access.CameraEye[1] : 0.0;
    std::array<float, 120>         History{};
    for (size_t Index = 0; Index < History.size(); ++Index)
        History[Index] = static_cast<float>(ResolveWindSpeed(Parameters, Forces, East, North, Seconds - 0.1 * static_cast<double>(History.size() - 1 - Index)));
    const double   Speed = Parameters.Enabled ? History.back() : 0.0;
    const uint32_t Force = ResolveBeaufortForce(Speed);
    char           Chip[64], Figure[24], Unit[96];
    std::snprintf(Chip, sizeof(Chip), "Beaufort %u \xC2\xB7 %s", Force, ResolveBeaufortName(Force));
    ForceFieldPieces::FormatFigure(Speed, Figure, sizeof(Figure));
    std::snprintf(Unit, sizeof(Unit), "m/s \xC2\xB7 from %.0f\xC2\xB0 \xC2\xB7 gusts to %.1f", EnvironmentReadings::WrapDegrees(Parameters.DirectionDegrees + 180.0f),
                  Parameters.SpeedMetresPerSecond * (1.0 + Parameters.GustStrength));
    ForceFieldPieces::HeroFigures Figures;
    Figures.Chip   = Chip;
    Figures.Tone   = !Parameters.Enabled ? ForceFieldPieces::HeroTone::Waiting
                     : Force >= 10u      ? ForceFieldPieces::HeroTone::Severe
                     : Force >= 8u       ? ForceFieldPieces::HeroTone::Warm
                                         : ForceFieldPieces::HeroTone::Calm;
    Figures.Tag    = Access.CameraReady ? "10 M \xC2\xB7 AT THE CAMERA" : "10 M \xC2\xB7 AT THE ORIGIN";
    Figures.Title  = Parameters.Enabled ? "Wind speed now" : "Wind speed now \xC2\xB7 the Wind is off";
    Figures.Figure = Figure;
    Figures.Unit   = Unit;
    ForceFieldPieces::BeginHero(Page, Figures);
    ForceFieldPieces::SeriesLook Look;
    Look.Target      = Parameters.SpeedMetresPerSecond;
    Look.TargetLabel = "Base";
    Look.Unit        = "m/s";
    Look.Headroom    = 1.35;
    ForceFieldPieces::ConstructSeries(Page, "WindSpeedSeries", History.data(), static_cast<uint32_t>(History.size()),
                                      Parameters.Enabled ? static_cast<uint32_t>(History.size()) : 0u, 0u, Look);
    ForceFieldPieces::EndHero(Page, "Above the dashed base speed: the travelling two-frequency gust wave of the planetary profile, plus the "
                                    "eddies where the Force Field includes them.");
}

void WindInspector::ConstructReadouts(ControlColumn& Page)
{
    char Toward[16], From[16], Shear[16], Veer[16], TowardPoint[32], FromPoint[32];
    const double Travel = EnvironmentReadings::WrapDegrees(Parameters.DirectionDegrees);
    const double Source = EnvironmentReadings::WrapDegrees(Parameters.DirectionDegrees + 180.0f);
    std::snprintf(Toward, sizeof(Toward), "%.0f\xC2\xB0", Travel);
    std::snprintf(From, sizeof(From), "%.0f\xC2\xB0", Source);
    std::snprintf(TowardPoint, sizeof(TowardPoint), "toward %s", SectionRows::ResolveCompassPoint(Travel));
    std::snprintf(FromPoint, sizeof(FromPoint), "from %s", SectionRows::ResolveCompassPoint(Source));
    std::snprintf(Shear, sizeof(Shear), "+%.0f", Parameters.ShearPerKilometre * 100.0);
    std::snprintf(Veer, sizeof(Veer), "%.0f\xC2\xB0", Parameters.VeerDegreesPerKilometre);
    const MetricTile Bearings[2] = {{"Travel", Toward, TowardPoint}, {"Source", From, FromPoint}};
    const MetricTile Profile[2]  = {{"Shear", Shear, "% speed / km"}, {"Veer", Veer, "per km"}};
    MetricTiles::Construct(Page, Bearings, 2, MetricTiles::NoLead);
    MetricTiles::Construct(Page, Profile, 2, MetricTiles::NoLead);
}

// 📝 The Gust wave and Mesoscale tiles zero their strength and restore the last one; Eddies is the Force Field's switch.
bool WindInspector::ConstructQuickControls(ControlColumn& Page)
{
    SectionHeading::Construct(Page, "Quick controls", "WindRecord");
    ForceFieldConfiguration* Forces    = InspectorRuntime::Instance().QueryEnvironmentAccess().Forces;
    bool                     Gusts     = Parameters.GustStrength > 0.0f;
    bool                     Mesoscale = Parameters.MesoscaleStrength > 0.0f;
    bool                     Eddies    = Forces != nullptr && Forces->IncludeTurbulence;
    const bool               Before[3] = {Gusts, Mesoscale, Eddies};
    const QuickTile          Tiles[6]  = {
        {"Wind", "Wind", &Parameters.Enabled, "The whole wind profile"},
        {"Gust wave", "Activity", &Gusts, "The travelling gust wave"},
        {"Eddies", "Swirl", Forces != nullptr ? &Eddies : nullptr, "The curl eddies (the Force Field's switch)"},
        {"Mesoscale", "Globe", &Mesoscale, "The slow variation over tens of kilometres"},
        {"Clouds", "CloudFog", &Parameters.AdvectClouds, "Cloud density travels with the wind"},
        {"Particles", "Sparkles", &Parameters.AdvectParticles, "Particle effects with wind coupling follow it"},
    };
    bool Changed = QuickTiles::Construct(Page, "WindQuick", Tiles, 6, 3);
    if (Gusts != Before[0])
    {
        if (!Gusts) StoredGust = Parameters.GustStrength;
        Parameters.GustStrength = Gusts ? (StoredGust > 0.0f ? StoredGust : 0.25f) : 0.0f;
    }
    if (Mesoscale != Before[1])
    {
        if (!Mesoscale) StoredMesoscale = Parameters.MesoscaleStrength;
        Parameters.MesoscaleStrength = Mesoscale ? (StoredMesoscale > 0.0f ? StoredMesoscale : 0.22f) : 0.0f;
    }
    if (Forces != nullptr && Eddies != Before[2])
    {
        Forces->IncludeTurbulence = Eddies;
        Forces->AdvanceRevision();
        Changed = false;
    }
    return Changed;
}

void WindInspector::ConstructSectionTabs(ControlColumn& Page)
{
    int Upper = ActiveSection < UpperTabs ? ActiveSection : -1;
    if (TabStrip::Construct(Page, "WindSectionsUpper", SectionLabels, UpperTabs, Upper) && Upper >= 0) ActiveSection = Upper;
    int Lower = ActiveSection >= UpperTabs ? ActiveSection - UpperTabs : -1;
    if (TabStrip::Construct(Page, "WindSectionsLower", SectionLabels + UpperTabs, SectionCount - UpperTabs, Lower) && Lower >= 0)
        ActiveSection = UpperTabs + Lower;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE
//------------------------------------------------------------------------------------------------------------------------

void WindInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Advect Clouds", "Global layer", &Parameters.AdvectClouds});
}

void WindInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveNative();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool          Changed = PageHeading::Construct(Page, "WindPower", "FORCES / FORCE FIELD", "Wind", Parameters.Enabled ? "\xE2\x97\x8F On" : "\xE2\x97\x8B Off",
                                                   "The planetary profile the Force Field carries as its guest: a direction of travel, a pulse "
                                                   "of gusts, Ekman shear and veer through altitude, and curl eddies.",
                                                   "Switch the wind");
    if (Changed) Parameters.Enabled = !Parameters.Enabled;
    ConstructHero(Page);
    ConstructReadouts(Page);
    Changed |= ConstructQuickControls(Page);
    ConstructSectionTabs(Page);
    if (!EddyPreview) EddyPreview = std::make_shared<ForceFieldVisualization>();
    const EnvironmentEditAccess& Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    WindPageReading              Reading;
    Reading.Forces          = Access.Forces;
    Reading.TimelineSeconds = Access.Forces != nullptr ? Access.Forces->TimelineSeconds : 0.0;
    Reading.EddyPreview     = EddyPreview.get();
    switch (ActiveSection)
    {
    case 0: Changed |= WindFlowSection::Construct(Page, Parameters, Reading); break;
    case 1: Changed |= WindGustSection::Construct(Page, Parameters, Reading); break;
    case 2: Changed |= WindAltitudeSection::Construct(Page, Parameters, Reading); break;
    case 3: Changed |= WindEddySection::Construct(Page, Parameters, Reading); break;
    default: Changed |= WindMesoscaleSection::Construct(Page, Parameters, Reading); break;
    }
    if (NextButton::Construct(Page, "OpenForceField", "Open the Force Field \xE2\x86\x97", Access.Forces != nullptr, "Add a Force Field record first"))
        InspectorRuntime::Instance().RequestSelection("ForceField");
    CaptionText::Construct(Page, "The Wind is the Force Field's guest: the field's view, receivers and localized forces are on its page. A "
                                 "switched-off field suspends the wind without changing these settings.");
    FooterRow::Construct(Page, "Levelspace record \xC2\xB7 Wind", "Nested under the Force Field");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void WindInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void WindInspector::ConstructInspector() {}
} // namespace Frontier

#endif
