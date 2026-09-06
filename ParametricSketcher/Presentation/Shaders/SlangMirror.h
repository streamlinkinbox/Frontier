//============================================================================================================================================
// 📦 ParametricSketcher/Presentation/Shaders/SlangMirror.h — Slang vocabulary in C++ so the .slang files and the software raster share one text
//============================================================================================================================================
// The shaders under Presentation/Shaders/*.slang are the source of truth for the Vulkan build. The software rasteriser
//    executes the same shader bodies compiled as C++ by including them after this header: float2/3/4, float4x4, mul,
//    dot, normalize, lerp, smoothstep. Screen-space derivatives are never emulated: the raster hands each fragment an
//    explicit per-pixel footprint (see GridProjection), which is also what the .slang files take as input — the subset the
//    SolidArc shaders use. Nothing here is engine-visible; it is a shader authoring convenience.
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace Frontier::SlangMirror
{

struct float2
{
    float x = 0, y = 0;
    constexpr float2() = default;
    constexpr float2(float X, float Y) : x(X), y(Y) {}
    constexpr explicit float2(float S) : x(S), y(S) {}
};
struct float3
{
    float x = 0, y = 0, z = 0;
    constexpr float3() = default;
    constexpr float3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    constexpr explicit float3(float S) : x(S), y(S), z(S) {}
    constexpr float3(float2 XY, float Z) : x(XY.x), y(XY.y), z(Z) {}
    constexpr float2 xy() const { return { x, y }; }
};
struct float4
{
    float x = 0, y = 0, z = 0, w = 0;
    constexpr float4() = default;
    constexpr float4(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) {}
    constexpr float4(float3 V, float W) : x(V.x), y(V.y), z(V.z), w(W) {}
    constexpr float4(float2 V, float Z, float W) : x(V.x), y(V.y), z(Z), w(W) {}
    constexpr explicit float4(float S) : x(S), y(S), z(S), w(S) {}
    constexpr float3 xyz() const { return { x, y, z }; }
    constexpr float2 xy() const { return { x, y }; }
    constexpr float2 zw() const { return { z, w }; }
};

// Column-major 4×4 exactly as Vulkan uniforms expect; mul(M, v) = M·v.
struct float4x4
{
    float m[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
};

#define SLANG_MIRROR_BINARY(Type, Op) \
    constexpr Type operator Op(Type A, Type B) { return Type Type##_BIN(A, B, Op); } \
    constexpr Type operator Op(Type A, float S) { return Type Type##_SCL(A, S, Op); } \
    constexpr Type operator Op(float S, Type A) { return Type Type##_LCS(S, A, Op); }
#define float2_BIN(A, B, Op) { A.x Op B.x, A.y Op B.y }
#define float2_SCL(A, S, Op) { A.x Op S, A.y Op S }
#define float2_LCS(S, A, Op) { S Op A.x, S Op A.y }
#define float3_BIN(A, B, Op) { A.x Op B.x, A.y Op B.y, A.z Op B.z }
#define float3_SCL(A, S, Op) { A.x Op S, A.y Op S, A.z Op S }
#define float3_LCS(S, A, Op) { S Op A.x, S Op A.y, S Op A.z }
#define float4_BIN(A, B, Op) { A.x Op B.x, A.y Op B.y, A.z Op B.z, A.w Op B.w }
#define float4_SCL(A, S, Op) { A.x Op S, A.y Op S, A.z Op S, A.w Op S }
#define float4_LCS(S, A, Op) { S Op A.x, S Op A.y, S Op A.z, S Op A.w }
SLANG_MIRROR_BINARY(float2, +) SLANG_MIRROR_BINARY(float2, -) SLANG_MIRROR_BINARY(float2, *) SLANG_MIRROR_BINARY(float2, /)
SLANG_MIRROR_BINARY(float3, +) SLANG_MIRROR_BINARY(float3, -) SLANG_MIRROR_BINARY(float3, *) SLANG_MIRROR_BINARY(float3, /)
SLANG_MIRROR_BINARY(float4, +) SLANG_MIRROR_BINARY(float4, -) SLANG_MIRROR_BINARY(float4, *) SLANG_MIRROR_BINARY(float4, /)
#undef SLANG_MIRROR_BINARY
constexpr float2 operator-(float2 A) { return { -A.x, -A.y }; }
constexpr float3 operator-(float3 A) { return { -A.x, -A.y, -A.z }; }
constexpr float4 operator-(float4 A) { return { -A.x, -A.y, -A.z, -A.w }; }

constexpr float dot(float2 A, float2 B) { return A.x * B.x + A.y * B.y; }
constexpr float dot(float3 A, float3 B) { return A.x * B.x + A.y * B.y + A.z * B.z; }
constexpr float dot(float4 A, float4 B) { return A.x * B.x + A.y * B.y + A.z * B.z + A.w * B.w; }
constexpr float3 cross(float3 A, float3 B) { return { A.y * B.z - A.z * B.y, A.z * B.x - A.x * B.z, A.x * B.y - A.y * B.x }; }
inline float length(float2 A) { return std::sqrt(dot(A, A)); }
inline float length(float3 A) { return std::sqrt(dot(A, A)); }
inline float2 normalize(float2 A) { float L = length(A); return L > 0 ? A / L : A; }
inline float3 normalize(float3 A) { float L = length(A); return L > 0 ? A / L : A; }
constexpr float saturate(float A) { return A < 0 ? 0 : (A > 1 ? 1 : A); }
constexpr float clamp(float A, float Lo, float Hi) { return A < Lo ? Lo : (A > Hi ? Hi : A); }
constexpr float lerp(float A, float B, float T) { return A + (B - A) * T; }
constexpr float3 lerp(float3 A, float3 B, float T) { return A + (B - A) * T; }
constexpr float4 lerp(float4 A, float4 B, float T) { return A + (B - A) * T; }
constexpr float smoothstep(float E0, float E1, float X) { float T = saturate((X - E0) / (E1 - E0)); return T * T * (3 - 2 * T); }
inline float abs(float A) { return std::fabs(A); }
inline float2 abs(float2 A) { return { std::fabs(A.x), std::fabs(A.y) }; }
inline float3 abs(float3 A) { return { std::fabs(A.x), std::fabs(A.y), std::fabs(A.z) }; }
inline float fract(float A) { return A - std::floor(A); }
inline float2 fract(float2 A) { return { fract(A.x), fract(A.y) }; }
inline float floor(float A) { return std::floor(A); }
inline float2 floor(float2 A) { return { std::floor(A.x), std::floor(A.y) }; }
inline float sqrt(float A) { return std::sqrt(A); }
inline float exp(float A) { return std::exp(A); }
inline float pow(float A, float B) { return std::pow(A, B); }
inline float min(float A, float B) { return A < B ? A : B; }
inline float max(float A, float B) { return A > B ? A : B; }
inline float2 min(float2 A, float2 B) { return { min(A.x, B.x), min(A.y, B.y) }; }
inline float2 max(float2 A, float2 B) { return { max(A.x, B.x), max(A.y, B.y) }; }
inline float3 max(float3 A, float S) { return { max(A.x, S), max(A.y, S), max(A.z, S) }; }
inline float step(float Edge, float X) { return X < Edge ? 0.0f : 1.0f; }
inline float3 reflect(float3 I, float3 N) { return I - N * (2.0f * dot(N, I)); }

constexpr float4 mul(const float4x4& M, float4 V)
{
    return {
        M.m[0] * V.x + M.m[4] * V.y + M.m[8]  * V.z + M.m[12] * V.w,
        M.m[1] * V.x + M.m[5] * V.y + M.m[9]  * V.z + M.m[13] * V.w,
        M.m[2] * V.x + M.m[6] * V.y + M.m[10] * V.z + M.m[14] * V.w,
        M.m[3] * V.x + M.m[7] * V.y + M.m[11] * V.z + M.m[15] * V.w };
}

} // namespace Frontier::SlangMirror
