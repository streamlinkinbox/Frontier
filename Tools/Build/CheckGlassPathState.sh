#!/usr/bin/env bash
# Glass path-state regression gate.
#
# The BSDF/furnace gate proves the local reflection/refraction math. This small source gate
# protects the renderer-side medium state that the CPU furnace cannot exercise: a reflection
# from the front face of solid glass must not inherit the interior Beer/exit state, and the
# active medium must be keyed by the instance/material pair rather than the primary material
# alone.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

python3 - <<'PY'
from pathlib import Path

source = Path("Engine/Shaders/ReSTIRViewport.slang").read_text()
checks = {
    "front-face starts outside the medium": "bool curInsideSolid = false;",
    "medium tracks the opening instance": "uint curMediumInstance = 0xFFFFFFFFu;",
    "medium tracks the opening material": "uint curMediumMaterial = 0xFFFFFFFFu;",
    "Beer uses carried medium sigma": "curThroughput *= exp(-curMediumSigma * bounceHit.t);",
    "miss fallback uses carried medium": "curThroughput *= exp(-curMediumSigma * curMediumFallbackThickness);",
    "exit is identity matched": "bounceHit.instance == curMediumInstance",
    "GI pool excludes interior paths": "&& !curInsideSolid",
    "solid interface toggles only on transmission": "bool crossesSolidInterface = !curFrame.ThinWalled",
}
missing = [name for name, text in checks.items() if text not in source]
for name, text in checks.items():
    if text in source:
        print(f"  PASS  {name}")
if "curEnteredSolid" in source:
    missing.append("stale curEnteredSolid state")
if missing:
    for name in missing:
        print(f"  FAIL  {name}")
    raise SystemExit(1)
print("[GlassPathState] GREEN — reflection/refraction medium-state regression checks passed")
PY
