//============================================================================================================================================
//                                                      PIXELSPACE.CPP
//============================================================================================================================================
// 🧩 The only translation unit in DisplayPresentation that spells ImGui. Everything above hands in pixels and colours.

#include "PixelSpace.h"

#include <algorithm>

#include <imgui.h>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace {

ImDrawList* List(void* Slot) noexcept
{
    return static_cast<ImDrawList*>(Slot);
}

ImU32 Pack(ColorQuad Colour) noexcept
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(Colour.Red, Colour.Green, Colour.Blue, Colour.Alpha));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

PixelSpace::PixelSpace() noexcept
    : Commands(nullptr)
    , DisplayWidth(0.0f)
    , Scale(1.0f)
    , DisplayHeight(0.0f)
    , TypefaceStack{}
    , TypefaceDepth(0u)
{
}

bool PixelSpace::Begin(SurfaceLayer Layer, float InDisplayWidth, float InDisplayHeight, float InterfaceScale) noexcept
{
    Scale         = InterfaceScale > 0.05f ? InterfaceScale : 1.0f;
    DisplayWidth  = InDisplayWidth  / Scale;
    DisplayHeight = InDisplayHeight / Scale;
    TypefaceDepth = 0u;

    if (ImGui::GetCurrentContext() == nullptr)
    {
        Commands = nullptr;
        return false;
    }

    // The foreground list sits in front of every ImGui window, so the notch and its shade cover the
    //    project's own panels when pulled down — exactly what a system overlay should do.
    Commands = (Layer == SurfaceLayer::Above)
             ? static_cast<void*>(ImGui::GetForegroundDrawList())
             : static_cast<void*>(ImGui::GetBackgroundDrawList());
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

void PixelSpace::FillRectangle(const PlaneExtent& Extent, ColorQuad Colour, float Radius) noexcept
{
    if (!Commands) return;
    List(Commands)->AddRectFilled(ImVec2(Extent.MinimumX * Scale, Extent.MinimumY * Scale),
                                  ImVec2(Extent.MaximumX * Scale, Extent.MaximumY * Scale),
                                  Pack(Colour), Radius * Scale);
}

void PixelSpace::FillRectangleBottomRounded(const PlaneExtent& Extent, ColorQuad Colour, float Radius) noexcept
{
    if (!Commands) return;
    List(Commands)->AddRectFilled(ImVec2(Extent.MinimumX * Scale, Extent.MinimumY * Scale),
                                  ImVec2(Extent.MaximumX * Scale, Extent.MaximumY * Scale),
                                  Pack(Colour), Radius * Scale, ImDrawFlags_RoundCornersBottom);
}

void PixelSpace::FillPolygon(const PlanePoint* Points, uint32_t PointCount, ColorQuad Colour) noexcept
{
    if (!Commands || PointCount < 3u) return;

    // ImGui's AddConvexPolyFilled produces artefacts on concave outlines; the notch is concave (it narrows
    //    toward the bottom), so the concave-capable path is used. It expects ImVec2 storage.
    std::vector<ImVec2> Converted;
    Converted.reserve(PointCount);
    for (uint32_t Index = 0u; Index < PointCount; ++Index)
        Converted.emplace_back(Points[Index].X * Scale, Points[Index].Y * Scale);

#if IMGUI_VERSION_NUM >= 19100
    List(Commands)->AddConcavePolyFilled(Converted.data(), static_cast<int>(Converted.size()), Pack(Colour));
#else
    List(Commands)->AddConvexPolyFilled(Converted.data(), static_cast<int>(Converted.size()), Pack(Colour));
#endif
}

void PixelSpace::StrokePolyline(const PlanePoint* Points, uint32_t PointCount, ColorQuad Colour, float Thickness, bool Closed) noexcept
{
    if (!Commands || PointCount < 2u) return;

    std::vector<ImVec2> Converted;
    Converted.reserve(PointCount);
    for (uint32_t Index = 0u; Index < PointCount; ++Index)
        Converted.emplace_back(Points[Index].X * Scale, Points[Index].Y * Scale);

    List(Commands)->AddPolyline(Converted.data(), static_cast<int>(Converted.size()), Pack(Colour),
                                Closed ? ImDrawFlags_Closed : ImDrawFlags_None, Thickness * Scale);
}

void PixelSpace::Text(float X, float Y, ColorQuad Colour, const char* Utf8, float FontSizePixels) noexcept
{
    if (!Commands || !Utf8) return;
    ImFont* Font = QueryTypeface() ? static_cast<ImFont*>(QueryTypeface()) : ImGui::GetFont();
    const float Size = FontSizePixels > 0.0f ? FontSizePixels : ImGui::GetFontSize();
    List(Commands)->AddText(Font, Size * Scale, ImVec2(X * Scale, Y * Scale), Pack(Colour), Utf8);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CLIPPING
//------------------------------------------------------------------------------------------------------------------------

void PixelSpace::PushClip(const PlaneExtent& Extent) noexcept
{
    if (!Commands) return;
    List(Commands)->PushClipRect(ImVec2(Extent.MinimumX * Scale, Extent.MinimumY * Scale), ImVec2(Extent.MaximumX * Scale, Extent.MaximumY * Scale), true);
}

void PixelSpace::PopClip() noexcept
{
    if (!Commands) return;
    List(Commands)->PopClipRect();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TYPEFACE
//------------------------------------------------------------------------------------------------------------------------

void PixelSpace::PushTypeface(void* FaceHandle) noexcept
{
    if (TypefaceDepth < 8u) TypefaceStack[TypefaceDepth] = FaceHandle;
    ++TypefaceDepth;   // over-deep pushes are counted so the matching pops balance
}

void PixelSpace::PopTypeface() noexcept
{
    if (TypefaceDepth > 0u) --TypefaceDepth;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        GROUPS
//------------------------------------------------------------------------------------------------------------------------

uint32_t PixelSpace::BeginGroup() const noexcept
{
    if (!Commands) return 0u;
    return static_cast<uint32_t>(List(Commands)->VtxBuffer.Size);
}

void PixelSpace::EndGroup(uint32_t Mark, float OffsetX, float OffsetY, float GroupScale, float PivotX, float PivotY, float Alpha) noexcept
{
    if (!Commands) return;
    ImDrawList* Draw = List(Commands);
    const int End = Draw->VtxBuffer.Size;
    const float A = std::clamp(Alpha, 0.0f, 1.0f);
    // Vertices are already physical; the group parameters arrive in logical pixels.
    const float Px = PivotX * this->Scale, Py = PivotY * this->Scale, Ox = OffsetX * this->Scale, Oy = OffsetY * this->Scale;
    for (int Index = static_cast<int>(Mark); Index < End; ++Index)
    {
        ImDrawVert& Vertex = Draw->VtxBuffer[Index];
        Vertex.pos.x = Px + (Vertex.pos.x - Px) * GroupScale + Ox;
        Vertex.pos.y = Py + (Vertex.pos.y - Py) * GroupScale + Oy;
        if (A < 1.0f)
        {
            const ImU32 Colour = Vertex.col;
            const ImU32 Faded  = static_cast<ImU32>(static_cast<float>((Colour >> IM_COL32_A_SHIFT) & 0xFFu) * A + 0.5f);
            Vertex.col = (Colour & ~IM_COL32_A_MASK) | (Faded << IM_COL32_A_SHIFT);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MEASUREMENT
//------------------------------------------------------------------------------------------------------------------------

PlanePoint PixelSpace::MeasureText(const char* Utf8, float FontSizePixels) const noexcept
{
    if (ImGui::GetCurrentContext() == nullptr || !Utf8) return {};
    ImFont* Font = QueryTypeface() ? static_cast<ImFont*>(QueryTypeface()) : ImGui::GetFont();
    const float Size = FontSizePixels > 0.0f ? FontSizePixels : ImGui::GetFontSize();
    const ImVec2 Measured = Font->CalcTextSizeA(Size * Scale, FLT_MAX, 0.0f, Utf8);
    return PlanePoint{ Measured.x / Scale, Measured.y / Scale };
}

} // namespace Frontier
