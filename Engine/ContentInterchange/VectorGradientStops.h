/*============================================================================================================================================
                                                       VECTORGRADIENTSTOPS.H
============================================================================================================================================*/
// 🧩 Shared CPU-side helpers for reading/writing a shape's up-to-4-stop gradient ramp (Engine/Render/VectorCoverage.h's
// GradientStopPositions/GradientStopColour2/GradientStopColour3), so StampWorkspacePanel and PatternWorkspacePanel
// author identical, already-validated stop layouts instead of hand-rolling the packed-slot bookkeeping twice.

#pragma once
#include "VectorAuthoringSelectionEditing.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier
{
inline constexpr int VectorGradientStopCapacity = 4;
inline constexpr int VectorFreeformPointCapacity = 8;

enum class VectorGradientInterpolation : uint32_t
{
    Linear = 0,
    Hermite = 1,
    CatmullRom = 2,
    BSpline = 3,
    Step = 4
};

struct VectorGradientStop
{
    float Position = 0.f;
    std::array<float, 4> Colour{1, 1, 1, 1};
};

struct VectorFreeformGradientPoint
{
    std::array<float, 2> Position{};
    std::array<float, 4> Colour{1, 1, 1, 1};
};

// 📝 Unpacks the instruction's 4 fixed slots (Colour, ColourEnd, GradientStopColour2/3) into an ordinary,
// already-ascending list — stopping at the first inactive (-1) position, per the struct's documented contract.
inline std::vector<VectorGradientStop> QueryVectorGradientStops(const VectorCoverageInstruction &Source)
{
    const std::array<std::array<float, 4>, 4> Colours{Source.Colour, Source.ColourEnd, Source.GradientStopColour2, Source.GradientStopColour3};
    std::vector<VectorGradientStop> Stops;
    for (int Index = 0; Index < VectorGradientStopCapacity; ++Index)
    {
        const float Position = Source.GradientStopPositions[Index];
        if (Position < 0.f) break;
        Stops.push_back({Position, Colours[Index]});
    }
    if (Stops.size() < 2) Stops = {{0.f, Source.Colour}, {1.f, Source.ColourEnd}};
    return Stops;
}

// 📝 Packs a 2–4 entry, ascending-by-Position stop list back into every slot-shaped patch field a layout
// change can touch. Callers own sorting/clamping/count limits beforehand (see StampWorkspacePanel.cpp's
// gradient marker editing for the add/remove/drag call sites).
inline void ApplyVectorGradientStops(VectorAuthoringSourcePatch &Patch, const std::vector<VectorGradientStop> &Stops)
{
    std::array<float, 4> Positions{-1, -1, -1, -1};
    std::array<std::array<float, 4>, 4> Colours{std::array<float, 4>{1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}};
    const size_t Count = std::clamp<size_t>(Stops.size(), 2, VectorGradientStopCapacity);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        Positions[Index] = std::clamp(Stops[Index].Position, 0.f, 1.f);
        Colours[Index] = Stops[Index].Colour;
    }
    Patch.GradientStopPositions = Positions;
    Patch.Colour = Colours[0];
    Patch.ColourEnd = Colours[1];
    Patch.GradientStopColour2 = Colours[2];
    Patch.GradientStopColour3 = Colours[3];
}

inline uint32_t QueryVectorFillInterpolation(const VectorCoverageInstruction &Source) noexcept
{
    return uint32_t(std::clamp(int(std::lround(Source.ShapeParameters[2])), 0, 4));
}

inline uint32_t QueryVectorStrokeInterpolation(const VectorCoverageInstruction &Source) noexcept
{
    return uint32_t(std::clamp(int(std::lround(Source.ShapeParameters[3])), 0, 4));
}

// The stroke ramp has independent slots but uses the same editor and interpolation rules.
inline VectorCoverageInstruction QueryVectorStrokeGradientSource(const VectorCoverageInstruction &Source)
{
    auto Ramp = Source;
    Ramp.Colour = (Source.Topology[3] & VectorCoverageStrokeColourExplicit) ? Source.StrokeColour : Source.Colour;
    Ramp.ColourEnd = Source.StrokeColourEnd;
    Ramp.GradientStopPositions = Source.StrokeGradientStopPositions;
    Ramp.GradientStopColour2 = Source.StrokeGradientStopColour2;
    Ramp.GradientStopColour3 = Source.StrokeGradientStopColour3;
    Ramp.GradientAxis = Source.StrokeGradientAxis;
    Ramp.Transform[3] = Source.ShapeParameters[1];
    Ramp.ShapeParameters[2] = Source.ShapeParameters[3];
    return Ramp;
}
inline std::vector<VectorGradientStop> QueryVectorStrokeGradientStops(const VectorCoverageInstruction &Source)
{
    return QueryVectorGradientStops(QueryVectorStrokeGradientSource(Source));
}
inline void ApplyVectorStrokeGradientStops(VectorAuthoringSourcePatch &Patch, const std::vector<VectorGradientStop> &Stops)
{
    VectorAuthoringSourcePatch Ramp;
    ApplyVectorGradientStops(Ramp, Stops);
    Patch.StrokeColour = Ramp.Colour; Patch.StrokeColourEnd = Ramp.ColourEnd;
    Patch.StrokeGradientStopPositions = Ramp.GradientStopPositions;
    Patch.StrokeGradientStopColour2 = Ramp.GradientStopColour2; Patch.StrokeGradientStopColour3 = Ramp.GradientStopColour3;
}

inline std::array<float, 4> InterpolateVectorSplineSegment(
    const std::array<float, 4> &C0, const std::array<float, 4> &C1,
    const std::array<float, 4> &C2, const std::array<float, 4> &C3, float Local, uint32_t Mode) noexcept
{
    if (Mode == 4) return Local < .5f ? C1 : C2;
    std::array<float, 4> Result{};
    if (Mode == 1)
    {
        const float Smooth = Local * Local * (3.f - 2.f * Local);
        for (int Channel = 0; Channel < 4; ++Channel) Result[Channel] = C1[Channel] + (C2[Channel] - C1[Channel]) * Smooth;
        return Result;
    }
    if (Mode == 2)
    {
        const float Local2 = Local * Local, Local3 = Local2 * Local;
        for (int Channel = 0; Channel < 4; ++Channel)
        {
            const float Value = .5f * ((2.f * C1[Channel]) + (-C0[Channel] + C2[Channel]) * Local +
                (2.f * C0[Channel] - 5.f * C1[Channel] + 4.f * C2[Channel] - C3[Channel]) * Local2 +
                (-C0[Channel] + 3.f * C1[Channel] - 3.f * C2[Channel] + C3[Channel]) * Local3);
            Result[Channel] = std::clamp(Value, 0.f, 1.f);
        }
        return Result;
    }
    if (Mode == 3)
    {
        const float Inv = 1.f - Local, Local2 = Local * Local, Local3 = Local2 * Local;
        for (int Channel = 0; Channel < 4; ++Channel)
        {
            const float Value = ((Inv * Inv * Inv) * C0[Channel] + (3.f * Local3 - 6.f * Local2 + 4.f) * C1[Channel] +
                (-3.f * Local3 + 3.f * Local2 + 3.f * Local + 1.f) * C2[Channel] + Local3 * C3[Channel]) / 6.f;
            Result[Channel] = std::clamp(Value, 0.f, 1.f);
        }
        return Result;
    }
    for (int Channel = 0; Channel < 4; ++Channel) Result[Channel] = C1[Channel] + (C2[Channel] - C1[Channel]) * Local;
    return Result;
}

// 📝 Piecewise spline/linear interpolation across whatever stops bracket Fraction — exact CPU mirror of
// VectorCoverage.slang/VectorSurface.slang's EvaluateGradientColour across all 5 interpolation modes.
inline std::array<float, 4> EvaluateVectorGradientColour(const std::vector<VectorGradientStop> &Stops, float Fraction, uint32_t Mode = 0)
{
    if (Stops.empty()) return {1, 1, 1, 1};
    if (Stops.size() == 1) return Stops.front().Colour;
    if (Fraction <= Stops.front().Position)
        return Mode == 3 ? InterpolateVectorSplineSegment(Stops[0].Colour, Stops[0].Colour, Stops[1].Colour,
            Stops[std::min<size_t>(2, Stops.size() - 1)].Colour, 0.f, 3) : Stops.front().Colour;
    if (Fraction >= Stops.back().Position)
    {
        const size_t Last = Stops.size() - 2;
        return Mode == 3 ? InterpolateVectorSplineSegment(Stops[Last > 0 ? Last - 1 : 0].Colour, Stops[Last].Colour,
            Stops[Last + 1].Colour, Stops[Last + 1].Colour, 1.f, 3) : Stops.back().Colour;
    }
    for (size_t Index = 0; Index + 1 < Stops.size(); ++Index)
    {
        const auto &A = Stops[Index]; const auto &B = Stops[Index + 1];
        if (Fraction > B.Position) continue;
        const float Span = std::max(B.Position - A.Position, 1e-5f);
        const float Local = std::clamp((Fraction - A.Position) / Span, 0.f, 1.f);
        const auto &C0 = Stops[Index > 0 ? Index - 1 : 0].Colour;
        const auto &C3 = Stops[Index + 2 < Stops.size() ? Index + 2 : Stops.size() - 1].Colour;
        return InterpolateVectorSplineSegment(C0, A.Colour, B.Colour, C3, Local, Mode);
    }
    return Stops.back().Colour;
}

inline std::vector<VectorFreeformGradientPoint> QueryVectorFreeformPoints(const VectorCoverageInstruction &Source)
{
    const int Count = std::clamp(int(std::lround(Source.FreeformParameters[0])), 2, VectorFreeformPointCapacity);
    const std::array<std::array<float, 2>, 8> Positions{{
        {Source.FreeformPointPositions01[0], Source.FreeformPointPositions01[1]},
        {Source.FreeformPointPositions01[2], Source.FreeformPointPositions01[3]},
        {Source.FreeformPointPositions23[0], Source.FreeformPointPositions23[1]},
        {Source.FreeformPointPositions23[2], Source.FreeformPointPositions23[3]},
        {Source.FreeformPointPositions45[0], Source.FreeformPointPositions45[1]},
        {Source.FreeformPointPositions45[2], Source.FreeformPointPositions45[3]},
        {Source.FreeformPointPositions67[0], Source.FreeformPointPositions67[1]},
        {Source.FreeformPointPositions67[2], Source.FreeformPointPositions67[3]}}};
    const std::array<std::array<float, 4>, 8> Colours{
        Source.Colour, Source.ColourEnd, Source.GradientStopColour2, Source.GradientStopColour3,
        Source.FreeformPointColour4, Source.FreeformPointColour5, Source.FreeformPointColour6, Source.FreeformPointColour7};
    std::vector<VectorFreeformGradientPoint> Points;
    Points.reserve(size_t(Count));
    for (int Index = 0; Index < Count; ++Index) Points.push_back({Positions[Index], Colours[Index]});
    return Points;
}

inline void ApplyVectorFreeformPoints(
    VectorAuthoringSourcePatch &Patch,
    const std::vector<VectorFreeformGradientPoint> &Points,
    float Sharpness = 2.f)
{
    const VectorCoverageInstruction Defaults{};
    std::array<std::array<float, 2>, 8> Positions{{
        {Defaults.FreeformPointPositions01[0], Defaults.FreeformPointPositions01[1]},
        {Defaults.FreeformPointPositions01[2], Defaults.FreeformPointPositions01[3]},
        {Defaults.FreeformPointPositions23[0], Defaults.FreeformPointPositions23[1]},
        {Defaults.FreeformPointPositions23[2], Defaults.FreeformPointPositions23[3]},
        {Defaults.FreeformPointPositions45[0], Defaults.FreeformPointPositions45[1]},
        {Defaults.FreeformPointPositions45[2], Defaults.FreeformPointPositions45[3]},
        {Defaults.FreeformPointPositions67[0], Defaults.FreeformPointPositions67[1]},
        {Defaults.FreeformPointPositions67[2], Defaults.FreeformPointPositions67[3]}}};
    std::array<std::array<float, 4>, 8> Colours{
        Defaults.Colour, Defaults.ColourEnd, Defaults.GradientStopColour2, Defaults.GradientStopColour3,
        Defaults.FreeformPointColour4, Defaults.FreeformPointColour5, Defaults.FreeformPointColour6, Defaults.FreeformPointColour7};
    const size_t Count = std::clamp<size_t>(Points.size(), 2, VectorFreeformPointCapacity);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        Positions[Index] = {std::clamp(Points[Index].Position[0], -64.f, 64.f), std::clamp(Points[Index].Position[1], -64.f, 64.f)};
        for (int Channel = 0; Channel < 4; ++Channel) Colours[Index][Channel] = std::clamp(Points[Index].Colour[Channel], 0.f, 1.f);
    }
    Patch.FreeformPointPositions01 = {Positions[0][0], Positions[0][1], Positions[1][0], Positions[1][1]};
    Patch.FreeformPointPositions23 = {Positions[2][0], Positions[2][1], Positions[3][0], Positions[3][1]};
    Patch.FreeformPointPositions45 = {Positions[4][0], Positions[4][1], Positions[5][0], Positions[5][1]};
    Patch.FreeformPointPositions67 = {Positions[6][0], Positions[6][1], Positions[7][0], Positions[7][1]};
    Patch.Colour = Colours[0];
    Patch.ColourEnd = Colours[1];
    Patch.GradientStopColour2 = Colours[2];
    Patch.GradientStopColour3 = Colours[3];
    Patch.FreeformPointColour4 = Colours[4];
    Patch.FreeformPointColour5 = Colours[5];
    Patch.FreeformPointColour6 = Colours[6];
    Patch.FreeformPointColour7 = Colours[7];
    Patch.FreeformParameters = {float(Count), std::clamp(Sharpness, .25f, 8.f), 0.f, 0.f};
}

inline std::array<float, 4> EvaluateVectorFreeformGradient2D(
    const std::vector<VectorFreeformGradientPoint> &Points,
    std::array<float, 2> LocalNorm,
    float Sharpness = 2.f,
    uint32_t Mode = 0)
{
    if (Points.empty()) return {1, 1, 1, 1};
    const size_t PointCount = std::min<size_t>(Points.size(), VectorFreeformPointCapacity);
    if (PointCount == 1) return Points[0].Colour;
    const float ClampedSharpness = std::clamp(Sharpness, .25f, 8.f);
    if (Mode == 4)
    {
        size_t Nearest = 0;
        float BestDistance = 1e20f;
        for (size_t Index = 0; Index < PointCount; ++Index)
        {
            const float Dx = LocalNorm[0] - Points[Index].Position[0], Dy = LocalNorm[1] - Points[Index].Position[1];
            const float Distance = std::sqrt(Dx * Dx + Dy * Dy);
            if (Distance < BestDistance) { BestDistance = Distance; Nearest = Index; }
        }
        return Points[Nearest].Colour;
    }
    std::array<float, 8> Weights{};
    float WeightSum = 0.f;
    for (size_t Index = 0; Index < PointCount; ++Index)
    {
        const float Dx = LocalNorm[0] - Points[Index].Position[0], Dy = LocalNorm[1] - Points[Index].Position[1];
        const float Distance = std::sqrt(Dx * Dx + Dy * Dy);
        if (Mode != 3 && Distance < 1e-4f) return Points[Index].Colour;
        float Weight = 0.f;
        if (Mode == 3)
        {
            const float Regularized = std::sqrt(Distance * Distance + .04f * .04f);
            Weight = 1.f / std::pow(Regularized, std::max(ClampedSharpness * .85f, .35f));
        }
        else if (Mode == 0)
            Weight = 1.f / std::pow(std::max(Distance, 1e-4f), std::max(ClampedSharpness * .65f, .35f));
        else if (Mode == 2)
            Weight = 1.f / std::pow(std::max(Distance, 1e-4f), std::max(ClampedSharpness * 1.25f, .5f));
        else
            Weight = 1.f / std::pow(std::max(Distance, 1e-4f), ClampedSharpness);
        Weights[Index] = Weight;
        WeightSum += Weight;
    }
    if (WeightSum <= 1e-12f) return Points[0].Colour;
    std::array<float, 4> Accum{};
    float ShapedSum = 0.f;
    for (size_t Index = 0; Index < PointCount; ++Index)
    {
        const float Normalized = Weights[Index] / WeightSum;
        float Shaped = Normalized;
        if (Mode == 1) Shaped = Normalized * Normalized * (3.f - 2.f * Normalized);
        else if (Mode == 2) Shaped = std::clamp(Normalized * (.35f + Normalized * (1.95f - 1.3f * Normalized)), 0.f, 1.f);
        else if (Mode == 3)
        {
            const float Inv = 1.f - Normalized;
            Shaped = ((1.f - Inv * Inv * Inv) + Normalized * Normalized * (3.f - 2.f * Normalized)) * .5f;
        }
        for (int Channel = 0; Channel < 4; ++Channel) Accum[Channel] += Points[Index].Colour[Channel] * Shaped;
        ShapedSum += Shaped;
    }
    if (ShapedSum <= 1e-12f) return Points[0].Colour;
    std::array<float, 4> Result{};
    for (int Channel = 0; Channel < 4; ++Channel) Result[Channel] = std::clamp(Accum[Channel] / ShapedSum, 0.f, 1.f);
    return Result;
}
}
