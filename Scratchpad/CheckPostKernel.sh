#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckPostKernel.sh — stars, flare and rainbow reach the kernel and agree with their models
#============================================================================================================================================
# The post seam's three transcriptions (PostRecords.slang) plus the record that feeds them (binding 24) and the
#    star tables (binding 23). Seven ways this rots, all guarded:
#    ① the pack drifts from the panel (a slider edits nothing, a gate misfires),
#    ② the bow's baked Descartes angles drift from the model that derived them,
#    ③ the star visibility ceiling forks (two copies of 0.09 that stop agreeing),
#    ④ a hook unwires (stars out of the sky, bow off a path, flare out of the resolve),
#    ⑤ the post file reaches for push constants or textures and quietly depends on include order,
#    ⑥ the C++ mirror and the shader's std140 block disagree (a wrong picture, not a compile error),
#    ⑦ the host stops writing a binding — the layout, the pool, the write or the per-frame push.
#
# SPIR-V is deliberately NOT recompiled here: CheckSkyKernel pins the whole 25-binding table including 23/24,
#    and CheckShaderCompile proves the file lowers. This gate pins everything else, so it runs standalone.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[PostKernel] the sequence packs the post params for the kernel"
Pack="$(mktemp -u /tmp/PostPackProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -ffunction-sections -fdata-sections -Wall -Wextra -I . -I Engine -o "$Pack" \
     Scratchpad/PostPackProof.cpp Projects/Project-Zero/Source/CelestialSequence.cpp \
     Engine/DisplayPresentation/CelestialSolver.cpp Engine/GeometricRaster/StarCatalogueIndex.cpp \
     -Wl,--gc-sections 2>/tmp/PostPack.build; then
    echo "  PACK PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/PostPack.build | head -16; exit 1
fi
"$Pack" || Fail=1
rm -f "$Pack"

echo
echo "[PostKernel] the bow's baked angles still match the model that derived them"
Bow="$(mktemp -u /tmp/BowAngles.XXXXXX)"
cat > /tmp/BowAngles.cpp <<'EOF'
#include "AtmosphericOptics.h"
#include <cstdio>
int main(){
    using namespace Frontier;
    for (int I = 0; I < 14; ++I) std::printf("%.7g\n", (double)AtmosphericOptics::RainbowAngle(380.0f + float(I) / 13.0f * 320.0f, 1u));
    for (int I = 0; I < 14; ++I) std::printf("%.7g\n", (double)AtmosphericOptics::RainbowAngle(380.0f + float(I) / 13.0f * 320.0f, 2u));
    std::printf("%.7g\n%.7g\n", (double)AtmosphericOptics::RainbowAngle(400.0f, 1u), (double)AtmosphericOptics::RainbowAngle(700.0f, 2u));
    return 0;
}
EOF
if ! g++ -std=c++17 -O1 -I Engine/DisplayPresentation /tmp/BowAngles.cpp -o "$Bow" 2>/tmp/BowAngles.build; then
    echo "  BOW PROBE FAILED TO BUILD"; sed 's/^/    /' /tmp/BowAngles.build | head -8; exit 1
fi
"$Bow" > /tmp/BowAngles.model
rm -f "$Bow" /tmp/BowAngles.cpp
python3 - /tmp/BowAngles.model Engine/Shaders/PostRecords.slang <<'PY' > /tmp/BowAngles.diff 2>&1
import re, sys
model = [float(x) for x in open(sys.argv[1]).read().split()]
src = open(sys.argv[2]).read()
got = []
for name in ("kBowPrimary", "kBowSecondary"):
    m = re.search(name + r"\[14\] = float\[14\]\(([^;]+)\)", src)
    got += [float(x) for x in m.group(1).replace(",", " ").split()]
edges = re.findall(r"FrontierSmooth\((0\.\d+), 0\.\d+ \+ 0\.02, Angle\)", src)
edges += re.findall(r"\(1\.0 - FrontierSmooth\((0\.\d+) - 0\.02, 0\.\d+, Angle\)\)", src)
got += [float(x) for x in edges]
bad = [(a, b) for a, b in zip(model, got) if abs(a - b) > 5e-7]
print("MATCH" if not bad and len(model) == len(got) == 30 else "DRIFT %s" % bad[:4])
PY
[ "$(cat /tmp/BowAngles.diff)" = "MATCH" ]
Report $? "all 28 Descartes angles + 2 Alexander edges match RainbowAngle ($(cat /tmp/BowAngles.diff))"

Kernel=Engine/Shaders/ReSTIRViewport.slang
Sky=Engine/Shaders/SkyRecords.slang
Post=Engine/Shaders/PostRecords.slang
Code="$(sed 's;//.*;;' "$Kernel")"
SkyCode="$(sed 's;//.*;;' "$Sky")"
PostCode="$(sed 's;//.*;;' "$Post")"

echo
echo "[PostKernel] the ceiling is one threshold, not two that agree today"
CpuCeil=$(grep -o 'kStarVisibilityCeiling = 0\.[0-9]*f\?' Engine/GeometricRaster/VisibilityRaster.cpp | grep -o '0\.[0-9]*')
GpuCeil=$(printf '%s' "$PostCode" | grep -o 'SkyLuminance >= 0\.[0-9]*' | grep -o '0\.[0-9]*' | head -1)
[ -n "$CpuCeil" ] && [ "$CpuCeil" = "$GpuCeil" ]
Report $? "raster and kernel gate stars at the same sky luminance ($CpuCeil)"

echo
echo "[PostKernel] the three effects are hooked into the frame"
# The catalogue rides behind the weather like every other celestial source: the transmittance suffix is the
#    weather integration's, not drift — without it stars would punch through overcast.
printf '%s' "$SkyCode" | grep -q 'Radiance += StarAlong(Direction, dot(Radiance, vec3(0.2126, 0.7152, 0.0722))) \* CloudTransmittance;'
Report $? "the sky entry point adds the catalogue before the twilight"
printf '%s' "$Code" | grep -q 'RainbowAlong(direction, SkySunDirection.xyz, 1.0e6)'
Report $? "a missed ray earns the full bow"
printf '%s' "$Code" | grep -q 'RainbowAlong(-viewDir, SkySunDirection.xyz, primaryT)'
Report $? "a shaded hit earns the bow its own column allows"
printf '%s' "$Code" | grep -q 'radiance += FlareAlong((vec2(pixel) + 0.5) / extent, extent.x / max(extent.y, 1.0));'
Report $? "the resolve adds the flare to the sample, not the mean"
# Regression: adding the flare to the MEAN stored it into the history and added a fresh one every frame, so the
#    flare grew as (n+1)/2 — the blinding-white frame and the ever-changing size. The mean update must come after.
! printf '%s' "$Code" | grep -q 'mean += FlareAlong'
Report $? "the flare is never added to the mean (no accumulation)"
FlareLine=$(grep -n 'radiance += FlareAlong' "$Kernel" | head -1 | cut -d: -f1); FlareLine=${FlareLine:-0}
MeanLine=$(grep -n 'vec3 mean  = history.rgb' "$Kernel" | head -1 | cut -d: -f1); MeanLine=${MeanLine:-0}
[ "$FlareLine" -gt 0 ] && [ "$FlareLine" -lt "$MeanLine" ]
Report $? "the flare lands before the running-mean update"
Entries=$(printf '%s' "$PostCode" | grep -c 'vec3 StarAlong(vec3 Direction, float SkyLuminance)')
[ "$Entries" = "1" ]
Report $? "PostRecords exposes exactly one StarAlong ($Entries found)"
Entries=$(printf '%s' "$PostCode" | grep -c 'vec3 RainbowAlong(vec3 Direction, vec3 SunDirection, float DistanceMetres)')
[ "$Entries" = "1" ]
Report $? "PostRecords exposes exactly one RainbowAlong ($Entries found)"
Entries=$(printf '%s' "$PostCode" | grep -c 'vec3 FlareAlong(vec2 ScreenUv, float Aspect)')
[ "$Entries" = "1" ]
Report $? "PostRecords exposes exactly one FlareAlong ($Entries found)"

echo
echo "[PostKernel] the post file depends on include order nowhere"
! printf '%s' "$PostCode" | grep -qE 'ViewportWidth|ViewportHeight|FieldOfViewTanHalf|CameraOrigin|CameraForward|CameraRight|CameraUp|Textures\['
Report $? "no push constants, no bindless table — arguments and its own bindings only"
printf '%s' "$Code" | grep -q '#include "Shaders/PostRecords.slang"'
Report $? "the kernel includes the post file"

echo
echo "[PostKernel] the C++ mirror matches the shader's std140 layout"
grep -q 'static_assert(sizeof(PostConstantRecord) == 128u' Engine/DisplayPresentation/PostConstantRecord.h
Report $? "the record is pinned at 128 bytes"
Offsets=$(grep -c 'static_assert(offsetof(PostConstantRecord' Engine/DisplayPresentation/PostConstantRecord.h)
[ "$Offsets" -ge 8 ]
Report $? "every member's offset is asserted ($Offsets of them)"
printf '%s' "$PostCode" | grep -q 'layout(std140, binding = 24) uniform PostConstants'
Report $? "the post uniform sits at binding 24"
printf '%s' "$PostCode" | grep -q 'layout(std430, binding = 23) readonly buffer StarTable'
Report $? "the star tables sit at binding 23"

echo
echo "[PostKernel] the sizes are stated once and agree everywhere"
grep -q 'kPostRecordBytes = 128u' Engine/DeviceExchange/SwapchainExchange.cpp
Report $? "the host allocation agrees with the mirror's 128 bytes"
grep -q 'static_assert(sizeof(StarRecord) == 32u' Engine/GeometricRaster/StarCatalogueIndex.h
Report $? "a star is pinned at 32 bytes"
grep -q 'kStarRecordBytes = 32u' Engine/DeviceExchange/SwapchainExchange.cpp
Report $? "the host restates the star size faithfully"
grep -q 'kCellCount.*=.*kGridResolution \* kGridResolution' Engine/GeometricRaster/StarCatalogueIndex.h
Report $? "the cell grid is 32x32 in the index"
grep -q 'kStarCellCount = 1024u' Engine/DeviceExchange/SwapchainExchange.cpp
Report $? "the host restates the 1 024 cells faithfully"
printf '%s' "$PostCode" | grep -q 'StarCells\[1024\]'
Report $? "the shader declares the 1 024 cells"

echo
echo "[PostKernel] the host writes both bindings as laid out"
X=Engine/DeviceExchange/SwapchainExchange.cpp
grep -q '(B == 21u || B == 22u || B == 24u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER' "$X"
Report $? "binding 24 is laid out as a uniform buffer (23 falls to storage)"
grep -q 'PoolSizes\[1\].descriptorCount = 12u;' "$X"
Report $? "the storage pool budgets the star tables"
grep -q 'PoolSizes\[3\].descriptorCount = 3u;' "$X"
Report $? "the UBO pool budgets the post record"
grep -q 'WriteBuffer(23u, StarInfo);' "$X"
Report $? "binding 23 is actually written"
grep -q 'WriteUniform(24u, PostInfo);' "$X"
Report $? "binding 24 is actually written"
PostLine=$(grep -n '"BringPostRecord"' "$X" | head -1 | cut -d: -f1); PostLine=${PostLine:-0}
StarLine=$(grep -n '"BringStarTables"' "$X" | head -1 | cut -d: -f1); StarLine=${StarLine:-0}
SetLine=$(grep -n '"BringDescriptorSet"' "$X" | head -1 | cut -d: -f1); SetLine=${SetLine:-0}
[ "$PostLine" -gt 0 ] && [ "$PostLine" -lt "$SetLine" ] && [ "$StarLine" -gt 0 ] && [ "$StarLine" -lt "$SetLine" ]
Report $? "both buffers are brought up before the set that writes them"

echo
echo "[PostKernel] the post survives a resize and reaches the frame"
grep -q 'Vulkan->PostBuffer) *vkDestroyBuffer' "$X"
Report $? "the post buffer is destroyed at retire"
grep -q 'Vulkan->StarBuffer) *vkDestroyBuffer' "$X"
Report $? "the star buffer is destroyed at retire"
! sed -n '/void SwapchainExchange::RetireSwapchain/,/^}/p' "$X" | grep -q 'PostBuffer\|StarBuffer'
Report $? "a resize unbinds neither"
PackSites=$(grep -rn 'PackPostConstants(' Engine Projects --include=*.cpp | wc -l)
[ "$PackSites" = "1" ]
Report $? "PackPostConstants has exactly one production caller ($PackSites found)"
grep -q 'Celestial.PackPostRecord(' Projects/Project-Zero/Source/GameExecution.cpp
Report $? "the project packs the record each frame"
grep -q 'Surface.RefreshPost(' Projects/Project-Zero/Source/GameExecution.cpp
Report $? "the project pushes it at the swapchain"
grep -q 'Surface.UploadStarTables(' Projects/Project-Zero/Source/GameExecution.cpp
Report $? "the project uploads the tables once the catalogue is loaded"
grep -q 'Traversal.TraceClosest(EyeArray, Frame.Sun.Direction' Projects/Project-Zero/Source/GameExecution.cpp
Report $? "the project traces the single sun-occlusion ray per frame"

echo
if [ "$Fail" != "0" ]; then echo "[PostKernel] FAILED"; exit 1; fi
echo "[PostKernel] OK"
