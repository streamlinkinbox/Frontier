//============================================================================================================================================
//                                                      TELEMETRYMETRICS.H
//============================================================================================================================================
// 🧩 Frame-time ring buffer and the top-left FPS overlay the Control Centre "FPS Overlay" tile toggles.
//
//    Overlay: 11 px monospace-style readout "60 FPS  16.7 ms" plus a 120-sample sparkline, on a #0A0A0B/70 pill.
//    Placed top-left beneath the notch line so it never collides with the notch or the toast column.

#pragma once

#include "PixelSpace.h"
#include <array>
#include <cstdint>
#include <string>

namespace Frontier {

// Optional overlay rows (Control Centre › Notifications › "Show RAM Usage" / "Scene Metadata").
struct TelemetryRowStructure
{
    bool        ShowMemory = false;   // "Show RAM Usage" — process resident set, sampled every 0.5 s
    bool        ShowScene  = false;   // "Scene Metadata" — SceneLine as supplied by the project
    std::string SceneLine;            // e.g. "Cornell  |  36 tris  |  1 luminaire  |  1280x720"
};

class TelemetryMetrics
{
public:
    static constexpr uint32_t SampleCount = 120u;   // [frames] ring length (2 s at 60 Hz)

    TelemetryMetrics() noexcept;

    void RecordFrame(float DeltaSeconds) noexcept;

    [[nodiscard]] float QueryAverageFrameSeconds() const noexcept;
    [[nodiscard]] float QueryAverageFramesPerSecond() const noexcept
    {
        const float S = QueryAverageFrameSeconds();
        return S > 0.0f ? 1.0f / S : 0.0f;
    }

    void ConstructTelemetryLayout(PixelSpace& Surface, float TopInset) const noexcept;

    void AssignRows(const TelemetryRowStructure& Rows) noexcept { ExtraRows = Rows; }
    [[nodiscard]] const TelemetryRowStructure& QueryRows() const noexcept { return ExtraRows; }
    // Resident-set size of this process in MiB (0 when the platform query is unavailable); refreshed by RecordFrame.
    [[nodiscard]] float QueryResidentMebibytes() const noexcept { return ResidentMebibytes; }

    // Frame-rate drop detector (Control Centre › Notifications › Alerts › "Frame-rate Drops"): true once per episode
    //    when the 2 s average dips below Threshold; re-arms after it recovers above Threshold × 1.2.
    [[nodiscard]] bool ConsumeFrameRateDrop(float ThresholdFramesPerSecond) noexcept;

private:
    std::array<float, SampleCount> Samples;   // [s]
    uint32_t Cursor;                          // [-] next write index
    uint32_t Filled;                          // [-] valid samples
    TelemetryRowStructure ExtraRows;
    float    ResidentMebibytes = 0.0f;        // [MiB]
    float    MemorySampleAge   = 1.0f;        // [s] since the last RSS query
    bool     DropArmed         = true;
    static float SampleResidentMebibytes() noexcept;
};

} // namespace Frontier
