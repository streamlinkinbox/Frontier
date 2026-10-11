//==============================================================================================================================================
//                                                           CLOUDDECKSURFACE.H
//==============================================================================================================================================
// 📦 CloudDeckPanel.jsx: the draggable vertical section in the "Cloud base" card. Its own comment calls it
//    "the ported InspectorDepot cloud-deck band adapted to the native base/thickness range" — which is the
//    piece the depot's fixed 0-400 m band could not supply. The altitude window sizes itself to the deck,
//    so a 1500 m base is as readable as a 130 m one, and the base line is a slider.
//
//    Reads the engine's own property names: Base, Thickness and Density (0-4, normalised here to 0-1).

#pragma once

#include "WindInstrumentSurface.h"

namespace Frontier::CloudDeck
{

namespace Kit = Frontier::WindInstrument;

//------------------------------------------------------------------------------------------------------------------------
//                                                   PALETTE AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 Ground     = IM_COL32(  6,   7,   8, 255);   // [-] fillRect #060708
constexpr ImU32 Lattice    = IM_COL32(255, 255, 255,  18);   // [-] gridline #ffffff12
constexpr ImU32 Ruled      = IM_COL32(139, 146, 153, 255);   // [-] altitude label #8b9299
constexpr ImU32 DatumInk   = IM_COL32(255, 255, 255, 170);   // [-] dashed base line #ffffffaa
constexpr ImU32 TitleInk   = IM_COL32(146, 154, 159, 255);   // [-] canvas title #929a9f

constexpr float DeckTall   = 176.0f;   // [px] the canvas height the component fixes
constexpr float DeckRound  =   8.0f;   // [px] style borderRadius
constexpr float FootPad    =  18.0f;   // [px] Y(): height - 18 at the window's floor
constexpr float HeadPad    =  48.0f;   // [px] Y(): the span is height - 48
constexpr float ColumnStep =   4.0f;   // [px] the sampling stride across the width
constexpr float ColumnWide =   4.3f;   // [px] each column is drawn slightly wider than the stride
constexpr float TextSize   =   8.0f;   // [px] ctx.font = "8px sans-serif"
constexpr float LabelLift  =   3.0f;   // [px] fillText(..., Vertical - 3)
constexpr float TextInset  =   6.0f;   // [px] fillText(..., 6, ...)
constexpr float TitleBase  =  11.0f;   // [px] the title's baseline
constexpr int   Rules      =   4;      // [-]  five gridlines, index 0 to 4

// A band's worth of the vertical gradient. A flat ImGui quad carries one colour, so each column is cut into
//    slices; 18 is past the point where the steps are visible at one device pixel per CSS pixel.
constexpr int GradientBands = 18;

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE MODEL
//------------------------------------------------------------------------------------------------------------------------

struct DeckDraft
{
    float Base      = 130.0f;    // [m] engine property "Base"
    float Thickness = 300.0f;    // [m] engine property "Thickness"
    float Density   = 0.62f;     // [-] engine property "Density" / 4, clamped to 0..1
    bool  Local     = false;     // [-] a local cloud volume rather than a world layer
};

// The floor of the window. A world layer always sits on the datum; a local volume drops below it.
inline float Floor(const DeckDraft& Deck)
{
    if (!Deck.Local) return 0.0f;
    return std::min(0.0f, std::floor((Deck.Base - Deck.Thickness * 0.25f) / 100.0f) * 100.0f);
}

// The window's height in metres, rounded up to a 500 m step and never tighter than 400 m.
inline float Window(const DeckDraft& Deck)
{
    const float Span = (Deck.Base - Floor(Deck) + Deck.Thickness) * 1.25f;
    return std::max(400.0f, std::ceil(Span / 500.0f) * 500.0f);
}

// Altitude to canvas Y, and back again for the drag.
inline float Vertical(const DeckDraft& Deck, float Altitude, float Tall = DeckTall)
{
    return Tall - FootPad - ((Altitude - Floor(Deck)) / Window(Deck)) * (Tall - HeadPad);
}

inline float Altitude(const DeckDraft& Deck, float Y, float Tall = DeckTall)
{
    return ((Tall - FootPad - Y) / (Tall - HeadPad)) * Window(Deck) + Floor(Deck);
}

inline float Lowest(const DeckDraft& Deck)
{
    return Deck.Local ? -100000.0f - Deck.Thickness * 0.5f : 100.0f;
}

inline float Highest(const DeckDraft& Deck)
{
    return Deck.Local ? 100000.0f - Deck.Thickness * 0.5f : 12000.0f;
}

// What a pointer at Y, or an arrow key, commits to the Base property.
inline float Commit(const DeckDraft& Deck, float Y, float Tall = DeckTall)
{
    return std::round(std::clamp(Altitude(Deck, Y, Tall), Lowest(Deck), Highest(Deck)));
}

inline float Nudge(const DeckDraft& Deck, bool Up)
{
    return std::clamp(Deck.Base + (Up ? 10.0f : -10.0f), Lowest(Deck), Highest(Deck));
}

// The deck's upper surface at a column, as a fraction of its thickness below the crest.
inline float Crown(float X)
{
    return 0.65f + 0.3f * std::sin(X * 0.08f) + 0.12f * std::sin(X * 0.31f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE PAINT
//------------------------------------------------------------------------------------------------------------------------

// createLinearGradient(0, Top, 0, Datum + 4) with three stops, sampled at a fraction of its run.
inline ImU32 Vapour(float Share, float Density)
{
    const float Clamped = std::clamp(Share, 0.0f, 1.0f);
    float R, G, B, A;
    if (Clamped <= 0.35f)
    {
        const float T = Clamped / 0.35f;
        R = 238.0f; G = 243.0f; B = 248.0f;
        A = 0.05f + (0.75f * Density - 0.05f) * T;
    }
    else
    {
        const float T = (Clamped - 0.35f) / 0.65f;
        R = 238.0f + (92.0f - 238.0f) * T;
        G = 243.0f + (108.0f - 243.0f) * T;
        B = 248.0f + (128.0f - 248.0f) * T;
        A = 0.75f * Density + (0.72f * Density - 0.75f * Density) * T;
    }
    return IM_COL32(int(R + 0.5f), int(G + 0.5f), int(B + 0.5f),
                    int(std::clamp(A, 0.0f, 1.0f) * 255.0f + 0.5f));
}

// Spot is the canvas's top-left. Wide is its CSS width; the height is fixed at 176.
inline void PaintDeck(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const DeckDraft& Deck,
                      ImU32 Backdrop = IM_COL32(18, 18, 18, 255), float Tall = DeckTall)
{
    const ImVec2 Corner { Spot.x + Wide, Spot.y + Tall };
    Draw->AddRectFilled(Spot, Corner, Ground, DeckRound);
    // A canvas clips its own overflow and an ImGui draw list does not: a thick deck reaches past the top.
    Draw->PushClipRect(Spot, Corner, true);

    const float Datum = Spot.y + Vertical(Deck, Deck.Base, Tall);
    const float Depth = (Deck.Thickness / Window(Deck)) * (Tall - HeadPad);

    for (float X = 0.0f; X < Wide; X += ColumnStep)
    {
        const float Top  = Datum - Depth * Crown(X);
        const float Foot = Datum + 4.0f;
        if (Foot <= Top) continue;
        const float Left = Spot.x + X, Right = Spot.x + X + ColumnWide;
        for (int I = 0; I < GradientBands; ++I)
        {
            const float A = float(I) / GradientBands, B = float(I + 1) / GradientBands;
            Draw->AddRectFilled({ Left, Top + (Foot - Top) * A }, { Right, Top + (Foot - Top) * B },
                                Vapour((A + B) * 0.5f, Deck.Density));
        }
    }

    for (int I = 0; I <= Rules; ++I)
    {
        const float Height = Floor(Deck) + Window(Deck) * I / Rules;
        const float Y = Spot.y + Vertical(Deck, Height, Tall);
        Draw->AddLine({ Spot.x, Y }, { Spot.x + Wide, Y }, Lattice, 1.0f);
        char Label[24];
        // Only an altitude of exactly zero reads as the datum; everything else is a metre mark.
        if (Height == 0.0f) std::snprintf(Label, sizeof(Label), "%s", "DATUM");
        else                std::snprintf(Label, sizeof(Label), "%d m", int(std::lround(Height)));
        Kit::Inked(Draw, Face, Spot.x + TextInset, Y - LabelLift, TextSize, Ruled, Label);
    }

    Kit::Dashed(Draw, { Spot.x, Datum }, { Spot.x + Wide, Datum }, DatumInk, 3.0f, 3.0f, 1.0f);
    Kit::Inked(Draw, Face, Spot.x + TextInset, Spot.y + TitleBase, TextSize, TitleInk,
               "CLOUD DECK \xc2\xb7 VERTICAL SECTION");

    Draw->PopClipRect();
    // border-radius on a canvas clips the paint; an ImGui clip rectangle has square corners, so the four
    //    notches are painted back over in the card's colour.
    Kit::CornerNotch(Draw, Spot,                 { Spot.x + DeckRound, Spot.y + DeckRound }, DeckRound,
                     Kit::Pi,          Backdrop);
    Kit::CornerNotch(Draw, { Corner.x, Spot.y }, { Corner.x - DeckRound, Spot.y + DeckRound }, DeckRound,
                     -Kit::Pi * 0.5f,  Backdrop);
    Kit::CornerNotch(Draw, Corner,               { Corner.x - DeckRound, Corner.y - DeckRound }, DeckRound,
                     0.0f,             Backdrop);
    Kit::CornerNotch(Draw, { Spot.x, Corner.y }, { Spot.x + DeckRound, Corner.y - DeckRound }, DeckRound,
                     Kit::Pi * 0.5f,   Backdrop);
}

} // namespace Frontier::CloudDeck
