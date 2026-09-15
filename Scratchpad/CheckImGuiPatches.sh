#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
#  CheckImGuiPatches.sh — Slate's ImGui divergence is present, tracked, idempotent, and reversible
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
#  These patches were lost once already: `git submodule update` restores the vendored tree to its pinned commit
#  and silently discards them. This gate exists so that loss is loud rather than silent.
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
set -u
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$Root" || exit 1
Fail=0

# ── The patches are tracked in the repository, not only in the vendored tree ────────────────────────────────────
echo "[ImGuiPatches] the divergence is tracked"
for Patch in PatchA-TrapezoidalTabs PatchB-TabOverlapZOrder PatchC-RoundTabButtons; do
    [ -f "Patches/$Patch.patch" ] || { echo "  Patches/$Patch.patch is missing"; Fail=1; }
done
[ -f Patches/Patches.md ]                 || { echo "  Patches/Patches.md is missing"; Fail=1; }
[ -f Scripts/ApplyImGuiPatches.py ]       || { echo "  the POSIX patcher is missing"; Fail=1; }
[ -f Scripts/ApplyImGuiPatches.ps1 ]      || { echo "  the Windows patcher is missing"; Fail=1; }

# ⚠️ The doc must not name Construct.ps1: it is banned, and following it would silently skip the patch step.
if grep -q 'Construct\.ps1' Patches/Patches.md 2>/dev/null; then
    echo "  Patches.md still points at the banned Construct.ps1"; Fail=1
fi

# ── Both build systems apply them before compiling ──────────────────────────────────────────────────────────────
grep -q 'ApplyImGuiPatches' Projects/Project-Zero/Build/ToolchainSequence.ps1 \
    || { echo "  the Windows build never applies the patches"; Fail=1; }
grep -q 'ApplyImGuiPatches' CMakeLists.txt \
    || { echo "  the CMake build never applies the patches"; Fail=1; }

# The patch step must come AFTER the submodule update, or the update wipes what was just applied.
SubLine=$(grep -n 'git submodule update --init -- \$SubmoduleList' Projects/Project-Zero/Build/ToolchainSequence.ps1 | head -1 | cut -d: -f1)
PatLine=$(grep -n 'ApplyImGuiPatches' Projects/Project-Zero/Build/ToolchainSequence.ps1 | head -1 | cut -d: -f1)
if [ -n "$SubLine" ] && [ -n "$PatLine" ] && [ "$PatLine" -lt "$SubLine" ]; then
    echo "  patches are applied before the submodule update — the update would discard them"; Fail=1
fi

[ "$Fail" = "0" ] && echo "  tracked, documented and wired into both builds                   PASS"

# ── The patcher round-trips on the real tree ────────────────────────────────────────────────────────────────────
echo
echo "[ImGuiPatches] apply / verify / revert round-trip"
if [ ! -d ExternalPackages/imgui/.git ] && [ ! -f ExternalPackages/imgui/imgui.cpp ]; then
    echo "  ExternalPackages/imgui is not checked out — round-trip skipped"
    [ "$Fail" = "0" ] && { echo; echo "[ImGuiPatches] OK (tracking only)"; exit 0; }
    echo; echo "[ImGuiPatches] FAILED"; exit 1
fi

# Start from a known state. The tree may arrive patched (a previous build) or pristine (a fresh checkout), and
#    comparing against whichever it happened to be would make the revert check meaningless.
python3 Scripts/ApplyImGuiPatches.py --revert >/dev/null 2>&1
Before="$(cd ExternalPackages/imgui && git status --porcelain | wc -l)"
if [ "$Before" != "0" ]; then
    echo "  the vendored tree is dirty before patching — hand edits in ExternalPackages/ are forbidden"; Fail=1
fi

python3 Scripts/ApplyImGuiPatches.py >/tmp/ImGuiPatch.apply 2>&1 \
    || { echo "  apply failed"; sed 's/^/    /' /tmp/ImGuiPatch.apply | head -10; Fail=1; }

# Idempotence: a second apply must skip, not fail and not double-apply.
python3 Scripts/ApplyImGuiPatches.py >/tmp/ImGuiPatch.again 2>&1
if ! grep -q 'already applied' /tmp/ImGuiPatch.again; then
    echo "  a second apply did not report the patches as already applied — not idempotent"; Fail=1
fi

python3 Scripts/ApplyImGuiPatches.py --verify >/tmp/ImGuiPatch.verify 2>&1
for Sentinel in A B C; do
    grep -q "Patch$Sentinel.*is applied" /tmp/ImGuiPatch.verify \
        || { echo "  verify does not see Patch$Sentinel"; Fail=1; }
done

# Every member the patches add must default to zero, or applying them would change how a stock build looks.
for Member in TabSlant TabOverlap TabHeight TabStripPadTop TabButtonRounding; do
    grep -qE "^\s+$Member\s+= 0\.0f;" ExternalPackages/imgui/imgui.cpp \
        || { echo "  $Member does not default to 0.0f — applying the patches would change a stock build"; Fail=1; }
done

# Revert must restore the tree exactly; a residue means the patches cannot be safely re-cut.
python3 Scripts/ApplyImGuiPatches.py --revert >/tmp/ImGuiPatch.revert 2>&1 \
    || { echo "  revert failed"; sed 's/^/    /' /tmp/ImGuiPatch.revert | head -10; Fail=1; }
After="$(cd ExternalPackages/imgui && git status --porcelain | wc -l)"
[ "$Before" = "$After" ] \
    || { echo "  revert left the vendored tree dirty ($Before -> $After modified files)"; Fail=1; }

# Leave the tree patched: that is the state a build expects.
python3 Scripts/ApplyImGuiPatches.py >/dev/null 2>&1

# ── The patches must actually DO something ───────────────────────────────────────────────────────────────────────
# 🔴 Every style variable the patches add defaults to 0.0f, and 0.0f is stock rectangular ImGui exactly — that
# default is deliberate, so a patched-but-unconfigured build is byte-identical to an unpatched one. Nothing in
# Slate had ever set them: three patches were applied on every build, the log reported them applied, and the tabs
# were square. A patch nobody configures is an elaborate way to change nothing.
for Variable in TabSlant TabOverlap TabHeight TabStripPadTop; do
    Seated=$(grep -oP "TabStyle\.${Variable}\s*=\s*\K[0-9.]+" Engine/DeviceExchange/SwapchainExchange.cpp | head -1)
    if [ -z "$Seated" ]; then
        echo "  $Variable is never set, so the patch that adds it draws stock ImGui"; Fail=1
    else
        awk -v v="$Seated" 'BEGIN { exit !(v > 0.0) }' \
            || { echo "  $Variable is seated at $Seated, which IS stock ImGui"; Fail=1; }
    fi
done

# ⚠️ And a tab is drawn by a dock node's tab bar, so without a dock space there is no node and no tab — the
# geometry would be correct and invisible, which is the state this replaced. The dockspace lives in the
# development editor (Engine/Editor/EditorHost.cpp), which the panel calls between NewFrame and Render.
grep -q 'ImGui::DockSpace' Engine/Editor/EditorHost.cpp \
    || { echo "  there is no dock space, so no tab bar exists for the trapezoid to be drawn on"; Fail=1; }
# PassthruCentralNode is what keeps the rendered scene visible under it; a dock space is opaque by default.
grep -q 'ImGuiDockNodeFlags_PassthruCentralNode' Engine/Editor/EditorHost.cpp \
    || { echo "  the dock space is opaque — it would paint over the render"; Fail=1; }
grep -q 'ImGuiConfigFlags_DockingEnable' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  docking is disabled, so nothing can be docked and no tab bar can appear"; Fail=1; }

# The patches need the DOCKING branch, and a pin that drifts back to master takes docking away silently.
grep -q 'branch = docking' .gitmodules \
    || { echo "  .gitmodules does not pin imgui to docking — the pin can drift to master unnoticed"; Fail=1; }

[ "$Fail" = "0" ] && echo "  apply is idempotent, the vars are seated, and a dock space exists    PASS"

echo
if [ "$Fail" = "0" ]; then echo "[ImGuiPatches] OK"; exit 0; else echo "[ImGuiPatches] FAILED"; exit 1; fi
