#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckWindField.sh — one wind field, and the expensive half stays out of the march
#============================================================================================================================================
# Celestial step 4. The wind lands before clouds, fog and precipitation because all of them advect by it; a cloud
#    drifting one way while its rain falls another is an error no single system owns.
#
# The structural check is the point. The source branch's last commit (6c8b1a8) exists because six noise
#    evaluations sat inside the advection called at every raymarch step - about 1500 extra per cloud pixel. The
#    picture is IDENTICAL when that regression happens; only the frame time moves, so nothing in an ordinary
#    suite notices. Measured here: the swirl is ~47x dearer than a march step, 0.64 us once per pixel against
#    160 us if it rides along at 250 steps.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[WindField] the field is physical and the split is measured"
Binary="$(mktemp -u /tmp/WindFieldProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I . -I Engine -o "$Binary" Scratchpad/WindFieldProof.cpp \
     2>/tmp/WindField.build; then
    echo "  PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/WindField.build | head -20; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

echo
echo "[WindField] the cheap term really is cheap"
Header=Engine/DisplayPresentation/WindField.h
# SampleStep is the only wind a march may call. If it ever reaches the noise, the regression is back.
python3 - "$Header" <<'PY'
import re, sys
Source = open(sys.argv[1], encoding='utf-8').read()
Start  = Source.index('static void SampleStep')
End    = Source.index('static float SampleGust')
Body   = Source[Start:End]
Bad    = [Name for Name in ('ValueNoise', 'SampleSwirl', 'Hash(') if Name in Body]
print('  %-66s %s' % ('SampleStep contains no noise call', 'PASS' if not Bad else 'FAIL ' + str(Bad)))
raise SystemExit(1 if Bad else 0)
PY
Report $? "SampleStep is trig-only"

# And the whole-field helper must NOT be presented as march-safe.
grep -q 'A raymarch must NOT call this' "$Header"
Report $? "Sample() is documented as off-limits inside a march"

echo
echo "[WindField] one definition, not one per medium"
Stray=$(grep -rln --include=*.cpp --include=*.h -e 'windBase' -e 'WindBase' Engine/ 2>/dev/null | grep -v WindField.h || true)
[ -z "$Stray" ]
Report $? "no second copy of the base flow ($Stray)"

echo
if [ "$Fail" != "0" ]; then echo "[WindField] FAILED"; exit 1; fi
echo "[WindField] OK"
