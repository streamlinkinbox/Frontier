//============================================================================================================================================
//                                                    DENOISECPUSHIM.H
//============================================================================================================================================
// 🧩 GLSL vocabulary for compiling Engine/Shaders/AtrousDenoise.slang 1:1 as C++ (FRONTIER_CPU_PORT), the way
//    SlangCpuShim.h does for MaterialEvaluation.slang. Only what the filter uses: integer vectors, image2D with
//    imageLoad/imageStore, the workgroup id, and the prologue macros that erase the GLSL-only declarations.
//
//    The shader text is NOT edited for the port. CheckMaterialDenoise.sh makes three mechanical substitutions and the
//    proof re-derives them line by line (see DenoiseReprojectionProof.cpp §C0):
//        ① `#version 460`                       — dropped (C++ has no such directive)
//        ② `layout(local_size_x = 8, ...) in;`  — dropped (the wrapper sets the workgroup id instead)
//        ③ `float[5](a, b, c, d, e)`            — GLSL's array constructor → C++ brace init, values identical
//    and the push-constant block is split out of the file so that the block's members can be reached unqualified the
//    way GLSL reaches them (the nine `DenoiseParameters.` macros below; the shader names them bare).
//
//    Bounds are checked, not clamped: a tap outside the image is a defect in the port (the shader's own guards exist
//    precisely to prevent it), and a silent zero would turn that defect into a plausible-looking picture.

#pragma once

#include "SlangCpuShim.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTEGER VECTORS
//------------------------------------------------------------------------------------------------------------------------

// Push-constant field types: uvec2 (Extent) and the scalar words. Declared before ivec2, which converts from it.
struct uvec2
{
    unsigned x, y;
    uvec2() : x(0u), y(0u) {}
    uvec2(unsigned x_, unsigned y_) : x(x_), y(y_) {}
    unsigned&       operator[](int Index)       { return Index == 0 ? x : y; }
    unsigned        operator[](int Index) const { return Index == 0 ? x : y; }
};

struct ivec2
{
    int x, y;
    ivec2() : x(0), y(0) {}
    ivec2(int x_, int y_) : x(x_), y(y_) {}
    ivec2(const vec2& v) : x(static_cast<int>(v.x)), y(static_cast<int>(v.y)) {}
    ivec2(const uvec2& v) : x(static_cast<int>(v.x)), y(static_cast<int>(v.y)) {}
};

inline ivec2 operator+(ivec2 a, ivec2 b) { return ivec2(a.x + b.x, a.y + b.y); }
inline ivec2 operator-(ivec2 a, ivec2 b) { return ivec2(a.x - b.x, a.y - b.y); }
inline ivec2 operator*(ivec2 a, int s)   { return ivec2(a.x * s, a.y * s); }
inline ivec2 operator-(ivec2 a, int s)   { return ivec2(a.x - s, a.y - s); }
inline ivec2 clamp(ivec2 x, ivec2 lo, ivec2 hi)
{
    return ivec2(clamp(x.x, lo.x, hi.x), clamp(x.y, lo.y, hi.y));
}

inline int   abs(int x)          { return x < 0 ? -x : x; }
inline int   min(int a, int b)   { return a < b ? a : b; }
inline int   max(int a, int b)   { return a > b ? a : b; }

//------------------------------------------------------------------------------------------------------------------------
//                                                       IMAGES
//------------------------------------------------------------------------------------------------------------------------
// rgba32f / rgba16f / rgba8 are all vec4 texels here — the port reads and writes float4 either way, and the storage
//    precision is the GPU's business (the proof's own tolerances are far looser than fp16).

struct image2D
{
    int Width  = 0;
    int Height = 0;
    std::vector<vec4> Texels;

    void Assign(int Width_, int Height_, vec4 Fill = vec4(0.0f))
    {
        Width = Width_; Height = Height_;
        Texels.assign(static_cast<size_t>(Width_) * static_cast<size_t>(Height_), Fill);
    }

    [[nodiscard]] size_t Offset(int X, int Y) const
    {
        if (X < 0 || Y < 0 || X >= Width || Y >= Height)
        {
            std::fprintf(stderr, "[DenoiseCpuShim] image2D read out of range (%d, %d) of %dx%d\n", X, Y, Width, Height);
            std::abort();
        }
        return static_cast<size_t>(Y) * static_cast<size_t>(Width) + static_cast<size_t>(X);
    }
};

inline vec4 imageLoad(const image2D& Image, ivec2 Pixel)             { return Image.Texels[Image.Offset(Pixel.x, Pixel.y)]; }
inline void imageStore(image2D& Image, ivec2 Pixel, vec4 Value)      { Image.Texels[Image.Offset(Pixel.x, Pixel.y)] = Value; }

//------------------------------------------------------------------------------------------------------------------------
//                                                    WORKGROUP ID
//------------------------------------------------------------------------------------------------------------------------

struct InvocationId
{
    unsigned x = 0u, y = 0u;
    struct Read2 { const unsigned* a; const unsigned* b; operator vec2() const { return vec2(static_cast<float>(*a), static_cast<float>(*b)); } } xy{ &x, &y };
};

extern InvocationId gl_GlobalInvocationID;

//------------------------------------------------------------------------------------------------------------------------
//                                                  GLSL PROLOGUE MACROS
//------------------------------------------------------------------------------------------------------------------------
// Erase the declarations C++ cannot parse. `push_constant` becomes the struct keyword, so the shader's
//    `layout(push_constant) uniform DenoiseConstants { … };` declares a plain aggregate whose instance the mirror
//    supplies (`DenoiseParameters`, declared below and closed by the transformed text).

#define layout(...)
#define uniform
#define readonly
#define writeonly
#define push_constant struct

// The push-constant members (`Extent`, `StepSize`, `Enabled`, `NormalPower`, `DepthScale`, `LuminanceScale`,
//    `Exposure`, `FinalLevel`, `ColourSaturation`) are macro-bound to the block instance by AtrousDenoiseMirror.cpp,
//    between the two halves of the transformed shader: GLSL lets `main` name a block member bare, C++ does not, and
//    the macros must not be in scope while the struct body itself is preprocessed.
