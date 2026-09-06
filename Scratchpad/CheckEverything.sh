#!/usr/bin/env bash
# Runs every headless gate in the repository, in dependency order, and reports one line each.
#
# This exists so "is the tree healthy?" is one command rather than seventeen. Each script is self-contained and
#    fetches what it needs, so a fresh clone with submodules initialised can run this directly.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

Passed=0
Failed=0
FailedNames=""

Run()
{
    printf "  %-30s " "$1"
    if bash "Scratchpad/$1.sh" >"/tmp/CheckEverything.$1.log" 2>&1; then
        tail -1 "/tmp/CheckEverything.$1.log"
        Passed=$((Passed + 1))
    else
        echo "FAILED  (see /tmp/CheckEverything.$1.log)"
        tail -3 "/tmp/CheckEverything.$1.log" | sed 's/^/      /'
        Failed=$((Failed + 1))
        FailedNames="$FailedNames $1"
    fi
}

echo
echo "=== build and toolchain ==="
Run CheckBuildIntegrity

echo
echo "=== spatial interface (P0 - P4) ==="
Run CheckPanelPlacement
Run CheckPointerProjection
Run CheckTextProjection
Run CheckScreenSequence
Run CheckVectorCodec
Run CheckLightProjection
Run CheckPanelSample
Run CheckInterfaceAudio

echo
echo "=== scene and dynamic geometry (D1 - D6) ==="
Run CheckShowroomGeometry
Run CheckTraversalIdentity
Run CheckHitIdentity
Run CheckInstanceMotion
Run CheckPhysicsInstances
Run CheckTraversalRefit
Run CheckTracedGeometry
Run CheckDynamicGeometryBudget

echo
echo "=== browser-side testbeds (source integrity only; DSP and WebGPU need a browser) ==="
Run CheckAcousticArchives
Run CheckFluidProject

echo
echo "=== shaders ==="
Run CompileInterfaceShaders

echo
if [ $Failed -eq 0 ]; then
    echo ">>> ALL $Passed SUITES GREEN"
else
    echo ">>> $Failed FAILED:$FailedNames  ($Passed passed)"
fi
exit $Failed
