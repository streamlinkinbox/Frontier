#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"
python3 Exhibits/Workbench/IconArt/PrepareTarget.py
python3 Exhibits/Workbench/IconArt/BuildProof.py
mkdir -p Exhibits/Gallery/IconArt
.cache/icon-art/release/IconArtProof EngineContent/Icons Exhibits/Gallery/IconArt .cache/icon-art/fixtures | tee Exhibits/Gallery/IconArt/Proof.txt
# Requires the web reference's pinned npm dependencies (npm ci); it is a separate CPU SVG decoder.
node Exhibits/Workbench/IconArt/CompareIcons.mjs
python3 Exhibits/Workbench/IconArt/BuildProof.py --sanitize
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .cache/icon-art/sanitized/IconArtProof \
    EngineContent/Icons .cache/icon-art/sanitized-output .cache/icon-art/sanitized-fixtures 2>&1 | tee Exhibits/Gallery/IconArt/Sanitizers.txt
echo 0 > Exhibits/Gallery/IconArt/SanitizerExitCode.txt
python3 Exhibits/Workbench/IconArt/RecordProof.py
