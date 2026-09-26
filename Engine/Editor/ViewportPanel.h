//============================================================================================================================================
//                                                    VIEWPORTPANEL.H
//============================================================================================================================================
// 🧩 Development editor viewport — the scene column. Header bar with the brand, the dock toggles, the views
//    menu and the transport strip; the dark view with its orbit gizmo; the command console with its suggestion
//    stack; the stats footer with the day clock. Every control here is local figures: the transport runs, the
//    clock scrubs, the console answers from its quick table, and the views menu poses the orbit the harness
//    re-traces from.

#pragma once

#include <cstdint>
#include "ViewportBillboards.h"

#include <imgui.h>

namespace Frontier {

class ControlPanel;
struct EditorInstance;
struct EditorReadout;

// The shared viewport panel can wear the game editor chrome or the SolidArc CAD chrome. The framebuffer body
// stays common; only the top tool strip changes.
enum class ViewportPanelChrome : uint32_t
{
    FrontierGame = 0u,
    SolidArcCad,
};

// The viewport's orbit: yaw and pitch around a target at a distance, the projection in use, and which
//    compass snap posed it (home reads free). The harness seats home from its own camera; the views menu,
//    the gizmo and the wheel rewrite the figures and bump Revision, and the harness re-poses its trace
//    from them. Yaw 0 with pitch 0 looks along +Y, exactly the solver's euler, so a snap poses the fly
//    camera with no conversion at all.
struct ViewportOrbit
{
    float    Yaw      = 0.0f;   // [rad] 0 faces +Y, positive turns toward +X
    float    Pitch    = 0.0f;   // [rad] positive looks up toward +Z
    float    Distance = 4.5f;   // [m] eye to target
    float    Target[3] = { 0.0f, 0.5f, 1.4f };
    bool     Ortho    = false;  // false reads perspective, true orthographic
    uint32_t ViewPoint = 0u;    // 0 home, 1 front, 2 back, 3 right, 4 left, 5 top, 6 bottom
    uint32_t Revision = 0u;     // bumps on every write the panels make
};

class ViewportPanel final
{
public:
    void AssignControls(ControlPanel* Controls) noexcept;
    // The tab's close mark writes through this; null leaves the tab without one.
    void AssignTabOpen(bool* Open) noexcept;
    // Lets SolidArc seat this exact panel beside Project-Zero without sharing the same ImGui title/id.
    void AssignWindowTitle(const char* Title) noexcept;
    // Switches the top chrome between the Project-Zero/game controls and SolidArc's CAD controls.
    void AssignChrome(ViewportPanelChrome Chrome) noexcept { Chrome_ = Chrome; }

    // Seats the scene view: RGBA32 top-down rows the view draws under its orb. The headless harness seats a CPU
    //    trace here; the engine build seats its ReSTIR target through AssignViewTexture instead.
    ViewportBillboards Billboards;
    void AssignView(const unsigned char* Rgba, uint32_t Width, uint32_t Height) noexcept;
    void AssignViewTexture(ImTextureID View, uint32_t Width, uint32_t Height, uint32_t StorageWidth = 0u, uint32_t StorageHeight = 0u) noexcept;

    // Shares the Control Centre shade's open figure with the bar's gear.
    void AssignShadeOpen(bool* Open) noexcept { ShadeOpen_ = Open; }

    // The foot strip's live figures (triangle total); without a readout the strip prints its resting dash.
    void AssignReadout(const EditorReadout* Readout) noexcept;

    // Seats the orbit's home from the harness camera (yaw, pitch, target, distance); the snaps and the
    //    gizmo work from there. Reads the orbit back for the harness trace and the game camera.
    void SeatViewportOrbit(const ViewportOrbit& Seated) noexcept;
    [[nodiscard]] const ViewportOrbit& QueryViewportOrbit() const noexcept { return Orbit_; }

    // Last view rect, so the project can size the view rows to the rect it draws into.
    [[nodiscard]] float QueryViewWidth() const noexcept { return LastW_; }
    [[nodiscard]] float QueryViewHeight() const noexcept { return LastH_; }

    // The view's pointer, in view fractions (0..1 across the drawn scene). QueryViewTap hands the click that
    //    landed on the scene view exactly once — the game turns it into a GPU pick tap; Additive reads true
    //    while Shift extends the pick, Blender's rule. QueryViewAim reads the live hover every tick, for the
    //    gizmo's grip test. Both stay false while the compass, the bar or a popup owns the pointer.
    [[nodiscard]] bool QueryViewTap(float* AcrossU, float* DownV, bool* Additive) noexcept;
    [[nodiscard]] bool QueryViewAim(float* AcrossU, float* DownV) const noexcept;

    void Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept;

private:
    void RecordBar() noexcept;
    void RecordSolidArcBar() noexcept;
    void RecordView() noexcept;
    void RecordCommand(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    void RecordFooter(EditorInstance* Instances, uint32_t InstanceCount) noexcept;

    void SetTransport(uint32_t Mode) noexcept;
    void SetPaused(bool Paused) noexcept;
    void SetRealtime(bool Realtime) noexcept;
    void StepOnce() noexcept;

    void PaintSuggestions(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    void RunSugRow(uint32_t Row, EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    static int ConsoleCallback(ImGuiInputTextCallbackData* Edit) noexcept;

    ControlPanel*        Controls_ = nullptr;
    bool*                TabOpen_ = nullptr;
    const EditorReadout* Readout_  = nullptr;
    const char*          WindowTitle_ = "Viewport";
    ViewportPanelChrome  Chrome_ = ViewportPanelChrome::FrontierGame;

    const unsigned char* ViewRgba_    = nullptr;   // CPU rows; the seated texture id aliases them headless
    ImTextureID          ViewTexture_ = static_cast<ImTextureID>(0);
    uint32_t             ViewW_       = 0u;
    uint32_t             ViewH_       = 0u;
    uint32_t             StorageW_    = 0u;
    uint32_t             StorageH_    = 0u;
    bool                 CanvasDragging_ = false;
    float                LastW_       = 0.0f;      // last view rect, for the QueryView* rect
    float                LastH_       = 0.0f;

    // The view tap and the live aim, seated by RecordView, consumed through QueryViewTap / QueryViewAim.
    bool  TapLive_     = false;   // a click landed on the view this tick; cleared by QueryViewTap
    bool  TapAdditive_ = false;   // Shift rode the click
    float TapU_        = 0.0f;    // view fractions, 0..1 top-left origin
    float TapV_        = 0.0f;
    bool  AimLive_     = false;   // the pointer hovers the view this tick
    float AimU_        = 0.0f;
    float AimV_        = 0.0f;
    bool*                ShadeOpen_ = nullptr;  // the Control Centre shade's open figure, shared with the host

    uint32_t Transport_ = 0u;   // 0 edit, 1 play, 2 simulate — the reference's three runs
    bool     Paused_    = false;
    bool     Realtime_  = true;   // the viewport boots live, like the reference

    bool     MarkersOn_ = true;
    ViewportOrbit Orbit_;   // the views menu, the gizmo and the wheel pose through this
    ViewportOrbit Home_    = {};   // the seated home; the projection rows restore it
    uint32_t SolidArcSelectMask_ = 1u;   // Body, Face, Edge, Vertex bits: HTML top-panel parity
    uint32_t SolidArcShade_      = 3u;   // Wire, Flat, Plastic, Matcap
    uint32_t SolidArcGizmo_      = 0u;   // Move, Rotate, Scale
    uint32_t SolidArcView_       = 3u;   // Top, Front, Right, Iso, Ortho
    bool     DockLeft_  = true;
    bool     DockRight_ = true;

    bool     ConsoleOpen_      = false;   // Ctrl+K raises the command console; shut till then
    char     CommandText_[128] = {};
    char     CommandEcho_[128] = {};
    double   EchoUntil_        = 0.0;   // the echo's 3.2-second lease
    bool     FocusCommand_     = false;
    bool     CommandFocus_     = false;   // doubles as last tick's focus: the blur trips the stack's grace
    bool     SugShut_          = false;   // a run shuts the stack until the text moves again
    double   SugUntil_         = 0.0;   // the stack lingers 120ms past blur, so its clicks land
    char     LastPaint_[128]   = {};
    struct SugRow
    {
        uint8_t Sort = 0u;   // 0 quick, 1 example, 2 verb, 3 entry, 4 bad
        uint8_t At   = 0u;   // index into the sort's own table
    };
    SugRow   SugRows_[9]       = {};   // the stack's rows, repainted while open
    uint32_t SugCount_         = 0u;
    uint32_t SugIndex_         = 0u;
    char     LastSugText_[128] = {};   // fresh text re-seats the standing row, as a repaint does
    bool     CaretToEnd_       = false;   // an insert parks the caret past its own tail
    char     Ghost_[64]        = {};
    char     QuickLabels_[6][48] = {};
    char     CommandPast_[8][128] = {};
    uint32_t PastCount_        = 0u;
    int32_t  PastAt_           = -1;

    bool     ViewMenuWasOpen_  = false;   // the views dropdown fades in like the category menu
    double   ViewMenuOpenedAt_ = 0.0;
    float    ViewChevronAnim_  = 0.0f;
    bool     OrbHeld_  = false;   // a gizmo drag owns the pointer
    bool     OrbMoved_ = false;   // the hold turned into an orbit (past the tap slop)
    float    OrbDownX_ = 0.0f;    // where the hold started, for the tap slop
    float    OrbDownY_ = 0.0f;
    uint32_t OrbHot_   = 0u;      // the hot gizmo pad, 0 none, 1..6 pads +X −X +Y −Y +Z −Z
};

} // namespace Frontier
