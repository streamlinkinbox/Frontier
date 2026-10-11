//=============================================================================================================================================
// SolidArcOutlinerAdapter.cpp
//=============================================================================================================================================

#include "SolidArcOutlinerAdapter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {
namespace {

// Exact SolidArc web palette from docs/solidarc/index.html palette:
// sketch/curve #4fd8e0, body #ffb454, surface #4da3ff, plane #b48cff, dim #e5d33a.
constexpr float Channel(uint32_t V) noexcept { return static_cast<float>(V) / 255.0f; }
constexpr float kSketchTint[3]       = { Channel( 79u), Channel(216u), Channel(224u) }; // #4fd8e0
constexpr float kBodyTint[3]         = { Channel(255u), Channel(180u), Channel( 84u) }; // #ffb454
constexpr float kProfileTint[3]      = { Channel( 52u), Channel(199u), Channel( 89u) }; // #34c759: profiles take the theme green, apart from the cyan lines
constexpr float kSurfaceTint[3]      = { Channel( 77u), Channel(163u), Channel(255u) }; // #4da3ff
constexpr float kConstructionTint[3] = { Channel(180u), Channel(140u), Channel(255u) }; // #b48cff
constexpr float kDimensionTint[3]    = { Channel(229u), Channel(211u), Channel( 58u) }; // #e5d33a
constexpr float kConstraintTint[3]   = { Channel(255u), Channel(107u), Channel(138u) }; // constraint rose #ff6b8a: its own colour, apart from the body amber

void AssignTint(EditorInstance& Row, const float Tint[3]) noexcept
{
    Row.Tint[0] = Tint[0];
    Row.Tint[1] = Tint[1];
    Row.Tint[2] = Tint[2];
}

struct SolidArcBucket
{
    enum class Content : uint32_t { Lines, Profiles, Bodies, Surfaces, Construction, Dimensions, Constraints } BucketContent;
    const char*          Label;
    EditorSymbol       Mark;
    EditorNarrowing      Narrowing;
    uint32_t             FilterMask;
    float                Tint[3];
};

void ClearRow(EditorInstance& Row, SolidArcOutlinerBinding& Binding) noexcept
{
    Row = EditorInstance{};
    Binding = SolidArcOutlinerBinding{};
}

void CopyText(char* Destination, size_t Capacity, const char* Text) noexcept
{
    if (Capacity == 0u)
        return;
    std::snprintf(Destination, Capacity, "%s", Text != nullptr ? Text : "");
}

void SeatFolder(EditorInstance& Row,
                const char* Label,
                uint32_t Depth,
                uint32_t KidCount,
                EditorSymbol Mark,
                EditorNarrowing Narrowing,
                uint32_t FilterMask,
                const float Tint[3]) noexcept
{
    CopyText(Row.Label, sizeof(Row.Label), Label);
    Row.Depth      = Depth;
    Row.KidCount   = KidCount;
    Row.Category   = EditorInstanceCategory::Folder;
    Row.Symbol   = Mark;
    Row.Narrowing  = Narrowing;
    Row.FilterMask = FilterMask;
    Row.Pinned     = true;
    Row.Visible    = true;
    Row.Tint[0]    = Tint[0];
    Row.Tint[1]    = Tint[1];
    Row.Tint[2]    = Tint[2];
}

const char* ClassLabel(FigureClassification Class) noexcept
{
    switch (Class)
    {
    case FigureClassification::Curve:   return "Sketch curve";
    case FigureClassification::Surface: return "Surface";
    case FigureClassification::Body:    return "Body";
    case FigureClassification::Empty:   return "Construction";
    default:                            return "Figure";
    }
}

const char* ClassNoun(FigureClassification Class) noexcept
{
    switch (Class)
    {
    case FigureClassification::Curve:   return "curve";
    case FigureClassification::Surface: return "surface";
    case FigureClassification::Body:    return "body";
    case FigureClassification::Empty:   return "empty";
    default:                            return "figure";
    }
}

bool IsLineFigure(const SceneFigure& Figure) noexcept;

EditorSymbol FigureMark(const SceneFigure& Figure) noexcept
{
    if (Figure.Construction || Figure.Classification == FigureClassification::Empty)
        return EditorSymbol::Construction;
    switch (Figure.Classification)
    {
    case FigureClassification::Body:    return EditorSymbol::Body;
    case FigureClassification::Surface: return EditorSymbol::Surface;
    case FigureClassification::Curve:   return IsLineFigure(Figure) ? EditorSymbol::Line : EditorSymbol::Profile;
    default:                            return EditorSymbol::Profile;
    }
}

bool IsLineFigure(const SceneFigure& Figure) noexcept
{
    using FormT = SceneFigure::ParametricForm;
    return Figure.Blueprint.Form == FormT::Line;
}

EditorNarrowing FigureNarrowing(const SceneFigure& Figure) noexcept
{
    if (Figure.Construction || Figure.Classification == FigureClassification::Empty)
        return EditorNarrowing::Camera;   // SolidArc catalogue: Construction
    if (Figure.Classification == FigureClassification::Body)
        return EditorNarrowing::Bodies;   // SolidArc catalogue: Bodies
    if (Figure.Classification == FigureClassification::Surface)
        return EditorNarrowing::Geometry; // SolidArc catalogue: Surfaces
    if (Figure.Classification == FigureClassification::Curve)
        return IsLineFigure(Figure) ? EditorNarrowing::Lights : EditorNarrowing::Sky; // Lines / Profiles
    return EditorNarrowing::Sky;
}

uint32_t FigureFilterMask(const SceneFigure& Figure) noexcept
{
    if (Figure.Construction || Figure.Classification == FigureClassification::Empty)
        return SolidArcOutlinerFilter::Construction;
    if (Figure.Classification == FigureClassification::Body)
        return SolidArcOutlinerFilter::Bodies;
    if (Figure.Classification == FigureClassification::Surface)
        return SolidArcOutlinerFilter::Surfaces;
    if (Figure.Classification == FigureClassification::Curve)
        return IsLineFigure(Figure) ? SolidArcOutlinerFilter::Lines : SolidArcOutlinerFilter::Profiles;
    return SolidArcOutlinerFilter::Profiles;
}

const float* FigureOutlinerTint(const SceneFigure& Figure) noexcept
{
    if (Figure.Construction || Figure.Classification == FigureClassification::Empty)
        return kConstructionTint;
    if (Figure.Classification == FigureClassification::Body)
        return kBodyTint;
    if (Figure.Classification == FigureClassification::Surface)
        return kSurfaceTint;
    if (Figure.Classification == FigureClassification::Curve)
        return IsLineFigure(Figure) ? kSketchTint : kProfileTint;
    return kSketchTint;
}

const char* BlueprintLabel(SceneFigure::ParametricForm Form) noexcept
{
    using FormT = SceneFigure::ParametricForm;
    switch (Form)
    {
    case FormT::Box:         return "box";
    case FormT::Sphere:      return "sphere";
    case FormT::Cylinder:    return "cylinder";
    case FormT::Cone:        return "cone";
    case FormT::Torus:       return "torus";
    case FormT::Line:        return "line";
    case FormT::Circle:      return "circle";
    case FormT::Arc:         return "arc";
    case FormT::Ellipse:     return "ellipse";
    case FormT::Polyline:    return "polyline";
    case FormT::Spline:      return "spline";
    case FormT::Rectangle:   return "rectangle";
    case FormT::Extrude:     return "extrude";
    case FormT::Revolve:     return "revolve";
    case FormT::Loft:        return "loft";
    case FormT::Sweep:       return "sweep";
    case FormT::Pipe:        return "pipe";
    case FormT::Boolean:     return "boolean";
    case FormT::ChamferEdge: return "chamfer edge";
    case FormT::Empty:       return "empty";
    default:                 return "authored";
    }
}

bool BucketAccepts(const SolidArcBucket& Bucket, const SceneFigure& Figure) noexcept
{
    using Content = SolidArcBucket::Content;
    switch (Bucket.BucketContent)
    {
    case Content::Lines:
        return Figure.Classification == FigureClassification::Curve && !Figure.Construction && IsLineFigure(Figure);
    case Content::Profiles:
        return Figure.Classification == FigureClassification::Curve && !Figure.Construction && !IsLineFigure(Figure);
    case Content::Bodies:
        return Figure.Classification == FigureClassification::Body && !Figure.Construction;
    case Content::Surfaces:
        return Figure.Classification == FigureClassification::Surface && !Figure.Construction;
    case Content::Construction:
        return Figure.Construction || Figure.Classification == FigureClassification::Empty;
    default:
        return false;
    }
}

uint32_t CountFigures(const ConsoleHost& Host, const SolidArcBucket& Bucket) noexcept
{
    uint32_t Count = 0u;
    for (const SceneFigure& Figure : Host.AllFigures())
        if (BucketAccepts(Bucket, Figure))
            ++Count;
    return Count;
}

const SceneFigure* FindFigure(const ConsoleHost& Host, uint32_t Identity) noexcept
{
    for (const SceneFigure& Figure : Host.AllFigures())
        if (Figure.Identity == Identity)
            return &Figure;
    return nullptr;
}

SceneFigure* FindFigure(ConsoleHost& Host, uint32_t Identity) noexcept
{
    return Host.Document().Find(Identity);
}

const ConsoleHost::DimensionEntry* FindDimension(const ConsoleHost& Host, uint32_t Id) noexcept
{
    for (const ConsoleHost::DimensionEntry& Dimension : Host.AllDimensions())
        if (Dimension.Id == Id)
            return &Dimension;
    return nullptr;
}

const ConstraintEntry* FindConstraint(const ConsoleHost& Host, uint32_t Id) noexcept
{
    for (const ConstraintEntry& Constraint : Host.AllConstraints())
        if (Constraint.Id == Id)
            return &Constraint;
    return nullptr;
}

// The row's second line, the way the HTML outliner prints it: the form, then the one measure that names it.
void FormatFigureMeta(const SceneFigure& Figure, char* Destination, size_t Capacity) noexcept
{
    if (Destination == nullptr || Capacity == 0u)
        return;
    const bool Authored = Figure.Blueprint.Form == SceneFigure::ParametricForm::None;
    if (Figure.Classification == FigureClassification::Body)
    {
        const BodyReport Report = Figure.Body.Validate();
        std::snprintf(Destination, Capacity, "%s \xc2\xb7 %.2f m\xc2\xb3", Authored ? "body" : BlueprintLabel(Figure.Blueprint.Form), Report.Volume);
    }
    else if (Figure.Classification == FigureClassification::Surface)
    {
        std::snprintf(Destination, Capacity, "deg %d\xc3\x97%d \xc2\xb7 %zu poles", Figure.Surface.DegreeU, Figure.Surface.DegreeV,
                      Figure.Surface.Poles.size());
    }
    else if (Figure.Classification == FigureClassification::Curve)
    {
        std::snprintf(Destination, Capacity, "%s \xc2\xb7 %.2f m", Authored ? "curve" : BlueprintLabel(Figure.Blueprint.Form),
                      Figure.Curve.Length());
    }
    else
    {
        std::snprintf(Destination, Capacity, "%s", BlueprintLabel(Figure.Blueprint.Form));
    }
}

void SeatFigureRow(EditorInstance& Row, SolidArcOutlinerBinding& Binding, const SceneFigure& Figure, uint32_t Depth) noexcept
{
    CopyText(Row.Label, sizeof(Row.Label), Figure.Name.c_str());
    Row.Depth     = Depth;
    Row.KidCount  = 0u;
    Row.Category  = EditorInstanceCategory::Geometry;
    Row.Symbol  = FigureMark(Figure);
    Row.Narrowing = FigureNarrowing(Figure);
    Row.FilterMask = FigureFilterMask(Figure);
    Row.Visible   = !Figure.Hidden;
    Row.Locked    = Figure.Locked;
    AssignTint(Row, FigureOutlinerTint(Figure));
    FormatFigureMeta(Figure, Row.Meta, sizeof(Row.Meta));
    if (Figure.Construction)
        CopyText(Row.Tag, sizeof(Row.Tag), "Ref");
    if (Figure.Recipe.Live())
        CopyText(Row.Tag, sizeof(Row.Tag), "Live");
    if (Figure.Selected || !Figure.SelectedFaces.empty() || !Figure.SelectedEdges.empty() || !Figure.SelectedPoles.empty())
    {
        Row.Standing = EditorStanding::Ok;
        CopyText(Row.StandingNote, sizeof(Row.StandingNote), "Selected");
    }
    else if (Figure.Classification == FigureClassification::Body)
    {
        const BodyReport Report = Figure.Body.Validate();
        if (!Report.Manifold || !Report.Oriented)
        {
            Row.Standing = EditorStanding::Warn;
            CopyText(Row.StandingNote, sizeof(Row.StandingNote), "B-rep check");
        }
    }
    if (!Figure.Recipe.Complaint.empty())
    {
        Row.Standing = EditorStanding::Warn;
        CopyText(Row.StandingNote, sizeof(Row.StandingNote), Figure.Recipe.Complaint.c_str());
    }
    Binding.RowRole = SolidArcOutlinerBinding::Role::Figure;
    Binding.FigureIdentity = Figure.Identity;
}

EditorPropertyGroup* AddGroup(EditorSheet& Sheet, const char* Title) noexcept
{
    if (Sheet.GroupCount >= kMaxEditorSheetGroups)
        return nullptr;
    EditorPropertyGroup& Group = Sheet.Groups[Sheet.GroupCount++];
    Group = EditorPropertyGroup{};
    CopyText(Group.Title, sizeof(Group.Title), Title);
    return &Group;
}

EditorProperty* AddProperty(EditorPropertyGroup& Group, const char* Label, EditorPropertyCategory Category) noexcept
{
    if (Group.PropertyCount >= kMaxEditorGroupProps)
        return nullptr;
    EditorProperty& Property = Group.Properties[Group.PropertyCount++];
    Property = EditorProperty{};
    CopyText(Property.Label, sizeof(Property.Label), Label);
    Property.Category = Category;
    return &Property;
}

void AddReadout(EditorPropertyGroup& Group, const char* Label, const char* Text) noexcept
{
    if (EditorProperty* Property = AddProperty(Group, Label, EditorPropertyCategory::Readout))
        CopyText(Property->Text, sizeof(Property->Text), Text);
}

void AddSwitch(EditorPropertyGroup& Group, const char* Label, bool On) noexcept
{
    if (EditorProperty* Property = AddProperty(Group, Label, EditorPropertyCategory::Switch))
        Property->On = On;
}

void AddAxis(EditorPropertyGroup& Group, const char* Label, Vec3 Value, bool Editable = false) noexcept
{
    if (EditorProperty* Property = AddProperty(Group, Label, EditorPropertyCategory::AxisVec3))
    {
        Property->Axes[0] = static_cast<float>(Value.X);
        Property->Axes[1] = static_cast<float>(Value.Y);
        Property->Axes[2] = static_cast<float>(Value.Z);
        Property->AxisStep = 0.1f;
        Property->Editable = Editable;
    }
}

EditorPropertyGroup* AddGroup(EditorSheet& Sheet, const char* Title, const char* Caption) noexcept
{
    EditorPropertyGroup* Group = AddGroup(Sheet, Title);
    if (Group != nullptr)
        CopyText(Group->Caption, sizeof(Group->Caption), Caption);
    return Group;
}

// The pivot, rotation and scale the inspector's Transform card shows for the picked figure. A figure's geometry carries
//    no rotation or scale of its own, so the card keeps the amounts applied since the pick (reset on a new pick) and
//    the pivot they turn about; the pivot follows the figure when the gizmo or a command moves it.
struct TransformDraft
{
    uint32_t Identity    = 0u;
    bool     Seeded      = false;
    Vec3     Pivot       = Vec3(0.0, 0.0, 0.0);     // [m]
    Vec3     LastCentre  = Vec3(0.0, 0.0, 0.0);     // [m] the bounds centre when the pivot last agreed with the figure
    float    Rotation[3] = { 0.0f, 0.0f, 0.0f };    // [deg]
    float    Scale[3]    = { 1.0f, 1.0f, 1.0f };    // [-]
    float    Uniform     = 1.0f;                    // [-]
};

TransformDraft gDraft;

TransformDraft& DraftFor(const SceneFigure& Figure) noexcept
{
    if (gDraft.Identity != Figure.Identity)
    {
        gDraft          = TransformDraft{};
        gDraft.Identity = Figure.Identity;
    }
    const Box3 Bounds = Figure.Bounds();
    if (!Bounds.Empty())
    {
        const Vec3 Centre = Bounds.Centre();
        if (!gDraft.Seeded)
        {
            gDraft.Pivot  = Centre;
            gDraft.Seeded = true;
        }
        else
        {
            gDraft.Pivot = gDraft.Pivot + (Centre - gDraft.LastCentre);
        }
        gDraft.LastCentre = Centre;
    }
    return gDraft;
}

void AddSlider(EditorPropertyGroup& Group, const char* Label, float Value, float Low, float High, uint32_t Decimals, const char* Unit) noexcept
{
    if (EditorProperty* Property = AddProperty(Group, Label, EditorPropertyCategory::Slider))
    {
        Property->Figure   = Value;
        Property->Minimum  = Low;
        Property->Maximum  = High;
        Property->Decimals = Decimals;
        CopyText(Property->Unit, sizeof(Property->Unit), Unit);
    }
}

// Applies M about the draft's pivot, so a rotation or scale turns the figure in place.
void TransformAboutPivot(SceneFigure& Figure, TransformDraft& Draft, const Mat4& M) noexcept
{
    Figure.Transform(Mat4::Translation(Draft.Pivot) * M * Mat4::Translation(Draft.Pivot * -1.0));
    const Box3 Bounds = Figure.Bounds();
    if (!Bounds.Empty())
        Draft.LastCentre = Bounds.Centre();
}

constexpr double kDegrees = 3.14159265358979323846 / 180.0;

void AddColour(EditorPropertyGroup& Group, const char* Label, const float Tint[3]) noexcept
{
    if (EditorProperty* Property = AddProperty(Group, Label, EditorPropertyCategory::Colour))
    {
        Property->ColourTint[0] = Tint[0];
        Property->ColourTint[1] = Tint[1];
        Property->ColourTint[2] = Tint[2];
        Property->Swatches = true;
    }
}

void AddMatcap(EditorPropertyGroup& Group, uint8_t Matcap) noexcept
{
    if (EditorProperty* Property = AddProperty(Group, "Matcap", EditorPropertyCategory::Select))
    {
        const char* Options[] = { "Clay", "Steel", "Plastic", "Matcap", "Glass", "Carbon" };
        Property->OptionCount = 6u;
        for (uint32_t I = 0u; I < Property->OptionCount; ++I)
            CopyText(Property->Options[I], sizeof(Property->Options[I]), Options[I]);
        Property->Picked = std::min<uint32_t>(Matcap, Property->OptionCount - 1u);
    }
}

void AddFigureCounts(EditorPropertyGroup& Group, const SceneFigure& Figure) noexcept
{
    char Buffer[48] = {};
    if (Figure.Classification == FigureClassification::Body)
    {
        const BodyReport Report = Figure.Body.Validate();
        std::snprintf(Buffer, sizeof(Buffer), "%d", Report.Faces); AddReadout(Group, "Faces", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%d", Report.Edges); AddReadout(Group, "Edges", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%d", Report.Vertices); AddReadout(Group, "Vertices", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%.3f", Report.Volume); AddReadout(Group, "Volume", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%.3f", Report.Area); AddReadout(Group, "Area", Buffer);
        AddReadout(Group, "B-rep", Report.Solid() ? "solid" : (Report.Manifold ? "sheet/wire" : "open"));
    }
    else if (Figure.Classification == FigureClassification::Surface)
    {
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure.Surface.Poles.size()); AddReadout(Group, "Control poles", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure.Surface.KnotsU.size()); AddReadout(Group, "Knots U", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure.Surface.KnotsV.size()); AddReadout(Group, "Knots V", Buffer);
    }
    else if (Figure.Classification == FigureClassification::Curve)
    {
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure.Curve.Poles.size()); AddReadout(Group, "Control poles", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure.Curve.Knots.size()); AddReadout(Group, "Knots", Buffer);
        AddReadout(Group, "Sketch", Figure.Construction ? "construction" : "profile");
    }
    else
    {
        AddReadout(Group, "Reference", "workplane / handle");
    }
}

void AddBlueprintParameters(EditorPropertyGroup& Group, const SceneFigure& Figure) noexcept
{
    const SceneFigure::ParametricBlueprint& B = Figure.Blueprint;
    char Buffer[48] = {};
    AddReadout(Group, "Form", BlueprintLabel(B.Form));
    if (B.Form == SceneFigure::ParametricForm::None)
    {
        AddReadout(Group, "Live edit", "read-only");
        return;
    }
    std::snprintf(Buffer, sizeof(Buffer), "%.3f", B.R0); AddReadout(Group, "R0", Buffer);
    std::snprintf(Buffer, sizeof(Buffer), "%.3f", B.R1); AddReadout(Group, "R1", Buffer);
    std::snprintf(Buffer, sizeof(Buffer), "%.3f", B.R2); AddReadout(Group, "R2", Buffer);
    std::snprintf(Buffer, sizeof(Buffer), "%d / %d", B.I0, B.I1); AddReadout(Group, "Integer slots", Buffer);
    AddReadout(Group, "Closed", B.Closed ? "yes" : "no");
}

// The hero card's building blocks. The hero is the SolidArc inspector's top card: subject pill, big measure, up to three stats.
void SeatHero(EditorSheet& Sheet, const char* Subject, uint32_t Identity, const char* Subtitle, const char* Measure, const char* Unit,
              const char* Caption, bool Renameable) noexcept
{
    EditorSheetHero& Hero = Sheet.Hero;
    Hero = EditorSheetHero{};
    Hero.Active     = true;
    Hero.Renameable = Renameable;
    Hero.Identity   = Identity;
    CopyText(Hero.Subject, sizeof(Hero.Subject), Subject);
    CopyText(Hero.Subtitle, sizeof(Hero.Subtitle), Subtitle);
    CopyText(Hero.Measure, sizeof(Hero.Measure), Measure);
    CopyText(Hero.Unit, sizeof(Hero.Unit), Unit);
    CopyText(Hero.Caption, sizeof(Hero.Caption), Caption);
}

void AddStat(EditorSheet& Sheet, const char* Label, const char* Text) noexcept
{
    EditorSheetHero& Hero = Sheet.Hero;
    if (Hero.StatCount >= 3u)
        return;
    CopyText(Hero.StatLabel[Hero.StatCount], sizeof(Hero.StatLabel[0]), Label);
    CopyText(Hero.StatText[Hero.StatCount], sizeof(Hero.StatText[0]), Text);
    ++Hero.StatCount;
}

void AddStatNumber(EditorSheet& Sheet, const char* Label, size_t Value) noexcept
{
    char Buffer[24] = {};
    std::snprintf(Buffer, sizeof(Buffer), "%zu", Value);
    AddStat(Sheet, Label, Buffer);
}

void OfferPresence(EditorSheet& Sheet, EditorSheetPresence Cell, bool On) noexcept
{
    Sheet.PresenceOffered[static_cast<uint32_t>(Cell)] = true;
    Sheet.Presence[static_cast<uint32_t>(Cell)]        = On;
}

constexpr uint32_t kSlotVisible = static_cast<uint32_t>(EditorSheetPresence::Visible);
constexpr uint32_t kSlotLocked  = static_cast<uint32_t>(EditorSheetPresence::Locked);
constexpr uint32_t kSlotBuild   = static_cast<uint32_t>(EditorSheetPresence::Construction);
constexpr uint32_t kSlotDims    = static_cast<uint32_t>(EditorSheetPresence::Dimensions);

// The whole document: no object picked, so the hero reports the scene and the cards list its contents and kernel.
void BuildDocumentSheet(const ConsoleHost& Host, EditorSheet& Sheet) noexcept
{
    uint32_t Bodies = 0u, Sketches = 0u, Surfaces = 0u, Construction = 0u;
    for (const SceneFigure& Figure : Host.AllFigures())
    {
        if (Figure.Construction || Figure.Classification == FigureClassification::Empty)
            ++Construction;
        else if (Figure.Classification == FigureClassification::Body)
            ++Bodies;
        else if (Figure.Classification == FigureClassification::Surface)
            ++Surfaces;
        else if (Figure.Classification == FigureClassification::Curve)
            ++Sketches;
    }
    char Buffer[48] = {};
    std::snprintf(Buffer, sizeof(Buffer), "%zu figures", Host.AllFigures().size());
    char Count[24] = {};
    std::snprintf(Count, sizeof(Count), "%u", Bodies);
    SeatHero(Sheet, "DOCUMENT", 0u, Buffer, Count, "", Bodies == 1u ? "body" : "bodies", false);
    AddStat(Sheet, "Kernel", "NURBS");
    AddStat(Sheet, "Tolerance", "1e-6");
    AddStatNumber(Sheet, "Undo", Host.Timeline().UndoEntries().size());

    EditorPropertyGroup* Summary = AddGroup(Sheet, "Contents", "What the document holds");
    if (Summary != nullptr)
    {
        std::snprintf(Buffer, sizeof(Buffer), "%u", Bodies); AddReadout(*Summary, "Bodies", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%u", Sketches); AddReadout(*Summary, "Sketches", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%u", Surfaces); AddReadout(*Summary, "Surfaces", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%u", Construction); AddReadout(*Summary, "Construction", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Host.AllDimensions().size()); AddReadout(*Summary, "Dimensions", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Host.AllConstraints().size()); AddReadout(*Summary, "Constraints", Buffer);
    }

    EditorPropertyGroup* Kernel = AddGroup(Sheet, "CAD kernel", "Geometry engine");
    if (Kernel != nullptr)
    {
        AddReadout(*Kernel, "Kernel", "NURBS \xc2\xb7 B-rep");
        AddReadout(*Kernel, "Tolerance", "1e-6");
        AddReadout(*Kernel, "Axes", "Z-up lattice");
        AddReadout(*Kernel, "Selection", SelectModeName(Host.CurrentSelectMode()));
        std::snprintf(Buffer, sizeof(Buffer), "%zu / %zu", Host.Timeline().UndoEntries().size(), Host.Timeline().RedoEntries().size());
        AddReadout(*Kernel, "Undo / redo", Buffer);
    }
}

// Writes one editable Transform row back: Position moves the figure's pivot; Rotation and Scale apply the change since
//    the last frame about the pivot and keep the new amounts in the draft.
void ApplyTransformAxis(SceneFigure& Figure, const EditorProperty& Property) noexcept
{
    TransformDraft& Draft = DraftFor(Figure);
    if (Figure.Locked)
        return;                 // a locked figure keeps its pose; the next frame's sheet shows the draft again
    if (std::strcmp(Property.Label, "Position") == 0)
    {
        if (!Draft.Seeded)
            return;
        const Vec3 Delta(Property.Axes[0] - Draft.Pivot.X, Property.Axes[1] - Draft.Pivot.Y, Property.Axes[2] - Draft.Pivot.Z);
        if (std::fabs(Delta.X) + std::fabs(Delta.Y) + std::fabs(Delta.Z) > 1e-5)
        {
            Figure.Transform(Mat4::Translation(Delta));
            Draft.Pivot = Draft.Pivot + Delta;
            const Box3 Bounds = Figure.Bounds();
            if (!Bounds.Empty())
                Draft.LastCentre = Bounds.Centre();
        }
    }
    else if (std::strcmp(Property.Label, "Rotation") == 0)
    {
        static const Vec3 Axes[3] = { Vec3(1.0, 0.0, 0.0), Vec3(0.0, 1.0, 0.0), Vec3(0.0, 0.0, 1.0) };
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const double Delta = static_cast<double>(Property.Axes[Axis] - Draft.Rotation[Axis]);
            if (std::fabs(Delta) < 1e-6)
                continue;
            TransformAboutPivot(Figure, Draft, Mat4::Rotation(Axes[Axis], Delta * kDegrees));
            Draft.Rotation[Axis] = Property.Axes[Axis];
        }
    }
    else if (std::strcmp(Property.Label, "Scale") == 0)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float Target = std::max(Property.Axes[Axis], 0.01f);
            const float Ratio  = Target / std::max(Draft.Scale[Axis], 0.01f);
            if (std::fabs(Ratio - 1.0f) < 1e-6f)
                continue;
            Vec3 Factor(1.0, 1.0, 1.0);
            (Axis == 0 ? Factor.X : (Axis == 1 ? Factor.Y : Factor.Z)) = Ratio;
            TransformAboutPivot(Figure, Draft, Mat4::Scaling(Factor));
            Draft.Scale[Axis] = Target;
        }
    }
}

} // namespace

uint32_t BuildSolidArcOutliner(const ConsoleHost& Host,
                               EditorInstance* Rows,
                               SolidArcOutlinerBinding* Bindings,
                               uint32_t Capacity,
                               EditorReadout* Readout) noexcept
{
    if (Rows == nullptr || Bindings == nullptr || Capacity == 0u)
        return 0u;

    for (uint32_t I = 0u; I < Capacity; ++I)
        ClearRow(Rows[I], Bindings[I]);

    // 📝 One folder per filter-catalogue entry, in the catalogue's order and spelling, each in its own colour.
    using Mark = EditorSymbol;
    const SolidArcBucket Buckets[] =
    {
        { SolidArcBucket::Content::Lines,        "Lines",        Mark::Line,         EditorNarrowing::Lights,   SolidArcOutlinerFilter::Lines,        { kSketchTint[0],       kSketchTint[1],       kSketchTint[2] } },
        { SolidArcBucket::Content::Profiles,     "Profiles",     Mark::Profile,      EditorNarrowing::Sky,      SolidArcOutlinerFilter::Profiles,     { kProfileTint[0],      kProfileTint[1],      kProfileTint[2] } },
        { SolidArcBucket::Content::Bodies,       "Bodies",       Mark::Body,         EditorNarrowing::Bodies,   SolidArcOutlinerFilter::Bodies,       { kBodyTint[0],         kBodyTint[1],         kBodyTint[2] } },
        { SolidArcBucket::Content::Surfaces,     "Surfaces",     Mark::Surface,      EditorNarrowing::Geometry, SolidArcOutlinerFilter::Surfaces,     { kSurfaceTint[0],      kSurfaceTint[1],      kSurfaceTint[2] } },
        { SolidArcBucket::Content::Construction, "Construction", Mark::Construction, EditorNarrowing::Camera,   SolidArcOutlinerFilter::Construction, { kConstructionTint[0], kConstructionTint[1], kConstructionTint[2] } },
        { SolidArcBucket::Content::Dimensions,   "Dimensions",   Mark::Dimension,    EditorNarrowing::Sky,      SolidArcOutlinerFilter::Dimensions,   { kDimensionTint[0],    kDimensionTint[1],    kDimensionTint[2] } },
        { SolidArcBucket::Content::Constraints,  "Constraints",  Mark::Constraint,   EditorNarrowing::Auto,     SolidArcOutlinerFilter::Constraints,   { kConstraintTint[0],   kConstraintTint[1],   kConstraintTint[2] } },
    };

    uint32_t At = 0u;
    for (const SolidArcBucket& Bucket : Buckets)
    {
        if (At >= Capacity)
            break;

        uint32_t KidCount = 0u;
        if (Bucket.BucketContent == SolidArcBucket::Content::Dimensions)
            KidCount = static_cast<uint32_t>(Host.AllDimensions().size());
        else if (Bucket.BucketContent == SolidArcBucket::Content::Constraints)
            KidCount = static_cast<uint32_t>(Host.AllConstraints().size());
        else
            KidCount = CountFigures(Host, Bucket);

        const uint32_t FolderRow = At++;
        SeatFolder(Rows[FolderRow], Bucket.Label, 0u, KidCount, Bucket.Mark, Bucket.Narrowing, Bucket.FilterMask, Bucket.Tint);
        char CountText[24] = {};
        std::snprintf(CountText, sizeof(CountText), "%u", KidCount);
        CopyText(Rows[FolderRow].Meta, sizeof(Rows[FolderRow].Meta), CountText);
        if (KidCount == 0u)
            Rows[FolderRow].Standing = EditorStanding::Quiet;

        if (Bucket.BucketContent == SolidArcBucket::Content::Dimensions)
        {
            for (const ConsoleHost::DimensionEntry& Dimension : Host.AllDimensions())
            {
                if (At >= Capacity)
                    break;
                EditorInstance& Row = Rows[At];
                SolidArcOutlinerBinding& Binding = Bindings[At];
                CopyText(Row.Label, sizeof(Row.Label), Dimension.AnchorName.empty() ? "Dimension" : Dimension.AnchorName.c_str());
                Row.Depth     = 1u;
                Row.Category  = EditorInstanceCategory::Geometry;
                Row.Symbol  = EditorSymbol::Dimension;
                Row.Narrowing = Bucket.Narrowing;
                Row.FilterMask = Bucket.FilterMask;
                Row.Visible   = !Dimension.Hidden;
                AssignTint(Row, Bucket.Tint);
                char Measure[16] = {};
                if (Dimension.Label.empty())
                    std::snprintf(Measure, sizeof(Measure), "%.2f m", Dimension.Value);
                else
                    std::snprintf(Measure, sizeof(Measure), "%s", Dimension.Label.c_str());
                std::snprintf(Row.Meta, sizeof(Row.Meta), "%.11s \xc2\xb7 %.8s", Measure, Dimension.Slot >= 0 ? "driving" : "measured");
                Binding.RowRole = SolidArcOutlinerBinding::Role::Dimension;
                Binding.DimensionId = Dimension.Id;
                ++At;
            }
        }
        else if (Bucket.BucketContent == SolidArcBucket::Content::Constraints)
        {
            for (const ConstraintEntry& Constraint : Host.AllConstraints())
            {
                if (At >= Capacity)
                    break;
                EditorInstance& Row = Rows[At];
                SolidArcOutlinerBinding& Binding = Bindings[At];
                CopyText(Row.Label, sizeof(Row.Label), Constraint.Note.empty() ? "Constraint" : Constraint.Note.c_str());
                Row.Depth     = 1u;
                Row.Category  = EditorInstanceCategory::Geometry;
                Row.Symbol  = EditorSymbol::Constraint;
                Row.Narrowing = Bucket.Narrowing;
                Row.FilterMask = Bucket.FilterMask;
                Row.Visible   = true;
                AssignTint(Row, Bucket.Tint);
                std::snprintf(Row.Meta, sizeof(Row.Meta), "#%u \xc2\xb7 sketch solver", Constraint.Id);
                Binding.RowRole = SolidArcOutlinerBinding::Role::Constraint;
                Binding.ConstraintId = Constraint.Id;
                ++At;
            }
        }
        else
        {
            for (const SceneFigure& Figure : Host.AllFigures())
            {
                if (!BucketAccepts(Bucket, Figure) || At >= Capacity)
                    continue;
                SeatFigureRow(Rows[At], Bindings[At], Figure, 1u);
                ++At;
            }
        }
    }

    if (Readout != nullptr)
    {
        *Readout = EditorReadout{};
        Readout->Fps = 60.0f;
        CopyText(Readout->Quality, sizeof(Readout->Quality), "CAD");
        const RasterExchange::Tally Tally = Host.Raster().QueryTally();
        Readout->Triangles = Tally.Triangles;
        CopyText(Readout->Scene, sizeof(Readout->Scene), "SolidArc");
    }

    return At;
}

void ApplySolidArcOutlinerVisibility(ConsoleHost& Host,
                                      const EditorInstance* Rows,
                                      const SolidArcOutlinerBinding* Bindings,
                                      uint32_t RowCount) noexcept
{
    if (Rows == nullptr || Bindings == nullptr)
        return;

    for (uint32_t I = 0u; I < RowCount; ++I)
    {
        if (Bindings[I].RowRole != SolidArcOutlinerBinding::Role::Figure || Bindings[I].FigureIdentity == 0u)
            continue;
        SceneFigure* Figure = Host.Document().Find(Bindings[I].FigureIdentity);
        if (Figure != nullptr)
        {
            Figure->Hidden = !Rows[I].Visible;
            if (Rows[I].Label[0] != '\0')
                Figure->Name = Rows[I].Label;
        }
    }
}

bool BuildSolidArcInspectorSheet(const ConsoleHost& Host,
                                  const SolidArcOutlinerBinding& Binding,
                                  EditorSheet* Sheet) noexcept
{
    if (Sheet == nullptr)
        return false;
    *Sheet = EditorSheet{};
    Sheet->Appearance = EditorSheetAppearance::SolidArc;

    if (Binding.RowRole == SolidArcOutlinerBinding::Role::Dimension && Binding.DimensionId != 0u)
    {
        const ConsoleHost::DimensionEntry* Dimension = FindDimension(Host, Binding.DimensionId);
        if (Dimension == nullptr)
            return false;
        static const char* const Forms[] = { "linear", "angle", "radius", "diameter", "arc length", "bounding box" };
        char Measure[24] = {};
        const char* Unit = "";
        if (Dimension->Label.empty())
        {
            std::snprintf(Measure, sizeof(Measure), "%.3f", Dimension->Value);
            Unit = Dimension->Form == ConsoleHost::DimensionForm::Angle ? "rad" : "m";
        }
        else
        {
            std::snprintf(Measure, sizeof(Measure), "%s", Dimension->Label.c_str());
        }
        const char* Target = Dimension->AnchorName.empty() ? "free" : Dimension->AnchorName.c_str();
        char Name[40] = {};
        std::snprintf(Name, sizeof(Name), "Dimension \xc2\xb7 %s", Forms[std::min<size_t>(static_cast<size_t>(Dimension->Form), 5u)]);
        SeatHero(*Sheet, "DIMENSION", Dimension->Id, Name, Measure, Unit, Dimension->Slot >= 0 ? "driving" : "measured", false);
        AddStat(*Sheet, "Target", Target);
        AddStat(*Sheet, "Style", Forms[std::min<size_t>(static_cast<size_t>(Dimension->Form), 5u)]);
        AddStat(*Sheet, "Driving", Dimension->Slot >= 0 ? "live" : "no");
        OfferPresence(*Sheet, EditorSheetPresence::Visible, !Dimension->Hidden);

        EditorPropertyGroup* Group = AddGroup(*Sheet, "Dimension", "Measured or driving");
        if (Group != nullptr)
        {
            char Buffer[48] = {};
            AddReadout(*Group, "Anchor", Target);
            if (!Dimension->Label.empty())
                AddReadout(*Group, "Label", Dimension->Label.c_str());
            std::snprintf(Buffer, sizeof(Buffer), "%.3f", Dimension->Value); AddReadout(*Group, "Value", Buffer);
            AddReadout(*Group, "Live slot", Dimension->Slot >= 0 ? "parametric" : "read-only");
        }
        return true;
    }

    if (Binding.RowRole == SolidArcOutlinerBinding::Role::Constraint && Binding.ConstraintId != 0u)
    {
        const ConstraintEntry* Constraint = FindConstraint(Host, Binding.ConstraintId);
        if (Constraint == nullptr)
            return false;
        static const char* const Types[] = { "distance", "angle", "coincident", "horizontal", "vertical", "parallel", "perpendicular",
                                             "equal length", "equal radius" };
        const char* Type = Types[std::min<size_t>(static_cast<size_t>(Constraint->C.Type), 8u)];
        SeatHero(*Sheet, "CONSTRAINT", Constraint->Id, "Sketch constraint", "", "", "", false);
        AddStat(*Sheet, "Relation", Type);
        AddStat(*Sheet, "Solver", "2D graph");
        AddStat(*Sheet, "State", Constraint->C.Active ? "active" : "off");
        EditorPropertyGroup* Group = AddGroup(*Sheet, "Constraint", "Sketch solver");
        if (Group != nullptr)
        {
            char Buffer[48] = {};
            std::snprintf(Buffer, sizeof(Buffer), "#%u", Constraint->Id); AddReadout(*Group, "Identity", Buffer);
            AddReadout(*Group, "Relation", Type);
            AddReadout(*Group, "Note", Constraint->Note.c_str());
            AddReadout(*Group, "Solver", "2D sketch graph");
        }
        return true;
    }

    if (Binding.RowRole != SolidArcOutlinerBinding::Role::Figure || Binding.FigureIdentity == 0u)
    {
        BuildDocumentSheet(Host, *Sheet);
        return true;
    }

    const SceneFigure* Figure = FindFigure(Host, Binding.FigureIdentity);
    if (Figure == nullptr)
        return false;

    char Buffer[48] = {};
    char Subtitle[40] = {};
    std::snprintf(Subtitle, sizeof(Subtitle), "%s \xc2\xb7 %s", ClassLabel(Figure->Classification), BlueprintLabel(Figure->Blueprint.Form));
    const bool IsEmpty   = Figure->Classification == FigureClassification::Empty;
    const bool IsCurve   = Figure->Classification == FigureClassification::Curve;
    const bool IsBody    = Figure->Classification == FigureClassification::Body;
    const bool IsSurface = Figure->Classification == FigureClassification::Surface;

    const EditorSymbol Mark = FigureMark(*Figure);
    const char* SubjectWord = "OBJECT";
    switch (Mark)
    {
    case EditorSymbol::Line:         SubjectWord = "LINE";         break;
    case EditorSymbol::Profile:      SubjectWord = "PROFILE";      break;
    case EditorSymbol::Body:         SubjectWord = "BODY";         break;
    case EditorSymbol::Surface:      SubjectWord = "SURFACE";      break;
    case EditorSymbol::Construction: SubjectWord = "CONSTRUCTION"; break;
    default: break;
    }

    if (IsBody)
    {
        const BodyReport Report = Figure->Body.Validate();
        std::snprintf(Buffer, sizeof(Buffer), "%.3f", Report.Volume);
        SeatHero(*Sheet, SubjectWord, Figure->Identity, Subtitle, Buffer, "m\xc2\xb3", "volume", true);
        AddStatNumber(*Sheet, "Faces", static_cast<size_t>(Report.Faces));
        AddStatNumber(*Sheet, "Edges", static_cast<size_t>(Report.Edges));
        AddStatNumber(*Sheet, "Vertices", static_cast<size_t>(Report.Vertices));
    }
    else if (IsSurface)
    {
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure->Surface.Poles.size());
        SeatHero(*Sheet, SubjectWord, Figure->Identity, Subtitle, Buffer, "poles", "control net", true);
        std::snprintf(Buffer, sizeof(Buffer), "%d \xc3\x97 %d", Figure->Surface.DegreeU, Figure->Surface.DegreeV);
        AddStat(*Sheet, "Degree", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%d \xc3\x97 %d", Figure->Surface.CountU, Figure->Surface.CountV);
        AddStat(*Sheet, "Net U\xc3\x97V", Buffer);
        AddStatNumber(*Sheet, "Knots", Figure->Surface.KnotsU.size() + Figure->Surface.KnotsV.size());
    }
    else if (IsCurve)
    {
        std::snprintf(Buffer, sizeof(Buffer), "%.3f", Figure->Curve.Length());
        SeatHero(*Sheet, SubjectWord, Figure->Identity, Subtitle, Buffer, "m", "length", true);
        AddStatNumber(*Sheet, "Degree", static_cast<size_t>(Figure->Curve.Degree));
        AddStatNumber(*Sheet, "Poles", Figure->Curve.Poles.size());
        AddStat(*Sheet, "Closed", Figure->Curve.Closed() ? "yes" : "no");
    }
    else
    {
        SeatHero(*Sheet, SubjectWord, Figure->Identity, Subtitle, "", "", "", true);
        AddStat(*Sheet, "Role", "reference");
        AddStat(*Sheet, "Shown", Figure->Hidden ? "no" : "yes");
    }

    // Presence: Visible always; Locked always; Construction for curves and empties; Dimensions when the figure anchors any.
    size_t Anchored = 0u, AnchoredShown = 0u;
    for (const ConsoleHost::DimensionEntry& Dimension : Host.AllDimensions())
    {
        if (Dimension.Anchor != Figure->Identity)
            continue;
        ++Anchored;
        if (!Dimension.Hidden)
            ++AnchoredShown;
    }
    OfferPresence(*Sheet, EditorSheetPresence::Visible, !Figure->Hidden);
    OfferPresence(*Sheet, EditorSheetPresence::Locked, Figure->Locked);
    if (IsCurve || IsEmpty)
        OfferPresence(*Sheet, EditorSheetPresence::Construction, Figure->Construction);
    if (Anchored > 0u)
        OfferPresence(*Sheet, EditorSheetPresence::Dimensions, AnchoredShown > 0u);
    Sheet->ActionsOffered = true;

    const Box3 Bounds = Figure->Bounds();
    TransformDraft& Draft = DraftFor(*Figure);
    EditorPropertyGroup* Transform = AddGroup(*Sheet, "Transform", "World space");
    if (Transform != nullptr)
    {
        if (Host.CurrentSelectMode() != SelectMode::Whole)
        {
            // These fields are OBJECT transforms. Never let them quietly move the whole solid
            // when the rail is editing a face, edge or vertex instead.
            AddReadout(*Transform, "Component editing", "Use the viewport gizmo");
        }
        else if (!Bounds.Empty())
        {
            AddAxis(*Transform, "Position", Draft.Pivot, true);
            AddAxis(*Transform, "Rotation", Vec3(Draft.Rotation[0], Draft.Rotation[1], Draft.Rotation[2]), true);
            AddAxis(*Transform, "Scale", Vec3(Draft.Scale[0], Draft.Scale[1], Draft.Scale[2]), true);
            AddSlider(*Transform, "Uniform scale", Draft.Uniform, 0.1f, 4.0f, 2u, "x");
        }
        else
        {
            AddReadout(*Transform, "Bounds", "empty");
        }
    }

    EditorPropertyGroup* BoundsCard = AddGroup(*Sheet, "Bounds", "Axis-aligned box");
    if (BoundsCard != nullptr && !Bounds.Empty())
    {
        AddAxis(*BoundsCard, "Size", Bounds.Extent());
        AddAxis(*BoundsCard, "Min", Bounds.Low);
        AddAxis(*BoundsCard, "Max", Bounds.High);
        AddAxis(*BoundsCard, "Centre", Bounds.Centre());
    }

    EditorPropertyGroup* Parameters = AddGroup(*Sheet, "Parameters", "Parametric blueprint");
    if (Parameters != nullptr)
    {
        if (Figure->Recipe.Operation == RecipeOperation::SurfaceOffset)
        {
            CopyText(Parameters->Caption, sizeof(Parameters->Caption), "Live surface-distance offset");
            AddReadout(*Parameters, "Metric", "along support skin");
            AddSlider(*Parameters, "Signed width", static_cast<float>(Figure->Recipe.Length * 1000), -1000, 1000, 3u, "mm");
            for (const auto& Input : Host.AllFigures())
            {
                if (!Figure->Recipe.Path.Figures.empty() && Input.Identity == Figure->Recipe.Path.Figures.front())
                    AddReadout(*Parameters, "Parent curve", Input.Name.c_str());
                if (Input.Identity == Figure->Recipe.Path.Support)
                    AddReadout(*Parameters, "Support", Input.Name.c_str());
            }
            char Tolerance[32] = {};
            std::snprintf(Tolerance, sizeof(Tolerance), "%.4g mm", Figure->Recipe.Radius * 1000);
            AddReadout(*Parameters, "Tolerance", Tolerance);
            AddReadout(*Parameters, "Status", Figure->Recipe.Complaint.empty() ? "live" : Figure->Recipe.Complaint.c_str());
        }
        else AddBlueprintParameters(*Parameters, *Figure);
    }

    if (Anchored > 0u)
    {
        EditorPropertyGroup* Dims = AddGroup(*Sheet, "Dimensions", "Anchored to this figure");
        if (Dims != nullptr)
        {
            for (const ConsoleHost::DimensionEntry& Dimension : Host.AllDimensions())
            {
                if (Dimension.Anchor != Figure->Identity)
                    continue;
                if (Dimension.Label.empty())
                    std::snprintf(Buffer, sizeof(Buffer), "%.3f", Dimension.Value);
                else
                    std::snprintf(Buffer, sizeof(Buffer), "%s", Dimension.Label.c_str());
                char Name[28] = {};
                std::snprintf(Name, sizeof(Name), "#%u %s", Dimension.Id, Dimension.Slot >= 0 ? "driving" : "measured");
                AddReadout(*Dims, Name, Buffer);
            }
        }
    }

    if (IsBody || IsSurface)
    {
        EditorPropertyGroup* Topology = AddGroup(*Sheet, "Topology", "B-rep and mass");
        if (Topology != nullptr)
        {
            if (IsBody)
            {
                const BodyReport Report = Figure->Body.Validate();
                std::snprintf(Buffer, sizeof(Buffer), "%.3f", Report.Area); AddReadout(*Topology, "Area", Buffer);
                std::snprintf(Buffer, sizeof(Buffer), "%.3f", Report.Volume); AddReadout(*Topology, "Volume", Buffer);
                AddReadout(*Topology, "B-rep", Report.Solid() ? "solid" : (Report.Manifold ? "sheet/wire" : "open"));
            }
            else
            {
                std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure->Surface.KnotsU.size()); AddReadout(*Topology, "Knots U", Buffer);
                std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure->Surface.KnotsV.size()); AddReadout(*Topology, "Knots V", Buffer);
            }
            std::snprintf(Buffer, sizeof(Buffer), "%zu F \xc2\xb7 %zu E \xc2\xb7 %zu V", Figure->SelectedFaces.size(),
                          Figure->SelectedEdges.size(), Figure->SelectedPoles.size());
            AddReadout(*Topology, "Picked", Buffer);
        }
    }

    EditorPropertyGroup* Display = AddGroup(*Sheet, "Display", "Tint and shading");
    if (Display != nullptr)
    {
        AddColour(*Display, "Tint", Figure->Tint);
        AddMatcap(*Display, Figure->Matcap);
    }
    return true;
}

void ApplySolidArcInspectorSheet(ConsoleHost& Host,
                                 const SolidArcOutlinerBinding& Binding,
                                 const EditorSheet& Sheet) noexcept
{
    if (Binding.RowRole == SolidArcOutlinerBinding::Role::Dimension && Binding.DimensionId != 0u)
    {
        const ConsoleHost::DimensionEntry* Dimension = FindDimension(Host, Binding.DimensionId);
        if (Dimension != nullptr && Sheet.Presence[kSlotVisible] == Dimension->Hidden)
        {
            char Line[48] = {};
            std::snprintf(Line, sizeof(Line), "dim %s %u", Dimension->Hidden ? "show" : "hide", Dimension->Id);
            Host.Execute(Line);
        }
        return;
    }
    if (Binding.RowRole != SolidArcOutlinerBinding::Role::Figure || Binding.FigureIdentity == 0u)
        return;
    SceneFigure* Figure = FindFigure(Host, Binding.FigureIdentity);
    if (Figure == nullptr)
        return;

    // Presence: Visible rides the outliner row (ApplySolidArcOutlinerVisibility), the rest is written here.
    if (Sheet.PresenceOffered[kSlotLocked])
        Figure->Locked = Sheet.Presence[kSlotLocked];
    if (Sheet.PresenceOffered[kSlotBuild])
        Figure->Construction = Sheet.Presence[kSlotBuild];
    if (Sheet.PresenceOffered[kSlotDims])
    {
        for (const ConsoleHost::DimensionEntry& Dimension : Host.AllDimensions())
        {
            if (Dimension.Anchor != Figure->Identity || Dimension.Hidden == !Sheet.Presence[kSlotDims])
                continue;
            char Line[48] = {};
            std::snprintf(Line, sizeof(Line), "dim %s %u", Sheet.Presence[kSlotDims] ? "show" : "hide", Dimension.Id);
            Host.Execute(Line);
            break;                  // the dimension list may have been touched; the next frame continues
        }
    }

    for (uint32_t G = 0u; G < Sheet.GroupCount; ++G)
    {
        const EditorPropertyGroup& Group = Sheet.Groups[G];
        for (uint32_t P = 0u; P < Group.PropertyCount; ++P)
        {
            const EditorProperty& Property = Group.Properties[P];
            if (Figure->Recipe.Operation == RecipeOperation::SurfaceOffset && Property.Category == EditorPropertyCategory::Slider &&
                std::strcmp(Property.Label, "Signed width") == 0 &&
                Property.Figure != static_cast<float>(Figure->Recipe.Length * 1000))
            {
                char Line[128] = {};
                std::snprintf(Line, sizeof(Line), "surface-offset edit \"#%u\" %.17g", Figure->Identity, double(Property.Figure) / 1000);
                Host.Execute(Line);
                return;
            }
            if (Property.Category == EditorPropertyCategory::AxisVec3 && Property.Editable &&
                Host.CurrentSelectMode() == SelectMode::Whole)
            {
                ApplyTransformAxis(*Figure, Property);
            }
            else if (Host.CurrentSelectMode() == SelectMode::Whole && Property.Category == EditorPropertyCategory::Slider &&
                     std::strcmp(Property.Label, "Uniform scale") == 0)
            {
                TransformDraft& Draft = DraftFor(*Figure);
                const float Target = std::max(Property.Figure, 0.01f);
                const float Ratio  = Target / std::max(Draft.Uniform, 0.01f);
                if (!Figure->Locked && std::fabs(Ratio - 1.0f) > 1e-6f)
                {
                    TransformAboutPivot(*Figure, Draft, Mat4::Scaling(Vec3(Ratio, Ratio, Ratio)));
                    for (float& Axis : Draft.Scale)
                        Axis *= Ratio;
                    Draft.Uniform = Target;
                }
            }
            else if (std::strcmp(Property.Label, "Matcap") == 0 && Property.Category == EditorPropertyCategory::Select)
                Figure->Matcap = static_cast<uint8_t>(Property.Picked);
            else if (std::strcmp(Property.Label, "Tint") == 0 && Property.Category == EditorPropertyCategory::Colour)
            {
                Figure->Tint[0] = Property.ColourTint[0];
                Figure->Tint[1] = Property.ColourTint[1];
                Figure->Tint[2] = Property.ColourTint[2];
            }
        }
    }

    // The action tiles run last: a delete or an isolate changes the roster, so nothing is read after them.
    if (Sheet.Action != EditorSheetAction::None && !(Sheet.Action == EditorSheetAction::Delete && Figure->Locked))
    {
        const char* Verb = Sheet.Action == EditorSheetAction::Duplicate ? "duplicate" : (Sheet.Action == EditorSheetAction::Isolate ? "isolate" : "delete");
        // With several figures selected and this one among them, the action takes the whole selection, locked ones excepted for a delete.
        std::string Line = Verb;
        if (Figure->Selected && Host.Document().SelectedCount() > 1)
        {
            for (const SceneFigure& Member : Host.Document().Figures())
                if (Member.Selected && !(Sheet.Action == EditorSheetAction::Delete && Member.Locked))
                    Line += " #" + std::to_string(Member.Identity);
        }
        else
            Line += " #" + std::to_string(Figure->Identity);
        Host.Execute(Line);
    }
}

} // namespace Frontier
