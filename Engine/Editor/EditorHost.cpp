//============================================================================================================================================
//                                                      EDITORHOST.CPP
//============================================================================================================================================
// 🧩 Development editor host — seats the theme, builds the dock columns, and records the three panels over the
//    project's record feed.

#include "EditorHost.h"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder*: the first-seat columns are built, not dragged

#include "../DisplayPresentation/FidelityClassifier.h"

#include <algorithm>
#include <cstdio>

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
    // The left column's add control reports through the vendor's poll; a press raises the menu, whose
    //    rows seat each tab open or shut. The menu is the only way back for a tab its close mark shut.
    if (ImGui::DockNodeConsumeAddRequest(LeftColumn_))
        ImGui::OpenPopup("##tabmenu");
    if (ImGui::BeginPopup("##tabmenu"))
    {
        ImGui::MenuItem("Outliner", nullptr, &OutlinerTabOpen_);
        ImGui::MenuItem("Viewport", nullptr, &ViewportTabOpen_);
        ImGui::MenuItem("Inspector", nullptr, &InspectorTabOpen_);
        ImGui::EndPopup();
    }
#else
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

void EditorHost::AssignViewTexture(ImTextureID View, uint32_t Width, uint32_t Height) noexcept
{
    Viewport_.AssignViewTexture(View, Width, Height);
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
#ifdef FRONTIER_DEVELOPMENT
    ImGuiStyle& Applied = ImGui::GetStyle();

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

    // Faces. Outfit carries the chrome — the celestial page's own face, its 300 body weight, 300 title and
    //    200 census numerals — and JetBrains Mono the figures, as the page's --mono. Each archive is probed
    //    before it is read, so a missing archive falls back to the raster default instead of tripping an
    //    assert — and the proof gates the seated count, so the fallback never passes silently. The seated
    //    size is a base: the panels ask each face for the page's own pixel size at draw time.
    if (!FontsSeated_)
    {
        FontsSeated_ = true;
        ImGuiIO& IO = ImGui::GetIO();
        const char* SansFaces    = "EngineContent/FontArchives/Outfit/Outfit-Light.ttf";
        const char* SmallFaces   = "EngineContent/FontArchives/Outfit/Outfit-Regular.ttf";
        const char* MonoFaces    = "EngineContent/FontArchives/JetBrainsMono/JetBrainsMono-Regular.ttf";
        const char* TitleFaces   = "EngineContent/FontArchives/Outfit/Outfit-Light.ttf";
        const char* DisplayFaces = "EngineContent/FontArchives/Outfit/Outfit-ExtraLight.ttf";

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

        ImFont* Ui        = SeatFace(SansFaces, 13.0f, SansRanges);
        ImFont* Small     = SeatFace(SmallFaces, 11.0f, SansRanges);
        ImFont* Mono      = SeatFace(MonoFaces, 13.0f, SansRanges);
        ImFont* MonoSmall = SeatFace(MonoFaces, 11.0f, SansRanges);
        ImFont* Title     = SeatFace(TitleFaces, 20.0f, SansRanges);
        ImFont* Display   = SeatFace(DisplayFaces, 30.0f, SansRanges);
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
    const ImGuiID DockId = ImGui::GetID("FrontierDockSpace");

    // Once the columns exist this rests, so a dragged rearrangement survives; with no .ini (the headless
    //    proof) the same seating rebuilds every run.
    if (ImGui::DockBuilderGetNode(DockId) != nullptr)
        return;

    ImGuiViewport* Main = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(DockId);
    ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockId, Main->Size);

    // Two columns: the outliner at the page's 316 px on the left, the viewport filling the rest. The inspector
    //    tabs behind the outliner — it is not one of the two panels the sheet opens on, but it stays a drag away.
    ImGuiID Left = 0u, Centre = 0u;
    const float LeftShare = Main->Size.x > 0.0f ? std::clamp(316.0f / Main->Size.x, 0.15f, 0.45f) : 0.25f;
    ImGui::DockBuilderSplitNode(DockId, ImGuiDir_Left, LeftShare, &Left, &Centre);

    ImGui::DockBuilderDockWindow("Outliner", Left);
    ImGui::DockBuilderDockWindow("Inspector", Left);
    ImGui::DockBuilderDockWindow("Viewport", Centre);
    LeftColumn_ = Left;   // seated for the add control
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
                                    | ImGuiWindowFlags_NoNavFocus
                                    | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("FrontierDockHost", nullptr, Bare))
    {
        // 🔴 PassthruCentralNode so the render shows through where nothing is docked — without it the vendor
        //    fills the whole column with its own colour. The silencer removes the caret the sheet has none
        //    of; the close marks stay, one per tab, and the add control closes the left column's strip.
        //    Docking over the centre stays allowed: the central column HOLDS the Viewport tab, so covering
        //    it means tabbing with the view, not losing it.
        const ImGuiDockNodeFlags NodeFlags =
              static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_PassthruCentralNode)
            | static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton);

        ConstructLayout();
        // The add control belongs to the left column alone; seated every tick so a rebuilt column keeps it.
        ImGui::DockNodeSetAddButton(LeftColumn_, true);
        ImGui::DockSpace(ImGui::GetID("FrontierDockSpace"), ImVec2(0.0f, 0.0f), NodeFlags);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    Outliner_.Record(Instances, InstanceCount);
    Viewport_.Record(Instances, InstanceCount);

    const uint32_t Picked = Outliner_.QueryPicked();
    EditorInstance* PickedInstance = (Picked < InstanceCount) ? &Instances[Picked] : nullptr;
    Inspector_.Record(PickedInstance, Picked, (PickedInstance != nullptr) ? PickedSheet : nullptr);
    if (!OutlinerRaised_)
    {
        // The sheet opens on the outliner: raise its tab once, then leave the choice to the user.
        ImGui::SetWindowFocus("Outliner");
        OutlinerRaised_ = true;
    }
    RecordTabAdd();

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
