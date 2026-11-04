/*==========================================================================================================================================
                                                         FORCESOURCEINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 One localized force: the type's radial profile, its status, and four tab-switched sections (Strength, Reach, Timing,
//    Receivers) over the ForceSource record, as the approved prototype's source inspector.

#include "Engine/Interface/Inspectors/ForceSourceInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Controls/SectionRows.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier
{

namespace
{

constexpr const char* SectionLabels[ForceSourceInspector::SectionCount] = {"Strength", "Reach", "Timing", "Receivers"};
constexpr const char* ReachLabels[]      = {"Everywhere", "Sphere", "Column", "Box"};
constexpr const char* FalloffLabels[]    = {"None", "Linear", "Smooth", "Inverse\xC2\xB2"};
constexpr const char* ActivationLabels[] = {"Always", "Window", "Trigger"};
constexpr const char* SpinLabels[]       = {"Cyclonic", "Anticyclonic"};

// 📝 The Force Field's timeline clock (Fire stamps it; the status reads it); 0 before the field record is found.
double ResolveTimelineSeconds() noexcept
{
    const EnvironmentEditAccess& Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    return Access.Forces != nullptr ? Access.Forces->TimelineSeconds : 0.0;
}

ForceWindReading ResolveWindReading() noexcept
{
    const EnvironmentEditAccess& Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    return Access.Wind != nullptr ? ResolveForceWindReading(*Access.Wind) : ForceWindReading{};
}

ImU32 ResolveContributionColour(ForceContribution Contribution, float Alpha = 1.0f) noexcept
{
    const uint32_t Rgb = ResolveForceContributionDescriptor(Contribution).ColourRgb;
    return ResolveAlpha(IM_COL32((Rgb >> 16u) & 255u, (Rgb >> 8u) & 255u, Rgb & 255u, 255), Alpha);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINER ROW
//------------------------------------------------------------------------------------------------------------------------

const char* ForceSourceInspector::QueryEyebrowText() const noexcept
{
    switch (ResolveForceSourceDescriptor(Parameters.Category).Contribution)
    {
    case ForceContribution::Accelerate: return "FORCE FIELD / ACCELERATE";
    case ForceContribution::Damp: return "FORCE FIELD / DAMP";
    default: return "FORCE FIELD / FLOW";
    }
}

const char* ForceSourceInspector::QuerySectionNote() const noexcept
{
    return ResolveForceSourceStatusLabel(ResolveForceActivation(Parameters, ResolveTimelineSeconds()).Status);
}

std::string ForceSourceInspector::QueryDirectoryReadout() const
{
    const ForceSourceDescriptor& Descriptor = ResolveForceSourceDescriptor(Parameters.Category);
    char Text[64];
    std::snprintf(Text, sizeof(Text), "%.*f %s", Parameters.Strength >= 100.0 ? 0 : 1, Parameters.Strength,
                  ResolveForceContributionDescriptor(Descriptor.Contribution).Unit);
    return Text;
}

const char* ForceSourceInspector::QueryDirectoryGlyph() const noexcept
{
    return ResolveForceSourceDescriptor(Parameters.Category).Glyph;
}

bool ForceSourceInspector::QueryViewTarget(InspectorViewTarget& View) const noexcept
{
    View.Position = {static_cast<float>(Parameters.CentreMetres[0]), static_cast<float>(Parameters.CentreMetres[1]),
                     static_cast<float>(Parameters.CentreMetres[2])};
    View.Radius   = static_cast<float>(std::clamp(Parameters.Reach == ForceReach::Box
                                                      ? std::max(Parameters.HalfExtentsMetres[0], Parameters.HalfExtentsMetres[1])
                                                      : Parameters.ReachMetres,
                                                  5.0, 50000.0));
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SECTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ForceSourceInspector::ConstructStrength(ControlColumn& Page)
{
    using namespace SectionRows;
    const ForceSourceDescriptor& Descriptor = ResolveForceSourceDescriptor(Parameters.Category);
    bool Changed = false;
    BeginSection(Page, "Strength", Descriptor.Glyph, ResolveForceContributionDescriptor(Descriptor.Contribution).Label, Descriptor.Blurb);
    for (const ForceParameterDescriptor& Parameter : Descriptor.Parameters)
    {
        double& Setting = Parameters.*(Parameter.Member);
        if (Parameter.Distance)
            Changed |= Distance(Page, Parameter.Identifier, Parameter.Label, Setting, Parameter.Minimum, Parameter.Maximum, Parameter.Step);
        else
            Changed |= Measure(Page, Parameter.Identifier, Parameter.Label, Setting, Parameter.Minimum, Parameter.Maximum, Parameter.Step,
                               Parameter.Unit);
    }
    for (const ForceBearingDescriptor& Direction : Descriptor.Bearings)
        Changed |= Bearing(Page, Direction.Identifier, Direction.Label, Parameters.*(Direction.Member));
    if (Descriptor.Spin)
    {
        SubHeading(Page, "Spin");
        int Spin = Parameters.Spin < 0.0 ? 1 : 0;
        if (TabStrip::Construct(Page, "Spin", SpinLabels, 2, Spin))
        {
            Parameters.Spin = Spin == 1 ? -1.0 : 1.0;
            Changed         = true;
        }
        CaptionText::Construct(Page, "Cyclonic turns counter-clockwise seen from above, as northern-hemisphere tornadoes mostly do.", 0.0f, 10.0f);
    }
    EndSection(Page);
    return Changed;
}

bool ForceSourceInspector::ConstructReach(ControlColumn& Page)
{
    using namespace SectionRows;
    bool Changed = false;
    BeginSection(Page, "Reach", "Target", ReachLabels[static_cast<int>(Parameters.Reach)],
                 "Where the force acts and how it fades toward the edge. A box turns with the bearing; a column stands on its "
                 "centre and tapers above its height.");
    int Reach = static_cast<int>(Parameters.Reach);
    if (TabStrip::Construct(Page, "ReachShape", ReachLabels, 4, Reach))
    {
        Parameters.Reach = static_cast<ForceReach>(Reach);
        Changed          = true;
    }
    float Centre[3] = {static_cast<float>(Parameters.CentreMetres[0]), static_cast<float>(Parameters.CentreMetres[1]),
                       static_cast<float>(Parameters.CentreMetres[2])};
    if (AxisFields::Construct(Page, "SourceCentre", "Centre \xC2\xB7 east / north / up (m)", Centre, {-400000.0f, -400000.0f, -1000.0f},
                              {400000.0f, 400000.0f, 40000.0f}))
    {
        for (size_t Axis = 0; Axis < 3; ++Axis) Parameters.CentreMetres[Axis] = Centre[Axis];
        Changed = true;
    }
    if (Parameters.Reach == ForceReach::Sphere || Parameters.Reach == ForceReach::Column)
        Changed |= Distance(Page, "ReachRadius", "Radius", Parameters.ReachMetres, 1.0, 400000.0);
    if (Parameters.Reach == ForceReach::Column || Parameters.Reach == ForceReach::Box)
        Changed |= Distance(Page, "ReachHeight", "Height", Parameters.HeightMetres, 1.0, 40000.0);
    if (Parameters.Reach == ForceReach::Box && Parameters.Category != ForceSourceCategory::JetCorridor &&
        Parameters.Category != ForceSourceCategory::VehicleWake)
    {
        Changed |= Distance(Page, "ReachAcross", "Half extent across", Parameters.HalfExtentsMetres[0], 0.5, 200000.0);
        Changed |= Distance(Page, "ReachAlong", "Half extent along", Parameters.HalfExtentsMetres[1], 0.5, 200000.0);
        if (ResolveForceSourceDescriptor(Parameters.Category).Bearings.empty())
            Changed |= Bearing(Page, "ReachBearing", "Box turned toward", Parameters.BearingDegrees);
    }
    if (Parameters.Reach != ForceReach::Everywhere)
    {
        SubHeading(Page, "Falloff");
        int Falloff = static_cast<int>(Parameters.Falloff);
        if (TabStrip::Construct(Page, "ReachFalloff", FalloffLabels, 4, Falloff))
        {
            Parameters.Falloff = static_cast<ForceFalloff>(Falloff);
            Changed            = true;
        }
    }
    else
        CaptionText::Construct(Page, "Everywhere: no edge, the force reaches the whole level.", 0.0f, 10.0f);
    EndSection(Page);
    return Changed;
}

bool ForceSourceInspector::ConstructTiming(ControlColumn& Page)
{
    using namespace SectionRows;
    bool                         Changed  = false;
    const double                 Timeline = ResolveTimelineSeconds();
    const ForceActivationReading Reading  = ResolveForceActivation(Parameters, Timeline);
    BeginSection(Page, "Timing", "Timer", ResolveForceSourceStatusLabel(Reading.Status),
                 "When the force acts, on the Force Field's timeline: always, in a window that may repeat (Lasts 0 is forever, "
                 "Repeats 0 is once), or from the moment it is fired.");
    int Activation = static_cast<int>(Parameters.Activation);
    if (TabStrip::Construct(Page, "Activation", ActivationLabels, 3, Activation))
    {
        Parameters.Activation = static_cast<ForceActivation>(Activation);
        Changed               = true;
    }
    if (Parameters.Activation == ForceActivation::Window)
    {
        Changed |= Measure(Page, "Begins", "Begins", Parameters.BeginsSeconds, 0.0, 600.0, 0.5, "s");
        Changed |= Measure(Page, "Lasts", "Lasts", Parameters.LastsSeconds, 0.0, 600.0, 0.5, "s");
        Changed |= Measure(Page, "Repeats", "Repeats every", Parameters.RepeatsSeconds, 0.0, 600.0, 0.5, "s");
    }
    if (Parameters.Activation == ForceActivation::Trigger)
    {
        Changed |= Measure(Page, "Lasts", "Lasts", Parameters.LastsSeconds, 0.0, 600.0, 0.5, "s");
        char Fired[96];
        if (Parameters.TriggeredAtSeconds < 0.0)
            std::snprintf(Fired, sizeof(Fired), "Armed: Fire starts it at the timeline's %.1f s.", Timeline);
        else
            std::snprintf(Fired, sizeof(Fired), "Fired at %.1f s \xC2\xB7 age %.1f s.", Parameters.TriggeredAtSeconds, Reading.AgeSeconds);
        CaptionText::Construct(Page, Fired, 0.0f, 8.0f);
        if (NextButton::Construct(Page, "FireSource", "Fire now \xE2\x86\x97"))
        {
            Parameters.TriggeredAtSeconds = Timeline;
            Changed                       = true;
        }
        if (Parameters.TriggeredAtSeconds >= 0.0 && NextButton::Construct(Page, "ArmSource", "Re-arm"))
        {
            Parameters.TriggeredAtSeconds = -1.0;
            Changed                       = true;
        }
    }
    if (Parameters.Activation != ForceActivation::Always)
    {
        Changed |= Measure(Page, "RampIn", "Ramp in", Parameters.RampInSeconds, 0.0, 60.0, 0.05, "s");
        Changed |= Measure(Page, "RampOut", "Ramp out", Parameters.RampOutSeconds, 0.0, 60.0, 0.05, "s");
    }
    EndSection(Page);
    return Changed;
}

bool ForceSourceInspector::ConstructReceivers(ControlColumn& Page)
{
    using namespace SectionRows;
    bool Changed = false;
    BeginSection(Page, "Receivers", "Layers", Parameters.ActsOn == 0 ? "Every receiver" : "Selective",
                 "What this force moves. Acting on every receiver lets a flow share the baked lattice; a selective force is "
                 "evaluated per receiver instead.");
    bool Every = Parameters.ActsOn == 0;
    if (Switch(Page, "ActsOnEvery", "Every receiver", Every))
    {
        Parameters.ActsOn = Every ? 0u : ForceReceiverParticles;
        Changed           = true;
    }
    if (!Every)
    {
        for (const ForceReceiverDescriptor& Receiver : EnumerateForceReceivers())
        {
            bool On = (Parameters.ActsOn & Receiver.Receiver) != 0u;
            if (Switch(Page, Receiver.Identifier, Receiver.Label, On))
            {
                Parameters.ActsOn = On ? (Parameters.ActsOn | Receiver.Receiver) : (Parameters.ActsOn & ~static_cast<uint32_t>(Receiver.Receiver));
                Changed           = true;
            }
        }
        if (Parameters.ActsOn == 0u) CaptionText::Construct(Page, "No receiver left: the force acts on every receiver again.", 0.0f, 8.0f);
    }
    EndSection(Page);
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE
//------------------------------------------------------------------------------------------------------------------------

void ForceSourceInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveNative();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    const ForceSourceDescriptor&       Descriptor   = ResolveForceSourceDescriptor(Parameters.Category);
    const ForceContributionDescriptor& Contribution = ResolveForceContributionDescriptor(Descriptor.Contribution);
    bool Changed = false;
    if (PageHeading::Construct(Page, "ForceSourceHeader", QueryEyebrowText(), Descriptor.Name, "Type defaults", Descriptor.Blurb,
                               "Every parameter back to the type's defaults (the centre, timing and receivers stay)"))
    {
        ForceSourceConfiguration Defaults = ConstructForceSource(Parameters.Category);
        for (size_t Axis = 0; Axis < 3; ++Axis) Defaults.CentreMetres[Axis] = Parameters.CentreMetres[Axis];
        Defaults.Enabled            = Parameters.Enabled;
        Defaults.Activation         = Parameters.Activation;
        Defaults.ActsOn             = Parameters.ActsOn;
        Defaults.TriggeredAtSeconds = Parameters.TriggeredAtSeconds;
        Defaults.Revision           = Parameters.Revision;
        Parameters                  = Defaults;
        Changed                     = true;
    }

    // 📝 The hero: the magnitude this force adds against the distance from its centre (held still and fully on).
    const ForceWindReading       Wind     = ResolveWindReading();
    const EnvironmentEditAccess& Access   = InspectorRuntime::Instance().QueryEnvironmentAccess();
    const double                 Slice    = Access.Forces != nullptr ? Access.Forces->SliceHeightMetres : 10.0;
    const ForceSourceProfile     Profile  = ResolveForceSourceProfile(Parameters, Wind, Wind.GroundHeightMetres + Slice, 0.0);
    const ForceActivationReading Reading  = ResolveForceActivation(Parameters, ResolveTimelineSeconds());
    {
        const ControlTheme& Look = Page.QueryTheme();
        DiagramPen          Pen  = DiagramCanvas::Begin(Page, "ForceProfile", 640, 220, 4, 10);
        const double        Peak = std::max(1e-6, Profile.PeakMagnitude * 1.15);
        const auto          X    = [&](double Distance) { return 40.0f + static_cast<float>(Distance / std::max(1e-6, Profile.ReachMetres)) * 580.0f; };
        const auto          Y    = [&](double Magnitude) { return 190.0f - static_cast<float>(Magnitude / Peak) * 170.0f; };
        for (int Guide = 0; Guide <= 4; ++Guide)
            Pen.Line({40, 190.0f - Guide * 42.5f}, {620, 190.0f - Guide * 42.5f}, ResolveCssColour(0xFFFFFF10));
        std::vector<ImVec2> Curve;
        std::vector<ImU32>  Colours;
        for (uint32_t Step = 0; Step < kForceProfileSamples; ++Step)
        {
            Curve.push_back({X(Profile.DistanceMetres[Step]), Y(Profile.Magnitude[Step])});
            Colours.push_back(ResolveContributionColour(Descriptor.Contribution, 0.28f));
        }
        Pen.ShadedArea(Curve, 190.0f, Colours);
        Pen.Path(Curve, ResolveContributionColour(Descriptor.Contribution), 2.0f);
        char Label[48], Reach[32];
        std::snprintf(Label, sizeof(Label), "%.1f %s", Profile.PeakMagnitude, Profile.Unit);
        Pen.Text({44, 16}, Label, Look.Ink, 15, -1);
        SectionRows::FormatDistance(Profile.ReachMetres, Reach, sizeof(Reach));
        Pen.Text({620, 212}, Reach, Look.Muted, 13, 1);
        Pen.Text({40, 212}, "0", Look.Muted, 13, -1);
    }
    char Envelope[16], Peak[24];
    std::snprintf(Envelope, sizeof(Envelope), "%.0f", Reading.Envelope * 100.0);
    std::snprintf(Peak, sizeof(Peak), "%.1f", Profile.PeakMagnitude);
    const MetricTile Tiles[3] = {{"Status", ResolveForceSourceStatusLabel(Reading.Status), ""},
                                 {"Envelope", Envelope, "%"},
                                 {"Peak", Peak, Contribution.Unit}};
    MetricTiles::Construct(Page, Tiles, 3, MetricTiles::NoLead);

    TabStrip::Construct(Page, "ForceSourceSections", SectionLabels, SectionCount, ActiveSection);
    switch (ActiveSection)
    {
    case 0: Changed |= ConstructStrength(Page); break;
    case 1: Changed |= ConstructReach(Page); break;
    case 2: Changed |= ConstructTiming(Page); break;
    default: Changed |= ConstructReceivers(Page); break;
    }
    char Footer[96];
    std::snprintf(Footer, sizeof(Footer), "Levelspace record \xC2\xB7 ForceSource \xC2\xB7 %s", Descriptor.Identifier);
    FooterRow::Construct(Page, Footer, "Saved with the level");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#endif
