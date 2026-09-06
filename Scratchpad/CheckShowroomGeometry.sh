#!/usr/bin/env bash
# Sandbox proof for ShowroomStructure: constructs the level in-process and asserts the geometry invariants
#    (normal/triangle parity, material range, room bounds, no degenerates, unit normals, luminaire-last,
#    and the emissive/albedo contract that makes the interface a real light source in the path tracer).
#
# Compiles against the REAL engine headers -- no stubs. SceneCodec::Encode is the only symbol stubbed out,
#    because exercising the glTF writer is not what this proof is about.
set -euo pipefail
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
Vkh="${1:-/tmp/vkh/include}"
Work="$(mktemp -d)"; trap 'rm -rf "$Work"' EXIT
g++ -std=c++20 -I "$Vkh" -I "$Root" \
    "$Root/Scratchpad/ShowroomGeometryProof.cpp" \
    "$Root/Projects/Project-Zero/Source/ShowroomStructure.cpp" \
    "$Root/Engine/DeviceExchange/OrientationClassifier.cpp" \
    -o "$Work/check"
"$Work/check"
echo "[Showroom] OK"
