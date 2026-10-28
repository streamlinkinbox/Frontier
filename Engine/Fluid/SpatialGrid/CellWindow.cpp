#include "CellWindow.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace Frontier::Fluid
{
CellPair WorldCell(const PositionPair& Position, double Edge)
{
    if (!std::isfinite(Edge) || Edge <= 0.0) throw std::invalid_argument("Cell edge must be positive and finite");
    CellPair Result{};
    for (std::size_t Axis = 0; Axis < 2; ++Axis)
    {
        const double Coordinate = std::floor(Position[Axis] / Edge);
        // Restrict to exact integer range of the JavaScript reference and avoid int64 conversion overflow.
        if (!std::isfinite(Coordinate) || std::abs(Coordinate) > 9007199254740991.0)
            throw std::out_of_range("Cell coordinate outside exact reference range");
        Result[Axis] = static_cast<std::int64_t>(Coordinate);
    }
    return Result;
}

CellTriple WrapCell(const CellTriple& Cell, const CellTriple& Extent)
{
    CellTriple Result{};
    for (std::size_t Axis = 0; Axis < 3; ++Axis)
    {
        if (Extent[Axis] <= 0) throw std::invalid_argument("Grid extent must be positive");
        const auto Remainder = Cell[Axis] % Extent[Axis];
        Result[Axis] = Remainder < 0 ? Remainder + Extent[Axis] : Remainder;
    }
    return Result;
}

CellPair RecentreWindow(const PositionPair& Focus, double Edge, const CellPair& Extent,
                        const CellPair& Origin, bool Force)
{
    const CellPair FocusCell = WorldCell(Focus, Edge);
    CellPair Result = Origin;
    bool Shift = Force;
    for (std::size_t Axis = 0; Axis < 2; ++Axis)
    {
        // The reference uses a signed JavaScript bit shift for the half extent.
        if (Extent[Axis] < 2 || Extent[Axis] > 2147483647)
            throw std::invalid_argument("Window extent outside reference range");
        if (Origin[Axis] < -9007199254740991LL || Origin[Axis] > 9007199254740991LL)
            throw std::out_of_range("Origin outside exact reference range");
        const auto Half = Extent[Axis] / 2;
        const auto Difference = FocusCell[Axis] - (Origin[Axis] + Half);
        if (std::abs(Difference) > Half / 2.0) Shift = true;
    }
    if (Shift)
        for (std::size_t Axis = 0; Axis < 2; ++Axis) Result[Axis] = FocusCell[Axis] - Extent[Axis] / 2;
    return Result;
}
}
