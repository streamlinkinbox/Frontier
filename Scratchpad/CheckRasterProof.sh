#!/usr/bin/env bash
# The proof that can be LOOKED AT. Everything else in this suite asserts about numbers; this one renders the
#    real exposure to a PNG, so a defect that a number would not catch — a sky that shifts when the camera
#    turns — is visible. (The editor's own visual proof lives in CheckEditorProof.sh.)
#
#    It is gated, not just generated: it returns non-zero if what it drew disagrees with what it claims.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
Fail=0
mkdir -p Diagnostics

echo "[RasterProof] the sky, from six camera angles at one hour"
Sky="$(mktemp -u /tmp/SkyRaster.XXXXXX)"
if ! g++ -std=c++20 -O2 -I Engine -I . -I Scratchpad \
     Scratchpad/SkyRasterProof.cpp \
     Engine/DisplayPresentation/DaylightSolver.cpp \
     Engine/DisplayPresentation/ExposureIntegrator.cpp -o "$Sky" 2>/tmp/SkyRaster.build; then
    echo "  COMPILE FAILED"; sed 's/^/    /' /tmp/SkyRaster.build | head -25; exit 1
fi
"$Sky" || Fail=1
rm -f "$Sky"

echo
# ⚠️ A proof that only ever writes a file proves nothing — it has to be able to fail. The sheet asserts the
#    spread across its six views is under a hundredth of a stop, and returns non-zero when it is not.
for Sheet in Diagnostics/Sky_CameraAngles.png; do
    if [[ ! -s "$Sheet" ]]; then echo "  MISSING $Sheet"; Fail=1; else echo "  wrote $Sheet"; fi
done

if (( Fail )); then echo "  >>> RASTER PROOF FAILED"; else echo "  >>> the raster proof agrees with its caption"; fi
exit "$Fail"
