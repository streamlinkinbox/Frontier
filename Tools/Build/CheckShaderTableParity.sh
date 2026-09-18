#!/usr/bin/env bash
#============================================================================================================================================
#                                                   CHECKSHADERTABLEPARITY.SH
#============================================================================================================================================
# CMakeLists.txt and ToolchainSequence.ps1 must lower the SAME set of shaders.
#
#    This gate exists because they did not, and the divergence cost a whole feature. ShadowResolve.slang and the two
#    ShadowRaster stages were listed in CMakeLists.txt but missing from the PowerShell table. On Linux/CMake the
#    shadows worked; on Windows — which is what the project is actually built and run on — the three .spv files were
#    never produced, the shadow stage could not bring up its pipelines, and every frame fell through to the unshadowed
#    path. The renderer looked like it had a shadow bug. It had a build-script bug.
#
#    Neither list is the authority: the gate asserts they are EQUAL, in both directions, so adding a shader to one and
#    forgetting the other fails here rather than in a screenshot.
#
#    usage: bash Tools/Build/CheckShaderTableParity.sh

set -euo pipefail
cd "$(dirname "$0")/../.."

Cmake="CMakeLists.txt"
Powershell="Projects/Project-Zero/Build/ToolchainSequence.ps1"

Stage="$(mktemp -d)"
trap 'rm -rf "$Stage"' EXIT

# CMake table entries look like:   "ShadowResolve.slang|compute|ShadowResolve.spv"
sed -n '/^set(SHADER_TABLE/,/^$/p' "$Cmake" \
  | grep -oE '"[A-Za-z0-9._]+\.slang\|[a-z]+\|[A-Za-z0-9._]+\.spv"' \
  | tr -d '"' | awk -F'|' '{print $1" "$2" "$3}' | sort > "$Stage/cmake.txt"

# PowerShell entries look like:    @{ Source = 'ShadowResolve.slang'; Stage = 'compute'; Output = 'ShadowResolve.spv' }
grep -oE "Source *= *'[^']+' *; *Stage *= *'[^']+' *; *Output *= *'[^']+'" "$Powershell" \
  | sed -E "s/Source *= *'([^']+)' *; *Stage *= *'([^']+)' *; *Output *= *'([^']+)'/\1 \2 \3/" \
  | sort > "$Stage/powershell.txt"

CmakeCount=$(wc -l < "$Stage/cmake.txt")
PowershellCount=$(wc -l < "$Stage/powershell.txt")
echo "[shader-parity] CMakeLists.txt       lowers $CmakeCount shaders"
echo "[shader-parity] ToolchainSequence.ps1 lowers $PowershellCount shaders"

if [ "$CmakeCount" -eq 0 ] || [ "$PowershellCount" -eq 0 ]; then
    echo "[shader-parity] FAIL — a table parsed as empty; the gate's pattern no longer matches the file." >&2
    exit 1
fi

if ! diff -u "$Stage/cmake.txt" "$Stage/powershell.txt" > "$Stage/delta.txt"; then
    echo
    echo "[shader-parity] FAIL — the two build scripts disagree about which shaders to compile."
    echo "[shader-parity]   '-' only in CMakeLists.txt (Windows builds would MISS these)"
    echo "[shader-parity]   '+' only in ToolchainSequence.ps1"
    echo
    sed -n '3,$p' "$Stage/delta.txt"
    exit 1
fi

echo "[shader-parity] GREEN — both build scripts lower the same $CmakeCount shaders"
