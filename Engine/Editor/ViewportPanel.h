//============================================================================================================================================
//                                                    VIEWPORTPANEL.H
//============================================================================================================================================
// 🧩 Development editor viewport — the scene column. Header bar with the brand, the dock toggles, the view
//    cycler and the transport strip; the dark view with its axis orb; the command console with its suggestion
//    stack; the stats footer with the day clock. Every control here is local figures: the transport runs, the
//    clock scrubs, the console answers from its quick table.

#pragma once

#include <cstdint>

#include <imgui.h>

namespace Frontier {

class ControlPanel;
struct EditorInstance;

class ViewportPanel final
{
public:
    void AssignControls(ControlPanel* Controls) noexcept;

    // Seats the scene view: RGBA32 top-down rows the view draws under its orb. The headless harness seats a CPU
    //    trace here; the engine build seats its ReSTIR target through AssignViewTexture instead.
    void AssignView(const unsigned char* Rgba, uint32_t Width, uint32_t Height) noexcept;
    void AssignViewTexture(ImTextureID View, uint32_t Width, uint32_t Height) noexcept;

    // Last view rect, so the project can size the view rows to the rect it draws into.
    [[nodiscard]] float QueryViewWidth() const noexcept { return LastW_; }
    [[nodiscard]] float QueryViewHeight() const noexcept { return LastH_; }

    void Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept;

private:
    void RecordBar() noexcept;
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

    ControlPanel* Controls_ = nullptr;

    const unsigned char* ViewRgba_    = nullptr;   // CPU rows; the seated texture id aliases them headless
    ImTextureID          ViewTexture_ = static_cast<ImTextureID>(0);
    uint32_t             ViewW_       = 0u;
    uint32_t             ViewH_       = 0u;
    float                LastW_       = 0.0f;      // last view rect, for QueryViewWidth/QueryViewHeight
    float                LastH_       = 0.0f;

    uint32_t Transport_ = 0u;   // 0 edit, 1 play, 2 simulate — the reference's three runs
    bool     Paused_    = false;
    bool     Realtime_  = true;   // the viewport boots live, like the reference
    bool     DayCycle_  = false;

    bool     MarkersOn_ = true;
    uint32_t ViewPick_  = 0u;
    bool     DockLeft_  = true;
    bool     DockRight_ = true;

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
    char     QuickLabels_[7][48] = {};
    char     CommandPast_[8][128] = {};
    uint32_t PastCount_        = 0u;
    int32_t  PastAt_           = -1;

    float    ClockHours_       = 19.15f;
};

} // namespace Frontier
