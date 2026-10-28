#pragma once

#include <array>
#include <cstdint>

namespace Frontier::Fluid
{
using CellPair = std::array<std::int64_t, 2>;
using CellTriple = std::array<std::int64_t, 3>;
using PositionPair = std::array<double, 2>;

CellPair WorldCell(const PositionPair& Position, double Edge);
CellTriple WrapCell(const CellTriple& Cell, const CellTriple& Extent);
CellPair RecentreWindow(const PositionPair& Focus, double Edge, const CellPair& Extent,
                        const CellPair& Origin, bool Force);
}
