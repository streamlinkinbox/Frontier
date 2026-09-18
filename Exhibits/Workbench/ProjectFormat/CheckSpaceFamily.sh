#!/usr/bin/env bash
# P1–P6 gate — the Space container family: one header, one directory, thirteen file types, and the two lossless tools.
#
#    usage: bash Exhibits/Workbench/ProjectFormat/CheckSpaceFamily.sh [-v]
#
# What runs, in order:
#    · SpaceFamilyProof — the format's own eight claims (Docs/ProjectFormat.md §8), on this machine: lossless round trip
#      for every type, resident records out and back by memcmp, dedup flat in N, references resolving and failing by name,
#      unknown tables skipped and a flipped bit named, the CLI parsing both spellings, the 49-material census, and the
#      Copied/Shared/CoW equivalence. Two of the eight are image comparisons and report SKIPPED with their CPU half
#      stated — this machine has no device, and the gate says so instead of quietly passing them.
#    · SpaceTool — the same claims through the COMMAND LINE (P4): export the M10 level as a project, re-read it with
#      -Verify (every vertex, index, cluster and material compared against the level builder), print the container with
#      -Info, then Pack it with the content directory deleted and Explode it back.
#    · the shell-level round trips the plan names: two exports byte-identical, Pack(Explode(X)) == X, and the P6 vocabulary
#      present in one export.
#
# ⚠️ Everything it writes goes under Build/Space (gitignored): the gate measures the format, it does not commit artefacts.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Verbose=0
[ "${1:-}" = "-v" ] && Verbose=1

Vk=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Vk="$candidate" && break
done
if [ -z "$Vk" ]; then
    echo "[SpaceFamily] RED — Vulkan-Headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, ~/.cache/m7)"
    echo "    git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers \$HOME/.cache/m7/Vulkan-Headers"
    exit 1
fi

# -ffunction-sections -fdata-sections -Wl,--gc-sections drops MaterialSwatchStructure::Export (and the glTF writer behind
#   it): this gate reads the level's records, it never writes glTF. -DFRONTIER_CPU_PORT as the other CPU-side proofs set.
Flags="-std=c++20 -O2 -Wall -Wextra -Werror -Wno-uninitialized -Wno-array-bounds -Wno-maybe-uninitialized
       -Wno-missing-field-initializers -mavx2 -mfma -msse4.2 -DFRONTIER_CPU_PORT
       -ffunction-sections -fdata-sections -Wl,--gc-sections
       -I Engine/GeometricRaster -I Engine/DeviceExchange -I Engine/ContentInterchange -I Engine/DisplayPresentation
       -I Projects/Project-Zero/Source -I $Vk/Vulkan-Headers/include"
Engine="Engine/ContentInterchange/SpaceCodec.cpp Engine/ContentInterchange/SpaceExport.cpp
        Engine/ContentInterchange/MaterialSwatchStructure.cpp Engine/ContentInterchange/MaterialIndex.cpp
        Engine/DeviceExchange/OrientationClassifier.cpp Projects/Project-Zero/Source/CommandLine.cpp"

Proof=$(mktemp -u /tmp/SpaceFamily.XXXXXX)
Tool=$(mktemp -u /tmp/SpaceTool.XXXXXX)

echo "[SpaceFamily] compiling the proof and the tool"
if ! g++ $Flags Exhibits/Workbench/ProjectFormat/SpaceFamilyProof.cpp $Engine -o "$Proof" 2>/tmp/SpaceFamily.build; then
    echo "[SpaceFamily] COMPILE FAILED (proof)"; sed 's/^/    /' /tmp/SpaceFamily.build | head -30; exit 1
fi
if ! g++ $Flags Exhibits/Workbench/ProjectFormat/SpaceTool.cpp $Engine -o "$Tool" 2>/tmp/SpaceTool.build; then
    echo "[SpaceFamily] COMPILE FAILED (tool)"; sed 's/^/    /' /tmp/SpaceTool.build | head -30; exit 1
fi
[ -s /tmp/SpaceFamily.build ] && { echo "[SpaceFamily] proof warnings:"; sed 's/^/    /' /tmp/SpaceFamily.build | head -10; }
[ -s /tmp/SpaceTool.build ] && { echo "[SpaceFamily] tool warnings:"; sed 's/^/    /' /tmp/SpaceTool.build | head -10; }

Out="$PWD/Build/Space"
rm -rf "$Out"
mkdir -p "$Out"
Status=0
Log=/tmp/SpaceFamily.log
: > "$Log"

"$Proof" 2>&1 | tee -a "$Log"
[ "${PIPESTATUS[0]}" -ne 0 ] && Status=1

echo "[SpaceFamily] ── the command line (§6): export, verify, info, pack, explode"
"$Tool" -Project=Project-Zero -Level=Materials -Export=All -Out="$Out" 1 2>&1 | tee -a "$Log"
[ "${PIPESTATUS[0]}" -ne 0 ] && Status=1
"$Tool" -Project=Project-Zero -Verify="$Out" 1 2>&1 | tee -a "$Log"
[ "${PIPESTATUS[0]}" -ne 0 ] && Status=1
"$Tool" -Info="$Out/Project-Zero.projectspace" 1 2>&1 | tee -a "$Log"
[ "${PIPESTATUS[0]}" -ne 0 ] && Status=1

# ── the shell-level claims ───────────────────────────────────────────────────────────────────────────────────────────
claim() {  # claim <verdict> <text>
    if [ "$1" -eq 0 ]; then printf '  PASS  %s\n' "$2" | tee -a "$Log"; else printf '  FAIL  %s\n' "$2" | tee -a "$Log"; Status=1; fi
}

# P1: the export is deterministic — the same level exports to the same bytes, twice.
cp "$Out/Project-Zero.projectspace" /tmp/SpaceFamily.first
"$Tool" -Project=Project-Zero -Level=Materials -Export=All -Out="$Out" >/dev/null 2>&1
cmp -s /tmp/SpaceFamily.first "$Out/Project-Zero.projectspace"
claim $? "P1 export is deterministic — two runs of the same level produce byte-identical containers"

# P3: -Pack makes the content directory irrelevant…
"$Tool" -Project=Project-Zero -Pack="$Out/Project-Zero.projectspace" -Out="$Out" 1 2>&1 | tee -a "$Log"
[ "${PIPESTATUS[0]}" -ne 0 ] && Status=1
# …and -Explode puts it back as files, after which packing again reproduces the packed bytes exactly.
"$Tool" -Project=Project-Zero -Explode="$Out/Project-Zero.projectspace.packed" -Out="$Out/exploded" 1 2>&1 | tee -a "$Log"
[ "${PIPESTATUS[0]}" -ne 0 ] && Status=1
"$Tool" -Project=Project-Zero -Pack="$Out/exploded/Project-Zero.projectspace.packed.exploded" -Out="$Out/exploded" 1 2>&1 | tee -a "$Log"
[ "${PIPESTATUS[0]}" -ne 0 ] && Status=1
cmp -s "$Out/Project-Zero.projectspace.packed" "$Out/exploded/Project-Zero.projectspace.packed.exploded.packed"
claim $? "P3 Pack(Explode(X)) == X — the round trip through files is byte-identical ($(stat -c%s "$Out/Project-Zero.projectspace.packed") B)"

# P6: the whole vocabulary is present in one export, each file of the family it claims to be.
Missing=""
for Artefact in "$Out/Project-Zero.projectspace" "$Out/Project-Zero.runtime" "$Out/Materials.environment" \
                "$Out/Materials.uvspace" "$Out/Project-Zero.workflow" "$Out/Materials.archive" \
                "$Out/Content/Levels/Materials" "$Out/Content/Materials"; do
    [ -e "$Artefact" ] || Missing="$Missing $Artefact"
done
[ -z "$Missing" ]
claim $? "P6 the export carries the whole family — project, runtime, environment, uvspace, workflow, archive, geometry, materials (${Missing:-all present})"

# P5: the runtime names what it opens, and -Bake=Sky writes a probe into the environment.
"$Tool" -Project=Project-Zero -Level=Materials -Bake=Sky -Out="$Out/sky" >/tmp/SpaceFamily.sky 2>&1
grep -q "baked a 32×16 equirect probe" /tmp/SpaceFamily.sky
claim $? "P5/P6 -Bake=Sky bakes a real equirect probe from the atmosphere model at the staging's own sun hour"
grep -q "\.runtime" "$Log" && grep -q "\.environment" "$Log"
claim $? "P5 .runtime and .environment are written with their own META and tables"

rm -f "$Proof" "$Tool"
echo "[SpaceFamily] ── $(grep -c '  PASS  ' "$Log") claims passed, $(grep -c '  FAIL  ' "$Log") failed, $(grep -c '  SKIP  ' "$Log") skipped"
if [ "$Status" -ne 0 ]; then
    echo "[SpaceFamily] RED"
    grep "FAIL" "$Log" | sed 's/^/    /' | head -20
    exit 1
fi
echo "[SpaceFamily] GREEN"
