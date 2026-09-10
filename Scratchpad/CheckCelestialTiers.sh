#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckCelestialTiers.sh — guards the Celestial quality ladder (port step 0)
#============================================================================================================================================
# The demo ships seven tiers and we have five, so the mapping is a judgement call recorded in
#    References/Backlog/CelestialPortPlan.md §6. Two ways it can rot:
#
#    ① a budget stops rising with the tier — a typo no screenshot would ever catch, because a slightly cheaper
#      cloud march still looks like a cloud, and
#    ② the ladder gets restated somewhere else (the panel, a shader constant) and the two copies drift.
#
# Both are checked here. The shadow round hit ② already, which is why it is a gate and not a comment.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[CelestialTiers] the ladder rises with the tier"
Bin="$(mktemp -u /tmp/CelestialTierLadder.XXXXXX)"
if ! g++ -std=c++20 -O2 -I Engine -o "$Bin" Scratchpad/CelestialTierLadder.cpp \
        Engine/DisplayPresentation/FidelityClassifier.cpp 2>/tmp/CelestialTierLadder.build; then
    echo "  LADDER PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/CelestialTierLadder.build | head -10; exit 1
fi
"$Bin" | sed 's/^/  /'
Report "${PIPESTATUS[0]}" "every celestial budget is monotonic across the five tiers"
rm -f "$Bin"

echo
echo "[CelestialTiers] the ladder lives in exactly one place"
# The budgets are assigned in FidelityClassifier.cpp and nowhere else. Any other assignment is a second copy.
Stray=$(grep -rln --include=*.cpp --include=*.h --include=*.slang \
        -e 'CloudMarchStepCount *=' -e 'AtmosphereSampleCount *=' -e 'GodRaySampleCount *=' \
        Engine/ Projects/ 2>/dev/null | grep -v 'FidelityClassifier.cpp' || true)
[ -z "$Stray" ]
Report $? "no second copy of the budgets ($(echo "$Stray" | tr '\n' ' '))"

# Reading them is fine and expected; restating them is not.
grep -q 'CloudMarchStepCount' Engine/DisplayPresentation/FidelityClassifier.h
Report $? "the criteria block declares the celestial budgets"

echo
if [ "$Fail" != "0" ]; then echo "[CelestialTiers] FAILED"; exit 1; fi
echo "[CelestialTiers] OK"
