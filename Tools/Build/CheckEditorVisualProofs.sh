#!/usr/bin/env bash
# One maintained source list for the native SolidArc and shared game-editor proofs.
# Compile/link failures and UI failures propagate; the legacy game-editor checks are not silently skipped.
set -euo pipefail
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
exec python3 "$Root/Exhibits/Workbench/FrontierMirror/RunSolidArcMirror.py" --game-ui "$@"
