/*==========================================================================================================================================
                                                          FORCEFIELDINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 The Force Field page: the contribution the Field view shows, the hero over the view's measured peak, the forces' counts and
//    cost, the quick switches and the seven sections (Inspectors/ForceField), as the approved prototype's Field page.

#include "Engine/Interface/Inspectors/ForceFieldInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/ForceFieldVisualization/ForceFieldVisualization.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/ForceField/ForceFieldPieces.h"
#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

constexpr const char* SectionLabels[ForceFieldInspector::SectionCount] = {"View", "Range", "Forces", "Receivers", "Timeline", "Vectors", "Budget"};
constexpr int         UpperTabs = 4;   // [-] View · Range · Forces · Receivers on the first row, the other three on the second

constexpr const char* ContributionLabels[] = {"Flow \xC2\xB7 m/s", "Accelerate \xC2\xB7 m/s\xC2\xB2", "Damp \xC2\xB7 1/s"};

// 📝 The hero's dashed reference per contribution: a gale, one g, a unit damping rate.
struct SeriesTarget
{
    double      Reading;
    const char* Label;
};
constexpr SeriesTarget Targets[] = {{17.2, "Gale"}, {9.81, "1 g"}, {1.0, "Rate"}};

ForceFieldPieces::HeroTone ResolveBeaufortTone(uint32_t Force) noexcept
{
    return Force >= 10u ? ForceFieldPieces::HeroTone::Severe : (Force >= 8u ? ForceFieldPieces::HeroTone::Warm : ForceFieldPieces::HeroTone::Calm);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINER ROW
//------------------------------------------------------------------------------------------------------------------------

std::string ForceFieldInspector::QueryDirectoryReadout() const
{
    const size_t Forces = InspectorRuntime::Instance().QueryEnvironmentAccess().ForceSources.size();
    char         Text[48];
    std::snprintf(Text, sizeof(Text), "%zu force%s", Forces, Forces == 1 ? "" : "s");
    return Text;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HERO
//------------------------------------------------------------------------------------------------------------------------

bool ForceFieldInspector::ConstructContribution(ControlColumn& Page)
{
    int Shown = static_cast<int>(Parameters.ViewContribution);
    if (!TabStrip::Construct(Page, "FieldContribution", ContributionLabels, 3, Shown)) return false;
    Parameters.ViewContribution = static_cast<ForceContribution>(Shown);
    if (Parameters.ViewContribution == ForceContribution::Damp) Parameters.Quantity = ForceFieldQuantity::Speed;
    return true;
}

// 📝 The Field view's peak in view and its last twelve seconds; before the view has measured, the card says how to start it.
void ForceFieldInspector::ConstructHero(ControlColumn& Page, const ForceFieldPageReading& Reading)
{
    const ForceFieldReadings& Readings = Reading.Access->FieldReadings;
    const bool                Speed    = Parameters.ViewContribution == ForceContribution::Flow && Parameters.Quantity == ForceFieldQuantity::Speed;
    char Chip[64], Figure[32], Unit[96], Span[32], Cell[32];
    ForceFieldPieces::HeroFigures Figures;
    Figures.Title = "Peak in view";
    if (Readings.Measured)
    {
        ForceFieldPieces::FormatFigure(Readings.Peak, Figure, sizeof(Figure));
        ForceFieldVisualization::FormatDistance(Readings.Span, Span, sizeof(Span));
        ForceFieldVisualization::FormatDistance(Readings.Cell, Cell, sizeof(Cell));
        std::snprintf(Unit, sizeof(Unit), "%s \xC2\xB7 %s span \xC2\xB7 %s cells \xC2\xB7 LOD %.0f", Readings.Unit, Span, Cell,
                      std::round(std::log2(std::max(1.0, Readings.Cell))));
        if (Speed)
        {
            const uint32_t Force = ResolveBeaufortForce(Readings.Peak);
            std::snprintf(Chip, sizeof(Chip), "Beaufort %u \xC2\xB7 %s", Force, ResolveBeaufortName(Force));
            Figures.Tone = ResolveBeaufortTone(Force);
        }
        else
        {
            std::snprintf(Chip, sizeof(Chip), "%s \xC2\xB7 %s",
                          Parameters.ViewContribution == ForceContribution::Damp ? "Damping rate"
                                                                                 : ForceFieldVisualization::ResolveQuantityLabel(Parameters.Quantity),
                          Parameters.AutomaticScale ? "auto scale" : "fixed scale");
        }
        Figures.Tag = Readings.FieldShown ? "ANALYTIC \xC2\xB7 IN VIEW" : "LAST MEASURED";
    }
    else
    {
        std::snprintf(Figure, sizeof(Figure), "\xE2\x80\x94");
        std::snprintf(Unit, sizeof(Unit), "%s", ForceFieldVisualization::ResolveUnit(Parameters.ViewContribution, Parameters.Quantity));
        std::snprintf(Chip, sizeof(Chip), "Waiting for the Field view");
        Figures.Tone = ForceFieldPieces::HeroTone::Waiting;
        Figures.Tag  = "FIELD VIEW";
    }
    Figures.Chip   = Chip;
    Figures.Figure = Figure;
    Figures.Unit   = Unit;
    ForceFieldPieces::BeginHero(Page, Figures);
    const SeriesTarget&          Target = Targets[std::clamp(static_cast<int>(Parameters.ViewContribution), 0, 2)];
    ForceFieldPieces::SeriesLook Look;
    Look.Target      = Target.Reading;
    Look.TargetLabel = Target.Label;
    Look.Unit        = Readings.Measured ? Readings.Unit : "";
    Look.Empty       = "The chart fills while the Field view shows";
    ForceFieldPieces::ConstructSeries(Page, "FieldPeakSeries", Readings.History.data(), static_cast<uint32_t>(Readings.History.size()),
                                      Readings.HistoryCount, Readings.HistoryCursor, Look);
    ForceFieldPieces::EndHero(Page, "The peak of the selected quantity across the view at its level of detail. Zoom out and small forces fade "
                                    "into their cells, as a coarse cascade averages them.");
    if (!Readings.FieldShown)
    {
        if (NextButton::Construct(Page, "ShowFieldView", "Show the Field view \xE2\x86\x97", true, "The viewport's Map / Oblique slice of the field"))
            InspectorRuntime::Instance().RequestWorkspaceAction("field-view:on");
    }
    else if (NextButton::Construct(Page, "ShowSceneView", "Back to the scene view \xE2\x86\x97", true, "The 3D scene viewport"))
    {
        InspectorRuntime::Instance().RequestWorkspaceAction("field-view:off");
    }
}

// 📝 The forces by contribution and the proposed cascades, then where they are evaluated and what the lattice costs.
void ForceFieldInspector::ConstructReadouts(ControlColumn& Page, const ForceFieldPageReading& Reading)
{
    const ForceFieldBudget& Budget = Reading.Budget;
    const MetricTile        Counts[4] = {{"Flow", std::to_string(Reading.Counts[0]), ""},
                                         {"Accelerate", std::to_string(Reading.Counts[1]), ""},
                                         {"Damp", std::to_string(Reading.Counts[2]), ""},
                                         {"Cascades", std::to_string(Budget.LevelCount), ""}};
    MetricTiles::Construct(Page, Counts, 4, MetricTiles::NoLead);
    char Slots[24], Memory[32], Extent[32];
    std::snprintf(Slots, sizeof(Slots), "of %u slots", Budget.GpuSlots);
    ForceFieldPieces::FormatBytes(Budget.TotalBytes, Memory, sizeof(Memory));
    ForceFieldVisualization::FormatDistance(Budget.LevelCount > 0 ? Budget.Levels[Budget.LevelCount - 1].ExtentMetres : 0.0, Extent, sizeof(Extent));
    const auto Split = [](const char* Label, const char* Text) {
        const std::string Whole(Text);
        const size_t      Gap = Whole.find(' ');
        return Gap == std::string::npos ? MetricTile{Label, Whole, ""} : MetricTile{Label, Whole.substr(0, Gap), Whole.substr(Gap + 1)};
    };
    const MetricTile Evaluated[2] = {{"Baked into the lattice", std::to_string(Budget.Baked), Slots},
                                     {"Evaluated per receiver", std::to_string(Budget.Live + Budget.Damping), "fields"}};
    const MetricTile Lattice[2]   = {Split("Cascade memory \xC2\xB7 proposed", Memory), Split("Coarsest cascade", Extent)};
    MetricTiles::Construct(Page, Evaluated, 2, MetricTiles::NoLead);
    MetricTiles::Construct(Page, Lattice, 2, MetricTiles::NoLead);
}

// 📝 The field's and its view's switches; the Wind guest switches the Wind record (it advances that record's revision).
bool ForceFieldInspector::ConstructQuickControls(ControlColumn& Page, const ForceFieldPageReading& Reading)
{
    SectionHeading::Construct(Page, "Quick controls", "The field and its view");
    WindConfiguration* Wind      = Reading.Access->Wind;
    const bool         WindShown = Wind != nullptr && Wind->Enabled;
    bool               WindGuest = WindShown;
    const QuickTile    Tiles[6]  = {
        {"Wind guest", "Wind", Wind != nullptr ? &WindGuest : nullptr, "The Wind record the field carries"},
        {"Eddies", "Swirl", &Parameters.IncludeTurbulence, "The wind's curl eddies"},
        {"Heatmap", "Layers", &Parameters.Heatmap, "The Field view's colours"},
        {"Arrows", "Move", &Parameters.Arrows, "The Field view's arrows"},
        {"Tracers", "Activity", &Parameters.Tracers, "Particles drifting with the flow"},
        {"Cascades", "GridLattice", &Parameters.ShowCascades, "The proposed cascades around the view centre"},
    };
    const bool Changed = QuickTiles::Construct(Page, "FieldQuick", Tiles, 6, 3);
    if (Wind != nullptr && WindGuest != WindShown)
    {
        Wind->Enabled = WindGuest;
        Wind->AdvanceRevision();
    }
    return Changed && WindGuest == WindShown;
}

void ForceFieldInspector::ConstructSectionTabs(ControlColumn& Page)
{
    int Upper = ActiveSection < UpperTabs ? ActiveSection : -1;
    if (TabStrip::Construct(Page, "FieldSectionsUpper", SectionLabels, UpperTabs, Upper) && Upper >= 0) ActiveSection = Upper;
    int Lower = ActiveSection >= UpperTabs ? ActiveSection - UpperTabs : -1;
    if (TabStrip::Construct(Page, "FieldSectionsLower", SectionLabels + UpperTabs, SectionCount - UpperTabs, Lower) && Lower >= 0)
        ActiveSection = UpperTabs + Lower;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveNative();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool          Changed = false;
    if (PageHeading::Construct(Page, "FieldPower", "FORCES", "Force Field", Parameters.Enabled ? "\xE2\x97\x8F On" : "\xE2\x97\x8B Off",
                               "The master component: one Wind profile (its guest) and every localized force, sampled from 100 m to 100 km.",
                               "Switch the whole field"))
    {
        Parameters.Enabled = !Parameters.Enabled;
        Changed            = true;
    }
    Changed |= ConstructContribution(Page);
    const ForceFieldPageReading Reading = ResolveForceFieldPageReading(Parameters);
    ConstructHero(Page, Reading);
    ConstructReadouts(Page, Reading);
    Changed |= ConstructQuickControls(Page, Reading);
    ConstructSectionTabs(Page);
    switch (ActiveSection)
    {
    case 0: Changed |= FieldViewSection::Construct(Page, Parameters, Reading); break;
    case 1: Changed |= FieldRangeSection::Construct(Page, Parameters, Reading); break;
    case 2: Changed |= FieldForcesSection::Construct(Page, Parameters, Reading); break;
    case 3: Changed |= FieldReceiversSection::Construct(Page, Parameters, Reading); break;
    case 4: Changed |= FieldTimelineSection::Construct(Page, Parameters, Reading); break;
    case 5: Changed |= FieldVectorsSection::Construct(Page, Parameters, Reading); break;
    default: Changed |= FieldBudgetSection::Construct(Page, Parameters, Reading); break;
    }
    if (NextButton::Construct(Page, "FieldWind", "Open the Wind guest \xE2\x86\x97", Reading.Access->Wind != nullptr, "Add a Wind record first"))
        InspectorRuntime::Instance().RequestSelection("Wind");
    FooterRow::Construct(Page, "Levelspace records \xC2\xB7 Force Field \xC2\xB7 Wind \xC2\xB7 forces", "Saved with the level");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
std::string ForceFieldInspector::QueryDirectoryReadout() const { return {}; }
void        ForceFieldInspector::ConstructInspector() {}
} // namespace Frontier

#endif
