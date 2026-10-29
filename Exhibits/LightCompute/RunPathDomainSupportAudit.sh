#!/usr/bin/env bash
# 📝 Enumerate finite random-domain support for temporal reservoir reuse.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
MODE=release
FLAGS=(-O2)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    FLAGS=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
fi
BUILD="$ROOT/_ClaudeScratch/build/path-domain-support-$MODE"
mkdir -p "$BUILD" Exhibits/LightCompute/Results
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror "${FLAGS[@]}" \
    Exhibits/LightCompute/Code/PathDomainSupportAudit.cpp -o "$BUILD/PathDomainSupportAudit"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/PathDomainSupport-$MODE.txt}"
{
    printf 'Host build: %s\n' "$MODE"
    sha256sum Exhibits/LightCompute/Code/PathDomainSupportAudit.cpp
    "$BUILD/PathDomainSupportAudit"
} | tee "$RESULTS"
