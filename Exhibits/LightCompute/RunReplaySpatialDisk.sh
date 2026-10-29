#!/usr/bin/env bash
# 🧩 Validate Vogel spatial-disk reuse, tangent-plane/roughness culling, intake guards, and unified seed-bank + reservoir GI.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
CMAKE="${CMAKE:-cmake}"
CTEST="${CTEST:-ctest}"
if ! command -v "$CMAKE" >/dev/null; then
    for CANDIDATE in "$ROOT/_ClaudeScratch/venv/bin/cmake" "$ROOT/_ClaudeScratch/pytools/bin/cmake"; do
        if [[ -x "$CANDIDATE" ]]; then CMAKE="$CANDIDATE"; break; fi
    done
fi
if ! command -v "$CTEST" >/dev/null; then
    for CANDIDATE in "$ROOT/_ClaudeScratch/venv/bin/ctest" "$ROOT/_ClaudeScratch/pytools/bin/ctest"; do
        if [[ -x "$CANDIDATE" ]]; then CTEST="$CANDIDATE"; break; fi
    done
fi
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-sample-history-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplaySpatialDisk-$MODE.txt}"
OUTPUT="${OUTPUT:-$ROOT/_ClaudeScratch/replay-spatial-disk-capture-$MODE}"
COMPARISON_PNG="${COMPARISON_PNG:-$ROOT/Exhibits/LightCompute/Images/ReplaySpatialDiskComparison.png}"
if [[ -z "${PYTHON:-}" ]]; then
    if [[ -x "$ROOT/_ClaudeScratch/venv/bin/python" ]]; then PYTHON="$ROOT/_ClaudeScratch/venv/bin/python"; else PYTHON=python3; fi
fi
mkdir -p "$BUILD" "$(dirname "$RESULTS")" "$(dirname "$COMPARISON_PNG")"
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
for PAIR in 'GatherReplaySampleHistory ReplayGatherSampleHistory' 'FinalizeReplaySampleHistory ReplayFinalizeSampleHistory'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplaySampleHistory.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" >/dev/null
{
    echo "Vogel spatial-disk + tangent-plane culling + unified seed-bank ReSTIR GI; host $MODE; CPU SwiftShader Vulkan"
    echo "Base $(git rev-parse --short HEAD), plus working changes; source hashes below."
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,PathReplaySampleHistory,MaterialRayScattering}.slang \
        Frontier/Engine/Rendering/{LightComputeFootprint.h,PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp,PathReplayVulkan.h,PathReplayVulkan.cpp} \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayDomainValidation.cpp,PathReplayVulkanValidation.cpp,ReplaySeedCorrelationValidation.cpp,ReplaySampleHistoryValidation.h,ReplayRoomReference.h} \
        Exhibits/LightCompute/RunReplaySpatialDisk.sh \
        "$TOOLS"/SwiftShader/{libvulkan.so.1,libvk_swiftshader.so,vk_swiftshader_icd.json} \
        "$BUILD"/{Primary,ReplayGenerate,ReplayResolve,ReplayGatherSampleHistory,ReplayFinalizeSampleHistory}.spv
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --replay-spatial-disk
    if [[ "${CAPTURE:-0}" == 1 ]]; then
        mkdir -p "$OUTPUT"
        CAPTURE_OUT="$("$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --capture-replay-spatial-disk "$OUTPUT")"
        printf '%s\n' "$CAPTURE_OUT"
        CAPTURE_LINE="$(printf '%s\n' "$CAPTURE_OUT" | grep '^SPATIAL_DISK_CAPTURE ')"
        "$PYTHON" - "$OUTPUT" "$COMPARISON_PNG" "$CAPTURE_LINE" <<'PY'
import hashlib
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

source = Path(sys.argv[1])
dest = Path(sys.argv[2])
fields = dict(tok.split("=", 1) for tok in sys.argv[3].split() if "=" in tok)

cross_rays = int(fields["cross_rays"])
disk_rays = int(fields["disk_rays"])
ray_delta = 100.0 * (disk_rays - cross_rays) / max(1, cross_rays)

top_panels = [
    ("CorridorReference512", "192-Pass Ground Truth", "4-Bounce NEE+MIS Reference"),
    ("CorridorFresh1Spp", "1-spp Fresh (No Reuse)", f"RMSE {float(fields['fresh_rmse']):.4f} | {int(fields['fresh_rays'])/1000:.1f}k Rays"),
    ("CorridorStandaloneSeed", "Standalone Seed-Bank", f"RMSE {float(fields['seed_only_rmse']):.4f} | {int(fields['seed_only_rays'])/1000:.1f}k Rays"),
    ("CorridorLegacyCross", "Temporal + 1px Cross", f"RMSE {float(fields['cross_rmse']):.4f} | {cross_rays/1000:.1f}k Rays"),
    ("CorridorSpatialDisk", "Temporal + Vogel Disk", f"RMSE {float(fields['disk_rmse']):.4f} | {disk_rays/1000:.1f}k ({ray_delta:+.1f}%)"),
    ("CorridorUnifiedSeedDisk", "Unified Seed + Vogel Disk", f"RMSE {float(fields['unified_rmse']):.4f} | Edge {float(fields['unified_edge']):.4f}"),
]
bottom_panels = [
    ("CorridorSurfaceDomains", "Primary Surface Domains", "Road / Kerb / Walls / Gantry"),
    ("CorridorErrorFresh", "|Error| x4 : 1-spp Fresh", f"Edge RMSE {float(fields['fresh_edge']):.4f}"),
    ("CorridorErrorSeed", "|Error| x4 : Seed-Bank Only", f"Edge RMSE {float(fields['seed_only_edge']):.4f} (No Spatial)"),
    ("CorridorErrorCross", "|Error| x4 : Legacy Cross", f"Edge RMSE {float(fields['cross_edge']):.4f} ({fields['cross_links']} Links)"),
    ("CorridorErrorDisk", "|Error| x4 : Vogel Disk", f"Culled {fields['disk_culled']} Cross-Surface Taps"),
    ("CorridorErrorUnified", "|Error| x4 : Unified GI", f"Lowest Frame & Edge ({fields['unified_seeds']} Seeds)"),
]

scale = 3
tile = 64 * scale
pad = 14
header_h = 54
label_h = 40
cols = 6
width = pad + cols * (tile + pad)
height = header_h + 2 * (tile + label_h + pad) + 8

canvas = Image.new("RGB", (width, height), (15, 18, 24))
draw = ImageDraw.Draw(canvas)
font = ImageFont.load_default()

draw.text((pad, 12), "Frontier C++/Vulkan ReSTIR GI - Driving Corridor 4-Bounce Proving Grounds (64x64, SwiftShader Vulkan)", fill=(238, 242, 248), font=font)
draw.text((pad, 30), "Top: Rendered Radiance (Gamma 1.8)   |   Bottom: Surface Guide Domains & Per-Pixel Absolute Error x4 vs. Converged Reference", fill=(164, 176, 194), font=font)

for row_idx, panels in enumerate((top_panels, bottom_panels)):
    y0 = header_h + row_idx * (tile + label_h + pad)
    for col_idx, (stem, title, subtitle) in enumerate(panels):
        x0 = pad + col_idx * (tile + pad)
        with Image.open(source / f"{stem}.ppm") as img:
            rgb = img.convert("RGB").resize((tile, tile), Image.Resampling.NEAREST)
        draw.rectangle([x0 - 1, y0 - 1, x0 + tile, y0 + tile], outline=(58, 68, 84))
        canvas.paste(rgb, (x0, y0))
        title_color = (130, 238, 168) if col_idx == 5 else ((130, 202, 255) if col_idx in (0, 4) else (226, 232, 240))
        draw.text((x0, y0 + tile + 5), title, fill=title_color, font=font)
        draw.text((x0, y0 + tile + 20), subtitle, fill=(148, 160, 178), font=font)

canvas.save(dest, optimize=True)
digest = hashlib.sha256(dest.read_bytes()).hexdigest()
print(f"SPATIAL_DISK_COMPARISON_PNG {digest} {dest} {canvas.size[0]}x{canvas.size[1]}")
PY
    fi
} | tee "$RESULTS"
