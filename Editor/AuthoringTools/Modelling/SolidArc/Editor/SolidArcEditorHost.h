//=============================================================================================================================================
// SolidArcEditorHost.h
//=============================================================================================================================================
// ImGui editor shell for SolidArc. It seats the same Frontier outliner and viewport panels used by Project-Zero,
// with SolidArc-specific row mapping supplied by SolidArcOutlinerAdapter. The tab sheet is the game editor's own
// (EditorStyleSpecification.h) and the top Control Centre notch is the same ControlCentreHost Project-Zero pulls.

#pragma once

#include "SolidArcOutlinerAdapter.h"
#include "../../../../../Engine/Editor/ControlPanel.h"
#include "../../../../../Engine/Editor/InspectorPanel.h"
#include "../../../../../Engine/Editor/OutlinerPanel.h"
#include "../../../../../Engine/Editor/ViewportPanel.h"
#include "../../../../../Engine/DisplayPresentation/ControlCentreHost.h"
#include "../../../../../Engine/DisplayPresentation/NotificationQueue.h"
#include "../../../../../Engine/DisplayPresentation/PixelSpace.h"
#include "../../../../../Engine/DeviceExchange/InputExchange.h"

#include <cstdint>
#include <string>

namespace Frontier {

class SolidArcEditorHost
{
public:
    SolidArcEditorHost() noexcept;

    ~SolidArcEditorHost() noexcept;

    SolidArcEditorHost(const SolidArcEditorHost&)            = delete;
    SolidArcEditorHost& operator=(const SolidArcEditorHost&) = delete;

    /// 📦 Seats the game editor's tab sheet and tokens, and the faces. Idempotent; call once the ImGui context exists.
    void ApplyTheme() noexcept;

    /// 📦 Opens the Control Centre notch on a Width x Height display. TickShade and the notch half of Record rest until
    ///    this returns true.
    bool SeatShade(uint32_t Width, uint32_t Height) noexcept;

    /// 📦 Call every tick before ImGui::NewFrame(): hands the pointer contact and the frame interval to the notch.
    void TickShade(float CursorX, float CursorY, bool Down, float Wheel, float DeltaSeconds) noexcept;

    /// 📦 True while the notch or its sheet owns the pointer; the dock columns below must not see the contact.
    [[nodiscard]] bool ShadeCoversPointer() const noexcept;
    [[nodiscard]] bool QueryShadeOpen() const noexcept;
    [[nodiscard]] float QueryNotchX() const noexcept;
    [[nodiscard]] float QueryNotchY() const noexcept;
    [[nodiscard]] float QueryGripX() const noexcept;
    [[nodiscard]] float QueryGripY() const noexcept;
    void OpenShade() noexcept  { ShadeOpen_ = true; }
    void CloseShade() noexcept { ShadeOpen_ = false; }

    void Record(ConsoleHost& Host) noexcept;
    void ShowDocumentCommands() noexcept { DocumentCommands_ = true; LayoutSeated_ = false; }

    [[nodiscard]] const RasterImage& QueryPresentedImage() const noexcept { return ViewImage_; }
    [[nodiscard]] uint64_t QueryPresentedRevision() const noexcept { return ViewRevision_; }
    void SetPreviewSamples(uint32_t Samples) noexcept { PreviewSamples_ = Samples == 2u ? 2u : 1u; }
    [[nodiscard]] uint32_t QueryPickedFigureIdentity() const noexcept;

    /// 📦 The proof's pick seam: seats the pick on the first row of the role (named Label when it is not null); false when none.
    bool PickRow(const char* Label, SolidArcOutlinerBinding::Role Role) noexcept;
    void ClearPick() noexcept;
    /// 📦 The proof's Ctrl+click on an outliner row: adds the first row of the role (named Label when it is not null) to the pick, or takes it out.
    bool ExtendRow(const char* Label, SolidArcOutlinerBinding::Role Role) noexcept;
    [[nodiscard]] uint32_t QueryPickedCount() const noexcept { return Outliner_.QueryPickedCount(); }
    void ReseatLayout() noexcept { LayoutSeated_ = false; }     // the next Record re-splits the dock for the current window
    [[nodiscard]] float    QueryViewOriginX() const noexcept { return Viewport_.QueryViewOriginX(); }
    [[nodiscard]] float    QueryViewOriginY() const noexcept { return Viewport_.QueryViewOriginY(); }
    [[nodiscard]] float    QueryViewWidth() const noexcept { return Viewport_.QueryViewWidth(); }
    [[nodiscard]] float    QueryViewHeight() const noexcept { return Viewport_.QueryViewHeight(); }
    [[nodiscard]] uint32_t QueryRailSelectMask() const noexcept { return Viewport_.QuerySolidArcSelectMask(); }
    [[nodiscard]] uint32_t QueryRailShade() const noexcept { return Viewport_.QuerySolidArcShade(); }
    [[nodiscard]] uint32_t QueryRailGizmo() const noexcept { return Viewport_.QuerySolidArcGizmo(); }

    /// 📦 Places one Construct tile's figure exactly as a click on it does; true when a figure was added. The proof's
    ///    seam: the menu click calls the same path.
    bool PlaceConstruct(ConsoleHost& Host, uint32_t Tile, double X, double Y) noexcept;
    [[nodiscard]] static uint32_t QueryConstructTileCount() noexcept;
    [[nodiscard]] const std::string& QueryConstructPlacedName() const noexcept { return ConstructPlacedName_; }
    [[nodiscard]] bool QueryConstructOpen() const noexcept { return Viewport_.QueryConstructOpen(); }
    [[nodiscard]] bool QueryConstructTileCentre(uint32_t Tile, float* X, float* Y) const noexcept { return Viewport_.QueryConstructTileCentre(Tile, X, Y); }
    [[nodiscard]] bool QueryConstructSectionCentre(uint32_t Section, float* X, float* Y) const noexcept { return Viewport_.QueryConstructSectionCentre(Section, X, Y); }

    /// 📦 The proof's seam for the selection the host keeps between the outliner, the view and the document.
    [[nodiscard]] uint32_t QuerySelectedCount() const noexcept { return static_cast<uint32_t>(MirrorSelected_.size()); }

private:
    void ConstructLayout() noexcept;
    void SeatView(ConsoleHost& Host) noexcept;
    void ReconcileSelection(ConsoleHost& Host) noexcept;
    void AdvanceViewport(ConsoleHost& Host) noexcept;

    ControlPanel Controls_;
    OutlinerPanel Outliner_;
    ViewportPanel Viewport_;
    InspectorPanel Inspector_;

    ControlCentreHost Shade_;           // the original notch and sheet, last: it draws above the dock columns
    InputExchange     ShadeInput_;      // the contact TickShade hands it every tick
    PixelSpace        ShadeSurface_;    // the foreground list its primitives land on
    NotificationQueue Toasts_;          // the toasts a settings change raises
    bool     ShadeSeated_   = false;    // SeatShade has opened the host's springs
    bool     ShadeOpen_     = false;    // shut at boot; shared with the viewport gear
    bool     OpenEcho_      = false;    // the gear's last obeyed figure: only an edge moves the shade
    uint32_t ToastRevision_ = 0u;       // the settings revision the last toast answered

    bool OutlinerTabOpen_ = true;
    bool ViewportTabOpen_ = true;
    bool InspectorTabOpen_ = true;
    ImGuiID LeftColumn_   = 0u;
    ImGuiID CentreColumn_ = 0u;
    ImGuiID RightColumn_  = 0u;
    bool LayoutSeated_    = false;
    bool DocumentCommands_ = false;

    std::vector<EditorInstance> Rows_{kMaxEditorInstances};
    std::vector<SolidArcOutlinerBinding> Bindings_{kMaxEditorInstances};
    uint32_t RowCount_ = 0u;
    std::vector<std::string> ShutFolders_; // Folder collapse is host-owned: the roster is rebuilt from the document every frame.
    EditorReadout Readout_ = {};
    EditorSheet PickedSheet_ = {};
    RasterImage ViewImage_ = {};
    uint64_t    ViewRevision_ = 0u;
    uint32_t    PreviewSamples_ = 1u; // software AA is optional; default to responsive editing
    int32_t     ConstructTile_ = -1;         // -1 = selection; otherwise an armed construction tile
    bool        ConstructAnchorSet_ = false; // placement requires a deliberate viewport click
    bool        ConstructCursorValid_ = false;
    double      ConstructX_ = 0.0, ConstructY_ = 0.0;
    double      ConstructCursorX_ = 0.0, ConstructCursorY_ = 0.0;
    std::string ConstructPlacedName_;

    // One selection, three views of it: the outliner's pick, the document's selected figures, and the view's taps and boxes.
    //    The mirrors hold what each side last agreed on, so a change on any one side is the one to carry to the others.
    std::vector<uint32_t> MirrorPicked_;    // figure identities the outliner's pick held after the last reconcile
    std::vector<uint32_t> MirrorSelected_;  // figure identities the document held selected after the last reconcile
    uint32_t              Lead_ = 0u;       // the figure the inspector reads: the newest of the pick
    uint32_t              MirrorMask_ = 0u;
    uint32_t              MirrorGizmo_ = 3u;
    bool                  GizmoDragging_ = false; // the rail's select-mode bits the document last followed
    int32_t               AimCellX_ = -1;   // the view pixel the hover last asked about
    int32_t               AimCellY_ = -1;
};

} // namespace Frontier
