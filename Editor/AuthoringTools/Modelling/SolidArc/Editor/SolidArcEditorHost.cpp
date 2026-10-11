//=============================================================================================================================================
// SolidArcEditorHost.cpp
//=============================================================================================================================================

#include "SolidArcEditorHost.h"
#include "../../../../../Engine/Editor/EditorStyleSpecification.h"
#include "../../../../../Engine/Editor/ShadeTick.h"
#include "../../../../../Engine/Editor/SunInspectorPanel.h"
#include "../../../../../Engine/DisplayPresentation/FidelityClassifier.h"
#include "../../../../../Engine/DisplayPresentation/IconPresentation.h"
#include "../Interaction/SnapResolution.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace
{
// The Construct menu: every figure the console builds from numbers alone, each in the section the web catalogue files
//    it under. The order here is the order of ConstructCommand's cases.
constexpr ViewportConstructTile kConstructTiles[] =
{
    { "Reference",   "Plane",         ConstructGlyph::Plane,           ""  },
    { "Sketch Draw", "Line",          ConstructGlyph::Line,            "L" },
    { "Sketch Draw", "Polyline",      ConstructGlyph::Polyline,        ""  },
    { "Sketch Draw", "Rectangle",     ConstructGlyph::Rectangle,       "R" },
    { "Sketch Draw", "Centre Rect.",  ConstructGlyph::CentreRectangle, ""  },
    { "Sketch Draw", "Slot",          ConstructGlyph::Slot,            ""  },
    { "Sketch Draw", "Circle",        ConstructGlyph::Circle,          "C" },
    { "Sketch Draw", "Arc",           ConstructGlyph::Arc,             "A" },
    { "Sketch Draw", "Ellipse",       ConstructGlyph::Ellipse,         ""  },
    { "Sketch Draw", "Polygon",       ConstructGlyph::Polygon,         "P" },
    { "Sketch Draw", "Spline",        ConstructGlyph::Spline,          ""  },
    { "Sketch Draw", "Control Curve", ConstructGlyph::ControlCurve,    ""  },
    { "Solid",       "Box",           ConstructGlyph::Box,             ""  },
    { "Solid",       "Sphere",        ConstructGlyph::Sphere,          ""  },
    { "Solid",       "Cylinder",      ConstructGlyph::Cylinder,        ""  },
    { "Solid",       "Cone",          ConstructGlyph::Cone,            ""  },
    { "Solid",       "Torus",         ConstructGlyph::Torus,           ""  },
    { "Surface",     "Sphere Sheet",  ConstructGlyph::Sphere,          ""  },
    { "Surface",     "Cylinder Sheet", ConstructGlyph::Cylinder,       ""  },
    { "Surface",     "Cone Sheet",    ConstructGlyph::Cone,            ""  },
    { "Surface",     "Torus Sheet",   ConstructGlyph::Torus,           ""  },
    { "Surface",     "Patch",         ConstructGlyph::Patch,           ""  },
};
constexpr uint32_t kConstructTileCount = static_cast<uint32_t>(sizeof(kConstructTiles) / sizeof(kConstructTiles[0]));

// The console line that places tile Index with its centre at (X, Y) on the workplane: every size is a default a
//    person can see at once and move afterwards. An empty string means the tile does not exist.
std::string ConstructCommand(uint32_t Index, double X, double Y) noexcept
{
    char Line[640];
    switch (Index)
    {
    case 0:  std::snprintf(Line, sizeof(Line), "plane (%.3f,%.3f,0) 1.4 1.4", X - 0.7, Y - 0.7); break;
    case 1:  std::snprintf(Line, sizeof(Line), "line (%.3f,%.3f) (%.3f,%.3f)", X - 0.7, Y - 0.5, X + 0.7, Y + 0.5); break;
    case 2:  std::snprintf(Line, sizeof(Line), "polyline (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)",
                           X - 0.8, Y - 0.5, X - 0.3, Y + 0.5, X + 0.3, Y - 0.2, X + 0.8, Y + 0.5); break;
    case 3:  std::snprintf(Line, sizeof(Line), "rect (%.3f,%.3f) (%.3f,%.3f)", X - 0.7, Y - 0.5, X + 0.7, Y + 0.5); break;
    case 4:  std::snprintf(Line, sizeof(Line), "rect (%.3f,%.3f) (%.3f,%.3f) --center", X, Y, X + 0.7, Y + 0.5); break;
    case 5:  std::snprintf(Line, sizeof(Line), "slot (%.3f,%.3f) (%.3f,%.3f) 0.25", X - 0.5, Y, X + 0.5, Y); break;
    case 6:  std::snprintf(Line, sizeof(Line), "circle (%.3f,%.3f) 0.6", X, Y); break;
    case 7:  std::snprintf(Line, sizeof(Line), "arc (%.3f,%.3f) 0.7 -60 240", X, Y); break;
    case 8:  std::snprintf(Line, sizeof(Line), "ellipse (%.3f,%.3f) 0.8 0.45", X, Y); break;
    case 9: std::snprintf(Line, sizeof(Line), "polygon (%.3f,%.3f) 0.6 6", X, Y); break;
    case 10: std::snprintf(Line, sizeof(Line), "spline (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)",
                           X - 0.8, Y - 0.3, X - 0.3, Y + 0.5, X + 0.3, Y - 0.5, X + 0.8, Y + 0.3); break;
    case 11: std::snprintf(Line, sizeof(Line), "cpcurve (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)",
                           X - 0.8, Y - 0.3, X - 0.3, Y + 0.6, X + 0.3, Y - 0.6, X + 0.8, Y + 0.3); break;
    case 12: std::snprintf(Line, sizeof(Line), "box (%.3f,%.3f,0) 1 1 1", X - 0.5, Y - 0.5); break;
    case 13: std::snprintf(Line, sizeof(Line), "sphere (%.3f,%.3f,0.6) 0.6", X, Y); break;
    case 14: std::snprintf(Line, sizeof(Line), "cylinder (%.3f,%.3f,0) 0.45 1.2", X, Y); break;
    case 15: std::snprintf(Line, sizeof(Line), "cone (%.3f,%.3f,0) 0.6 0.15 1.2", X, Y); break;
    case 16: std::snprintf(Line, sizeof(Line), "torus (%.3f,%.3f,0.3) 0.6 0.25", X, Y); break;
    case 17: std::snprintf(Line, sizeof(Line), "sphere (%.3f,%.3f,0.6) 0.6 --sheet", X, Y); break;
    case 18: std::snprintf(Line, sizeof(Line), "cylinder (%.3f,%.3f,0) 0.45 1.2 --sheet", X, Y); break;
    case 19: std::snprintf(Line, sizeof(Line), "cone (%.3f,%.3f,0) 0.6 0.15 1.2 --sheet", X, Y); break;
    case 20: std::snprintf(Line, sizeof(Line), "torus (%.3f,%.3f,0.3) 0.6 0.25 --sheet", X, Y); break;
    case 21:
    {
        // A three-by-three net with a hump in the middle: row-major, degree 2.
        int Used = std::snprintf(Line, sizeof(Line), "patch 3 3 --degree=2");
        for (int Row = 0; Row < 3; ++Row)
            for (int Column = 0; Column < 3; ++Column)
                Used += std::snprintf(Line + Used, sizeof(Line) - static_cast<size_t>(Used), " (%.3f,%.3f,%.3f)",
                                      X + (Column - 1) * 0.7, Y + (Row - 1) * 0.7, (Row == 1 && Column == 1) ? 0.5 : 0.0);
        break;
    }
    default: return std::string();
    }
    return std::string(Line);
}
}

SolidArcEditorHost::SolidArcEditorHost() noexcept
{
    Outliner_.AssignControls(&Controls_);
    Viewport_.AssignControls(&Controls_);
    Inspector_.AssignControls(&Controls_);
    Outliner_.AssignTabOpen(&OutlinerTabOpen_);
    Viewport_.AssignTabOpen(&ViewportTabOpen_);
    Inspector_.AssignTabOpen(&InspectorTabOpen_);
    Outliner_.AssignWindowTitle("SolidArc Outliner");
    Viewport_.AssignWindowTitle("SolidArc Viewport");
    Inspector_.AssignWindowTitle("SolidArc Inspector");
    Inspector_.AssignGlassCards(true);
    Outliner_.AssignDocumentStyle(true);
    // SolidArc uses its CAD filter catalogue, not Project-Zero's game narrowing labels.
    // Colours match the web editor palette: curve/sketch #4fd8e0, body #ffb454,
    // surface #4da3ff, construction plane #b48cff, dimensions #e5d33a and constraints #ff6b8a.
    const OutlinerFilterEntry SolidArcFilters[] =
    {
        { "Lines",        IM_COL32(79, 216, 224, 255), SolidArcOutlinerFilter::Lines },
        { "Profiles",     IM_COL32(79, 216, 224, 255), SolidArcOutlinerFilter::Profiles },
        { "Bodies",       IM_COL32(255, 180, 84, 255), SolidArcOutlinerFilter::Bodies },
        { "Surfaces",     IM_COL32(77, 163, 255, 255), SolidArcOutlinerFilter::Surfaces },
        { "Construction", IM_COL32(180, 140, 255, 255), SolidArcOutlinerFilter::Construction },
        { "Dimensions",   IM_COL32(229, 211, 58, 255), SolidArcOutlinerFilter::Dimensions },
        { "Constraints",  IM_COL32(255, 107, 138, 255), SolidArcOutlinerFilter::Constraints },
    };
    Outliner_.AssignFilterCatalog(SolidArcFilters, static_cast<uint32_t>(sizeof(SolidArcFilters) / sizeof(SolidArcFilters[0])));
    Viewport_.AssignChrome(ViewportPanelChrome::SolidArcCad);
    Viewport_.AssignConstructTiles(kConstructTiles, kConstructTileCount);
    Outliner_.AssignReadout(&Readout_);
    Viewport_.AssignReadout(&Readout_);
    Inspector_.AssignReadout(&Readout_);
    Viewport_.AssignShadeOpen(&ShadeOpen_);
}

SolidArcEditorHost::~SolidArcEditorHost() noexcept
{
    Shade_.Terminate();
}

void SolidArcEditorHost::ApplyTheme() noexcept
{
    PrepareSunInspectorFonts();
    IconPresentation::Attach();
#ifdef FRONTIER_DEVELOPMENT
    // 📝 One style for both editors: tab figures, geometry and every colour token come from the game editor's own seating.
    SeatEditorStyle(ImGui::GetStyle());
#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTROL CENTRE NOTCH
//------------------------------------------------------------------------------------------------------------------------

bool SolidArcEditorHost::SeatShade(uint32_t Width, uint32_t Height) noexcept
{
    ShadeSeated_ = Shade_.Initialize(Width, Height);
    // 📝 The same 200 px pull the game editor seats: full width would sit on the viewport tab's close mark.
    Shade_.AssignNotchWidth(200.0f);
    Shade_.AssignProjectName("SolidArc");
    return ShadeSeated_;
}

void SolidArcEditorHost::TickShade(float CursorX, float CursorY, bool Down, float Wheel, float DeltaSeconds) noexcept
{
    if (!ShadeSeated_)
        return;
    // The host runs in logical pixels; the contact arrives in display pixels, so the exchange carries the contact
    //    scaled while the advance takes it logical.
    const float Scale = std::clamp(Shade_.QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.5f, 2.0f);
    const ImVec2 Display = ImGui::GetIO().DisplaySize;
    Shade_.Resize(static_cast<uint32_t>(Display.x / Scale + 0.5f), static_cast<uint32_t>(Display.y / Scale + 0.5f));
    ShadeInput_.AssignCursorPosition(CursorX * Scale, CursorY * Scale);
    ShadeInput_.AssignMouseButton(MouseButtonCategory::ButtonLeft, Down);
    ShadeInput_.ResetMouseScroll();
    if (Wheel != 0.0f)
        ShadeInput_.AssignMouseScroll(Wheel);
    Shade_.AdvanceInteraction(ShadeInput_, CursorX, CursorY);
    Shade_.AdvanceLocomotion(DeltaSeconds);
    Toasts_.Advance(DeltaSeconds);

    // Only an edge past the echo moves the shade, so the publish below never fights a tap that already seated the pose.
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
}

bool SolidArcEditorHost::ShadeCoversPointer() const noexcept
{
    return ShadeSeated_ && Shade_.CoversPointer();
}

bool SolidArcEditorHost::QueryShadeOpen() const noexcept
{
    return ShadeSeated_ && Shade_.IsOpen();
}

float SolidArcEditorHost::QueryNotchX() const noexcept
{
    const PlaneExtent Grip = Shade_.QueryHandleExtent();
    return (Grip.MinimumX + Grip.MaximumX) * 0.5f;
}

float SolidArcEditorHost::QueryNotchY() const noexcept
{
    const PlaneExtent Grip = Shade_.QueryHandleExtent();
    return (Grip.MinimumY + Grip.MaximumY) * 0.5f;
}

float SolidArcEditorHost::QueryGripX() const noexcept
{
    const PlaneExtent Grip = Shade_.QueryGripExtent();
    return (Grip.MinimumX + Grip.MaximumX) * 0.5f;
}

float SolidArcEditorHost::QueryGripY() const noexcept
{
    const PlaneExtent Grip = Shade_.QueryGripExtent();
    return (Grip.MinimumY + Grip.MaximumY) * 0.5f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DOCK LAYOUT
//------------------------------------------------------------------------------------------------------------------------

void SolidArcEditorHost::ConstructLayout() noexcept
{
    const ImGuiID DockId = ImGui::GetID("SolidArcEditorDockSpace");
    if (LayoutSeated_)
        return;
    LayoutSeated_ = true;

    ImGuiViewport* Main = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(DockId);
    ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockId, Main->Size);

    ImGuiID Left = 0u, CentreAndRight = 0u, Centre = 0u, Right = 0u;
    const float LeftShare = Main->Size.x > 0.0f ? std::clamp(300.0f / Main->Size.x, 0.18f, 0.30f) : 0.18f;
    ImGui::DockBuilderSplitNode(DockId, ImGuiDir_Left, LeftShare, &Left, &CentreAndRight);
    const float RestWidth = std::max(1.0f, Main->Size.x * (1.0f - LeftShare));
    // Roomy windows seat the inspector at its designed width; a small one keeps the compact column.
    const float RightShare = std::clamp((Main->Size.x >= 1500.0f ? 380.0f : 236.0f) / RestWidth, 0.16f, 0.34f);
    ImGui::DockBuilderSplitNode(CentreAndRight, ImGuiDir_Right, RightShare, &Right, &Centre);
    ImGui::DockBuilderDockWindow("SolidArc Outliner", Left);
    if (DocumentCommands_)
    {
        ImGuiID Commands = 0u, View = 0u;
        ImGui::DockBuilderSplitNode(Centre, ImGuiDir_Down, 0.23f, &Commands, &View);
        ImGui::DockBuilderDockWindow("Document commands", Commands);
        Centre = View;
    }
    ImGui::DockBuilderDockWindow("SolidArc Viewport", Centre);
    ImGui::DockBuilderDockWindow("SolidArc Inspector", Right);
    LeftColumn_ = Left;
    CentreColumn_ = Centre;
    RightColumn_ = Right;
    ImGui::DockBuilderFinish(DockId);
}

uint32_t SolidArcEditorHost::QueryConstructTileCount() noexcept
{
    return kConstructTileCount;
}

bool SolidArcEditorHost::PlaceConstruct(ConsoleHost& Host, uint32_t Tile, double X, double Y) noexcept
{
    const std::string Command = ConstructCommand(Tile, X, Y);
    if (Command.empty())
        return false;
    const size_t Before = Host.AllFigures().size();
    const bool   Done   = Host.Execute(Command);
    if (Done && Host.AllFigures().size() > Before)
    {
        ConstructPlacedName_ = Host.AllFigures().back().Name;
        Host.Execute("select " + ConstructPlacedName_);
        return true;
    }
    return false;
}

void SolidArcEditorHost::SeatView(ConsoleHost& Host) noexcept
{
    Host.AssignLatticeCell(0.01); // web SolidArc's 10 mm XY construction grid
    // The raster takes the size of the view it fills, in device pixels. The optional smooth preview uses four
    //    working samples per pixel; fast editing uses one to avoid multiplying CPU raster cost on every gesture.
    const float Scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
    const float Width = Viewport_.QueryViewWidth() * Scale;
    const float Height = Viewport_.QueryViewHeight() * Scale;
    // The CAD viewport never invokes ProjectDrive/ReSTIR. Bound the CPU raster's
    // pixel work and use a smaller interactive budget while navigating/dragging;
    // picking and ray projection both use the same scaled target coordinates.
    const ViewportCadContact& Contact = Viewport_.QueryCadContact();
    const bool Interactive = GizmoDragging_ || Contact.Start || Contact.Move || (Contact.Left && !Contact.End);
    const double Budget = Interactive ? 460000.0 : 1100000.0;
    const double Ratio = Width > 0.0f && Height > 0.0f
        ? std::min(1.0, std::sqrt(Budget / (double(Width) * double(Height)))) : 1.0;
    if (Width >= 32.0f && Height >= 32.0f)
        Host.SeatSurface(static_cast<uint32_t>(Width * Ratio + 0.5),
                         static_cast<uint32_t>(Height * Ratio + 0.5), Interactive ? 1u : PreviewSamples_);
    Host.ResizeGizmoAtView(110.0 * Scale * Ratio);
}

void SolidArcEditorHost::AdvanceViewport(ConsoleHost& Host) noexcept
{
    const ViewportCadContact& Contact = Viewport_.QueryCadContact();
    const double Width = static_cast<double>(Host.Raster().Width());
    const double Height = static_cast<double>(Host.Raster().Height());
    const float Scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
    if (ConstructTile_ < 0 && Contact.Start && Contact.Left && !Contact.Orbit && !Contact.Pan)
    {
        GizmoDragging_ = Host.BeginGizmoAtView(Contact.U * Width, Contact.V * Height);
        if (GizmoDragging_) Viewport_.CaptureCadGizmo();
    }
    if (Contact.Move)
    {
        if (GizmoDragging_)
            Host.DragGizmoAtView(Contact.U * Width, Contact.V * Height, Contact.Snap);
        else if (!Contact.Box && (ConstructTile_ < 0 || Contact.Pan || Contact.Orbit))
        {
            if (Contact.Pan)
                Host.Camera().Pan(Contact.DeltaX * Scale, -Contact.DeltaY * Scale, Height);
            else
                Host.Camera().Orbit(-Contact.DeltaX * 0.006, Contact.DeltaY * 0.006);
        }
    }
    if (Contact.End || Contact.Cancel)
    {
        if (GizmoDragging_)
        {
            Viewport_.DiscardCadPick();
            if (Contact.End) Host.DragGizmoAtView(Contact.U * Width, Contact.V * Height, Contact.Snap);
            Host.FinishGizmoAtView(Contact.Cancel);
            GizmoDragging_ = false;
        }
        else if (Contact.Cancel) Viewport_.DiscardCadPick();
    }
    if (ConstructTile_ >= 0)
    {
        Viewport_.DiscardCadPick();
        // A construction gesture intersects the actual camera ray with the XY workplane. Missing
        // (edge-on) rays cannot produce a figure; no origin/ring default is substituted.
        Vec3 Hit;
        ConstructCursorValid_ = Contact.Hover && SnapResolution::PlaneHit(Contact.U * Width, Contact.V * Height,
            Host.Camera(), Host.Raster().Width(), Host.Raster().Height(), Workplane::XY(), Hit);
        if (ConstructCursorValid_)
        {
            ConstructCursorX_ = Hit.X;
            ConstructCursorY_ = Hit.Y;
        }
        if (ConstructTile_ >= 1 && ConstructTile_ <= 11)
        {
            // The existing CAD ToolSession owns sketch previews, snaps, point prompts, Enter and Esc.
            // Only send real pointer positions over the view; its tool never fabricates coordinates.
            if (ConstructCursorValid_ && (AimCellX_ != int32_t(Contact.U * Width) || AimCellY_ != int32_t(Contact.V * Height)))
            {
                Host.MoveToolPointer(Contact.U * Width, Contact.V * Height, Contact.Snap);
                AimCellX_ = int32_t(Contact.U * Width);
                AimCellY_ = int32_t(Contact.V * Height);
            }
            if (Contact.End && Contact.Left && !Contact.Travelled && !Contact.Orbit && !Contact.Pan && ConstructCursorValid_)
            {
                char Line[96];
                std::snprintf(Line, sizeof(Line), "click %.2f %.2f%s", Contact.U * Width, Contact.V * Height,
                    Contact.Snap ? " --ctrl" : "");
                Host.Execute(Line);
                ConstructAnchorSet_ = true;
                if (!Host.HasActiveTool()) { ConstructTile_ = -1; ConstructAnchorSet_ = false; }
            }
        }
        else if (Contact.End && Contact.Left && !Contact.Travelled && !Contact.Orbit && !Contact.Pan && ConstructCursorValid_)
        {
            ConstructX_ = ConstructCursorX_;
            ConstructY_ = ConstructCursorY_;
            ConstructAnchorSet_ = true;
        }
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            if (Host.HasActiveTool()) Host.Execute("tool cancel");
            ConstructTile_ = -1;
            ConstructAnchorSet_ = false;
        }
        else if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        {
            if (Host.HasActiveTool() && ConstructAnchorSet_)
            {
                Host.Execute("key enter");
                if (!Host.HasActiveTool()) { ConstructTile_ = -1; ConstructAnchorSet_ = false; }
            }
            else if (ConstructAnchorSet_ && PlaceConstruct(Host, uint32_t(ConstructTile_), ConstructX_, ConstructY_))
            {
                ConstructTile_ = -1;
                ConstructAnchorSet_ = false;
                Outliner_.AssignPicks(nullptr, 0u);
                MirrorPicked_.clear();
                MirrorSelected_.clear();
            }
        }
    }

    if (Contact.Wheel != 0.0f && !GizmoDragging_) Host.Camera().Dolly(Contact.Wheel);
    if (GizmoDragging_) Viewport_.DiscardCadPick();

    const uint32_t Rail = Viewport_.QuerySolidArcGizmo();
    if (Rail != MirrorGizmo_)
    {
        MirrorGizmo_ = Rail;
        Host.Execute(Rail == 0u ? "gizmo translate" : Rail == 1u ? "gizmo rotate" : "gizmo scale");
    }
    ImGuiIO& IO = ImGui::GetIO();
    if (ConstructTile_ < 0 && !IO.WantTextInput && !IO.KeyCtrl && !IO.KeySuper && !Viewport_.QueryConstructOpen())
    {
        const bool Shift = IO.KeyShift;
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) Viewport_.AssignSolidArcGizmo(0u);
        if (Shift && ImGui::IsKeyPressed(ImGuiKey_R, false)) Viewport_.AssignSolidArcGizmo(1u);
        if (!Shift && ImGui::IsKeyPressed(ImGuiKey_S, false)) Viewport_.AssignSolidArcGizmo(2u);
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) Viewport_.AssignSolidArcSelectMask(1u);
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) Viewport_.AssignSolidArcSelectMask(2u);
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) Viewport_.AssignSolidArcSelectMask(4u);
        if (ImGui::IsKeyPressed(ImGuiKey_4, false)) Viewport_.AssignSolidArcSelectMask(8u);
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) Host.Execute("view fit");
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) Host.Execute(Shift ? "view bottom" : "view top");
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) Host.Execute(Shift ? "view back" : "view front");
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) Host.Execute(Shift ? "view left" : "view right");
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad5, false)) Host.Execute("view toggle");
    }
    if (!IO.WantTextInput && (IO.KeyCtrl || IO.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        Host.Execute(IO.KeyShift ? "redo" : "undo");
    // [m] Keep the viewport's readout aligned with the actual renderer camera, not an unused second orbit.
    ViewportOrbit Orbit = Viewport_.QueryViewportOrbit();
    const CameraProjection& Camera = Host.Camera();
    Orbit.Yaw = static_cast<float>(Camera.Yaw);
    Orbit.Pitch = static_cast<float>(Camera.Pitch);
    Orbit.Distance = static_cast<float>(Camera.Distance);
    Orbit.Target[0] = static_cast<float>(Camera.Pivot.X);
    Orbit.Target[1] = static_cast<float>(Camera.Pivot.Y);
    Orbit.Target[2] = static_cast<float>(Camera.Pivot.Z);
    Orbit.Ortho = Camera.Orthographic;
    Viewport_.SeatViewportOrbit(Orbit);
}

void SolidArcEditorHost::ReconcileSelection(ConsoleHost& Host) noexcept
{
    ImGuiIO& IO = ImGui::GetIO();
    SceneDocument& Scene = Host.Document();

    // The rail's select mode drives what a tap or a box picks: body, face, edge or vertex.
    const uint32_t Mask = Viewport_.QuerySolidArcSelectMask();
    if (Mask != MirrorMask_)
    {
        MirrorMask_ = Mask;
        const char* Mode = (Mask & 8u) != 0u ? "control" : ((Mask & 4u) != 0u ? "edge" : ((Mask & 2u) != 0u ? "face" : "whole"));
        Host.Execute(std::string("selectmode ") + Mode);
    }

    auto Identities = [&](bool FromPick)
    {
        std::vector<uint32_t> Found;
        if (FromPick)
        {
            for (uint32_t Slot = 0u; Slot < Outliner_.QueryPickedCount(); ++Slot)
            {
                const uint32_t Row = Outliner_.QueryPickedAt(Slot);
                if (Row < RowCount_ && Bindings_[Row].RowRole == SolidArcOutlinerBinding::Role::Figure)
                    Found.push_back(Bindings_[Row].FigureIdentity);
            }
        }
        else
        {
            for (const SceneFigure& Figure : Scene.Figures())
                if (Figure.Selected || !Figure.SelectedFaces.empty() || !Figure.SelectedEdges.empty() || !Figure.SelectedPoles.empty())
                    Found.push_back(Figure.Identity);
        }
        return Found;
    };
    auto Sorted = [](std::vector<uint32_t> List)
    {
        std::sort(List.begin(), List.end());
        List.erase(std::unique(List.begin(), List.end()), List.end());
        return List;
    };

    // What the view did this tick: a tap, a swept box, the hover, the keys.
    bool Acted = false;
    const double ViewW = static_cast<double>(Host.Raster().Width());
    const double ViewH = static_cast<double>(Host.Raster().Height());
    float U = 0.0f, V = 0.0f, U1 = 0.0f, V1 = 0.0f;
    bool Extend = false, Subtract = false;
    if (ConstructTile_ < 0 && Viewport_.QueryViewTap(&U, &V, &Extend))
    {
        Host.SelectAtView(U * ViewW, V * ViewH, Extend);
        Acted = true;
    }
    if (ConstructTile_ < 0 && Viewport_.QueryViewBox(&U, &V, &U1, &V1, &Extend, &Subtract))
    {
        Host.SelectBoxAtView(U * ViewW, V * ViewH, U1 * ViewW, V1 * ViewH, Extend, Subtract);
        Acted = true;
    }
    if (float AimU = 0.0f, AimV = 0.0f; ConstructTile_ < 0 && !GizmoDragging_ && Viewport_.QueryViewAim(&AimU, &AimV))
    {
        const int32_t CellX = static_cast<int32_t>(AimU * ViewW);
        const int32_t CellY = static_cast<int32_t>(AimV * ViewH);
        if (CellX != AimCellX_ || CellY != AimCellY_)
        {
            AimCellX_ = CellX;
            AimCellY_ = CellY;
            Host.HoverAtView(CellX, CellY);
            Host.AimGizmoAtView(CellX, CellY);
        }
    }
    else if (AimCellX_ >= 0)
    {
        AimCellX_ = -1;
        AimCellY_ = -1;
        Host.HoverNothing();
        Host.AimGizmoAtView(-1000.0, -1000.0);
    }
    if (ConstructTile_ < 0 && !IO.WantTextInput && !Viewport_.QueryConstructOpen())
    {
        if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            if (Host.CurrentSelectMode() == SelectMode::Whole) Host.Execute("select all");
            else
            {
                Scene.ClearSelection();
                for (SceneFigure& Figure : Scene.Figures())
                {
                    if (Figure.Hidden || Figure.Locked) continue;
                    if (Host.CurrentSelectMode() == SelectMode::Face && Figure.Classification == FigureClassification::Body)
                        for (int F = 0; F < static_cast<int>(Figure.Body.Faces.size()); ++F) Figure.SelectedFaces.push_back(F);
                    else if (Host.CurrentSelectMode() == SelectMode::Edge && Figure.Classification == FigureClassification::Body)
                        for (int E = 0; E < static_cast<int>(Figure.Body.Edges.size()); ++E) Figure.SelectedEdges.push_back(E);
                    else if (Host.CurrentSelectMode() == SelectMode::Control)
                    {
                        for (int P = 0; P < Figure.PoleCount(); ++P) Figure.SelectedPoles.push_back(P);
                        Figure.Selected = !Figure.SelectedPoles.empty();
                    }
                }
            }
            Acted = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
            (Scene.SelectedCount() + Scene.SelectedFaceCount() + Scene.SelectedEdgeCount() + Scene.SelectedPoleCount()) > 0)
        {
            Host.Execute("select none");
            Acted = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && Host.CurrentSelectMode() == SelectMode::Whole && Scene.SelectedCount() > 0)
        {
            std::string Line = "delete";
            for (const SceneFigure& Figure : Scene.Figures())
                if (Figure.Selected && !Figure.Locked)
                    Line += " #" + std::to_string(Figure.Identity);
            if (Line.size() > 6u)
                Host.Execute(Line);
            Acted = true;
        }
    }

    // Plasticity-style edge operations: B starts a rounded bevel (fillet), C a flat
    // chamfer. Act only on explicitly selected edges; never bevel every edge of a body
    // because a user pressed a shortcut while the whole body was selected.
    if (ConstructTile_ < 0 && !IO.WantTextInput && !IO.KeyCtrl && !IO.KeySuper && !Viewport_.QueryConstructOpen())
    {
        const bool Bevel = ImGui::IsKeyPressed(ImGuiKey_B, false);
        const bool Chamfer = ImGui::IsKeyPressed(ImGuiKey_C, false);
        if (Bevel || Chamfer)
        {
            bool HadEdges = false;
            bool Succeeded = false;
            for (const SceneFigure& Figure : Scene.Figures())
            {
                if (Figure.Hidden || Figure.Locked || Figure.Classification != FigureClassification::Body || Figure.SelectedEdges.empty()) continue;
                std::string Edges;
                for (int Edge : Figure.SelectedEdges)
                {
                    if (!Edges.empty()) Edges += ',';
                    Edges += std::to_string(Edge);
                }
                HadEdges = true;
                const std::string Command = std::string(Bevel ? "fillet " : "chamfer ")
                    + std::to_string(Figure.Identity) + " 0.1 --edges=" + Edges;
                if (Host.Execute(Command)) Succeeded = true;
                break; // the operation can replace the figure, invalidating this iteration
            }
            Toasts_.Push(Bevel ? "Bevel (B)" : "Chamfer (C)",
                !HadEdges ? "Select one or more body edges first" :
                Succeeded ? "Applied to selected edges" : "Kernel refused this edge set; see console");
            Acted = Succeeded;
        }
    }

    const std::vector<uint32_t> PickNow = Sorted(Identities(true));
    const std::vector<uint32_t> SelectedNow = Sorted(Identities(false));

    auto LeadAmong = [&](const std::vector<uint32_t>& Now, const std::vector<uint32_t>& Before)
    {
        for (uint32_t Identity : Now)
            if (!std::binary_search(Before.begin(), Before.end(), Identity))
                return Identity;
        if (std::binary_search(Now.begin(), Now.end(), Lead_))
            return Lead_;
        return Now.empty() ? 0u : Now.front();
    };
    auto PushToOutliner = [&](const std::vector<uint32_t>& Selected)
    {
        uint32_t Rows[kMaxEditorPicked] = {};
        uint32_t Count = 0u;
        auto Seat = [&](uint32_t Identity)
        {
            for (uint32_t Row = 0u; Row < RowCount_ && Count < kMaxEditorPicked; ++Row)
                if (Bindings_[Row].RowRole == SolidArcOutlinerBinding::Role::Figure && Bindings_[Row].FigureIdentity == Identity)
                {
                    Rows[Count++] = Row;
                    return;
                }
        };
        Seat(Lead_);                                                   // the lead goes first: the inspector reads the first pick
        for (uint32_t Identity : Selected)
            if (Identity != Lead_)
                Seat(Identity);
        Outliner_.AssignPicks(Rows, Count);
        MirrorPicked_ = Sorted(Identities(true));
    };

    if (!Acted && PickNow != MirrorPicked_)
    {
        // The outliner's pick moved (a click, Ctrl, Shift): the document follows it.
        Lead_ = LeadAmong(PickNow, MirrorPicked_);
        if (const uint32_t Row = Outliner_.QueryPicked(); Row < RowCount_ && Bindings_[Row].RowRole == SolidArcOutlinerBinding::Role::Figure)
            Lead_ = Bindings_[Row].FigureIdentity;
        for (SceneFigure& Figure : Scene.Figures())
        {
            const bool In = std::binary_search(PickNow.begin(), PickNow.end(), Figure.Identity);
            if (In)
                Figure.Selected = true;
            else if (Figure.Selected || !Figure.SelectedFaces.empty() || !Figure.SelectedEdges.empty() || !Figure.SelectedPoles.empty())
            {
                Figure.Selected = false;
                Figure.SelectedFaces.clear();
                Figure.SelectedEdges.clear();
                Figure.SelectedPoles.clear();
            }
        }
        MirrorPicked_   = PickNow;
        MirrorSelected_ = Sorted(Identities(false));
    }
    else if (Acted || SelectedNow != MirrorSelected_)
    {
        // The view, a key or the console moved the document's selection: the outliner follows it.
        Lead_ = LeadAmong(SelectedNow, MirrorSelected_);
        MirrorSelected_ = SelectedNow;
        PushToOutliner(SelectedNow);
    }
}

void SolidArcEditorHost::Record(ConsoleHost& Host) noexcept
{
    SeatView(Host);
    // The raster is dear and the picture rarely changes: draw and read it back only when it has.
    if (Host.RenderIfChanged() || ViewImage_.Pixels.empty())
    {
        ViewImage_ = Host.Raster().Readback();
        ++ViewRevision_;
    }
    // The roster is rebuilt from the document each frame, so a folder's fold would reset. Carry it by folder label.
    ShutFolders_.clear();
    for (uint32_t Index = 0u; Index < RowCount_; ++Index)
        if (Rows_[Index].Depth == 0u && Bindings_[Index].RowRole == SolidArcOutlinerBinding::Role::None && Rows_[Index].Shut)
            ShutFolders_.emplace_back(Rows_[Index].Label);
    RowCount_ = BuildSolidArcOutliner(Host, Rows_.data(), Bindings_.data(), kMaxEditorInstances, &Readout_);
    for (uint32_t Index = 0u; Index < RowCount_; ++Index)
        if (Rows_[Index].Depth == 0u && Bindings_[Index].RowRole == SolidArcOutlinerBinding::Role::None)
            Rows_[Index].Shut = std::find(ShutFolders_.begin(), ShutFolders_.end(), std::string(Rows_[Index].Label)) != ShutFolders_.end();
    if (!ViewImage_.Pixels.empty())
        Viewport_.AssignView(ViewImage_.Pixels.data(), ViewImage_.Width, ViewImage_.Height);
    else
        Viewport_.AssignView(nullptr, 0u, 0u);

    ImGuiViewport* Main = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(Main->Pos.x, Main->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(Main->Size.x, Main->Size.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar
                                         | ImGuiWindowFlags_NoResize
                                         | ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoScrollbar
                                         | ImGuiWindowFlags_NoScrollWithMouse
                                         | ImGuiWindowFlags_NoSavedSettings
                                         | ImGuiWindowFlags_NoBringToFrontOnFocus
                                         | ImGuiWindowFlags_NoNavFocus;
    if (ImGui::Begin("SolidArcDockHost", nullptr, HostFlags))
    {
        const ImGuiDockNodeFlags DockFlags =
            static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton);
        ConstructLayout();
        ImGui::DockSpace(ImGui::GetID("SolidArcEditorDockSpace"), ImVec2(0.0f, 0.0f), DockFlags);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    Outliner_.Record(Rows_.data(), RowCount_);
    Viewport_.Record(Rows_.data(), RowCount_);

    // Choosing a tile only ARMS the tool. No geometry is created until the user supplies
    // viewport coordinates. A sketch uses the same modal tool session as the console.
    if (uint32_t Tile = 0u; Viewport_.QueryConstructPick(&Tile))
    {
        if (Host.HasActiveTool()) Host.Execute("tool cancel");
        ConstructTile_ = int32_t(Tile);
        ConstructAnchorSet_ = false;
        AimCellX_ = AimCellY_ = -1;
        if (Tile >= 1u && Tile <= 11u)
        {
            constexpr const char* Names[] = { "line", "polyline", "rect", "centerrect", "slot",
                "circle", "arc", "ellipse", "polygon", "spline", "cpcurve" };
            if (!Host.Execute(std::string("tool ") + Names[Tile - 1u])) ConstructTile_ = -1;
        }
    }

    AdvanceViewport(Host);
    // Viewport-local drafting feedback lives in the UI layer; it never costs a scene
    // tessellation and remains legible when the software CAD raster is downscaled.
    if (ConstructTile_ >= 0)
    {
        const ImVec2 Origin(Viewport_.QueryViewOriginX(), Viewport_.QueryViewOriginY());
        const ImVec2 Extent(Viewport_.QueryViewWidth(), Viewport_.QueryViewHeight());
        ImDrawList* Draw = ImGui::GetForegroundDrawList();
        Draw->PushClipRect(Origin, ImVec2(Origin.x + Extent.x, Origin.y + Extent.y), true);
        const char* Label = kConstructTiles[ConstructTile_].Label;
        char Prompt[192];
        std::snprintf(Prompt, sizeof(Prompt), "%s  |  %s  |  Enter: finish  Esc: cancel",
            Label, ConstructAnchorSet_ ? "Anchor chosen" : "Click workplane to draw/place");
        Draw->AddRectFilled(ImVec2(Origin.x + 14, Origin.y + 16),
            ImVec2(Origin.x + std::min(Extent.x - 14.0f, 470.0f), Origin.y + 45), IM_COL32(15, 20, 30, 218), 6.0f);
        Draw->AddText(ImVec2(Origin.x + 23, Origin.y + 23), IM_COL32(246, 204, 117, 255), Prompt);
        if (ConstructCursorValid_)
        {
            const ImVec2 P(Origin.x + Viewport_.QueryCadContact().U * Extent.x,
                           Origin.y + Viewport_.QueryCadContact().V * Extent.y);
            Draw->AddLine(ImVec2(Origin.x, P.y), ImVec2(Origin.x + Extent.x, P.y), IM_COL32(79, 216, 224, 65));
            Draw->AddLine(ImVec2(P.x, Origin.y), ImVec2(P.x, Origin.y + Extent.y), IM_COL32(79, 216, 224, 65));
            Draw->AddCircle(P, 6.0f, IM_COL32(255, 180, 84, 255));
            char Coordinates[80];
            std::snprintf(Coordinates, sizeof(Coordinates), "XY  %.3f, %.3f", ConstructCursorX_, ConstructCursorY_);
            Draw->AddText(ImVec2(P.x + 12, P.y + 10), IM_COL32(255, 230, 190, 255), Coordinates);
        }
        Draw->PopClipRect();
    }
    ReconcileSelection(Host);

    const uint32_t Picked = Outliner_.QueryPicked();
    EditorInstance* PickedRow = (Picked < RowCount_) ? &Rows_[Picked] : nullptr;
    const SolidArcOutlinerBinding Selection = PickedRow != nullptr ? Bindings_[Picked] : SolidArcOutlinerBinding{};
    const bool SheetReady = BuildSolidArcInspectorSheet(Host, Selection, &PickedSheet_);
    Inspector_.Record(PickedRow, Picked, SheetReady ? &PickedSheet_ : nullptr);

    ApplySolidArcOutlinerVisibility(Host, Rows_.data(), Bindings_.data(), RowCount_);
    if (SheetReady && Picked < RowCount_)
        ApplySolidArcInspectorSheet(Host, Bindings_[Picked], PickedSheet_);

    // The notch records last, above the dock columns, onto the foreground list: the Project-Zero order, kept.
    if (ShadeSeated_)
    {
        const float UiScale = std::clamp(Shade_.QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.5f, 2.0f);
        if (ShadeSurface_.Begin(SurfaceLayer::Above, Main->Size.x, Main->Size.y, UiScale))
        {
            const float BandLine = ImGui::GetStyle().TabHeight + ImGui::GetStyle().TabStripPadTop;
            Shade_.ConstructControlLayout(ShadeSurface_);
            Toasts_.ConstructNotificationLayout(ShadeSurface_, BandLine);
        }
    }
}

bool SolidArcEditorHost::PickRow(const char* Label, SolidArcOutlinerBinding::Role Role) noexcept
{
    for (uint32_t Index = 0u; Index < RowCount_; ++Index)
    {
        if (Bindings_[Index].RowRole != Role || (Label != nullptr && std::strcmp(Rows_[Index].Label, Label) != 0))
            continue;
        Outliner_.PickInstance(Index);
        return true;
    }
    return false;
}

bool SolidArcEditorHost::ExtendRow(const char* Label, SolidArcOutlinerBinding::Role Role) noexcept
{
    for (uint32_t Index = 0u; Index < RowCount_; ++Index)
    {
        if (Bindings_[Index].RowRole != Role || (Label != nullptr && std::strcmp(Rows_[Index].Label, Label) != 0))
            continue;
        Outliner_.TogglePick(Index);
        return true;
    }
    return false;
}

void SolidArcEditorHost::ClearPick() noexcept
{
    Outliner_.PickInstance(kNoEditorInstance);
}

uint32_t SolidArcEditorHost::QueryPickedFigureIdentity() const noexcept
{
    const uint32_t Picked = Outliner_.QueryPicked();
    if (Picked >= RowCount_ || Bindings_[Picked].RowRole != SolidArcOutlinerBinding::Role::Figure)
        return 0u;
    return Bindings_[Picked].FigureIdentity;
}

} // namespace Frontier
