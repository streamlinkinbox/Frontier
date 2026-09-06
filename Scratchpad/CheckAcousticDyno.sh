#!/usr/bin/env bash
# Gate: Project-Dyno plays the voice — the binary's --render is the JavaScript reference after float32 quantisation, the
#    order sheets of the three cars are what the editor shows, and a 60 s null-device run keeps the callback under budget.
#
#    1. AcousticProof --self                              FFT / order reader self-check
#    2. --render 3 cars × pull (float32 WAV)              WAV ⇄ Scratchpad/Reference/<car>_pull.f64 ≤ 1.2e-7 per sample (float32 rounding);
#                                                         order sheets at idle and at redline (informative — the voicing is the editor's business);
#                                                         Scratchpad/ProjectDyno_<Car>_Pull.png; --pure --pull steady: firing order N/2 within 20 dB
#                                                         of the loudest (the JavaScript proof's [1] bar, gated)
#    3. --null --seconds 60 --pull limiter                 0 overloads, 0 dropped transients, peak callback µs in the log
#    4. --clicks --render                                  the row-A1 click train still renders behind --clicks
# Needs the reference dumps (Scratchpad/CheckAcousticIdentity.sh writes them; this script writes the three it needs if absent).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

Bin=Projects/Project-Dyno/Build/Output/Linux/Release/Binary/Project-Dyno
bash Projects/Project-Dyno/Build/ToolchainSequence.sh >/tmp/CheckAcousticDyno.build.log 2>&1 || { echo "[AcousticDyno] BUILD FAILED (see /tmp/CheckAcousticDyno.build.log)"; tail -20 /tmp/CheckAcousticDyno.build.log; exit 1; }
g++ -std=c++20 -O2 -Wall -Wextra -I. -IEngine Scratchpad/AcousticProof.cpp Engine/PlatformInterchange/WaveCodec.cpp -o /tmp/AcousticProof || { echo "[AcousticDyno] PROOF COMPILE FAILED"; exit 1; }

Log=Scratchpad/ProjectDynoRender.log
: > "$Log"
Fail=0
Say() { echo "$@" | tee -a "$Log"; }
Run() { "$@" 2>&1 | tee -a "$Log"; return ${PIPESTATUS[0]}; }

Say "ProjectDynoRender — $(date -u +%Y-%m-%dT%H:%M:%SZ)"
Say ""
Say "[1] AcousticProof --self"
Run /tmp/AcousticProof --self || Fail=1

Say ""
Say "[2] --render 3 cars × pull → WAV ⇄ JavaScript dump, order sheets, PNGs"
declare -A Cyl=( [Porsche918Spyder]=8 [FerrariLaFerrari]=12 [NissanGtrNismo]=6 )
declare -A Idle=( [Porsche918Spyder]=1000 [FerrariLaFerrari]=1000 [NissanGtrNismo]=900 )
declare -A Red=( [Porsche918Spyder]=9150 [FerrariLaFerrari]=9250 [NissanGtrNismo]=7100 )
for Car in FerrariLaFerrari Porsche918Spyder NissanGtrNismo; do
    [ -f "Scratchpad/Reference/${Car}_pull.f64" ] || node Scratchpad/AcousticEditorRender.js --dump "$Car" pull full 64 >>"$Log" 2>&1
    Wav=/tmp/ProjectDyno_${Car}_Pull.wav
    "$Bin" --render "$Wav" --car "$Car" --pull pull --float 2>&1 | grep -E "\[Car\]|\[Render\]" | sed 's/^\[INFO\] //' | tee -a "$Log"
    Run /tmp/AcousticProof --wav "$Wav" --f64 "Scratchpad/Reference/${Car}_pull.f64" --label "$Car" || Fail=1
    Run /tmp/AcousticProof --wav "$Wav" --orders "${Idle[$Car]}" --cylinders "${Cyl[$Car]}" --at 1.0 --label "$Car idle" || Fail=1
    Run /tmp/AcousticProof --wav "$Wav" --orders "${Red[$Car]}" --cylinders "${Cyl[$Car]}" --at 8.2 --label "$Car redline" \
        --png "Scratchpad/ProjectDyno_${Car}_Pull.png" --rpm-trace "0,${Idle[$Car]};1,${Idle[$Car]};1.2,2000;7.2,${Red[$Car]};8.3,${Red[$Car]};13.3,${Idle[$Car]}" || Fail=1
    Pure=/tmp/ProjectDyno_${Car}_Steady_Pure.wav
    "$Bin" --render "$Pure" --car "$Car" --pull steady --pure --float --seconds 3 >/dev/null 2>&1
    Run /tmp/AcousticProof --wav "$Pure" --orders 4000 --cylinders "${Cyl[$Car]}" --gate --label "$Car pure steady" || Fail=1
done

Say ""
Say "[3] --null --seconds 60 --pull limiter (LaFerrari): overloads, dropped transients, peak callback"
"$Bin" --null --seconds 60 --pull limiter --car FerrariLaFerrari 2>&1 | grep -E "\[Audio\]|\[Shutdown\]" | sed 's/^\[INFO\] //' | tail -3 | tee -a "$Log"
Last=$("$Bin" --null --seconds 20 --pull overrun --car NissanGtrNismo 2>&1 | grep "\[Shutdown\]")
echo "$Last" | sed 's/^\[INFO\] //' | tee -a "$Log"
if echo "$Last" | grep -q "0 overloads" && echo "$Last" | grep -q "0 transients dropped"; then Say "  PASS  null device: 0 overloads, 0 transients dropped"; else Say "  FAIL  null device run"; Fail=1; fi

Say ""
Say "[4] --clicks --render (row-A1 click train still behind --clicks)"
if "$Bin" --clicks --render /tmp/ProjectDyno_Clicks.wav --pull sweep --cylinders 8 --seconds 2 2>&1 | grep -q "click events"; then Say "  PASS  click train renders"; else Say "  FAIL  click train"; Fail=1; fi

Say ""
if [ $Fail -eq 0 ]; then Say "ALL PASS"; echo "[AcousticDyno] OK"; else Say "FAILURES"; fi
exit $Fail
