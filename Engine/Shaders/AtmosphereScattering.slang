//============================================================================================================================================
//                                                    ATMOSPHERESCATTERING.SLANG
//============================================================================================================================================
// 🧩 Analytic single-scattering sky, after Hillaire 2020's atmosphere parameterisation.
//
//    A3 — FIRST LIGHT. This is the ray-miss path: a camera ray that hits nothing used to return pure black, and
//    now returns the radiance of the sky in that direction. The oculus stops being a hole into nothing.
//
//    🔴 Single scattering only, ray-marched per pixel. The LUT chain (transmittance, multi-scattering, sky-view,
//    aerial perspective) is A2 and is NOT here. That ordering is deliberate: this establishes the ray-miss
//    plumbing and the physical parameterisation against something visible, and the LUTs then replace the march
//    with a texture fetch WITHOUT changing the model. If the LUTs had come first there would have been two
//    unverified layers stacked with nothing to compare against.
//
//    What single scattering costs, so nobody mistakes it for a bug later:
//      · a slightly dark sky away from the sun, because light scattered twice never arrives;
//      · a night that is properly black rather than faintly blue;
//      · no earth-shine and no glow on the underside of the horizon.
//    All three are fixed by the multi-scattering LUT in A2, and none of them make the sunset wrong.
//
//    Everything below is in METRES and works at planetary scale in floats. The camera sits at
//    PlanetRadius + CameraAltitude, so a ray from the ground toward the horizon travels ~300 km — which is why
//    every intersection is written in the numerically stable form rather than the textbook one.

#ifndef ATMOSPHERE_SCATTERING_SLANG
#define ATMOSPHERE_SCATTERING_SLANG

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARAMETERISATION
//------------------------------------------------------------------------------------------------------------------------
// Earth's values. These are the same set the previous project used and the same set Hillaire's paper states, so
//    the sky can be checked against published figures rather than against taste.

const float kPlanetRadius        = 6360000.0;    // [m]   to the surface
const float kAtmosphereThickness =  100000.0;    // [m]   above it
const float kAtmosphereRadius    = kPlanetRadius + kAtmosphereThickness;

const float kRayleighScaleHeight = 8000.0;       // [m]   density falls by e over this height
const float kMieScaleHeight      = 1200.0;       // [m]   aerosols hug the ground far more closely

// Rayleigh scattering at sea level, per metre, per channel. The λ⁻⁴ dependence is why the sky is blue and why
//    the sun reddens at a grazing angle: blue is removed from the direct beam first.
const vec3  kRayleighScattering  = vec3(5.802e-6, 13.558e-6, 33.1e-6);

// Mie is wavelength-neutral — aerosols are large compared with visible light — which is why haze is grey/white.
//    These two are the CLEAR reference atmosphere, i.e. turbidity 1. Everything that scatters off aerosols reads
//    them through MieScattering()/MieExtinction() below rather than directly.
const float kMieScattering       = 3.996e-6;     // [1/m]  at turbidity 1
const float kMieExtinction       = 4.440e-6;     // [1/m]  at turbidity 1; scattering plus absorption
const float kMieAsymmetry        = 0.80;         // [-]   forward-biased; this is the glow AROUND the sun

//------------------------------------------------------------------------------------------------------------------------
//                                                       TURBIDITY
//------------------------------------------------------------------------------------------------------------------------
// 🧩 The aerosol load, as a multiple of the clear reference above. 1 is clean maritime air; 2.5 is a hazy
//    afternoon over a city. It scales Mie only — Rayleigh is the air molecules themselves and does not care how
//    dusty the day is, which is why a hazy sky whitens near the horizon while the zenith barely changes.
//
//    🔴 WHY THIS EXISTS. A clear sunrise and a clear sunset at the same sun elevation are, in this model,
//    IDENTICAL: scattering depends only on the geometry, and the geometry is symmetric about the horizon. That
//    is physically right and visually wrong, because real dawns look different from real dusks — paler, whiter,
//    the glow tighter to the horizon. The difference is not in the light path, it is in the air: overnight the
//    boundary layer cools, convection stops and aerosols settle out, so dawn air is CLEANER. By late afternoon a
//    day of heating has stirred dust and haze back up. So the parameter that must differ between them is the
//    aerosol load, not anything about the sun.
//
//    ⚠️ A GLOBAL, not an argument. Threading a float through SkyRadiance, SunDisc, OpticalDepth,
//    SunTransmittance, ComputeTransmittanceTexel and ComputeMultiScatterTexel is six signatures and six call
//    sites, and the one that gets missed is silent: the tables would keep describing a different atmosphere from
//    the march that samples them, which reads as a horizon that does not match the sky above it.
//
//    Each entry point assigns it ONCE at the top of main from its own uniform, so the whole invocation — sky,
//    sun disc, and both table builders — sees the same air.
float gAerosolTurbidity = 1.0;

float MieScattering() { return kMieScattering * gAerosolTurbidity; }
float MieExtinction() { return kMieExtinction * gAerosolTurbidity; }

// Ozone. It scatters nothing and only absorbs, but it is what keeps a twilight sky blue instead of turning it
//    muddy brown once the direct sunlight has been reddened away.
const vec3  kOzoneAbsorption     = vec3(0.650e-6, 1.881e-6, 0.085e-6);
const float kOzoneCentre         = 25000.0;      // [m]   the tent's peak
const float kOzoneWidth          = 15000.0;      // [m]   half-width; density falls to zero this far from centre

const float kSunAngularRadius    = 0.004675;     // [rad] 0.268° — the disc, not a point

// 🔴 THE PLANET HAS A SURFACE. Without this the model has air and nothing under it, so any ray that passes
//    below the horizon returns only the radiance of the short air path in front of it — which at eye height is
//    almost nothing. Measured before this existed: looking 0.2° below horizontal dropped from 7473 to 74 cd/m²
//    and to 14 by 1° down, a hundredfold cliff into a black void exactly at the horizon line.
//
//    That void is not a cosmetic problem. In an outdoor scene the ground plate ends a few hundred metres out,
//    and past its edge the void fills the bottom HALF of the frame at eye level. Measured, that dragged the
//    metered scene luminance from 5752 to 67 cd/m² and the exposure rose 86×, so everything lit blew to white —
//    and it corrected itself when the camera climbed high enough for the plate to cover the void, which is
//    precisely the reported "it looks right up close, and the scene gets brighter as I move away".
//
//    0.10 is Earth's average visible albedo over land and ocean. It is deliberately not per-scene: this is the
//    PLANET, seen out to the horizon, not the ground the camera is standing on.
const float kGroundAlbedo        = 0.10;         // [-]

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE GROUND (A7c)
//------------------------------------------------------------------------------------------------------------------------

// Lambertian, so the radiance leaving the surface is albedo·E/π and does not depend on where it is viewed from.
//    SunSurvival is the fraction of the direct beam reaching this point, and SkyIrradiance is what the rest of
//    the sky puts on it — the second term is what keeps the ground from going black the instant the sun sets.
vec3 GroundReflection(vec3 Normal, vec3 SunDirection, vec3 SunIlluminance, vec3 SunSurvival, vec3 SkyIrradiance)
{
    const float CosSun = max(dot(Normal, SunDirection), 0.0);
    return kGroundAlbedo * (SunIlluminance * SunSurvival * CosSun + SkyIrradiance) / 3.14159265;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DENSITY AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

// Densities relative to sea level: x Rayleigh, y Mie, z ozone.
vec3 AtmosphereDensity(float Altitude)
{
    const float Rayleigh = exp(-max(Altitude, 0.0) / kRayleighScaleHeight);
    const float Mie      = exp(-max(Altitude, 0.0) / kMieScaleHeight);
    // A linear tent, as Hillaire uses. Ozone is a layer, not an exponential fall-off, so an exponential here
    //    would put absorption at the ground where there is almost none.
    const float Ozone    = max(0.0, 1.0 - abs(Altitude - kOzoneCentre) / kOzoneWidth);
    return vec3(Rayleigh, Mie, Ozone);
}

// Distance to a sphere of the given radius, or −1 for a miss. Written in the numerically stable form: the
//    textbook (-b ± √d)/2a catastrophically cancels when the ray starts near the surface of a 6360 km sphere,
//    which is the normal case here, and the error shows as a shimmering horizon.
float RaySphereNearest(vec3 Origin, vec3 Direction, float Radius)
{
    const float b = dot(Origin, Direction);
    const float c = dot(Origin, Origin) - Radius * Radius;
    const float Discriminant = b * b - c;
    if (Discriminant < 0.0) return -1.0;
    const float Root = sqrt(Discriminant);
    const float Near = -b - Root;
    const float Far  = -b + Root;
    if (Far < 0.0) return -1.0;              // sphere entirely behind
    return Near >= 0.0 ? Near : Far;         // inside the sphere: the exit is the useful root
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHASE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Rayleigh: gently forward/backward symmetric. This is the broad brightening of the whole sky.
float RayleighPhase(float CosAngle)
{
    return 3.0 / (16.0 * 3.14159265) * (1.0 + CosAngle * CosAngle);
}

// Cornette–Shanks rather than raw Henyey–Greenstein: it stays finite and correctly shaped at g → 0, and it is
//    what produces the tight bright halo hugging the sun.
float MiePhase(float CosAngle, float g)
{
    const float g2 = g * g;
    const float Numerator   = 3.0 * (1.0 - g2) * (1.0 + CosAngle * CosAngle);
    const float Denominator = 8.0 * 3.14159265 * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * CosAngle, 1.5);
    return Numerator / max(Denominator, 1e-9);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TRANSMITTANCE
//------------------------------------------------------------------------------------------------------------------------

// Optical depth along a ray, integrated by marching. A2 replaces this with the transmittance LUT — this is the
//    expensive part, and the reason the LUT exists at all.
vec3 OpticalDepth(vec3 Origin, vec3 Direction, float Distance, int Steps)
{
    const float Step = Distance / float(Steps);
    vec3 Sum = vec3(0.0);
    for (int i = 0; i < Steps; ++i)
    {
        const vec3  Position = Origin + Direction * (float(i) + 0.5) * Step;
        const float Altitude = length(Position) - kPlanetRadius;
        const vec3  Density  = AtmosphereDensity(Altitude);
        Sum += (kRayleighScattering * Density.x
              + vec3(MieExtinction()) * Density.y
              + kOzoneAbsorption    * Density.z) * Step;
    }
    return Sum;
}

// Fraction of sunlight surviving from a point to the top of the atmosphere. A point in the planet's shadow gets
//    zero — without that test the ground glows at midnight because the sun is still "visible" through the rock.
vec3 SunTransmittance(vec3 Position, vec3 SunDirection, int Steps)
{
    if (RaySphereNearest(Position, SunDirection, kPlanetRadius) > 0.0) return vec3(0.0);
    const float ToSpace = RaySphereNearest(Position, SunDirection, kAtmosphereRadius);
    if (ToSpace < 0.0) return vec3(1.0);
    return exp(-OpticalDepth(Position, SunDirection, ToSpace, Steps));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SKY RADIANCE
//------------------------------------------------------------------------------------------------------------------------

// Radiance arriving along ViewDirection from an observer at CameraAltitude above the surface.
//    SunIlluminance is the illuminance of the direct beam outside the atmosphere.
//
//    Quality tiers select the two step counts. The view march dominates the cost — the light march runs inside
//    it — so the tiers spend their budget mostly on the view.
vec3 SkyRadiance(float CameraAltitude, vec3 ViewDirection, vec3 SunDirection,
                 vec3 SunIlluminance, int ViewSteps, int LightSteps)
{
    const vec3 Origin = vec3(0.0, 0.0, kPlanetRadius + max(CameraAltitude, 1.0));

    // How far the ray travels before it leaves the air — or hits the ground, which is nearer and stops it.
    float Distance = RaySphereNearest(Origin, ViewDirection, kAtmosphereRadius);
    if (Distance < 0.0) return vec3(0.0);                       // outside the atmosphere looking away
    const float Ground = RaySphereNearest(Origin, ViewDirection, kPlanetRadius);
    if (Ground > 0.0) Distance = min(Distance, Ground);

    const float CosTheta      = dot(ViewDirection, SunDirection);
    const float PhaseRayleigh = RayleighPhase(CosTheta);
    const float PhaseMie      = MiePhase(CosTheta, kMieAsymmetry);

    const float Step = Distance / float(ViewSteps);
    vec3 Radiance     = vec3(0.0);
    vec3 Transmittance = vec3(1.0);   // camera → current sample, carried forward rather than re-integrated

    // A7c. If the ray ends on the planet rather than in space, what it sees there is the lit ground, dimmed by
    //    the air in front of it. Added AFTER the march below, using the transmittance the march accumulates.
    const bool HitsGround = Ground > 0.0;

    for (int i = 0; i < ViewSteps; ++i)
    {
        const vec3  Position = Origin + ViewDirection * (float(i) + 0.5) * Step;
        const float Altitude = length(Position) - kPlanetRadius;
        const vec3  Density  = AtmosphereDensity(Altitude);

        const vec3 Extinction = kRayleighScattering * Density.x
                              + vec3(MieExtinction()) * Density.y
                              + kOzoneAbsorption    * Density.z;
        const vec3 StepTransmittance = exp(-Extinction * Step);

        const vec3 ScatteringHere = kRayleighScattering * Density.x * PhaseRayleigh
                                  + vec3(MieScattering()) * Density.y * PhaseMie;

        const vec3 SunArriving = SunTransmittance(Position, SunDirection, LightSteps);

        // Analytic integration of the scattering across the step rather than a midpoint sample. With few steps
        //    the midpoint form visibly banks the horizon into bands; this costs one extra divide and removes them.
        const vec3 Integrated = (ScatteringHere - ScatteringHere * StepTransmittance) / max(Extinction, vec3(1e-9));

        Radiance      += Transmittance * SunArriving * Integrated * SunIlluminance;
        Transmittance *= StepTransmittance;
    }

    if (HitsGround)
    {
        const vec3 Surface = Origin + ViewDirection * Distance;
        const vec3 Normal  = normalize(Surface);
        // ⚠️ Single scattering has no sky-irradiance term to offer — that light is precisely what it cannot
        //    represent — so this path lights the ground with the direct beam alone. The tabulated path in
        //    ReSTIRViewport adds the multiple-scattering term, which is why it is the one the renderer uses.
        Radiance += Transmittance * GroundReflection(Normal, SunDirection, SunIlluminance,
                                                     SunTransmittance(Surface, SunDirection, LightSteps),
                                                     vec3(0.0));
    }

    return Radiance;
}

// The solar disc itself, for a ray that looks straight at it. Limb darkening is deliberately omitted: at 0.268°
//    it is well under a pixel of variation, and the disc is almost always clipped by exposure anyway.
vec3 SunDisc(float CameraAltitude, vec3 ViewDirection, vec3 SunDirection, vec3 SunIlluminance, int LightSteps)
{
    const float CosAngle = dot(ViewDirection, SunDirection);
    if (CosAngle < cos(kSunAngularRadius)) return vec3(0.0);

    const vec3 Origin = vec3(0.0, 0.0, kPlanetRadius + max(CameraAltitude, 1.0));
    // Divided by the solid angle so this is a RADIANCE, matching the sky it is added to. Adding an illuminance
    //    directly would make the disc's brightness depend on the render resolution.
    const float SolidAngle = 6.28318530 * (1.0 - cos(kSunAngularRadius));
    return SunTransmittance(Origin, SunDirection, LightSteps) * SunIlluminance / SolidAngle;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOOKUP TABLES (A2)
//------------------------------------------------------------------------------------------------------------------------
// Two tables, both CONSTANT for a given atmosphere, so they are built once and only rebuilt when a parameter
//    changes. They replace the two costs the A3 march pays per pixel:
//
//      Transmittance   — replaces SunTransmittance()'s inner march. That march runs LightSteps times inside
//                        every one of ViewSteps samples, so removing it takes the per-pixel cost from a product
//                        to a sum. This is where nearly all of A2's speed comes from.
//      Multi-scattering— adds what single scattering physically cannot: light that bounced more than once.
//                        Without it the sky away from the sun is too dark and twilight is black rather than blue.

const uint kTransmittanceWidth  = 256u;   // [px] along cos(sun zenith)
const uint kTransmittanceHeight = 64u;    // [px] along altitude
const uint kMultiScatterSize    = 64u;    // [px] square, in the same two parameters (A7f: was 32)

// ── Parameterisation ────────────────────────────────────────────────────────────────────────────────────────
// u ← cos(sun zenith), v ← altitude. Both are stored with a SQUARE-ROOT distribution in altitude: density falls
//    exponentially with height, so a linear table spends most of its rows on thin air where nothing changes and
//    starves the first kilometre where everything does.

// 🔴 u is the SIGNED SQUARE ROOT of the sun's cosine, not the cosine itself. Linear-in-cosine spends its texels
//    where nothing happens and starves the one place everything does: at the horizon, one texel of 32 spanned
//    3.58° of sun elevation, so the whole of civil twilight — every sunrise and sunset there is — fell inside
//    two of them. Measured against the exact integral, that cost up to 2.71× error in the multiple-scattering
//    term, and multiple scattering is precisely what makes a dawn horizon pale rather than blood red: it takes
//    the horizon from R/B 238 down to 5.4, so an error there lands straight on the colour.
//
//    The square root crowds texels toward cos = 0. The same 32 texels then span 0.224° at the horizon instead
//    of 3.58°, and the error falls to 1.72×; at 64 it is 1.13×. Chosen by sweeping the exponent rather than
//    picked: 0.4 is marginally better still in twilight but starts to starve the daytime sky, and 1.0 is what
//    was there.
//
//    ⚠️ Whatever this becomes, the build and the sample must move together. The tables are FILLED through the
//    inverse and READ through the forward, so a mismatch does not fail loudly — it silently describes an
//    atmosphere lit from a different sun angle than the one being asked about.
// 🔴 Transmittance and multiple scattering used to share ONE parameterisation and must not: transmittance is
//    only ever wanted for a ray that CLEARS the horizon (below it the ray meets the ground and the answer is
//    zero), while multiple scattering is wanted precisely when the sun is BELOW the horizon, because that is
//    what lights twilight. Sharing meant half the transmittance table stored zero and bilinear filtering
//    interpolated across the discontinuity at the horizon.
//
//    Transmittance now uses Bruneton & Neyret's mapping, by DISTANCE to the atmosphere boundary rather than by
//    a power of the cosine: near the horizon a 100 km change of path moves the cosine by 0.016, too little to
//    resolve, but moves the distance ratio by 0.11. Measured worst reconstruction error vs exact integration:
//
//        size |  signed-sqrt cosine  |  Bruneton distance ratio
//          32 |        7.27x         |          2.22x
//          64 |        2.76x         |          1.43x
//         128 |        1.86x         |          1.15x
//
//    ⚠️ Must stay identical to AtmosphereModel.h. The CPU harness greps both and compares them.
float HorizonSpan()
{
    return sqrt(kAtmosphereRadius * kAtmosphereRadius - kPlanetRadius * kPlanetRadius);
}

vec2 TransmittanceParameterisation(float Altitude, float CosSunZenith)
{
    const float Span   = HorizonSpan();
    const float Radius = kPlanetRadius + max(Altitude, 0.0);
    const float Rho    = sqrt(max(0.0, Radius * Radius - kPlanetRadius * kPlanetRadius));

    const float Discriminant = max(0.0, Radius * Radius * (CosSunZenith * CosSunZenith - 1.0)
                                        + kAtmosphereRadius * kAtmosphereRadius);
    const float Distance = max(0.0, -Radius * CosSunZenith + sqrt(Discriminant));
    const float Nearest  = kAtmosphereRadius - Radius;
    const float Farthest = Rho + Span;
    return vec2(clamp((Distance - Nearest) / max(Farthest - Nearest, 1e-6), 0.0, 1.0),
                min(1.0, Rho / Span));
}

void TransmittanceInverse(vec2 Uv, out float Altitude, out float CosSunZenith)
{
    const float Span   = HorizonSpan();
    const float Rho    = Span * Uv.y;
    const float Radius = sqrt(Rho * Rho + kPlanetRadius * kPlanetRadius);
    Altitude = Radius - kPlanetRadius;

    const float Nearest  = kAtmosphereRadius - Radius;
    const float Farthest = Rho + Span;
    const float Distance = Nearest + Uv.x * (Farthest - Nearest);
    CosSunZenith = Distance <= 0.0
        ? 1.0
        : clamp((Span * Span - Rho * Rho - Distance * Distance) / (2.0 * Radius * Distance), -1.0, 1.0);
}

// ⚠️ The multiple-scattering table spans the FULL sun-zenith range, negative included, concentrated near the
//    horizon by a signed square root. It must never be swapped for the mapping above: that one cannot express
//    a sun below the horizon at all, and multiple scattering below the horizon is the whole of twilight.
vec2 MultiScatterParameterisation(float Altitude, float CosSunZenith)
{
    const float H = clamp(Altitude / kAtmosphereThickness, 0.0, 1.0);
    const float S = CosSunZenith < 0.0 ? -1.0 : 1.0;
    return vec2(clamp(0.5 + 0.5 * S * sqrt(abs(CosSunZenith)), 0.0, 1.0), sqrt(H));
}

void MultiScatterInverse(vec2 Uv, out float Altitude, out float CosSunZenith)
{
    Altitude = Uv.y * Uv.y * kAtmosphereThickness;
    const float T = Uv.x * 2.0 - 1.0;
    CosSunZenith = (T < 0.0 ? -1.0 : 1.0) * T * T;
}

// ── Building ────────────────────────────────────────────────────────────────────────────────────────────────
// Evaluated once per texel by the build kernel. Deliberately expensive — 64 steps — because it runs 16 384 times
//    at bring-up rather than millions of times per frame.
vec3 ComputeTransmittanceTexel(vec2 Uv)
{
    float Altitude, CosSunZenith;
    TransmittanceInverse(Uv, Altitude, CosSunZenith);

    const vec3  Origin    = vec3(0.0, 0.0, kPlanetRadius + Altitude);
    const float SinZenith = sqrt(max(0.0, 1.0 - CosSunZenith * CosSunZenith));
    const vec3  Direction = vec3(SinZenith, 0.0, CosSunZenith);

    // A direction that meets the ground transmits nothing. Without this the table would report sunlight arriving
    //    through the planet and the ground would glow at night.
    if (RaySphereNearest(Origin, Direction, kPlanetRadius) > 0.0) return vec3(0.0);

    const float ToSpace = RaySphereNearest(Origin, Direction, kAtmosphereRadius);
    if (ToSpace <= 0.0) return vec3(1.0);
    return exp(-OpticalDepth(Origin, Direction, ToSpace, 64));
}

// The isotropic multiple-scattering transfer factor, following Hillaire's geometric-series closure: light that
//    scatters twice is treated as scattering isotropically thereafter, and the infinite series 1 + f + f² + …
//    collapses to 1/(1−f). That closure is what makes this a 32×32 table instead of a 4-D integral.
vec3 ComputeMultiScatterTexel(vec2 Uv, int Directions, int Steps)
{
    float Altitude, CosSunZenith;
    MultiScatterInverse(Uv, Altitude, CosSunZenith);

    const vec3  Origin    = vec3(0.0, 0.0, kPlanetRadius + Altitude);
    const float SinZenith = sqrt(max(0.0, 1.0 - CosSunZenith * CosSunZenith));
    const vec3  SunDirection = vec3(SinZenith, 0.0, CosSunZenith);

    vec3 SecondOrder = vec3(0.0);   // L₂: light scattered once, gathered from every direction
    vec3 Transfer    = vec3(0.0);   // f_ms: the fraction that goes on to scatter again

    // A Fibonacci sphere: uniform on the sphere without the pole clustering a latitude/longitude sweep gives,
    //    which would bias the gather toward straight up and down.
    for (int i = 0; i < Directions; ++i)
    {
        const float Fraction  = (float(i) + 0.5) / float(Directions);
        const float CosTheta  = 1.0 - 2.0 * Fraction;
        const float SinTheta  = sqrt(max(0.0, 1.0 - CosTheta * CosTheta));
        const float Phi       = float(i) * 2.39996323;      // the golden angle
        const vec3  Ray       = vec3(SinTheta * cos(Phi), SinTheta * sin(Phi), CosTheta);

        float Distance = RaySphereNearest(Origin, Ray, kAtmosphereRadius);
        const float Ground = RaySphereNearest(Origin, Ray, kPlanetRadius);
        if (Ground > 0.0) Distance = min(Distance, Ground);
        if (Distance <= 0.0) continue;

        const float Step = Distance / float(Steps);
        vec3 RayTransmittance = vec3(1.0);

        for (int j = 0; j < Steps; ++j)
        {
            const vec3  Position = Origin + Ray * (float(j) + 0.5) * Step;
            const float H        = length(Position) - kPlanetRadius;
            const vec3  Density  = AtmosphereDensity(H);

            const vec3 Extinction = kRayleighScattering * Density.x
                                  + vec3(MieExtinction()) * Density.y
                                  + kOzoneAbsorption    * Density.z;
            const vec3 Scattering = kRayleighScattering * Density.x + vec3(MieScattering()) * Density.y;
            const vec3 StepTransmittance = exp(-Extinction * Step);
            const vec3 Integrated = (Scattering - Scattering * StepTransmittance) / max(Extinction, vec3(1e-9));

            // Isotropic: the phase function is deliberately omitted, because by the second bounce the light has
            //    lost its directional memory. Including it here would double-count the forward lobe.
            const vec3 SunArriving = SunTransmittance(Position, SunDirection, 16);
            SecondOrder += RayTransmittance * SunArriving * Integrated * (1.0 / (4.0 * 3.14159265));
            Transfer    += RayTransmittance * Integrated * (1.0 / (4.0 * 3.14159265));

            RayTransmittance *= StepTransmittance;
        }
    }

    const float Weight = 4.0 * 3.14159265 / float(Directions);
    SecondOrder *= Weight;
    Transfer    *= Weight;

    // 1 + f + f² + … = 1/(1 − f). Clamped below 1 because a transfer of exactly 1 would be an atmosphere that
    //    never absorbs anything and the series would diverge.
    const vec3 Series = vec3(1.0) / max(vec3(1.0) - min(Transfer, vec3(0.999)), vec3(1e-4));
    return SecondOrder * Series;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    NIGHT SKY (A7)
//------------------------------------------------------------------------------------------------------------------------

const float kMoonAngularRadius = 0.004526;   // [rad] 0.259° — very nearly the sun's, which is why eclipses work
const float kMoonAlbedo        = 0.12;       // [-]   the moon is about as reflective as worn asphalt

// The lit fraction of the moon's disc at a point on it, given the phase. Phase 0 is new, 1 is full.
//
// 🔴 The terminator is a COSINE boundary on a sphere, not a straight line across a circle. Drawing a straight
//    chord gives a "D" shape at half phase instead of the correct half-disc, and at crescent phases the error is
//    obvious — the horns point the wrong way. The sphere's own geometry is what produces the curve.
float MoonLitFraction(vec2 DiscOffset, float Phase)
{
    const float R2 = dot(DiscOffset, DiscOffset);
    if (R2 > 1.0) return 0.0;

    // Reconstruct the sphere's surface normal at this point of the disc: the disc IS the sphere seen flat, so
    //    the z component follows from the other two.
    const vec3 Normal = vec3(DiscOffset, sqrt(max(0.0, 1.0 - R2)));

    // The sun's direction relative to the moon, expressed as an angle around the disc. Phase 0.5 puts it exactly
    //    to the side, which is the half moon.
    const float PhaseAngle = (1.0 - Phase) * 3.14159265;
    const vec3  ToSun = vec3(sin(PhaseAngle), 0.0, cos(PhaseAngle));

    // Lambert. The soft edge is one disc-radius wide, which at 10 px across is under a pixel — enough to stop
    //    the terminator aliasing without smearing the shape.
    return clamp(dot(Normal, ToSun) * 4.0, 0.0, 1.0);
}

// A procedural star field. A hash over a direction grid rather than a catalogue: a real catalogue is the better
//    answer eventually, but it needs a texture binding and an asset, and neither is worth spending before the
//    exposure and the atmosphere are known to present stars correctly at all.
//
//    ⚠️ Cell-based, so the density is uniform over the sphere. Sampling a latitude/longitude grid instead
//    clusters stars at the poles, which reads as two bright patches wherever the axis happens to point.
vec3 StarField(vec3 Direction, float Rotation, float Brightness)
{
    if (Brightness <= 0.0) return vec3(0.0);

    // Rotate with sidereal time so the field turns with the sky rather than being painted on the camera.
    const float C = cos(Rotation), S = sin(Rotation);
    const vec3 Turned = vec3(Direction.x * C - Direction.y * S,
                             Direction.x * S + Direction.y * C,
                             Direction.z);

    // Quantise to a grid fine enough that two stars rarely share a cell.
    //
    // ⚠️ These two numbers together set the STAR COUNT, and they were originally wrong by an order of
    //    magnitude. 700 cells at a 0.982 threshold puts 120 352 stars on the sphere; the real naked-eye sky has
    //    about 9 100 to magnitude 6.5. Thirteen times too many does not read as a rich sky, it reads as noise —
    //    the eye stops seeing individual stars and sees texture instead, which is exactly why a real sky looks
    //    sparser than people expect when they first try to render one.
    //
    //    260 and 0.990 give 9 056, measured by walking the sphere and counting distinct cells rather than by
    //    estimating from the threshold — the shell only intersects a fraction of the grid, so the fraction of
    //    CELLS holding a star is not the fraction of DIRECTIONS that land on one.
    const float Cells = 260.0;
    const vec3  Scaled = Turned * Cells;
    const vec3  Cell   = floor(Scaled);
    const vec3  Within = Scaled - Cell;

    // One hash per cell decides whether it holds a star, where in the cell, and how bright.
    const float H = fract(sin(dot(Cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
    if (H < 0.990) return vec3(0.0);            // ≈ 9 100 stars on the sphere — the naked-eye sky

    const float H2 = fract(H * 7919.0);
    const float H3 = fract(H * 6271.0);
    const float H4 = fract(H * 4703.0);

    const vec3  Centre   = vec3(H2, H3, H4);
    const float Distance = length(Within - Centre);

    // ⚠️ Wide enough to cover about two pixels, and no wider. A falloff of 900 gave a 0.72 px star at 1080p —
    //    SUB-PIXEL — so whether a pixel centre happened to land inside it changed with the smallest camera
    //    movement and the whole field blinked on and off. That reads as a rendering fault rather than as stars.
    //    120 gives 2.0 px at 1080p and 3.9 px at 4K, which survives sub-pixel motion and still reads as a point.
    const float Intensity = exp(-Distance * Distance * 120.0);
    if (Intensity < 0.002) return vec3(0.0);

    // Real stars are not white. A cheap colour temperature spread across the hash keeps the field from looking
    //    like uniform pixel noise.
    const float Warmth = fract(H * 2711.0);
    const vec3  Colour = mix(vec3(0.72, 0.80, 1.00), vec3(1.00, 0.86, 0.68), Warmth);

    // Magnitude distribution: a few bright stars and many faint ones, which is what the eye reads as a real sky.
    //
    //    ⚠️ Normalised by the widening above. Making each star 2.8× wider in radius spreads its light over ~7.7×
    //    the solid angle, so without this the field would have got dramatically brighter as a side effect of a
    //    flicker fix — two changes for the price of one, and the second one unintended.
    const float Magnitude = pow(fract(H * 3319.0), 3.0);
    return Colour * Intensity * Magnitude * Brightness * 0.13;
}

#endif // ATMOSPHERE_SCATTERING_SLANG
