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

// Exact SolidArc web palette from docs/solidarc/index.html KINDS:
// sketch/curve #4fd8e0, body #ffb454, surface #4da3ff, plane #b48cff, dim #e5d33a.
constexpr float Channel(uint32_t V) noexcept { return static_cast<float>(V) / 255.0f; }
constexpr float kSketchTint[3]       = { Channel( 79u), Channel(216u), Channel(224u) }; // #4fd8e0
constexpr float kBodyTint[3]         = { Channel(255u), Channel(180u), Channel( 84u) }; // #ffb454
constexpr float kSurfaceTint[3]      = { Channel( 77u), Channel(163u), Channel(255u) }; // #4da3ff
constexpr float kConstructionTint[3] = { Channel(180u), Channel(140u), Channel(255u) }; // #b48cff
constexpr float kDimensionTint[3]    = { Channel(229u), Channel(211u), Channel( 58u) }; // #e5d33a
constexpr float kConstraintTint[3]   = { Channel(255u), Channel(180u), Channel( 84u) }; // SolidArc constraint key rows use the orange status accent

void AssignTint(EditorInstance& Row, const float Tint[3]) noexcept
{
    Row.Tint[0] = Tint[0];
    Row.Tint[1] = Tint[1];
    Row.Tint[2] = Tint[2];
}

struct SolidArcBucket
{
    enum class Kind : uint32_t { Sketches, Bodies, Surfaces, Construction, Dimensions, Constraints } BucketKind;
    const char*          Label;
    EditorGlyph          Glyph;
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
                EditorGlyph Glyph,
                EditorNarrowing Narrowing,
                uint32_t FilterMask,
                const float Tint[3]) noexcept
{
    CopyText(Row.Label, sizeof(Row.Label), Label);
    Row.Depth      = Depth;
    Row.KidCount   = KidCount;
    Row.Category   = EditorInstanceCategory::Folder;
    Row.Glyph      = Glyph;
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

EditorGlyph FigureGlyph(FigureClassification Class) noexcept
{
    switch (Class)
    {
    case FigureClassification::Curve:   return EditorGlyph::Wave;
    case FigureClassification::Surface: return EditorGlyph::Plane;
    case FigureClassification::Body:    return EditorGlyph::Lattice;
    case FigureClassification::Empty:   return EditorGlyph::Orbit;
    default:                            return EditorGlyph::Auto;
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
        return kSketchTint;
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
    using Kind = SolidArcBucket::Kind;
    switch (Bucket.BucketKind)
    {
    case Kind::Sketches:
        return Figure.Classification == FigureClassification::Curve && !Figure.Construction;
    case Kind::Bodies:
        return Figure.Classification == FigureClassification::Body && !Figure.Construction;
    case Kind::Surfaces:
        return Figure.Classification == FigureClassification::Surface && !Figure.Construction;
    case Kind::Construction:
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

void FormatFigureMeta(const SceneFigure& Figure, char* Destination, size_t Capacity) noexcept
{
    if (Destination == nullptr || Capacity == 0u)
        return;
    if (Figure.Classification == FigureClassification::Body)
    {
        const BodyReport Report = Figure.Body.Validate();
        std::snprintf(Destination, Capacity, "%s · %dF %dE", BlueprintLabel(Figure.Blueprint.Form), Report.Faces, Report.Edges);
    }
    else if (Figure.Classification == FigureClassification::Surface)
    {
        std::snprintf(Destination, Capacity, "%s · %zu poles", BlueprintLabel(Figure.Blueprint.Form), Figure.Surface.Poles.size());
    }
    else if (Figure.Classification == FigureClassification::Curve)
    {
        std::snprintf(Destination, Capacity, "%s · %zu poles", BlueprintLabel(Figure.Blueprint.Form), Figure.Curve.Poles.size());
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
    Row.Glyph     = FigureGlyph(Figure.Classification);
    Row.Narrowing = FigureNarrowing(Figure);
    Row.FilterMask = FigureFilterMask(Figure);
    Row.Visible   = !Figure.Hidden;
    Row.Locked    = Figure.Construction;
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

void BuildDocumentSheet(const ConsoleHost& Host, EditorSheet& Sheet) noexcept
{
    EditorPropertyGroup* Summary = AddGroup(Sheet, "Document");
    if (Summary != nullptr)
    {
        char Buffer[48] = {};
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
        std::snprintf(Buffer, sizeof(Buffer), "%u", Bodies); AddReadout(*Summary, "Bodies", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%u", Sketches); AddReadout(*Summary, "Sketches", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%u", Surfaces); AddReadout(*Summary, "Surfaces", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%u", Construction); AddReadout(*Summary, "Construction", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Host.AllDimensions().size()); AddReadout(*Summary, "Dimensions", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Host.AllConstraints().size()); AddReadout(*Summary, "Constraints", Buffer);
    }

    EditorPropertyGroup* Kernel = AddGroup(Sheet, "CAD kernel");
    if (Kernel != nullptr)
    {
        AddReadout(*Kernel, "Kernel", "NURBS · B-rep");
        AddReadout(*Kernel, "Tolerance", "1e-6");
        AddReadout(*Kernel, "Axes", "Z-up lattice");
        AddReadout(*Kernel, "Selection", SelectModeName(Host.CurrentSelectMode()));
    }

    EditorPropertyGroup* Presentation = AddGroup(Sheet, "Presentation");
    if (Presentation != nullptr)
    {
        AddSwitch(*Presentation, "Dimension lines", true);
        AddSwitch(*Presentation, "Construction", true);
        AddSwitch(*Presentation, "Lattice", true);
        AddMatcap(*Presentation, 3u);
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

    const SolidArcBucket Buckets[] =
    {
        { SolidArcBucket::Kind::Sketches,     "Sketches",     EditorGlyph::Wave,    EditorNarrowing::Lights,   SolidArcOutlinerFilter::Lines | SolidArcOutlinerFilter::Profiles, { kSketchTint[0],       kSketchTint[1],       kSketchTint[2] } },
        { SolidArcBucket::Kind::Bodies,       "Bodies",       EditorGlyph::Lattice, EditorNarrowing::Bodies,   SolidArcOutlinerFilter::Bodies,                                { kBodyTint[0],         kBodyTint[1],         kBodyTint[2] } },
        { SolidArcBucket::Kind::Surfaces,     "Surfaces",     EditorGlyph::Plane,   EditorNarrowing::Geometry, SolidArcOutlinerFilter::Surfaces,                              { kSurfaceTint[0],      kSurfaceTint[1],      kSurfaceTint[2] } },
        { SolidArcBucket::Kind::Construction, "Construction", EditorGlyph::Orbit,   EditorNarrowing::Camera,   SolidArcOutlinerFilter::Construction,                          { kConstructionTint[0], kConstructionTint[1], kConstructionTint[2] } },
        { SolidArcBucket::Kind::Dimensions,   "Dimensions",   EditorGlyph::Sliders, EditorNarrowing::Sky,      SolidArcOutlinerFilter::Dimensions,                            { kDimensionTint[0],    kDimensionTint[1],    kDimensionTint[2] } },
        { SolidArcBucket::Kind::Constraints,  "Constraints",  EditorGlyph::Key,     EditorNarrowing::Auto,     SolidArcOutlinerFilter::Unfiltered,                         { kConstraintTint[0],   kConstraintTint[1],   kConstraintTint[2] } },
    };

    uint32_t At = 0u;
    for (const SolidArcBucket& Bucket : Buckets)
    {
        if (At >= Capacity)
            break;

        uint32_t KidCount = 0u;
        if (Bucket.BucketKind == SolidArcBucket::Kind::Dimensions)
            KidCount = static_cast<uint32_t>(Host.AllDimensions().size());
        else if (Bucket.BucketKind == SolidArcBucket::Kind::Constraints)
            KidCount = static_cast<uint32_t>(Host.AllConstraints().size());
        else
            KidCount = CountFigures(Host, Bucket);

        const uint32_t FolderRow = At++;
        SeatFolder(Rows[FolderRow], Bucket.Label, 0u, KidCount, Bucket.Glyph, Bucket.Narrowing, Bucket.FilterMask, Bucket.Tint);
        char CountText[24] = {};
        std::snprintf(CountText, sizeof(CountText), "%u", KidCount);
        CopyText(Rows[FolderRow].Meta, sizeof(Rows[FolderRow].Meta), CountText);
        if (KidCount == 0u)
            Rows[FolderRow].Standing = EditorStanding::Quiet;

        if (Bucket.BucketKind == SolidArcBucket::Kind::Dimensions)
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
                Row.Glyph     = EditorGlyph::Sliders;
                Row.Narrowing = Bucket.Narrowing;
                Row.FilterMask = Bucket.FilterMask;
                Row.Visible   = !Dimension.Hidden;
                AssignTint(Row, Bucket.Tint);
                CopyText(Row.Meta, sizeof(Row.Meta), Dimension.Label.c_str());
                Binding.RowRole = SolidArcOutlinerBinding::Role::Dimension;
                Binding.DimensionId = Dimension.Id;
                ++At;
            }
        }
        else if (Bucket.BucketKind == SolidArcBucket::Kind::Constraints)
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
                Row.Glyph     = EditorGlyph::Key;
                Row.Narrowing = Bucket.Narrowing;
                Row.FilterMask = Bucket.FilterMask;
                Row.Visible   = true;
                AssignTint(Row, Bucket.Tint);
                char IdText[24] = {};
                std::snprintf(IdText, sizeof(IdText), "#%u", Constraint.Id);
                CopyText(Row.Meta, sizeof(Row.Meta), IdText);
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

    if (Binding.RowRole == SolidArcOutlinerBinding::Role::Dimension && Binding.DimensionId != 0u)
    {
        const ConsoleHost::DimensionEntry* Dimension = FindDimension(Host, Binding.DimensionId);
        if (Dimension == nullptr)
            return false;
        EditorPropertyGroup* Group = AddGroup(*Sheet, "Dimension");
        if (Group != nullptr)
        {
            char Buffer[48] = {};
            AddReadout(*Group, "Anchor", Dimension->AnchorName.empty() ? "free" : Dimension->AnchorName.c_str());
            AddReadout(*Group, "Label", Dimension->Label.c_str());
            std::snprintf(Buffer, sizeof(Buffer), "%.3f", Dimension->Value); AddReadout(*Group, "Value", Buffer);
            AddReadout(*Group, "Live slot", Dimension->Slot >= 0 ? "parametric" : "read-only");
            AddSwitch(*Group, "Hidden", Dimension->Hidden);
        }
        return true;
    }

    if (Binding.RowRole == SolidArcOutlinerBinding::Role::Constraint && Binding.ConstraintId != 0u)
    {
        const ConstraintEntry* Constraint = FindConstraint(Host, Binding.ConstraintId);
        if (Constraint == nullptr)
            return false;
        EditorPropertyGroup* Group = AddGroup(*Sheet, "Constraint");
        if (Group != nullptr)
        {
            char Buffer[48] = {};
            std::snprintf(Buffer, sizeof(Buffer), "#%u", Constraint->Id); AddReadout(*Group, "Identity", Buffer);
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

    EditorPropertyGroup* Identity = AddGroup(*Sheet, "Identity");
    if (Identity != nullptr)
    {
        AddReadout(*Identity, "Class", ClassLabel(Figure->Classification));
        AddReadout(*Identity, "Form", BlueprintLabel(Figure->Blueprint.Form));
        AddSwitch(*Identity, "Hidden", Figure->Hidden);
        AddSwitch(*Identity, "Construction", Figure->Construction);
        AddMatcap(*Identity, Figure->Matcap);
    }

    EditorPropertyGroup* Geometry = AddGroup(*Sheet, "CAD geometry");
    if (Geometry != nullptr)
        AddFigureCounts(*Geometry, *Figure);

    const Box3 Bounds = Figure->Bounds();
    EditorPropertyGroup* BoundsGroup = AddGroup(*Sheet, "Bounds");
    if (BoundsGroup != nullptr)
    {
        if (!Bounds.Empty())
        {
            AddAxis(*BoundsGroup, "Min", Bounds.Low);
            AddAxis(*BoundsGroup, "Max", Bounds.High);
            AddAxis(*BoundsGroup, "Extent", Bounds.Extent());
            AddAxis(*BoundsGroup, "Centre", Bounds.Centre());
        }
        else
        {
            AddReadout(*BoundsGroup, "Bounds", "empty");
        }
    }

    EditorPropertyGroup* Parameters = AddGroup(*Sheet, "Parameters");
    if (Parameters != nullptr)
        AddBlueprintParameters(*Parameters, *Figure);

    EditorPropertyGroup* Selection = AddGroup(*Sheet, "Sub-selection");
    if (Selection != nullptr)
    {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure->SelectedFaces.size()); AddReadout(*Selection, "Faces", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure->SelectedEdges.size()); AddReadout(*Selection, "Edges", Buffer);
        std::snprintf(Buffer, sizeof(Buffer), "%zu", Figure->SelectedPoles.size()); AddReadout(*Selection, "Vertices", Buffer);
    }

    EditorPropertyGroup* Display = AddGroup(*Sheet, "Display");
    if (Display != nullptr)
        AddColour(*Display, "Tint", Figure->Tint);

    return true;
}

void ApplySolidArcInspectorSheet(ConsoleHost& Host,
                                 const SolidArcOutlinerBinding& Binding,
                                 const EditorSheet& Sheet) noexcept
{
    if (Binding.RowRole != SolidArcOutlinerBinding::Role::Figure || Binding.FigureIdentity == 0u)
        return;
    SceneFigure* Figure = FindFigure(Host, Binding.FigureIdentity);
    if (Figure == nullptr)
        return;

    for (uint32_t G = 0u; G < Sheet.GroupCount; ++G)
    {
        const EditorPropertyGroup& Group = Sheet.Groups[G];
        for (uint32_t P = 0u; P < Group.PropertyCount; ++P)
        {
            const EditorProperty& Property = Group.Properties[P];
            if (std::strcmp(Property.Label, "Hidden") == 0 && Property.Category == EditorPropertyCategory::Switch)
                Figure->Hidden = Property.On;
            else if (std::strcmp(Property.Label, "Construction") == 0 && Property.Category == EditorPropertyCategory::Switch)
                Figure->Construction = Property.On;
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
}

} // namespace Frontier
