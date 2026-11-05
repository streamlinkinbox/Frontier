#!/bin/bash
# 🧩 Mutation check of the scan's CPU modules: each mutant changes one constant or one line of a module, rebuilds XRayScanValidation and expects it to
#    fail against Results/ScanReference.toml (the numbers the prototype's own modules wrote). The original source is restored after every mutant, and on
#    exit. A mutant that survives is reported with the reason it could not be told apart, if there is one.
#
#      Exhibits/XRayScan/Code/MutateXRayScan.sh <build folder> > Exhibits/XRayScan/Results/ScanMutations.txt
#
#    Never interrupt it while a mutant is in place without letting the trap run: it edits the repository's sources.
set -u
BUILD="$(cd "${1:?usage: MutateXRayScan.sh <build folder>}" && pwd)"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
MODULES="$ROOT/Frontier/Engine/Rendering/XRayScan"
SCRATCH="$(mktemp -d)"
cd "$ROOT"
export PATH="$HOME/.local/bin:$PATH"
Caught=0
Total=0

run()   # <file> <name> <sed expression> [note for a survivor]
{
    local File="$MODULES/$1" Name="$2" Expression="$3" Note="${4:-}"
    local Original="$SCRATCH/$1.orig"
    [ -f "$Original" ] || cp "$File" "$Original"
    cp "$Original" "$File"
    sed -i "$Expression" "$File"
    if cmp -s "$File" "$Original"; then echo "MUTANT $Name: NO CHANGE"; return; fi
    Total=$((Total + 1))
    if ! (cd "$BUILD" && ninja -j2 XRayScanValidation > "$SCRATCH/build.log" 2>&1); then echo "MUTANT $Name: BUILD FAILED"; Total=$((Total - 1)); cp "$Original" "$File"; return; fi
    local Out
    Out=$("$BUILD/XRayScanValidation" "$ROOT" 2>&1 | tail -1)
    if echo "$Out" | grep -q " 0 failures"; then echo "MUTANT $Name: SURVIVED ($Out)${Note:+ — $Note}"; else Caught=$((Caught + 1)); echo "MUTANT $Name: caught ($Out)"; fi
    cp "$Original" "$File"
}

restore() { for Backup in "$SCRATCH"/*.orig; do [ -f "$Backup" ] && cp "$Backup" "$MODULES/$(basename "${Backup%.orig}")"; done; (cd "$BUILD" && ninja -j2 XRayScanValidation > /dev/null 2>&1); rm -rf "$SCRATCH"; }
trap restore EXIT

run XRayScanSequence.cpp  AcesInput        's/0.59719/0.59718/'
run XRayScanSequence.cpp  AcesFit          's/0.983729 \* X/0.983728 * X/'
run XRayScanSequence.cpp  SrgbExponent     's/1.0 \/ 2.4) - 0.055/1.0 \/ 2.3) - 0.055/'
run XRayScanSequence.cpp  EaseCubic        's/4.0 \* X \* X \* X/3.0 * X * X * X/'
run XRayScanSequence.cpp  DampRate         's/1.0 - std::exp(-Lambda \* Seconds)/1.0 - std::exp(-Lambda * Seconds * 0.9)/'
run XRayScanSequence.cpp  FadeSpan         's/(Spec.Cycle - Spec.Sweep - Spec.Hold))/(Spec.Cycle - Spec.Sweep))/'
run XRayScanSequence.cpp  StepLimit        's/std::min(RawSeconds, Spec.StepLimit)/std::min(RawSeconds, Spec.StepLimit * 2.0)/'
run XRayScanSequence.cpp  CurtainIdle      's/(Sweeping_ ? 1.0 : Spec.CurtainIdle)/(Sweeping_ ? 1.0 : 1.0)/'
run XRayScanSequence.cpp  SolidFloor       's/Ramp_ > Spec.SolidFloor;/Ramp_ > Spec.SolidFloor * 2.0;/'
run XRayScanSequence.cpp  EdgeFollowsRamp  's/EdgeOpacity \* Amount_/EdgeOpacity * Ramp_/'
run XRayScanEdges.cpp     WeldTie          's/Value - Lower >= 0.5/Value - Lower > 0.5/'
run XRayScanEdges.cpp     ThresholdEdge    's/Dot <= ThresholdDot/Dot < ThresholdDot/' "equivalent: no pair of facets of the car has a dot product exactly at the threshold"
run XRayScanEdges.cpp     TagShift         's/Entry.Family << 8/Entry.Family << 7/'
run XRayScanSparkles.cpp  SparkleSeed      's/0x6d2b79f5u/0x6d2b79f4u/'
run XRayScanSparkles.cpp  SparkleBox       's/Box\[1\] \* (0.5 - Generator.Next())/Box[1] * (0.4 - Generator.Next())/'
run XRayScanProfile.cpp   ProfileBob       's/0.012 \* std::sin/0.013 * std::sin/'
run XRayScanProfile.cpp   ProfileHeight    's/-0.62 + Bob/-0.61 + Bob/'
run XRayScanProfile.cpp   MaskBit          's/1u << (Index \& 31u)/1u << (Index \& 30u)/'
run XRayScanProfile.cpp   SparedLamps      's/{HeadlightFamily, TaillightFamily, 0u, 0u}/{HeadlightFamily, HeadlightFamily, 0u, 0u}/'
run XRayScanSpecification.h Cycle          's/double Cycle          = 6.6;/double Cycle          = 6.5;/'
run XRayScanSpecification.h CurtainOpacity 's/{0.075, 0.30}/{0.075, 0.31}/'
run XRayScanSpecification.h ParticleOpacity 's/ParticleOpacity = 0.55;/ParticleOpacity = 0.56;/'
run XRayScanSpecification.h GhostColour    's/#a9dcec/#a9dced/'
run XRayScanSpecification.h EdgeMargin     's/EdgeMargin     = 0.05;/EdgeMargin     = 0.06;/'
run XRayScanSpecification.h ParticleCount  's/ParticleCount  = 130;/ParticleCount  = 131;/'
echo "$Caught of $Total mutants caught"
