#!/usr/bin/env bash
# Project-Fluid is a browser/WebGPU testbed with no compiler, so there is nothing to link here. What CAN be
#    checked headlessly is that the source tree is complete, every module parses, and the shaders carry entry
#    points — enough to catch a truncated or half-merged import, which is the failure this guards against.
#
# The simulation itself can only be judged in a WebGPU browser:
#    powershell -File Projects\Project-Fluid\Build\ToolchainSequence.ps1 -Proof
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

Failed=0

# The project's own check: file inventory and 142-char headers.
bash Projects/Project-Fluid/Build/ToolchainSequence.sh --check || Failed=1

# Every JS module must parse. A merge that truncated a file would otherwise only surface in a browser.
if command -v node >/dev/null 2>&1; then
    for Module in Projects/Project-Fluid/Source/*.js; do
        node --check "$Module" >/dev/null 2>&1 || { echo "  PARSE FAILED: $Module"; Failed=1; }
    done
    echo "  $(ls Projects/Project-Fluid/Source/*.js | wc -l) JS modules parse"
else
    echo "  node absent - JS parse check skipped"
fi

# The WGSL shaders must at least be present and carry entry points.
for Shader in Projects/Project-Fluid/Source/Shaders/*.wgsl; do
    Entries=$(grep -c "@compute\|@vertex\|@fragment" "$Shader")
    [ "$Entries" -gt 0 ] || { echo "  NO ENTRY POINTS: $Shader"; Failed=1; }
done
echo "  $(ls Projects/Project-Fluid/Source/Shaders/*.wgsl | wc -l) WGSL shaders carry entry points"

[ $Failed -eq 0 ] && echo "[Fluid] OK"
exit $Failed
