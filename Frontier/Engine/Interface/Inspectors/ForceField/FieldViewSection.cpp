/*==========================================================================================================================================
                                                           FIELDVIEWSECTION.CPP
==========================================================================================================================================*/
// 🧩 View: the slice the viewport's Field view shows — which quantity, map or oblique, how high above the ground it is cut, how
//    it is coloured, and its arrows and tracers.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/ForceFieldVisualization/ForceFieldVisualization.h"

#include <cstdio>

namespace Frontier
{

namespace
{

constexpr const char* Components[]  = {"Speed", "East", "South", "Up"};
constexpr const char* Derivatives[] = {"Vorticity \xCF\x89", "Divergence \xE2\x88\x87\xC2\xB7v"};
constexpr const char* Projections[] = {"Map \xC2\xB7 top-down", "Oblique \xC2\xB7 3D slice"};
constexpr const char* Heatmaps[]    = {"Smooth", "Cells (eos)"};
constexpr const char* Scales[]      = {"Automatic \xC2\xB7 98th percentile", "Fixed"};

} // namespace

bool FieldViewSection::Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading&)
{
    using namespace SectionRows;
    const bool  Damp = Field.ViewContribution == ForceContribution::Damp;
    char        Status[64];
    std::snprintf(Status, sizeof(Status), "%s \xC2\xB7 %s", Field.Projection == ForceFieldProjection::Oblique ? "Oblique" : "Map",
                  Damp ? "Rate" : ForceFieldVisualization::ResolveQuantityLabel(Field.Quantity));
    BeginSection(Page, "View", "Eye", Status,
                 "The slice the viewport shows: which quantity, how it is coloured, and how high above the ground it is cut.");
    bool Changed = false;

    // 📝 Six quantities on two rows sharing one selection (the components, then ω and ∇·v); damping is a rate, so it shows
    //    its magnitude only.
    const int Shown     = static_cast<int>(Field.Quantity);
    int       Component = Shown < 4 ? Shown : -1;
    int       Derived   = Shown >= 4 ? Shown - 4 : -1;
    if (ChoiceRow::Construct(Page, "FieldQuantity", "Quantity", Components, 4, Component, !Damp, nullptr, Damp ? "Damping is a rate" : nullptr) &&
        Component >= 0)
    {
        Field.Quantity = static_cast<ForceFieldQuantity>(Component);
        Changed        = true;
    }
    if (ChoiceRow::Construct(Page, "FieldDerivative", "Derivative", Derivatives, 2, Derived, !Damp, nullptr, Damp ? "Damping is a rate" : nullptr) &&
        Derived >= 0)
    {
        Field.Quantity = static_cast<ForceFieldQuantity>(4 + Derived);
        Changed        = true;
    }
    int Projection = static_cast<int>(Field.Projection);
    if (ChoiceRow::Construct(Page, "FieldProjection", "Projection", Projections, 2, Projection))
    {
        Field.Projection = static_cast<ForceFieldProjection>(Projection);
        Changed          = true;
    }
    double Slice = Field.SliceHeightMetres;
    if (Distance(Page, "FieldSliceHeight", "Slice height above ground", Slice, 1.0, 8000.0, 0.0, nullptr,
                 "The Ekman layer turns the wind through the first kilometre"))
    {
        Field.SliceHeightMetres = static_cast<float>(Slice);
        Changed                 = true;
    }
    int Heatmap = Field.SmoothHeatmap ? 0 : 1;
    if (ChoiceRow::Construct(Page, "FieldHeatmapStyle", "Heatmap", Heatmaps, 2, Heatmap))
    {
        Field.SmoothHeatmap = Heatmap == 0;
        Changed             = true;
    }
    int Scale = Field.AutomaticScale ? 0 : 1;
    if (ChoiceRow::Construct(Page, "FieldScaleMode", "Colour scale", Scales, 2, Scale))
    {
        Field.AutomaticScale = Scale == 0;
        Changed              = true;
    }
    Changed |= Amount(Page, "FieldFixedScale", "Fixed full scale", Field.FixedScale, 0.5f, 120.0f, 0.5f,
                      ForceFieldVisualization::ResolveUnit(Field.ViewContribution, Field.Quantity),
                      Field.AutomaticScale ? "Automatic scale" : nullptr);
    Changed |= Amount(Page, "FieldOpacity", "Heatmap opacity", Field.HeatmapOpacity, 0.1f, 1.0f, 0.05f, "");

    SubHeading(Page, "Arrows and tracers");
    Changed |= Amount(Page, "FieldArrowSpacing", "Arrow spacing on screen", Field.ArrowSpacingPixels, 22.0f, 90.0f, 1.0f, "px");
    int Tracers = static_cast<int>(Field.TracerCount);
    if (Count(Page, "FieldTracerCount", "Tracers", Tracers, 0, 1200, ""))
    {
        Field.TracerCount = static_cast<uint32_t>(Tracers);
        Changed           = true;
    }
    Changed |= Amount(Page, "FieldCellPixels", "Sample cell on screen", Field.CellPixels, 3.0f, 14.0f, 1.0f, "px", nullptr,
                      "Finer cells sample more and refresh slower");
    Changed |= Switch(Page, "FieldLabels", "Source labels", Field.Labels, nullptr,
                      "Small forces keep their dot and drop their label when the view is wide");
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
