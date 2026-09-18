#!/usr/bin/env bash
# Keeps the two Windows build paths in agreement about which TUs make Project-Zero.exe.
#
#    On 2026-09-18 the user's build linked 82 of 82 objects and came back with 14 unresolved externals:
#    MaterialInspector (ControlCentreHost + GameExecution), InstanceAcceleration (GameExecution's D6/D7 scene), and
#    RenderShaderballPreview (M7b). All three implementations existed in the tree; they were simply not in
#    `ToolchainSequence.ps1`'s `$EngineRelative`. The list's own guard could not see it — it checks that every LISTED
#    file EXISTS, and the failure mode here is the opposite one: a file that exists, is referenced by the app, and is
#    not listed. CMakeLists.txt had them, which is why CMake builds and the PowerShell build did not.
#
#    This gate is the missing half, and it is deliberately dependency-free (bash + awk/sed/grep, like the other gates):
#
#      ① every CMake-listed TU exists on disk                        (a renamed file, from CMake's side)
#      ② every PS1-listed TU exists on disk                          (the PS1's own guard, re-checked here)
#      ③ CMake ⊆ PS1: nothing CMake compiles for the exe may be absent from the PS1 batch — this is the check that
#        would have caught 2026-09-18 before the user ever ran the build;
#      ④ every .cpp under Engine/ and Projects/Project-Zero/Source/ is either in a list or named in the allowlist
#        below with a reason — so a new TU cannot quietly land in neither build system;
#      ⑤ per-file overrides agree: every TU CMake gives extra defines/include dirs gets the same treatment here.
#      ⑥ the headless CPU reference agrees across all THREE of its lists (CMake, the Makefile, Construct.ps1) — it is
#        built on the sandbox side too, so a divergence there breaks the CPU work as well as the product;
#      ⑦ Project-Dyno agrees between CMake and its own ToolchainSequence.ps1.
#
#    Every list in the repo that names .cpp files by hand is covered here. If a new build target grows one, add it.
#
#    Usage: CheckBuildSourceList.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

CMakeFile='CMakeLists.txt'
Ps1File='Projects/Project-Zero/Build/ToolchainSequence.ps1'
Work="$(mktemp -d /tmp/BuildSourceList.XXXXXX)"
trap 'rm -rf "$Work"' EXIT

Failures=0
Pass() { echo "  PASS  $1"; }
Fail() { echo "  FAIL  $1"; Failures=$((Failures + 1)); }

# ── extract the CMake source lists (the `set(NAME ...)` blocks whose entries are bare .cpp paths) ─────────────────────
CmakeList() { # $1 = variable name
    awk -v Name="$1" '
        $0 ~ "^set\\(" Name "[ \t]*$" || $0 ~ "^set\\(" Name "[ \t]+" { Inside = 1; Next }
        Inside && $0 ~ /^\)/ { Inside = 0 }
        Inside { print }
    ' "$CMakeFile" | grep -oE '^[[:space:]]*[A-Za-z][A-Za-z0-9_/.-]*\.cpp' \
        | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' | tr '/' '\\' | sort -u
}
CmakeList FRONTIER_ENGINE_SOURCES  > "$Work/cmake_engine.txt"
CmakeList PROJECT_ZERO_SOURCES     > "$Work/cmake_project.txt"
cat "$Work/cmake_engine.txt" "$Work/cmake_project.txt" | sort -u > "$Work/cmake.txt"

# ── extract the PS1 list ($EngineRelative = @( ... ) — the batch that becomes Project-Zero.exe) ───────────────────────
sed -n '/^\$EngineRelative = @(/,/^)/p' "$Ps1File" \
    | grep -oE "'[A-Za-z][^']*\.cpp'" | tr -d "'" | sed 's|\\\\|\\|g' | sort -u > "$Work/ps1.txt"

CmakeCount=$(wc -l < "$Work/cmake.txt")
Ps1Count=$(wc -l < "$Work/ps1.txt")
echo "[BuildSourceList] CMake names $CmakeCount TUs for Project-Zero.exe, the PowerShell script $Ps1Count"

# ① every CMake entry exists
Missing=0
while IFS= read -r Rel; do
    [ -f "${Rel//\\//}" ] || { echo "    CMake lists a file that is not in the tree: $Rel"; Missing=$((Missing + 1)); }
done < "$Work/cmake.txt"
[ "$Missing" -eq 0 ] && Pass "① all $CmakeCount CMake-listed TUs exist on disk" \
                     || Fail "① $Missing CMake-listed TU(s) missing from the tree"

# ② every PS1 entry exists
Missing=0
while IFS= read -r Rel; do
    [ -f "${Rel//\\//}" ] || { echo "    the PowerShell list names a file that is not in the tree: $Rel"; Missing=$((Missing + 1)); }
done < "$Work/ps1.txt"
[ "$Missing" -eq 0 ] && Pass "② all $Ps1Count PowerShell-listed TUs exist on disk" \
                     || Fail "② $Missing PowerShell-listed TU(s) missing from the tree"

# ③ CMake ⊆ PS1 — the check the 2026-09-18 link failure needed
comm -23 "$Work/cmake.txt" "$Work/ps1.txt" > "$Work/gap.txt"
if [ -s "$Work/gap.txt" ]; then
    Fail "③ $(wc -l < "$Work/gap.txt") TU(s) CMake compiles for the exe are NOT in the PowerShell batch (add them, or they link as unresolved externals):"
    sed 's/^/          /' "$Work/gap.txt"
else
    Pass "③ every CMake-listed TU is in the PowerShell batch — the two build systems agree"
fi

# ④ every engine/app TU is accounted for
#    The allowlist exists so that "not linked into the exe" is a DECISION with a reason, never an omission. Anything
#    here that gains a consumer must move into both lists.
cat > "$Work/allow.txt" <<'ALLOW'
Engine\GeometricRaster\VisibilityRaster.cpp|the CPU visibility mirror the shadow proofs read; no exe consumer yet
Projects\Project-Zero\Source\RendererHost.cpp|linked into the separate Project-Zero-CpuReference target, not the showroom
Projects\Project-Zero\Source\SkyFogIntegrator.cpp|RendererHost's dependency; CpuReference target only
ALLOW

cut -d'|' -f1 < "$Work/allow.txt" | sort -u > "$Work/allow_paths.txt"
: > "$Work/orphans.txt"
while IFS= read -r Abs; do
    Rel="${Abs#./}"; WinRel="${Rel//\//\\}"
    grep -qxF "$WinRel" "$Work/allow_paths.txt" && continue
    grep -qxF "$WinRel" "$Work/cmake.txt" && continue
    grep -qxF "$WinRel" "$Work/ps1.txt" && continue
    echo "$WinRel" >> "$Work/orphans.txt"
done < <(find Engine Projects/Project-Zero/Source -name '*.cpp' | sort)
# A TU with its own entry point is a tool/runner/gate, not part of the exe: it is linked by its own build line.
: > "$Work/orphan_final.txt"
while IFS= read -r Rel; do
    grep -qE '\bint[[:space:]]+main[[:space:]]*\(' "${Rel//\\//}" && continue
    echo "$Rel" >> "$Work/orphan_final.txt"
done < "$Work/orphans.txt"
if [ -s "$Work/orphan_final.txt" ]; then
    Fail "④ $(wc -l < "$Work/orphan_final.txt") engine/app TU(s) are in NEITHER build system and not in this gate's allowlist:"
    sed 's/^/          /' "$Work/orphan_final.txt"
    echo "          (add them to both lists, or name them in the allowlist above with a reason)"
else
    Pass "④ every engine/app TU without its own main() is in a build list or explicitly accounted for"
fi

# ⑤ per-file overrides agree on both sides
grep -q "SHADERBALL_PREVIEW_LIB" "$CMakeFile" || Fail "⑤ CMake no longer sets SHADERBALL_PREVIEW_LIB on the preview TU"
grep -q "SHADERBALL_PREVIEW_LIB" "$Ps1File"   || Fail "⑤ the PowerShell script does not set SHADERBALL_PREVIEW_LIB on the preview TU"
#    ShaderballExhibit.cpp is the editor's material-preview renderer and is DEVELOPMENT-ONLY: it #includes
#    MaterialEvaluation.slang as host C++, which a shipping build has no use for. It therefore no longer sits in the
#    unconditional list on either side — CMake adds it under FRONTIER_ZERO_DEVELOPMENT, the PowerShell batch under
#    -Development. What must still agree is that BOTH gate it and BOTH set SHADERBALL_PREVIEW_LIB on it; a file
#    conditional on one side and unconditional on the other is the rot this check exists to catch.
if grep -q "SHADERBALL_PREVIEW_LIB" "$CMakeFile" && grep -q "SHADERBALL_PREVIEW_LIB" "$Ps1File"; then
    CmakeGated=0; Ps1Gated=0
    grep -q "FRONTIER_ZERO_DEVELOPMENT" "$CMakeFile" && \
        grep -A3 "if(FRONTIER_ZERO_DEVELOPMENT)" "$CMakeFile" | grep -q "ShaderballExhibit.cpp" && CmakeGated=1
    grep -B3 "ShaderballExhibit.cpp'   # M7b preview entry" "$Ps1File" | grep -q 'if ($Development)' && Ps1Gated=1

    if [ "$CmakeGated" -eq 1 ] && [ "$Ps1Gated" -eq 1 ]; then
        Pass "⑤ ShaderballExhibit.cpp is development-only in BOTH build systems, with SHADERBALL_PREVIEW_LIB on both"
    elif [ "$CmakeGated" -eq 0 ] && [ "$Ps1Gated" -eq 0 ] && \
         grep -q "ShaderballExhibit.cpp" "$Work/cmake.txt" && \
         grep -qxF 'Exhibits\Workbench\Materials\ShaderballExhibit.cpp' "$Work/ps1.txt"; then
        Pass "⑤ ShaderballExhibit.cpp is unconditional in both batches, with SHADERBALL_PREVIEW_LIB on both"
    else
        Fail "⑤ ShaderballExhibit.cpp is gated in one build system and not the other (CMake gated=$CmakeGated, PowerShell gated=$Ps1Gated)"
    fi
fi

# ── ⑥/⑦ the other hand-maintained lists ──────────────────────────────────────────────────────────────────────────────
Compare() { # $1 = label, $2/$3 = files with one TU per line, $4/$5 = their names
    if diff -q "$2" "$3" > /dev/null; then
        Pass "$1"
    else
        Fail "$1"
        diff "$2" "$3" | sed 's/^</        only in '"$4"': /; s/^>/        only in '"$5"': /' | grep -v '^[0-9-]' | head -12
    fi
}

# CPU reference — the Makefile, normalised to repo-relative Windows paths the way the other extractors do.
sed -n '/^SRCS :=/,/^OBJS :=/p' Projects/Project-Zero/Makefile \
    | grep -oE '[A-Za-z][A-Za-z0-9_/.-]*\.cpp' \
    | sed 's|^\./||' \
    | awk '{ if ($0 ~ /^Source\//) { sub(/^Source\//, ""); print "Projects\\Project-Zero\\Source\\" $0 }
             else { sub(/^\.\.\/\.\.\//, ""); gsub(/\//, "\\\\"); print } }' \
    | sort -u > "$Work/cpu_make.txt"

# CPU reference — Construct.ps1's $SourceFiles, normalised through its Join-Path roots. Read line by line on purpose:
#    a $Root inside a double-quoted pattern would be a SHELL SUBSTITUTION, which is how this extractor returned an
#    empty list the first time it ran — and a gate that silently checks nothing is worse than no gate at all.
sed -n '/^\$SourceFiles = @(/,/^)/p' Projects/Project-Zero/Build/Construct.ps1 > "$Work/construct.raw"
: > "$Work/cpu_construct.txt"
while IFS= read -r Line; do
    Rel=$(printf '%s' "$Line" | grep -oE "'[^']+\.cpp'" | tr -d "'")
    [ -z "$Rel" ] && continue
    Rel="${Rel//\//\\}"
    case "$Line" in
        *'ProjectRoot'*)      echo "Projects\\Project-Zero\\$Rel" >> "$Work/cpu_construct.txt" ;;
        *'RepositoryRoot'*)   echo "$Rel" >> "$Work/cpu_construct.txt" ;;
    esac
done < "$Work/construct.raw"
sort -u "$Work/cpu_construct.txt" -o "$Work/cpu_construct.txt"

CmakeList PROJECT_ZERO_CPU_SOURCES | sort -u > "$Work/cpu_cmake.txt"
Compare "⑥ the CPU reference names the same TUs in CMake, the Makefile and Construct.ps1" \
        "$Work/cpu_cmake.txt" "$Work/cpu_make.txt" CMake Makefile
Compare "⑥ …and Construct.ps1 agrees with CMake too" \
        "$Work/cpu_cmake.txt" "$Work/cpu_construct.txt" CMake Construct.ps1

CmakeList PROJECT_DYNO_SOURCES | sort -u > "$Work/dyno_cmake.txt"
sed -n '/^\$EngineRelative = @(/,/^)/p' Projects/Project-Dyno/Build/ToolchainSequence.ps1 \
    | grep -oE "'[A-Za-z][^']*\.cpp'" | tr -d "'" | sed 's|\\\\|\\|g' | sort -u > "$Work/dyno_ps.txt"
Compare "⑦ Project-Dyno names the same TUs in CMake and its ToolchainSequence.ps1" \
        "$Work/dyno_cmake.txt" "$Work/dyno_ps.txt" CMake Dyno.ps1

echo
if [ "$Failures" -eq 0 ]; then
    echo "[BuildSourceList] GREEN — every hand-maintained source list names the same translation units"
    exit 0
fi
echo "[BuildSourceList] RED — $Failures check(s) failed"
exit 1
