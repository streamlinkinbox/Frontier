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

float CloudShadowHash(float X, float Y, float Z)
{
    float S = sin(X * 127.1 + Y * 311.7 + Z * 74.7) * 43758.5453;
    return S - floor(S);
}

// Trilinear value noise — VolumetricMedia::Noise (the 8-corner lerp, [-1,1] output).
float CloudShadowNoise(float X, float Y, float Z)
{
    float Ix = floor(X), Iy = floor(Y), Iz = floor(Z);
    float Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
    float Ux = Fx * Fx * (3.0 - 2.0 * Fx);
    float Uy = Fy * Fy * (3.0 - 2.0 * Fy);
    float Uz = Fz * Fz * (3.0 - 2.0 * Fz);
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
                         float Anvil, float DriftX, float DriftY, vec3 Position)
{
    float Normalised = (Position.z - Base) / (Top - Base);
    float Profile = CloudShadowProfile(Type, Normalised, Anvil);
    if (Profile <= 0.0) return 0.0;

    float Inverse = 1.0 / max(Scale * 900.0, 1.0);
    float Sx = (Position.x + DriftX) * Inverse;
    float Sy = (Position.y + DriftY) * Inverse;
    float Sz = Position.z * Inverse;

    // Three octaves, not four — the CPU comment records why (the fourth's 38 m features alias into moire).
    float Shape = CloudShadowNoise(Sx, Sy, Sz) * 0.5
                + CloudShadowNoise(Sx * 2.02 + 3.1, Sy * 2.02 + 1.7, Sz * 2.02 + 9.2) * 0.25
                + CloudShadowNoise(Sx * 4.10 + 7.7, Sy * 4.10 + 2.2, Sz * 4.10 + 1.1) * 0.125;
    Shape /= 0.875;
    Shape = Shape * 0.5 + 0.5;

    float Threshold = 1.0 - clamp(Coverage, 0.0, 1.0);
    float Raw = clamp((Shape - Threshold) / max(1.0 - Threshold, 1.0e-3), 0.0, 1.0);
    float Body = smoothstep(0.0, 1.0, Raw);
    return Body * Profile * Density;
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
        OpticalDepth += CloudShadowDensity(Type, Base, Top, PostSpare0.y, PostSpare0.z, SkyPlanet.w,
                                           PostSpare1.z, PostSpare1.x, PostSpare1.y, Q) * Step * 0.01;
        if (OpticalDepth > 4.0) break;
    }
    return exp(-OpticalDepth);
}

#endif // FRONTIER_CLOUD_SHADOW
