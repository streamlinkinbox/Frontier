#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
#  CheckAtmosphereScattering.sh — A3: the single-scattering sky, and its agreement with the shader
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
set -u
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$Root" || exit 1
Fail=0

echo "[Atmosphere] physical behaviour"
Binary="$(mktemp -u /tmp/Atmosphere.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I Engine -I . Scratchpad/AtmosphereScatteringTest.cpp -o "$Binary" 2>/tmp/Atmosphere.build; then
    echo "  COMPILE FAILED"; sed 's/^/    /' /tmp/Atmosphere.build | head -20; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

echo
echo "[Atmosphere] the CPU port and the shader agree"
# The CPU port is verbatim. If a constant is changed in one and not the other the proof silently stops describing
# what the GPU runs, which is worse than having no proof.
#
# ⚠️ The port lives in the ENGINE now, not in this harness: adaptive exposure needs the same model to work out
# how much light the sky is putting on the scene, and a second copy would have been a third description of one
# atmosphere. The proof and the exposure read the same file, and this is what keeps that file honest.
Shader="Engine/Shaders/AtmosphereScattering.slang"
Harness="Engine/DisplayPresentation/AtmosphereModel.h"
for Constant in kPlanetRadius kAtmosphereThickness kRayleighScaleHeight kMieScaleHeight \
                kMieScattering kMieExtinction kMieAsymmetry kOzoneCentre kOzoneWidth kSunAngularRadius; do
    ShaderValue=$(grep -oP "${Constant}\s*=\s*\K[0-9.e+-]+" "$Shader"  | head -1)
    HarnessValue=$(grep -oP "${Constant}\s*=\s*\K[0-9.e+-]+" "$Harness" | head -1)
    if [ -z "$ShaderValue" ] || [ -z "$HarnessValue" ]; then
        echo "  $Constant is missing from one side"; Fail=1
    elif [ "${ShaderValue%f}" != "${HarnessValue%f}" ]; then
        echo "  $Constant is $ShaderValue in the shader but $HarnessValue in the harness"; Fail=1
    fi
done
# The scattering vectors are the ones that set the sky's colour; compare them as whole lines.
for Vector in kRayleighScattering kOzoneAbsorption; do
    ShaderNumbers=$(grep -oP "${Vector}[^;]*" "$Shader"  | grep -oP '[0-9]+\.[0-9]+e-[0-9]+' | tr '\n' ' ')
    HarnessNumbers=$(grep -oP "${Vector}[^;]*" "$Harness" | grep -oP '[0-9]+\.[0-9]+e-[0-9]+' | tr '\n' ' ')
    [ "$ShaderNumbers" = "$HarnessNumbers" ] \
        || { echo "  $Vector differs: shader [$ShaderNumbers] harness [$HarnessNumbers]"; Fail=1; }
done

# The ray-miss path must actually call the sky, and must keep the zero-illuminance identity switch.
grep -q 'SkyRadiance(CameraAltitude' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the kernel's ray-miss path does not evaluate the sky"; Fail=1; }
grep -q 'SunIlluminance > 0.0' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the zero-illuminance identity switch is gone — pre-A3 images could not be reproduced"; Fail=1; }

# ⚠️ A moving sun must invalidate the accumulated history exactly as a moving camera does, or a sunset blends
# two different skies and smears into a grey dissolve that reads as a denoiser fault.
grep -q 'Resized || SunMoved' Engine/DisplayPresentation/ReSTIRIntegrator.cpp \
    || { echo "  a moving sun does not restart accumulation — a sunset would smear"; Fail=1; }

# Push constants are now exactly at Vulkan's guaranteed 128 bytes; anything further needs a uniform buffer.
grep -q 'sizeof(DispatchConfiguration) == 128u' Engine/DeviceExchange/SwapchainExchange.h \
    || { echo "  the push-constant size assertion no longer states 128 bytes"; Fail=1; }

# ── A2 lookup tables ─────────────────────────────────────────────────────────────────────────────────────────────
# Table sizes must agree between the shader that defines them, the C++ that allocates them, and the harness that
# proves them. Three copies of a number is three chances to drift.
for Pair in "kTransmittanceWidth:kTransmittanceLutWidth" "kTransmittanceHeight:kTransmittanceLutHeight" \
            "kMultiScatterSize:kMultiScatterLutSize"; do
    ShaderName="${Pair%%:*}"; CppName="${Pair##*:}"
    ShaderValue=$(grep -oP "${ShaderName}\s*=\s*\K[0-9]+" Engine/Shaders/AtmosphereScattering.slang | head -1)
    CppValue=$(grep -oP "${CppName}\s*=\s*\K[0-9]+" Engine/DeviceExchange/SwapchainExchange.h | head -1)
    HarnessValue=$(grep -oP "${ShaderName}\s*=\s*\K[0-9]+" Engine/DisplayPresentation/AtmosphereModel.h | head -1)
    if [ "$ShaderValue" != "$CppValue" ] || [ "$ShaderValue" != "$HarnessValue" ]; then
        echo "  $ShaderName disagrees: shader $ShaderValue, C++ $CppValue, harness $HarnessValue"; Fail=1
    fi
done

# ⚠️ Textures[] is a variable-count binding and Vulkan requires it on the HIGHEST binding number. The LUTs took
# 21 and 22, so it had to move to 23; leaving it where it was silently breaks descriptor-indexing devices.
TexturesBinding=$(grep -oP 'layout\(binding = \K[0-9]+(?=\) uniform sampler2D Textures)' Engine/Shaders/ReSTIRViewport.slang)
BindingCount=$(grep -oP 'kComputeBindingCount\s*=\s*\K[0-9]+' Engine/DeviceExchange/SwapchainExchange.h)
[ "$TexturesBinding" = "$((BindingCount - 1))" ] \
    || { echo "  Textures[] is at $TexturesBinding but must be the highest of $BindingCount bindings"; Fail=1; }

# The LUTs must be sampled, not storage, in the kernel's set — a storage image cannot filter, and an unfiltered
# transmittance table bands visibly across the sunset.
grep -q 'B == 21u || B == 22u' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  the atmosphere LUTs are not declared as sampled images"; Fail=1; }

# The tables are constant FOR A GIVEN ATMOSPHERE; building them per frame would throw away the entire point.
grep -q '!Vulkan->AtmosphereTablesBuilt' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  the LUT build has no latch at all — it would run every frame"; Fail=1; }

# Bring-up order: the tables must exist before WriteDescriptorSet binds them.
LutLine=$(grep -n '"BringAtmosphereTables"' Engine/DeviceExchange/SwapchainExchange.cpp | head -1 | cut -d: -f1)
SetLine=$(grep -n '"BringDescriptorSet"'    Engine/DeviceExchange/SwapchainExchange.cpp | head -1 | cut -d: -f1)
if [ -n "$LutLine" ] && [ -n "$SetLine" ] && [ "$LutLine" -gt "$SetLine" ]; then
    echo "  BringAtmosphereTables runs after BringDescriptorSet — the LUTs would never be bound"; Fail=1
fi

# ── A4: the sun as a light ───────────────────────────────────────────────────────────────────────────────────────
# 🔴 Emission must be read through ONE accessor. It used to be fetched inline at eleven separate sites, and
# adding the sun to each would have been eleven chances to miss one — a missed site reads a scene material for
# the sun, which is a plausible wrong colour rather than an obvious failure.
InlineFetches=$(grep -c 'floatBitsToUint(Triangles\[LightTriangle' Engine/Shaders/ReSTIRViewport.slang)
[ "$InlineFetches" = "1" ] \
    || { echo "  $InlineFetches inline emission fetches; only LightEmission() may read a light's material"; Fail=1; }

grep -q 'vec3 LightEmission(uint lightIndex)' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the LightEmission accessor is gone"; Fail=1; }
grep -q 'vec3 SampleLightPointFor(' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the SampleLightPointFor accessor is gone"; Fail=1; }

# The sun and the sky must consult the SAME transmittance, or the disc, the sky and the light on the ground
# redden at different rates and the sunset comes apart.
grep -q 'SampleTransmittance(max(CameraAltitude, 1.0), SunDirection.z) \* SunIlluminance' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the sun's radiance does not use the shared transmittance table"; Fail=1; }

# A sun-only scene has no emissive triangle; gating direct lighting on the triangle count would leave it black.
grep -q 'LightSlotCount() > 0u' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  direct lighting is still gated on the triangle count — a sun-only scene would be black"; Fail=1; }

# ⚠️ The second bounce must divide by the slot count it sampled from. Dividing by a different number is a bias
# that scales the entire indirect term.
grep -q 'bDist2 + 0.01) \* float(bSlots)' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the bounce estimator does not divide by the slot count it sampled from"; Fail=1; }

# The sun must drop out of the candidate set once it is below the horizon, or samples are spent on a light that
# cannot contribute.
grep -q 'SunDirection.z > -0.05' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  a set sun is still offered as a light candidate"; Fail=1; }

# ── A5: the sky as an environment light ──────────────────────────────────────────────────────────────────────────
# An escaped bounce ray must gather sky radiance, or a surface facing the oculus receives no skylight at all.
grep -q 'if (!bounceHit.valid' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  an escaped bounce ray does not gather the sky — the room gets no skylight"; Fail=1; }
grep -q 'kFeatureSkyLighting' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  sky lighting has no feature bit, so the pre-A5 image cannot be reproduced"; Fail=1; }
grep -q 'DispatchFeatureSkyLighting' Engine/DeviceExchange/SwapchainExchange.h \
    || { echo "  the C++ mirror of the sky-lighting bit is missing"; Fail=1; }

# ⚠️ Single dome, by design. CLAUDE.md 15b records why and what changes when portals arrive; losing that note
# means the next reader either generalises speculatively or trips over the assumption without warning.
grep -q 'Deferred by Design — Single Sky Dome' CLAUDE.md \
    || { echo "  the single-dome note is gone from CLAUDE.md"; Fail=1; }

# ── A7: the night sky ────────────────────────────────────────────────────────────────────────────────────────────
# 🔴 The push block must stay at 128 bytes — Vulkan's GUARANTEED MINIMUM. A7's sky parameters moved to a uniform
# buffer for exactly this reason: growing to 160 would work on a card offering 256 and fail on one offering the
# guarantee, i.e. break on someone else's machine rather than here.
grep -q 'sizeof(DispatchConfiguration) == 128u' Engine/DeviceExchange/SwapchainExchange.h \
    || { echo "  the push block is no longer pinned at 128 bytes"; Fail=1; }
grep -q 'sizeof(SkyRecord) == 64u' Engine/DeviceExchange/SwapchainExchange.h \
    || { echo "  SkyRecord is not pinned to its std140 size"; Fail=1; }
grep -q 'layout(binding = 24) uniform SkyRecord' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the sky record is not a uniform buffer at binding 24"; Fail=1; }

# Textures[] must still be the highest binding.
TexturesBinding=$(grep -oP 'layout\(binding = \K[0-9]+(?=\) uniform sampler2D Textures)' Engine/Shaders/ReSTIRViewport.slang)
BindingCount=$(grep -oP 'kComputeBindingCount\s*=\s*\K[0-9]+' Engine/DeviceExchange/SwapchainExchange.h)
[ "$TexturesBinding" = "$((BindingCount - 1))" ] \
    || { echo "  Textures[] is at $TexturesBinding but must be the highest of $BindingCount"; Fail=1; }

# ⚠️ The moon's terminator must come from the sphere's normal. A straight chord gives a "D" at half phase and
# horns pointing the wrong way at crescent — instantly wrong, and not fixable by tuning.
grep -q 'sqrt(max(0.0, 1.0 - R2))' Engine/Shaders/AtmosphereScattering.slang \
    || { echo "  the moon phase is not derived from the sphere normal"; Fail=1; }

# Stars must be cell-based, or a latitude/longitude grid clusters them at the poles as two bright patches.
grep -q 'floor(Scaled)' Engine/Shaders/AtmosphereScattering.slang \
    || { echo "  the star field is not cell-based — it would cluster at the poles"; Fail=1; }

# ⚠️ The grid resolution and hash threshold together set the STAR COUNT, and the harness measures it by walking
# the sphere. They must agree with the shader or the harness is counting a field that is not the one rendered.
# This shipped at 700 / 0.982 = 120 352 stars, thirteen times the real sky, and it looked plausible in a still.
for Pair in "Cells = :kCells     = " "H < :kThreshold = "; do
    ShaderToken="${Pair%%:*}"; HarnessToken="${Pair##*:}"
    ShaderValue=$(grep -oP "const float ${ShaderToken}\K[0-9.]+" Engine/Shaders/AtmosphereScattering.slang | head -1)
    [ -n "$ShaderValue" ] || ShaderValue=$(grep -oP "if \(${ShaderToken}\K[0-9.]+" Engine/Shaders/AtmosphereScattering.slang | head -1)
    HarnessValue=$(grep -oP "${HarnessToken}\K[0-9.]+" Engine/DisplayPresentation/AtmosphereModel.h | head -1)
    if [ -n "$ShaderValue" ] && [ -n "$HarnessValue" ] && [ "${ShaderValue%f}" != "${HarnessValue%f}" ]; then
        echo "  star-field constant differs: shader $ShaderValue, harness $HarnessValue"; Fail=1
    fi
done

# The night sky must be attenuated by the atmosphere, so it fades at dawn rather than switching off.
grep -q 'StarField(rayDirection, StarRotation, StarBrightness) \* Attenuation' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the stars are not attenuated by the atmosphere — they would pop at dawn"; Fail=1; }

grep -q 'kFeatureNightSky' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the night sky has no feature bit — the pre-A7 image is unreproducible"; Fail=1; }

# ⚠️ Stars must be wide enough to survive sub-pixel camera motion. A falloff of 900 gave a 0.72 px star at
# 1080p, so whether a pixel centre landed inside it changed as the camera moved and the field blinked on and
# off — which reads as a rendering fault, not as stars.
StarFalloff=$(grep -oP 'exp\(-Distance \* Distance \* \K[0-9.]+' Engine/Shaders/AtmosphereScattering.slang)
HarnessFalloff=$(grep -oP 'kFalloff = \K[0-9.]+' Scratchpad/AtmosphereScatteringTest.cpp | head -1)
awk -v f="$StarFalloff" 'BEGIN { exit !(f > 0 && f < 300) }' \
    || { echo "  star falloff $StarFalloff makes them sub-pixel — they will flicker"; Fail=1; }
[ "${StarFalloff%f}" = "${HarnessFalloff%f}" ] \
    || { echo "  star falloff differs: shader $StarFalloff, harness $HarnessFalloff"; Fail=1; }

# ⚠️ Stars must NOT be gated on time of day. They are in the sky at noon and are merely outshone; a
# "skip stars when the sun is up" optimisation looks harmless and silently breaks every case where daylight
# stars are real — high altitude, a total eclipse, deep twilight. The CPU side may only gate on the feature
# toggle and on the sky being enabled at all.
if grep -qE 'StarBrightness\s*=.*Sun(Direction|\.Zenith)' Engine/DisplayPresentation/ReSTIRIntegrator.cpp; then
    echo "  star brightness is gated on the sun's position — daylight stars would be lost"; Fail=1
fi
if grep -qE 'SunDirection\.z\s*[<>].*StarField|StarField.*SunDirection\.z' Engine/Shaders/ReSTIRViewport.slang; then
    echo "  the shader skips stars based on the sun's elevation"; Fail=1
fi

# ── A7b: turbidity ───────────────────────────────────────────────────────────────────────────────────────────────
# 🔴 Every aerosol term must go through the accessors. Turbidity scales Mie, and the whole point of routing it
# through MieScattering()/MieExtinction() is that a site which still reads the raw constant is SILENT: it keeps
# marching clear air while everything around it thickens. The worst case is a LUT builder that misses one, since
# the table then describes different air from the march that samples it and the horizon stops matching the sky.
for Shader in Engine/Shaders/AtmosphereScattering.slang Engine/Shaders/ReSTIRViewport.slang Engine/Shaders/AtmosphereLut.slang; do
    if grep -qE 'vec3\(kMie(Scattering|Extinction)\)' "$Shader"; then
        echo "  $Shader still reads a raw Mie constant — that site would ignore turbidity"; Fail=1
    fi
done
if grep -qE 'Vec3\(kMie(Scattering|Extinction)\)' Engine/DisplayPresentation/AtmosphereModel.h; then
    echo "  the harness still reads a raw Mie constant — it would stop describing the shader"; Fail=1
fi
grep -q 'float MieScattering() { return kMieScattering \* gAerosolTurbidity; }' Engine/Shaders/AtmosphereScattering.slang \
    || { echo "  the Mie accessors no longer scale by turbidity"; Fail=1; }

# ⚠️ Every entry point must set the global inside main, before anything can read it. The kernel evaluates the
# sky, the sun disc and the bounce gather; whichever ran first would see clear air if the assignment came later.
for Pair in "Engine/Shaders/ReSTIRViewport.slang:SkyTurbidity" "Engine/Shaders/AtmosphereLut.slang:Turbidity"; do
    Shader="${Pair%%:*}"; Uniform="${Pair##*:}"
    AssignLine=$(grep -n "gAerosolTurbidity = clamp(${Uniform}" "$Shader" | head -1 | cut -d: -f1)
    MainLine=$(grep -n '^void main()' "$Shader" | head -1 | cut -d: -f1)
    if [ -z "$AssignLine" ]; then
        echo "  $Shader never assigns gAerosolTurbidity — it would render the clear reference atmosphere"; Fail=1
    elif [ -n "$MainLine" ] && [ "$AssignLine" -lt "$MainLine" ]; then
        echo "  $Shader assigns gAerosolTurbidity outside main()"; Fail=1
    fi
done

# 🔴 The tables are a function of the ATMOSPHERE, and turbidity is now part of it. A plain "built once" latch
# would leave them baked at whatever the air happened to be on frame one, so the sun's survival and the sky's
# scattering would describe different days. Rebuild on a threshold, not on inequality: the diurnal curve moves
# continuously and exact comparison would rebuild every frame.
grep -q 'AtmosphereTablesTurbidity = FrameTurbidity' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  the LUTs never record the turbidity they were baked at — they would go stale"; Fail=1; }
grep -q 'std::fabs(FrameTurbidity - Vulkan->AtmosphereTablesTurbidity)' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  nothing compares the frame's turbidity against the tables' — they would never rebuild"; Fail=1; }
grep -q 'Push.Turbidity  = FrameTurbidity' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  the LUT build does not bake the frame's turbidity"; Fail=1; }

# ⚠️ A rebuild is not the first build. With two frames in flight the previous frame's kernel may still be
# sampling these images, so the transition into GENERAL must wait on the reading stage. TOP_OF_PIPE waits for
# nothing, which was correct exactly once.
if grep -B3 'ToGeneral.data()' Engine/DeviceExchange/SwapchainExchange.cpp | grep -q 'TOP_OF_PIPE'; then
    echo "  the LUT rebuild barrier still waits on nothing — it can race a frame still sampling the tables"; Fail=1
fi

# The record must carry it on both sides, and the block must not have grown.
grep -q 'float   SkyTurbidity;' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the sky record has no turbidity field"; Fail=1; }
grep -q 'float    SkyTurbidity;' Engine/DeviceExchange/SwapchainExchange.h \
    || { echo "  the C++ mirror of the sky record has no turbidity field"; Fail=1; }

# Both shaders must clamp to the same ceiling, or the tables and the march diverge at the top of the slider.
KernelClamp=$(grep -oP 'gAerosolTurbidity = clamp\(SkyTurbidity, 0.0, \K[0-9.]+' Engine/Shaders/ReSTIRViewport.slang)
LutClamp=$(grep -oP 'gAerosolTurbidity = clamp\(Turbidity, 0.0, \K[0-9.]+' Engine/Shaders/AtmosphereLut.slang)
[ -n "$KernelClamp" ] && [ "$KernelClamp" = "$LutClamp" ] \
    || { echo "  turbidity is clamped to '$KernelClamp' in the kernel but '$LutClamp' in the LUT builder"; Fail=1; }

# ⚠️ The diurnal curve is the mechanism that makes dawn differ from dusk, and the harness ports it verbatim.
# If the two drift, the proof describes a day the renderer does not have.
grep -q 'kTwoPi \* (Hour - 18.0) / 24.0' Engine/DisplayPresentation/ReSTIRIntegrator.h \
    || { echo "  the aerosol curve no longer peaks at 18:00 — dawn and dusk would not differ as claimed"; Fail=1; }
grep -q 'kTwoPi \* (Hour - 18.0) / 24.0' Scratchpad/AtmosphereScatteringTest.cpp \
    || { echo "  the harness no longer ports the aerosol curve verbatim"; Fail=1; }
for Side in Engine/DisplayPresentation/ReSTIRIntegrator.h Scratchpad/AtmosphereScatteringTest.cpp; do
    grep -q 'Value < 0.05 ? 0.05 : Value' "$Side" \
        || { echo "  $Side lost the turbidity floor — negative air amplifies light"; Fail=1; }
done

# 🔴 Turbidity must scale Mie ONLY. Rayleigh is the air molecules themselves and does not care how dusty the day
# is; wiring turbidity into it would turn a haze control into a global brightness control, which would look
# plausible on a single screenshot and be wrong everywhere else.
if grep -qE 'kRayleighScattering[^;]*gAerosolTurbidity|gAerosolTurbidity[^;]*kRayleighScattering' \
        Engine/Shaders/AtmosphereScattering.slang Engine/Shaders/ReSTIRViewport.slang; then
    echo "  turbidity scales Rayleigh — it is a haze parameter, not a brightness one"; Fail=1
fi

# ── A7c: the planet has a surface ────────────────────────────────────────────────────────────────────────────────
# 🔴 A view ray that ends on the planet must see LIT GROUND, not the thin slice of air in front of it. Without
# this the world ended in a black void along the horizon: measured, 0.2 deg below horizontal fell from 7473 to
# 74 cd/m2, and in an outdoor scene that void filled the bottom half of the frame, dragged the metered scene
# luminance from 5752 to 67 and raised the exposure 86x. That is the reported "fine up close, and the whole
# scene gets brighter as I move away".
grep -q 'vec3 GroundReflection(' Engine/Shaders/AtmosphereScattering.slang \
    || { echo "  the ground reflection is gone — the world would end at the horizon again"; Fail=1; }
for Shader in Engine/Shaders/AtmosphereScattering.slang Engine/Shaders/ReSTIRViewport.slang; do
    grep -q 'HitsGround' "$Shader" \
        || { echo "  $Shader does not add the ground where the ray ends on it"; Fail=1; }
done
grep -q 'GroundReflection' Engine/DisplayPresentation/AtmosphereModel.h \
    || { echo "  the harness no longer ports the ground, so it cannot prove the cliff is gone"; Fail=1; }

# The albedo is duplicated in the shader and the harness.
ShaderAlbedo=$(grep -oP 'kGroundAlbedo\s*=\s*\K[0-9.]+' Engine/Shaders/AtmosphereScattering.slang | head -1)
HarnessAlbedo=$(grep -oP 'kGroundAlbedo\s*=\s*\K[0-9.]+' Engine/DisplayPresentation/AtmosphereModel.h | head -1)
[ -n "$ShaderAlbedo" ] && [ "${ShaderAlbedo%f}" = "${HarnessAlbedo%f}" ] \
    || { echo "  ground albedo differs: shader '$ShaderAlbedo', harness '$HarnessAlbedo'"; Fail=1; }

# ⚠️ Lambertian, so the ground's brightness must NOT depend on the view direction. A view-dependent term here
# would make the ground change brightness as the camera turns, which reads as a flickering horizon.
if grep -A6 'vec3 GroundReflection(' Engine/Shaders/AtmosphereScattering.slang | grep -q 'ViewDirection'; then
    echo "  the ground reflection depends on the view direction — it is meant to be lambertian"; Fail=1
fi

# ⚠️ The tabulated path must light the ground with the SKY as well as the sun, or the surface switches off the
# instant the sun dips and the horizon goes black through the whole of twilight.
grep -q 'SkyIrradiance = SampleMultiScatter' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the ground gets no skylight — the horizon would go black at dusk"; Fail=1; }

# ── A7c: the moon's apparent size ────────────────────────────────────────────────────────────────────────────────
# The scale is a MULTIPLE of the true angle, never a replacement for it, so 1.0 always means the real moon.
grep -q 'kMoonAngularRadius \* clamp(MoonAngularScale' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the moon's size is no longer a multiple of its true angular radius"; Fail=1; }
grep -q 'float    MoonAngularScale;' Engine/DeviceExchange/SwapchainExchange.h \
    || { echo "  the C++ mirror of the sky record has no moon scale"; Fail=1; }

# 🔴 The disc's radiance must be divided by the TRUE solid angle, not the scaled one. Dividing by the scaled
# angle conserves flux, so an enlarged moon dims as it grows and reads as a flat grey disc instead of the moon.
if grep -q 'SolidAngle = 6.28318530 \* (1.0 - cos(MoonRadius))' Engine/Shaders/ReSTIRViewport.slang; then
    echo "  the moon's radiance is divided by its scaled solid angle — a larger moon would dim"; Fail=1
fi

# ── A7f: the tables must resolve twilight ────────────────────────────────────────────────────────────────────────
# 🔴 Multiple scattering is what makes a dawn horizon pale rather than blood red — it takes the horizon from
# R/B 238 down to 5.4 — so an error in the table that stores it lands straight on the colour. Parameterised
# LINEAR IN COSINE, one texel of 32 spanned 3.58 deg of sun elevation and the whole of civil twilight fell inside
# two of them, for up to 2.51x error. The signed square root crowds texels toward the horizon instead.
grep -q '0.5 + 0.5 \* S \* sqrt(abs(CosSunZenith))' Engine/Shaders/AtmosphereScattering.slang \
    || { echo "  the sun-angle parameterisation is linear in cosine again — twilight would be two texels wide"; Fail=1; }
grep -q 'CosSunZenith = (T < 0.0 ? -1.0 : 1.0) \* T \* T;' Engine/Shaders/AtmosphereScattering.slang \
    || { echo "  the inverse does not undo the forward map — the tables would be filled for the wrong angles"; Fail=1; }
grep -q 'CosSunZenith = (T < 0.0f ? -1.0f : 1.0f) \* T \* T;' Engine/DisplayPresentation/AtmosphereModel.h \
    || { echo "  the CPU model still inverts the old mapping"; Fail=1; }

# ⚠️ Forward and inverse are a PAIR. The tables are filled through the inverse and read through the forward, so
# a mismatch does not fail loudly — it silently describes an atmosphere lit from a different sun angle.
if grep -q 'CosSunZenith \* 0.5 + 0.5' Engine/Shaders/AtmosphereScattering.slang; then
    echo "  a linear-in-cosine forward map survives somewhere — it cannot pair with the square-root inverse"; Fail=1
fi

# The multi-scatter surface's size is duplicated in the header and the CPU model.
ShaderSize=$(grep -oP 'kMultiScatterLutSize\s*=\s*\K[0-9]+' Engine/DeviceExchange/SwapchainExchange.h | head -1)
ModelSize=$(grep -oP 'kMultiScatterSize\s*=\s*\K[0-9]+' Engine/DisplayPresentation/AtmosphereModel.h | head -1)
[ -n "$ShaderSize" ] && [ "$ShaderSize" = "$ModelSize" ] \
    || { echo "  multi-scatter surface size differs: engine $ShaderSize, model $ModelSize"; Fail=1; }
awk -v f="$ShaderSize" 'BEGIN { exit !(f >= 64) }' \
    || { echo "  the multi-scatter surface is back below 64 — twilight error rises above a third of a stop"; Fail=1; }

[ "$Fail" = "0" ] && echo "  constants, LUTs, bindings, sun, sky lighting, night sky, turbidity, ground and moon agree PASS"

echo
Glslang=""
for Candidate in /tmp/gl/build/StandAlone/glslang "$(command -v glslang 2>/dev/null)" "$(command -v glslangValidator 2>/dev/null)"; do
    [ -n "$Candidate" ] && [ -x "$Candidate" ] && Glslang="$Candidate" && break
done
if [ -z "$Glslang" ]; then
    echo "[Atmosphere] glslang not found — SPIR-V check skipped"
    [ "$Fail" = "0" ] && { echo; echo "[Atmosphere] OK (CPU only)"; exit 0; }
    echo; echo "[Atmosphere] FAILED"; exit 1
fi

echo "[Atmosphere] compiling the kernel with the sky included"
if ( cd Engine/Shaders && "$Glslang" -V --target-env vulkan1.2 -S comp ReSTIRViewport.slang -o /dev/null ) >/tmp/Atmosphere.spv.log 2>&1; then
    echo "  the kernel compiles to SPIR-V                                    PASS"
else
    sed 's/^/    /' /tmp/Atmosphere.spv.log | head -20; Fail=1
fi

# ⚠️ The table builder shares the header and is NOT part of the kernel's compilation, so a change that breaks
# only the builder — a uniform it does not declare, a global assigned before it exists — compiles above and
# fails at bring-up, where the symptom is a black sky rather than an error message.
if ( cd Engine/Shaders && "$Glslang" -V --target-env vulkan1.2 -S comp AtmosphereLut.slang -o /dev/null ) >/tmp/AtmosphereLut.spv.log 2>&1; then
    echo "  the table builder compiles to SPIR-V                             PASS"
else
    sed 's/^/    /' /tmp/AtmosphereLut.spv.log | head -20; Fail=1
fi

echo

# ── A7f: the transmittance and multiple-scattering tables must NOT share a parameterisation ──────────────────
# 🔴 They answer different questions. Transmittance is only ever wanted for a ray that clears the horizon;
# multiple scattering is wanted precisely when the sun is BELOW it, because that is what lights twilight.
# Sharing one mapping meant half the transmittance table stored zero and bilinear filtering interpolated across
# the discontinuity at the horizon. Measured worst reconstruction error vs exact integration, over altitudes
# 0/500/4000 m: signed-sqrt cosine 7.27x / 2.76x / 1.86x at 32 / 64 / 128 texels; Bruneton's distance ratio
# 2.22x / 1.43x / 1.15x. Bruneton at 64 beats the old mapping at 128.
grep -q 'MultiScatterParameterisation' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  the multi-scatter table is sampled with the transmittance mapping — twilight cannot be expressed"; Fail=1; }
for Source in Engine/Shaders/AtmosphereScattering.slang Engine/DisplayPresentation/AtmosphereModel.h; do
    grep -q 'HorizonSpan' "$Source" \
        || { echo "  $Source lost Bruneton's distance parameterisation"; Fail=1; }
    # ⚠️ Anchored on the distance reconstruction itself, not on the word: a file could name HorizonSpan and
    #    still map by the cosine.
    grep -q 'Rho \* Rho - Distance \* Distance' "$Source" \
        || { echo "  $Source no longer reconstructs the cosine from the boundary distance"; Fail=1; }
done
# The two mappings must remain textually distinct — collapsing them back is the exact regression guarded here.
grep -q 'Uv.x \* 2.0 - 1.0' Engine/Shaders/AtmosphereScattering.slang \
    || { echo "  the multi-scatter mapping lost its full-range sun-zenith span"; Fail=1; }



if [ "$Fail" = "0" ]; then echo "[Atmosphere] OK"; exit 0; else echo "[Atmosphere] FAILED"; exit 1; fi
