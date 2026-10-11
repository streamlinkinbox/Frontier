//============================================================================================================================================
// 📦 Engine/DisplayPresentation/ColourTransfer.h — one definition of how linear radiance becomes a display pixel
//============================================================================================================================================
// Celestial port. Written because the sky feeds BOTH render paths, and the two were tone-mapping differently.
//
// ⚠️ WHAT WAS WRONG. The engine had three tone maps, not one:
//        ReSTIRViewport.slang   ACES + exposure + low-light desaturation, gamma 2.2
//        AtrousDenoise.slang    the same, deliberately kept in step with the kernel
//        ShadowResolve.slang    plain Reinhard, no exposure, no desaturation
//        VisibilityRaster.cpp   plain Reinhard, no exposure, no desaturation
//    So the GI-off path and the GI-on path turned identical radiance into different pixels. Measured over the
//    range the sky actually occupies:
//
//        linear   Reinhard→8-bit   ACES→8-bit   difference
//         0.02          43              32         −11
//         0.10          86              99         +13
//         0.20         113             147         +34
//        →0.40         144             193         +49  ← worst
//         0.80         176             224         +48
//         3.00         224             250         +26
//
//    A 49/255 divergence at mid-tones. That was tolerable while the two paths drew different things — the GI-off
//    path was a shadow-map preview and nobody compared them side by side. It stops being tolerable the moment one
//    sky feeds both, because the SAME atmosphere at the SAME time of day would be a visibly different colour
//    depending on a setting that is supposed to change lighting, not exposure.
//
// This header is the single definition. It is header-only and dependency-free for the same reason
//    AtmosphereModel.h is: it has to run in the CPU raster, in the headless proofs, and be transcribed into GLSL,
//    and none of those can share a binary. A shared DEFINITION is the thing that stops the copies drifting.
//
// ⚠️ ON PRECISION. The demo this port follows is WebGL and had exactly one colour space available: 8-bit RGBA,
//    tone-mapped in the fragment shader, no intermediate. This engine is not so limited, and the difference
//    matters most for exactly the content being ported. The chain here is deliberately three-tier:
//
//        rgba32f  accumulation and history   — the running mean, moments, the à-trous input. Full float, because
//                                              a running mean of thousands of samples in half would quantise.
//        rgba16f  intermediate G-buffers     — normals, history surface. Half is plenty for a unit vector.
//        rgba8    presentation only          — the LAST step, after the tone map, never before it.
//
//    Everything upstream of presentation is LINEAR and unbounded. The sky's radiance range makes that necessary
//    rather than luxurious: a noon zenith is ~0.53 and the solar aureole at sunset is ~6.2, a factor of twelve,
//    and the twilight white line sits at ~0.08 where 8-bit steps are 1/255 apart and would band the one feature
//    the transition is judged by. The demo could not do better; we can, so we should.
//
//    The presentation format is UNORM with the gamma applied in the shader, NOT an _SRGB format. Both are correct
//    on their own; together they apply the curve twice and wash the image out. Whichever is chosen has to be
//    chosen once, and it is chosen here.

#pragma once

#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     SETTINGS
//------------------------------------------------------------------------------------------------------------------------

// Named as the reference demo names them (Post Process > Exposure & Tonemap), so the panel is a projection of
//    this rather than a translation of it.
enum class ToneMapCategory : uint32_t
{
    None     = 0u,   // clamp only — for debug views that must show raw values
    Aces     = 1u,   // the default; matches the ReSTIR kernel
    Reinhard = 2u,   // what the GI-off path used to do alone; kept so old sheets can be reproduced
    Filmic   = 3u,
    AgX      = 4u,
};

struct ColourTransfer
{
    ToneMapCategory ToneMap    = ToneMapCategory::Aces;
    float           Exposure   = 1.0f;    // [x] linear multiplier applied BEFORE the curve
    float           Gamma      = 2.2f;    // display encode

    // ── Low-light desaturation (Purkinje) ──────────────────────────────────────────────────────────────────────
    // Below roughly 3 cd/m² the cones give out before the rods do, so colour drains from what the eye sees, and
    //    by ~0.003 it is gone. Rendering full saturation down there is what turns a faint pre-dawn glow into a
    //    lurid orange band.
    //
    // ⚠️ THIS IS A PER-FRAME VALUE, NOT A PER-PIXEL ONE, and getting that wrong is worth recording. The first
    //    version of this header evaluated the cone response from each pixel's OWN luminance. That is wrong twice
    //    over: the eye adapts to a scene, not to a pixel, so a dark pixel in a bright frame is not seen
    //    achromatically; and scene radiance is not cd/m² until exposure has mapped it, so a 3.0 threshold applied
    //    to raw render units desaturated almost everything. Measured on our own sky, the noon zenith kept 11% of
    //    its colour and the whole set of proof sheets came out grey.
    //
    //    ExposureIntegrator::QueryColourSaturation already does this correctly and has since A7d: one value per
    //    frame, computed from the ADAPTED luminance on a LOG scale between the two thresholds, and hard-wired to
    //    1.0 in Manual mode so an image made before the curve existed is still reproducible. `Saturation` below
    //    is that value. This header must not recompute it — it consumes it, exactly as the ReSTIR kernel does.
    float           Saturation = 1.0f;    // [0..1] from ExposureIntegrator::QueryColourSaturation()
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TRANSFER
//------------------------------------------------------------------------------------------------------------------------

class ColourPipeline
{
public:
    static constexpr float kLuminance[3] = { 0.2126f, 0.7152f, 0.0722f };

    static float Luminance(const float Rgb[3]) noexcept
    {
        return Rgb[0] * kLuminance[0] + Rgb[1] * kLuminance[1] + Rgb[2] * kLuminance[2];
    }

    // The Narkowicz ACES fit, as used by the ReSTIR kernel. Matching the kernel exactly is the point.
    static float AcesFilm(float X) noexcept
    {
        constexpr float A = 2.51f, B = 0.03f, C = 2.43f, D = 0.59f, E = 0.14f;
        const float Y = (X * (A * X + B)) / (X * (C * X + D) + E);
        return Y < 0.0f ? 0.0f : (Y > 1.0f ? 1.0f : Y);
    }

    static float Reinhard(float X) noexcept { return X < 0.0f ? 0.0f : X / (1.0f + X); }

    static float Filmic(float X) noexcept
    {
        // Hable/Uncharted2 shoulder, evaluated against a white point so the curve reaches 1.
        const float Mapped = HableCurve(X) / HableCurve(11.2f);
        return Mapped < 0.0f ? 0.0f : (Mapped > 1.0f ? 1.0f : Mapped);
    }

    static float AgX(float X) noexcept
    {
        // A compact approximation of the AgX sigmoid in log space: gentler highlight roll-off than ACES and it
        //    holds hue far better on saturated lights, which is what it is chosen for.
        if (X <= 0.0f) return 0.0f;
        const float LogX = std::log2(X + 1e-6f);
        const float T = (LogX + 12.47393f) / (12.47393f + 4.026069f);   // AgX's standard log range
        const float S = T < 0.0f ? 0.0f : (T > 1.0f ? 1.0f : T);
        return S * S * (3.0f - 2.0f * S);
    }

    // Linear scene radiance → display-encoded [0,1]. This is the whole transfer, in the order it must happen:
    //    desaturate, expose, curve, encode. Every stage is order-dependent and the order is the reason this is
    //    one function rather than four call sites.
    static void Apply(const ColourTransfer& Transfer, const float LinearRgb[3], float OutRgb[3]) noexcept
    {
        float Working[3] = { LinearRgb[0], LinearRgb[1], LinearRgb[2] };

        // ① Desaturation FIRST, in linear radiance, because that is where the colour physically is. Doing it
        //    after the curve would mix display values, and a channel that had already clipped would drag the
        //    grey it is mixed toward. Saturation is the frame's value from ExposureIntegrator, never recomputed
        //    per pixel — see the note on the field.
        if (Transfer.Saturation < 1.0f)
        {
            const float L = Luminance(Working);
            for (int C = 0; C < 3; ++C) Working[C] = L + (Working[C] - L) * Transfer.Saturation;
        }

        // ② Exposure, still linear.
        for (int C = 0; C < 3; ++C) Working[C] *= Transfer.Exposure;

        // ③ The curve.
        for (int C = 0; C < 3; ++C)
        {
            switch (Transfer.ToneMap)
            {
                case ToneMapCategory::None:     Working[C] = Working[C] < 0.0f ? 0.0f : (Working[C] > 1.0f ? 1.0f : Working[C]); break;
                case ToneMapCategory::Reinhard: Working[C] = Reinhard(Working[C]); break;
                case ToneMapCategory::Filmic:   Working[C] = Filmic(Working[C]);   break;
                case ToneMapCategory::AgX:      Working[C] = AgX(Working[C]);      break;
                case ToneMapCategory::Aces:
                default:                        Working[C] = AcesFilm(Working[C]); break;
            }
        }

        // ④ Display encode, last. The presentation image is UNORM, so the curve is applied here and the format
        //    must NOT also be _SRGB or it is applied twice.
        const float Inverse = Transfer.Gamma > 0.0f ? 1.0f / Transfer.Gamma : 1.0f;
        for (int C = 0; C < 3; ++C)
            OutRgb[C] = std::pow(Working[C] < 0.0f ? 0.0f : Working[C], Inverse);
    }

    // The same, quantised to 8-bit for a presentation image or a proof sheet.
    static void ApplyToByte(const ColourTransfer& Transfer, const float LinearRgb[3], unsigned char OutRgb[3]) noexcept
    {
        float Encoded[3];
        Apply(Transfer, LinearRgb, Encoded);
        for (int C = 0; C < 3; ++C)
        {
            const float Scaled = Encoded[C] * 255.0f + 0.5f;
            OutRgb[C] = static_cast<unsigned char>(Scaled < 0.0f ? 0.0f : (Scaled > 255.0f ? 255.0f : Scaled));
        }
    }

private:
    static float HableCurve(float X) noexcept
    {
        constexpr float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f;
        return ((X * (A * X + C * B) + D * E) / (X * (A * X + B) + D * F)) - E / F;
    }
};

} // namespace Frontier
