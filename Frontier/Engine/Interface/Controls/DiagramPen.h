/*==========================================================================================================================================
                                                                DIAGRAMPEN.H
==========================================================================================================================================*/
// 🧩 The demo's canvas Pen (NativeEnvironmentControls.js) over one diagram: logical W×H coordinates mapped onto the
//    diagram's screen rectangle; text sizes are logical canvas pixels and text sits on its alphabetic baseline.

#pragma once

#include <imgui.h>

#include <vector>

namespace Frontier
{

struct EditorTypefaces;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIAGRAM PEN
//------------------------------------------------------------------------------------------------------------------------

class DiagramPen
{
  public:
    DiagramPen() = default;
    DiagramPen(ImDrawList* InDrawList, const EditorTypefaces* InFaces, ImVec2 InOrigin, float InScale, float InLogicalWidth,
               float InLogicalHeight) noexcept;

    void Line(ImVec2 From, ImVec2 To, ImU32 Colour, float Width = 1.0f, float DashOn = 0.0f, float DashOff = 0.0f) const;
    void Path(const std::vector<ImVec2>& Points, ImU32 Colour, float Width = 1.0f, float DashOn = 0.0f, float DashOff = 0.0f) const;
    void Circle(ImVec2 Centre, float Radius, ImU32 Fill, ImU32 Stroke = 0) const;
    void Ellipse(ImVec2 Centre, float RadiusX, float RadiusY, ImU32 Fill, ImU32 Stroke = 0) const;
    void Text(ImVec2 Baseline, const char* Caption, ImU32 Colour, float Size = 10.0f, int Alignment = 0) const;   // -1 left, 0 centre, 1 right
    void Rect(ImVec2 Min, ImVec2 Size, ImU32 Fill, float Radius = 0.0f) const;
    // 📝 createRadialGradient(0 → R): rgba(Rgb, Alpha) at the centre fading to transparent at Radius.
    void Glow(ImVec2 Centre, float Radius, ImU32 Rgb, float Alpha) const;
    // 📝 The area between a curve and the level Floor, coloured per curve point (the spectrum's gradient fill).
    void ShadedArea(const std::vector<ImVec2>& Curve, float Floor, const std::vector<ImU32>& Colours) const;
    // 📝 A horizontal multi-stop gradient bar (`X.rect` with a createLinearGradient fill), square-ended.
    void GradientBar(ImVec2 Min, ImVec2 Size, const std::vector<ImU32>& Stops) const;

    [[nodiscard]] ImVec2 QueryScreen(ImVec2 Logical) const noexcept;
    [[nodiscard]] ImVec2 QueryLogical(ImVec2 Screen) const noexcept;
    [[nodiscard]] float  QueryScale() const noexcept { return Scale; }
    [[nodiscard]] ImDrawList* QueryDrawList() const noexcept { return DrawList; }

    // 📝 A triangle fan from Centre (CentreColour) to an ellipse ring (RimColour): CSS radial gradients' linear stops.
    static void RasterizeRadialFan(ImDrawList* DrawList, ImVec2 Centre, ImVec2 Radius, ImU32 CentreColour, ImU32 RimColour,
                                   int Segments = 48);
    static void RasterizeDashedSegment(ImDrawList* DrawList, ImVec2 From, ImVec2 To, ImU32 Colour, float Width, float DashOn,
                                       float DashOff);

  private:
    ImDrawList*            DrawList      = nullptr;
    const EditorTypefaces* Faces         = nullptr;
    ImVec2                 Origin        = ImVec2(0.0f, 0.0f);
    float                  Scale         = 1.0f;   // [screen px / logical px]
    float                  LogicalWidth  = 640.0f;
    float                  LogicalHeight = 420.0f;
};

} // namespace Frontier
