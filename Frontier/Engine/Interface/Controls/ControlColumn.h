/*==========================================================================================================================================
                                                              CONTROLCOLUMN.H
==========================================================================================================================================*/
// 🧩 Lays one Inspector page down the column: the page fill and padding, CSS block margins that collapse
//    between neighbours (never through a card's padding), and the instrument cards. Every metric is the CSS
//    pixel value times Scale.

#pragma once

#include "Engine/Interface/Controls/ControlTheme.h"

#include <imgui.h>

#include <vector>

namespace Frontier
{

struct EditorTypefaces;

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTROL COLUMN
//------------------------------------------------------------------------------------------------------------------------

// 📝 One `.co-card` after its page's `[data-instrument]` rule: fill (flat, or the 125° gradient to FillEnd at 65 %), an
//    optional radial glow ("radial-gradient(ellipse at X% Y%, Glow, transparent Extent)"), border, radius, padding.
struct CardLook
{
    const char* HeadingGlyph = nullptr;
    float HeadingMargin = 14.0f;
    ImU32  Fill         = 0;                    // 0: transparent
    ImU32  FillEnd      = 0;                    // 0: flat fill
    float  GradientDegrees = 125.0f;
    float  GradientStop    = 0.65f;
    ImU32  Glow         = 0;                    // 0: no radial glow
    ImVec2 GlowCentre   = ImVec2(0.5f, 0.5f);   // [-] ellipse centre as a fraction of the card
    float  GlowExtent   = 0.7f;                 // [-] the transparent stop along the farthest-corner ellipse
    ImU32  Border       = 0;                    // 0: no border
    ImU32  TopRule      = 0;                    // a top border only (border-top), when Border is 0
    float  Radius       = 23.0f;                // [px]
    float  PaddingTop   = 18.0f;                // [px]
    float  PaddingSide  = 18.0f;                // [px]
    float  PaddingBottom  = 18.0f;                // [px]
    float  MarginTop    = 0.0f;                 // [px]
    float  MarginBottom   = 12.0f;                // [px]
    ImDrawFlags Corners = ImDrawFlags_RoundCornersAll;   // the rounded corners (border-radius: 0 R R 0 → right only)
    ImU32  LeftRule     = 0;                    // a left border only (border-left), drawn over the fill
    float  LeftRuleWidth = 2.0f;                // [px]
};

class ControlColumn
{
  public:
    static constexpr float PagePadding = 20.0f;   // [px] .native-environment padding

    ControlColumn(const ControlTheme& InTheme, const EditorTypefaces& InFaces, float InScale);

    [[nodiscard]] const ControlTheme&    QueryTheme() const noexcept { return Theme; }
    [[nodiscard]] const EditorTypefaces& QueryFaces() const noexcept { return Faces; }
    [[nodiscard]] float                  QueryScale() const noexcept { return Scale; }
    [[nodiscard]] float                  QueryWidth() const noexcept { return Width; }   // [screen px]
    [[nodiscard]] ImVec2                 QueryCursor() const noexcept { return ImVec2(Left, Cursor); }
    [[nodiscard]] ImDrawList*            QueryDrawList() const noexcept { return DrawList; }
    // 📝 The container's content width in CSS pixels (the stylesheets' @container breakpoints).
    [[nodiscard]] float                  QueryContainerWidth() const noexcept { return PageWidth / Scale; }

    // 📝 Space reserves a block of CssHeight with CSS margins and returns its top-left in screen pixels.
    ImVec2 Space(float CssHeight, float MarginTop = 0.0f, float MarginBottom = 0.0f);
    void   Finalize();

    void BeginCard(const CardLook& Look, const char* Heading, const char* Index);
    void EndCard();
    // 📝 The open card's border-box top-left and width (for its absolutely positioned children).
    [[nodiscard]] ImVec2 QueryCardOrigin() const noexcept;
    [[nodiscard]] float  QueryCardWidth() const noexcept;

    // 📝 The hover tooltip of the last item (a pending control names the native field it waits for).
    static void ConstructTooltip(const char* Tooltip);

    // 📝 Narrows the column by CSS pixels on each side (a block laid out beside a fixed element) until PopIndent.
    void PushIndent(float LeftCss, float RightCss = 0.0f);
    void PopIndent();

  private:
    struct CardScope
    {
        CardLook Look{};
        float    Top        = 0.0f;
        float    OuterLeft  = 0.0f;
        float    OuterWidth = 0.0f;
    };

    const ControlTheme&    Theme;
    const EditorTypefaces& Faces;
    ImDrawList*            DrawList      = nullptr;
    float                  Scale         = 1.0f;
    float                  Left          = 0.0f;
    float                  Width         = 0.0f;
    float                  PageWidth     = 0.0f;
    float                  Cursor        = 0.0f;
    float                  PendingMargin = 0.0f;
    float                  Lowest        = 0.0f;
    std::vector<CardScope> Cards{};
    std::vector<ImVec2>    Indents{};   // [screen px] the Left and Width each PushIndent replaced
};

} // namespace Frontier
