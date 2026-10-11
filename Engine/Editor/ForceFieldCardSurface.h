//==============================================================================================================================================
//                                                      FORCEFIELDCARDSURFACE.H
//==============================================================================================================================================
// 📦 The native force field inspector — the browser's one panel, ported chrome for chrome.
//
//    Reference: Experimental/ParticleEditor/js/app.js renderForcesInspector/forceCard, over the taxonomy
//    in js/forcefields.js. Held to it by Tools/Build/ForceFieldParity.py.
//
// 🔴 THE PANEL IS GROUPED BY WHAT A FIELD RETURNS, AND THAT IS THE WHOLE DESIGN.
//    Wind and gravity are not the same quantity. A Flow is a velocity the receiver is dragged toward at
//    its own coupling; an Accelerate is added regardless of coupling; a Damp scales velocity down. Three
//    headings, each with a sentence saying so, because an author who cannot see the difference will file
//    "gravity feels wrong on light debris" as a tuning bug forever. The grouping is read from
//    FactsOf(Kind).Give rather than hand-sorted here, so a kind cannot go missing from the panel.
//
// 📐 FOUR THINGS THIS PANEL SAYS OUT LOUD.
//    ① A field that reaches Everywhere draws NO centre and NO radius. Not a greyed-out pair — absent.
//       Everywhere is not a very large sphere, and a radius control on gravity is a lie with a handle.
//    ② The cost card counts what sums into the lattice against what is evaluated per receiver, because
//       that is the consequence of the taxonomy and the one number that changes when you add a field.
//    ③ A kind with no shader path yet is LISTED, with a plain line saying it does nothing. A control that
//       silently does nothing is worse than one that admits it.
//    ④ Per-system attractors are not in the list and the header says so: they move with their system and
//       die with it, so showing them here would invite deleting one and watching it come back.
//
// This file owns no painters. Every pill, switch, select, note and card shell is GasCards', because the
//    stylesheet is shared and two painters that resemble each other drift. What is here is the stack: the
//    order, the grouping, the heights, and the hit regions.

#pragma once

#include "GasCardSurface.h"

#include "../VolumetricDynamics/ForceFieldSet.h"

#include <cstdio>
#include <cstring>

namespace Frontier::ForceCards
{

namespace Kit = Frontier::GasCards;

//------------------------------------------------------------------------------------------------------------------------
//                                                     METRICS
//------------------------------------------------------------------------------------------------------------------------

constexpr float GroupNoteSize = 10.0f;    // [px] the sentence under each contribution heading
constexpr float GroupNoteGap  = 10.0f;    // [px] its margin below
constexpr float AdderTall     = Kit::ActionTall;   // [px] one "+ Kind" button — the editor's action button
constexpr float AdderGap      =  8.0f;    // [px] between them
constexpr uint32_t AddersAcross = 2u;     // [-]  the add grid is two wide at the inspector's width

constexpr uint32_t MaxFields = 16u;       // [-]  as many as a card stack can show without becoming a list

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SUBJECT
//------------------------------------------------------------------------------------------------------------------------

// What the panel is shown. A ForceField is the engine's own struct — the panel does not get a parallel
//    copy of it, because two structs describing one field is how a port starts disagreeing with itself.
struct ForceCardSubject
{
    ForceField Fields[MaxFields];
    uint32_t   Count      = 0u;
    uint32_t   Picked     = 0u;      // [-] which card is expanded; the rest draw collapsed
    uint32_t   OwnedByKin = 0u;      // [-] ④ per-system attractors, counted but not listed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   WORDS
//------------------------------------------------------------------------------------------------------------------------

inline const char* ContributionCaption(ForceContribution Give) noexcept
{
    switch (Give)
    {
        case ForceContribution::Flow:       return "FLOW - m/s";
        case ForceContribution::Accelerate: return "ACCELERATE - m/s2";
        default:                            return "DAMP - 1/s";
    }
}

// The sentence under each heading. These are the browser's, word for word, because the explanation IS
//    the feature — see the note at the top of this file.
inline const char* ContributionNote(ForceContribution Give) noexcept
{
    switch (Give)
    {
        case ForceContribution::Flow:
            return "Velocity the air carries. A receiver is dragged toward it at its own coupling, so the "
                   "same wind moves a leaf and a hailstone differently, and nothing in it accelerates "
                   "forever. These sum into one lattice however many there are.";
        case ForceContribution::Accelerate:
            return "Added straight to velocity. Coupling has no say, which is why gravity belongs here and "
                   "not in the wind: it must move everything by the same amount. Evaluated per receiver, "
                   "so these cost more than flow does.";
        default:
            return "Scales velocity down. Removes energy rather than adding a direction, so unlike a force "
                   "it can never start something moving.";
    }
}

inline const char* ReachNote() noexcept
{
    return "Reaches everywhere. This is not a very large sphere - it has no centre and no edge, which is "
           "the case a bounded lattice could not have held.";
}

inline const char* UnpackableNote() noexcept
{
    return "This kind has no GPU path yet, so it is authored and described but does not move anything. It "
           "is listed rather than hidden because a control that silently does nothing is worse than one "
           "that says so.";
}

inline const char* CostNote() noexcept
{
    return "Flow fields sum into one velocity texture and then cost one sample each step no matter how "
           "many there are. Acceleration fields cannot: the useful ones are unbounded and a bounded "
           "texture would clip them, so each is evaluated per receiver per step.";
}

inline const char* HeaderNote() noexcept
{
    return "Every field in the scene, grouped by what it contributes. Wind is the flow kinds; gravity and "
           "attraction are acceleration. Black hole and magnetic systems carry their own, which move with "
           "them and are not listed here.";
}

inline const char* FalloffName(ForceFalloff Shape) noexcept
{
    switch (Shape)
    {
        case ForceFalloff::None:          return "none";
        case ForceFalloff::Linear:        return "linear";
        case ForceFalloff::Smooth:        return "smooth";
        default:                          return "inverse-square";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE ROWS OF A CARD
//------------------------------------------------------------------------------------------------------------------------

// 📦 Which rows a field's card carries. Driven entirely by the kind and the reach, which is what makes
//    ① and ③ structural rather than something a painter remembers to do.
enum class ForceRow : uint8_t
{
    Name, Enabled, Strength, Centre, Radius, Falloff, Bearing, BandSpeed,
    Swirl, Swallow, Begins, Lasts, Repeats, Remove, Count,
};

struct ForceRowPlan
{
    ForceRow Rows[16] = {};
    uint32_t Count    = 0u;
    bool     Unpackable = false;    // ③ draws the plain warning
    bool     Everywhere = false;    // ① draws the reach note in place of centre/radius
};

inline ForceRowPlan PlanRows(const ForceField& Field) noexcept
{
    const ForceFieldKindFacts& Facts = FactsOf(Field.Kind);
    ForceRowPlan Plan;
    auto Add = [&Plan](ForceRow Row) { if (Plan.Count < 16u) Plan.Rows[Plan.Count++] = Row; };

    Add(ForceRow::Name);
    Add(ForceRow::Enabled);
    Add(ForceRow::Strength);

    Plan.Everywhere = (Field.Reaches == ForceReach::Everywhere);
    if (!Plan.Everywhere)
    {
        Add(ForceRow::Centre);
        Add(ForceRow::Radius);
        Add(ForceRow::Falloff);
    }

    if (Facts.Give == ForceContribution::Flow)
    {
        Add(ForceRow::Bearing);
        if (Field.Kind == ForceFieldKind::Gust) Add(ForceRow::BandSpeed);
    }
    if (Field.Kind == ForceFieldKind::Attract)
    {
        Add(ForceRow::Swirl);
        Add(ForceRow::Swallow);
    }

    Add(ForceRow::Begins);
    Add(ForceRow::Lasts);
    Add(ForceRow::Repeats);

    Plan.Unpackable = !ForcePackable(Field.Kind);
    Add(ForceRow::Remove);
    return Plan;
}

inline const char* RowLabel(ForceRow Row) noexcept
{
    switch (Row)
    {
        case ForceRow::Name:      return "Name";
        case ForceRow::Enabled:   return "Enabled";
        case ForceRow::Strength:  return "Strength";
        case ForceRow::Centre:    return "Centre";
        case ForceRow::Radius:    return "Radius";
        case ForceRow::Falloff:   return "Falloff";
        case ForceRow::Bearing:   return "Bearing";
        case ForceRow::BandSpeed: return "Band speed";
        case ForceRow::Swirl:     return "Swirl";
        case ForceRow::Swallow:   return "Swallow radius";
        case ForceRow::Begins:    return "Start";
        case ForceRow::Lasts:     return "Lasts (0 = forever)";
        case ForceRow::Repeats:   return "Repeats (0 = once)";
        default:                  return "Remove";
    }
}

// 📦 What a row reads, and over what span. Ranges are the browser's.
struct ForceRowReading
{
    float       Reading  = 0.0f;
    float       Minimum  = 0.0f;
    float       Maximum  = 1.0f;
    uint32_t    Decimals = 2u;
    const char* Unit     = "";
};

inline ForceRowReading ReadRow(const ForceField& Field, ForceRow Row) noexcept
{
    const ForceFieldKindFacts& Facts = FactsOf(Field.Kind);
    switch (Row)
    {
        case ForceRow::Strength:  return { Field.Strength, -20.0f, 20.0f, 2u, Facts.Unit };
        case ForceRow::Radius:    return { Field.Radius,     0.5f, 30.0f, 2u, "m" };
        case ForceRow::Bearing:   return { ForceBearing(Field), 0.0f, 360.0f, 0u, "deg" };
        case ForceRow::BandSpeed: return { Field.Rate,       0.0f,  2.0f, 2u, "" };
        case ForceRow::Swirl:     return { Field.Swirl,      0.0f, 12.0f, 2u, "" };
        case ForceRow::Swallow:   return { Field.Swallow,    0.0f,  3.0f, 2u, "m" };
        case ForceRow::Begins:    return { Field.Begins,     0.0f, 60.0f, 1u, "s" };
        case ForceRow::Lasts:     return { Field.Lasts,      0.0f,120.0f, 1u, "s" };
        case ForceRow::Repeats:   return { Field.Repeats,    0.0f,120.0f, 1u, "s" };
        default:                  return {};
    }
}

inline bool RowIsSlider(ForceRow Row) noexcept
{
    switch (Row)
    {
        case ForceRow::Strength: case ForceRow::Radius:  case ForceRow::Bearing:
        case ForceRow::BandSpeed: case ForceRow::Swirl:  case ForceRow::Swallow:
        case ForceRow::Begins:   case ForceRow::Lasts:   case ForceRow::Repeats:
            return true;
        default:
            return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE COST CARD
//------------------------------------------------------------------------------------------------------------------------

struct ForceCost
{
    uint32_t Baked = 0u;    // summed into the shared lattice
    uint32_t Live  = 0u;    // evaluated per receiver
};

// 📦 ② What this scene pays. Read through ForceBakeable so the panel and the engine cannot disagree
//    about which fields are cheap — that judgement lives in exactly one function.
inline ForceCost ResolveCost(const ForceCardSubject& Subject) noexcept
{
    ForceCost Cost;
    for (uint32_t At = 0u; At < Subject.Count; ++At)
    {
        if (ForceBakeable(Subject.Fields[At])) ++Cost.Baked;
        else                                   ++Cost.Live;
    }
    return Cost;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE STACK
//------------------------------------------------------------------------------------------------------------------------

struct ForceStackLayout
{
    float Header = 0.0f;
    float Add    = 0.0f;
    float Cost   = 0.0f;
    float Groups = 0.0f;
    float Total  = 0.0f;
};

inline float RowHeight(ForceRow Row) noexcept
{
    switch (Row)
    {
        case ForceRow::Name:    return Kit::LabelSize + Kit::LabelFoot + Kit::SplitTall + Kit::FieldGap;
        case ForceRow::Enabled: return Kit::SwitchTall + Kit::FieldGap;
        case ForceRow::Centre:  return Kit::LabelSize + Kit::LabelFoot + Kit::SplitTall + Kit::FieldGap;
        case ForceRow::Falloff: return Kit::LabelSize + Kit::LabelFoot + Kit::SelectTall + Kit::FieldGap;
        case ForceRow::Remove:  return Kit::ActionTall + Kit::FieldGap;
        default:                return Kit::LabelSize + Kit::LabelFoot + Kit::PillTall + Kit::FieldGap;
    }
}

inline float CardHeight(ImFont* Light, float Wide, const ForceField& Field) noexcept
{
    const ForceRowPlan Plan = PlanRows(Field);
    const float Inner = Wide - Kit::CardPadX * 2.0f;
    float Tall = Kit::CardPadTop + Kit::TitleSize + Kit::TitleFoot;
    for (uint32_t At = 0u; At < Plan.Count; ++At) Tall += RowHeight(Plan.Rows[At]);
    if (Plan.Everywhere) Tall += Kit::NoteHeight(Light, Inner, ReachNote()) + Kit::FieldGap;
    if (Plan.Unpackable) Tall += Kit::NoteHeight(Light, Inner, UnpackableNote()) + Kit::FieldGap;
    return Tall + Kit::CardPadFoot;
}

inline ForceStackLayout ResolveStack(ImFont* Light, float Wide, const ForceCardSubject& Subject) noexcept
{
    ForceStackLayout Layout;
    const float Inner      = Wide - Kit::CardPadX * 2.0f;
    const float PlainInner = Wide - Kit::PlainPadX * 2.0f;

    Layout.Header = Kit::PlainPadY + Kit::NoteHeight(Light, PlainInner, HeaderNote()) + Kit::PlainPadY;

    const uint32_t Kinds = uint32_t(ForceFieldKind::Count);
    const uint32_t Rows  = (Kinds + AddersAcross - 1u) / AddersAcross;
    Layout.Add = Kit::CardPadTop + Kit::TitleSize + Kit::TitleFoot
               + float(Rows) * AdderTall + float(Rows - 1u) * AdderGap + Kit::CardPadFoot;

    Layout.Cost = Kit::CardPadTop + Kit::TitleSize + Kit::TitleFoot
                + Kit::MetricSize + Kit::MetricFoot
                + Kit::NoteHeight(Light, Inner, CostNote()) + Kit::CardPadFoot;

    // The groups, in the fixed order Flow, Accelerate, Damp. A heading only exists if something is under
    //    it, so an empty scene is an empty panel rather than three lonely sentences.
    for (int Give = 0; Give <= 2; ++Give)
    {
        uint32_t Here = 0u;
        for (uint32_t At = 0u; At < Subject.Count; ++At)
        {
            if (int(FactsOf(Subject.Fields[At].Kind).Give) == Give) ++Here;
        }
        if (Here == 0u) continue;
        Layout.Groups += Kit::NoteHeight(Light, Wide, ContributionNote(ForceContribution(Give)))
                       + GroupNoteGap;
        for (uint32_t At = 0u; At < Subject.Count; ++At)
        {
            if (int(FactsOf(Subject.Fields[At].Kind).Give) != Give) continue;
            Layout.Groups += CardHeight(Light, Wide, Subject.Fields[At]) + Kit::CardGap;
        }
    }

    Layout.Total = Layout.Header + Kit::PlainGap + Layout.Add + Kit::CardGap
                 + Layout.Cost + Kit::CardGap + Layout.Groups;
    return Layout;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   HIT REGIONS
//------------------------------------------------------------------------------------------------------------------------

struct ForceHitRegions
{
    ImVec4 Add[uint32_t(ForceFieldKind::Count)] = {};
    ImVec4 Enabled[MaxFields] = {};
    ImVec4 Strength[MaxFields] = {};
    ImVec4 Remove[MaxFields] = {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     PAINTING
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Paint the whole force field panel at Spot, in a column Wide pixels across.
/// in    Draw / Light / Regular   [-]   the draw list and the two DM Sans faces the editor ships
/// in    Spot / Wide              [px]  top-left of the stack, and the column width
/// in    Subject                  [-]   the scene's fields
/// out   Regions                  [px]  where everything landed, for the host's invisible buttons
/// out   float                    [px]  the stack's height
inline float PaintForceCard(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                            const ForceCardSubject& Subject, ForceHitRegions* Regions = nullptr) noexcept
{
    ForceHitRegions Discard;
    ForceHitRegions& Where = Regions != nullptr ? *Regions : Discard;
    Where = ForceHitRegions{};

    const ForceStackLayout Layout = ResolveStack(Light, Wide, Subject);
    const float Inner      = Wide - Kit::CardPadX * 2.0f;
    const float PlainInner = Wide - Kit::PlainPadX * 2.0f;
    float Y = Spot.y;

    // ── the header ─────────────────────────────────────────────────────────────────────────────────
    Draw->AddRectFilled({ Spot.x, Y }, { Spot.x + Wide, Y + Layout.Header }, Kit::PlainFill, Kit::CardRound);
    Draw->AddRect({ Spot.x, Y }, { Spot.x + Wide, Y + Layout.Header }, Kit::PlainEdge, Kit::CardRound);
    Kit::PaintNote(Draw, Light, { Spot.x + Kit::PlainPadX, Y + Kit::PlainPadY }, PlainInner, HeaderNote());
    Y += Layout.Header + Kit::PlainGap;

    // ── add ────────────────────────────────────────────────────────────────────────────────────────
    Kit::PaintCardShell(Draw, Light, { Spot.x, Y }, Wide, Layout.Add, "Add");
    {
        float RowY = Y + Kit::CardPadTop + Kit::TitleSize + Kit::TitleFoot;
        const float ButtonWide = (Inner - AdderGap * float(AddersAcross - 1u)) / float(AddersAcross);
        for (uint32_t At = 0u; At < uint32_t(ForceFieldKind::Count); ++At)
        {
            const uint32_t Column = At % AddersAcross;
            const float X = Spot.x + Kit::CardPadX + float(Column) * (ButtonWide + AdderGap);
            char Words[64];
            std::snprintf(Words, sizeof(Words), "+ %s", FactsOf(ForceFieldKind(At)).Name);
            Kit::PaintAction(Draw, Light, { X, RowY }, ButtonWide, Words,
                             Kit::ActionFill, Kit::ActionEdge, Kit::ActionInk);
            Where.Add[At] = ImVec4(X, RowY, ButtonWide, AdderTall);
            if (Column + 1u == AddersAcross) RowY += AdderTall + AdderGap;
        }
    }
    Y += Layout.Add + Kit::CardGap;

    // ── ② cost ─────────────────────────────────────────────────────────────────────────────────────
    Kit::PaintCardShell(Draw, Light, { Spot.x, Y }, Wide, Layout.Cost, "Cost");
    {
        const ForceCost Cost = ResolveCost(Subject);
        char Reading[48];
        std::snprintf(Reading, sizeof(Reading), "%u / %u", Cost.Baked, Cost.Live);
        const float MetricY = Y + Kit::CardPadTop + Kit::TitleSize + Kit::TitleFoot;
        Draw->AddText(Regular, Kit::MetricSize, { Spot.x + Kit::CardPadX, MetricY }, Kit::MetricInk, Reading);
        Draw->AddText(Light, Kit::CaptionSize,
                      { Spot.x + Kit::CardPadX, MetricY + Kit::MetricSize + 2.0f }, Kit::MetricUnit,
                      "summed into the lattice / evaluated per receiver");
        Kit::PaintNote(Draw, Light, { Spot.x + Kit::CardPadX, MetricY + Kit::MetricSize + Kit::MetricFoot },
                       Inner, CostNote());
    }
    Y += Layout.Cost + Kit::CardGap;

    // ── the groups ─────────────────────────────────────────────────────────────────────────────────
    for (int Give = 0; Give <= 2; ++Give)
    {
        bool Any = false;
        for (uint32_t At = 0u; At < Subject.Count && !Any; ++At)
        {
            if (int(FactsOf(Subject.Fields[At].Kind).Give) == Give) Any = true;
        }
        if (!Any) continue;

        const char* Prose = ContributionNote(ForceContribution(Give));
        Kit::PaintNote(Draw, Light, { Spot.x, Y }, Wide, Prose);
        Y += Kit::NoteHeight(Light, Wide, Prose) + GroupNoteGap;

        for (uint32_t At = 0u; At < Subject.Count; ++At)
        {
            const ForceField& Field = Subject.Fields[At];
            if (int(FactsOf(Field.Kind).Give) != Give) continue;

            const ForceRowPlan Plan = PlanRows(Field);
            const float Tall = CardHeight(Light, Wide, Field);
            Kit::PaintCardShell(Draw, Light, { Spot.x, Y }, Wide, Tall, Field.Name);
            Draw->AddText(Light, Kit::CaptionSize,
                          { Spot.x + Wide - Kit::CardPadX - 120.0f, Y + Kit::CardPadTop },
                          Kit::CaptionInk, ContributionCaption(ForceContribution(Give)));

            float RowY = Y + Kit::CardPadTop + Kit::TitleSize + Kit::TitleFoot;
            for (uint32_t Index = 0u; Index < Plan.Count; ++Index)
            {
                const ForceRow Row = Plan.Rows[Index];
                const float X = Spot.x + Kit::CardPadX;

                if (Row == ForceRow::Enabled)
                {
                    Kit::PaintSwitch(Draw, Light, { X, RowY }, Inner, "Enabled", Field.Enabled);
                    if (At < MaxFields) Where.Enabled[At] = ImVec4(X + Inner - Kit::SwitchWide, RowY, Kit::SwitchWide, Kit::SwitchTall);
                }
                else if (Row == ForceRow::Falloff)
                {
                    Kit::PaintSelect(Draw, Light, { X, RowY }, Inner, "Falloff", FalloffName(Field.Fades));
                }
                else if (Row == ForceRow::Remove)
                {
                    Kit::PaintAction(Draw, Light, { X, RowY }, Inner, "Remove",
                                     Kit::ActionFill, Kit::ActionEdge, Kit::TileOffInk);
                    if (At < MaxFields) Where.Remove[At] = ImVec4(X, RowY, Inner, Kit::ActionTall);
                }
                else if (RowIsSlider(Row))
                {
                    const ForceRowReading Reading = ReadRow(Field, Row);
                    // The shared painter wants the editor's own descriptor, so the row is described as
                    //    one. A force slider IS a gas slider: same pill, same split value, one painter.
                    Kit::GasField Chrome;
                    Chrome.Label    = RowLabel(Row);
                    Chrome.Minimum  = Reading.Minimum;
                    Chrome.Maximum  = Reading.Maximum;
                    Chrome.Decimals = Reading.Decimals;
                    Chrome.Unit     = Reading.Unit;
                    Kit::PaintSlider(Draw, Light, { X, RowY }, Inner, Chrome, Reading.Reading);
                    if (Row == ForceRow::Strength && At < MaxFields)
                        Where.Strength[At] = ImVec4(X, RowY + Kit::LabelSize + Kit::LabelFoot, Inner - Kit::SplitWide - Kit::PillGap, Kit::PillTall);
                }
                else
                {
                    // Name and Centre are text and three numbers; both are drawn by the shared chrome.
                    Draw->AddText(Light, Kit::LabelSize, { X, RowY }, Kit::FieldInk, RowLabel(Row));
                    const float BoxY = RowY + Kit::LabelSize + Kit::LabelFoot;
                    Draw->AddRectFilled({ X, BoxY }, { X + Inner, BoxY + Kit::SplitTall }, Kit::SplitFill, 8.0f);
                    Draw->AddRect({ X, BoxY }, { X + Inner, BoxY + Kit::SplitTall }, Kit::SplitEdge, 8.0f);
                    if (Row == ForceRow::Name)
                    {
                        Draw->AddText(Light, Kit::LabelSize + 1.0f, { X + 10.0f, Kit::Baseline(BoxY, Kit::SplitTall, Kit::LabelSize + 1.0f) },
                                      Kit::BodyInk, Field.Name);
                    }
                    else
                    {
                        char Axis[72];
                        std::snprintf(Axis, sizeof(Axis), "%.2f   %.2f   %.2f",
                                      double(Field.Centre[0]), double(Field.Centre[1]), double(Field.Centre[2]));
                        Draw->AddText(Light, Kit::LabelSize + 1.0f, { X + 10.0f, Kit::Baseline(BoxY, Kit::SplitTall, Kit::LabelSize + 1.0f) },
                                      Kit::BodyInk, Axis);
                    }
                }
                RowY += RowHeight(Row);

                // ① The reach note goes where the centre and radius would have been.
                if (Index == 2u && Plan.Everywhere)
                {
                    Kit::PaintNote(Draw, Light, { X, RowY }, Inner, ReachNote());
                    RowY += Kit::NoteHeight(Light, Inner, ReachNote()) + Kit::FieldGap;
                }
            }

            // ③ and the plain warning, last, above Remove's own spacing.
            if (Plan.Unpackable)
            {
                Kit::PaintNote(Draw, Light, { Spot.x + Kit::CardPadX, RowY }, Inner, UnpackableNote());
            }
            Y += Tall + Kit::CardGap;
        }
    }

    return Layout.Total;
}

} // namespace Frontier::ForceCards
