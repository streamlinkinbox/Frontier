/*==========================================================================================================================================
                                                      FLUIDEDITORVIEWPORT.CPP
==========================================================================================================================================*/
// 🧩 The Fluid editor's viewport: the solver's picture with the prototype's tools, caption, HUD, help line and axis gizmo over it; the pointer on the
//    picture (FluidPanel.js ConnectViewport: orbit, pan, the source brush, the burst tool, the wheel); the transport and the simulation ruler under it;
//    and the preset library as a sheet over the editor.

#include "Engine/Interface/FluidEditor/FluidEditorWorkspace.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

using namespace FluidEditor;

constexpr ImU32 FireTint  = IM_COL32(232, 131, 58, 255);
constexpr ImU32 SmokeTint = IM_COL32(190, 196, 206, 255);
constexpr ImU32 SunTint   = IM_COL32(232, 194, 122, 255);

using WindowControls::ConstructPillButton;
using WindowControls::Look;
using WindowControls::Lowered;
using WindowControls::MeasurePillWidth;

const char* ResolveViewLabel(const std::string& View)
{
    return View == "front" ? "Front" : View == "side" ? "Side" : View == "top" ? "Top" : "Perspective";
}

const char* ResolveBurstLabel(const std::string& Kind)
{
    return Kind == "shrapnel" ? "Shrapnel" : Kind == "salvo" ? "Cluster salvo" : Kind == "collide" ? "Colliding waves" : Kind == "mushroom" ? "Mushroom cloud" : "Single burst";
}

const char* ResolveNextBurst(const std::string& Kind)
{
    return Kind == "burst" ? "shrapnel" : Kind == "shrapnel" ? "salvo" : Kind == "salvo" ? "collide" : Kind == "collide" ? "mushroom" : "burst";
}

const char* ResolveCategoryGlyph(const char* Glyph)
{
    // 📝 The prototype's preset swatches (flame, burst, smoke, box, cylinder, orbit, rotate, wall) on the editor's glyphs.
    if (std::strcmp(Glyph, "flame") == 0) return "Flame";
    if (std::strcmp(Glyph, "burst") == 0) return "Blast";
    if (std::strcmp(Glyph, "smoke") == 0) return "CloudFog";
    if (std::strcmp(Glyph, "box") == 0) return "Box";
    if (std::strcmp(Glyph, "cylinder") == 0) return "Cylinder";
    if (std::strcmp(Glyph, "orbit") == 0) return "Orbit";
    if (std::strcmp(Glyph, "rotate") == 0) return "Swirl";
    if (std::strcmp(Glyph, "wall") == 0) return "Square";
    return "Flame";
}

ImU32 ResolveCategoryTint(const char* Category)
{
    if (std::strcmp(Category, "fire") == 0) return FireTint;
    if (std::strcmp(Category, "blast") == 0) return SunTint;
    return SmokeTint;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorWorkspace::ConstructViewport(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look  Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    const auto& P = Style.Palette;
    Ink->AddRectFilled(Minimum, Maximum, P.ColumnBg, 14.0f * Scale);
    Ink->AddRect(Minimum, Maximum, P.EdgeBorder, 14.0f * Scale);

    // 📝 The bar: the title, the live pill, the camera views, Focus, Bounds, Turntable and the presets. It is planned from the widths it needs: where the
    //    column is narrower than the whole bar it gives up, in turn, the title's second line (1), the four views for one that cycles (2), and the pill's
    //    words and the title's text (3).
    const float Left    = Minimum.x + 14.0f * Scale;
    const float BarY    = Minimum.y + 12.0f * Scale;
    const bool  Live    = Image.Texture != 0;
    const bool  Paused  = Record->Gas.Paused;
    const char* Subtitle = "Eulerian volume \xC2\xB7 physically shaded";
    const char* Pill    = !Live ? "OFFLINE" : Paused ? "PAUSED" : "LIVE";
    const float TitleText = Style.Measure(Style.Faces.Medium, 13.0f, "Scene view");
    const float SubText   = Style.Measure(Style.Faces.Regular, 10.0f, Subtitle);
    const float PillText  = Style.Measure(Style.Faces.Medium, 10.0f, Pill) + 30.0f * Scale;
    static constexpr const char* Views[4]  = { "perspective", "front", "side", "top" };
    const auto  ViewWidths = [&](int Squeeze)
    {
        if (Squeeze >= 2) return MeasurePillWidth(Style, ResolveViewLabel(View), false, false);
        float Sum = 0.0f;
        for (const char* Name : Views) Sum += MeasurePillWidth(Style, ResolveViewLabel(Name), false, false) + 3.0f * Scale;
        return Sum - 3.0f * Scale;
    };
    const auto Needed = [&](int Squeeze)
    {
        const float Title = Squeeze >= 3 ? 40.0f * Scale : 40.0f * Scale + (Squeeze >= 1 ? TitleText : std::max(TitleText, SubText));
        const float Dot   = Squeeze >= 3 ? 22.0f * Scale : PillText;
        return Title + 12.0f * Scale + Dot + 12.0f * Scale + ViewWidths(Squeeze) + 14.0f * Scale + 4.0f * 36.0f * Scale + 3.0f * 3.0f * Scale;
    };
    const float Available = Maximum.x - 14.0f * Scale - Left;
    int         Squeeze   = 0;
    while (Squeeze < 3 && Needed(Squeeze) > Available) ++Squeeze;
    Ink->AddRectFilled(ImVec2(Left, BarY), ImVec2(Left + 30.0f * Scale, BarY + 30.0f * Scale), P.CountCardBg, 8.0f * Scale);
    GlyphSymbol::Construct(Ink, "Box", ImVec2(Left + 7.0f * Scale, BarY + 7.0f * Scale), 16.0f * Scale, 1.4f, FireTint);
    float Cursor = Left + 40.0f * Scale;
    if (Squeeze < 3)
    {
        Style.Text(Ink, Style.Faces.Medium, 13.0f, ImVec2(Cursor, BarY + (Squeeze >= 1 ? 7.0f : 1.0f) * Scale), P.TextPrimary, "Scene view");
        if (Squeeze < 1) Style.Text(Ink, Style.Faces.Regular, 10.0f, ImVec2(Cursor, BarY + 18.0f * Scale), P.TextSecondary, Subtitle);
        Cursor += (Squeeze >= 1 ? TitleText : std::max(TitleText, SubText)) + 12.0f * Scale;
    }
    else
        Cursor += 12.0f * Scale;
    const float PillW = Squeeze >= 3 ? 22.0f * Scale : PillText;
    const ImU32 Mark  = !Live ? P.WarmAmber : Paused ? P.TextSecondary : P.LiveGreen;
    Ink->AddRectFilled(ImVec2(Cursor, BarY + 3.0f * Scale), ImVec2(Cursor + PillW, BarY + 27.0f * Scale), IM_COL32(18, 40, 28, 255), 12.0f * Scale);
    Ink->AddCircleFilled(ImVec2(Cursor + 11.0f * Scale + (Squeeze >= 3 ? 0.0f : 1.0f * Scale), BarY + 15.0f * Scale), 3.5f * Scale, Mark);
    if (Squeeze < 3) Style.Text(Ink, Style.Faces.Medium, 10.0f, ImVec2(Cursor + 21.0f * Scale, BarY + 8.5f * Scale), Mark, Pill);
    Cursor += PillW + 12.0f * Scale;
    float Width = 0.0f;
    // 📝 The camera view select: Perspective is Focus volume; Front, Side and Top turn the aim.
    if (Squeeze >= 2)
    {
        if (ConstructPillButton(Style, Ink, Controls, "view", ImVec2(Cursor, BarY), ResolveViewLabel(View), false, nullptr, &Width, false, "Fluid"))
        {
            const std::string Next = View == "perspective" ? "front" : View == "front" ? "side" : View == "side" ? "top" : "perspective";
            Requests.push_back("view:" + Next);
        }
        Cursor += Width + 14.0f * Scale;
    }
    else
    {
        for (const char* Name : Views)
        {
            char Control[24];
            std::snprintf(Control, sizeof(Control), "view:%s", Name);
            if (ConstructPillButton(Style, Ink, Controls, Control, ImVec2(Cursor, BarY), ResolveViewLabel(Name), View == Name, nullptr, &Width, false, "Fluid"))
                Requests.push_back(std::string("view:") + Name);
            Cursor += Width + 3.0f * Scale;
        }
        Cursor += 11.0f * Scale;
    }
    // 📝 The right of the bar: Focus volume (F), Domain bounds (B), Turntable (T) and the presets (/), as glyphs that name themselves in a tooltip.
    float Right = Maximum.x - 14.0f * Scale - 4.0f * 36.0f * Scale - 3.0f * 3.0f * Scale;
    Right       = std::max(Right, Cursor);
    if (ConstructPillButton(Style, Ink, Controls, "focus", ImVec2(Right, BarY), "Focus volume \xC2\xB7 F", false, "Focus", &Width, true, "Fluid")) Requests.push_back("camera:fit");
    Right += Width + 3.0f * Scale;
    if (ConstructPillButton(Style, Ink, Controls, "bounds", ImVec2(Right, BarY), "Domain bounds \xC2\xB7 B", Record->Gas.BoundingBoxShown, "ScanCorners", &Width, true, "Fluid"))
        Requests.push_back("bounds:toggle");
    Right += Width + 3.0f * Scale;
    if (ConstructPillButton(Style, Ink, Controls, "turntable", ImVec2(Right, BarY), "Turntable \xC2\xB7 T", Record->Gas.AutoTurntable, "RotateCcw", &Width, true, "Fluid"))
        Requests.push_back("turntable:toggle");
    Right += Width + 3.0f * Scale;
    if (ConstructPillButton(Style, Ink, Controls, "presets", ImVec2(Right, BarY), "Presets \xC2\xB7 /", LibraryShown, "Library", &Width, true, "Fluid")) Requests.push_back("library:on");

    // 📝 The picture, and what lies over it.
    const ImVec2 PictureMin = ViewportMinimum;
    const ImVec2 PictureMax = ViewportMaximum;
    Ink->AddRectFilled(PictureMin, PictureMax, IM_COL32(9, 9, 12, 255), 8.0f * Scale);
    if (Image.Texture != 0)
    {
        Ink->AddImageRounded(static_cast<ImTextureID>(Image.Texture), PictureMin, PictureMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f * Scale);
    }
    else
    {
        // 📝 Where there is no picture the viewport says why, in its middle; the Outliner, the Inspector and the presets go on working.
        const char* Reason = !Image.Failure.empty() ? Image.Failure.c_str() : "Starting the volume\xE2\x80\xA6";
        const float Middle = (PictureMin.x + PictureMax.x) * 0.5f;
        const float Centre = (PictureMin.y + PictureMax.y) * 0.5f;
        GlyphSymbol::Construct(Ink, "Box", ImVec2(Middle - 14.0f * Scale, Centre - 58.0f * Scale), 28.0f * Scale, 1.3f, IM_COL32(120, 90, 62, 255));
        Style.Text(Ink, Style.Faces.Medium, 13.0f, ImVec2(Middle - Style.Measure(Style.Faces.Medium, 13.0f, Reason) * 0.5f, Centre - 14.0f * Scale), P.WarmAmber, Reason);
        const char* First  = "The volume is still edited here.";
        const char* Second = "The Outliner, the Inspector and the presets work without it.";
        Style.Text(Ink, Style.Faces.Regular, 11.0f, ImVec2(Middle - Style.Measure(Style.Faces.Regular, 11.0f, First) * 0.5f, Centre + 10.0f * Scale), P.TextSecondary, First);
        Style.Text(Ink, Style.Faces.Regular, 11.0f, ImVec2(Middle - Style.Measure(Style.Faces.Regular, 11.0f, Second) * 0.5f, Centre + 28.0f * Scale), P.TextSecondary, Second);
    }
    Ink->PushClipRect(PictureMin, PictureMax, true);
    ConstructPointer(PictureMin, PictureMax);

    // 📝 The domain's bounds: the box's twelve edges projected with the camera (BoundsProjection.js), drawn over the picture while Domain bounds is on.
    BoundsSegments.clear();
    if (Record->Gas.BoundingBoxShown && (Live || ResolveReadout() != nullptr))
    {
        std::array<float, 3> BoxMinimum{}, BoxMaximum{};
        ResolveBoundsBox(BoxMinimum, BoxMaximum);
        PyroOrbitCamera Aimed = Camera;
        Aimed.Aspect          = (PictureMax.x - PictureMin.x) / std::max(1.0f, PictureMax.y - PictureMin.y);
        ProjectBounds(Aimed.ComputeBasis(), BoxMinimum, BoxMaximum, PictureMax.x - PictureMin.x, PictureMax.y - PictureMin.y, BoundsSegments);
        for (const std::array<float, 4>& Segment : BoundsSegments)
            Ink->AddLine(ImVec2(PictureMin.x + Segment[0], PictureMin.y + Segment[1]), ImVec2(PictureMin.x + Segment[2], PictureMin.y + Segment[3]), IM_COL32(255, 255, 255, 96), 1.2f * Scale);
    }

    // 📝 The caption (the selected part's name and what the viewport shows), top left.
    const std::string Name = FluidEditorPages::ResolvePartName(Memory, Selected);
    Style.Text(Ink, Style.Faces.Medium, 11.0f, ImVec2(PictureMin.x + 16.0f * Scale, PictureMin.y + 14.0f * Scale), IM_COL32(215, 215, 215, 255), Name.c_str());
    Style.Text(Ink, Style.Faces.Regular, 9.5f, ImVec2(PictureMin.x + 16.0f * Scale, PictureMin.y + 31.0f * Scale), IM_COL32(148, 148, 148, 255), Subtitle);

    // 📝 The tools (.viewport-tools): Orbit camera 1, Source brush 2 (or Shift + drag), Place burst 3.
    {
        const ImVec2 PanelMin(PictureMin.x + 16.0f * Scale, PictureMin.y + 56.0f * Scale);
        const float  Side = 29.0f * Scale, Pad = 4.0f * Scale, Step = 3.0f * Scale;
        const ImVec2 PanelMax(PanelMin.x + Side + 2.0f * Pad, PanelMin.y + 3.0f * Side + 2.0f * Step + 2.0f * Pad);
        Ink->AddRectFilled(PanelMin, PanelMax, IM_COL32(39, 39, 39, 235), 6.0f * Scale);
        Ink->AddRect(PanelMin, PanelMax, IM_COL32(68, 68, 68, 217), 6.0f * Scale);
        struct ToolEntry
        {
            const char* Token;
            const char* Label;
            const char* Glyph;
        };
        static constexpr ToolEntry Tools[3] = { { "orbit", "Orbit camera \xC2\xB7 1", "Orbit" }, { "flamethrower", "Source brush \xC2\xB7 2 or Shift + drag", "Crosshair" },
                                                { "detonate", "Place burst \xC2\xB7 3", "Blast" } };
        float ToolY = PanelMin.y + Pad;
        for (const ToolEntry& Tool : Tools)
        {
            const ImVec2 At(PanelMin.x + Pad, ToolY);
            char         Control[40];
            std::snprintf(Control, sizeof(Control), "tool:%s", Tool.Token);
            const bool Active = std::strcmp(Record->Gas.InteractionMode, Tool.Token) == 0;
            ImGui::SetCursorScreenPos(At);
            char Id[64];
            std::snprintf(Id, sizeof(Id), "##FluidTool%s", Tool.Token);
            const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(Side, Side));
            const bool Hovered = ImGui::IsItemHovered();
            if (Hovered) ImGui::SetTooltip("%s", Tool.Label);
            Controls.push_back({ Control, At, ImVec2(At.x + Side, At.y + Side) });
            if (Active || Hovered) Ink->AddRectFilled(At, ImVec2(At.x + Side, At.y + Side), Active ? P.SegmentedActiveBg : P.RowHoverBg, 5.0f * Scale);
            GlyphSymbol::Construct(Ink, Tool.Glyph, ImVec2(At.x + 7.0f * Scale, At.y + 7.0f * Scale), 15.0f * Scale, 1.4f, Active ? P.TextPrimary : P.TextSecondary);
            if (Pressed) Requests.push_back(std::string("tool:") + Tool.Token);
            ToolY += Side + Step;
        }
    }

    // 📝 The right HUD (.viewport-right-hud): the interface's refresh rate over half a second (a dash while paused) and the grid.
    {
        char Rate[24], Grid[24];
        if (Record->Gas.Paused) std::snprintf(Rate, sizeof(Rate), "\xE2\x80\x94 FPS");
        else std::snprintf(Rate, sizeof(Rate), "%d FPS", FramesPerSecond);
        std::snprintf(Grid, sizeof(Grid), "%s GRID", FluidEditorPages::FormatLattice(Record->Gas.LatticeResolution).c_str());
        float HudRight = PictureMax.x - 16.0f * Scale;
        for (const char* Words : { Grid, Rate })
        {
            const float W = Style.Measure(Style.Faces.Monospace, 9.5f, Words) + 14.0f * Scale;
            const ImVec2 At(HudRight - W, PictureMin.y + 16.0f * Scale);
            Ink->AddRectFilled(At, ImVec2(At.x + W, At.y + 22.0f * Scale), IM_COL32(24, 24, 24, 136), 4.0f * Scale);
            Ink->AddRect(At, ImVec2(At.x + W, At.y + 22.0f * Scale), IM_COL32(255, 255, 255, 18), 4.0f * Scale);
            Style.Text(Ink, Style.Faces.Monospace, 9.5f, ImVec2(At.x + 7.0f * Scale, At.y + 5.5f * Scale), IM_COL32(201, 201, 201, 255), Words);
            HudRight = At.x - 9.0f * Scale;
        }
    }

    // 📝 The help line and the axis gizmo (the prototype's static drawing: Y up, X down and left, Z down and right).
    Style.Text(Ink, Style.Faces.Regular, 9.5f, ImVec2(PictureMin.x + 18.0f * Scale, PictureMax.y - 24.0f * Scale), IM_COL32(148, 148, 148, 255),
               "Drag to orbit  \xC2\xB7  Right-drag to pan  \xC2\xB7  Scroll to zoom");
    {
        const float  Unit   = 60.0f / 76.0f * Scale;
        const ImVec2 Origin(PictureMax.x - 16.0f * Scale - 60.0f * Scale, PictureMax.y - 17.0f * Scale - 60.0f * Scale);
        const auto   At     = [&](float X, float Y) { return ImVec2(Origin.x + X * Unit, Origin.y + Y * Unit); };
        const ImU32  Stroke = IM_COL32(114, 114, 114, 255);
        Ink->AddLine(At(38, 39), At(38, 10), Stroke, 1.0f * Scale);
        Ink->AddLine(At(38, 39), At(11, 55), Stroke, 1.0f * Scale);
        Ink->AddLine(At(38, 39), At(65, 55), Stroke, 1.0f * Scale);
        Ink->AddCircle(At(38, 39), 3.0f * Unit, Stroke, 0, 1.0f * Scale);
        Style.Text(Ink, Style.Faces.Monospace, 10.0f, At(34, 0), IM_COL32(153, 153, 153, 255), "Y");
        Style.Text(Ink, Style.Faces.Monospace, 10.0f, At(3, 56), IM_COL32(153, 153, 153, 255), "X");
        Style.Text(Ink, Style.Faces.Monospace, 10.0f, At(64, 56), IM_COL32(160, 160, 160, 255), "Z");
    }
    Ink->PopClipRect();

    ConstructTransport(Ink, ImVec2(Minimum.x + 8.0f * Scale, PictureMax.y + 6.0f * Scale), ImVec2(Maximum.x - 8.0f * Scale, Maximum.y - 6.0f * Scale), Scale);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    POINTER
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorWorkspace::ConstructPointer(ImVec2 Minimum, ImVec2 Maximum)
{
    // 📝 The button lies under the tools, which are submitted after it and may overlap it.
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##FluidViewport", ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    Controls.push_back({ "viewport", Minimum, Maximum });
    const ImGuiIO& IO      = ImGui::GetIO();
    const bool     Hovered = ImGui::IsItemHovered();
    GasSettings&   Gas     = Record->Gas;

    // 📝 Hit: the point of the volume under the pointer (Camera.raycastVolumeUVW on the domain's box now), or none.
    const auto Hit = [&](ImVec2 Pointer, std::array<float, 3>& OutUvw) -> bool
    {
        const float Across = std::max(1.0f, Maximum.x - Minimum.x), Down = std::max(1.0f, Maximum.y - Minimum.y);
        std::array<float, 3> BoxMinimum{}, BoxMaximum{};
        ResolveBoundsBox(BoxMinimum, BoxMaximum);
        PyroOrbitCamera Aimed = Camera;
        Aimed.Aspect          = Across / Down;
        return Aimed.RaycastVolumeUvw((Pointer.x - Minimum.x) / Across * 2.0f - 1.0f, 1.0f - (Pointer.y - Minimum.y) / Down * 2.0f, BoxMinimum, BoxMaximum, OutUvw);
    };

    if (ImGui::IsItemActivated())
    {
        // 📝 pointerdown: the right or middle button pans, Shift + the left button is the brush, otherwise the chosen tool.
        const bool Secondary = IO.MouseDown[1] || IO.MouseDown[2];
        if (Secondary) DragMode = 2;
        else if (IO.KeyShift || std::strcmp(Gas.InteractionMode, "flamethrower") == 0) DragMode = 3;
        else if (std::strcmp(Gas.InteractionMode, "detonate") == 0) DragMode = 4;
        else DragMode = 1;
        DragHeld = Hit(IO.MousePos, DragHit);
        if (DragMode == 4)
        {
            if (DragHeld)
            {
                char Place[64];
                std::snprintf(Place, sizeof(Place), "burst-at:%.6f,%.6f,%.6f", DragHit[0], DragHit[1], DragHit[2]);
                Requests.push_back(Place);
            }
            else
                AppendLog("BURST", "Click inside the domain to place a burst.", {}, true);
        }
        if (DragMode == 3 && DragHeld)
        {
            Brush.Held        = true;
            Brush.PositionUvw = DragHit;
            Brush.Velocity    = { 0.0f, 3.0f, 0.0f };
        }
    }
    if (DragMode != 0 && ImGui::IsItemActive())
    {
        const ImVec2 Delta = IO.MouseDelta;
        if (DragMode == 1 && (Delta.x != 0.0f || Delta.y != 0.0f))
        {
            Camera.OrbitByPixels(Delta.x, Delta.y);
            Record->AdvanceRevision();
            View.clear();
            View = "perspective";
        }
        if (DragMode == 2 && (Delta.x != 0.0f || Delta.y != 0.0f))
        {
            Camera.PanByPixels(Delta.x, Delta.y);
            Record->AdvanceRevision();
        }
        if (DragMode == 3)
        {
            // 📝 pointermove: the brush's velocity is how far the point moved since the last move, times 55, with the 3 that lifts the flame in y.
            std::array<float, 3> Position{};
            if (Hit(IO.MousePos, Position))
            {
                Brush.Held        = true;
                Brush.PositionUvw = Position;
                for (size_t Axis = 0; Axis < 3; ++Axis)
                    Brush.Velocity[Axis] = ((DragHeld ? Position[Axis] - DragHit[Axis] : 0.0f)) * 55.0f + (Axis == 1 ? 3.0f : 0.0f);
                DragHit  = Position;
                DragHeld = true;
            }
            else
            {
                Brush.Held = false;
                DragHeld   = false;
            }
        }
    }
    else if (DragMode != 0)
    {
        // 📝 pointerup, pointercancel, lostpointercapture: the drag ends and the brush goes out.
        DragMode   = 0;
        Brush.Held = false;
    }
    // 📝 wheel: the browser's delta (a notch is 100, opposite to ImGui's sign), held to -100..100, zooms toward the aim.
    if (Hovered && IO.MouseWheel != 0.0f) Camera.ZoomByWheel(std::clamp(-IO.MouseWheel * 100.0f, -100.0f, 100.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorWorkspace::ConstructTransport(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look  Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    const auto& P = Style.Palette;
    const GasSettings& Gas = Record->Gas;
    const FluidVolumeReadout* Readout = ResolveReadout();
    const double Seconds = Readout != nullptr ? Readout->SimulationSeconds : 0.0;
    const float  Top     = Minimum.y;
    float        Cursor  = Minimum.x;
    float        Width   = 0.0f;

    // 📝 Row one: Restart (R), Play or Pause (Space), Step once (S), the timecode, the speed, then the burst.
    if (ConstructPillButton(Style, Ink, Controls, "restart", ImVec2(Cursor, Top), "Restart simulation \xC2\xB7 R", false, "RotateCcw", &Width, true, "Fluid")) Requests.push_back("restart");
    Cursor += Width + 4.0f * Scale;
    if (ConstructPillButton(Style, Ink, Controls, "play", ImVec2(Cursor, Top), Gas.Paused ? "Play" : "Pause", Gas.Paused, Gas.Paused ? "Play" : "Pause", &Width, false, "Fluid"))
        Requests.push_back("toggle-play");
    Cursor += Width + 4.0f * Scale;
    if (ConstructPillButton(Style, Ink, Controls, "step", ImVec2(Cursor, Top), "Step once \xC2\xB7 S", false, "ChevronRight", &Width, true, "Fluid")) Requests.push_back("step");
    Cursor += Width + 14.0f * Scale;
    const std::string Timecode = FluidEditorPages::FormatTimecode(Seconds);
    Style.Text(Ink, Style.Faces.Monospace, 13.0f, ImVec2(Cursor, Top + 7.0f * Scale), P.TextPrimary, Timecode.c_str());
    Cursor += Style.Measure(Style.Faces.Monospace, 13.0f, Timecode.c_str()) + 14.0f * Scale;
    Ink->AddLine(ImVec2(Cursor, Top + 5.0f * Scale), ImVec2(Cursor, Top + 25.0f * Scale), P.InsetFieldBorder, 1.0f);
    Cursor += 12.0f * Scale;

    // The burst controls are right-aligned; the speed and the live label take what room is left of them.
    const std::string BurstLabel   = ResolveBurstLabel(BurstKind);
    const float       TriggerWidth = MeasurePillWidth(Style, "Trigger  D", true, false);
    const float       KindWidth    = MeasurePillWidth(Style, BurstLabel.c_str(), false, false);
    float             RightEdge    = Maximum.x - TriggerWidth;
    if (RightEdge - KindWidth - 6.0f * Scale >= Cursor)
    {
        if (ConstructPillButton(Style, Ink, Controls, "trigger", ImVec2(RightEdge, Top), "Trigger  D", false, "Blast", &Width, false, "Fluid")) Requests.push_back("burst");
        RightEdge -= KindWidth + 6.0f * Scale;
        if (ConstructPillButton(Style, Ink, Controls, "burst-kind", ImVec2(RightEdge, Top), BurstLabel.c_str(), false, nullptr, &Width, false, "Fluid"))
            Requests.push_back(std::string("burst-kind:") + ResolveNextBurst(BurstKind));
    }
    else
        RightEdge = Maximum.x;
    static constexpr float Speeds[4]        = { 0.25f, 0.5f, 1.0f, 2.0f };
    static constexpr const char* SpeedWords[4] = { "0.25\xC3\x97", "0.5\xC3\x97", "1.0\xC3\x97", "2.0\xC3\x97" };
    float NeededSpeed = 0.0f;
    for (const char* Words : SpeedWords) NeededSpeed += MeasurePillWidth(Style, Words, false, false) + 3.0f * Scale;
    if (Cursor + NeededSpeed <= RightEdge - 8.0f * Scale)
    {
        for (int Index = 0; Index < 4; ++Index)
        {
            char Control[24];
            std::snprintf(Control, sizeof(Control), "speed:%g", Speeds[Index]);
            if (ConstructPillButton(Style, Ink, Controls, Control, ImVec2(Cursor, Top), SpeedWords[Index], std::fabs(Gas.TimeScale - Speeds[Index]) < 1.0e-4f, nullptr, &Width, false, "Fluid"))
                Requests.push_back(std::string("speed:") + std::to_string(Speeds[Index]));
            Cursor += Width + 3.0f * Scale;
        }
    }
    else
    {
        char Words[24];
        std::snprintf(Words, sizeof(Words), "%.2f\xC3\x97", Gas.TimeScale);
        if (ConstructPillButton(Style, Ink, Controls, "speed", ImVec2(Cursor, Top), Words, false, nullptr, &Width, false, "Fluid"))
        {
            const float Next = Gas.TimeScale < 0.3f ? 0.5f : Gas.TimeScale < 0.75f ? 1.0f : Gas.TimeScale < 1.5f ? 2.0f : 0.25f;
            Requests.push_back(std::string("speed:") + std::to_string(Next));
        }
        Cursor += Width + 3.0f * Scale;
    }
    Cursor += 11.0f * Scale;
    const char* Liveness = "Live / uncached";
    if (Cursor + Style.Measure(Style.Faces.Regular, 10.0f, Liveness) + 16.0f * Scale <= RightEdge - 8.0f * Scale)
    {
        Ink->AddCircleFilled(ImVec2(Cursor + 4.0f * Scale, Top + 15.0f * Scale), 3.0f * Scale, P.LiveGreen);
        Style.Text(Ink, Style.Faces.Regular, 10.0f, ImVec2(Cursor + 14.0f * Scale, Top + 9.5f * Scale), P.TextSecondary, Liveness);
    }

    // 📝 Row two: the simulation's clock on a 30 s ruler (the playhead wraps, the numbers move on by 30), and what feeds it.
    const float RowTwo = Top + 42.0f * Scale;
    char        Frames[32];
    std::snprintf(Frames, sizeof(Frames), "FRAME %04u", Readout != nullptr ? Readout->Steps : 0u);
    Style.Text(Ink, Style.Faces.SemiBold, 9.0f, ImVec2(Minimum.x, RowTwo), P.TextMuted, "SIMULATION");
    Style.Text(Ink, Style.Faces.Monospace, 9.0f, ImVec2(Minimum.x + Style.Measure(Style.Faces.SemiBold, 9.0f, "SIMULATION") + 12.0f * Scale, RowTwo), P.TextMuted, Frames);
    const float RulerLeft  = Minimum.x;
    const float RulerRight = Maximum.x;
    const float Origin     = std::floor(Seconds / 30.0) * 30.0f;
    for (int Mark = 0; Mark < 7; ++Mark)
    {
        char        Number[16];
        std::snprintf(Number, sizeof(Number), "%ds", static_cast<int>(Origin) + Mark * 5);
        const float Along = RulerLeft + (RulerRight - RulerLeft) * static_cast<float>(Mark) / 6.0f;
        const float W     = Style.Measure(Style.Faces.Monospace, 8.5f, Number);
        const float TextX = Mark == 0 ? Along : Mark == 6 ? Along - W : Along - W * 0.5f;
        Style.Text(Ink, Style.Faces.Monospace, 8.5f, ImVec2(TextX, RowTwo + 14.0f * Scale), P.TextMuted, Number);
        Ink->AddLine(ImVec2(Along, RowTwo + 26.0f * Scale), ImVec2(Along, RowTwo + 31.0f * Scale), P.InsetFieldBorder, 1.0f);
    }
    const float TrackY = RowTwo + 33.0f * Scale;
    Ink->AddRectFilled(ImVec2(RulerLeft, TrackY), ImVec2(RulerRight, TrackY + 14.0f * Scale), P.CountCardBg, 4.0f * Scale);
    Ink->AddRect(ImVec2(RulerLeft, TrackY), ImVec2(RulerRight, TrackY + 14.0f * Scale), P.CountCardBorder, 4.0f * Scale);
    Style.Text(Ink, Style.Faces.Regular, 8.5f, ImVec2(RulerLeft + 8.0f * Scale, TrackY + 2.0f * Scale), P.TextSecondary, Gas.EmitterEnabled ? "Continuous emission" : "Burst-only simulation");
    const float Playhead = RulerLeft + (RulerRight - RulerLeft) * static_cast<float>(std::fmod(Seconds, 30.0) / 30.0);
    Ink->AddLine(ImVec2(Playhead, RowTwo + 12.0f * Scale), ImVec2(Playhead, TrackY + 16.0f * Scale), FireTint, 1.5f * Scale);
    Ink->AddTriangleFilled(ImVec2(Playhead - 4.0f * Scale, RowTwo + 12.0f * Scale), ImVec2(Playhead + 4.0f * Scale, RowTwo + 12.0f * Scale), ImVec2(Playhead, RowTwo + 18.0f * Scale), FireTint);
    Controls.push_back({ "ruler", ImVec2(RulerLeft, RowTwo + 12.0f * Scale), ImVec2(RulerRight, TrackY + 16.0f * Scale) });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRESET LIBRARY
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorWorkspace::ConstructLibrarySheet(ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look  Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    const auto& P = Style.Palette;
    LibraryCards  = 0;
    LibraryMarked = Record->Preset;
    // 📝 The mark on a card, a green dot and its words at the card's right edge (the dot is drawn: DM Sans has no ●).
    const auto ConstructMark = [&](ImDrawList* Sheet, ImVec2 Corner, const char* Words) {
        const float Right = Corner.x - 12.0f * Scale;
        const float Wide  = Style.Measure(Style.Faces.Medium, 9.5f, Words);
        Sheet->AddCircleFilled(ImVec2(Right - Wide - 8.0f * Scale, Corner.y + 6.0f * Scale), 3.0f * Scale, P.LiveGreen);
        Style.Text(Sheet, Style.Faces.Medium, 9.5f, ImVec2(Right - Wide, Corner.y), P.LiveGreen, Words);
    };
    // A child over the whole editor, created last so it draws above the columns and takes the pointer from them.
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##FluidLibraryVeil", ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(Minimum, Maximum, IM_COL32(4, 5, 8, 175), 14.0f * Scale);
        const float  SheetW = std::min(980.0f * Scale, Maximum.x - Minimum.x - 80.0f * Scale);
        const float  SheetH = std::min(680.0f * Scale, Maximum.y - Minimum.y - 60.0f * Scale);
        const ImVec2 SheetMin((Minimum.x + Maximum.x - SheetW) / 2.0f, (Minimum.y + Maximum.y - SheetH) / 2.0f);
        const ImVec2 SheetMax(SheetMin.x + SheetW, SheetMin.y + SheetH);
        // 📝 A click on the veil outside the sheet closes the library. The button lies under every control of the sheet, which are submitted after it: it
        //    allows them to overlap it, or it would keep the pointer from them.
        ImGui::SetCursorScreenPos(Minimum);
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("##FluidLibraryOutside", ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y)))
        {
            const ImVec2 Pointer = ImGui::GetIO().MousePos;
            if (Pointer.x < SheetMin.x || Pointer.x > SheetMax.x || Pointer.y < SheetMin.y || Pointer.y > SheetMax.y) LibraryShown = false;
        }
        Controls.push_back({ "library-veil", Minimum, Maximum });
        Controls.push_back({ "library-sheet", SheetMin, SheetMax });
        Ink->AddRectFilled(SheetMin, SheetMax, P.ColumnBg, 16.0f * Scale);
        Ink->AddRect(SheetMin, SheetMax, P.EdgeBorder, 16.0f * Scale);
        const float Left = SheetMin.x + 22.0f * Scale;
        Style.Text(Ink, Style.Faces.Medium, 20.0f, ImVec2(Left, SheetMin.y + 18.0f * Scale), P.TextPrimary, "Preset library");
        char Subtitle[128];
        std::snprintf(Subtitle, sizeof(Subtitle), "%u effects \xC2\xB7 the prototype's, unchanged \xC2\xB7 each one sets all 85 settings", FluidPresetCardCount);
        Style.Text(Ink, Style.Faces.Regular, 11.0f, ImVec2(Left, SheetMin.y + 46.0f * Scale), P.TextSecondary, Subtitle);
        float Width = 0.0f;
        if (ConstructPillButton(Style, Ink, Controls, "library-close", ImVec2(SheetMax.x - 120.0f * Scale, SheetMin.y + 18.0f * Scale), "Close (Esc)", false, "X", &Width, false, "Fluid"))
            LibraryShown = false;
        // 📝 The search field in the inspector's inset look.
        ImGui::SetCursorScreenPos(ImVec2(SheetMax.x - 420.0f * Scale, SheetMin.y + 18.0f * Scale));
        ImGui::SetNextItemWidth(280.0f * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * Scale, 8.0f * Scale));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(P.InsetFieldBg));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(P.InsetFieldBg));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::ColorConvertU32ToFloat4(P.InsetFieldBg));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(P.InsetFieldBorder));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(P.TextPrimary));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(P.TextMuted));
        ImGui::PushFont(Style.Pick(Style.Faces.Regular), 11.5f * Scale);
        ImGui::InputTextWithHint("##FluidLibrarySearch", "Find an effect\xE2\x80\xA6", LibraryQuery, sizeof(LibraryQuery));
        Controls.push_back({ "library-search", ImGui::GetItemRectMin(), ImGui::GetItemRectMax() });
        ImGui::PopFont();
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);

        // 📝 The category chips (All, Fire, Smoke, Blast).
        float       ChipX = Left;
        const float ChipY = SheetMin.y + 78.0f * Scale;
        for (const auto& [Key, Label] : { std::pair<const char*, const char*>{ "all", "All" }, { "fire", "Fire" }, { "smoke", "Smoke" }, { "blast", "Blast" } })
        {
            const float W = Style.Measure(Style.Faces.Medium, 10.5f, Label) + 22.0f * Scale;
            char        Control[40];
            std::snprintf(Control, sizeof(Control), "library-group:%s", Key);
            if (Press(Control, ImVec2(ChipX, ChipY), ImVec2(W, 26.0f * Scale))) LibraryGroup = Key;
            const bool Active = LibraryGroup == Key;
            Ink->AddRectFilled(ImVec2(ChipX, ChipY), ImVec2(ChipX + W, ChipY + 26.0f * Scale), Active ? P.SegmentedActiveBg : P.CountCardBg, 13.0f * Scale);
            Style.Text(Ink, Style.Faces.Medium, 10.5f, ImVec2(ChipX + 11.0f * Scale, ChipY + 6.0f * Scale), Active ? P.TextPrimary : P.TextSecondary, Label);
            ChipX += W + 6.0f * Scale;
        }

        // 📝 The cards scroll in their own child.
        const ImVec2 CardsMin(Left, ChipY + 40.0f * Scale);
        const ImVec2 CardsMax(SheetMax.x - 22.0f * Scale, SheetMax.y - 16.0f * Scale);
        ImGui::SetCursorScreenPos(CardsMin);
        if (ImGui::BeginChild("##FluidLibraryCards", ImVec2(CardsMax.x - CardsMin.x, CardsMax.y - CardsMin.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
        {
            ImDrawList*       Sheet  = ImGui::GetWindowDrawList();
            const ImVec2      Origin = ImGui::GetCursorScreenPos();
            const float       CardW  = (CardsMax.x - CardsMin.x - 20.0f * Scale) / 3.0f;
            const float       CardH  = 112.0f * Scale;
            const std::string Query  = Lowered(LibraryQuery);
            int               Shown  = 0;
            const PresetCard* Cards  = FluidPresetCards();
            for (uint32_t Index = 0; Index < FluidPresetCardCount; ++Index)
            {
                const PresetCard& Card   = Cards[Index];
                const GasPreset*  Preset = FindGasPreset(Card.Identity);
                if (LibraryGroup != "all" && LibraryGroup != Card.Category) continue;
                const std::string Searched = std::string(Card.Name) + " " + Card.Category + " " + Card.Summary + " " + (Preset != nullptr ? Preset->Description : "");
                if (!Query.empty() && Lowered(Searched).find(Query) == std::string::npos) continue;
                const int    Column = Shown % 3, Line = Shown / 3;
                const ImVec2 CardMin(Origin.x + Column * (CardW + 10.0f * Scale), Origin.y + Line * (CardH + 10.0f * Scale));
                const ImVec2 CardMax(CardMin.x + CardW, CardMin.y + CardH);
                ImGui::SetCursorScreenPos(CardMin);
                ImGui::PushID(Card.Identity);
                const bool Pressed = ImGui::InvisibleButton("##Card", ImVec2(CardW, CardH));
                const bool Hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                Controls.push_back({ std::string("library-card:") + Card.Identity, CardMin, CardMax });
                if (Pressed) Requests.push_back(std::string("preset:") + Card.Identity);
                Sheet->AddRectFilled(CardMin, CardMax, Hovered ? P.RowHoverBg : P.CountCardBg, 12.0f * Scale);
                Sheet->AddRect(CardMin, CardMax, Hovered ? P.RowSelectedBorder : P.CountCardBorder, 12.0f * Scale);
                const ImU32 Tint = ResolveCategoryTint(Card.Category);
                Sheet->AddRectFilled(ImVec2(CardMin.x + 12.0f * Scale, CardMin.y + 12.0f * Scale), ImVec2(CardMin.x + 38.0f * Scale, CardMin.y + 38.0f * Scale), (Tint & 0x00FFFFFFu) | (64u << 24),
                                     8.0f * Scale);
                GlyphSymbol::Construct(Sheet, ResolveCategoryGlyph(Card.Glyph), ImVec2(CardMin.x + 17.0f * Scale, CardMin.y + 17.0f * Scale), 16.0f * Scale, 1.4f, Tint);
                Style.Text(Sheet, Style.Faces.Medium, 12.5f, ImVec2(CardMin.x + 48.0f * Scale, CardMin.y + 11.0f * Scale), P.TextPrimary, Card.Name);
                Style.Text(Sheet, Style.Faces.Regular, 10.0f, ImVec2(CardMin.x + 48.0f * Scale, CardMin.y + 28.0f * Scale), P.TextSecondary, Card.Summary);
                if (Record->Preset == Card.Identity) ConstructMark(Sheet, ImVec2(CardMax.x, CardMin.y + 14.0f * Scale), "Applied");
                if (Preset != nullptr)
                {
                    Sheet->PushClipRect(ImVec2(CardMin.x + 12.0f * Scale, CardMin.y + 46.0f * Scale), ImVec2(CardMax.x - 10.0f * Scale, CardMax.y - 8.0f * Scale), true);
                    Sheet->AddText(Style.Pick(Style.Faces.Regular), 10.0f * Scale, ImVec2(CardMin.x + 12.0f * Scale, CardMin.y + 48.0f * Scale), P.TextMuted, Preset->Description, nullptr,
                                   CardW - 24.0f * Scale);
                    Sheet->PopClipRect();
                }
                ++Shown;
            }
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + ((Shown + 2) / 3) * (CardH + 10.0f * Scale)));
            LibraryCards = static_cast<size_t>(Shown);
            if (Shown == 0) Style.Text(Sheet, Style.Faces.Regular, 11.0f, ImVec2(Origin.x + 4.0f * Scale, Origin.y + 8.0f * Scale), P.TextMuted, "No matching effects.");
            ImGui::Dummy(ImVec2(1.0f, 4.0f * Scale));
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

} // namespace Frontier

#endif
