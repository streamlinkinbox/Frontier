//============================================================================================================================================
//                                                      EDITORHOST.CPP
//============================================================================================================================================
// 🧩 Development editor host — seats the theme, builds the dock columns, and records the three panels over the
//    project's record feed.

#include "EditorHost.h"
#include "EditorStyleSpecification.h"
#include "SunInspectorPanel.h"
#include "../DisplayPresentation/IconPresentation.h"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder*: the first-seat columns are built, not dragged

#include "../DisplayPresentation/FidelityClassifier.h"
#include "../ContentInterchange/AssetResolution.h"   // ResolveAssetText: faces come from the content folder

#include <algorithm>
#include <cstdio>
#include <string>

namespace Frontier {

//============================================================================================================================================
//                                                         WIRING
//============================================================================================================================================

EditorHost::EditorHost() noexcept
{
    Outliner_.AssignControls(&Controls_);
    Viewport_.AssignControls(&Controls_);
    Inspector_.AssignControls(&Controls_);
    Viewport_.AssignShadeOpen(&ShadeOpen_);
    Outliner_.AssignTabOpen(&OutlinerTabOpen_);
    Viewport_.AssignTabOpen(&ViewportTabOpen_);
    Inspector_.AssignTabOpen(&InspectorTabOpen_);

    // Project-Zero owns the game/editor filter catalogue explicitly; tools such as SolidArc seat their own.
    const OutlinerFilterEntry GameFilters[] =
    {
        { "Lights",   IM_COL32(0xFF, 0xB4, 0x54, 255), 1u << static_cast<uint32_t>(EditorNarrowing::Lights) },
        { "Sky",      IM_COL32(0x5A, 0xA9, 0xFF, 255), 1u << static_cast<uint32_t>(EditorNarrowing::Sky) },
        { "Bodies",   IM_COL32(0xDF, 0xE6, 0xF5, 255), 1u << static_cast<uint32_t>(EditorNarrowing::Bodies) },
        { "Geometry", IM_COL32(0xE2, 0xE8, 0xF0, 255), 1u << static_cast<uint32_t>(EditorNarrowing::Geometry) },
        { "Camera",   IM_COL32(0x34, 0xC7, 0x59, 255), 1u << static_cast<uint32_t>(EditorNarrowing::Camera) },
    };
    Outliner_.AssignFilterCatalog(GameFilters, static_cast<uint32_t>(sizeof(GameFilters) / sizeof(GameFilters[0])));
}

EditorHost::~EditorHost() noexcept
{
    Shade_.Terminate();
}

uint32_t EditorHost::QueryPickedInstance() const noexcept
{
    return Outliner_.QueryPicked();
}

int EditorHost::QueryFontCount() const noexcept
{
    return FontCount_;
}

void EditorHost::PickInstance(uint32_t Index) noexcept
{
    Outliner_.PickInstance(Index);
}

float EditorHost::QueryTabAddX() const noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    const ImGuiDockNode* Found = ImGui::DockBuilderGetNode(LeftColumn_);
    if (Found != nullptr)
        return (Found->FrontierAddRect.Min.x + Found->FrontierAddRect.Max.x) * 0.5f;
    return -1.0f;
#else
    return -1.0f;
#endif
}

float EditorHost::QueryTabAddY() const noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    const ImGuiDockNode* Found = ImGui::DockBuilderGetNode(LeftColumn_);
    if (Found != nullptr)
        return (Found->FrontierAddRect.Min.y + Found->FrontierAddRect.Max.y) * 0.5f;
    return -1.0f;
#else
    return -1.0f;
#endif
}

bool EditorHost::QueryTabOpen(uint32_t Tab) const noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    if (Tab == 0u)
        return OutlinerTabOpen_;
    if (Tab == 1u)
        return ViewportTabOpen_;
    if (Tab == 2u)
        return InspectorTabOpen_;
    return false;
#else
    (void)Tab;
    return false;
#endif
}

void EditorHost::RecordTabAdd() noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    if(ImGui::DockNodeConsumeAddRequest(LeftColumn_))Construct_.Open();
    // Ctrl+A belongs to Construct only outside text editing and camera steering.
    // Shift+WASD remains fast flight; holding A must not reopen the dialog.
    const ImGuiIO& Io=ImGui::GetIO();
    if(!Steering_ && !Io.WantTextInput && Io.KeyCtrl && !Io.KeyShift && !Io.KeyAlt && !Io.KeySuper
       && ImGui::IsKeyPressed(ImGuiKey_A,false))Construct_.Open();
#endif
}

void EditorHost::SeatViewportOrbit(const ViewportOrbit& Seated) noexcept
{
    Viewport_.SeatViewportOrbit(Seated);
}

const ViewportOrbit& EditorHost::QueryViewportOrbit() const noexcept
{
    return Viewport_.QueryViewportOrbit();
}

void EditorHost::AssignView(const unsigned char* Rgba, uint32_t Width, uint32_t Height) noexcept
{
    Viewport_.AssignView(Rgba, Width, Height);
}

void EditorHost::AssignViewTexture(ImTextureID View, uint32_t Width, uint32_t Height, uint32_t StorageWidth, uint32_t StorageHeight) noexcept
{
    Viewport_.AssignViewTexture(View, Width, Height, StorageWidth, StorageHeight);
}

void EditorHost::AssignReadout(const EditorReadout* Readout) noexcept
{
    Outliner_.AssignReadout(Readout);
    Viewport_.AssignReadout(Readout);
    Inspector_.AssignReadout(Readout);
}

uint32_t EditorHost::QueryOrderRevision() const noexcept
{
    return Outliner_.QueryOrderRevision();
}

float EditorHost::QueryViewWidth() const noexcept
{
    return Viewport_.QueryViewWidth();
}

float EditorHost::QueryViewHeight() const noexcept
{
    return Viewport_.QueryViewHeight();
}

bool EditorHost::SeatShade(uint32_t Width, uint32_t Height) noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    ShadeSeated_ = Shade_.Initialize(Width, Height);
    // The editor's pull is narrow: full width would sit on the viewport tab's close mark.
    Shade_.AssignNotchWidth(200.0f);
    return ShadeSeated_;
#else
    (void)Width; (void)Height;
    return false;
#endif
}

void EditorHost::TickShade(float CursorX, float CursorY, bool Down, float Wheel, float DeltaSeconds) noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    if (!ShadeSeated_)
        return;
    // The host runs in logical pixels; the contact arrives in display pixels, so the exchange carries the
    //    contact scaled while the advance takes it logical — the Rig's mapping, kept exact under UI scale.
    const float Scale = std::clamp(Shade_.QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.5f, 2.0f);
    const ImVec2 Display = ImGui::GetIO().DisplaySize;
    Shade_.Resize(static_cast<uint32_t>(Display.x / Scale + 0.5f),
                  static_cast<uint32_t>(Display.y / Scale + 0.5f));
    ShadeInput_.AssignCursorPosition(CursorX * Scale, CursorY * Scale);
    ShadeInput_.AssignMouseButton(MouseButtonCategory::ButtonLeft, Down);
    ShadeInput_.ResetMouseScroll();
    if (Wheel != 0.0f)
        ShadeInput_.AssignMouseScroll(Wheel);
    Shade_.AdvanceInteraction(ShadeInput_, CursorX, CursorY);
    Shade_.AdvanceLocomotion(DeltaSeconds);
    Toasts_.Advance(DeltaSeconds);
    AdvanceShadeTelemetry(Telemetry_, DeltaSeconds);

    // The gear toggles the shared figure; only an edge past the echo moves the shade, so the publish
    //    below never fights a tap or a scrim press that already seated the pose.
    if (!Shade_.IsDragging() && ShadeOpen_ != OpenEcho_)
    {
        if (ShadeOpen_)
            Shade_.OpenNotch();
        else
            Shade_.CloseNotch();
        OpenEcho_ = ShadeOpen_;
    }
    ShadeOpen_ = Shade_.IsOpen();
    OpenEcho_  = ShadeOpen_;

    // A settings change raises the dashboard's toast, as the Rig and the game both do.
    const ControlCentreSettings& Current = Shade_.QuerySettings();
    if (Current.Revision != ToastRevision_)
    {
        Toasts_.AssignEnabled(Current.Notifications);
        char Body[96];
        std::snprintf(Body, sizeof(Body), "%s  |  GI %s, AA %s, scale %d%%", FidelityLabel(Current.Quality),
                      Current.GlobalIllumination ? "on" : "off", Current.AntiAliasing ? "on" : "off",
                      static_cast<int>(Current.RenderScale * 100.0f + 0.5f));
        Toasts_.Push("Render settings applied", Body);
        ToastRevision_ = Current.Revision;
    }
#else
    (void)CursorX; (void)CursorY; (void)Down; (void)Wheel; (void)DeltaSeconds;
#endif
}

bool EditorHost::ShadeCoversPointer() const noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    return ShadeSeated_ && Shade_.CoversPointer();
#else
    return false;
#endif
}

bool EditorHost::QueryGiEnabled() const noexcept
{
    return Shade_.QuerySettings().GlobalIllumination;
}

FidelityCriteria EditorHost::QueryShadeCriteria() const noexcept
{
    return Shade_.QueryEffectiveCriteria();
}

float EditorHost::QueryRenderScale() const noexcept
{
    return Shade_.QuerySettings().RenderScale;
}

uint32_t EditorHost::QueryRevision() const noexcept
{
    return Shade_.QuerySettings().Revision;
}

const ControlCentreSettings& EditorHost::QueryShadeSettings() const noexcept
{
    return Shade_.QuerySettings();
}

void EditorHost::AssignProjectName(const char* Name) noexcept
{
    Shade_.AssignProjectName(Name != nullptr ? Name : "");
}

bool EditorHost::QueryShadeOpen() const noexcept
{
    return ShadeSeated_ && Shade_.IsOpen();
}

uint32_t EditorHost::QueryShadePage() const noexcept
{
    return static_cast<uint32_t>(Shade_.QueryActivePage());
}

float EditorHost::QueryGiTileX() const noexcept
{
    const PlaneExtent Disc = Shade_.QueryTileDiscExtent(0u);
    return (Disc.MinimumX + Disc.MaximumX) * 0.5f;
}

float EditorHost::QueryGiTileY() const noexcept
{
    const PlaneExtent Disc = Shade_.QueryTileDiscExtent(0u);
    return (Disc.MinimumY + Disc.MaximumY) * 0.5f;
}

float EditorHost::QueryPillX0() const noexcept
{
    return Shade_.QueryPillTrackExtent().MinimumX;
}

float EditorHost::QueryPillX1() const noexcept
{
    return Shade_.QueryPillTrackExtent().MaximumX;
}

float EditorHost::QueryPillY() const noexcept
{
    const PlaneExtent Track = Shade_.QueryPillTrackExtent();
    return (Track.MinimumY + Track.MaximumY) * 0.5f;
}

float EditorHost::QueryNotchX() const noexcept
{
    const PlaneExtent Grip = Shade_.QueryHandleExtent();
    return (Grip.MinimumX + Grip.MaximumX) * 0.5f;
}

float EditorHost::QueryNotchY() const noexcept
{
    const PlaneExtent Grip = Shade_.QueryHandleExtent();
    return (Grip.MinimumY + Grip.MaximumY) * 0.5f;
}

float EditorHost::QueryGripX() const noexcept
{
    const PlaneExtent Grip = Shade_.QueryGripExtent();
    return (Grip.MinimumX + Grip.MaximumX) * 0.5f;
}

float EditorHost::QueryGripY() const noexcept
{
    const PlaneExtent Grip = Shade_.QueryGripExtent();
    return (Grip.MinimumY + Grip.MaximumY) * 0.5f;
}

float EditorHost::QueryGearX() const noexcept
{
    const PlaneExtent Gear = Shade_.QueryHeaderGearExtent();
    return (Gear.MinimumX + Gear.MaximumX) * 0.5f;
}

float EditorHost::QueryGearY() const noexcept
{
    const PlaneExtent Gear = Shade_.QueryHeaderGearExtent();
    return (Gear.MinimumY + Gear.MaximumY) * 0.5f;
}

//============================================================================================================================================
//                                                       APPLY THEME
//============================================================================================================================================

void EditorHost::ApplyTheme() noexcept
{
    PrepareSunInspectorFonts();
    IconPresentation::Attach();
#ifdef FRONTIER_DEVELOPMENT
    SeatEditorStyle(ImGui::GetStyle());

    // Faces. Every face comes out of the content folder — `EngineContent/Fonts/` — and nowhere else, so the
    //    editor reads what is actually shipped rather than what a path string wishes were shipped. DM Sans
    //    carries the chrome: Light for the body and the title, Regular for the small label and the figures.
    //    📝 The content folder holds no monospaced face, so the figure columns are seated from DM Sans
    //    Regular. Figures there are proportional, so a changing number can shimmer by a fraction of a pixel;
    //    drop a mono face into `EngineContent/Fonts/` and point `FigureFacePath` at it to settle them.
    //    Paths run through `ResolveAssetText`, which walks up from the executable and the working directory
    //    to the nearest ancestor holding `EngineContent` — bare relative paths only worked when the host
    //    happened to be launched from the repository root. Each archive is still probed before it is read,
    //    so a missing archive falls back to the raster default instead of tripping an assert — and the proof
    //    gates the seated count, so the fallback never passes silently. The seated size is a base: the
    //    panels ask each face for the page's own pixel size at draw time.
    if (!FontsSeated_)
    {
        FontsSeated_ = true;
        ImGuiIO& IO = ImGui::GetIO();
        const std::string BodyFacePath    = ResolveAssetText("EngineContent/Fonts/SunReference/DMSans-Light.ttf");
        const std::string LabelFacePath   = ResolveAssetText("EngineContent/Fonts/SunReference/DMSans-Regular.ttf");
        const std::string FigureFacePath  = ResolveAssetText("EngineContent/Fonts/SunReference/DMSans-Regular.ttf");
        const std::string TitleFacePath   = ResolveAssetText("EngineContent/Fonts/SunReference/DMSans-Light.ttf");
        const std::string DisplayFacePath = ResolveAssetText("EngineContent/Fonts/SunReference/DMSans-Light.ttf");

        // The chrome faces carry the punctuation the panels speak: the middot, the degree sign, the
        //    multiplication sign, the em dash, the curly quotes, the ellipsis, and the command key.
        static const ImWchar SansRanges[] = { 0x0020, 0x00FF, 0x2013, 0x2014, 0x2018, 0x201E,
            0x2026, 0x2026, 0x2318, 0x2318, 0 };
        auto SeatFace = [&IO](const char* Path, float Size, const ImWchar* Ranges) -> ImFont*
        {
            std::FILE* Check = std::fopen(Path, "rb");
            if (Check == nullptr)
            {
                return nullptr;
            }
            std::fclose(Check);
            return IO.Fonts->AddFontFromFileTTF(Path, Size, nullptr, Ranges);
        };

        ImFont* Ui        = SeatFace(BodyFacePath.c_str(), 13.0f, SansRanges);
        ImFont* Small     = SeatFace(LabelFacePath.c_str(), 11.0f, SansRanges);
        ImFont* Mono      = SeatFace(FigureFacePath.c_str(), 13.0f, SansRanges);
        ImFont* MonoSmall = SeatFace(FigureFacePath.c_str(), 11.0f, SansRanges);
        ImFont* Title     = SeatFace(TitleFacePath.c_str(), 20.0f, SansRanges);
        ImFont* Display   = SeatFace(DisplayFacePath.c_str(), 30.0f, SansRanges);
        FontCount_ = (Ui != nullptr ? 1 : 0) + (Small != nullptr ? 1 : 0)
                   + (Mono != nullptr ? 1 : 0) + (MonoSmall != nullptr ? 1 : 0);
        if (Ui != nullptr)
        {
            IO.FontDefault = Ui;
        }
        Controls_.AssignFonts(Ui, Small, Mono, MonoSmall, Title, Display);
    }
#else
    // Without the define the editor draws nothing: the dockspace stays, the panels stay away.
#endif
}

//============================================================================================================================================
//                                                     CONSTRUCT LAYOUT
//============================================================================================================================================

void EditorHost::ConstructLayout() noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    const ImGuiID DockId = ImGui::GetID("FrontierEditorDockSpace");

    // Seat once per run so stale .ini entries cannot keep the old stacked layout, while user drags still
    //    survive after this first canonical seating.
    if (LayoutSeated_)
        return;
    LayoutSeated_ = true;

    ImGuiViewport* Main = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(DockId);
    ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockId, Main->Size);

    // Three columns: outliner left, the viewport in its own centre docked window, inspector right. The centre
    //    window is opaque and docked, so the render target cannot sit underneath the editor as a fullscreen plate.
    ImGuiID Left = 0u, CentreAndRight = 0u, Centre = 0u, Right = 0u;
    const float LeftShare = Main->Size.x > 0.0f ? std::clamp(316.0f / Main->Size.x, 0.15f, 0.34f) : 0.25f;
    ImGui::DockBuilderSplitNode(DockId, ImGuiDir_Left, LeftShare, &Left, &CentreAndRight);
    if(InspectorWorkspace_){
        ImGui::DockBuilderDockWindow("Outliner",Left);ImGui::DockBuilderDockWindow("Inspector",CentreAndRight);
        LeftColumn_=Left;CentreColumn_=CentreAndRight;RightColumn_=CentreAndRight;ImGui::DockBuilderFinish(DockId);return;
    }
    const float RestWidth = std::max(1.0f, Main->Size.x * (1.0f - LeftShare));
    const float RightShare = std::clamp(340.0f / RestWidth, 0.18f, 0.38f);
    ImGui::DockBuilderSplitNode(CentreAndRight, ImGuiDir_Right, RightShare, &Right, &Centre);

    ImGui::DockBuilderDockWindow("Outliner", Left);
    ImGui::DockBuilderDockWindow("Viewport", Centre);
    ImGui::DockBuilderDockWindow("Inspector", Right);
    LeftColumn_ = Left;   // seated for the add control
    CentreColumn_ = Centre;
    RightColumn_ = Right;
    ImGui::DockBuilderFinish(DockId);
#endif
}

//============================================================================================================================================
//                                                          RECORD
//============================================================================================================================================

void EditorHost::Record(EditorInstance* Instances, uint32_t InstanceCount, EditorSheet* PickedSheet) noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    ImGuiViewport* Main = ImGui::GetMainViewport();
    // The columns run edge to edge; the shade's pull floats above the tab band and the sheet slides
    //    over the columns from the top edge.
    ImGui::SetNextWindowPos(ImVec2(Main->Pos.x, Main->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(Main->Size.x, Main->Size.y));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    constexpr ImGuiWindowFlags Bare = ImGuiWindowFlags_NoTitleBar
                                    | ImGuiWindowFlags_NoResize
                                    | ImGuiWindowFlags_NoMove
                                    | ImGuiWindowFlags_NoScrollbar
                                    | ImGuiWindowFlags_NoScrollWithMouse
                                    | ImGuiWindowFlags_NoSavedSettings
                                    | ImGuiWindowFlags_NoBringToFrontOnFocus
                                    | ImGuiWindowFlags_NoNavFocus;

    if (ImGui::Begin("FrontierDockHost", nullptr, Bare))
    {
        // The viewport now lives in its own docked window. Do not use a passthrough centre: the swapchain render
        //    target must not remain visible as a fullscreen plate under the editor columns.
        const ImGuiDockNodeFlags NodeFlags =
            static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton);

        ConstructLayout();
        // The add control belongs to the left column alone; seated every tick so a rebuilt column keeps it.
        ImGui::DockNodeSetAddButton(LeftColumn_, true);
        ImGui::DockSpace(ImGui::GetID("FrontierEditorDockSpace"), ImVec2(0.0f, 0.0f), NodeFlags);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    Inspector_.AssignRoster(Instances, InstanceCount);
    Outliner_.Record(Instances, InstanceCount);
    if(!InspectorWorkspace_) {
        const uint32_t Before=Outliner_.QueryPicked();
        Viewport_.Billboards.Select(Before<InstanceCount?Instances[Before].InspectorKey:0);
        Viewport_.Record(Instances, InstanceCount);
        if(const uint64_t Key=Viewport_.Billboards.TakePick()) {
            for(uint32_t I=0;I<InstanceCount;++I)if(Instances[I].InspectorKey==Key) {
                PickInstance(I);BillboardSelection_=true;break;
            }
        }
    }

    const uint32_t Picked = Outliner_.QueryPicked();
    EditorInstance* PickedInstance = (Picked < InstanceCount) ? &Instances[Picked] : nullptr;
    if(!Construct_.ShowingProperties()) {
        EditorSheet* Current=InspectorExchange_?InspectorExchange_(Picked,false,InspectorContext_):PickedSheet;
        Inspector_.Record(PickedInstance, Picked, (PickedInstance != nullptr) ? Current : nullptr);
        if(InspectorExchange_)InspectorExchange_(Picked,true,InspectorContext_);
    } else {
        if(ImGui::Begin("Inspector",&InspectorTabOpen_))ImGui::TextDisabled("Editing entity properties in Construct");
        ImGui::End();
    }
    if (!OutlinerRaised_)
    {
        // The sheet opens on the outliner: raise its tab once, then leave the choice to the user.
        ImGui::SetWindowFocus("Outliner");
        OutlinerRaised_ = true;
    }
    const uint32_t CollectionPick=Inspector_.ConsumeCollectionPick();
    if(CollectionPick<InstanceCount)Outliner_.RevealInstance(CollectionPick,Instances,InstanceCount);
    RecordTabAdd();
    const uint32_t ConstructPick=Construct_.Record(Instances,InstanceCount,Inspector_,InspectorExchange_,InspectorContext_);
    if(ConstructPick<InstanceCount)PickInstance(ConstructPick);

    // The shade records last, above the dock columns: the FPS readout, the shade itself, and the
    //    toasts, all onto the foreground list — the Rig's order, kept.
    if (ShadeSeated_)
    {
        const float UiScale = std::clamp(Shade_.QueryAppearance().QueryApplied().InterfaceScale / 100.0f,
                                         0.5f, 2.0f);
        if (ShadeSurface_.Begin(SurfaceLayer::Above, Main->Size.x, Main->Size.y, UiScale))
        {
            // No strip anymore: the readout and the toasts clear the tab band, whose height the style
            //    carries (PatchB's two figures).
            const float BandLine = ImGui::GetStyle().TabHeight + ImGui::GetStyle().TabStripPadTop;
            if (Shade_.QuerySettings().FrameRateOverlay)
                Telemetry_.ConstructTelemetryLayout(ShadeSurface_, BandLine);
            Shade_.ConstructControlLayout(ShadeSurface_);
            Toasts_.ConstructNotificationLayout(ShadeSurface_, BandLine);
        }
    }
#else
    (void)Instances; (void)InstanceCount; (void)PickedSheet;
#endif
}

} // namespace Frontier
