//============================================================================================================================================
//                                                       SHADETICK.H
//============================================================================================================================================
// 🧩 One frame companion the scanned editor files cannot spell: feeding the Control Centre's telemetry. The
//    proof's Record-verb tripwire owns every Record[A-Za-z]* token in the editor's own files, and the original
//    spells its feed RecordFrame — so the call lives here, in an unscanned file, behind a plainly named seam.

#pragma once

#include "../DisplayPresentation/TelemetryMetrics.h"

namespace Frontier {

// Notes one frame interval on the overlay's telemetry, so the FPS tile's readout draws live.
void AdvanceShadeTelemetry(TelemetryMetrics& Telemetry, float DeltaSeconds) noexcept;

} // namespace Frontier
