//============================================================================================================================================
//                                                  EDITORSTYLESPECIFICATION.H
//============================================================================================================================================
// 📦 The development editor's one style: the trapezoid tab sheet (Patches A/B/C), the geometry tokens and the colour tokens. The game
//    editor and the SolidArc editor both seat it through SeatEditorStyle, so a tab reads the same wherever it is docked.

#pragma once

#include <imgui.h>

namespace Frontier {

#ifdef FRONTIER_DEVELOPMENT

/// 📦 Seats the sheet's tab figures, geometry tokens and colour tokens over whatever the host seated before.
/// in    Applied   [-]  the live ImGui style; every figure below is overwritten
/// note  🔴 every Tab* colour is seated, including the selected overline: the stock overline is ImGui's blue, which is the blue
///       tab SolidArc drew before it shared this seating
/// tag   api, nonallocating, nonthrowing
inline void SeatEditorStyle(ImGuiStyle& Applied) noexcept
{
    // Trapezoid sheet (Patches A/B/C; the figures are References/DockWorkspace.html's). The four geometry
    //    figures repeat the SwapchainExchange seating on purpose: the headless proof never runs the swapchain,
    //    and this call alone must draw the identical strip there.
    Applied.TabSlant            = 14.0f;   // [px] inset of a tab's two upper corners
    Applied.TabOverlap           = 24.0f;   // [px] neighbour interlock, so slanted edges overlap
    Applied.TabHeight            = 24.0f;   // [px] strip height
    Applied.TabStripPadTop       = 4.0f;    // [px] strip showing above the tabs
    Applied.TabMinWidthBase      = 110.0f;  // [px] tab width floor: two tabs plus the pinned add
    Applied.TabMinWidthShrink    = 110.0f;  // [px] disc seat inside the left column with no shrink
    Applied.DockingNodeHasCloseButton = false; // [-] no close-all mark on the node: a tab's own mark
                                                 //     shuts only its tab, and the add menu seats it back
    Applied.TabRounding          = 0.0f;    // [px] the sheet's corners are cut, not rounded
    Applied.TabBorderSize        = 0.0f;    // [px] no tab outline
    Applied.TabBarBorderSize     = 0.0f;    // [px] no strip outline
    Applied.TabButtonRounding    = 1.0f;    // [-] tab buttons are full discs

    // Geometry tokens: pills for fields, square-cut tabs, hairlines everywhere else.
    Applied.WindowPadding      = ImVec2(14.0f, 12.0f);
    Applied.FramePadding       = ImVec2(13.0f, 9.0f);
    Applied.ItemSpacing        = ImVec2(10.0f, 8.0f);
    Applied.ItemInnerSpacing   = ImVec2(6.0f, 4.0f);
    Applied.ScrollbarSize      = 8.0f;
    Applied.WindowRounding     = 8.0f;
    Applied.ChildRounding      = 12.0f;
    Applied.FrameRounding      = 16.0f;
    Applied.PopupRounding      = 18.0f;
    Applied.ScrollbarRounding  = 9.0f;
    Applied.GrabRounding       = 12.0f;
    Applied.WindowBorderSize   = 1.0f;
    Applied.ChildBorderSize    = 0.0f;
    Applied.FrameBorderSize    = 1.0f;
    Applied.PopupBorderSize    = 1.0f;

    // Colour tokens. The lone tab carries the window tint, the sheet's seamless rule: the tab and the body
    //    it opens onto are one surface, and the strip behind is the vendor's own untinted dark. TitleBg and
    //    DockingEmptyBg stay stock on purpose — one of them paints the unfocused strip — and TitleBgActive
    //    is seated to the same dark, because the focused node's strip paints with it and the stock blue
    //    would wedge the strip. The proof gates the tab edges against that uniform dark.
    ImVec4* Tints = Applied.Colors;
    Tints[ImGuiCol_Text]                  = ImVec4(0.941f, 0.941f, 0.941f, 1.0f);   // #f0f0f0
    Tints[ImGuiCol_TextDisabled]          = ImVec4(0.361f, 0.361f, 0.361f, 1.0f);   // #5c5c5c
    Tints[ImGuiCol_WindowBg]              = ImVec4(0.071f, 0.071f, 0.071f, 1.0f);   // #121212
    Tints[ImGuiCol_ChildBg]               = ImVec4(0.000f, 0.000f, 0.000f, 0.0f);
    Tints[ImGuiCol_PopupBg]               = ImVec4(0.102f, 0.102f, 0.102f, 1.0f);   // #1a1a1a
    Tints[ImGuiCol_Border]                = ImVec4(1.000f, 1.000f, 1.000f, 0.05f);
    Tints[ImGuiCol_BorderShadow]          = ImVec4(0.000f, 0.000f, 0.000f, 0.0f);
    Tints[ImGuiCol_FrameBg]               = ImVec4(0.000f, 0.000f, 0.000f, 1.0f);   // #000000
    Tints[ImGuiCol_FrameBgHovered]        = ImVec4(0.031f, 0.031f, 0.031f, 1.0f);
    Tints[ImGuiCol_FrameBgActive]         = ImVec4(0.071f, 0.071f, 0.071f, 1.0f);
    Tints[ImGuiCol_MenuBarBg]             = ImVec4(0.071f, 0.071f, 0.071f, 1.0f);
    Tints[ImGuiCol_TitleBgActive]         = ImVec4(0.039f, 0.039f, 0.039f, 1.0f);   // #0a0a0a
    Tints[ImGuiCol_ScrollbarBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.0f);
    Tints[ImGuiCol_ScrollbarGrab]         = ImVec4(0.141f, 0.141f, 0.141f, 1.0f);   // #242424
    Tints[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.180f, 0.180f, 0.180f, 1.0f);   // #2e2e2e
    Tints[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.200f, 0.200f, 0.200f, 1.0f);   // #333333
    Tints[ImGuiCol_CheckMark]             = ImVec4(1.000f, 1.000f, 1.000f, 1.0f);
    Tints[ImGuiCol_SliderGrab]            = ImVec4(0.878f, 0.878f, 0.878f, 1.0f);   // #e0e0e0
    Tints[ImGuiCol_SliderGrabActive]      = ImVec4(1.000f, 1.000f, 1.000f, 1.0f);
    Tints[ImGuiCol_Button]                = ImVec4(0.133f, 0.133f, 0.133f, 1.0f);   // #222222
    Tints[ImGuiCol_ButtonHovered]         = ImVec4(0.180f, 0.180f, 0.180f, 1.0f);
    Tints[ImGuiCol_ButtonActive]          = ImVec4(0.220f, 0.220f, 0.220f, 1.0f);
    Tints[ImGuiCol_Header]                = ImVec4(0.165f, 0.165f, 0.165f, 1.0f);   // #2a2a2a
    Tints[ImGuiCol_HeaderHovered]         = ImVec4(0.110f, 0.110f, 0.110f, 1.0f);   // #1c1c1c
    Tints[ImGuiCol_HeaderActive]          = ImVec4(0.165f, 0.165f, 0.165f, 1.0f);
    Tints[ImGuiCol_Separator]             = ImVec4(0.180f, 0.180f, 0.180f, 1.0f);
    Tints[ImGuiCol_SeparatorHovered]      = ImVec4(0.298f, 0.302f, 1.000f, 1.0f);
    Tints[ImGuiCol_SeparatorActive]       = ImVec4(0.424f, 0.467f, 1.000f, 1.0f);
    Tints[ImGuiCol_ResizeGrip]            = ImVec4(0.180f, 0.180f, 0.180f, 1.0f);
    Tints[ImGuiCol_ResizeGripHovered]     = ImVec4(0.298f, 0.302f, 1.000f, 1.0f);
    Tints[ImGuiCol_ResizeGripActive]      = ImVec4(0.424f, 0.467f, 1.000f, 1.0f);
    Tints[ImGuiCol_Tab]                   = ImVec4(0.149f, 0.149f, 0.173f, 1.0f);   // #26262c
    Tints[ImGuiCol_TabHovered]            = ImVec4(0.196f, 0.196f, 0.227f, 1.0f);   // #32323a
    Tints[ImGuiCol_TabActive]             = ImVec4(0.071f, 0.071f, 0.071f, 1.0f);   // #121212
    Tints[ImGuiCol_TabUnfocused]          = ImVec4(0.149f, 0.149f, 0.173f, 1.0f);
    Tints[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.071f, 0.071f, 0.071f, 1.0f);
    Tints[ImGuiCol_TabDimmed]             = ImVec4(0.118f, 0.118f, 0.141f, 1.0f);   // #1e1e24
    Tints[ImGuiCol_TabDimmedSelected]     = ImVec4(0.071f, 0.071f, 0.071f, 1.0f);
    Tints[ImGuiCol_TabSelectedOverline]   = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Tints[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Tints[ImGuiCol_DockingPreview]        = ImVec4(1.000f, 1.000f, 1.000f, 0.12f);
    Tints[ImGuiCol_TextSelectedBg]        = ImVec4(0.424f, 0.467f, 1.000f, 0.35f);
}

#endif

} // namespace Frontier
