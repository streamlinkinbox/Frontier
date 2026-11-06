/*==========================================================================================================================================
                                                    FLUIDPLUMEEDITORWORKSPACE.H
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor as the Particle editor's window lays an editor out: the Outliner (the record's parts), the viewport (the plume's GPU
//    image, its HUD and its bar), the Log Stream, the Inspector (the selected part's page) and the libraries (the looks and the grades).

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/FluidPlumeEditor/FluidPlumeLibrary.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumePages.h"
#include "Engine/Interface/FluidPlumeEditor/PlumeViewportPresentation.h"

#include <imgui.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

struct FluidPlumeLogLine
{
    std::string Time;
    std::string Category;
    std::string Text;
    std::string Detail;
    bool        Warm = false;
};

// 📝 Where a control of the window lay at its last layout, in screen pixels: what a capture or a validation needs to press it with the pointer.
struct FluidPlumeControlRectangle
{
    std::string Name;   // "row:Fire", "eye:Domain", "library", "library-card:inferno", "library-tab:grades", "pause", "emitter:2", …
    ImVec2      Minimum{};
    ImVec2      Maximum{};
};

class FluidPlumeEditorWorkspace
{
  public:
    FluidPlumeEditorWorkspace();

    // 📝 The record the window edits and the name the Outliner shows for it; the window attaches both on every refresh it is open.
    void AttachRecord(FluidPlumeConfiguration& InRecord, std::string InName);
    [[nodiscard]] FluidPlumeConfiguration* QueryRecord() const noexcept { return Record; }
    // 📝 The window closes: the record is let go, and the test emitter returns to what it was when the window opened (it is the editor's own).
    void DetachRecord() noexcept;

    // 📝 One interface tick: advances the stage clock by DeltaSeconds, asks the presenter for the image and lays the editor out in the rectangle
    //    (the editor window's body).
    void Construct(ImVec2 Minimum, ImVec2 Maximum, float Scale, double DeltaSeconds);

    // 📝 The verbs the pages, the keys and the captures use: "select:<token>" (Domain, Sources, Emitter, Simulation, Appearance, Fire),
    //    "look:<identifier>", "defaults", "enabled:on|off|toggle", "emitter:off|smoke|fire|toggle", "clear-smoke", "pause", "play", "library:on|off",
    //    "library-tab:looks|grades", "library-group:<group>", "library-search:<text>", "camera:<yaw>,<pitch>,<distance>", "close".
    bool Perform(std::string_view Action);

    // 📝 A host that runs the plume without the presenter reports it here; QueryReadout is the presenter's when it has one.
    void AssignReadout(const FluidPlumeReadout& Readout);
    void RevokeReadout() noexcept;
    [[nodiscard]] const FluidPlumeReadout* QueryReadout() const noexcept;
    // 📝 The verbs only a host without the presenter can carry out ("clear-smoke"); the call takes them.
    [[nodiscard]] std::vector<std::string> TakeHostRequests();

    [[nodiscard]] const std::string&                QuerySelected() const noexcept { return Selected; }
    [[nodiscard]] bool                              QueryLibraryShown() const noexcept { return LibraryShown; }
    [[nodiscard]] const std::string&                QueryLibraryTab() const noexcept { return LibraryTab; }
    [[nodiscard]] const std::string&                QueryLibraryGroup() const noexcept { return LibraryGroup; }
    [[nodiscard]] size_t                            QueryLibraryCards() const noexcept { return LibraryCards; }
    // 📝 The card the sheet marks: the look the readings hold ("Applied") or the grade the solver is built at ("In use"); empty when none is.
    [[nodiscard]] const std::string&                QueryLibraryMarked() const noexcept { return LibraryMarked; }
    [[nodiscard]] bool                              QueryPlaying() const noexcept { return Playing; }
    [[nodiscard]] const PlumeViewportImage&         QueryImage() const noexcept { return Image; }
    [[nodiscard]] const PlumeViewportRequest&       QueryLastRequest() const noexcept { return LastRequest; }
    [[nodiscard]] const std::vector<FluidPlumeLogLine>& QueryLog() const noexcept { return Log; }
    [[nodiscard]] const std::vector<FluidPlumeControlRectangle>& QueryControls() const noexcept { return Controls; }
    [[nodiscard]] const FluidPlumeControlRectangle* FindControl(std::string_view Name) const noexcept;
    [[nodiscard]] std::array<ImVec2, 2>             QueryStage() const noexcept { return { StageMinimum, StageMaximum }; }
    [[nodiscard]] std::array<double, 3>             QueryCamera() const noexcept { return { Yaw, Pitch, Distance }; }

    PlumeViewportPresenter Presenter;

  private:
    void ConstructOutliner(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void ConstructViewport(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void ConstructLogStream(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void ConstructInspector(ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void ConstructLibrarySheet(ImVec2 Minimum, ImVec2 Maximum, float Scale);
    void Present(uint32_t Width, uint32_t Height, double DeltaSeconds);
    void AppendLog(std::string Category, std::string Text, std::string Detail = {}, bool Warm = false);
    // 📝 An invisible button over a rectangle that remembers where it lay; true when it was pressed.
    bool Press(const char* Name, ImVec2 Minimum, ImVec2 Size);
    [[nodiscard]] const FluidPlumeReadout* ResolveReadout() const noexcept;

    FluidPlumeConfiguration*                  Record = nullptr;
    std::string                               RecordName;
    std::string                               Selected = "Domain";
    std::vector<std::string>                  Requests;
    std::vector<std::string>                  HostRequests;
    std::vector<FluidPlumeLogLine>            Log;
    std::vector<FluidPlumeControlRectangle>   Controls;
    std::optional<FluidPlumeReadout>          Assigned;
    PlumeViewportImage                        Image;
    PlumeViewportRequest                      LastRequest;
    std::string                               LibraryTab   = "looks";
    std::string                               LibraryGroup = "All";
    std::string                               LibraryMarked;
    char                                      LibraryQuery[64] = {};
    size_t                                    LibraryCards = 0;
    uint32_t                                  OpenedEmitter = 0;    // [-] the record's test emitter when the window opened
    bool                                      LibraryShown = false;
    bool                                      Playing      = true;
    bool                                      ClearPending = false;
    bool                                      LoggedDevice = false;
    double                                    StageSeconds = 0.0;   // [s] interface time the stage has run
    double                                    RefreshRate  = 60.0;  // [Hz] the interface's refresh rate, smoothed
    double                                    Yaw          = 0.30;  // [rad]
    double                                    Pitch        = 0.22;  // [rad]
    double                                    Distance     = 11.0;  // [m]
    ImVec2                                    StageMinimum{};
    ImVec2                                    StageMaximum{};
};

} // namespace Frontier

#endif
