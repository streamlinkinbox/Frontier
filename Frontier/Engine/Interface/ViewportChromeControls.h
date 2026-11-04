/*==========================================================================================================================================
                                                      VIEWPORTCHROMECONTROLS.H
==========================================================================================================================================*/
// 🧩 The VehicleHMI demo's viewport chrome as ImGui controls, 1:1 with its FrontierEditor.css: the viewport bar (scene
//    heading, live pill, label and icon buttons, segmented switch, select field), the readout overlay, the bring-up card,
//    the failure panel and the status strip.

#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace Frontier
{

struct EditorTypefaces;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STATUS STRIP ITEM
//------------------------------------------------------------------------------------------------------------------------

// 📝 One `#status .status-item`: a spaced upper-case caption beside its measurement.
struct ViewportStatusItem
{
    std::string Caption{};       // [-] e.g. "REALTIME"
    std::string Measurement{};   // [-] e.g. "60 fps"
    ImU32 Colour = 0;
    const char* Glyph = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEWPORT CHROME
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every control draws at screen pixels with its CSS metrics times Scale (the interface scale) and reports its width,
//    or whether it was pressed or changed. Ids are ImGui ids, unique within the calling window.
class ViewportChromeControls
{
  public:
    static constexpr float BarHeight         = 52.0f;   // [px] .viewport-bar
    static constexpr float BarPadding        = 14.0f;   // [px] .viewport-bar padding
    static constexpr float BarGap            = 10.0f;   // [px] .viewport-bar gap
    static constexpr float IconButtonExtent  = 34.0f;   // [px] .icon-button
    static constexpr float SegmentedWidth    = 190.0f;  // [px] the camera-rig switch (EditorControls.js)
    static constexpr float SelectWidth       = 210.0f;  // [px] the inspection select (EditorControls.js)
    static constexpr float StatusStripHeight = 28.0f;   // [px] #shell's status row

    // 📝 .viewport-tabs .scene-pill: the scene's name hanging from the top of the tab strip, centred across it.

    static void  ConstructBarBackground(ImDrawList* DrawList, ImVec2 Min, ImVec2 Max, float Scale);


    [[nodiscard]] static float MeasureLabelButton(const EditorTypefaces& Faces, const char* Label, float Scale);
    static bool  ConstructLabelButton(const char*            Id,
                                      const EditorTypefaces& Faces,
                                      ImVec2                 Min,
                                      ImVec2                 Size,
                                      const char*            Label,
                                      bool                   Enabled,
                                      const char*            Tooltip,
                                      float                  Scale);
    static bool  ConstructIconButton(const char* Id, ImVec2 Min, float Extent, const char* Glyph, bool Active, bool Enabled, const char* Tooltip);
    static bool  ConstructSegmentedSwitch(const char*            Id,
                                          const EditorTypefaces& Faces,
                                          ImVec2                 Min,
                                          ImVec2                 Size,
                                          const char* const*     Options,
                                          int                    OptionCount,
                                          int&                   InOutIndex,
                                          const bool*            EnabledOptions,
                                          const char* const*     Tooltips,
                                          float                  Scale);
    static bool  ConstructSelectField(const char*            Id,
                                      const EditorTypefaces& Faces,
                                      ImVec2                 Min,
                                      ImVec2                 Size,
                                      const char* const*     Options,
                                      int                    OptionCount,
                                      int&                   InOutIndex,
                                      float                  Scale);
    // 📝 #hud: the monospace readout at the stage's top-left with its close button; true when the close button was pressed.
    static bool  ConstructReadoutOverlay(const char*                     Id,
                                         const EditorTypefaces&          Faces,
                                         ImVec2                          StageMin,
                                         ImVec2                          StageMax,
                                         const std::vector<std::string>& Lines,
                                         const char*                     CloseTooltip,
                                         float                           Scale);

    // 📝 #overlay-notice: a short line centred near the stage's bottom edge.
    static void  ConstructOverlayNotice(ImDrawList* DrawList, const EditorTypefaces& Faces, ImVec2 StageMin, ImVec2 StageMax, const char* Text, float Scale);
    // 📝 #bring-up: the start-up card over the whole stage (heading, caption, 360 px progress bar).
    static void  ConstructBringUpCard(ImDrawList*            DrawList,
                                      const EditorTypefaces& Faces,
                                      ImVec2                 StageMin,
                                      ImVec2                 StageMax,
                                      const char*            Heading,
                                      const char*            Caption,
                                      float                  Fraction,
                                      float                  Scale);
    // 📝 #failure: the failure panel at the stage's bottom-left.
    static void  ConstructFailurePanel(ImDrawList* DrawList, const EditorTypefaces& Faces, ImVec2 StageMin, ImVec2 StageMax, const std::string& Text,
                                       float Scale);
    // 📝 #status: items from the left, then a fill, then the trailing items against the right edge.
    static void  ConstructStatusStrip(ImDrawList*                            DrawList,
                                      const EditorTypefaces&                 Faces,
                                      ImVec2                                 Min,
                                      ImVec2                                 Max,
                                      const std::vector<ViewportStatusItem>& Leading,
                                      const std::vector<ViewportStatusItem>& Trailing,
                                      float                                  Scale);
};

} // namespace Frontier
