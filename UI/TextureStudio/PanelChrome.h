/*============================================================================================================================================
                                                      PANELCHROME.H
============================================================================================================================================*/
// 🧩 Shared UI primitives for the five Texture Studio workspace panels (Baking, Material, Pattern,
// Stamp, Texture Paint). These helpers used to be copy-pasted into each panel's anonymous namespace -
// Spectrum/Caption alone existed four times, DrawGlyph three, the dock-theme push block five. One
// implementation now lives here; panels pull exactly what they use with `using PanelChrome::Name;`
// inside their translation unit, which keeps every existing call site (and every pixel) unchanged.
// Panel-specific chrome (Material's ComputeMaterialCaption metrics, Texture's pill extent tracking)
// stays in its panel and composes on top of these primitives.

#pragma once
#include "Engine/Interface/ControlKit.h" // ControlKitPalette, ColorQuad via ThemeStructure
#include <imgui.h>
#include <cstdint>
#include <functional>
#include <string>

namespace Frontier::PanelChrome
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     COLOUR CONVERSION
//------------------------------------------------------------------------------------------------------------------------

// 0xRRGGBB as an ImVec4 (Baking/Texture form: optional alpha).
ImVec4 Spectrum(uint32_t Colour, float Alpha = 1.f) noexcept;
// 0xRRGGBB as a ColorQuad (GlyphSpace placements, palette fields).
ColorQuad SpectrumQuad(uint32_t Colour, float Alpha = 1.f) noexcept;
// ColorQuad as ImVec4, optionally overriding alpha (negative keeps the quad's own).
ImVec4 ThemeQuad(const ColorQuad &Colour, float AlphaOverride = -1.f) noexcept;
// Packed draw colour via ImGui's float conversion (Pattern/Stamp form).
ImU32 Ink32(uint32_t Colour, float Alpha = 1.f);
// Packed draw colour via IM_COL32 with clamped alpha (Texture Paint form).
ImU32 Colour32(uint32_t Colour, float Alpha = 1.f) noexcept;
// Packed draw colour with a 0-255 alpha (Material form).
ImU32 Ink255(uint32_t Colour, unsigned Alpha = 255) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                     TEXT + GLYPHS
//------------------------------------------------------------------------------------------------------------------------

// Absolute-position draw-list text on the fixed typographic grid (DM Sans STB-vs-CSS em factor 1.302).
void Caption(ImVec2 Position, const char *Text, float Size, uint32_t Colour, void *Typeface = nullptr);
float CaptionWidth(const char *Text, float Size, void *Typeface = nullptr);
// One stroked SVG-path glyph (24x24 viewBox) at an exact position/size.
void DrawGlyph(const char *Path, ImVec2 Position, float Size, uint32_t Colour, float StrokeWidth = 1.6f);
// ASCII lowercase copy (search fields).
std::string Lowered(std::string Text) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED WIDGETS
//------------------------------------------------------------------------------------------------------------------------

// Bare icon button: invisible hitbox, hover/selected wash only, centred glyph (no capsule fill).
bool BareIconButton(const char *Id, const char *Path, float Size, float Scale, bool Selected, uint32_t Colour);
// Text capsule pill. Without an icon the label centres; with one it follows the glyph. ClipText
// additionally clamps the label inside the capsule (library filter pills); leave it off for exact
// Texture Paint pill behaviour. Returns true on an enabled click.
bool Pill(const char *Id, const char *Label, ImVec2 Position, float Width, float Height, bool Selected,
    float Scale, const char *IconPath = nullptr, uint32_t Accent = 0, float TextSize = 9.5f, bool ClipText = false);
// Green selected check disc used by every catalogue card (Material / Pattern / stamp picker).
void CheckDisc(ImDrawList *Ink, ImVec2 CardPosition, float Scale);
// The two 66px Visible/Hidden layer-stat cards that double as the layer-stack filter (Texture
// Paint original; Stamp/Pattern mirror it exactly). PressedIndex is -1/0/1 (none/Visible/Hidden);
// ClickedIndex receives the clicked card (-1 = none) and the panel toggles its own filter state.
// OnCardExtent, when given, fires right after each card's hitbox with its legacy automation
// caption ("%02zu\nVisible") so extent-tracking panels keep their registered control keys.
void VisibilityStatCards(ImVec2 Origin, float CardWidth, float LeftInset, float Scale,
    size_t VisibleCount, size_t HiddenCount, int PressedIndex, int &ClickedIndex,
    void *NumberTypeface, void *NumberFallbackTypeface = nullptr,
    const std::function<void(const char *)> &OnCardExtent = {});

//------------------------------------------------------------------------------------------------------------------------
//                                                     WORKSPACE THEME
//------------------------------------------------------------------------------------------------------------------------

// The shared studio ControlKit palette: neutral charcoal panels with a per-workspace
// selection/accent pair (Baking/Pattern/Stamp sage, Texture Paint warm gold).
ControlKitPalette StudioPalette(const ControlKitPalette &Saved, ColorQuad Selected, ColorQuad Accent);

// The dock-window chrome every workspace pushes around its PanelDockspace: near-black title/tab
// colours, transparent tab overlines, square windows, 2px docking separators, hidden scrollbars
// (eased wheel scrolling lives in ImGui Patch H), and optional tab-close suppression. PushDockChrome
// must be the *last* style push in the workspace wrapper and PopDockChrome the *first* pop in its
// tail, so the token's counts are always on top of the two style stacks.
struct DockChromeToken
{
    int Colours = 0;
    int Vars = 0;
    float SavedCloseSelected = 0;
    float SavedCloseUnselected = 0;
    bool CloseSaved = false;
};
DockChromeToken PushDockChrome(ImVec4 Title, ImVec4 Tab, bool IncludeCollapsedAndEmpty, bool SuppressTabClose, float Scale);
void PopDockChrome(const DockChromeToken &Token);
}
