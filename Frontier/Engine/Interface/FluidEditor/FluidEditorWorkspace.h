/*==========================================================================================================================================
                                                      FLUIDEDITORWORKSPACE.H
==========================================================================================================================================*/
// 🧩 The Fluid editor as the Particle editor's window lays an editor out: the Outliner (the volume's four parts), the viewport (the solver's picture, its
//    tools, HUD and transport), the Log Stream, the Inspector (the selected part's page, on the real controls) and the preset library. The behaviour is
//    the prototype's (FluidPanel.js): its keys, its pointer tools, its transport, its camera and what each edit does.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/WindowControls.h"
#include "Engine/Interface/FluidEditor/FluidEditorPages.h"
#include "Engine/Interface/FluidEditor/FluidViewportPresentation.h"

#include <imgui.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

struct FluidLogLine
{
    std::string Time;
    std::string Category;
    std::string Text;
    std::string Detail;
    bool        Warm = false;
};

// 📝 Where a control of the window lay at its last layout (names like "row:Emitter", "eye:Sun", "tool:flamethrower", "play", "view:top", "library-card:<preset>"):
//    the rectangle the editor's windows share.
using FluidControlRectangle = EditorControlRectangle;

class FluidEditorWorkspace
{
  public:
    FluidEditorWorkspace();

    // 📝 The record the window edits and the name the Outliner shows for it; the window attaches both on every refresh it is open.
    void AttachRecord(FluidVolumeConfiguration& InRecord, std::string InName);
    [[nodiscard]] FluidVolumeConfiguration* QueryRecord() const noexcept { return Record; }
    // 📝 The window closes: the record is let go (what it holds stays as the person left it).
    void DetachRecord() noexcept;

    // 📝 One interface tick: asks the presenter for the picture, lays the editor out in the rectangle (the editor window's body), then settles the verbs
    //    the layout raised.
    void Construct(ImVec2 Minimum, ImVec2 Maximum, float Scale, double DeltaSeconds);

    // 📝 The verbs the pages, the keys and the captures use:
    //    "select:<Domain|Emitter|Collider|Sun>", "tab:<source|simulation|rendering>", "preset:<identity>", "defaults", "enabled:on|off|toggle",
    //    "part:<part>:toggle", "add:<fire|smoke|sphere|cylinder|slab>", "remove:<Emitter|Collider>",
    //    "play", "pause", "toggle-play", "step", "restart", "burst" / "burst:<burst|shrapnel|salvo|collide|mushroom>", "burst-kind:<kind>",
    //    "speed:<0.25|0.5|1|2>", "tool:<orbit|flamethrower|detonate>", "turntable:on|off|toggle", "bounds:toggle",
    //    "view:<perspective|front|side|top>", "camera:fit", "camera:orbit,<dx>,<dy>" (pixels), "camera:zoom,<delta>", "camera:snap",
    //    "library:on|off", "library-group:<all|fire|smoke|blast>", "library-search:<text>", "close".
    bool Apply(std::string_view Action);

    // 📝 A host that runs the volume without the presenter reports it here; QueryReadout is the presenter's when it has one.
    void AssignReadout(const FluidVolumeReadout& Readout);
    void RevokeReadout() noexcept;
    [[nodiscard]] const FluidVolumeReadout* QueryReadout() const noexcept;
    // 📝 The verbs only a host without the presenter can carry out ("restart", "step", "burst:<kind>"); the call takes them.
    [[nodiscard]] std::vector<std::string> TakeHostRequests();

    [[nodiscard]] FluidPart                         QuerySelected() const noexcept { return Selected; }
    [[nodiscard]] FluidTab                          QueryTab() const noexcept { return Tab; }
    [[nodiscard]] bool                              QueryLibraryShown() const noexcept { return LibraryShown; }
    [[nodiscard]] const std::string&                QueryLibraryGroup() const noexcept { return LibraryGroup; }
    [[nodiscard]] size_t                            QueryLibraryCards() const noexcept { return LibraryCards; }
    // 📝 The card the sheet marks "Applied": the preset last loaded; empty when none was.
    [[nodiscard]] const std::string&                QueryLibraryMarked() const noexcept { return LibraryMarked; }
    [[nodiscard]] bool                              QueryPlaying() const noexcept { return Record != nullptr && !Record->Gas.Paused; }
    [[nodiscard]] const std::string&                QueryBurstKind() const noexcept { return BurstKind; }
    [[nodiscard]] const FluidViewportImage&         QueryImage() const noexcept { return Image; }
    [[nodiscard]] const FluidViewportRequest&       QueryLastRequest() const noexcept { return LastRequest; }
    [[nodiscard]] const std::vector<FluidLogLine>&  QueryLog() const noexcept { return Log; }
    [[nodiscard]] const std::vector<FluidControlRectangle>& QueryControls() const noexcept { return Controls; }
    [[nodiscard]] const FluidControlRectangle*      FindControl(std::string_view Name) const noexcept;
    [[nodiscard]] std::array<ImVec2, 2>             QueryViewport() const noexcept { return { ViewportMinimum, ViewportMaximum }; }
    [[nodiscard]] const PyroOrbitCamera&            QueryCamera() const noexcept { return Camera; }
    [[nodiscard]] const FluidEditorMemory&          QueryMemory() const noexcept { return Memory; }
    // 📝 The projected domain bounds the last refresh drew (the prototype's SVG overlay), as screen segments.
    [[nodiscard]] const std::vector<std::array<float, 4>>& QueryBoundsSegments() const noexcept { return BoundsSegments; }

    FluidViewportPresenter Presenter;

  private:
    void ConstructOutliner(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void ConstructViewport(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void ConstructTransport(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void ConstructLogStream(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void ConstructInspector(ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void ConstructLibrarySheet(ImVec2 Minimum, ImVec2 Maximum, float Scale);
    // 📝 The pointer on the picture: orbit, pan, the source brush and the burst tool, as FluidPanel.js ConnectViewport has them.
    void ConstructPointer(ImVec2 Minimum, ImVec2 Maximum);
    void Present(uint32_t Width, uint32_t Height, double DeltaSeconds);
    void AppendLog(std::string Category, std::string Text, std::string Detail = {}, bool Warm = false);
    // 📝 An invisible button over a rectangle that remembers where it lay; true when it was pressed.
    bool Press(const char* Name, ImVec2 Minimum, ImVec2 Size);
    [[nodiscard]] const FluidVolumeReadout* ResolveReadout() const noexcept;
    // 📝 The domain's box now (the readout's surged size when a solver is running, the record's otherwise): where the pointer's ray and the bounds overlay look.
    void ResolveBoundsBox(std::array<float, 3>& OutMinimum, std::array<float, 3>& OutMaximum) const noexcept;
    void ResolveFocus(bool UseSettings);
    void ApplyTool(const char* Tool);

    FluidVolumeConfiguration*                 Record = nullptr;
    std::string                               RecordName;
    FluidEditorMemory                         Memory;
    FluidPart                                 Selected = FluidPart::Emitter;
    FluidTab                                  Tab      = FluidTab::Source;
    PyroOrbitCamera                           Camera;
    std::vector<std::string>                  Requests;
    std::vector<std::string>                  HostRequests;
    std::vector<FluidLogLine>                 Log;
    std::vector<FluidControlRectangle>        Controls;
    std::optional<FluidVolumeReadout>         Assigned;
    FluidViewportImage                        Image;
    FluidViewportRequest                      LastRequest;
    std::vector<FluidBurstRequest>            PendingBursts;       // [-] raised since the last refresh
    uint32_t                                  PendingSteps  = 0;   // [-] single steps raised since the last refresh
    bool                                      ClearPending  = false;
    FluidBrushRequest                         Brush;
    std::vector<std::array<float, 4>>         BoundsSegments;
    std::string                               BurstKind     = "burst";
    std::string                               LibraryGroup  = "all";
    std::string                               LibraryMarked;
    char                                      LibraryQuery[64] = {};
    size_t                                    LibraryCards  = 0;
    std::string                               View          = "perspective";
    bool                                      LibraryShown  = false;
    bool                                      LoggedDevice  = false;
    bool                                      Opened        = false;   // [-] the camera has been set to the prototype's opening view
    int                                       DragMode      = 0;       // [-] 0 none, 1 orbit, 2 pan, 3 source brush, 4 place burst
    std::array<float, 3>                      DragHit{};
    bool                                      DragHeld      = false;   // [-] DragHit is a point of the volume (the ray met it)
    double                                    ElapsedSeconds = 0.0;    // [s] interface time since the last rate figure
    uint32_t                                  FramesCounted = 0;       // [-] refreshes since the last rate figure
    int                                       FramesPerSecond = 0;     // [-] the prototype's FPS readout, over half a second
    ImVec2                                    ViewportMinimum{};
    ImVec2                                    ViewportMaximum{};
};

} // namespace Frontier

#endif
