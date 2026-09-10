#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckStarCatalogue.sh — the sky is the real sky
#============================================================================================================================================
# Celestial step 3. The renderer drew a procedural hash until the full HYG catalogue could be obtained; it now
#    draws 8 920 real stars. This guards the three ways that silently degrades:
#
#    ① the catalogue asset is replaced by a stub or an empty file (the converter did exactly this once, writing a
#      16-byte header over the good asset and reporting success),
#    ② the equatorial-to-horizon rotation is written with its sine and cosine transposed, which puts Polaris
#      overhead at the equator and still looks like a sky, and
#    ③ the binning and the shader's cell lookup disagree, which empties the sky with no error anywhere.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[StarCatalogue] the asset is a real catalogue, not a stub"
Bytes=$(stat -c%s EngineContent/StarCatalogue/BrightStars.bin 2>/dev/null || echo 0)
[ "$Bytes" -gt 100000 ]
Report $? "BrightStars.bin is $Bytes bytes (a header-only stub is 16)"

echo
echo "[StarCatalogue] the catalogue, the transforms and the constellations"
Binary="$(mktemp -u /tmp/StarCatalogueTest.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I . -I Engine -o "$Binary" \
     Scratchpad/StarCatalogueTest.cpp \
     Engine/GeometricRaster/StarCatalogueIndex.cpp \
     Engine/DisplayPresentation/CelestialSolver.cpp 2>/tmp/StarCatalogue.build; then
    echo "  PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/StarCatalogue.build | head -20; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

echo
echo "[StarCatalogue] the converter cannot write an empty catalogue again"
grep -q 'Refusing to write an empty catalogue' Tools/StarCatalogue/ConvertHygCatalogue.py
Report $? "a zero-star conversion is a hard error"
grep -q '"proper"' Tools/StarCatalogue/ConvertHygCatalogue.py
Report $? "quoted CSV headers are recognised (HYG v41 changed this)"

echo
if [ "$Fail" != "0" ]; then echo "[StarCatalogue] FAILED"; exit 1; fi
echo "[StarCatalogue] OK"
