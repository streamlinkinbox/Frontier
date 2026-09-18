#!/usr/bin/env bash
#============================================================================================================================================
#                                                     CHECKFRAMETELEMETRY.SH
#============================================================================================================================================
# The verbose per-frame ledger must record everything in a development build and must NOT EXIST in a shipping one.
#
#    Both halves are compiled and run here, and the shipping half is checked twice over: once by running it, and once
#    by reading the object file's symbol table. "It compiled without the define" is a weaker claim than "the code is
#    not in the binary", and only the second one is worth making.
#
#    usage: bash Tools/Build/CheckFrameTelemetry.sh

set -euo pipefail
cd "$(dirname "$0")/../.."

Stage="$(mktemp -d)"
trap 'rm -rf "$Stage"' EXIT

Gate=Tools/Build/Gates/FrameTelemetryLedgerGate.cpp
Ledger=Projects/Project-Zero/Source/FrameTelemetryLedger.cpp

echo "── development build ───────────────────────────────────────────────────────────"
g++ -std=c++20 -O1 -g -DFRONTIER_DEVELOPMENT -I. -o "$Stage/LedgerDev" "$Gate" "$Ledger"
( cd "$Stage" && ./LedgerDev )

echo
echo "── shipping build ──────────────────────────────────────────────────────────────"
g++ -std=c++20 -O2 -I. -o "$Stage/LedgerShip" "$Gate" "$Ledger"
( cd "$Stage" && ./LedgerShip )

echo
echo "── the facility is absent from the shipping object, not merely disabled ────────"
g++ -std=c++20 -O2 -I. -c -o "$Stage/Ledger.ship.o" "$Ledger"
g++ -std=c++20 -O1 -g -DFRONTIER_DEVELOPMENT -I. -c -o "$Stage/Ledger.dev.o" "$Ledger"

DevSymbols=$(nm -C "$Stage/Ledger.dev.o"  2>/dev/null | grep -c "FrameTelemetryLedger" || true)
ShipSymbols=$(nm -C "$Stage/Ledger.ship.o" 2>/dev/null | grep -c "FrameTelemetryLedger" || true)
DevBytes=$(wc -c < "$Stage/Ledger.dev.o")
ShipBytes=$(wc -c < "$Stage/Ledger.ship.o")

echo "  development object: $DevSymbols FrameTelemetryLedger symbol(s), $DevBytes bytes"
echo "  shipping    object: $ShipSymbols FrameTelemetryLedger symbol(s), $ShipBytes bytes"

Failed=0
if [ "$DevSymbols" -eq 0 ]; then
    echo "  FAIL  the development build defines no ledger symbols — the facility is not being compiled at all" >&2
    Failed=1
else
    echo "  PASS  the development build carries the ledger"
fi

if [ "$ShipSymbols" -ne 0 ]; then
    echo "  FAIL  the shipping object still defines $ShipSymbols ledger symbol(s) — it must compile out entirely" >&2
    nm -C "$Stage/Ledger.ship.o" | grep "FrameTelemetryLedger" >&2 || true
    Failed=1
else
    echo "  PASS  the shipping object defines no ledger symbols at all"
fi

# The 64 MB ring must not be reachable from a release binary, so the TU should be essentially empty there.
if [ "$ShipBytes" -gt "$DevBytes" ]; then
    echo "  FAIL  the shipping object is not smaller than the development one" >&2
    Failed=1
else
    echo "  PASS  the shipping object is smaller ($ShipBytes < $DevBytes bytes)"
fi

[ "$Failed" -eq 0 ] || exit 1
echo
echo "[frame-telemetry] GREEN — verbose in development, absent in a shipping build"
