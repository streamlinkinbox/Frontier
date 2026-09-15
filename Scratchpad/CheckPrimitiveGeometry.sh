#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
#  CheckPrimitiveGeometry.sh — sphere, cone, torus and the roof oculus are closed, outward-facing and watertight
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
set -u
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$Root" || exit 1
Vkh="${VKH:-/tmp/vkh/include}"
Fail=0

echo "[Primitives] closed-surface, winding and degeneracy audit"
Binary="$(mktemp -u /tmp/PrimitiveGeometry.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I Engine -I . -I Projects/Project-Zero/Source -I "$Vkh" \
     Scratchpad/PrimitiveGeometryTest.cpp \
     Projects/Project-Zero/Source/RayTracingSolver.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
 -o "$Binary" 2>/tmp/Primitives.build; then
    echo "  COMPILE FAILED"; sed 's/^/    /' /tmp/Primitives.build | head -20; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

echo
echo "[Primitives] source invariants"
# The winding notes are the only record of WHY each order is what it is. Losing them invites the next reader to
# "tidy" a reversal back into a bug that presents as bad lighting.
grep -q 'inward normals' Projects/Project-Zero/Source/RayTracingSolver.cpp \
    || { echo "  the sphere winding rationale is gone — a reversal here reads as a lighting bug"; Fail=1; }
grep -q 'not star-shaped' Scratchpad/PrimitiveGeometryTest.cpp \
    || { echo "  the torus reference-point rationale is gone"; Fail=1; }

# The oculus must stay circular: a rectangular hole cannot produce the elliptical shaft the sky work needs.
grep -q 'AppendPlateWithCircularHole' Projects/Project-Zero/Source/RayTracingSolver.cpp \
    || { echo "  the ceiling no longer uses a circular aperture"; Fail=1; }

# Every primitive must reject degenerate parameters rather than emitting a broken hull.
for Guard in 'Segments < 3u' 'MinorRadius >= MajorRadius'; do
    grep -q "$Guard" Projects/Project-Zero/Source/RayTracingSolver.cpp \
        || { echo "  missing parameter guard: $Guard"; Fail=1; }
done

[ "$Fail" = "0" ] && echo "  rationale and guards intact                                      PASS"

echo
if [ "$Fail" = "0" ]; then echo "[Primitives] OK"; exit 0; else echo "[Primitives] FAILED"; exit 1; fi
