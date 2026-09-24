#!/usr/bin/env bash
#============================================================================================================================================
#                                                 CHECKPERFORMANCETELEMETRY.SH
#============================================================================================================================================
# Runs Project-Zero's PerformanceTelemetrySequence and reads back the report it writes.
#
#    Not a source grep: the gate constructs the SHIPPED sequence, feeds it synthetic frames and a synthetic GPU
#    timing block, points it at a DiagnosticMetrics sink configured exactly as ProjectZero_TelemetryReport is, and
#    parses the resulting markdown. A report that would come out of the application without performance or GPU rows
#    comes out of this gate that way too.
#
#    usage: bash Tools/Build/CheckPerformanceTelemetry.sh

set -euo pipefail
cd "$(dirname "$0")/../.."

VULKAN_ROOT=""
for Candidate in ExternalPackages/Vulkan-Headers "${MATERIAL_SCENES_EXT:-}/Vulkan-Headers" "$HOME/.cache/m7/Vulkan-Headers"; do
    if [ -f "$Candidate/include/vulkan/vulkan.h" ]; then VULKAN_ROOT="$Candidate"; break; fi
done
if [ -z "$VULKAN_ROOT" ]; then
    echo "[perf-telemetry] Vulkan headers not found — VisibilityTelemetry is declared in a header that includes them." >&2
    echo "[perf-telemetry]   git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers ~/.cache/m7/Vulkan-Headers" >&2
    exit 2
fi
echo "[perf-telemetry] Vulkan headers: $VULKAN_ROOT"

Stage="$(mktemp -d)"
trap 'rm -rf "$Stage"' EXIT

g++ -std=c++20 -O1 -g -I. -I"$VULKAN_ROOT/include" \
    -o "$Stage/PerformanceTelemetryGate" \
    Tools/Build/Gates/PerformanceTelemetryGate.cpp \
    Projects/Project-Zero/Source/PerformanceTelemetrySequence.cpp \
    Engine/DeviceExchange/DiagnosticMetrics.cpp

# Run in the staging directory so the probe report is written and removed there, not in the tree.
( cd "$Stage" && "$Stage/PerformanceTelemetryGate" )
