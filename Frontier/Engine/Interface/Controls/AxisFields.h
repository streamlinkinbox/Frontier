/*==========================================================================================================================================
                                                                AXISFIELDS.H
==========================================================================================================================================*/
// 🧩 Three compact, finite numeric fields for world-space coordinates or half extents.

#pragma once

#include <array>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    AXIS FIELDS
//------------------------------------------------------------------------------------------------------------------------

class AxisFields
{
  public:
    static bool Construct(ControlColumn &Column,
                          const char    *Id,
                          const char    *Caption,
                          float (&Coordinates)[3],
                          const std::array<float, 3> &Minimum,
                          const std::array<float, 3> &Maximum);
};

} // namespace Frontier
