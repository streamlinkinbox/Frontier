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
        -e 'CloudMarchStepCount *=' -e 'AtmosphereSampleCount *=' -e 'LocalVolumeStepCount *=' \
        Engine/ Projects/ 2>/dev/null | grep -v 'FidelityClassifier.cpp' || true)
[ -z "$Stray" ]
Report $? "no second copy of the budgets ($(echo "$Stray" | tr '\n' ' '))"

# Reading them is fine and expected; restating them is not.
grep -q 'CloudMarchStepCount' Engine/DisplayPresentation/FidelityClassifier.h
Report $? "the criteria block declares the celestial budgets"

echo
echo "[CelestialTiers] the tier reaches the simulation through one translation"
Bin2="$(mktemp -u /tmp/CelestialTierProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I . -I Engine -o "$Bin2" Scratchpad/CelestialTierProof.cpp \
        Engine/DisplayPresentation/FidelityClassifier.cpp 2>/tmp/CelestialTierProof.build; then
    echo "  TIER PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/CelestialTierProof.build | head -12; exit 1
fi
"$Bin2" || Fail=1
rm -f "$Bin2"

echo
echo "[CelestialTiers] nothing translates a tier except CelestialTier"
# Every caller must take a CelestialBudget rather than reading FidelityCriteria field by field. Hand-assembly at
#    each call site is how the ladder rots: the next system copies whichever site it happens to read.
# ⚠️ Matches READS, not writes. FidelityClassifier.cpp assigns these fields - that is the ladder's own
#    definition and the whole point of it - so a bare name match flags the one file that is allowed to.
#    The pattern requires the field NOT be followed by '=', which is what separates a read from a write.
Hand=$(grep -rlnE --include=*.cpp --include=*.h \
       'Criteria\.(CloudMarchStepCount|LocalVolumeStepCount|CloudLightTapCount|AtmosphereSampleCount)[^=]*[;,)]' \
       Engine/ 2>/dev/null | grep -v 'CelestialTier.h' || true)
[ -z "$Hand" ]
Report $? "no engine file assembles a celestial budget by hand ($Hand)"
grep -q 'static CelestialBudget BudgetFor' Engine/DisplayPresentation/CelestialTier.h
Report $? "CelestialTier owns the only translation"

echo
echo "[CelestialTiers] Auto is opt-in and cannot oscillate"
grep -q 'bool     Enabled        = false;' Engine/DisplayPresentation/CelestialTier.h
Report $? "the Auto ladder defaults to disabled"
# The asymmetry is the whole control law: two intervals to climb, one to fall.
grep -q 'if (Consecutive < 2) return false;' Engine/DisplayPresentation/CelestialTier.h
Report $? "climbing requires two consecutive intervals of headroom"

echo
if [ "$Fail" != "0" ]; then echo "[CelestialTiers] FAILED"; exit 1; fi
echo "[CelestialTiers] OK"
