#!/usr/bin/env bash
# M0 gate — the material programme's executable baseline. Compiles the dependency-free CPU proofs with the system
#    compiler (no Vulkan, no GLFW, no submodules) and runs them:
#      ① MaterialChannelCoverage — the plan §0 table as code (records + acknowledged-gap registry)
#      ② MaterialFurnaceProof   — MaterialEvaluation.slang compiled 1:1 as C++ (furnace / reciprocity / sampling)
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1
Fail=0

echo "[MaterialsProof] compiling the coverage proof (MaterialIndex only)"
CoverageBin="$(mktemp -u /tmp/MaterialsCoverage.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I Exhibits/Workbench/Materials -I Engine/ContentInterchange \
     Exhibits/Workbench/Materials/MaterialChannelCoverage.cpp \
     Engine/ContentInterchange/MaterialIndex.cpp \
     -o "$CoverageBin" 2>/tmp/MaterialsProof.coverage.build; then
    echo "  COVERAGE COMPILE FAILED"; sed 's/^/    /' /tmp/MaterialsProof.coverage.build | head -30; exit 1
fi
if ! "$CoverageBin" 2>&1 | tee /tmp/MaterialsProof.coverage.log | grep -q "MATERIAL COVERAGE: PASS"; then
    echo "  COVERAGE FAILED"; tail -20 /tmp/MaterialsProof.coverage.log | sed 's/^/    /'; Fail=1
else
    grep -h "FAIL" /tmp/MaterialsProof.coverage.log | sed 's/^/    /' || true
fi
rm -f "$CoverageBin"

echo "[MaterialsProof] compiling the furnace proof (slang-as-C++ against baked tables)"
FurnaceBin="$(mktemp -u /tmp/MaterialsFurnace.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -DFRONTIER_CPU_PORT -I Exhibits/Workbench/Materials \
     -I Engine/DisplayPresentation -I Engine/Shaders \
     Exhibits/Workbench/Materials/MaterialFurnaceProof.cpp \
     Engine/DisplayPresentation/ShadingTableCodec.cpp \
     -o "$FurnaceBin" 2>/tmp/MaterialsProof.furnace.build; then
    echo "  FURNACE COMPILE FAILED"; sed 's/^/    /' /tmp/MaterialsProof.furnace.build | head -40; exit 1
fi
if ! "$FurnaceBin" 2>&1 | tee /tmp/MaterialsProof.furnace.log | grep -q "MATERIAL FURNACE: PASS"; then
    echo "  FURNACE FAILED"; grep "FAIL" /tmp/MaterialsProof.furnace.log | sed 's/^/    /' | head -20; Fail=1
fi
rm -f "$FurnaceBin"

[ "$Fail" -eq 0 ] && echo "[MaterialsProof] GREEN — coverage + furnace pass" || echo "[MaterialsProof] RED"
exit "$Fail"
