/*==========================================================================================================================================
                                                          STACK RAIL CONTROLS
                                     ------------------------------------------------------------------
            First kit set beyond the Log Stream: StackRail chrome, FolderRow, TreeRow, IdentityHeader (interface SDK builds).
==========================================================================================================================================*/

// 🧩 First kit set beyond the Log Stream: StackRail chrome, FolderRow, TreeRow, IdentityHeader (interface SDK builds only).

#pragma once

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include <cstdint>
#include <imgui.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 KitInkRgba(int Red, int Green, int Blue, int Alpha) noexcept
{
    return static_cast<ImU32>(Alpha) << 24 | static_cast<ImU32>(Blue) << 16 | static_cast<ImU32>(Green) << 8 |
           static_cast<ImU32>(Red);
}

constexpr ImU32 KitInkHex(int Rgb, int Alpha = 255) noexcept
{
    return KitInkRgba((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255, Alpha);
}

constexpr ImU32 KitInkWhite(float Alpha) noexcept
{
    return KitInkRgba(255, 255, 255, static_cast<int>(Alpha * 255.0f + 0.5f));
}

constexpr ImU32 KitInkWithAlpha(ImU32 Colour, float Alpha) noexcept
{
    return (Colour & 0x00ffffffu) | static_cast<ImU32>(static_cast<int>(Alpha * 255.0f + 0.5f)) << 24;
}

struct KitTheme
{
    ImU32 PanelBg     = KitInkHex(0x0b100e);
    ImU32 PanelEdge   = KitInkRgba(255, 255, 255, 20);
    ImU32 Surface     = KitInkHex(0x0e1411);
    ImU32 Surface2    = KitInkHex(0x151d18);
    ImU32 Surface3    = KitInkHex(0x1c2620);
    ImU32 Line        = KitInkRgba(255, 255, 255, 18);
    ImU32 LineSoft    = KitInkRgba(255, 255, 255, 11);
    ImU32 RowHover    = KitInkRgba(255, 255, 255, 8);
    ImU32 Text        = KitInkHex(0xeef3f0);
    ImU32 Muted       = KitInkHex(0x97a29b);
    ImU32 Faint       = KitInkHex(0x5f6b64);
    ImU32 Mint        = KitInkHex(0x2fe6a7);
    ImU32 MintDim     = KitInkRgba(47, 230, 167, 33);
    ImU32 ChipOnBg    = KitInkHex(0xe9f2ed);
    ImU32 ChipOnInk   = KitInkHex(0x0b120e);
    bool  LightCanvas = false;
};

[[nodiscard]] KitTheme ResolveKitTheme() noexcept;

struct KitFaces
{
    ImFont* Light    = nullptr;
    ImFont* Regular  = nullptr;
    ImFont* Medium   = nullptr;
    ImFont* SemiBold = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] KitFaces ResolveKitFaces() noexcept;
[[nodiscard]] float    KitTextWidth(ImFont* Font, float Size, const char* Begin, const char* End = nullptr) noexcept;
void                   KitDrawText(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour,
                                   const char* Begin, const char* End = nullptr);
float                  KitDrawTracked(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour,
                                      const char* Text, float Tracking);
[[nodiscard]] bool     KitTarget(const char* Id, ImVec2 Minimum, ImVec2 Size, bool& Hovered,
                                 const char* Tooltip = nullptr);
void                   KitPillSurface(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, ImU32 Background,
                                      ImU32 Border, float Radius);
void                   DrawFolderTabGlyph(ImDrawList* Ink, ImVec2 Centre, float Scale, ImU32 Accent, bool Open);

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONTROLS
//------------------------------------------------------------------------------------------------------------------------

struct StackRailHeadInteraction
{
    bool AddClicked    = false;
    bool FolderClicked = false;
};

// StackRail head: title, live count pill, optional "+ Folder" pill + Add button at the right.
[[nodiscard]] bool                     DrawStackRailHead(const KitFaces& Faces, ImVec2 Minimum, float Width,
                                                         const char* Title, const char* CountText);
[[nodiscard]] StackRailHeadInteraction DrawOutlinerHead(const KitFaces& Faces, ImVec2 Minimum, float Width,
                                                        const char* Title, const char* CountText);

// StackRail foot: left/right readouts on one line, hairline above.
void DrawStackRailFoot(const KitFaces& Faces, ImVec2 Minimum, float Width, const char* LeftText,
                       const char* RightText);

// IdentityHeader: icon disc with classification colour, name, sub-line (inspector tops).
void DrawIdentityHeader(const KitFaces& Faces, ImVec2 Minimum, float Width, ImU32 IconColour, const char* Name,
                        const char* Sub);

struct TreeRowInteraction
{
    bool Clicked        = false;   // Row body (selection)
    bool ChevronClicked = false;   // Fold toggle
    bool EyeClicked     = false;   // Visibility toggle
};

// One Outliner tree row (30 px tall).
[[nodiscard]] TreeRowInteraction DrawTreeRow(const KitFaces& Faces, const char* Id, ImVec2 Minimum, float Width,
                                             float Indent, bool HasEnclosed, bool Collapsed, ImU32 DotColour,
                                             const char* Name, const char* Meta, bool Visible, bool Selected);

// One Outliner Folder enclosure row (34 px tall: tabbed folder silhouette, title, enclosed count badge, cascade eye).
[[nodiscard]] TreeRowInteraction DrawFolderRow(const KitFaces& Faces, const char* Id, ImVec2 Minimum, float Width,
                                               float Indent, bool Collapsed, ImU32 AccentColour, const char* Name,
                                               const char* TagText, uint32_t EnclosedCount, bool Visible,
                                               bool Selected);

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
