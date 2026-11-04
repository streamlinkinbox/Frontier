/*==========================================================================================================================================
                                                           WORLDVECTOROVERLAY.CPP
==========================================================================================================================================*/
// 🧩 World-anchored velocity glyphs with perspective depth, viewport clipping and speed colouring.

#include "Engine/Interface/Controls/WorldVectorOverlay.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

size_t WorldVectorOverlay::Construct(ImDrawList                        *Ink,
                                     ImVec2                             Minimum,
                                     ImVec2                             Extent,
                                     const WorldVectorView             &View,
                                     std::span<const FieldVectorSample> Samples,
                                     double                             Seconds,
                                     float                              InterfaceScale)
{
    if (!Ink || Extent.x <= 0 || Extent.y <= 0 || !std::isfinite(Seconds)) return 0;
    const double Tangent = std::tan(std::clamp(View.VerticalFieldOfView, .05, 3.0) * .5);
    const double Aspect  = Extent.x / Extent.y;
    auto         Project = [&](const std::array<double, 3> &Point, ImVec2 &Screen)
    {
        double Depth = 0, Across = 0, Vertical = 0;
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            const double Relative = Point[Axis] - View.Eye[Axis];
            Depth += Relative * View.Forward[Axis];
            Across += Relative * View.Right[Axis];
            Vertical += Relative * View.Up[Axis];
        }
        if (!std::isfinite(Depth) || Depth < .1) return false;
        Across /= Depth * Tangent * Aspect;
        Vertical /= Depth * Tangent;
        if (!std::isfinite(Across) || !std::isfinite(Vertical) || std::abs(Across) > 8 || std::abs(Vertical) > 8) return false;
        Screen = {Minimum.x + float(.5 + Across * .5) * Extent.x, Minimum.y + float(.5 - Vertical * .5) * Extent.y};
        return true;
    };
    size_t Drawn = 0;
    Ink->PushClipRect(Minimum, {Minimum.x + Extent.x, Minimum.y + Extent.y}, true);
    for (const auto &Sample : Samples)
    {
        const double Speed = std::hypot(Sample.Velocity[0], Sample.Velocity[1], Sample.Velocity[2]);
        if (!std::isfinite(Speed) || Speed < .001) continue;
        auto End = Sample.Position;
        for (size_t Axis = 0; Axis < 3; ++Axis)
            End[Axis] += Sample.Velocity[Axis] * Seconds;
        ImVec2 First, Last;
        if (!Project(Sample.Position, First) || !Project(End, Last)) continue;
        const float Along = Last.x - First.x, Across = Last.y - First.y;
        const float Length = std::hypot(Along, Across);
        if (Length < 1) continue;
        const float Mix    = static_cast<float>(std::clamp(Speed / 20, 0.0, 1.0));
        const ImU32 Colour = IM_COL32(100 + int(150 * Mix), 220 - int(70 * Mix), 240 - int(160 * Mix), 210);
        Ink->AddLine(First, Last, Colour, 1.4f * InterfaceScale);
        const float Head       = std::min(7 * InterfaceScale, Length * .35f);
        const float DirectionX = Along / Length, DirectionY = Across / Length;
        Ink->AddTriangleFilled(
            Last, {Last.x - DirectionX * Head - DirectionY * Head * .45f, Last.y - DirectionY * Head + DirectionX * Head * .45f},
            {Last.x - DirectionX * Head + DirectionY * Head * .45f, Last.y - DirectionY * Head - DirectionX * Head * .45f}, Colour);
        ++Drawn;
    }
    Ink->PopClipRect();
    return Drawn;
}

} // namespace Frontier
