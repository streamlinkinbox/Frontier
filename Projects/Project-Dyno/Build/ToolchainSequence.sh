#!/usr/bin/env bash
# Frontier/Projects/Project-Dyno/Build/ToolchainSequence.sh
#   Builds Project-Dyno with g++ (>= 12) or clang++ (>= 15) directly. No Vulkan, no GLFW, no shaders — audio only.
#
#     bash Projects/Project-Dyno/Build/ToolchainSequence.sh            # Release
#     bash Projects/Project-Dyno/Build/ToolchainSequence.sh debug      # Debug (-O0 -g, FRONTIER_DEBUG=1)
#     bash Projects/Project-Dyno/Build/ToolchainSequence.sh --rebuild  # discard objects first
#     bash Projects/Project-Dyno/Build/ToolchainSequence.sh --run -- --null --seconds 3
#
#   Output: Projects/Project-Dyno/Build/Output/Linux/<Configuration>/Binary/Project-Dyno
#   Requires: ExternalPackages/miniaudio (git submodule update --init -- ExternalPackages/miniaudio)

set -euo pipefail

ScriptRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RepositoryRoot="$(cd "$ScriptRoot/../../.." && pwd)"
EngineRoot="$RepositoryRoot/Engine"
PackageRoot="$RepositoryRoot/ExternalPackages"
ProjectRoot="$RepositoryRoot/Projects/Project-Dyno"

Configuration="Release"
Rebuild=0
Run=0
RunArguments=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        debug|Debug)      Configuration="Debug" ;;
        release|Release)  Configuration="Release" ;;
        --rebuild)        Rebuild=1 ;;
        --run)            Run=1 ;;
        --)               shift; RunArguments=("$@"); break ;;
        *)                echo "[FAILED]   unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

OutputRoot="$ProjectRoot/Build/Output/Linux/$Configuration"
ObjectRoot="$OutputRoot/Object"
BinaryRoot="$OutputRoot/Binary"

Compiler="${CXX:-}"
if [[ -z "$Compiler" ]]; then
    if command -v g++ >/dev/null 2>&1; then Compiler=g++; elif command -v clang++ >/dev/null 2>&1; then Compiler=clang++; else
        echo "[FAILED]   neither g++ nor clang++ is on PATH" >&2; exit 1; fi
fi

if [[ ! -f "$PackageRoot/miniaudio/miniaudio.h" ]]; then
    echo "[Build]    initialising ExternalPackages/miniaudio submodule..."
    (cd "$RepositoryRoot" && git submodule update --init -- ExternalPackages/miniaudio) || {
        echo "[FAILED]   ExternalPackages/miniaudio is missing and could not be fetched" >&2; exit 1; }
fi

CommonFlags=(-std=c++20 -Wall -Wextra -pthread -DFRONTIER_DEVELOPMENT)
if [[ "$Configuration" == "Debug" ]]; then
    CommonFlags+=(-O0 -g -DFRONTIER_DEBUG=1)
else
    CommonFlags+=(-O2 -g -DNDEBUG)
fi

IncludePaths=(-I"$RepositoryRoot" -I"$EngineRoot" -I"$ProjectRoot/Source" -I"$PackageRoot/miniaudio" -I"$PackageRoot/tomlpp/include" -I"$PackageRoot/stb")

# NOTE: this list must match the .cpp files actually in the tree — the script fails fast with names if it rots.
Sources=(
    "Engine/DeviceExchange/DiagnosticMetrics.cpp"
    "Engine/PlatformInterchange/MiniaudioTranslation.cpp"
    "Engine/PlatformInterchange/WaveCodec.cpp"
    "Engine/PlatformInterchange/AudioExchange.cpp"
    "Projects/Project-Dyno/Source/DynoSequence.cpp"
    "Projects/Project-Dyno/Source/CrankClickIntegrator.cpp"
    "Projects/Project-Dyno/Source/GameExecution.cpp"
)

Missing=()
for Relative in "${Sources[@]}"; do [[ -f "$RepositoryRoot/$Relative" ]] || Missing+=("$Relative"); done
if [[ ${#Missing[@]} -gt 0 ]]; then
    echo "[FAILED]   missing source files in the translation batch:" >&2
    printf '           %s\n' "${Missing[@]}" >&2
    exit 1
fi

[[ $Rebuild -eq 1 ]] && rm -rf "$ObjectRoot"
mkdir -p "$ObjectRoot" "$BinaryRoot"

Objects=()
Translated=0
for Relative in "${Sources[@]}"; do
    Source="$RepositoryRoot/$Relative"
    Stem="$(basename "$Relative" .cpp)"
    Object="$ObjectRoot/$Stem.o"
    Dependency="$ObjectRoot/$Stem.d"
    Objects+=("$Object")

    Fresh=0
    if [[ -f "$Object" && -f "$Dependency" ]]; then
        Fresh=1
        # Every header the compiler recorded must be older than the object
        while read -r Header; do
            [[ -z "$Header" ]] && continue
            [[ "$Header" -nt "$Object" ]] && { Fresh=0; break; }
        done < <(tr ' \\' '\n\n' < "$Dependency" | grep -v ':$' | grep -v '^$')
    fi
    [[ $Fresh -eq 1 ]] && continue

    # miniaudio's TU is third-party: warnings silenced there only (same rule as UfbxTranslation.cpp)
    Extra=()
    [[ "$Stem" == "MiniaudioTranslation" ]] && Extra=(-w)
    "$Compiler" "${CommonFlags[@]}" "${Extra[@]}" "${IncludePaths[@]}" -MMD -MF "$Dependency" -c "$Source" -o "$Object"
    Translated=$((Translated + 1))
done

if [[ $Translated -eq 0 ]]; then
    echo "[SKIP]     Project-Dyno unchanged"
else
    echo "[Build]    Project-Dyno - translated $Translated of ${#Sources[@]}"
fi

Executable="$BinaryRoot/Project-Dyno"
"$Compiler" "${CommonFlags[@]}" "${Objects[@]}" -o "$Executable" -ldl -lm -lpthread
echo "[Compiled] $Executable"

if [[ $Run -eq 1 ]]; then
    echo "[Build]    Launching Project-Dyno (working directory = repository root)..."
    (cd "$RepositoryRoot" && "$Executable" "${RunArguments[@]}")
    echo "[Build]    Project-Dyno exited with code $?"
fi
