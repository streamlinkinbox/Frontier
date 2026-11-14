/*============================================================================================================================================
                                                             PLAYBACKSCRUBBER.H
============================================================================================================================================*/
// 🧩 Reusable animation play slider, timeline ruler scrubber, and simulation playback transport control

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <cstdint>

namespace Frontier
{

namespace PlaybackScrubber
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr float TransportRowHeight  = 52.0f; // [px] - Upper playback controls row height
inline constexpr float ScrubberRailHeight  = 44.0f; // [px] - Lower animation play slider ruler height
inline constexpr float FullTransportHeight = 96.0f; // [px] - Combined transport plus animation play slider height

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PlaybackClockParameters
{
    float       ElapsedSeconds      = 0.0f;                            // [s]  - Current playback position within loop span
    float       LoopDurationSeconds = 30.0f;                           // [s]  - Total scrubber ruler span
    float       FrameRateHz         = 60.0f;                           // [Hz] - Frame counter cadence
    float       SpeedMultiplier     = 1.0f;                            // [x]  - Active simulation speed multiplier
    bool        Playing             = true;                            // [-]  - Continuous playback active
    bool        LiveUncached        = true;                            // [-]  - Live uncached indicator toggle
    bool        ShowSpeedSelector   = true;                            // [-]  - Display speed multiplier capsule
    const char* RulerHeading        = "SIMULATION";                    // [-]  - Leading uppercase ruler caption
    const char* TrackCaption        = "Active particle & force field"; // [-]  - Text label inside the scrubber rail
    const char* ActionLabel         = "Burst";                         // [-]  - Optional right-hand action button caption
    const char* ActionShortcut      = "B";                             // [-]  - Optional key hint printed beside action caption
};

struct PlaybackScrubberInteraction
{
    bool  PlayPauseToggled       = false; // [-] - User clicked the primary Play / Pause circular button
    bool  ResetTriggered         = false; // [-] - User clicked the Reset button
    bool  StepForwardTriggered   = false; // [-] - User clicked the Single-Frame Step button
    bool  SpeedCycled            = false; // [-] - User clicked the Speed multiplier capsule
    bool  ActionTriggered        = false; // [-] - User clicked the right-hand action button
    bool  Scrubbed               = false; // [-] - Playhead moved via click or drag along the animation slider
    bool  DraggingPlayhead       = false; // [-] - Pointer drag currently active on the animation play slider
    bool  Hovered                = false; // [-] - Pointer hovers any interactive region in the control
    float UpdatedSeconds         = 0.0f;  // [s] - Resulting playhead position after scrubbing
    float UpdatedSpeedMultiplier = 1.0f;  // [x] - Resulting speed multiplier after cycling
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] float QueryNextSpeedMultiplier(float CurrentSpeedMultiplier) noexcept;

void FormatTimecode(float ElapsedSeconds, char* OutBuffer, uint32_t BufferCapacity) noexcept;

[[nodiscard]] PlaybackScrubberInteraction ConstructScrubberRail(
    PixelSpace&                    Surface,
    Rectangle2D                    Bounds,
    const PlaybackClockParameters& Clock,
    bool                           DraggingPlayhead,
    const ControlPointer&          Pointer,
    float                          Opacity = 1.0f,
    const OptionalTooltip&         Tooltip = {}) noexcept;

[[nodiscard]] PlaybackScrubberInteraction ConstructFullTransport(
    PixelSpace&                    Surface,
    Rectangle2D                    Bounds,
    const PlaybackClockParameters& Clock,
    bool                           DraggingPlayhead,
    const ControlPointer&          Pointer,
    float                          Opacity = 1.0f,
    const OptionalTooltip&         Tooltip = {}) noexcept;

} // namespace PlaybackScrubber

} // namespace Frontier
