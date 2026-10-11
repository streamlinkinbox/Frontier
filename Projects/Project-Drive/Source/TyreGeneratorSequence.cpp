//============================================================================================================================================
//                                                     TYREGENERATORSEQUENCE.CPP
//============================================================================================================================================
// 📦 Builds the tyre's outliner rows and inspector sheets, and writes edited sheets back into the document.

#include "TyreGeneratorSequence.h"

#include <cstdio>
#include <cstring>

namespace Frontier {
namespace Drive {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                      ROW TINTS
//------------------------------------------------------------------------------------------------------------------------
// The outliner's own category colours, so nothing here is invented: a row's tint is the colour the panel already
//    uses for that kind of thing, and the selection bar picks it up for free.

constexpr float BodiesTint[3]   = { 0.874f, 0.902f, 0.961f };   // #DFE6F5
constexpr float GeometryTint[3] = { 0.886f, 0.910f, 0.941f };   // #E2E8F0
constexpr float SkyTint[3]      = { 0.353f, 0.663f, 1.000f };   // #5AA9FF
constexpr float CameraTint[3]   = { 0.204f, 0.780f, 0.349f };   // #34C759
constexpr float PeriwinkleTint[3] = { 0.424f, 0.467f, 1.000f }; // #6C77FF

void Seat(EditorInstance& Row, const char* Label, uint64_t Key, uint32_t Depth,
          EditorInstanceCategory Category, const float Tint[3], EditorGlyph Glyph) noexcept
{
    Row = EditorInstance{};
    std::snprintf(Row.Label, sizeof(Row.Label), "%s", Label);
    Row.InspectorKey = Key;
    Row.Depth        = Depth;
    Row.Category     = Category;
    Row.Glyph        = Glyph;
    Row.Tint[0] = Tint[0];
    Row.Tint[1] = Tint[1];
    Row.Tint[2] = Tint[2];
    Row.Narrowing = EditorNarrowing::Geometry;
    Row.Component = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHEET BUILDING
//------------------------------------------------------------------------------------------------------------------------

EditorPropertyGroup& OpenGroup(EditorSheet& Sheet, const char* Title, const char* Caption = nullptr) noexcept
{
    EditorPropertyGroup& Group = Sheet.Groups[Sheet.GroupCount++];
    Group = EditorPropertyGroup{};
    std::snprintf(Group.Title, sizeof(Group.Title), "%s", Title);
    if (Caption != nullptr)
    {
        std::snprintf(Group.Caption, sizeof(Group.Caption), "%s", Caption);
    }
    return Group;
}

/// 📦 A continuous, bounded figure the viewport answers for — the slider case.
void AddSlider(EditorPropertyGroup& Group, const char* Label, float Figure, float Minimum, float Maximum,
               uint32_t Decimals, const char* Unit, bool Hi = false) noexcept
{
    if (Group.PropertyCount >= kMaxEditorGroupProps)
    {
        return;
    }
    EditorProperty& Property = Group.Properties[Group.PropertyCount++];
    Property = EditorProperty{};
    std::snprintf(Property.Label, sizeof(Property.Label), "%s", Label);
    std::snprintf(Property.Unit, sizeof(Property.Unit), "%s", Unit);
    Property.Category = EditorPropertyCategory::Slider;
    Property.Figure   = Figure;
    Property.Minimum  = Minimum;
    Property.Maximum  = Maximum;
    Property.Decimals = Decimals;
    Property.Hi       = Hi;
}

/// 📦 A discrete or topology-changing figure — drawn as the same pill with the track withheld.
/// note  📝 The property still reads as a Slider to the panel; what makes it a type-in is the integer step
///       implied by Decimals 0 over a count range. The distinction the prototype settled lives in the ranges.
void AddCount(EditorPropertyGroup& Group, const char* Label, float Figure, float Minimum, float Maximum) noexcept
{
    AddSlider(Group, Label, Figure, Minimum, Maximum, 0u, "");
}

void AddReadout(EditorPropertyGroup& Group, const char* Label, const char* Text) noexcept
{
    if (Group.PropertyCount >= kMaxEditorGroupProps)
    {
        return;
    }
    EditorProperty& Property = Group.Properties[Group.PropertyCount++];
    Property = EditorProperty{};
    std::snprintf(Property.Label, sizeof(Property.Label), "%s", Label);
    std::snprintf(Property.Text, sizeof(Property.Text), "%s", Text);
    Property.Category = EditorPropertyCategory::Readout;
}

void AddSwitch(EditorPropertyGroup& Group, const char* Label, bool On) noexcept
{
    if (Group.PropertyCount >= kMaxEditorGroupProps)
    {
        return;
    }
    EditorProperty& Property = Group.Properties[Group.PropertyCount++];
    Property = EditorProperty{};
    std::snprintf(Property.Label, sizeof(Property.Label), "%s", Label);
    Property.Category = EditorPropertyCategory::Switch;
    Property.On       = On;
}

[[nodiscard]] const char* KindLabel(TreadLayerKind Kind) noexcept
{
    switch (Kind)
    {
    case TreadLayerKind::Circumferential: return "Circumferential";
    case TreadLayerKind::Lateral:         return "Lateral";
    case TreadLayerKind::Chevron:         return "Chevron";
    case TreadLayerKind::Sipe:            return "Sipe";
    case TreadLayerKind::Dimple:          return "Dimple";
    case TreadLayerKind::Hexagon:         return "Hexagon";
    default:                              return "Noise";
    }
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE PRESET
//------------------------------------------------------------------------------------------------------------------------

void SeatGrizzlyMagnum(TyreDocument& Document) noexcept
{
    Document.Tread = TreadSpecification{};
    Document.Tread.Width         = 285.0f;
    Document.Tread.Aspect        = 70.0f;
    Document.Tread.Rim           = 17.0f;
    Document.Tread.TreadDepth    = 15.0f;
    Document.Tread.TreadFraction = 0.92f;
    Document.Tread.Crown         = 3.0f;
    Document.Tread.Shoulder      = 14.0f;

    Document.Pattern.Name = "Grizzly Magnum";
    Document.Pattern.Layers.clear();

    auto Add = [&Document](TreadLayerKind Kind) -> TreadLayerSpecification&
    {
        Document.Pattern.Layers.push_back(TreadLayerSpecification{});
        TreadLayerSpecification& Layer = Document.Pattern.Layers.back();
        Layer.Kind = Kind;
        return Layer;
    };

    {
        TreadLayerSpecification& L = Add(TreadLayerKind::Circumferential);
        L.Position = -0.52f; L.Width = 17.0f; L.Zig = 7.0f; L.ZigCount = 26.0f;
    }
    {
        TreadLayerSpecification& L = Add(TreadLayerKind::Circumferential);
        L.Position = 0.52f; L.Width = 17.0f; L.Zig = 7.0f; L.ZigCount = 26.0f;
    }
    {
        TreadLayerSpecification& L = Add(TreadLayerKind::Circumferential);
        L.Position = 0.0f; L.Width = 12.0f; L.Zig = 4.0f; L.ZigCount = 26.0f; L.DepthFraction = 0.90f;
    }
    {
        TreadLayerSpecification& L = Add(TreadLayerKind::Chevron);
        L.Count = 26.0f; L.Angle = 26.0f; L.Width = 13.0f; L.From = -0.95f; L.To = 0.95f;
        L.Mirror = true; L.Curve = 0.25f;
    }
    {
        TreadLayerSpecification& L = Add(TreadLayerKind::Lateral);
        L.Count = 26.0f; L.Angle = -14.0f; L.Width = 7.0f; L.From = -0.5f; L.To = 0.5f;
        L.Phase = 0.5f; L.DepthFraction = 0.55f;
    }
    {
        TreadLayerSpecification& L = Add(TreadLayerKind::Sipe);
        L.Count = 52.0f; L.Angle = 8.0f; L.Width = 1.6f; L.From = -0.82f; L.To = 0.82f; L.DepthFraction = 0.40f;
    }
    {
        TreadLayerSpecification& L = Add(TreadLayerKind::Dimple);
        L.Count = 26.0f; L.Rows = 2.0f; L.Radius = 3.4f; L.Stagger = true;
        L.From = -0.3f; L.To = 0.3f; L.DepthFraction = 0.35f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE ROSTER
//------------------------------------------------------------------------------------------------------------------------

uint32_t TyreGeneratorSequence::FillRoster(EditorInstance* Rows, uint32_t First, uint32_t Capacity,
                                       uint32_t Depth) const noexcept
{
    if (Rows == nullptr || First >= Capacity)
    {
        return 0u;
    }

    const TreadDerivedValues Derived = DeriveTreadValues(Document.Tread);
    uint32_t Written = 0u;
    auto Room = [&]() noexcept -> bool { return First + Written < Capacity; };

    // ① the tyre itself
    if (!Room())
    {
        return Written;
    }
    EditorInstance& Tyre = Rows[First + Written++];
    Seat(Tyre, "Tyre", TyreInspectorKey(TyreSection::Tyre), Depth,
         EditorInstanceCategory::Geometry, BodiesTint, EditorGlyph::Orbit);
    std::snprintf(Tyre.Meta, sizeof(Tyre.Meta), "%s", Document.Pattern.Name.c_str());
    Tyre.Component = false;
    Tyre.Dynamic   = true;
    Tyre.Physics   = true;
    Tyre.KidCount  = 4u;

    // ② the carcass
    if (!Room())
    {
        return Written;
    }
    EditorInstance& Carcass = Rows[First + Written++];
    Seat(Carcass, "Carcass", TyreInspectorKey(TyreSection::Carcass), Depth + 1u,
         EditorInstanceCategory::Geometry, GeometryTint, EditorGlyph::Flat);
    std::snprintf(Carcass.Meta, sizeof(Carcass.Meta), "%g/%gR%g",
                  double(Document.Tread.Width), double(Document.Tread.Aspect), double(Document.Tread.Rim));

    // ③ the tread pattern, then one row per layer
    if (!Room())
    {
        return Written;
    }
    EditorInstance& Pattern = Rows[First + Written++];
    Seat(Pattern, "Tread pattern", TyreInspectorKey(TyreSection::Pattern), Depth + 1u,
         EditorInstanceCategory::Geometry, SkyTint, EditorGlyph::Lattice);
    std::snprintf(Pattern.Meta, sizeof(Pattern.Meta), "%u layers", unsigned(Document.Pattern.Layers.size()));
    Pattern.KidCount = uint32_t(Document.Pattern.Layers.size());
    std::snprintf(Pattern.Tag, sizeof(Pattern.Tag), "%s", "Generator");

    for (uint32_t I = 0; I < uint32_t(Document.Pattern.Layers.size()); ++I)
    {
        if (!Room())
        {
            return Written;
        }
        const TreadLayerSpecification& Layer = Document.Pattern.Layers[I];
        EditorInstance& Row = Rows[First + Written++];
        Seat(Row, KindLabel(Layer.Kind), TyreInspectorKey(TyreSection::Layer, I), Depth + 2u,
             EditorInstanceCategory::Geometry, SkyTint, EditorGlyph::Sliders);
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%.1f mm",
                      double(Layer.DepthFraction * Document.Tread.TreadDepth));
        if (Layer.Kind == TreadLayerKind::Noise)
        {
            std::snprintf(Row.Tag, sizeof(Row.Tag), "%s", "Field");
        }
    }

    // ④ the sidewall decals — authored, but phase 4 has not landed, so the row stands warned rather than absent
    if (!Room())
    {
        return Written;
    }
    EditorInstance& Decals = Rows[First + Written++];
    Seat(Decals, "Sidewall decals", TyreInspectorKey(TyreSection::Decals), Depth + 1u,
         EditorInstanceCategory::Geometry, CameraTint, EditorGlyph::Palette);
    Decals.Standing = EditorStanding::Warn;
    std::snprintf(Decals.StandingNote, sizeof(Decals.StandingNote), "%s", "Phase 4");
    std::snprintf(Decals.Meta, sizeof(Decals.Meta), "%s", "0 placed");

    // ⑤ the XPBD lattice
    if (!Room())
    {
        return Written;
    }
    EditorInstance& Lattice = Rows[First + Written++];
    Seat(Lattice, "XPBD lattice", TyreInspectorKey(TyreSection::Lattice), Depth + 1u,
         EditorInstanceCategory::Geometry, PeriwinkleTint, EditorGlyph::Lattice);
    std::snprintf(Lattice.Meta, sizeof(Lattice.Meta), "%ux%u",
                  unsigned(Document.Carcass.Rings), unsigned(Document.Carcass.Segments));
    Lattice.Physics = true;

    (void)Derived;
    return Written;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SHEETS
//------------------------------------------------------------------------------------------------------------------------

bool TyreGeneratorSequence::BuildSheet(uint64_t Key, EditorSheet* Sheet) const noexcept
{
    if (Sheet == nullptr || !IsTyreKey(Key))
    {
        return false;
    }

    *Sheet = EditorSheet{};
    Sheet->InspectorKey = Key;
    Sheet->Appearance   = EditorSheetAppearance::Tyre;

    const TreadSpecification&   Tread   = Document.Tread;
    const TyreCarcassAuthoring& Carcass = Document.Carcass;
    const TreadDerivedValues    Derived = DeriveTreadValues(Tread);

    switch (TyreSectionOf(Key))
    {
    case TyreSection::Tyre:
    {
        EditorPropertyGroup& Inflation = OpenGroup(*Sheet, "Inflation",
            "The authored value. Hoop and spoke compliance derive from it, so the slider means something physical.");
        AddSlider(Inflation, "Pressure", Carcass.InflationPressure, 80.0f, 420.0f, 0u, "kPa", true);

        EditorPropertyGroup& TreadGroup = OpenGroup(*Sheet, "Tread");
        AddSlider(TreadGroup, "Depth", Tread.TreadDepth, 2.0f, 22.0f, 1u, "mm");
        AddSlider(TreadGroup, "Wear", Tread.Wear, 0.0f, 1.0f, 2u, "");
        AddSlider(TreadGroup, "Bias", Tread.WearBias, -1.0f, 1.0f, 2u, "");

        EditorPropertyGroup& Rim = OpenGroup(*Sheet, "Rim bottoming",
            "Part 4 settled the damping ratio at 0.25. At 1.00 the stop pumped energy back into the carcass.");
        AddSlider(Rim, "Clearance", Carcass.RimStopClearance, 0.0f, 20.0f, 1u, "mm");
        AddSlider(Rim, "Damping", Carcass.RimBottomingDampingRatio, 0.0f, 1.5f, 2u, "");

        EditorPropertyGroup& Instance = OpenGroup(*Sheet, "Instance");
        char Text[48];
        std::snprintf(Text, sizeof(Text), "%.0f mm", double(2.0f * Derived.OuterRadius));
        AddReadout(Instance, "Outer", Text);
        std::snprintf(Text, sizeof(Text), "%.0f mm", double(Derived.Circumference));
        AddReadout(Instance, "Circumference", Text);
        AddReadout(Instance, "Source", "TreadMeshSolver");
        break;
    }
    case TyreSection::Carcass:
    {
        EditorPropertyGroup& Size = OpenGroup(*Sheet, "Marked size",
            "A standards value, not a continuum: 285 is a real tyre and 284.6 is not. Committed, never scrubbed.");
        AddCount(Size, "Width", Tread.Width, 125.0f, 395.0f);
        AddCount(Size, "Aspect", Tread.Aspect, 25.0f, 90.0f);
        AddCount(Size, "Rim", Tread.Rim, 10.0f, 26.0f);

        EditorPropertyGroup& Shape = OpenGroup(*Sheet, "Moulded shape");
        AddSlider(Shape, "Tread frac", Tread.TreadFraction, 0.6f, 1.0f, 2u, "");
        AddSlider(Shape, "Crown", Tread.Crown, 0.0f, 10.0f, 1u, "mm");
        AddSlider(Shape, "Shoulder", Tread.Shoulder, 2.0f, 40.0f, 1u, "mm");
        AddSlider(Shape, "Bulge", Tread.Bulge, 0.4f, 1.8f, 2u, "");
        AddSlider(Shape, "Bead seat", Tread.RimWidthFraction, 0.5f, 0.95f, 2u, "");
        break;
    }
    case TyreSection::Pattern:
    case TyreSection::Layer:
    {
        Sheet->Appearance = EditorSheetAppearance::TyreTread;

        // Group 0 IS the layer sequence. Tyre Generator reads it; the quick strip skips it, because an ordered list
        //    of arbitrary length is the one thing a strip cannot hold.
        EditorPropertyGroup& Sequence = OpenGroup(*Sheet, "Layer sequence");
        const uint32_t Picked = (TyreSectionOf(Key) == TyreSection::Layer) ? TyreOrdinalOf(Key)
                                                                           : Document.PickedLayer;
        for (uint32_t I = 0; I < uint32_t(Document.Pattern.Layers.size()); ++I)
        {
            if (Sequence.PropertyCount >= kMaxEditorGroupProps)
            {
                break;
            }
            const TreadLayerSpecification& Layer = Document.Pattern.Layers[I];
            AddSlider(Sequence, KindLabel(Layer.Kind), Layer.DepthFraction * Tread.TreadDepth,
                      0.0f, Tread.TreadDepth, 1u, "mm");
            Sequence.Properties[Sequence.PropertyCount - 1u].On = (I == Picked);
        }

        if (Picked < uint32_t(Document.Pattern.Layers.size()))
        {
            const TreadLayerSpecification& Layer = Document.Pattern.Layers[Picked];
            char Title[24];
            std::snprintf(Title, sizeof(Title), "Layer %u", unsigned(Picked + 1u));
            EditorPropertyGroup& Detail = OpenGroup(*Sheet, Title);
            AddSlider(Detail, "Depth", Layer.DepthFraction, 0.05f, 1.0f, 2u, "x");
            AddSlider(Detail, "Width", Layer.Width, 0.5f, 40.0f, 1u, "mm");
            switch (Layer.Kind)
            {
            case TreadLayerKind::Circumferential:
                AddSlider(Detail, "Seat", Layer.Position, -1.0f, 1.0f, 2u, "x");
                AddSlider(Detail, "Zig", Layer.Zig, 0.0f, 30.0f, 1u, "mm");
                AddCount(Detail, "Periods", Layer.ZigCount, 1.0f, 200.0f);
                break;
            case TreadLayerKind::Dimple:
                AddCount(Detail, "Per row", Layer.Count, 1.0f, 400.0f);
                AddCount(Detail, "Rows", Layer.Rows, 1.0f, 12.0f);
                AddSlider(Detail, "Radius", Layer.Radius, 0.5f, 20.0f, 1u, "mm");
                AddSwitch(Detail, "Stagger", Layer.Stagger);
                break;
            case TreadLayerKind::Hexagon:
                AddSlider(Detail, "Cell R", Layer.Size, 2.0f, 40.0f, 1u, "mm");
                AddSlider(Detail, "Rotate", Layer.Rotate, 0.0f, 60.0f, 1u, "deg");
                break;
            case TreadLayerKind::Noise:
                break;
            default:
                AddCount(Detail, "Repeats", Layer.Count, 1.0f, 400.0f);
                AddSlider(Detail, "Lean", Layer.Angle, -70.0f, 70.0f, 1u, "deg");
                AddSlider(Detail, "From", Layer.From, -1.0f, 1.0f, 2u, "x");
                AddSlider(Detail, "To", Layer.To, -1.0f, 1.0f, 2u, "x");
                AddSlider(Detail, "Bow", Layer.Curve, -1.0f, 1.0f, 2u, "");
                if (Layer.Kind == TreadLayerKind::Chevron)
                {
                    AddSwitch(Detail, "Mirror", Layer.Mirror);
                }
                break;
            }
        }
        break;
    }
    case TyreSection::Decals:
    {
        EditorPropertyGroup& Decals = OpenGroup(*Sheet, "Sidewall decals",
            "Phase 4. Lettering becomes a decal projection so it stays editable instead of being burnt in.");
        AddReadout(Decals, "Placements", "0");
        AddReadout(Decals, "Projection", "72-DecalProjection");
        break;
    }
    case TyreSection::Lattice:
    {
        Sheet->Appearance = EditorSheetAppearance::TyreLattice;
        EditorPropertyGroup& Solver = OpenGroup(*Sheet, "Solver");
        AddCount(Solver, "Iterations", float(Carcass.Iterations), 1.0f, 64.0f);
        AddCount(Solver, "Rings", float(Carcass.Rings), 4.0f, 48.0f);
        AddCount(Solver, "Segments", float(Carcass.Segments), 12.0f, 192.0f);

        EditorPropertyGroup& Derived2 = OpenGroup(*Sheet, "Compliance",
            "Derived from inflation pressure. The override exists because the parity harness pins them.");
        char Text[48];
        std::snprintf(Text, sizeof(Text), "%.2e m/N", double(Carcass.HoopCompliance()));
        AddReadout(Derived2, "Hoop", Text);
        std::snprintf(Text, sizeof(Text), "%.2e m/N", double(Carcass.SpokeCompliance()));
        AddReadout(Derived2, "Spoke", Text);
        AddSwitch(Derived2, "Override", Carcass.Advanced);
        break;
    }
    default:
        return false;
    }

    return Sheet->GroupCount > 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE WRITE-BACK
//------------------------------------------------------------------------------------------------------------------------

bool TyreGeneratorSequence::ApplySheet(uint64_t Key, const EditorSheet& Sheet) noexcept
{
    if (!IsTyreKey(Key) || Sheet.GroupCount == 0u)
    {
        return false;
    }

    bool Changed = false;
    auto Take = [&Changed](float& Target, float Figure) noexcept
    {
        if (Target != Figure)
        {
            Target  = Figure;
            Changed = true;
        }
    };
    auto TakeCount = [&Changed](uint32_t& Target, float Figure) noexcept
    {
        const uint32_t Next = uint32_t(Figure + 0.5f);
        if (Target != Next)
        {
            Target  = Next;
            Changed = true;
        }
    };

    switch (TyreSectionOf(Key))
    {
    case TyreSection::Tyre:
        Take(Document.Carcass.InflationPressure, Sheet.Groups[0].Properties[0].Figure);
        if (Sheet.GroupCount > 1u && Sheet.Groups[1].PropertyCount >= 3u)
        {
            Take(Document.Tread.TreadDepth, Sheet.Groups[1].Properties[0].Figure);
            Take(Document.Tread.Wear,       Sheet.Groups[1].Properties[1].Figure);
            Take(Document.Tread.WearBias,   Sheet.Groups[1].Properties[2].Figure);
        }
        if (Sheet.GroupCount > 2u && Sheet.Groups[2].PropertyCount >= 2u)
        {
            Take(Document.Carcass.RimStopClearance,         Sheet.Groups[2].Properties[0].Figure);
            Take(Document.Carcass.RimBottomingDampingRatio, Sheet.Groups[2].Properties[1].Figure);
        }
        break;
    case TyreSection::Carcass:
        if (Sheet.Groups[0].PropertyCount >= 3u)
        {
            Take(Document.Tread.Width,  Sheet.Groups[0].Properties[0].Figure);
            Take(Document.Tread.Aspect, Sheet.Groups[0].Properties[1].Figure);
            Take(Document.Tread.Rim,    Sheet.Groups[0].Properties[2].Figure);
        }
        if (Sheet.GroupCount > 1u && Sheet.Groups[1].PropertyCount >= 5u)
        {
            Take(Document.Tread.TreadFraction,    Sheet.Groups[1].Properties[0].Figure);
            Take(Document.Tread.Crown,            Sheet.Groups[1].Properties[1].Figure);
            Take(Document.Tread.Shoulder,         Sheet.Groups[1].Properties[2].Figure);
            Take(Document.Tread.Bulge,            Sheet.Groups[1].Properties[3].Figure);
            Take(Document.Tread.RimWidthFraction, Sheet.Groups[1].Properties[4].Figure);
        }
        break;
    case TyreSection::Layer:
    case TyreSection::Pattern:
    {
        const uint32_t Picked = (TyreSectionOf(Key) == TyreSection::Layer) ? TyreOrdinalOf(Key)
                                                                           : Document.PickedLayer;
        if (Picked < uint32_t(Document.Pattern.Layers.size()) && Sheet.GroupCount > 1u)
        {
            TreadLayerSpecification& Layer = Document.Pattern.Layers[Picked];
            const EditorPropertyGroup& Detail = Sheet.Groups[1];
            if (Detail.PropertyCount >= 2u)
            {
                Take(Layer.DepthFraction, Detail.Properties[0].Figure);
                Take(Layer.Width,         Detail.Properties[1].Figure);
            }
        }
        break;
    }
    case TyreSection::Lattice:
        if (Sheet.Groups[0].PropertyCount >= 3u)
        {
            TakeCount(Document.Carcass.Iterations, Sheet.Groups[0].Properties[0].Figure);
            TakeCount(Document.Carcass.Rings,      Sheet.Groups[0].Properties[1].Figure);
            TakeCount(Document.Carcass.Segments,   Sheet.Groups[0].Properties[2].Figure);
        }
        break;
    default:
        break;
    }

    return Changed;
}

}   // namespace Drive
}   // namespace Frontier
