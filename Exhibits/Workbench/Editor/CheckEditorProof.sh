#!/usr/bin/env bash
# Link the shared inspector and its complete dependencies before exercising both native editor hosts.
set -euo pipefail
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
exec python3 "$Root/Exhibits/Workbench/FrontierMirror/RunSolidArcMirror.py" --game-ui "$@"
