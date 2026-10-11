#!/usr/bin/env bash
#============================================================================================================================================
#                                                    CHECKTELEMETRYPROBE.SH
#============================================================================================================================================
# Proves the two contracts of Engine/DeviceExchange/TelemetryProbe:
#
#   ① DEV BUILD — compiles the SHIPPED probe with FRONTIER_DEVELOPMENT and runs TelemetryProbeGate: nothing may
#     touch the disk until SaveReport, and the saved report must carry the startup phases, the shader/stage rows
#     and one verbose CSV line per frame.
#   ② SHIP BUILD — compiles the same two TUs WITHOUT the define and verifies the probe leaves no trace: the
#     TelemetryProbe.cpp object exports no symbols at all, and the gate TU (whose FRONTIER_PROBE_* calls all
#     expand to ((void)0)) links without the probe's implementation even existing.
#
#   usage: bash Tools/Build/CheckTelemetryProbe.sh

set -euo pipefail
cd "$(dirname "$0")/../.."

Stage="$(mktemp -d)"
trap 'rm -rf "$Stage"' EXIT

echo "[telemetry-probe] ① dev build (FRONTIER_DEVELOPMENT): compile + run the gate"
g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -DFRONTIER_DEVELOPMENT -I. \
    -o "$Stage/TelemetryProbeGate" \
    Tools/Build/Gates/TelemetryProbeGate.cpp \
    Engine/DeviceExchange/TelemetryProbe.cpp

( cd "$Stage" && "$Stage/TelemetryProbeGate" )

echo "[telemetry-probe] ② ship build (no define): the probe must compile to nothing"
g++ -std=c++20 -O1 -Wall -Wextra -Werror -I. \
    -c Engine/DeviceExchange/TelemetryProbe.cpp -o "$Stage/ProbeShip.o"

SymbolCount="$(nm "$Stage/ProbeShip.o" 2>/dev/null | grep -c ' [TtDdBbRr] ' || true)"
if [ "$SymbolCount" -ne 0 ]; then
    echo "  FAIL  ship TelemetryProbe.o exports $SymbolCount symbol(s) — the probe leaked into the production build"
    exit 1
fi
echo "  PASS  ship TelemetryProbe.o defines zero symbols"

# The gate's own TU must also compile AND LINK without the define — every macro call site must vanish, so the
#    linker needs nothing from the (empty) probe object.
g++ -std=c++20 -O1 -I. \
    -o "$Stage/TelemetryProbeGateShip" \
    Tools/Build/Gates/TelemetryProbeGate.cpp \
    Engine/DeviceExchange/TelemetryProbe.cpp
echo "  PASS  every FRONTIER_PROBE_* call site compiles out and links clean without the define"

echo "[telemetry-probe] GREEN — dev build records to RAM and saves at close; ship build carries no probe at all"
