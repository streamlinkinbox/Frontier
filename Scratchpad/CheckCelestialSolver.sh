#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
#  CheckCelestialSolver.sh — A1: the authoritative clock and the celestial directions derived from it
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
set -u
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$Root" || exit 1
Fail=0

echo "[Celestial] astronomy and determinism"
Binary="$(mktemp -u /tmp/CelestialSolver.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I Engine -I . \
     Scratchpad/CelestialSolverTest.cpp Engine/GeometricRaster/CelestialSolver.cpp -o "$Binary" 2>/tmp/Celestial.build; then
    echo "  COMPILE FAILED"; sed 's/^/    /' /tmp/Celestial.build | head -20; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

echo
echo "[Celestial] design invariants"

# 🔴 Closed form, never integrated. An integrator accumulates rounding differently per machine and two clients
# eventually see different suns — which would silently defeat the entire "replicate one number" design.
if grep -qE '(Position|Angle|Anomaly)\s*\+=' Engine/GeometricRaster/CelestialSolver.cpp; then
    echo "  a celestial quantity is being accumulated — positions must be closed-form functions of time"; Fail=1
fi

# Only the clock itself may be stateful. Any other mutable member is a cached position that can go stale or
# diverge between clients.
MemberCount=$(sed -n '/^private:/,/^};/p' Engine/GeometricRaster/CelestialSolver.h | grep -cE '^\s+(double|float|uint|int)')
[ "$MemberCount" -le 2 ] \
    || { echo "  the solver holds $MemberCount scalar members; only the clock and its rate may be stateful"; Fail=1; }

# Phase must come from the synodic period. The sidereal one is 2.2 days short and drifts out of step with the
# sun over a season — correct-looking for a week, wrong by autumn.
grep -q 'LunarSynodicSeconds' Engine/GeometricRaster/CelestialSolver.cpp \
    || { echo "  moon phase does not use the synodic period"; Fail=1; }

# The sidereal term must be present, or the star field drifts a full day against the sun over a year.
grep -q 'OrbitalTurns' Engine/GeometricRaster/CelestialSolver.cpp \
    || { echo "  sidereal rotation is missing its orbital term"; Fail=1; }

# The engine must not learn project nouns.
for Word in Cornell Showroom Earth; do
    grep -q "\"$Word" Engine/GeometricRaster/CelestialSolver.cpp && { echo "  hard-codes the project noun '$Word'"; Fail=1; }
done

[ "$Fail" = "0" ] && echo "  closed form, single source of truth, correct periods              PASS"

echo
if [ "$Fail" = "0" ]; then echo "[Celestial] OK"; exit 0; else echo "[Celestial] FAILED"; exit 1; fi
