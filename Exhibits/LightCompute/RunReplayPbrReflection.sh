#!/usr/bin/env bash
#==========================================================================================================================================
#                                                RUNREPLAYPBRREFLECTION.SH
#==========================================================================================================================================
# 🧩 Validate bounded compact replay storage and capture exploratory authored PBR reflections.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

export PYTHONPATH="${ROOT}/_ClaudeScratch/pytools${PYTHONPATH:+:${PYTHONPATH}}"
export PATH="${ROOT}/_ClaudeScratch/pytools/bin:${PATH}"

BUILD_DIR="${BUILD_DIR:-${ROOT}/_ClaudeScratch/build/replay-pbr-reflection-release}"
SW_SHADER_DIR="${BUILD_DIR}/sw-shaders"
HW_SHADER_DIR="${BUILD_DIR}/hw-shaders"
CAPTURE_DIR="${BUILD_DIR}/capture"
RESULTS_FILE="${ROOT}/Exhibits/LightCompute/Results/ReplayPbrReflection-release.txt"
OUTPUT_PNG="${ROOT}/Exhibits/LightCompute/Images/ReplayPbrReflectionComparison.png"

SLANGC="${ROOT}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc"
SWIFTSHADER="${ROOT}/ExternalPackages/VulkanCompute/SwiftShader/Linux/vk_swiftshader_icd.json"
if [[ ! -f "$SWIFTSHADER" ]]; then
    SWIFTSHADER="${ROOT}/ExternalPackages/VulkanCompute/SwiftShader/libvk_swiftshader.so"
fi
if [[ ! -f "$SWIFTSHADER" ]]; then
    SWIFTSHADER="$(find "${ROOT}/ExternalPackages/VulkanCompute/SwiftShader" -name 'libvk_swiftshader.so' | head -n 1)"
fi

mkdir -p "$SW_SHADER_DIR" "$HW_SHADER_DIR" "$CAPTURE_DIR" "$(dirname "$RESULTS_FILE")" "$(dirname "$OUTPUT_PNG")"
rm -f "${SW_SHADER_DIR}"/*.spv "${HW_SHADER_DIR}"/*.spv

echo "[1/5] Compiling SPIR-V 1.3 Software BVH and SPIR-V 1.4 VK_KHR_ray_query PBR/ReSTIR shaders..."
"$SLANGC" Frontier/Engine/Rendering/Shaders/LightInfluence.slang \
    -entry TracePrimarySurfaces -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "${SW_SHADER_DIR}/Primary.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang \
    -entry GenerateReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "${SW_SHADER_DIR}/ReplayGenerate.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang \
    -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "${SW_SHADER_DIR}/ReplayResolve.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplaySampleHistory.slang \
    -entry GatherReplaySampleHistory -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "${SW_SHADER_DIR}/ReplayGatherSampleHistory.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplaySampleHistory.slang \
    -entry FinalizeReplaySampleHistory -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "${SW_SHADER_DIR}/ReplayFinalizeSampleHistory.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplayVarianceFilter.slang \
    -entry GatherReplayVarianceHistory -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "${SW_SHADER_DIR}/ReplayGatherVarianceHistory.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplayVarianceFilter.slang \
    -entry IntegrateReplayVarianceMoments -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "${SW_SHADER_DIR}/ReplayIntegrateVarianceMoments.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplayVarianceFilter.slang \
    -entry FilterReplayVarianceWavelet -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
    -o "${SW_SHADER_DIR}/ReplayFilterVarianceWavelet.spv"

"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplayRayQuery.slang \
    -entry TracePrimarySurfaces -target spirv -profile "spirv_1_4+spvRayQueryKHR" -emit-spirv-directly -O0 \
    -o "${HW_SHADER_DIR}/PrimaryHardwareRT.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplayRayQuery.slang \
    -entry GenerateReplayReservoirs -target spirv -profile "spirv_1_4+spvRayQueryKHR" -emit-spirv-directly -O0 \
    -o "${HW_SHADER_DIR}/ReplayGenerateHardwareRT.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplayRayQuery.slang \
    -entry ResolveReplayReservoirs -target spirv -profile "spirv_1_4+spvRayQueryKHR" -emit-spirv-directly -O0 \
    -o "${HW_SHADER_DIR}/ReplayResolveHardwareRT.spv"
"$SLANGC" Frontier/Engine/Rendering/Shaders/PathReplayRayQuery.slang \
    -entry FinalizeReplaySampleHistory -target spirv -profile "spirv_1_4+spvRayQueryKHR" -emit-spirv-directly -O0 \
    -o "${HW_SHADER_DIR}/ReplayFinalizeSampleHistoryHardwareRT.spv"

echo "[2/5] Building LightCompute + Engine Authored PBR & Unified ReSTIR GI targets..."
cmake -S Exhibits/LightCompute -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C "$BUILD_DIR" -j 2

{
    echo "=== FRONTIER: COMPACT REPLAY + EXPERIMENTAL AUTHORED PBR AND REFLECTIONS ==="
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    uname -srm
    "$SLANGC" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,PathReplayVarianceFilter,MaterialRayScattering}.slang \
        Exhibits/LightCompute/Code/{ReplayPbrReflectionGpuValidation.h,LightComputeValidation.cpp} \
        "$SW_SHADER_DIR"/{Primary,ReplayGenerate,ReplayResolve,ReplayGatherVarianceHistory,ReplayIntegrateVarianceMoments,ReplayFilterVarianceWavelet}.spv
    ctest --test-dir "$BUILD_DIR" --output-on-failure
    "${BUILD_DIR}/ReplayRayAccelerationValidation" --verify-spirv "$HW_SHADER_DIR"
    if [[ "${CAPTURE:-0}" == "1" ]]; then
        "${BUILD_DIR}/LightComputeValidation" "$SWIFTSHADER" "$SW_SHADER_DIR" \
            --replay-pbr-reflection "$CAPTURE_DIR"
    else
        "${BUILD_DIR}/LightComputeValidation" "$SWIFTSHADER" "$SW_SHADER_DIR" --replay-pbr-reflection
    fi
} | tee "$RESULTS_FILE"

if [[ "${CAPTURE:-0}" == "1" ]]; then
    echo "[5/5] Composing 6-panel visual + linear error heatmap proof: ${OUTPUT_PNG}"
    python3 - "$CAPTURE_DIR" "$OUTPUT_PNG" <<'PY'
import math, os, struct, sys
from PIL import Image, ImageDraw, ImageFont

cap_dir, out_png = sys.argv[1], sys.argv[2]
metrics = {}
with open(os.path.join(cap_dir, "PbrReflectionMetrics.txt"), "r", encoding="utf-8") as f:
    for line in f:
        if "=" in line:
            k, v = line.strip().split("=", 1)
            metrics[k] = float(v) if "." in v else int(v)

w, h = int(metrics["width"]), int(metrics["height"])
raw_img = Image.open(os.path.join(cap_dir, "ShowroomBalanceRaw.ppm")).convert("RGB")
uni_img = Image.open(os.path.join(cap_dir, "ShowroomUnifiedNoSvgf.ppm")).convert("RGB")
svgf_img = Image.open(os.path.join(cap_dir, "ShowroomUnifiedFullSvgf.ppm")).convert("RGB")
ref_img = Image.open(os.path.join(cap_dir, "ShowroomReference.ppm")).convert("RGB")

def load_f32_rgb(name):
    raw = open(os.path.join(cap_dir, name), "rb").read()
    vals = struct.unpack("<" + "f" * (w * h * 3), raw)
    return [(vals[3 * i], vals[3 * i + 1], vals[3 * i + 2]) for i in range(w * h)]

raw_lin = load_f32_rgb("ShowroomBalanceRaw.f32")
svgf_lin = load_f32_rgb("ShowroomUnifiedFullSvgf.f32")
ref_lin = load_f32_rgb("ShowroomReference.f32")

def px_rmse(a, b):
    return math.sqrt(((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) / 3.0)

err_raw = [px_rmse(raw_lin[i], ref_lin[i]) for i in range(w * h)]
err_svgf = [px_rmse(svgf_lin[i], ref_lin[i]) for i in range(w * h)]

sorted_err = sorted(err_raw)
err_scale = max(0.05, sorted_err[int(0.95 * (len(sorted_err) - 1))])

def turbo_like(t):
    t = max(0.0, min(1.0, t))
    stops = [
        (0.00, (14, 18, 52)),
        (0.22, (24, 96, 178)),
        (0.45, (32, 184, 156)),
        (0.68, (226, 198, 46)),
        (0.86, (240, 96, 28)),
        (1.00, (255, 244, 214)),
    ]
    for idx in range(len(stops) - 1):
        t0, c0 = stops[idx]
        t1, c1 = stops[idx + 1]
        if t <= t1:
            u = (t - t0) / max(1e-6, t1 - t0)
            return tuple(int(round(c0[c] + u * (c1[c] - c0[c]))) for c in range(3))
    return stops[-1][1]

heat_raw = Image.new("RGB", (w, h))
heat_svgf = Image.new("RGB", (w, h))
for i in range(w * h):
    x, y = i % w, i // w
    heat_raw.putpixel((x, y), turbo_like(err_raw[i] / err_scale))
    heat_svgf.putpixel((x, y), turbo_like(err_svgf[i] / err_scale))

scale = 5
pw, ph = w * scale, h * scale
pad_x, pad_y, header_h, label_h, footer_h = 22, 18, 66, 48, 42
canvas_w = pad_x * 4 + pw * 3
canvas_h = header_h + 2 * (label_h + ph + pad_y) + footer_h

canvas = Image.new("RGB", (canvas_w, canvas_h), (15, 18, 24))
draw = ImageDraw.Draw(canvas)
font = ImageFont.load_default()

rmse_drop = 100.0 * (1.0 - metrics["unified_full_svgf_rmse"] / metrics["raw_balance_rmse"])
flicker_drop = 100.0 * (1.0 - metrics["full_svgf_flicker"] / metrics["raw_flicker"])
glass_ratio = metrics["glazed_window_lum"] / max(1e-6, metrics["blocked_window_lum"])

title = (f"FRONTIER: EXPERIMENTAL PBR RESERVOIR REPLAY - SHOWROOM "
         f"({metrics['facets']} TRIANGLES, {w}x{h}, 3 BOUNCES)")
subtitle = (f"64x64 compact output: -{metrics['output_saved_pct']:.2f}% bytes, max diff 0  |  "
            f"1080p output-only extrapolation: 1455.7 to 537.9 MiB  |  Same-shader RMSE -{rmse_drop:.1f}%")
draw.text((pad_x, 14), title, fill=(238, 242, 248), font=font)
draw.text((pad_x, 36), subtitle, fill=(146, 214, 178), font=font)

panels = [
    (raw_img, "1. Raw 1-sample PBR replay",
     f"RMSE={metrics['raw_balance_rmse']:.4f} | Consecutive-frame difference={metrics['raw_flicker']:.4f}"),
    (uni_img, "2. Temporal + spatial replay (no seed bank)",
     f"RMSE={metrics['unified_no_svgf_rmse']:.4f} | Power MIS | Additional warm-up and spatial rays"),
    (svgf_img, "3. Replay + compact storage + SVGF",
     f"RMSE={metrics['unified_full_svgf_rmse']:.4f} | Frame difference={metrics['full_svgf_flicker']:.4f}"),
    (ref_img, "4. 80-sample same-shader comparison",
     "Finite comparison; not independent ground truth"),
    (heat_raw, "5. Linear RGB difference: raw vs comparison",
     f"Scale 0..{err_scale:.2f} linear radiance RMSE"),
    (heat_svgf, "6. Linear RGB difference: replay + SVGF",
     f"Same scale 0..{err_scale:.2f} | Hood={metrics['hood_clearcoat_rmse']:.3f} Velvet={metrics['velvet_sheen_rmse']:.3f}"),
]

for idx, (img, line1, line2) in enumerate(panels):
    col, row = idx % 3, idx // 3
    x0 = pad_x + col * (pw + pad_x)
    y0 = header_h + row * (label_h + ph + pad_y)
    draw.rectangle([x0 - 2, y0 + label_h - 2, x0 + pw + 2, y0 + label_h + ph + 2], outline=(58, 68, 84), width=2)
    up = img.resize((pw, ph), Image.Resampling.NEAREST)
    canvas.paste(up, (x0, y0 + label_h))
    draw.text((x0, y0 + 4), line1, fill=(232, 236, 244), font=font)
    draw.text((x0, y0 + 22), line2, fill=(168, 184, 206), font=font)

footer = (f"SwiftShader CPU Vulkan; no game-GPU timing. Canopy balance / power RMSE: "
          f"{metrics['canopy_balance_rmse']:.4f} / {metrics['canopy_power_rmse']:.4f} (descriptive, not a promotion gate).")
draw.text((pad_x, canvas_h - 28), footer, fill=(148, 160, 178), font=font)
canvas.save(out_png, format="PNG")
print("SAVED_PNG", out_png, canvas.size)
PY
fi
