//============================================================================================================================================
//                                                      TYREINSPECTORPANEL.CPP
//============================================================================================================================================
// 📦 Draws the tyre's two editor surfaces from one property sheet: the quick strip in the shared Inspector, and
//    Tyre Generator, the dockable asset window that owns the tread layer sequence.

#include "TyreInspectorPanel.h"

#include "ControlPanel.h"
#include "EditorInstance.h"

#include "../Generators/Tyre/TyreProfileSpecification.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                        TOKENS
//------------------------------------------------------------------------------------------------------------------------
// The inspector's own seats, so the tyre page cannot drift from the pages beside it.

constexpr ImU32 CardInsetTint  = IM_COL32(26, 26, 26, 255);    // the card seat
constexpr ImU32 StrokeTint = IM_COL32(255, 255, 255, 13);  // rgba(255,255,255,.05)
constexpr ImU32 TextTint   = IM_COL32(240, 240, 240, 255);
constexpr ImU32 DimTint    = IM_COL32(136, 136, 136, 255);
constexpr ImU32 FaintTint  = IM_COL32(92, 92, 92, 255);
constexpr ImU32 FigureColumnWidth = IM_COL32(255, 255, 255, 255);
constexpr ImU32 FractionColumnWidth = IM_COL32(94, 94, 94, 255);  // the dimmed decimal of a display figure
constexpr ImU32 SeatedFaceSize = IM_COL32(42, 42, 42, 255);

constexpr float CardCornerRadius = 18.0f;
constexpr float CardSidePad   = 17.0f;
constexpr float MeterWidth       = 3.0f;
constexpr float MeterHeight       = 13.0f;

//------------------------------------------------------------------------------------------------------------------------
//                                                       SHEET READS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The row tint as a draw colour, so a card's bar matches the glyph the outliner drew.
[[nodiscard]] ImU32 RowTint(const EditorInstance& Row, float Alpha = 1.0f) noexcept
{
    const int R = int(Row.Tint[0] * 255.0f + 0.5f);
    const int G = int(Row.Tint[1] * 255.0f + 0.5f);
    const int B = int(Row.Tint[2] * 255.0f + 0.5f);
    return IM_COL32(R, G, B, int(Alpha * 255.0f + 0.5f));
}

/// 📦 Splits a figure into its whole and fractional halves, the way the references print a display value.
/// note  The fraction is drawn dimmer, which is what makes a large figure readable at a glance.
void SplitFigure(float Value, uint32_t Decimals, char* Whole, size_t WholeSize, char* Fraction, size_t FractionSize) noexcept
{
    char Text[32];
    std::snprintf(Text, sizeof(Text), "%.*f", int(Decimals), double(Value));
    const char* Dot = std::strchr(Text, '.');
    if (Dot == nullptr)
    {
        std::snprintf(Whole, WholeSize, "%s", Text);
        Fraction[0] = '\0';
        return;
    }
    const size_t Lead = size_t(Dot - Text);
    std::snprintf(Whole, WholeSize, "%.*s", int(Lead), Text);
    std::snprintf(Fraction, FractionSize, "%s", Dot);
}

/// 📦 The group's headline property — the first slider, which is the figure the card is about.
[[nodiscard]] const EditorProperty* HeadlineOf(const EditorPropertyGroup& Group) noexcept
{
    for (uint32_t I = 0; I < Group.PropertyCount; ++I)
    {
        if (Group.Properties[I].Category == EditorPropertyCategory::Slider)
        {
            return &Group.Properties[I];
        }
    }
    return nullptr;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                     TREAD PREVIEW
//------------------------------------------------------------------------------------------------------------------------
// The grooves as centreline polylines in the developed domain (x circumferential, y lateral). This is a PREVIEW:
//    it strokes where the solver will cut, it does not run the boolean partition. TreadRegionSolver remains the
//    geometry of record; this exists so the generator shows the tyre while it is being authored.

constexpr float HalfTurnRadians = 3.14159265358979323846f;

struct GrooveStroke
{
    std::vector<ImVec2> Points;   // x = circumferential [mm], y = lateral [mm]
    float Width = 8.0f;           // [mm]
};

[[nodiscard]] float ZigWave(float Phase) noexcept
{
    const float Wrapped = Phase - std::floor(Phase);
    return (Wrapped < 0.5f) ? (4.0f * Wrapped - 1.0f) : (3.0f - 4.0f * Wrapped);
}

void CollectGrooves(const TreadLayerSpecification& Layer, const TreadSpecification& Specification,
                    const TreadDerivedValues& Derived, std::vector<GrooveStroke>& Out) noexcept
{
    const float Half = Derived.TreadHalf;
    const float Loop = Derived.Circumference;
    (void)Specification;

    if (Layer.Kind == TreadLayerKind::Noise)
    {
        return;                                            // carries no polygons, by design
    }

    if (Layer.Kind == TreadLayerKind::Circumferential)
    {
        GrooveStroke Stroke;
        Stroke.Width = Layer.Width;
        const int Steps = (Layer.Zig > 0.0f) ? int(Layer.ZigCount * 8.0f) + 1 : 2;
        for (int I = 0; I <= Steps; ++I)
        {
            const float T = float(I) / float(Steps);
            Stroke.Points.push_back(ImVec2(T * Loop, Layer.Position * Half + Layer.Zig * ZigWave(T * Layer.ZigCount)));
        }
        Out.push_back(std::move(Stroke));
        return;
    }

    if (Layer.Kind == TreadLayerKind::Dimple)
    {
        const int Rows  = Layer.Rows  < 1.0f ? 1 : int(Layer.Rows + 0.5f);
        const int Count = Layer.Count < 1.0f ? 1 : int(Layer.Count + 0.5f);
        for (int R = 0; R < Rows; ++R)
        {
            const float Fraction = (Rows == 1) ? 0.5f : float(R) / float(Rows - 1);
            const float Lateral  = (Layer.From + (Layer.To - Layer.From) * Fraction) * Half;
            for (int I = 0; I < Count; ++I)
            {
                const float Offset = (Layer.Stagger && (R % 2) != 0) ? 0.5f : 0.0f;
                const float Around = (float(I) + Offset + Layer.Phase) / float(Count) * Loop;
                GrooveStroke Disc;
                Disc.Width = Layer.Radius * 2.0f;
                Disc.Points.push_back(ImVec2(Around, Lateral));
                Disc.Points.push_back(ImVec2(Around, Lateral));   // a dot: stroked round, it reads as the disc
                Out.push_back(std::move(Disc));
            }
        }
        return;
    }

    // The lateral family: Lateral, Chevron, Sipe — and Hexagon, previewed as its two lattice diagonals.
    const int   Count = Layer.Count < 1.0f ? 1 : int(Layer.Count + 0.5f);
    const int   Steps = (Layer.Curve != 0.0f) ? 8 : 1;
    const float Lean  = std::tan((Layer.Angle < -75.0f ? -75.0f : (Layer.Angle > 75.0f ? 75.0f : Layer.Angle)) * HalfTurnRadians / 180.0f);
    const bool  Mirrored = (Layer.Kind == TreadLayerKind::Chevron) && Layer.Mirror;

    for (int I = 0; I < Count; ++I)
    {
        const float Base = (float(I) + Layer.Phase) / float(Count) * Loop;
        for (int Side = 0; Side < (Mirrored ? 2 : 1); ++Side)
        {
            float From = Layer.From * Half;
            float To   = Layer.To * Half;
            float Sign = 1.0f;
            if (Mirrored)
            {
                if (Side == 0) { From = 0.0f; To = Layer.To * Half; }
                else           { From = Layer.From * Half; To = 0.0f; Sign = -1.0f; }
            }
            GrooveStroke Stroke;
            Stroke.Width = Layer.Width;
            for (int S = 0; S <= Steps; ++S)
            {
                const float T       = float(S) / float(Steps);
                const float Lateral = From + (To - From) * T;
                const float Bow     = Layer.Curve * (To - From) * 0.5f * (1.0f - (2.0f * T - 1.0f) * (2.0f * T - 1.0f));
                Stroke.Points.push_back(ImVec2(Base + Sign * ((Lateral - From) * Lean + Bow), Lateral));
            }
            Out.push_back(std::move(Stroke));
        }
    }
}

/// 📦 Draws the tyre face-on: the moulded annulus with the pattern wrapped around it, which is the one view
///    that reads as "a tyre" at a glance.
void DrawTyreFace(ImDrawList* Draw, ImVec2 Centre, float Pixels, const TreadSpecification& Specification,
                  const TreadPatternSpecification& Pattern, int PickedLayer) noexcept
{
    const TreadDerivedValues Derived = DeriveTreadValues(Specification);
    const float Scale = Pixels / Derived.OuterRadius;             // [px/mm]
    const float Outer = Derived.OuterRadius * Scale;
    const float Bead  = Derived.RimRadius * Scale;

    Draw->AddCircleFilled(Centre, Outer, IM_COL32(28, 28, 30, 255), 96);
    Draw->AddCircle(Centre, Outer, IM_COL32(255, 255, 255, 46), 96, 1.2f);

    // The tread band, as a ring: the pattern is drawn inside it, wrapped by angle.
    const float BandOuter = Outer;
    const float BandInner = Outer * 0.80f;
    Draw->AddCircleFilled(Centre, BandOuter, IM_COL32(54, 54, 58, 255), 96);
    Draw->AddCircleFilled(Centre, BandInner, IM_COL32(20, 20, 22, 255), 96);

    std::vector<GrooveStroke> Strokes;
    for (uint32_t L = 0; L < uint32_t(Pattern.Layers.size()); ++L)
    {
        const TreadLayerSpecification& Layer = Pattern.Layers[L];
        Strokes.clear();
        CollectGrooves(Layer, Specification, Derived, Strokes);

        const bool  Picked = (int(L) == PickedLayer);
        const float Depth  = Layer.DepthFraction;
        const ImU32 Tint   = Picked ? IM_COL32(0x5A, 0xA9, 0xFF, 235)
                                    : IM_COL32(10, 10, 12, ImU8(170 + 85 * Depth));

        for (const GrooveStroke& Stroke : Strokes)
        {
            if (Stroke.Points.size() < 2u)
            {
                continue;
            }
            // Lateral maps across the band; circumference maps to angle.
            const float Thickness = (Stroke.Width * Scale) * 0.55f;
            std::vector<ImVec2> Wrapped;
            Wrapped.reserve(Stroke.Points.size());
            for (const ImVec2& P : Stroke.Points)
            {
                const float Angle  = (P.x / Derived.Circumference) * 2.0f * HalfTurnRadians - HalfTurnRadians * 0.5f;
                const float Across = 0.5f + 0.5f * (P.y / Derived.AcrossHalf);
                const float Radius = BandInner + (BandOuter - BandInner) * Across;
                Wrapped.push_back(ImVec2(Centre.x + std::cos(Angle) * Radius, Centre.y + std::sin(Angle) * Radius));
            }
            if (Wrapped.size() == 2u && Stroke.Points[0].x == Stroke.Points[1].x)
            {
                Draw->AddCircleFilled(Wrapped[0], Thickness, Tint, 10);   // a dimple
            }
            else
            {
                Draw->AddPolyline(Wrapped.data(), int(Wrapped.size()), Tint, ImDrawFlags_None,
                                  Thickness < 1.0f ? 1.0f : Thickness);
            }
        }
    }

    // The wheel face, so the bore does not read as a hole in the picture.
    Draw->AddCircleFilled(Centre, Bead, IM_COL32(23, 23, 26, 255), 64);
    Draw->AddCircle(Centre, Bead, IM_COL32(255, 255, 255, 30), 64, 1.0f);
    Draw->AddCircle(Centre, BandInner, IM_COL32(255, 255, 255, 20), 96, 1.0f);
}

/// 📦 The px/mm the cross-section draws at: the width decides it alone, so the drawing fills its box instead of
///    leaving the dead band a min(x,y) fit leaves under a shape far wider than it is deep.
[[nodiscard]] float SectionScale(float Width, const TreadSpecification& Specification) noexcept
{
    const TreadDerivedValues Derived = DeriveTreadValues(Specification);
    return (Width - 18.0f) / (2.0f * Derived.AcrossHalf * 1.10f);
}

/// 📦 The height that cross-section then needs, which is what the viewport reserves for it.
[[nodiscard]] float MeasureTyreSection(float Width, const TreadSpecification& Specification) noexcept
{
    const TreadDerivedValues Derived = DeriveTreadValues(Specification);
    const float Deep = Specification.Crown + Derived.ShoulderRadius + Specification.TreadDepth * 1.7f + 6.0f;
    return Deep * SectionScale(Width, Specification) + 22.0f;
}

/// 📦 Draws the moulded cross-section from EvaluateTyreProfile, with a dashed floor per distinct cut depth.
void DrawTyreSection(ImDrawList* Draw, ImVec2 Min, ImVec2 Max, const TreadSpecification& Specification,
                     const TreadPatternSpecification& Pattern) noexcept
{
    const TreadDerivedValues Derived = DeriveTreadValues(Specification);
    const float Scale = SectionScale(Max.x - Min.x, Specification);
    const ImVec2 Origin(( Min.x + Max.x) * 0.5f, Min.y + 14.0f);

    auto At = [&](float Lateral, float Radius) noexcept -> ImVec2
    { return ImVec2(Origin.x + Lateral * Scale, Origin.y + (Derived.OuterRadius - Radius) * Scale); };

    constexpr int SectionSamples = 96;

    // the rubber body
    std::vector<ImVec2> Body;
    Body.reserve(SectionSamples + 3);
    for (int I = 0; I <= SectionSamples; ++I)
    {
        const float Lateral = -Derived.AcrossHalf + 2.0f * Derived.AcrossHalf * float(I) / float(SectionSamples);
        const TyreProfileSample S = EvaluateTyreProfile(Lateral, Specification, Derived);
        Body.push_back(At(S.Lateral, S.Radius));
    }
    Body.push_back(ImVec2(At(Derived.AcrossHalf, 0.0f).x, Max.y - 6.0f));
    Body.push_back(ImVec2(At(-Derived.AcrossHalf, 0.0f).x, Max.y - 6.0f));
    Draw->AddConvexPolyFilled(Body.data(), int(Body.size()), IM_COL32(255, 255, 255, 12));

    // one dashed floor per distinct depth the pattern cuts
    for (const TreadLayerSpecification& Layer : Pattern.Layers)
    {
        if (Layer.Kind == TreadLayerKind::Noise)
        {
            continue;
        }
        const float Depth = Layer.DepthFraction * Specification.TreadDepth;
        ImVec2 Last{};
        for (int I = 0; I <= SectionSamples; ++I)
        {
            const float Lateral = -Derived.TreadHalf + 2.0f * Derived.TreadHalf * float(I) / float(SectionSamples);
            const TyreProfileSample S = EvaluateTyreProfile(Lateral, Specification, Derived);
            const ImVec2 Point = At(S.Lateral, S.Radius - Depth * S.ContactWeight);
            if (I > 0 && (I % 2) == 0)
            {
                Draw->AddLine(Last, Point, IM_COL32(0x5A, 0xA9, 0xFF, 90), 1.0f);
            }
            Last = Point;
        }
    }

    // the moulded surface
    for (int I = 1; I <= SectionSamples; ++I)
    {
        const float A = -Derived.AcrossHalf + 2.0f * Derived.AcrossHalf * float(I - 1) / float(SectionSamples);
        const float B = -Derived.AcrossHalf + 2.0f * Derived.AcrossHalf * float(I)     / float(SectionSamples);
        const TyreProfileSample SA = EvaluateTyreProfile(A, Specification, Derived);
        const TyreProfileSample SB = EvaluateTyreProfile(B, Specification, Derived);
        Draw->AddLine(At(SA.Lateral, SA.Radius), At(SB.Lateral, SB.Radius), IM_COL32(255, 255, 255, 230), 1.6f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE PROPERTY
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Draws one property as a labelled row: the label on the left, the widget filling the rest.
/// note  The widget comes from the property's own Category. Nothing here decides between a slider and a type-in —
///       that decision belongs to whoever filled the sheet, which is the prototype's finding carried into the engine.
void RecordProperty(ControlPanel& Controls, EditorProperty& Property, float LabelWidth) noexcept
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const float  RowWidth = ImGui::GetContentRegionAvail().x;

    constexpr float RowHeight = 30.0f;
    Draw->AddText(ImVec2(Origin.x, Origin.y + (RowHeight - ImGui::GetFontSize()) * 0.5f), DimTint, Property.Label);

    ImGui::SetCursorScreenPos(ImVec2(Origin.x + LabelWidth, Origin.y));
    ImGui::PushID(Property.Label);
    ImGui::BeginChild(Property.Label, ImVec2(ImGui::GetContentRegionAvail().x, RowHeight),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    switch (Property.Category)
    {
    case EditorPropertyCategory::Slider:
        // The pill carries the figure; the track is withheld for a value that is discrete or rebuilds topology.
        Controls.SliderPill("##figure", &Property.Figure, Property.Minimum, Property.Maximum,
                            Property.Decimals, Property.Unit, Property.Hi, false, true);
        break;
    case EditorPropertyCategory::Switch:
        Controls.Switch("##switch", &Property.On);
        break;
    case EditorPropertyCategory::Select:
        Controls.DropDown("##select", &Property.Picked, Property.Options, Property.OptionCount);
        break;
    case EditorPropertyCategory::AxisVec3:
        Controls.AxisVec3("##axes", Property.Axes, Property.AxisStep, Property.Editable);
        break;
    case EditorPropertyCategory::Colour:
        Controls.ColourChip("##tint", Property.ColourTint);
        break;
    default:
        Controls.Readout(Property.Text);
        break;
    }

    ImGui::EndChild();
    ImGui::PopID();

    // The row is claimed with a real item: a bare cursor move does not grow the parent, and ImGui says so.
    ImGui::SetCursorScreenPos(Origin);
    ImGui::Dummy(ImVec2(RowWidth, RowHeight + 6.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ONE CARD
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Draws one group as a card: a tint bar, the title, the headline figure, then a row per property.
/// out   the cursor is left below the card
float RecordCard(ControlPanel& Controls, EditorPropertyGroup& Group, ImU32 Tint, float Width) noexcept
{
    ImDrawList* Draw   = ImGui::GetWindowDrawList();
    const ImVec2 Origin = ImGui::GetCursorScreenPos();

    const EditorProperty* Headline = HeadlineOf(Group);
    const bool HasCaption = Group.Caption[0] != '\0';

    // The card's height has to be known before its seat is filled, so it is measured from what it will hold.
    //    The caption is MEASURED, not assumed: a two-line caption and a four-line caption are both common, and
    //    guessing one height for both is what put prose through the first slider.
    const float CaptionWrap = Width - CardSidePad * 2.0f;
    float CaptionHeight = 0.0f;
    if (HasCaption)
    {
        CaptionHeight = ImGui::GetFont()->CalcTextSizeA(11.0f, FLT_MAX, CaptionWrap, Group.Caption).y + 12.0f;
    }

    float Height = 14.0f + 17.0f;                                   // top pad + title line
    if (Headline != nullptr)  { Height += 46.0f; }                  // the display figure
    Height += CaptionHeight;
    Height += float(Group.PropertyCount) * 36.0f + 12.0f;

    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + Height), CardInsetTint, CardCornerRadius);
    Draw->AddRect(Origin, ImVec2(Origin.x + Width, Origin.y + Height), StrokeTint, CardCornerRadius);

    // ① the tint bar and the title
    const float TitleY = Origin.y + 15.0f;
    Draw->AddRectFilled(ImVec2(Origin.x + CardSidePad, TitleY),
                        ImVec2(Origin.x + CardSidePad + MeterWidth, TitleY + MeterHeight), Tint, 1.5f);
    Draw->AddText(ImVec2(Origin.x + CardSidePad + MeterWidth + 9.0f, TitleY - 1.0f), TextTint, Group.Title);

    float Cursor = TitleY + 20.0f;

    // ② the display figure, whole bright and fraction dimmed
    if (Headline != nullptr)
    {
        char Whole[24] = {}, Fraction[16] = {};
        SplitFigure(Headline->Figure, Headline->Decimals, Whole, sizeof(Whole), Fraction, sizeof(Fraction));
        ImFont* Font = ImGui::GetFont();
        const float Size = 30.0f;
        const ImVec2 WholeSize = Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, Whole);
        Draw->AddText(Font, Size, ImVec2(Origin.x + CardSidePad, Cursor), FigureColumnWidth, Whole);
        float Pen = Origin.x + CardSidePad + WholeSize.x;
        if (Fraction[0] != '\0')
        {
            const ImVec2 FractionSize = Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, Fraction);
            Draw->AddText(Font, Size, ImVec2(Pen, Cursor), FractionColumnWidth, Fraction);
            Pen += FractionSize.x;
        }
        if (Headline->Unit[0] != '\0')
        {
            Draw->AddText(Font, 12.0f, ImVec2(Pen + 5.0f, Cursor + Size - 15.0f), DimTint, Headline->Unit);
        }
        Cursor += 42.0f;
    }

    // ③ the caption, which is the card's one line of prose
    if (HasCaption)
    {
        Draw->AddText(ImGui::GetFont(), 11.0f, ImVec2(Origin.x + CardSidePad, Cursor), DimTint,
                      Group.Caption, nullptr, CaptionWrap);
        Cursor += CaptionHeight;
    }

    // ④ the properties
    const float Inner = Width - CardSidePad * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + CardSidePad, Cursor));
    ImGui::PushID(Group.Title);
    ImGui::BeginChild("##card-rows", ImVec2(Inner, Height - (Cursor - Origin.y) - 8.0f),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (uint32_t I = 0; I < Group.PropertyCount; ++I)
    {
        ImGui::PushID(int(I));
        RecordProperty(Controls, Group.Properties[I], 86.0f);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopID();

    // Claim the whole card, so the next card lands below it and the parent grows to hold both.
    ImGui::SetCursorScreenPos(Origin);
    ImGui::Dummy(ImVec2(Width, Height + 12.0f));
    return Height + 12.0f;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE QUICK STRIP
//------------------------------------------------------------------------------------------------------------------------

void RecordTyreInspector(ControlPanel& Controls, EditorInstance& Picked, EditorSheet& Sheet)
{
    const float Width = ImGui::GetContentRegionAvail().x - 28.0f;
    if (Width < 120.0f)
    {
        return;
    }

    ImVec2 Origin = ImGui::GetCursorScreenPos();
    Origin.x += 14.0f;
    Origin.y += 12.0f;
    ImGui::SetCursorScreenPos(Origin);

    const ImU32 Tint = RowTint(Picked);
    for (uint32_t I = 0; I < Sheet.GroupCount; ++I)
    {
        RecordCard(Controls, Sheet.Groups[I], Tint, Width);
    }

    ImGui::Dummy(ImVec2(Width, 8.0f));
}


}   // namespace Frontier
