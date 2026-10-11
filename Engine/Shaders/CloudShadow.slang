//============================================================================================================================================
// 📦 Engine/Shaders/CloudShadow.slang — the real cloud shadow on the GPU: sun transmittance through the cloud deck
//============================================================================================================================================
// GPU transcription of VolumetricMedia::SunTransmittanceAt (Engine/DisplayPresentation/VolumetricMedia.h): the same
//    8 midpoint taps across the slab interval, the same 3-octave FBM, the same 6 height profiles, the same 0.01
//    extinction per (density·m), the same exp(-OD), the same OD 4.0 early-out. Every literal below matches the CPU
//    line it mirrors (127.1/311.7/74.7/43758.5453 hash, 2.02/4.10 octave gains + offsets, 0.875 normaliser, 1e-3
//    coverage floor, 1e-4 sun gate, 1e-6 slab gate, 1.0e6 interval cap, 900.0 feature divisor). Float32 on both
//    sides; GPU FMA may round the last ulp differently — close, not bit-exact (the CPU split proof documents why
//    even CPU reassociation drifts: the (Time * 0.8) parentheses are load-bearing there too).
//
//    INCLUDE ORDER: this file reads SkyConstants (binding 21, SkyRecords.slang) and PostConstants (binding 24,
//    PostRecords.slang) — include it AFTER both (ReSTIRViewport.slang includes SkyRecords then this file).
//    The 11 staging floats ride the records' spare lanes (packed by PackSkyConstants/PackPostConstants):
//      SkyMie.w = slab base [m], SkyOzone.w = thickness [m], SkyPlanet.w = feature scale [-],
//      SkyTwilight.w = enabled 0/1 (early-out to T = 1 when 0),
//      PostSpare0.y/z/w = coverage [-], density [x], type [0..5],
//      PostSpare1 = drift X/Y [m] (host-folded mid-slab wind x time x 0.8), anvil [0..1], ceiling [m].
//
//    REUSE-SAFE BY CONSTRUCTION: the shadow is a property of the RECEIVER (hitPos), recomputed at the current
//    pixel on every p-hat evaluation — initial candidates and spatial/temporal reuse alike — never stored in
//    the reservoir. A stored transmittance would be the wrong pixel's weather after a merge.

#ifndef FRONTIER_CLOUD_SHADOW
#define FRONTIER_CLOUD_SHADOW

// Vertical profile within the slab — VolumetricMedia::HeightProfile, all six types, same smoothstep edges.
float CloudShadowProfile(uint Type, float Normalised, float Anvil)
{
    float H = clamp(Normalised, 0.0, 1.0);
    if (Type == 0u)      // Stratus
        return smoothstep(0.0, 0.08, H) * (1.0 - smoothstep(0.75, 1.0, H));
    if (Type == 1u)      // Stratocumulus
        return smoothstep(0.0, 0.12, H) * (1.0 - smoothstep(0.50, 0.95, H));
    if (Type == 2u)      // Cumulus
        return smoothstep(0.0, 0.07, H) * (1.0 - smoothstep(0.35, 1.0, H)) * 1.15;
    if (Type == 3u)      // Cumulonimbus
        return smoothstep(0.0, 0.05, H) * (1.0 - smoothstep(0.85, 1.0, H))
             * mix(1.0, 1.6, smoothstep(0.7, 1.0, H) * Anvil);
    if (Type == 4u)      // Altostratus
        return smoothstep(0.0, 0.20, H) * (1.0 - smoothstep(0.55, 0.9, H)) * 0.75;
                         // Cirrus (5) and default
    return smoothstep(0.0, 0.25, H) * (1.0 - smoothstep(0.4, 0.85, H)) * 0.45;
}

// Slab extent after the ceiling — VolumetricMedia::SlabExtent (false when pushed fully above the ceiling).
bool CloudShadowExtent(float Base, float Thickness, float Ceiling, out float OutBase, out float OutTop)
{
    OutBase = clamp(Base, 0.0, Ceiling);
    OutTop  = clamp(Base + Thickness, 0.0, Ceiling);
    return OutTop > OutBase + 1.0;
}

// Where a ray crosses a horizontal slab — VolumetricMedia::SlabInterval (private there, transcribed here).
bool CloudShadowInterval(float OriginZ, float DirectionZ, float Base, float Top, float Maximum,
                         out float Near, out float Far)
{
    if (abs(DirectionZ) < 1.0e-6)
    {
        if (OriginZ < Base || OriginZ > Top) { Near = 0.0; Far = 0.0; return false; }
        Near = 0.0; Far = Maximum;
        return true;
    }
    float T1 = (Base - OriginZ) / DirectionZ;
    float T2 = (Top - OriginZ) / DirectionZ;
    if (T1 > T2) { float Swap = T1; T1 = T2; T2 = Swap; }
    Near = max(T1, 0.0);
    Far  = min(T2, Maximum);
    return Far > Near;
}

// The hash was fract(sin(127.1x + 311.7y + 74.7z) * 43758.5453), the shader-golf classic. ⚠️ Measured before
//    replacing it, because the comment it deserves is the honest one: over a 64³ lattice its neighbour
//    correlation is 0.013 at the origin and 0.014 at offset 3000 — small, not the cause of the blocky sky.
//    What it IS is magnitude-dependent: the argument to sin() grows with the lattice coordinate (127.1 × 3000
//    is ~381 000 radians) and float32 has ~0.03 rad of resolution left there, so the quality of the field
//    depends on where in the world it is sampled. A cloud slab is exactly the case that wanders.
//
//    This is an integer bit-mix instead: no transcendental, no magnitude cliff, the same quality at the origin
//    and at 20 km, and exactly reproducible in C++ and GLSL because it is uint32 arithmetic on both sides
//    (VolumetricMedia::Hash is the same five lines; the shader-mirror gate compares the two fields numerically).
//    Measured after: neighbour correlation 0.0000 / 0.0002 / 0.0047 at offsets 0 / 300 / 3000.
//
//    The blockiness itself came from the three things below — an fbm whose octaves all share one lattice, a
//    C¹ fade, and an unjittered march — not from this function.
float CloudShadowHash(float X, float Y, float Z)
{
    uint Hx = uint(int(X)) * 1597334677u;
    uint Hy = uint(int(Y)) * 3812015801u;
    uint Hz = uint(int(Z)) * 2654435761u;
    uint H  = Hx ^ (Hy + 2654435769u + (Hx << 6) + (Hx >> 2)) ^ (Hz + 374761393u + (Hy << 5) + (Hy >> 3));
    H ^= H >> 15; H *= 2246822519u;
    H ^= H >> 13; H *= 3266489917u;
    H ^= H >> 16;
    return float(H) * 2.3283064365386963e-10;   // 1 / 2^32
}

// Fixed per-octave rotations. Doubling an octave's frequency on the SAME axes keeps every octave's cell walls
//    parallel to the first one's, so the fbm never hides the lattice — it restates it three times. Rotating
//    each octave by an arbitrary fixed angle (37°/29°, −61°/53°) breaks that alignment for the cost of six
//    multiply-adds. The constants are literals so the CPU mirror reproduces them to the bit.
vec3 CloudShadowTurn1(vec3 P)
{
    return vec3( 0.798636 * P.x - 0.601815 * P.y,
                 0.526359 * P.x + 0.698502 * P.y - 0.484810 * P.z,
                 0.291766 * P.x + 0.387186 * P.y + 0.874620 * P.z);
}
vec3 CloudShadowTurn2(vec3 P)
{
    return vec3( 0.484810 * P.x + 0.874620 * P.y,
                -0.526359 * P.x + 0.291766 * P.y - 0.798636 * P.z,
                -0.698502 * P.x + 0.387186 * P.y + 0.601815 * P.z);
}

// Trilinear value noise — VolumetricMedia::Noise (the 8-corner lerp, [-1,1] output). The fade is QUINTIC
//    (6t⁵−15t⁴+10t³, Perlin's own correction), not the cubic smoothstep: a cubic is only C¹, and the second
//    derivative jumping at every cell wall is visible in a marched volume as a faint grid of creases.
float CloudShadowNoise(float X, float Y, float Z)
{
    float Ix = floor(X), Iy = floor(Y), Iz = floor(Z);
    float Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
    float Ux = Fx * Fx * Fx * (Fx * (Fx * 6.0 - 15.0) + 10.0);
    float Uy = Fy * Fy * Fy * (Fy * (Fy * 6.0 - 15.0) + 10.0);
    float Uz = Fz * Fz * Fz * (Fz * (Fz * 6.0 - 15.0) + 10.0);
    float N000 = CloudShadowHash(Ix, Iy, Iz),         N100 = CloudShadowHash(Ix + 1.0, Iy, Iz);
    float N010 = CloudShadowHash(Ix, Iy + 1.0, Iz),     N110 = CloudShadowHash(Ix + 1.0, Iy + 1.0, Iz);
    float N001 = CloudShadowHash(Ix, Iy, Iz + 1.0),     N101 = CloudShadowHash(Ix + 1.0, Iy, Iz + 1.0);
    float N011 = CloudShadowHash(Ix, Iy + 1.0, Iz + 1.0), N111 = CloudShadowHash(Ix + 1.0, Iy + 1.0, Iz + 1.0);
    float X00 = N000 + (N100 - N000) * Ux, X10 = N010 + (N110 - N010) * Ux;
    float X01 = N001 + (N101 - N001) * Ux, X11 = N011 + (N111 - N011) * Ux;
    float Y0 = X00 + (X10 - X00) * Uy,     Y1 = X01 + (X11 - X01) * Uy;
    return (Y0 + (Y1 - Y0) * Uz) * 2.0 - 1.0;
}

// Density at a slab point with the drift passed in — VolumetricMedia::CloudDensityCore. The caller guarantees
//    Position.z is inside the slab (the march below only taps the slab interval, same contract as the CPU).
float CloudShadowDensity(uint Type, float Base, float Top, float Coverage, float Density, float Scale,
                         float Anvil, float DriftX, float DriftY, float Evolve, vec3 Position)
{
    float Normalised = (Position.z - Base) / (Top - Base);
    float Profile = CloudShadowProfile(Type, Normalised, Anvil);
    if (Profile <= 0.0) return 0.0;

    float Inverse = 1.0 / max(Scale * 900.0, 1.0);
    vec3  S = vec3((Position.x + DriftX) * Inverse, (Position.y + DriftY) * Inverse, Position.z * Inverse);

    // Three octaves, each ROTATED off the previous one's axes, and each walked through the noise at its own
    //    rate by `Evolve` (seconds × an evolution rate). Drift slides the whole field; Evolve makes it BOIL,
    //    which is the difference between wallpaper going past and weather.
    vec3 O1 = CloudShadowTurn1(S) * 2.02 + vec3(3.1, 1.7, 9.2 + Evolve * 1.7);
    vec3 O2 = CloudShadowTurn2(S) * 4.10 + vec3(7.7, 2.2, 1.1 + Evolve * 2.6);
    float Shape = CloudShadowNoise(S.x, S.y, S.z + Evolve) * 0.5
                + CloudShadowNoise(O1.x, O1.y, O1.z) * 0.25
                + CloudShadowNoise(O2.x, O2.y, O2.z) * 0.125;
    Shape /= 0.875;
    Shape = Shape * 0.5 + 0.5;

    float Threshold = 1.0 - clamp(Coverage, 0.0, 1.0);
    float Raw = clamp((Shape - Threshold) / max(1.0 - Threshold, 1.0e-3), 0.0, 1.0);

    // Edge erosion. A fourth octave added to the SHAPE aliases (the CPU comment recorded the measurement: the
    //    march cannot resolve 38 m features and they smear into moire). Subtracting it at the EDGE instead —
    //    weighted by (1 − Raw), so it is strongest in the wisps and vanishes in the opaque core — buys the
    //    cauliflower silhouette without putting any high frequency where the ray marches through.
    vec3 D = CloudShadowTurn1(S) * 8.30 + vec3(11.3, 4.9, 2.7 + Evolve * 4.1);
    float Detail = CloudShadowNoise(D.x, D.y, D.z) * 0.5 + 0.5;
    Raw = clamp(Raw - (1.0 - Raw) * Detail * 0.35, 0.0, 1.0);

    float Body = smoothstep(0.0, 1.0, Raw);
    return Body * Profile * Density;
}

// One rate, shared by the shader and the CPU mirror, so a cloud evolves at the same speed in the render, in
//    the shadow and in the inspector's preview. Units are noise cells per second: a still day turns a cell
//    over in ~100 s, a 7 m/s wind in ~50 s. Wind moves the field; wind also stirs it.
float CloudEvolutionRate(float WindSpeed)
{
    return 0.010 + 0.0015 * max(WindSpeed, 0.0);
}

// Sun transmittance at a SURFACE point — VolumetricMedia::SunTransmittanceAt. World frame, Z-up, same as the
//    CPU query (HitLocation/SunWorld); no frame conversion on either side. Returns 1 when disabled, gated by
//    the same sun-elevation epsilon, so night pixels pay one compare.
float CloudShadowTransmittance(vec3 WorldPos, vec3 SunDirection)
{
    if (SkyTwilight.w < 0.5) return 1.0;
    if (SunDirection.z <= 1.0e-4) return 1.0;
    float Base = 0.0, Top = 0.0;
    if (!CloudShadowExtent(SkyMie.w, SkyOzone.w, PostSpare1.w, Base, Top)) return 1.0;
    float Near = 0.0, Far = 0.0;
    if (!CloudShadowInterval(WorldPos.z, SunDirection.z, Base, Top, 1.0e6, Near, Far)) return 1.0;
    uint Type = uint(PostSpare0.w + 0.5);
    if (Type > 5u) Type = 2u;
    const uint Taps = 8u;
    float Step = (Far - Near) / float(Taps);
    float OpticalDepth = 0.0;
    for (uint I = 0u; I < Taps; ++I)
    {
        float T = Near + (float(I) + 0.5) * Step;
        vec3 Q = WorldPos + SunDirection * T;
        // Evolve = 0: the ground shadow is staged from a FROZEN time (ShadowTimeSeconds, a slider, not a
        //    clock) and its drift is folded on the CPU from that same frozen value. Boiling the field here
        //    while the drift stands still would be the worse inconsistency. Carrying the staged time into
        //    the post record needs a spare float it does not have — noted in the backlog.
        OpticalDepth += CloudShadowDensity(Type, Base, Top, PostSpare0.y, PostSpare0.z, SkyPlanet.w,
                                           PostSpare1.z, PostSpare1.x, PostSpare1.y, 0.0, Q) * Step * 0.01;
        if (OpticalDepth > 4.0) break;
    }
    return exp(-OpticalDepth);
}

#endif // FRONTIER_CLOUD_SHADOW
