/*============================================================================================================================================
                                                        SIMULATIONPLAYBACKCONTROLS.H
============================================================================================================================================*/
// ⏯️ Reusable transport strip (Restart, Play/Pause, SingleStep, Loop, TimeScale, Timecode, Burst trigger) shared across simulation workspaces

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SimulationPlaybackAction : uint8_t
{
    None       = 0u,
    Restart    = 1u,
    TogglePlay = 2u,
    SingleStep = 3u,
    ToggleLoop = 4u,
    CycleScale = 5u,
    TriggerBurst = 6u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SimulationPlaybackTuning
{
    double   ElapsedSeconds = 0.0;   // [s]   - Active simulation clock
    float    TimeScale      = 1.0f;  // [x]   - Playback rate multiplier (0.25x, 0.5x, 1.0x, 2.0x)
    bool     Playing        = true;  // [-]   - True while advancing simulation steps
    bool     Looping        = true;  // [-]   - True to wrap ElapsedSeconds at LoopDurationSeconds
    bool     LiveMode       = true;  // [-]   - True for live simulation; false for cached scrub playback
    bool     ShowBurst      = true;  // [-]   - Show right-hand Burst [B] action capsule
    bool     ScaleMenuOpen  = false; // [-]   - Dropdown sheet open flag for TimeScale selector
};

struct SimulationPlaybackReading
{
    SimulationPlaybackAction TriggeredAction = SimulationPlaybackAction::None;
    bool                     Modified        = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace SimulationPlaybackControls
{

[[nodiscard]] SimulationPlaybackReading Construct(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    SimulationPlaybackTuning& Tuning,
    const ControlPointer&     Pointer,
    float                     Opacity = 1.0f) noexcept;

} // namespace SimulationPlaybackControls

} // namespace Frontier
