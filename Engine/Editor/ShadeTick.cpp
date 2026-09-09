//============================================================================================================================================
//                                                      SHADETICK.CPP
//============================================================================================================================================
// 🧩 The telemetry feed behind AdvanceShadeTelemetry: a single call into the original's API.

#include "ShadeTick.h"

namespace Frontier {

void AdvanceShadeTelemetry(TelemetryMetrics& Telemetry, float DeltaSeconds) noexcept
{
    Telemetry.RecordFrame(DeltaSeconds);
}

} // namespace Frontier
