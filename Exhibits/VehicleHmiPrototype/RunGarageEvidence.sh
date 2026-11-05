#!/bin/bash
# 🧩 The Garage's ReSTIR view drawn on the CPU: runs the demo under Node with Dawn over SwiftShader (Tools/RunOnSoftwareGpu.mjs), drives the view from
#    Tools/GarageHooks.mjs and writes what the tablet's preview well would show.
#
#      Exhibits/VehicleHmiPrototype/RunGarageEvidence.sh [output folder]      (about ten minutes on two cores, then a quarter of an hour for the last run)
#
#    Two plans at 192 x 120: `paint` drops Command Red on the car at the centre of the picture (the reference's ripple, 12 pictures) and `scan` runs the
#    X-ray scan across its sweep (8 pictures). A last run takes two pictures of the scan at the stage's own size, 640 x 400, into Images/Stage640x400.
#    Each picture is <plan>-<name>.png with a .json beside it that says what the view had drawn: the ripple's clock and radius, the scan's clock and
#    front. MakeContactSheets.mjs tiles the first two plans into one sheet each.
#    Needs `npm ci` in Experimental/VehicleHmiPrototype and in its Tools (webgpu, @napi-rs/canvas), and registry.npmjs.org the first time (SwiftShader).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/Exhibits/VehicleHmiPrototype/Images}"
mkdir -p "$OUT"
cd "$ROOT/Experimental/VehicleHmiPrototype/Tools"
export HARNESS_TIMEOUT=1500000
# 📝 The studio camera's own picture is not the evidence here, and the software rasteriser draws it every frame: a small one leaves the time to the
#    Garage's frames (the harness's default is 1280 x 720, about two minutes a frame on two cores).
export SIZE="${SIZE:-320x180}"
QUERY="?capture=1&quality=low&lobby=1&garage=192x120"

echo "① paint: the ripple"
HOOKS=GarageHooks.mjs GARAGE_PLAN=paint GARAGE_OUT="$OUT" node RunOnSoftwareGpu.mjs node HeadlessHarness.mjs .. 12 "$QUERY"
echo "② scan: the sweep"
HOOKS=GarageHooks.mjs GARAGE_PLAN=scan GARAGE_OUT="$OUT" node RunOnSoftwareGpu.mjs node HeadlessHarness.mjs .. 8 "$QUERY"
echo "③ contact sheets"
node "$ROOT/Exhibits/VehicleHmiPrototype/MakeContactSheets.mjs" "$OUT"
if [ "${STAGE_SIZE:-1}" != "0" ]; then
    # 📝 The HUD's insets and type are in the reference's pixels, laid out for a 640 x 400 stage: at 192 x 120 its text does not fit. Two pictures of the scan
    #    at the stage's own size (the front at the middle of the car, then everything passed), a few minutes each. STAGE_SIZE=0 skips them.
    echo "④ scan at the stage's own size, 640 x 400"
    HOOKS=GarageHooks.mjs GARAGE_PLAN=scan GARAGE_STEP=2.1 GARAGE_OUT="$OUT/Stage640x400" node RunOnSoftwareGpu.mjs node HeadlessHarness.mjs .. 4 "?capture=1&quality=low&lobby=1&garage=640x400"
fi
