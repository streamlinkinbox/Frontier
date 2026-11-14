/*============================================================================================================================================
                                                                SCALARSLIDER.H
============================================================================================================================================*/
// 🧩 Reusable scalar slider, split readout capsule ("[ 100 | % ]" / "[ 16 | px ]"), and labeled inspector slider row

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <cstdint>

namespace Frontier
{

namespace ScalarSlider
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ControlContact Construct(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    float                  Minimum,
    float                  Maximum,
    float                  CurrentPosition,
    bool                   DraggingActive,
    const ControlPointer&  Pointer,
    float&                 UpdatedPosition,
    float                  Opacity      = 1.0f,
    float                  RailHeight   = ControlMetrics::SliderHeight,
    float                  KnobDiameter = ControlMetrics::SliderThumbDiameter,
    const OptionalTooltip& Tooltip      = {}) noexcept;

void ConstructReadoutCapsule(
    PixelSpace& Surface,
    float       X,
    float       Y,
    const char* NumberText,
    const char* UnitText,
    float       Opacity       = 1.0f,
    float       TotalWidth    = ControlMetrics::ReadoutCapsuleWidth,
    float       UnitCellWidth = ControlMetrics::ReadoutUnitWidth) noexcept;

void ConstructCompactReadoutCapsule(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* NumberText,
    const char* UnitText,
    float       Opacity       = 1.0f,
    float       UnitCellWidth = 30.0f) noexcept;

[[nodiscard]] ControlContact ConstructWithReadout(
    PixelSpace&            Surface,
    Rectangle2D            RowBounds,
    const char*            NumberText,
    const char*            UnitText,
    float                  Minimum,
    float                  Maximum,
    float                  CurrentPosition,
    bool                   DraggingActive,
    const ControlPointer&  Pointer,
    float&                 UpdatedPosition,
    float                  Opacity = 1.0f,
    const OptionalTooltip& Tooltip = {}) noexcept;

[[nodiscard]] bool ConstructLabeledInspectorRow(
    PixelSpace&            Surface,
    Rectangle2D            RowBounds,
    int32_t                SliderId,
    const char*            Label,
    float                  Minimum,
    float                  Maximum,
    float&                 InOutScalar,
    const char*            UnitSuffix,
    int32_t                DecimalDigits,
    const ControlPointer&  Pointer,
    int32_t&               ActiveSliderId,
    float                  Opacity      = 1.0f,
    float                  RailHeight   = 26.0f,
    float                  KnobDiameter = 24.0f,
    const OptionalTooltip& Tooltip      = {}) noexcept;

} // namespace ScalarSlider

} // namespace Frontier
