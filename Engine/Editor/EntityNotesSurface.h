//==============================================================================================================================================
//                                                         ENTITYNOTESSURFACE.H
//==============================================================================================================================================
// 📦 EntityNotes (Inspectors.jsx:409): the optional note every inspector subject carries — folders and
//    groups included, since the shared Header() renders it for all of them.
//
//    Two states, and the native panel had neither. It opens iff the subject already has a note:
//      closed  a dashed "Add notes" button, pinned to the heading's top-right corner
//      open    a panel under the heading: Notes / OPTIONAL / Hide, over a 76 px textarea
//
//    The native RecordNotes drew something else entirely — a chevron disclosure in a rounded 18 px inset
//    card with a caps header and a 64 px field. None of that is in the bundle.

#pragma once

#include "WindInstrumentSurface.h"

namespace Frontier::EntityNotes
{

namespace Kit = Frontier::WindInstrument;

//------------------------------------------------------------------------------------------------------------------------
//                                                   PALETTE AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 AddEdge    = IM_COL32( 66,  66,  66, 255);  // [-] .entity-notes-add border 1px dashed #424242
constexpr ImU32 AddInk     = IM_COL32(137, 137, 137, 255);  // [-] and its colour #898989
constexpr ImU32 AddHotInk  = IM_COL32(210, 210, 210, 255);  // [-] :hover colour #d2d2d2
constexpr ImU32 AddHotEdge = IM_COL32(103, 103, 103, 255);  // [-] :hover border #676767
constexpr ImU32 PanelFill  = IM_COL32( 25,  25,  25, 255);  // [-] .entity-notes background #191919
constexpr ImU32 PanelEdge  = IM_COL32( 48,  48,  48, 255);  // [-] its border #303030
constexpr ImU32 TitleInk   = IM_COL32(201, 201, 201, 255);  // [-] .entity-notes strong #c9c9c9
constexpr ImU32 AsideInk   = IM_COL32(110, 110, 110, 255);  // [-] .entity-notes small #6e6e6e
constexpr ImU32 HideFill   = IM_COL32( 37,  37,  37, 255);  // [-] .entity-notes button #252525
constexpr ImU32 HideInk    = IM_COL32(133, 133, 133, 255);  // [-] and its colour #858585
constexpr ImU32 FieldFill  = IM_COL32( 17,  17,  17, 255);  // [-] .entity-notes textarea #111
constexpr ImU32 FieldEdge  = IM_COL32(255, 255, 255,  13);  // [-] input,select,textarea border #ffffff0d
constexpr ImU32 FieldInk   = IM_COL32(204, 204, 204, 255);  // [-] its colour #ccc
constexpr ImU32 GhostInk   = IM_COL32(110, 110, 110, 255);  // [-] the placeholder

constexpr float AddTall    = 30.0f;   // [px] .entity-notes-add min-height
constexpr float AddPadX    =  9.0f;   // [px] its padding
constexpr float AddGap     =  6.0f;   // [px] its flex gap
constexpr float AddSize    =  9.0f;   // [px] its font-size
constexpr float PlusBox    = 13.0f;   // [px] <Glyph Name="plus" Size={13} />
constexpr float PanelPad   = 10.0f;   // [px] .entity-notes padding
constexpr float PanelRound =  9.0f;   // [px] its border-radius
constexpr float RowGap     =  8.0f;   // [px] the header row's flex gap
constexpr float RowDrop    =  8.0f;   // [px] and its margin-bottom
constexpr float TitleSize  = 10.0f;   // [px] strong
constexpr float AsideSize  =  8.0f;   // [px] small
constexpr float AsideTrack =  0.8f;   // [px] its letter-spacing
constexpr float HideTall   = 22.0f;   // [px] the Hide button's min-height
constexpr float HidePadX   =  7.0f;   // [px] its padding
constexpr float HideSize   =  8.0f;   // [px] its font-size
constexpr float FieldTall  = 76.0f;   // [px] .entity-notes textarea min-height
constexpr float FieldRound =  6.0f;   // [px] its border-radius
constexpr float FieldPad   = 10.0f;   // [px] textarea padding, inherited from the base rule
constexpr float FieldSize  = 13.0f;   // [px] body font-size, inherited
constexpr float FieldLead  = 1.5f;    // [-]  the textarea's line-height

// .inspector-heading is 110 px tall, and 224 when it has an open note.
constexpr float HeadShut   = 110.0f;
constexpr float HeadOpen   = 224.0f;
constexpr float NotesTop   = 105.0f;  // [px] .inspector-heading > .entity-notes { top: 105px }

inline float HeadingHeight(bool Open) { return Open ? HeadOpen : HeadShut; }

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE MODEL
//------------------------------------------------------------------------------------------------------------------------

struct Draft
{
    bool Open = false;          // useState(Boolean(Value)) — open iff a note already exists
    char Body[512] = {};
};

// The initial disclosure: EntityNotes opens itself when the subject already carries a note.
inline bool OpensItself(const char* Body) { return Body != nullptr && Body[0] != '\0'; }

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CLOSED STATE
//------------------------------------------------------------------------------------------------------------------------

// An inline-flex button sized by its content: padding, the plus glyph, the gap, the label, padding.
inline float AddWidth(ImFont* Face)
{
    return AddPadX + PlusBox + AddGap + Kit::Measured(Face, AddSize, "Add notes") + AddPadX + 2.0f;
}

// A 1 px dashed rectangle. ImGui has no dashed stroke, so each edge is walked by hand.
inline void DashedBox(ImDrawList* Draw, ImVec2 Spot, ImVec2 Corner, ImU32 Colour)
{
    Kit::Dashed(Draw, { Spot.x, Spot.y },     { Corner.x, Spot.y },     Colour, 2.0f, 2.0f, 1.0f);
    Kit::Dashed(Draw, { Corner.x, Spot.y },   { Corner.x, Corner.y },   Colour, 2.0f, 2.0f, 1.0f);
    Kit::Dashed(Draw, { Corner.x, Corner.y }, { Spot.x, Corner.y },     Colour, 2.0f, 2.0f, 1.0f);
    Kit::Dashed(Draw, { Spot.x, Corner.y },   { Spot.x, Spot.y },       Colour, 2.0f, 2.0f, 1.0f);
}

inline void PaintPlus(ImDrawList* Draw, ImVec2 Heart, float Box, ImU32 Colour)
{
    const float Arm = Box * 0.5f - 2.0f, Thin = 1.2f;
    Draw->AddRectFilled({ Heart.x - Arm, Heart.y - Thin * 0.5f }, { Heart.x + Arm, Heart.y + Thin * 0.5f }, Colour);
    Draw->AddRectFilled({ Heart.x - Thin * 0.5f, Heart.y - Arm }, { Heart.x + Thin * 0.5f, Heart.y + Arm }, Colour);
}

inline void PaintAdd(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, bool Hot = false)
{
    const float Wide = AddWidth(Face);
    const ImVec2 Corner { Spot.x + Wide, Spot.y + AddTall };
    DashedBox(Draw, Spot, Corner, Hot ? AddHotEdge : AddEdge);
    const ImU32 Ink = Hot ? AddHotInk : AddInk;
    PaintPlus(Draw, { Spot.x + 1.0f + AddPadX + PlusBox * 0.5f, Spot.y + AddTall * 0.5f }, PlusBox, Ink);
    Kit::Inked(Draw, Face, Spot.x + 1.0f + AddPadX + PlusBox + AddGap,
               Spot.y + (AddTall - Kit::Grind(AddSize)) * 0.5f + Kit::AscentShare * AddSize,
               AddSize, Ink, "Add notes");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE OPEN STATE
//------------------------------------------------------------------------------------------------------------------------

inline float RowHeight() { return std::max({ HideTall, Kit::Grind(TitleSize), Kit::Grind(AsideSize) }); }

inline float PanelHeight()
{
    return 1.0f + PanelPad + RowHeight() + RowDrop + FieldTall + PanelPad + 1.0f;
}

inline float HideWidth(ImFont* Face)
{
    return HidePadX + Kit::Measured(Face, HideSize, "Hide") + HidePadX;
}

// Spot is the panel's top-left; Wide its full width. Light is the body face, Regular carries <strong>.
inline void PaintPanel(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                       const char* Body, bool Focused = false)
{
    const float Tall = PanelHeight();
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, PanelFill, PanelRound);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, PanelEdge, PanelRound);

    const float Left = Spot.x + 1.0f + PanelPad, Right = Spot.x + Wide - 1.0f - PanelPad;
    const float RowTop = Spot.y + 1.0f + PanelPad, Row = RowHeight();

    auto Middle = [&](float Size) { return RowTop + (Row - Kit::Grind(Size)) * 0.5f + Kit::AscentShare * Size; };
    Kit::Inked(Draw, Regular, Left, Middle(TitleSize), TitleSize, TitleInk, "Notes");
    const float AsideX = Left + Kit::Measured(Regular, TitleSize, "Notes") + RowGap;
    Kit::Tracked(Draw, Light, AsideX, Middle(AsideSize), AsideSize, AsideInk, "OPTIONAL", AsideTrack);

    // The Hide button is pushed to the end of the row by margin-left:auto.
    const float HideSpan = HideWidth(Light);
    const ImVec2 HideAt { Right - HideSpan, RowTop + (Row - HideTall) * 0.5f };
    Draw->AddRectFilled(HideAt, { HideAt.x + HideSpan, HideAt.y + HideTall }, HideFill, 0.0f);
    Kit::Inked(Draw, Light, HideAt.x + HideSpan * 0.5f,
               HideAt.y + (HideTall - Kit::Grind(HideSize)) * 0.5f + Kit::AscentShare * HideSize,
               HideSize, HideInk, "Hide", Kit::Anchor::Middle);

    const ImVec2 FieldAt { Left, RowTop + Row + RowDrop };
    const ImVec2 FieldTo { Right, FieldAt.y + FieldTall };
    Draw->AddRectFilled(FieldAt, FieldTo, FieldFill, FieldRound);
    Draw->AddRect(FieldAt, FieldTo, Focused ? PanelEdge : FieldEdge, FieldRound);

    const bool Empty = Body == nullptr || Body[0] == '\0';
    const char* Shown = Empty ? "Purpose, ownership, review notes\xe2\x80\xa6" : Body;
    Draw->PushClipRect({ FieldAt.x + 1.0f, FieldAt.y + 1.0f }, { FieldTo.x - 1.0f, FieldTo.y - 1.0f }, true);
    Kit::TrackedLine Lines[12];
    const int Count = std::max(1, Kit::TrackedWrap(Light, FieldSize, Shown, 0.0f,
                                                   FieldTo.x - FieldAt.x - FieldPad * 2.0f, Lines, 12));
    for (int I = 0; I < Count; ++I)
    {
        char Piece[256];
        const size_t Taken = std::min(sizeof(Piece) - 1, size_t(Lines[I].To - Lines[I].From));
        std::memcpy(Piece, Lines[I].From, Taken);
        Piece[Taken] = 0;
        Kit::Inked(Draw, Light, FieldAt.x + FieldPad,
                   FieldAt.y + FieldPad + I * FieldSize * FieldLead + Kit::AscentShare * FieldSize,
                   FieldSize, Empty ? GhostInk : FieldInk, Piece);
    }
    Draw->PopClipRect();
}

} // namespace Frontier::EntityNotes
