//============================================================================================================================================
//                                                       EDITORHOST.H
//============================================================================================================================================
// 🧩 Development editor host — seats the theme, builds the dock columns, and records the three panels over the
//    project's instance feed. The host owns the controls the panels draw with and the four faces they draw in.
//    The shade over the columns is the original Control Centre host, not an editor lookalike: the editor feeds
//    it the pointer every tick through TickShade and draws it last through Record, and reads the dashboard's
//    own figures back through the seams below.

#pragma once

#include "ControlPanel.h"
#include "OutlinerPanel.h"
#include "ViewportPanel.h"
#include "InspectorPanel.h"
#include "ShadeTick.h"

#include "../DisplayPresentation/ControlCentreHost.h"
#include "../DisplayPresentation/NotificationQueue.h"
#include "../DisplayPresentation/PixelSpace.h"
#include "../DeviceExchange/InputExchange.h"

#include <cstdint>

#include <imgui.h>

namespace Frontier {

class EditorHost
{
public:
    EditorHost() noexcept;
    ~EditorHost() noexcept;

    EditorHost(const EditorHost&)            = delete;
    EditorHost& operator=(const EditorHost&) = delete;

    // Call once after the ImGui context exists — seats the sheet's tab figures, the full token theme, and the
    //    four faces. Idempotent: the faces seat once (a second seating would duplicate the glyph sheet), the style
    //    re-seats freely. Runs last, over the scheduler's own seating, so the editor's tokens win everywhere.
    void ApplyTheme() noexcept;

    // Seats the shade on a display: the original host opens its springs on Width × Height logical pixels.
    //    TickShade and the overlay branch of Record rest until this returns true.
    bool SeatShade(uint32_t Width, uint32_t Height) noexcept;

    // Call every tick before ImGui::NewFrame() — hands the pointer contact (position, left button, wheel
    //    clicks) and the frame interval to the shade, mirrors a settings change into a toast, and keeps the
    //    gear's open figure in agreement with the shade's own pose. The harness asks ShadeCoversPointer next
    //    and parks the ImGui pointer while the overlay owns the contact.
    void TickShade(float CursorX, float CursorY, bool Down, float Wheel, float DeltaSeconds) noexcept;

    // True while the shade owns the pointer contact this frame: taps and carries over the notch, the card,
    //    and the scrim must not reach the dock columns below.
    [[nodiscard]] bool ShadeCoversPointer() const noexcept;

    // Call every tick between ImGui::NewFrame() and ImGui::Render() — records the fullscreen dock host, the
    //    dockspace, and the three panels over the project's feed, then the shade above them. The panels borrow
    //    the feed and edit it in place; the sheet must already describe the currently picked instance (see
    //    QueryPickedInstance).
    void Record(EditorInstance* Instances, uint32_t InstanceCount, EditorSheet* PickedSheet) noexcept;

    // Seats the viewport's scene view (see ViewportPanel::AssignView) and reads back the view rect.
    void AssignView(const unsigned char* Rgba, uint32_t Width, uint32_t Height) noexcept;
    // The engine build's route: the resolved scene image as an ImGui texture (the Vulkan backend's descriptor
    //    set), drawn under the viewport's orb. Re-seat after every swapchain rebuild.
    void AssignViewTexture(ImTextureID View, uint32_t Width, uint32_t Height) noexcept;

    // The outliner's foot strip figures; the project refreshes the struct in place each tick.
    void AssignReadout(const EditorReadout* Readout) noexcept;
    // Bumps when a drag reparents a row: the project re-reads the roster order.
    [[nodiscard]] uint32_t QueryOrderRevision() const noexcept;
    [[nodiscard]] float QueryViewWidth() const noexcept;
    [[nodiscard]] float QueryViewHeight() const noexcept;

    // The dashboard's own figures: GI seats the render path, render scale trims the view rows, and the
    //    revision bumps on every change the tiles and the pill make.
    [[nodiscard]] bool     QueryGiEnabled() const noexcept;
    [[nodiscard]] float    QueryRenderScale() const noexcept;
    [[nodiscard]] uint32_t QueryRevision() const noexcept;
    [[nodiscard]] const ControlCentreSettings& QueryShadeSettings() const noexcept;
    // The tier's criteria with the shade's shadow-resolution override already applied — what the renderer runs.
    [[nodiscard]] FidelityCriteria QueryShadeCriteria() const noexcept;
    void AssignProjectName(const char* Name) noexcept;

    // The shade's pose and page for the harness log: open while the sheet travels or rests down, page zero
    //    on the dashboard, one on the settings hub.
    [[nodiscard]] bool     QueryShadeOpen() const noexcept;
    [[nodiscard]] uint32_t QueryShadePage() const noexcept;

    // The harness seams; the preview taps and drags the shade through them.
    [[nodiscard]] float QueryGiTileX() const noexcept;
    [[nodiscard]] float QueryGiTileY() const noexcept;
    [[nodiscard]] float QueryGripX() const noexcept;
    [[nodiscard]] float QueryGripY() const noexcept;
    [[nodiscard]] float QueryNotchX() const noexcept;
    [[nodiscard]] float QueryNotchY() const noexcept;
    [[nodiscard]] float QueryPillX0() const noexcept;
    [[nodiscard]] float QueryPillX1() const noexcept;
    [[nodiscard]] float QueryPillY() const noexcept;
    [[nodiscard]] float QueryGearX() const noexcept;
    [[nodiscard]] float QueryGearY() const noexcept;

    // The primary pick — the instance the sheet must describe. kNoEditorInstance when nothing is picked.
    [[nodiscard]] uint32_t QueryPickedInstance() const noexcept;

    // Faces seated by ApplyTheme (four when both archives resolve, zero without the define).
    [[nodiscard]] int QueryFontCount() const noexcept;

    // The test seam; the proof drives the pick through it.
    void PickInstance(uint32_t Index) noexcept;

    // The tab seams; the proof shuts each tab through its close mark and seats it again through the
    //    add menu. Tabs run outliner, viewport, inspector; anything else reads shut.
    [[nodiscard]] bool QueryTabOpen(uint32_t Tab) const noexcept;
    // The add control's last drawn centre; the proof taps it through these. -1 when the column never built.
    [[nodiscard]] float QueryTabAddX() const noexcept;
    [[nodiscard]] float QueryTabAddY() const noexcept;


    // The viewport's orbit in and out: the harness seats home from its camera, and reads the pose back
    //    for its trace (the game poses the fly camera off the same figures).
    void SeatViewportOrbit(const ViewportOrbit& Seated) noexcept;
    [[nodiscard]] const ViewportOrbit& QueryViewportOrbit() const noexcept;

private:
    // Splits the dockspace into the outliner / viewport / inspector columns on the first tick, then rests.
    //    Runs with the host window open: the builder addresses the host, and without it there is nothing to
    //    build against.
    void ConstructLayout() noexcept;

    // Polls the add control and draws its menu.
    void RecordTabAdd() noexcept;

    ControlPanel      Controls_;          // first: the panels borrow it
    OutlinerPanel  Outliner_;
    ViewportPanel  Viewport_;
    InspectorPanel Inspector_;

    ControlCentreHost Shade_;             // the original shade, last: it draws above the dock columns
    InputExchange     ShadeInput_;        // the contact TickShade hands it every tick
    PixelSpace        ShadeSurface_;      // the foreground list its primitives land on
    NotificationQueue Toasts_;            // the toasts a settings change raises
    TelemetryMetrics  Telemetry_;         // the FPS tile's readout, fed through ShadeTick

    bool ShadeOpen_    = false;           // shut at boot, Android-style; shared with the viewport gear
    bool OpenEcho_     = false;           // the gear's last obeyed figure: only an edge moves the shade
    bool OutlinerTabOpen_  = true;        // the outliner tab's close mark clears this; the add menu seats it
    bool ViewportTabOpen_  = true;        // the viewport tab's close mark clears this; the add menu seats it
    bool InspectorTabOpen_ = true;        // the inspector tab's close mark clears this; the add menu seats it
    ImGuiID LeftColumn_    = 0u;          // the left column's address, for the add control and its poll
    bool ShadeSeated_  = false;           // SeatShade has opened the host's springs
    uint32_t ToastRevision_ = 0u;         // the settings revision the last toast answered

    bool FontsSeated_ = false;
    bool OutlinerRaised_ = false;
    int  FontCount_   = 0;
};

} // namespace Frontier
