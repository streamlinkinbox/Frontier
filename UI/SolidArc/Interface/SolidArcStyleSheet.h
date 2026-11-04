/*====================================================================================================================================
                                                         SOLIDARCSTYLESHEET.H
====================================================================================================================================*/
// 🧩 The HTML SolidArc style sheet (Experimental/SolidArc/src/WorkspacePanel.css) on PixelSpace: tokens, inks, DM Sans text
//    placed like CSS line boxes, IconArt glyphs of the SolidArc domain, and pressable regions the walkthrough scripts click

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Interface/IconArt.h"
#include "Engine/Interface/PixelSpace.h"

#include <imgui.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 WorkspacePanel.css at 1600 × 960: header 68, document bar 34, workspace grid 300 | 1fr | 330 with 10 px gaps inside
//    0 12 px padding, status bar 31; panels round 23 px with a #ffffff08 hairline.
struct SolidArcStyleTokens
{
    static constexpr float HeaderHeight      = 68.0f;                                        // [px] - .app-header
    static constexpr float DocumentBarHeight = 34.0f;                                        // [px] - .document-bar
    static constexpr float StatusBarHeight   = 31.0f;                                        // [px] - .status-bar
    static constexpr float WorkspacePadding  = 12.0f;                                        // [px] - .workspace padding (left / right)
    static constexpr float ColumnGap         = 10.0f;                                        // [px] - .workspace gap
    static constexpr float OutlinerWidth     = 300.0f;                                       // [px] - First grid column
    static constexpr float InspectorWidth    = 330.0f;                                       // [px] - Last grid column
    static constexpr float PanelRadius       = 23.0f;                                        // [px] - .panel border-radius
    static constexpr float FontScale         = 1.302f;                                       // [-]  - DM Sans hhea ascent + descent per em
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class SolidArcWeight : uint8_t
{
    Light   = 0,                                                                             // font-weight 300 (the :root default)
    Regular = 1,                                                                             // font-weight 400 (strong, buttons)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Faces come from the shared TypefaceRegistry (EngineContent/FontArchives/DMSans); null falls back to the ImGui font.
struct SolidArcTypefaces
{
    void* Light   = nullptr;                                                                 // [-] - ImFont* of DMSans-Light
    void* Regular = nullptr;                                                                 // [-] - ImFont* of DMSans-Regular
};

// One frame of a pressable region (an invisible ImGui button, so scripted pointer input reaches it like a mouse).
struct SolidArcPress
{
    bool HoveredCondition = false;                                                           // [-] - Pointer over it
    bool HeldCondition    = false;                                                           // [-] - Pressed and held
    bool ClickedCondition = false;                                                           // [-] - Released on it this frame
    bool DoubleCondition  = false;                                                           // [-] - Double-clicked this frame
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLASSES
//------------------------------------------------------------------------------------------------------------------------

// 💡 Named widget rectangles of the last frame, for the walkthrough scripts. A widget not wholly inside its window's clip
//    (scrolled away in the Inspector, the history strip, the scene rows) is Clipped; the widget named Reveal scrolls its
//    window so it sits in the middle, then Reveal clears.
class SolidArcWidgetBoard
{
public:
    void Record(const std::string& Key, ImVec2 Low, ImVec2 High);
    void Clear();
    [[nodiscard]] const ImVec4* Find(const std::string& Key) const;
    [[nodiscard]] bool ClippedCondition(const std::string& Key) const { return Clipped.contains(Key); }
    void RequestReveal(const std::string& Key) { Reveal = Key; }
    [[nodiscard]] bool SettledCondition(const std::string& Key) const;

private:
    std::map<std::string, ImVec4> Rectangles = {};                                           // [px] - (x0, y0, x1, y1) per key
    std::map<std::string, ImVec4> Previous   = {};                                           // [px] - The frame before (a scroll clamp can still move it)
    std::set<std::string>         Clipped    = {};                                           // [-]  - Keys partly outside their clip
    std::string                   Reveal     = {};                                           // [-]  - Key to scroll into view
};

// 💡 PixelSpace bound to the current ImGui window, with the CSS conventions of the HTML panels: 1 px borders drawn
//    inside the box, text centred on a line box of 1.302 em, letter spacing, and SolidArc-domain icons tinted on draw.
class SolidArcSheet
{
public:
    bool Begin();                                                                            // false outside an ImGui window
    [[nodiscard]] PixelSpace& Surface() { return Space; }

    void  Box(const PlaneExtent& Extent, ColorQuad Fill, float Radius, ColorQuad Border = { 0.0f, 0.0f, 0.0f, 0.0f });
    void  Line(float FromX, float FromY, float ToX, float ToY, ColorQuad Ink, float Thickness = 1.0f);
    float Text(float X, float CentreY, float CssSize, ColorQuad Ink, const char* Utf8, SolidArcWeight Weight = SolidArcWeight::Light,
               float LetterSpacing = 0.0f);                                                  // returns the advance [px]
    float TextRight(float Right, float CentreY, float CssSize, ColorQuad Ink, const char* Utf8, SolidArcWeight Weight = SolidArcWeight::Light,
                    float LetterSpacing = 0.0f);
    float TextClipped(float X, float CentreY, float Width, float CssSize, ColorQuad Ink, const char* Utf8,
                      SolidArcWeight Weight = SolidArcWeight::Light);                        // text-overflow: ellipsis
    [[nodiscard]] float Measure(const char* Utf8, float CssSize, SolidArcWeight Weight = SolidArcWeight::Light, float LetterSpacing = 0.0f);
    void  Icon(IconSymbol Symbol, float X, float Y, float Size, ColorQuad Ink);

private:
    PixelSpace Space;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConfigureSolidArcTypefaces(const SolidArcTypefaces& Typefaces);
[[nodiscard]] const SolidArcTypefaces& QuerySolidArcTypefaces();

// #rrggbb (+ alpha) and white washes (#ffffffXX) as ColorQuad.
[[nodiscard]] ColorQuad SolidArcInk(uint32_t Rgb, float Alpha = 1.0f);
[[nodiscard]] ColorQuad SolidArcWash(uint32_t Alpha);
[[nodiscard]] ImU32     SolidArcPack(ColorQuad Ink);

// An invisible button over Extent under ImGui ID Key; its rectangle is recorded on the board for the scripts.
SolidArcPress PressSolidArcRegion(SolidArcWidgetBoard& Widgets, const std::string& Key, const PlaneExtent& Extent,
                                  bool EnabledCondition = true);

} // namespace Frontier::SolidArc
