#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckGpuTimestamps.sh — guards the R10 ② GPU timestamp scheme
#============================================================================================================================================
# The shadow stage and the ReSTIR dispatch each own a timestamp pair, and the reader subtracts them out of the
#    trailing span so no stage is credited with another's time. Three things can silently break that:
#
#    ① the pool stops being big enough for the pairs (a stray edit to kTimestampCount),
#    ② a write is added or removed so a span no longer closes,
#    ③ the availability flag is dropped from the read, at which point a frame that skipped a stage subtracts
#       UNDEFINED data — the failure that motivated the flag, and one that looks like a plausible number.
#
# All three are checked structurally here; the arithmetic itself was verified on a device (see the note in
#    References/GpuTimestamps.md).
set -u
cd "$(dirname "$0")/.."
Fail=0
V=Engine/DeviceExchange/VisibilityExchange.cpp
H=Engine/DeviceExchange/VisibilityExchange.h
S=Engine/DeviceExchange/SwapchainExchange.cpp

Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[GpuTimestamps] the pool holds every span"
Count=$(grep -oP 'kTimestampCount\s*=\s*\K[0-9]+' "$V" | head -1)
[ -n "$Count" ] && [ "$Count" -ge 20 ]
Report $? "kTimestampCount is $Count (>= 20: 12 base + shadow + restir + sky + volume)"

# Highest query index actually written must fit inside the pool.
Highest=$(grep -oP 'kTimestampCount \+ \K[0-9]+' "$V" | sort -n | tail -1)
[ -n "$Highest" ] && [ "$Highest" -lt "$Count" ]
Report $? "highest written query is $Highest, inside a pool of $Count"

echo
echo "[GpuTimestamps] every span opens and closes"
for Pair in "12:13:shadow" "14:15:restir" "16:17:sky" "18:19:volumetrics"; do
    A=${Pair%%:*}; Rest=${Pair#*:}; B=${Rest%%:*}; Name=${Rest#*:}
    Open=$(grep -c "kTimestampCount + ${A}u" "$V")
    Close=$(grep -c "kTimestampCount + ${B}u" "$V")
    [ "$Open" -ge 1 ] && [ "$Close" -ge 1 ]
    Report $? "$Name span writes both $A (x$Open) and $B (x$Close)"
done

echo
echo "[GpuTimestamps] the reader cannot consume undefined stamps"
grep -q 'VK_QUERY_RESULT_WITH_AVAILABILITY_BIT' "$V"
Report $? "results are read WITH_AVAILABILITY"
# A two-word stride is what makes the availability word land where Have() looks for it.
grep -q 'sizeof(uint64_t) \* 2u' "$V"
Report $? "the read uses a two-word stride to match"
grep -q 'Stamps\[kTimestampCount \* 2u\]' "$V"
Report $? "the destination buffer is sized for both words"
# Every span must go through Ms(), which is the only place the availability check lives.
Bare=$(grep -cE 'Telemetry\.[A-Za-z]+Milliseconds\s*=\s*[^;]*Stamps\[' "$V")
[ "$Bare" = "0" ]
Report $? "no telemetry field reads Stamps[] directly, bypassing the guard"

echo
echo "[GpuTimestamps] the stages are reported apart"
grep -q 'ShadowMilliseconds' "$H" && grep -q 'RestirMilliseconds' "$H" && grep -q 'PostMilliseconds' "$H" \
    && grep -q 'SkyMilliseconds' "$H" && grep -q 'VolumeMilliseconds' "$H"
Report $? "telemetry exposes shadow, restir, sky, volume and post separately"
# "post" must mean denoise + luminance only. If a new stage is added to the trailing span without being
#    subtracted here, post silently absorbs it and the new stage looks free.
grep -qE 'const float Owned = .*\+ Sky \+ Volume;' "$V"
Report $? "post subtracts the celestial spans instead of absorbing them"
# The bug this replaced: the trailing span reported whole as "kernel".
! grep -qE 'KernelMilliseconds\s*=\s*Ms\(10, *11\);' "$V"
Report $? "kernel is no longer the raw trailing span"
grep -q 'RecordRestirBegin' "$S" && grep -q 'RecordRestirEnd' "$S"
Report $? "the ReSTIR dispatch is bracketed at its call site"

echo
if [ "$Fail" != "0" ]; then echo "[GpuTimestamps] FAILED"; exit 1; fi
echo "[GpuTimestamps] OK"
