//============================================================================================================================================
//                                                   ATROUSDENOISEMIRROR.H
//============================================================================================================================================
// 🧩 The M9 gate's handle on the real à-trous filter: Engine/Shaders/AtrousDenoise.slang compiled 1:1 as C++ (see
//    DenoiseCpuShim.h for the port's three mechanical substitutions), driven through a plain-float API so the proof
//    never touches the shim's types.
//
//    Nothing here is a re-implementation. `Run` fills the shader's four image2D bindings and the push constant, sets
//    one invocation id and calls the shader's own `main()`. `ToneMap`, `AcesFilm`, `KernelWeight` and the early-out
//    threshold are the shader's own functions and constants, called directly — which is what makes the presentation
//    A/B in §C3 a statement about the shipped filter rather than about a transcription of it.

#pragma once

#include <cstdint>

namespace DenoiseMirror {

struct RunConfiguration
{
    uint32_t Extent           = 0u;     // [px]  square image extent
    uint32_t StepSize         = 1u;     // [px]  tap spacing for this level (1, 2, 4, 8, 16)
    bool     Enabled          = true;   // [-]   0 = straight copy, the identity switch
    bool     FinalLevel       = true;   // [-]   also tone-map into the output image
    float    NormalPower      = 64.0f;  // [-]   σn — the engine's Push.NormalPower
    float    DepthScale       = 0.05f;  // [-]   σz — the engine's Push.DepthScale
    float    LuminanceScale   = 4.0f;   // [-]   σl — the engine's Push.LuminanceScale
    float    Exposure         = 1.0f;   // [-]   the engine's Dispatch.Exposure
    float    ColourSaturation = 1.0f;   // [-]   the engine's Dispatch.ColourSaturation
};

// One dispatch over an Extent × Extent image. `Source` and `Surface` are 4-float texels (xyz = linear radiance,
//    w = variance / depth ≤ 0 = none); `Target` receives the filtered texels and `Output`, when non-null, the
//    tone-mapped presentation texels. All four arrays are Extent × Extent × 4 floats.
void Run(const RunConfiguration& Configuration, const float* Source, const float* Surface, float* Target, float* Output);

// The shader's own functions and constants, for the proof's parity and sanity checks.
float AcesFilm(float X);
void  ToneMap(float R, float G, float B, float Exposure, float ColourSaturation, float* Out3);
float KernelWeight(int32_t Offset);          // the 5-tap B₃ spline, offsets −2 … +2
float EarlyOutVariance();                    // kEarlyOutVariance, derived in the shader from one 8-bit step

// The reference simulation the kernel runs before the filter sees a pixel (ReSTIRViewport.slang `ResolveSurface`):
//    running mean + first two luminance moments → variance of the MEAN (sample variance / count, a brand-new pixel
//    reporting luma²). Mirrored here from the shader text, which the proof pins line by line (§B) — the filter's
//    input contract is that quantity, and a proof that fed it anything else would prove the wrong thing.
struct Accumulator
{
    float Mean[3]    = { 0.0f, 0.0f, 0.0f };
    float Moments[2] = { 0.0f, 0.0f };
    float Count      = 0.0f;

    void Resolve(const float SampleRadiance[3], float* OutRadiance, float* OutVariance);
};

} // namespace DenoiseMirror
