//============================================================================================================================================
// 📦 Engine/Shaders/SkyRecords.slang — the sky as the ray-tracing kernel sees it
//============================================================================================================================================
// The Celestial port's GPU seam. Everything here is a transcription of Engine/DisplayPresentation/AtmosphereModel.h
//    and Twilight, and it must stay one: the CPU raster, the headless proofs and this kernel are three consumers
//    of the same model, and the first time they disagree the sky becomes two skies.
//
//    ⚠️ The coefficients are NOT repeated here. They arrive in the uniform block from AtmosphereMedium, because a
//    second copy of 5.8e-6 in a shader is exactly how the GI-on and GI-off skies drift apart — the same failure
//    ColourTransfer.h records for the tone map.
//
// ⚠️ WHY THE KERNEL NEEDS THIS AT ALL. ReSTIRViewport had two branches saying "there is no environment light":
//    a missed primary ray and an escaped bounce ray. With a real atmosphere both are wrong in different ways.
//    The primary is the sky BEHIND the geometry. The bounce is the sky as a LIGHT — it is the largest emitter in
//    an outdoor scene, and it is what makes an unlit face take the colour of the air above it. Round 9 removed
//    the sun/sky system and left bindings 21-24 vacant; this fills the first of them.

#ifndef FRONTIER_SKY_RECORDS
#define FRONTIER_SKY_RECORDS

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE UNIFORM BLOCK
//------------------------------------------------------------------------------------------------------------------------

// 112 bytes, std140. Sized against Vulkan's guaranteed 128-byte push limit deliberately NOT applying here: the
//    push block is already at that limit with five uint reserves left, and the sky needs 108 bytes of medium and
//    control. A uniform buffer is the only place it fits, which is what the binding reservation anticipated.
layout(std140, binding = 21) uniform SkyConstants
{
    vec4 SkySunDirection;    // xyz = unit, toward the sun (CelestialSolver's convention); w = elevation [deg]
    vec4 SkySunRadiance;     // xyz = colour x intensity; w = 0 disables the sky entirely
    vec4 SkyRayleigh;        // xyz = beta_R x strength [1/m]; w = Rayleigh scale height [m]
    vec4 SkyMie;             // x = beta_M x strength [1/m], y = Mie scale height [m], z = anisotropy g, w = unused
    vec4 SkyOzone;           // xyz = beta_O x strength [1/m]; w = unused
    vec4 SkyPlanet;          // x = planet radius [m], y = atmosphere height [m], z = camera height [m], w = unused
    uvec4 SkyControl;        // x = view samples, y = light samples, z = twilight flags, w = unused
    vec4 SkyTwilight;        // x = glow intensity, y = line intensity, z = 1 when the line is civil-only, w = unused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE INTEGRAL
//------------------------------------------------------------------------------------------------------------------------

// Ray-sphere about the planet centre. Returns false on a miss; Near/Far may be negative and the caller clamps.
bool SkyIntersect(vec3 Origin, vec3 Direction, float Radius, out float Near, out float Far)
{
    float B = dot(Origin, Direction);
    float C = dot(Origin, Origin) - Radius * Radius;
    float D = B * B - C;
    if (D < 0.0) { Near = 0.0; Far = 0.0; return false; }
    float S = sqrt(D);
    Near = -B - S;
    Far  = -B + S;
    return true;
}

// Single-scattering sky radiance along a direction, and the transmittance along it.
//
//    Transcribed from AtmosphereModel::Integrate. The quadratic sample spacing (s*s below) is what makes 16
//    samples look like far more: the near half of the ray holds nearly all the density, so uniform spacing
//    wastes most of its samples in near-vacuum.
vec3 SkyRadiance(vec3 Direction, out vec3 OutTransmittance, out bool OutHitGround)
{
    OutTransmittance = vec3(1.0);
    OutHitGround = false;
    if (SkySunRadiance.w <= 0.0) return vec3(0.0);

    float PlanetRadius = SkyPlanet.x;
    float TopRadius    = PlanetRadius + SkyPlanet.y;
    vec3  Origin       = vec3(0.0, 0.0, PlanetRadius + max(SkyPlanet.z, 0.0));

    float Near, Far;
    if (!SkyIntersect(Origin, Direction, TopRadius, Near, Far) || Far < 0.0) return vec3(0.0);

    float Start = max(Near, 0.0);
    float End   = Far;

    // A ray that meets the ground stops there — everything beyond is rock, not air.
    float GroundNear, GroundFar;
    if (SkyIntersect(Origin, Direction, PlanetRadius, GroundNear, GroundFar) && GroundNear > 0.0)
    {
        End = GroundNear;
        OutHitGround = true;
    }
    if (End <= Start) return vec3(0.0);

    vec3  BetaR = SkyRayleigh.xyz;
    float BetaM = SkyMie.x;
    vec3  BetaO = SkyOzone.xyz;
    float Hr = SkyRayleigh.w;
    float Hm = SkyMie.y;
    float G  = SkyMie.z;

    float Mu = dot(Direction, SkySunDirection.xyz);
    const float kPi = 3.14159265358979323846;
    float PhaseR = 3.0 / (16.0 * kPi) * (1.0 + Mu * Mu);
    float Denominator = (2.0 + G * G) * pow(max(1.0 + G * G - 2.0 * G * Mu, 1e-6), 1.5);
    float PhaseM = 3.0 / (8.0 * kPi) * ((1.0 - G * G) * (1.0 + Mu * Mu)) / max(Denominator, 1e-9);

    uint N  = max(SkyControl.x, 1u);
    uint NL = max(SkyControl.y, 1u);
    float Length = End - Start;

    vec3  SumR = vec3(0.0), SumM = vec3(0.0);
    float OpticalR = 0.0, OpticalM = 0.0;

    for (uint I = 0u; I < N; ++I)
    {
        float S0 = float(I)      / float(N);
        float S1 = float(I + 1u) / float(N);
        S0 *= S0; S1 *= S1;                       // quadratic: dense near the camera

        float Ta  = Start + Length * S0;
        float Tb  = Start + Length * S1;
        float Seg = Tb - Ta;
        float Tm  = 0.5 * (Ta + Tb);

        vec3  P = Origin + Direction * Tm;
        float H = length(P) - PlanetRadius;

        float DensityR = exp(-H / Hr) * Seg;
        float DensityM = exp(-H / Hm) * Seg;
        OpticalR += DensityR;
        OpticalM += DensityM;

        float LightNear, LightFar;
        if (!SkyIntersect(P, SkySunDirection.xyz, TopRadius, LightNear, LightFar)) continue;

        float LightR = 0.0, LightM = 0.0;
        bool  Lit = true;
        for (uint J = 0u; J < NL; ++J)
        {
            float Q0 = float(J)      / float(NL);
            float Q1 = float(J + 1u) / float(NL);
            Q0 *= Q0; Q1 *= Q1;
            float SegL = LightFar * (Q1 - Q0);
            float Tq   = LightFar * 0.5 * (Q0 + Q1);
            vec3  Q    = P + SkySunDirection.xyz * Tq;
            float Hq   = length(Q) - PlanetRadius;
            if (Hq < 0.0) { Lit = false; break; }
            LightR += exp(-Hq / Hr) * SegL;
            LightM += exp(-Hq / Hm) * SegL;
        }
        if (!Lit) continue;

        // Mie extinction runs 1.1x its scattering: real aerosols absorb a little as well as scatter.
        vec3 Tau = BetaR * (OpticalR + LightR) + BetaM * 1.1 * (OpticalM + LightM) + BetaO * (OpticalR + LightR);
        vec3 Attenuation = exp(-Tau);
        SumR += Attenuation * DensityR;
        SumM += Attenuation * DensityM;
    }

    OutTransmittance = exp(-(BetaR * OpticalR + BetaM * 1.1 * OpticalM + BetaO * OpticalR));
    return (SumR * BetaR * PhaseR + SumM * BetaM * PhaseM) * SkySunRadiance.xyz;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      TWILIGHT
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ NOT PHYSICAL, and deliberately so — the same note as AtmosphereModel.h carries. The integral above is
//    SINGLE scattering, so once the sun is below the horizon every sample sits in the planet's shadow and the
//    sky goes black. Real twilight is lit by light scattered two or more times over the limb, which single
//    scattering cannot produce at any sample count. This is the art-directed term, added on top.
vec3 SkyTwilightGlow(vec3 Direction)
{
    const float kPi = 3.14159265358979323846;
    float Altitude = degrees(asin(clamp(Direction.z, -1.0, 1.0)));
    if (Altitude < -2.0) return vec3(0.0);
    float AltitudePositive = max(Altitude, 0.0);

    float SunElevation = SkySunDirection.w;
    float Window = smoothstep(-16.0, -5.0, SunElevation) * (1.0 - smoothstep(0.5, 6.0, SunElevation));
    if (Window <= 0.0) return vec3(0.0);

    // Angle between the view ray's horizontal bearing and the sun's, folded to [0, pi].
    float Bearing    = atan(Direction.x, Direction.y);
    float SunBearing = atan(SkySunDirection.x, SkySunDirection.y);
    float AzimuthDelta = abs(Bearing - SunBearing);
    if (AzimuthDelta > kPi) AzimuthDelta = 2.0 * kPi - AzimuthDelta;

    float Az     = exp(-pow(AzimuthDelta / 0.95, 2.0));
    float AzWide = exp(-pow(AzimuthDelta / 1.8, 2.0));
    float Depth  = clamp(-SunElevation / 10.0, 0.0, 1.0);

    vec3 C0 = vec3(1.00, 0.88, 0.62);
    vec3 C1 = vec3(1.00, 0.62, 0.28);
    vec3 C2 = vec3(0.95, 0.42, 0.30);
    vec3 C3 = vec3(0.62, 0.36, 0.48);
    vec3 C4 = vec3(0.25, 0.30, 0.58);

    float U = log2(1.0 + AltitudePositive * 2.0);
    vec3 Colour = mix(C0, C1, smoothstep(0.0, 1.6, U));
    Colour = mix(Colour, C2, smoothstep(1.6, 2.9, U));
    Colour = mix(Colour, C3, smoothstep(2.9, 4.0, U));
    Colour = mix(Colour, C4, smoothstep(4.0, 5.2, U));
    Colour = mix(Colour, mix(C2, C4, 0.6), Depth * 0.6);

    float H    = mix(1.9, 3.8, Depth);
    float Env  = exp(-AltitudePositive / H) * (1.0 - Depth * 0.35);
    float Rim  = exp(-AltitudePositive / 0.45) * (1.0 - Depth);
    float Lobe = Az * 0.85 + AzWide * 0.15;

    vec3 Glow = (Colour * Env * 0.30 + C0 * Rim * 0.25) * Lobe * Window;

    // The white line: a hairline about 0.11 deg wide — a fifth of the solar disc — gated to civil twilight.
    float LineWindow = SkyTwilight.z > 0.5
        ? smoothstep(-5.5, -2.5, SunElevation) * (1.0 - smoothstep(-0.6, 0.3, SunElevation))
        : 1.0;
    float LineAz = exp(-pow(AzimuthDelta / 0.55, 2.0));
    float Line   = exp(-pow(Altitude / 0.11, 2.0)) * (0.7 + 0.3 * smoothstep(-0.4, 0.0, Altitude));
    Glow += vec3(1.0, 0.98, 0.92) * Line * LineAz * LineWindow * SkyTwilight.y * 0.45;

    // The cool dome fill: the earth's shadow, bluest high up and away from the sun.
    float DomeWindow = smoothstep(-16.0, -8.0, SunElevation) * (1.0 - smoothstep(-2.0, 4.0, SunElevation));
    Glow += vec3(0.10, 0.15, 0.30) * 0.035 * DomeWindow * (1.0 - exp(-AltitudePositive / 6.0)) * (1.0 - 0.5 * Az);

    return Glow * SkyTwilight.x;
}

// What an escaped ray sees: the physical sky plus the twilight approximation.
//
//    This is the ONE entry point the kernel calls, so the two miss branches cannot drift into treating the sky
//    differently — a primary ray and a bounce ray must see the same air.
vec3 SkyAlong(vec3 Direction)
{
    if (SkySunRadiance.w <= 0.0) return vec3(0.0);
    vec3 Transmittance;
    bool HitGround;
    vec3 Radiance = SkyRadiance(Direction, Transmittance, HitGround);
    if (!HitGround) Radiance += SkyTwilightGlow(Direction);
    // The moons ride the same transmittance the integral measured, and hide behind the planet like the twilight
    //    does. MoonAlong is declared by MoonRecords.slang, which ReSTIRViewport includes BEFORE this file.
    if (!HitGround) Radiance += MoonAlong(Direction, Transmittance);
    // The sun's body. The integral above is skylight only, so without this the sun is a Mie glow with no
    //    source — the "blurred disc". Transcribed from the reference panel (Diagnostics/CelestialPanel.html on
    //    arena/01a08682-frontier): smoothstep-edged disc, limb-darkened, gated near the horizon, at the panel's
    //    own defaults (0.53 deg diameter, 0.25 softness, 12x disc radiance). Two deliberate differences: the
    //    reddening uses the integral's transmittance already in hand (the panel recomputes extinction from air
    //    mass; the view-ray value is exact for the disc pixel), and the panel's extra analytic sun-glow (sg) is
    //    NOT transcribed — the Mie lobe already provides the forward glow and both would double it. A hidden sun
    //    needs no extra gate: PackSkyRecord zeroes the radiance, which kills the disc while twilight and the
    //    moons live on. Elevation reads Direction.z: the engine is Z-up, the panel Y-up.
    if (!HitGround)
    {
        const float kPi = 3.14159265358979323846;
        float SunAng = acos(clamp(dot(Direction, SkySunDirection.xyz), -1.0, 1.0));
        const float kSunRadius = 0.53 * (kPi / 180.0) * 0.5;
        const float kSunSoft = 0.25;
        const float kSunBoost = 12.0;
        float ViewElev = degrees(asin(clamp(Direction.z, -1.0, 1.0)));
        // The panel evaluates the edge twice and keeps the second (horizon-softened) form; only it is kept here.
        float SunSoftElev = mix(1.0, 2.2, 1.0 - smoothstep(0.0, 4.0, ViewElev));
        float SunDisc = 1.0 - smoothstep(kSunRadius * (1.0 - kSunSoft * 0.9 * SunSoftElev), kSunRadius, SunAng);
        float SunLimb = mix(1.0, 0.55, smoothstep(0.0, kSunRadius, SunAng));
        Radiance += SunDisc * SunLimb * SkySunRadiance.xyz * Transmittance * kSunBoost
                  * mix(0.35, 1.0, smoothstep(-1.0, 8.0, ViewElev));
    }
    return Radiance;
}

#endif // FRONTIER_SKY_RECORDS
