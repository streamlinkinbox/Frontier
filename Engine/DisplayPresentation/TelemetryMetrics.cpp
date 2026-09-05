//============================================================================================================================================
//                                                     TELEMETRYMETRICS.CPP
//============================================================================================================================================
// 🧩 Frame-time ring and FPS overlay — see TelemetryMetrics.h.

#include "TelemetryMetrics.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#   include <psapi.h>
#elif defined(__linux__)
#   include <unistd.h>
#endif

namespace Frontier {

TelemetryMetrics::TelemetryMetrics() noexcept
    : Samples{}, Cursor(0u), Filled(0u)
{
}

void TelemetryMetrics::RecordFrame(float DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;
    Samples[Cursor] = DeltaSeconds;
    Cursor = (Cursor + 1u) % SampleCount;
    if (Filled < SampleCount) ++Filled;
    MemorySampleAge += DeltaSeconds;
    if (ExtraRows.ShowMemory && MemorySampleAge >= 0.5f) { ResidentMebibytes = SampleResidentMebibytes(); MemorySampleAge = 0.0f; }
}

float TelemetryMetrics::SampleResidentMebibytes() noexcept
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS Counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &Counters, sizeof(Counters)))
        return static_cast<float>(Counters.WorkingSetSize) / (1024.0f * 1024.0f);
    return 0.0f;
#elif defined(__linux__)
    if (FILE* F = std::fopen("/proc/self/statm", "r"))
    {
        long Pages = 0, Resident = 0;
        const int Read = std::fscanf(F, "%ld %ld", &Pages, &Resident);
        std::fclose(F);
        if (Read == 2) return static_cast<float>(Resident) * static_cast<float>(sysconf(_SC_PAGESIZE)) / (1024.0f * 1024.0f);
    }
    return 0.0f;
#else
    return 0.0f;
#endif
}

bool TelemetryMetrics::ConsumeFrameRateDrop(float ThresholdFramesPerSecond) noexcept
{
    if (Filled < SampleCount / 2u) return false;   // wait for a meaningful average
    const float Fps = QueryAverageFramesPerSecond();
    if (DropArmed && Fps < ThresholdFramesPerSecond) { DropArmed = false; return true; }
    if (!DropArmed && Fps > ThresholdFramesPerSecond * 1.2f) DropArmed = true;
    return false;
}

float TelemetryMetrics::QueryAverageFrameSeconds() const noexcept
{
    if (Filled == 0u) return 0.0f;
    float Sum = 0.0f;
    for (uint32_t I = 0u; I < Filled; ++I) Sum += Samples[I];
    return Sum / static_cast<float>(Filled);
}

void TelemetryMetrics::ConstructTelemetryLayout(PixelSpace& Surface, float TopInset) const noexcept
{
    if (!Surface.IsRecording()) return;

    constexpr ColorQuad Pill { 0x0A / 255.0f, 0x0A / 255.0f, 0x0B / 255.0f, 0.70f };
    constexpr ColorQuad Ink  { 1.0f, 1.0f, 1.0f, 0.90f };
    constexpr ColorQuad Trace{ 0x3B / 255.0f, 0x82 / 255.0f, 0xF6 / 255.0f, 0.90f };   // #3B82F6, same blue as active tiles
    constexpr float Inset = 16.0f, Padding = 10.0f, FontSize = 11.0f, GraphWidth = 120.0f, GraphHeight = 24.0f;

    char Readout[64];
    std::snprintf(Readout, sizeof(Readout), "%3.0f FPS  %5.1f ms",
                  static_cast<double>(QueryAverageFramesPerSecond()),
                  static_cast<double>(QueryAverageFrameSeconds() * 1000.0f));

    // Optional rows beneath the readout: RAM ("Show RAM Usage") and the project's scene line ("Scene Metadata").
    char MemoryLine[48] = {};
    if (ExtraRows.ShowMemory) std::snprintf(MemoryLine, sizeof(MemoryLine), "RAM %.0f MiB", static_cast<double>(ResidentMebibytes));
    const char* SceneLine = ExtraRows.ShowScene && !ExtraRows.SceneLine.empty() ? ExtraRows.SceneLine.c_str() : nullptr;

    const PlanePoint TextSize = Surface.MeasureText(Readout, FontSize);
    float RowsHeight = 0.0f, RowsWidth = 0.0f;
    if (ExtraRows.ShowMemory) { const PlanePoint M = Surface.MeasureText(MemoryLine, FontSize); RowsHeight += M.Y + 4.0f; RowsWidth = std::max(RowsWidth, M.X); }
    if (SceneLine)            { const PlanePoint M = Surface.MeasureText(SceneLine,  FontSize); RowsHeight += M.Y + 4.0f; RowsWidth = std::max(RowsWidth, M.X); }
    const float Width  = Padding * 2.0f + std::max({ TextSize.X, GraphWidth, RowsWidth });
    const float Height = Padding * 2.0f + TextSize.Y + 6.0f + GraphHeight + RowsHeight;
    const PlaneExtent Extent = Spanning(Inset, TopInset + Inset, Width, Height);

    Surface.FillRectangle(Extent, Pill, 12.0f);
    Surface.Text(Extent.MinimumX + Padding, Extent.MinimumY + Padding, Ink, Readout, FontSize);
    {
        constexpr ColorQuad InkDim{ 1.0f, 1.0f, 1.0f, 0.60f };
        float RowY = Extent.MinimumY + Padding + TextSize.Y + 6.0f + GraphHeight + 4.0f;
        if (ExtraRows.ShowMemory) { Surface.Text(Extent.MinimumX + Padding, RowY, InkDim, MemoryLine, FontSize); RowY += TextSize.Y + 4.0f; }
        if (SceneLine)            { Surface.Text(Extent.MinimumX + Padding, RowY, InkDim, SceneLine,  FontSize); }
    }

    // Sparkline of the last SampleCount frames, oldest left. Scale: 0 ms at the bottom, 2 x the average at the top.
    if (Filled >= 2u)
    {
        const float Ceiling = std::max(QueryAverageFrameSeconds() * 2.0f, 1.0f / 240.0f);
        const float GraphX  = Extent.MinimumX + Padding;
        const float GraphY  = Extent.MinimumY + Padding + TextSize.Y + 6.0f;

        std::vector<PlanePoint> Line;
        Line.reserve(Filled);
        for (uint32_t I = 0u; I < Filled; ++I)
        {
            const uint32_t Index = (Cursor + SampleCount - Filled + I) % SampleCount;
            const float T = std::clamp(Samples[Index] / Ceiling, 0.0f, 1.0f);
            Line.push_back({ GraphX + GraphWidth * static_cast<float>(I) / static_cast<float>(SampleCount - 1u),
                             GraphY + GraphHeight * (1.0f - T) });
        }
        Surface.StrokePolyline(Line.data(), static_cast<uint32_t>(Line.size()), Trace, 1.0f, false);
    }
}

} // namespace Frontier
