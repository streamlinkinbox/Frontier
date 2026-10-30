/*============================================================================================================================================
                                                         FACETAREASCORE.CPP
============================================================================================================================================*/
// 🧩 Preserve low-word cancellation in source-to-receiver projection before evaluating the diffuse area target.

#include "FacetAreaScore.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{
float Subtract(float First, float Second, float& Remainder) noexcept
{
    const float Difference = First - Second;
    const float Virtual = First - Difference;
    Remainder = (First - (Difference + Virtual)) + (Virtual - Second);
    return Difference;
}

float ProductRemainder(float First, float Second, float Product) noexcept
{
    const float SplitFirst = 4097.f * First;
    const float FirstHigh = SplitFirst - (SplitFirst - First);
    const float FirstLow = First - FirstHigh;
    const float SplitSecond = 4097.f * Second;
    const float SecondHigh = SplitSecond - (SplitSecond - Second);
    const float SecondLow = Second - SecondHigh;
    return ((FirstHigh * SecondHigh - Product) + FirstHigh * SecondLow + FirstLow * SecondHigh) + FirstLow * SecondLow;
}

float Dot(const std::array<float, 3>& First, const std::array<float, 3>& Second) noexcept
{
    return (First[0] * Second[0] + First[1] * Second[1]) + First[2] * Second[2];
}

bool UnitNormal(const std::array<float, 3>& Normal) noexcept
{
    const float Squared = Dot(Normal, Normal);
    return std::isfinite(Squared) && std::abs(Squared - 1.f) <= 2e-4f;
}
}

FacetAreaScore EvaluateFacetAreaScore(const FacetAreaScoreInput& Input) noexcept
{
    FacetAreaScore Score;
    if (!std::isfinite(Input.U) || !std::isfinite(Input.V) || Input.U < 0 || Input.V < 0 ||
        Input.U + Input.V > 1.f + 2e-6f || !std::isfinite(Input.TargetFloor) || Input.TargetFloor <= 0 ||
        !UnitNormal(Input.SourceNormal) || !UnitNormal(Input.ReceiverNormal) || !UnitNormal(Input.LightDirection)) return Score;
    std::array<float, 3> Delta{}, Low{}, Product{}, Remainder{}, ReceiverDirection{};
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        for (float Coordinate : {Input.Triangle.A[Axis], Input.Triangle.B[Axis], Input.Triangle.C[Axis], Input.Receiver[Axis]})
            if (!std::isfinite(Coordinate)) return Score;
        for (float Radiometry : {Input.SourceAlbedo[Axis], Input.ReceiverAlbedo[Axis], Input.LightRadiance[Axis]})
            if (!std::isfinite(Radiometry) || Radiometry < 0) return Score;
        float OriginError, FirstError, SecondError, SumError, DeltaError;
        const float Origin = Subtract(Input.Receiver[Axis], Input.Triangle.A[Axis], OriginError);
        const float FirstEdge = Subtract(Input.Triangle.B[Axis], Input.Triangle.A[Axis], FirstError);
        const float SecondEdge = Subtract(Input.Triangle.C[Axis], Input.Triangle.A[Axis], SecondError);
        const float First = Input.U * FirstEdge, Second = Input.V * SecondEdge;
        const float FirstResidual = ProductRemainder(Input.U, FirstEdge, First) + Input.U * FirstError;
        const float SecondResidual = ProductRemainder(Input.V, SecondEdge, Second) + Input.V * SecondError;
        const float Sum = Subtract(Origin, First, SumError);
        Delta[Axis] = Subtract(Sum, Second, DeltaError);
        Low[Axis] = (((OriginError + SumError) + DeltaError) - FirstResidual) - SecondResidual;
        ReceiverDirection[Axis] = -Input.ReceiverNormal[Axis];
        Product[Axis] = ReceiverDirection[Axis] * Delta[Axis];
        Remainder[Axis] = ProductRemainder(ReceiverDirection[Axis], Delta[Axis], Product[Axis]);
        if (!std::isfinite(Delta[Axis]) || !std::isfinite(Low[Axis]) || !std::isfinite(Remainder[Axis])) return Score;
    }
    const float Sum = Product[0] + Product[1];
    float Virtual = Sum - Product[0];
    float Error = (Product[0] - (Sum - Virtual)) + (Product[1] - Virtual);
    const float Total = Sum + Product[2];
    Virtual = Total - Sum;
    Error += (Sum - (Total - Virtual)) + (Product[2] - Virtual);
    const float Projection = Total + ((((Error + Remainder[0]) + Remainder[1]) + Remainder[2]) + Dot(ReceiverDirection, Low));
    if (!std::isfinite(Projection)) return Score;
    Score.ReceiverProjection = std::max(0.f, Projection);
    for (uint32_t Axis = 0; Axis < 3; ++Axis) Delta[Axis] += Low[Axis];
    Score.SquaredDistance = Dot(Delta, Delta);
    const float Denominator = Score.SquaredDistance * Score.SquaredDistance;
    const float SourceProjection = Dot(Input.SourceNormal, Delta);
    const float LightProjection = Dot(Input.SourceNormal, Input.LightDirection);
    if (!std::isfinite(Denominator) || Denominator <= 0 || !std::isfinite(SourceProjection) ||
        !std::isfinite(LightProjection)) return FacetAreaScore{};
    Score.SourceProjection = std::max(0.f, SourceProjection);
    constexpr float Pi = 3.141592653589793f;
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        Score.Geometry[Axis] = ((Input.ReceiverAlbedo[Axis] / Pi * Score.ReceiverProjection) * Score.SourceProjection) /
                               Denominator;
        Score.ProxyRgb[Axis] = (((Input.SourceAlbedo[Axis] * Input.LightRadiance[Axis] / Pi) *
                                 std::max(0.f, LightProjection)) * Score.Geometry[Axis]);
        if (Score.Geometry[Axis] == 0 && Input.ReceiverAlbedo[Axis] > 0 &&
            Score.ReceiverProjection > 0 && Score.SourceProjection > 0) return FacetAreaScore{};
        if (Score.ProxyRgb[Axis] == 0 && Input.SourceAlbedo[Axis] > 0 && Input.LightRadiance[Axis] > 0 &&
            LightProjection > 0 && Score.Geometry[Axis] > 0) return FacetAreaScore{};
        if (!std::isfinite(Score.Geometry[Axis]) || !std::isfinite(Score.ProxyRgb[Axis])) return FacetAreaScore{};
    }
    Score.Target = Input.TargetFloor + ((Score.ProxyRgb[0] + Score.ProxyRgb[1]) + Score.ProxyRgb[2]) / 3.f;
    if (!std::isfinite(Score.Target) || Score.Target <= 0) return FacetAreaScore{};
    Score.Accepted = true;
    return Score;
}
}
