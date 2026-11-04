/*==========================================================================================================================================
                                                          FIELDRANGESECTION.CPP
==========================================================================================================================================*/
// 🧩 Range and detail: the Field view's range presets (100 m to 100 km), the proposed cascades drawn nested on a log scale, and
//    their count, finest cell and lattice size with what they cost.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/Inspectors/ForceField/ForceFieldPieces.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

constexpr const char* SideOptions[]     = {"64", "128", "256"};
constexpr uint32_t    SideCells[]       = {64u, 128u, 256u};
constexpr const char* VerticalOptions[] = {"16", "32", "64"};
constexpr uint32_t    VerticalCells[]   = {16u, 32u, 64u};

struct RangePreset
{
    const char* Id;
    const char* Caption;
    double      Span;
};

constexpr RangePreset Presets[] = {
    {"Range100m", "100 m \xC2\xB7 a yard", 100.0},
    {"Range1km", "1 km \xC2\xB7 a district", 1000.0},
    {"Range10km", "10 km \xC2\xB7 a city", 10000.0},
    {"Range100km", "100 km \xC2\xB7 a weather system", 100000.0},
};

int ResolveChoice(const uint32_t* Options, int Count, uint32_t Setting) noexcept
{
    for (int Index = 0; Index < Count; ++Index)
        if (Options[Index] == Setting) return Index;
    return -1;
}

// 📝 Each cascade is 8× the last, too much to draw to proportion: the squares shrink by 0.58 each and the labels carry the sizes.
void ConstructCascadeDiagram(ControlColumn& Page, const ForceFieldConfiguration& Field, const ForceFieldBudget& Budget)
{
    const ControlTheme& Theme   = Page.QueryTheme();
    const float         Height  = 300.0f;
    DiagramPen          Pen     = DiagramCanvas::Begin(Page, "CascadeDiagram", 640, Height, 2, 10);
    const float         CentreX = Height / 2.0f + 6.0f, CentreY = Height / 2.0f;
    const uint32_t      Count   = Budget.LevelCount;
    for (uint32_t Step = 0; Step < Count; ++Step)
    {
        const uint32_t Index = Count - 1u - Step;
        const float    Side  = (Height - 16.0f) * std::pow(0.58f, static_cast<float>(Count - 1u - Index));
        const float    Depth = static_cast<float>(Count - 1u - Index);
        const ImVec2   Min(CentreX - Side / 2.0f, CentreY - Side / 2.0f);
        Pen.Rect(Min, {Side, Side}, ResolveAlpha(IM_COL32(159, 180, 214, 255), 0.03f + 0.05f * Depth), 6.0f);
        const ImU32 Edge = ResolveAlpha(IM_COL32(159, 180, 214, 255), 0.3f + 0.15f * Depth);
        const float Dash = Index == Count - 1u ? 5.0f : 0.0f;
        Pen.Path({Min, {Min.x + Side, Min.y}, {Min.x + Side, Min.y + Side}, {Min.x, Min.y + Side}, Min}, Edge, 1.5f, Dash, Dash);
    }
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        char Extent[32], Cell[32], Title[64], Detail[96];
        SectionRows::FormatDistance(Budget.Levels[Index].ExtentMetres, Extent, sizeof(Extent));
        SectionRows::FormatDistance(Budget.Levels[Index].CellMetres, Cell, sizeof(Cell));
        std::snprintf(Title, sizeof(Title), "C%u \xC2\xB7 %s", Index, Extent);
        std::snprintf(Detail, sizeof(Detail), "%s cells \xC2\xB7 refresh 1/%u", Cell, Budget.Levels[Index].RefreshFrames);
        const float Row = 40.0f + static_cast<float>(Index) * (Height - 60.0f) / static_cast<float>(std::max(1u, Count));
        Pen.Text({Height + 36.0f, Row}, Title, Theme.Ink, 21, -1);
        Pen.Text({Height + 36.0f, Row + 24.0f}, Detail, Theme.Muted, 16, -1);
    }
    Pen.Circle({CentreX, CentreY}, 4.0f, ResolveCssColour(0xFFFFFFFF));
    char Footer[64];
    std::snprintf(Footer, sizeof(Footer), "%u\xC2\xB2 \xC3\x97 %u each \xC2\xB7 log scale", Field.CellsPerSide, Field.VerticalCells);
    Pen.Text({636.0f, Height - 6.0f}, Footer, Theme.Muted, 15, 1);
}

} // namespace

bool FieldRangeSection::Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading)
{
    using namespace SectionRows;
    const ForceFieldBudget& Budget = Reading.Budget;
    char Finest[32] = "\xE2\x80\x94", Coarsest[32] = "\xE2\x80\x94", Status[80];
    if (Budget.LevelCount > 0)
    {
        FormatDistance(Budget.Levels[0].ExtentMetres, Finest, sizeof(Finest));
        FormatDistance(Budget.Levels[Budget.LevelCount - 1].ExtentMetres, Coarsest, sizeof(Coarsest));
    }
    std::snprintf(Status, sizeof(Status), "%s \xE2\x80\x93 %s", Finest, Coarsest);
    BeginSection(Page, "Range and detail", "Globe", Status,
                 "The field reaches from a street to a weather system. The viewport samples it at a cell size that follows the zoom (a "
                 "power of two, so panning never shimmers); the engine would hold the flows in cascaded lattices around the camera.");
    std::array<ActionEntry, 4> Actions{};
    for (size_t Index = 0; Index < Actions.size(); ++Index)
        Actions[Index] = {Presets[Index].Id, Presets[Index].Caption, true, "Shows the Field view at this range"};
    ActionRowLook Look;
    Look.MarginTop    = 2;
    Look.MarginBottom = 12;
    if (const int Pressed = ActionRow::Construct(Page, Actions, Look); Pressed >= 0)
        InspectorRuntime::Instance().RequestWorkspaceAction("field-span:" + std::to_string(static_cast<int>(Presets[Pressed].Span)));
    ConstructCascadeDiagram(Page, Field, Budget);

    bool Changed  = false;
    int  Cascades = static_cast<int>(Field.Cascades);
    if (Count(Page, "FieldCascades", "Cascades \xC2\xB7 proposed", Cascades, 2, 5, ""))
    {
        Field.Cascades = static_cast<uint32_t>(Cascades);
        Changed        = true;
    }
    double Cell = Field.FinestCellMetres;
    if (Distance(Page, "FieldFinestCell", "Finest cell \xC2\xB7 proposed", Cell, 0.5, 8.0, 0.0))
    {
        Field.FinestCellMetres = static_cast<float>(Cell);
        Changed                = true;
    }
    int Side = ResolveChoice(SideCells, 3, Field.CellsPerSide);
    if (ChoiceRow::Construct(Page, "FieldCellsPerSide", "Cells per side", SideOptions, 3, Side) && Side >= 0)
    {
        Field.CellsPerSide = SideCells[Side];
        Changed            = true;
    }
    int Vertical = ResolveChoice(VerticalCells, 3, Field.VerticalCells);
    if (ChoiceRow::Construct(Page, "FieldVerticalCells", "Vertical cells", VerticalOptions, 3, Vertical) && Vertical >= 0)
    {
        Field.VerticalCells = VerticalCells[Vertical];
        Changed             = true;
    }
    Changed |= Switch(Page, "FieldShowCascades", "Show cascades in the viewport", Field.ShowCascades, nullptr,
                      "Nested squares around the view centre, labelled with extent and cell size");
    char Memory[32], Extent[32], Coarse[32];
    ForceFieldPieces::FormatBytes(Budget.TotalBytes, Memory, sizeof(Memory));
    FormatDistance(Budget.LevelCount > 0 ? Budget.Levels[0].ExtentMetres : 0.0, Extent, sizeof(Extent));
    FormatDistance(Budget.LevelCount > 0 ? Budget.Levels[Budget.LevelCount - 1].CellMetres : 0.0, Coarse, sizeof(Coarse));
    const auto Split = [](const char* Text) {
        const std::string Whole(Text);
        const size_t      Gap = Whole.find(' ');
        return Gap == std::string::npos ? MetricTile{"", Whole, ""} : MetricTile{"", Whole.substr(0, Gap), Whole.substr(Gap + 1)};
    };
    MetricTile Tiles[3] = {Split(Memory), Split(Extent), Split(Coarse)};
    Tiles[0].Label      = "Memory";
    Tiles[1].Label      = "Finest extent";
    Tiles[2].Label      = "Coarsest cell";
    MetricTiles::Construct(Page, Tiles, 3, MetricTiles::NoLead);
    CaptionText::Construct(Page, "Terms shorter than two cells fade out \xE2\x80\x94 the 571 m gust bands, the 95 m eddies \xE2\x80\x94 so a "
                                 "100 km view shows the weather, not aliasing.",
                           4.0f, 8.0f);
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
