/*============================================================================================================================================
                                                          SIMULATIONTIMERULER.H
============================================================================================================================================*/
// ⏱️ Reusable scrubbable time ruler (0s..30s, step counter, active track strip & playhead needle) shared across simulation & animation workspaces

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SimulationRulerSpec
{
    double      ElapsedSeconds   = 0.0;                 // [s]     - Current playhead time
    double      DurationSeconds  = 30.0;                // [s]     - Total ruler span
    uint64_t    StepCounter      = 0u;                  // [steps] - Current simulation step ordinal
    const char* DomainHeading    = "SIMULATION";        // [-]     - Left caption heading
    const char* TrackCaption     = "Active emission";   // [-]     - Active track strip label
    bool        DraggingPlayhead = false;               // [-]     - Active pointer drag on ruler track
};

struct SimulationRulerReading
{
    double ScrubbedSeconds = 0.0;   // [s] - Resulting playhead time when modified
    bool   ScrubbingActive = false; // [-] - True while dragging playhead
    bool   Modified        = false; // [-] - True when user clicked or dragged the ruler
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace SimulationTimeRuler
{

[[nodiscard]] SimulationRulerReading Construct(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    SimulationRulerSpec&  Spec,
    const ControlPointer& Pointer,
    float                 Opacity = 1.0f) noexcept;

} // namespace SimulationTimeRuler

} // namespace Frontier
