/*============================================================================================================================================
                                                            SCALARCURVEEDITOR.H
============================================================================================================================================*/
// 📈 Reusable interactive 2D spline curve canvas (SizeOverLife, OpacityOverLife, AttenuationFalloff, ModulationEnvelope)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ScalarCurveKnot
{
    float TimeNormalized  = 0.0f; // [0-1] - Horizontal normalized curve domain
    float ScaleNormalized = 1.0f; // [0-1] - Vertical normalized amplitude
};

struct ScalarCurveProfile
{
    std::array<ScalarCurveKnot, 4u> Knots {{
        { 0.00f, 0.25f },
        { 0.25f, 1.00f },
        { 0.70f, 0.80f },
        { 1.00f, 0.00f },
    }};
    uint32_t ActiveKnots = 4u; // [count] - Active control knots (2..4)

    [[nodiscard]] float Evaluate(float T) const noexcept;
};

struct ScalarCurveEditorSession
{
    int32_t SelectedKnotIndex = -1;    // [idx] - Currently dragged knot (-1 if none)
    bool    DraggingKnot      = false; // [-]   - Active drag tracking
    bool    Modified          = false; // [-]   - True during edit step
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace ScalarCurveEditor
{

[[nodiscard]] bool Construct(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    std::string_view          HeadingTitle,
    ScalarCurveProfile&       Profile,
    ScalarCurveEditorSession& Session,
    const ControlPointer&     Pointer,
    ColorRgba                 AccentInk = { 255u, 138u, 76u, 255u },
    float                     Opacity   = 1.0f) noexcept;

} // namespace ScalarCurveEditor

} // namespace Frontier
