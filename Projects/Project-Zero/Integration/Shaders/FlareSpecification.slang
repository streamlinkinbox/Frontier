//============================================================================================================================================
// FlareSpecification.slang — panel `post` lens flare mirror (reference `lensFlare`)
//
// Verbatim transcription of the celestial panel's flare kernel (upstream C++ port,
// Host/CpuPort/CelestialIntegrator.cpp `LensFlare` + `HueOf` + `AddLensFlare` visibility):
// 8-slot chromatic ghosts on the source axis, a chromatic halo ring, the blue
// anamorphic streak, starburst diffraction spikes, and the hot core — weighted by
// FlareVariety (0 Cinematic · 1 Anamorphic · 2 Starburst · 3 Halo).
//
// Dual-compile core (Slang + C++17 through SlangInterchange.h): pure functions, no
// file-scope state. Coordinates are centered tan-half-angle units shared with the
// driver: pixel (u,v) = ray slope over tan(fov/2), source (srcU,srcV) the same
// projection of the luminaire direction. Included by exactly ONE translation unit
// (RendererHost.cpp). Operation order matches the port term by term.
//============================================================================================================================================

#ifndef FLARE_SPECIFICATION_INCLUDED
#define FLARE_SPECIFICATION_INCLUDED

// Panel entity `post` flare fields (CelestialSpecification.h PostCriteria).
struct FlarePostConfig
{
    float variety;              // 0 Cinematic · 1 Anamorphic · 2 Starburst · 3 Halo
    float intensity;            // FlareIntensity master gain
    float ghosts;               // Ghosts active ghost count (<= 8)
    float halo;                 // Halo ring radius
    float streak;               // Streak gain
    float chroma;               // Chroma white-to-spectral blend
};

FlarePostConfig FlarePostDefaults() noexcept
{
    FlarePostConfig c;
    c.variety = 0.0f;
    c.intensity = 1.0f;
    c.ghosts = 5.0f;
    c.halo = 0.55f;
    c.streak = 0.8f;
    c.chroma = 0.65f;
    return c;
}

float FlareFract(float x) noexcept
{
    return x - floor(x);
}

float FlareStep(float edge, float x) noexcept
{
    return x < edge ? 0.0f : 1.0f;
}

// GLSL smoothstep, including the port's edge-equal degeneracy guard.
float FlareSmoothStep(float e0, float e1, float x) noexcept
{
    float denom = e1 - e0;
    if (abs(denom) < 1e-20f)
    {
        return x < e0 ? 0.0f : 1.0f;
    }
    float t = clamp((x - e0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float3 FlareSplat(float s) noexcept
{
    return float3(s, s, s);
}

// `hue` — the flare's chromatic ring. The channel lambda is spelled as a plain
// helper ( Slang has no lambdas); the GLSL mod match is verbatim from the port.
float FlareHueChannel(float x) noexcept
{
    float m = x - 6.0f * floor(x / 6.0f);
    return clamp(abs(m - 3.0f) - 1.0f, 0.0f, 1.0f);
}

float3 FlareHue(float h) noexcept
{
    float base = h * 6.0f;
    return float3(FlareHueChannel(base + 0.0f), FlareHueChannel(base + 4.0f), FlareHueChannel(base + 2.0f));
}

// Reference `lensFlare`, transcribed term by term: (u,v) is the pixel, (srcU,srcV)
// the luminaire, both centered tan-half-angle units. Returns the flare radiance
// before the luminaire colour/intensity scale, which the driver applies per source.
float3 FlareLensKernel(float u, float v, float srcU, float srcV, float visibility, FlarePostConfig cfg) noexcept
{
    float3 f = float3(0.0f, 0.0f, 0.0f);
    float mx = u - srcU;
    float my = v - srcV;
    float variety = cfg.variety;

    float wGhost = (variety == 0.0f || variety == 2.0f) ? 1.0f : 0.0f;
    float wHalo = (variety == 0.0f || variety == 3.0f) ? 1.0f : ((variety == 2.0f) ? 0.5f : 0.0f);
    float wStreak = (variety == 1.0f) ? 2.2f : ((variety == 0.0f) ? 1.0f : 0.35f);
    float wBurst = (variety == 2.0f) ? 1.0f : ((variety == 0.0f) ? 0.35f : 0.0f);

    for (int i = 0; i < 8; ++i)
    {
        if (float(i) >= cfg.ghosts)
        {
            break;
        }
        float fi = float(i);
        float k = -1.35f + fi * 0.42f;
        float px = srcU * k;
        float py = srcV * k;
        float r = 0.035f + 0.07f * FlareFract(fi * 0.618f + 0.31f);
        float dx = u - px;
        float dy = v - py;
        float dd = sqrt(dx * dx + dy * dy);
        float g = FlareSmoothStep(r, r * 0.35f, dd) * 0.9f + FlareSmoothStep(r * 1.6f, r, dd) * 0.25f;
        g = g * (0.06f + 0.06f * FlareFract(fi * 0.37f));
        f = f + lerp(FlareSplat(1.0f), FlareHue(FlareFract(fi * 0.23f + 0.5f)), cfg.chroma) * (g * wGhost);
    }

    float hx = u - srcU * 0.25f;
    float hy = v - srcV * 0.25f;
    float hr = sqrt(hx * hx + hy * hy);
    float ringDist = abs(hr - cfg.halo);
    float halo = FlareSmoothStep(0.045f, 0.0f, ringDist) * 0.09f;
    float ringAngle = atan2(hy, hx);
    f = f + lerp(FlareSplat(1.0f), FlareHue(FlareFract(ringAngle / 6.283f + ringDist * 8.0f)), cfg.chroma * 0.8f) * (halo * wHalo);

    float streak = exp(-abs(my) * 95.0f) * exp(-abs(mx) * 2.2f) * 0.55f;
    f = f + lerp(FlareSplat(1.0f), float3(0.45f, 0.65f, 1.0f), cfg.chroma) * (streak * cfg.streak * wStreak);

    float a = atan2(my, mx);
    float len = sqrt(mx * mx + my * my);
    float burst = pow(abs(sin(a * 4.0f + 0.3f)), 24.0f) * exp(-len * 3.5f) * 0.35f
                + pow(abs(sin(a * 7.0f)), 40.0f) * exp(-len * 6.0f) * 0.25f;
    f = f + float3(1.0f, 0.95f, 0.85f) * (burst * wBurst);
    f = f + float3(1.0f, 0.9f, 0.8f) * (exp(-len * 1.6f) * 0.04f);

    return f * (visibility * cfg.intensity);
}

// Reference `AddLensFlare` visibility: hard in-front gate, elevation ramp,
// in-frame fade, below-horizon step.
float FlareSourceVisibility(float cz, float elevDeg, float srcU, float srcV) noexcept
{
    float inFront = (cz > 0.02f) ? 1.0f : 0.0f;
    float sunUp = FlareSmoothStep(-6.0f, 4.0f, elevDeg);
    float inFrame = 1.0f - FlareSmoothStep(1.25f, 2.2f, sqrt(srcU * srcU + srcV * srcV));
    return inFront * sunUp * inFrame * FlareStep(0.0f, elevDeg + 0.5f);
}

#endif // FLARE_SPECIFICATION_INCLUDED
