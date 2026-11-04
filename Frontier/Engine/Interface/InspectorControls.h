/*==========================================================================================================================================
                                                        INSPECTORCONTROLS.H
==========================================================================================================================================*/
// 🧩 General-purpose Editor surface palette, typeface resolver, vector glyphs, and modular Inspector controls.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/SceneDirectory.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    EDITOR TYPEFACES
//------------------------------------------------------------------------------------------------------------------------

struct EditorTypefaces
{
    ImFont* Light     = nullptr;
    ImFont* Regular   = nullptr;
    ImFont* Medium    = nullptr;
    ImFont* SemiBold  = nullptr;
    ImFont* Bold      = nullptr;
    ImFont* Monospace = nullptr;

    [[nodiscard]] static EditorTypefaces Resolve() noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR PALETTE
//------------------------------------------------------------------------------------------------------------------------

struct InspectorPalette
{
    bool  DarkMode          = true;
    ImU32 ViewportBg        = 0;
    ImU32 ColumnBg          = 0;
    ImU32 StatusBarBg       = 0;
    ImU32 ViewportBarBg     = 0;
    ImU32 InspectorTopBg    = 0;
    ImU32 InspectorBottomBg = 0;
    ImU32 EdgeBorder        = 0;
    ImU32 CountCardBg       = 0;
    ImU32 CountCardBorder   = 0;
    ImU32 SearchBg          = 0;
    ImU32 SearchBorder      = 0;
    ImU32 RowHoverBg        = 0;
    ImU32 RowSelectedTop    = 0;
    ImU32 RowSelectedBottom = 0;
    ImU32 RowSelectedBorder = 0;
    ImU32 InsetFieldBg      = 0;
    ImU32 InsetFieldBorder  = 0;
    ImU32 SliderTrackBg     = 0;
    ImU32 SliderFillBg      = 0;
    ImU32 SliderThumbBg     = 0;
    ImU32 ToggleOffBg       = 0;
    ImU32 ToggleOnBg        = 0;
    ImU32 SegmentedBg       = 0;
    ImU32 SegmentedActiveBg = 0;
    ImU32 TextPrimary       = 0;
    ImU32 TextSecondary     = 0;
    ImU32 TextMuted         = 0;
    ImU32 TextFaint         = 0;
    ImU32 LiveGreen         = 0;
    ImU32 WarmAmber         = 0;
    ImU32 DangerRed         = 0;
    ImU32 ThemeAccent       = 0;

    [[nodiscard]] static InspectorPalette Resolve() noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPECTRUM & GLYPHS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ImU32 ParseHexSpectrum(std::string_view Hex, ImU32 Fallback) noexcept;
[[nodiscard]] ImU32 ModulateSpectrumAlpha(ImU32 Colour, float AlphaMultiplier) noexcept;

void ConstructEditorGlyph(ImDrawList*      DrawList,
                          ImVec2           Centre,
                          float            HalfExtent,
                          ImU32            Colour,
                          std::string_view Identifier,
                          float            StrokeThickness = 1.45f) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    READOUT CELL
//------------------------------------------------------------------------------------------------------------------------

struct InspectorReadoutCell
{
    const char* Caption = "";
    const char* Metric  = "";
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR CONTROLS
//------------------------------------------------------------------------------------------------------------------------

struct InspectorControls
{
    static void ConstructCrumbBar(const InspectorPalette& Palette,
                                  const EditorTypefaces&  Faces,
                                  const char*             GroupLabel);

    // Returns true when the optional *Editor launch button is clicked.
    static bool ConstructRecordHeader(const InspectorPalette& Palette,
                                      const EditorTypefaces&  Faces,
                                      const char*             EyebrowCategory,
                                      const char*             RecordName,
                                      const char*             EditorButtonLabel,
                                      bool                    ToggleSupported,
                                      bool&                   InOutEnabled);

    static void ConstructSectionDivider(const InspectorPalette& Palette,
                                        const EditorTypefaces&  Faces,
                                        const char*             UpperLabel,
                                        const char*             TrailingNote = nullptr);

    static bool ConstructSwitchGrid(const InspectorPalette&     Palette,
                                    const EditorTypefaces&      Faces,
                                    const char*                 IdPrefix,
                                    const InspectorSwitchEntry* Switches,
                                    size_t                      SwitchCount);

    static bool ConstructSliderRow(const InspectorPalette& Palette,
                                   const EditorTypefaces&  Faces,
                                   const char*             Id,
                                   const char*             Label,
                                   float&                  InOutParameter,
                                   float                   Minimum,
                                   float                   Maximum,
                                   const char*             DisplayFormat = "%.2f");

    static bool ConstructIntegerSliderRow(const InspectorPalette& Palette,
                                          const EditorTypefaces&  Faces,
                                          const char*             Id,
                                          const char*             Label,
                                          uint32_t&               InOutParameter,
                                          uint32_t                Minimum,
                                          uint32_t                Maximum,
                                          const char*             Suffix = "");

    static bool ConstructSegmentedRow(const InspectorPalette& Palette,
                                      const EditorTypefaces&  Faces,
                                      const char*             Id,
                                      const char*             Label,
                                      int&                    InOutSelectedIndex,
                                      const char* const*      Options,
                                      int                     OptionCount);

    static bool ConstructColourRow(const InspectorPalette& Palette,
                                   const EditorTypefaces&  Faces,
                                   const char*             Id,
                                   const char*             Label,
                                   float                   InOutRgb[3]);

    static void ConstructReadoutStrip(const InspectorPalette&     Palette,
                                      const EditorTypefaces&      Faces,
                                      const InspectorReadoutCell* Cells,
                                      size_t                      CellCount);

    static bool ConstructActionButton(const InspectorPalette& Palette,
                                      const EditorTypefaces&  Faces,
                                      const char*             Id,
                                      const char*             Label);

    static bool ConstructDangerButton(const InspectorPalette& Palette,
                                      const EditorTypefaces&  Faces,
                                      const char*             Id,
                                      const char*             Label);

    static void ConstructMutedNote(const InspectorPalette& Palette,
                                   const EditorTypefaces&  Faces,
                                   const char*             Text);

    static void ConstructFooter(const InspectorPalette& Palette,
                                const EditorTypefaces&  Faces,
                                const char*             RecordName,
                                const char*             CategoryText);
};

} // namespace Frontier

#endif // FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK
