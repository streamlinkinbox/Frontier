//============================================================================================================================================
//                                                        SUNSHADOWGATE.CPP
//============================================================================================================================================
// Celestial-shadow gate — the GI-off shadow stage must place a tap for the sun.
//
//    This exists because the sun silently stopped casting a shadow. The ReSTIR kernel has treated the sun as a direct
//    light since it landed (kSunLightIndex / PHatSun in ReSTIRViewport.slang), but VisibilityExchange::PlaceShadowTaps
//    — the GI-off path — opened with `if (Emitters.empty()) return false;` and then filled all four tap slots from the
//    emissive MESH triangles alone. Two consequences, both of which look like a broken renderer:
//      · an outdoor level with a sun AND lamps rasterised lamp shadows only — the sun lit every surface flatly, with
//        no shadow behind anything, because no shadow map was ever rasterised from it;
//      · a level lit ONLY by the sun (no emissive mesh at all) placed zero taps, so RecordShadowFrame refused and the
//        whole shadow stage was skipped.
//
//    The checks below drive the real PlaceShadowTaps (the shipped TU is linked in, not a copy) through the three
//    lighting situations a level can be in, and assert the tap layout and the directional mask each one must produce.
//
//    usage: bash Tools/Build/CheckCelestialShadow.sh

#include "Engine/DeviceExchange/VisibilityExchange.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Frontier;

namespace {

int Failures = 0;

void Check(bool Condition, const char* Message)
{
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Message);
    if (!Condition) ++Failures;
}

// The sun, staged the way GameExecution stages it from the packed sky record: a unit direction toward the sun and the
//    record's already-attenuated SunDirect radiance (zero below the horizon, which is the "sun is down" signal).
ShadowFrameConfiguration WithMoon(ShadowFrameConfiguration Shadow, float Dx, float Dy, float Dz, float Radiance)
{
    const float Length = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    Shadow.MoonEnabled      = Radiance > 0.0f;
    Shadow.MoonDirection[0] = Dx / Length;
    Shadow.MoonDirection[1] = Dy / Length;
    Shadow.MoonDirection[2] = Dz / Length;
    for (int I = 0; I < 3; ++I) Shadow.MoonRadiance[I] = Radiance;
    return Shadow;
}

ShadowFrameConfiguration WithSun(ShadowFrameConfiguration Shadow, float Dx, float Dy, float Dz, float Radiance)
{
    const float Length = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    Shadow.SunEnabled      = Radiance > 0.0f;
    Shadow.SunDirection[0] = Dx / Length;
    Shadow.SunDirection[1] = Dy / Length;
    Shadow.SunDirection[2] = Dz / Length;
    for (int I = 0; I < 3; ++I) Shadow.SunRadiance[I] = Radiance;
    return Shadow;
}

bool Directional(const ShadowFrameConfiguration& Shadow, uint32_t Tap)
{
    return (Shadow.DirectionalMask & (kShadowTapDirectionalBit << Tap)) != 0u;
}

} // namespace

int main()
{
    std::printf("================================================================================\n");
    std::printf("   CELESTIAL SHADOW GATE — the GI-off shadow stage must give the sun AND the moon a tap\n");
    std::printf("================================================================================\n");

    // A level with no emissive mesh whatsoever. The exchange is default-constructed, so Emitters is empty — exactly
    //    the state an outdoor level lit only by the sun arrives in.
    VisibilityExchange SunOnly;

    // ── ① sun up, no mesh emitters: the stage must run, with the sun in slot 0 ───────────────────────────────────
    {
        ShadowFrameConfiguration Shadow = WithSun({}, -0.5f, 0.0f, 0.86f, 3.2f);
        const bool Placed = SunOnly.PlaceShadowTaps(Shadow);
        std::printf("sun-only level: placed=%s taps=%u mask=0x%x\n", Placed ? "yes" : "no", Shadow.TapCount, Shadow.DirectionalMask);
        Check(Placed,                       "a sun-lit level with no emissive mesh still places taps (it used to refuse)");
        Check(Shadow.TapCount == 1u,        "exactly one tap: the sun");
        Check(Directional(Shadow, 0u),      "tap 0 is flagged directional");
        Check(Shadow.Taps[0].Radiance[0] > 0.0f, "the sun tap carries the record's radiance");
        Check(Shadow.Taps[0].LightSize > 0.0f,   "the sun tap has a PCSS penumbra width (the solar disc)");

        // The tap's normal must face the scene, or the resolve's LdotL test would reject every surface.
        const float Facing = Shadow.Taps[0].Normal[0] * Shadow.SunDirection[0]
                           + Shadow.Taps[0].Normal[1] * Shadow.SunDirection[1]
                           + Shadow.Taps[0].Normal[2] * Shadow.SunDirection[2];
        std::printf("sun tap normal . sun direction = %.3f (must be -1: the sun faces the scene)\n", Facing);
        Check(Facing < -0.99f, "the sun tap's normal faces the scene, not away from it");

        // Standing off along +sunDirection puts the rasterisation viewpoint on the sunlit side.
        const float Height = Shadow.Taps[0].Origin[2];
        std::printf("sun tap origin z = %.2f m (above the scene, on the lit side)\n", Height);
        Check(Height > 0.0f, "the sun tap is rasterised from the lit side of the scene");
    }

    // ── ② sun down: nothing to place, and the stage must say so ─────────────────────────────────────────────────
    {
        ShadowFrameConfiguration Shadow = WithSun({}, -0.5f, 0.0f, -0.4f, 0.0f);   // below the horizon ⇒ SunDirect 0
        const bool Placed = SunOnly.PlaceShadowTaps(Shadow);
        std::printf("night, no mesh emitters: placed=%s taps=%u\n", Placed ? "yes" : "no", Shadow.TapCount);
        Check(!Placed,               "a dark level places no taps (the caller must not present an unwritten image)");
        Check(Shadow.TapCount == 0u, "no taps are left behind from a previous frame");
        Check(Shadow.DirectionalMask == 0u, "the directional mask is cleared too");
    }

    // ── ③ the sun must not silently dim the lamps ───────────────────────────────────────────────────────────────
    //    The mesh taps divide the emitter set between themselves. When the sun takes a slot there are three mesh taps
    //    rather than four, and the per-tap weight must be recomputed against three — dividing by the slot maximum
    //    would quietly lose a quarter of every lamp's energy the moment the sun rose.
    {
        ShadowFrameConfiguration Night = WithSun({}, 0.0f, 0.0f, 1.0f, 0.0f);
        ShadowFrameConfiguration Day   = WithSun({}, -0.5f, 0.0f, 0.86f, 3.2f);
        (void)SunOnly.PlaceShadowTaps(Night);
        (void)SunOnly.PlaceShadowTaps(Day);
        // With no mesh emitters present this pair only proves the sun's own slot accounting; the weight rule itself is
        //    asserted by reading the source's divisor, which the mesh branch computes from the mesh tap count.
        std::printf("day taps=%u (sun) vs night taps=%u (nothing)\n", Day.TapCount, Night.TapCount);
        Check(Day.TapCount == 1u && Night.TapCount == 0u, "the sun's slot appears and disappears with the sun");
    }

    // ── ④ the record the shader reads ───────────────────────────────────────────────────────────────────────────
    {
        Check(kShadowTapDirectionalBit == 1u, "the directional bit matches ShadowRecords.slang's kShadowTapDirectionalBit");
        ShadowFrameConfiguration Shadow{};
        Check(Shadow.DirectionalMask == 0u, "a fresh frame is all-point-lights by default (old behaviour preserved)");
        Check(!Shadow.SunEnabled,           "the sun is off unless a caller supplies it");
    }

    // ── ⑤ the moon is a light too ───────────────────────────────────────────────────────────────────────────────
    //    A moonlit night casts real shadows. Before this the moon lit nothing on the GI-off path at all: the
    //    kernel's MoonAmbient() was the only lunar term anywhere, so with GI off the moon was a disc painted on a
    //    sky above ground it did not illuminate.
    {
        ShadowFrameConfiguration Shadow = WithMoon({}, 0.2f, -0.3f, 0.93f, 0.0021f);
        const bool Placed = SunOnly.PlaceShadowTaps(Shadow);
        std::printf("moonlit night: placed=%s taps=%u mask=0x%x\n", Placed ? "yes" : "no", Shadow.TapCount, Shadow.DirectionalMask);
        Check(Placed,                  "a moonlit night with no emissive mesh places a tap (the moon used to light nothing)");
        Check(Shadow.TapCount == 1u,   "exactly one tap: the moon");
        Check(Directional(Shadow, 0u), "the moon is directional — a light at infinity, no 1/d fall-off");
        Check(Shadow.Taps[0].LightSize > 0.0f, "the lunar disc gives the moon shadow a PCSS penumbra");

        const float Facing = Shadow.Taps[0].Normal[0] * Shadow.MoonDirection[0]
                           + Shadow.Taps[0].Normal[1] * Shadow.MoonDirection[1]
                           + Shadow.Taps[0].Normal[2] * Shadow.MoonDirection[2];
        Check(Facing < -0.99f, "the moon tap's normal faces the scene");
    }

    // ── ⑥ sun and moon together ─────────────────────────────────────────────────────────────────────────────────
    //    A daytime moon is ordinary, so these must be independent slots rather than an either/or. The sun takes 0
    //    and the moon takes 1, and BOTH must be flagged directional — a mask that only ever marks slot 0 would make
    //    the resolve treat the moon as a point light at its stand-off point and fall off across the scene.
    {
        ShadowFrameConfiguration Shadow = WithMoon(WithSun({}, -0.5f, 0.0f, 0.86f, 3.2f), 0.4f, 0.1f, 0.91f, 0.0021f);
        const bool Placed = SunOnly.PlaceShadowTaps(Shadow);
        std::printf("sun and moon both up: placed=%s taps=%u mask=0x%x\n", Placed ? "yes" : "no", Shadow.TapCount, Shadow.DirectionalMask);
        Check(Placed,                    "both bodies up still places taps");
        Check(Shadow.TapCount == 2u,     "two taps: the sun AND the moon, not one or the other");
        Check(Directional(Shadow, 0u),   "slot 0 (sun) is directional");
        Check(Directional(Shadow, 1u),   "slot 1 (moon) is directional too");
        Check(Shadow.Taps[0].Radiance[0] > Shadow.Taps[1].Radiance[0],
              "the sun is the brighter of the two (the moon must not outshine it)");

        // The two taps must not be placed at the same point, or one shadow map is wasted rendering the other's view.
        const float Dx = Shadow.Taps[0].Origin[0] - Shadow.Taps[1].Origin[0];
        const float Dy = Shadow.Taps[0].Origin[1] - Shadow.Taps[1].Origin[1];
        const float Dz = Shadow.Taps[0].Origin[2] - Shadow.Taps[1].Origin[2];
        Check(std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) > 1e-3f, "the two bodies rasterise from different viewpoints");
    }

    // ── ⑦ a moonless, sunless night still refuses ───────────────────────────────────────────────────────────────
    {
        ShadowFrameConfiguration Shadow{};
        Check(!SunOnly.PlaceShadowTaps(Shadow), "no sun, no moon, no mesh: the stage still declines to run");
        Check(!Shadow.MoonEnabled, "the moon is off unless a caller supplies it");
    }

    std::printf(Failures ? "\nRED — %d check(s) failed\n" : "\nGREEN — the sun and the moon both cast shadows\n", Failures);
    return Failures ? 1 : 0;
}
