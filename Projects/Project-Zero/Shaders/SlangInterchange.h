//============================================================================================================================================
// 📦 Frontier/Shaders/SlangInterchange.h — Slang/C++ Dual-Compile Arithmetic Header
// SlangInterchange.h — C++ dual header for compiling Integration/Shaders/*.slang as C++17.
//
// Include this BEFORE including any dual-compile .slang file from Host/. It
// provides the vector types, the shared builtins, and the keyword macros so
// that the shipped shader source executes verbatim on the CPU for verification.
// Defines SLANG_COMPAT_CXX so Slang-only entry regions (entries, resources) are
// excluded from the C++ translation unit.
#ifndef FRONTIER_SLANG_INTERCHANGE_H
#define FRONTIER_SLANG_INTERCHANGE_H

#define SLANG_COMPAT_CXX 1

#include <algorithm>
#include <cmath>
#include <cstdint>

// Slang keywords that C++ spells differently (or not at all). NOTE: `out` and
// `inout` have NO C++ spelling, so the dual-compile text never uses them —
// multi-value returns use small structs instead (checked by Diagnostics).
#define uniform
#define in

typedef unsigned int uint;

struct float2
{
    float x, y;
    float2() : x(0.0f), y(0.0f) {}
    explicit float2(float s) : x(s), y(s) {}
    float2(float ix, float iy) : x(ix), y(iy) {}
};

struct float3
{
    float x, y, z;
    float3() : x(0.0f), y(0.0f), z(0.0f) {}
    explicit float3(float s) : x(s), y(s), z(s) {}
    float3(float ix, float iy, float iz) : x(ix), y(iy), z(iz) {}
};

struct float4
{
    float x, y, z, w;
    float4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    explicit float4(float s) : x(s), y(s), z(s), w(s) {}
    float4(float ix, float iy, float iz, float iw) : x(ix), y(iy), z(iz), w(iw) {}
    float4(const float3& v, float iw) : x(v.x), y(v.y), z(v.z), w(iw) {}
};

// ---- operators (mirror Slang/HLSL elementwise + splat semantics) ----
inline float3 operator+(const float3& a, const float3& b) { return float3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline float3 operator-(const float3& a, const float3& b) { return float3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline float3 operator*(const float3& a, const float3& b) { return float3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline float3 operator/(const float3& a, const float3& b) { return float3(a.x / b.x, a.y / b.y, a.z / b.z); }
inline float3 operator-(const float3& a) { return float3(-a.x, -a.y, -a.z); }

inline float3 operator+(const float3& a, float s) { return float3(a.x + s, a.y + s, a.z + s); }
inline float3 operator-(const float3& a, float s) { return float3(a.x - s, a.y - s, a.z - s); }
inline float3 operator*(const float3& a, float s) { return float3(a.x * s, a.y * s, a.z * s); }
inline float3 operator/(const float3& a, float s) { return float3(a.x / s, a.y / s, a.z / s); }
inline float3 operator+(float s, const float3& a) { return float3(s + a.x, s + a.y, s + a.z); }
inline float3 operator-(float s, const float3& a) { return float3(s - a.x, s - a.y, s - a.z); }
inline float3 operator*(float s, const float3& a) { return float3(s * a.x, s * a.y, s * a.z); }
inline float3 operator/(float s, const float3& a) { return float3(s / a.x, s / a.y, s / a.z); }

inline float3& operator+=(float3& a, const float3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline float3& operator-=(float3& a, const float3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline float3& operator*=(float3& a, const float3& b) { a.x *= b.x; a.y *= b.y; a.z *= b.z; return a; }
inline float3& operator*=(float3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }

inline float2 operator+(const float2& a, const float2& b) { return float2(a.x + b.x, a.y + b.y); }
inline float2 operator-(const float2& a, const float2& b) { return float2(a.x - b.x, a.y - b.y); }
inline float2 operator*(const float2& a, const float2& b) { return float2(a.x * b.x, a.y * b.y); }
inline float2 operator*(const float2& a, float s) { return float2(a.x * s, a.y * s); }
inline float2 operator*(float s, const float2& a) { return float2(s * a.x, s * a.y); }
inline float2 operator/(const float2& a, float s) { return float2(a.x / s, a.y / s); }
inline float2 operator+(const float2& a, float s) { return float2(a.x + s, a.y + s); }
inline float2 operator-(const float2& a, float s) { return float2(a.x - s, a.y - s); }

inline float4 operator+(const float4& a, const float4& b) { return float4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline float4 operator*(const float4& a, float s) { return float4(a.x * s, a.y * s, a.z * s, a.w * s); }

// ---- scalar builtins live in std; vector overloads are defined here ----
using std::exp;
using std::log;
using std::exp2;
using std::log2;
using std::pow;
using std::sqrt;
using std::sin;
using std::cos;
using std::acos;
using std::asin;
using std::atan2;
using std::floor;
using std::fmod;
using std::min;
using std::max;
using std::abs;
using std::clamp;

inline float3 exp(const float3& v) { return float3(exp(v.x), exp(v.y), exp(v.z)); }
inline float3 log(const float3& v) { return float3(log(v.x), log(v.y), log(v.z)); }
inline float3 sqrt(const float3& v) { return float3(sqrt(v.x), sqrt(v.y), sqrt(v.z)); }
inline float3 pow(const float3& v, float e) { return float3(pow(v.x, e), pow(v.y, e), pow(v.z, e)); }
inline float3 abs(const float3& v) { return float3(abs(v.x), abs(v.y), abs(v.z)); }
inline float3 min(const float3& a, const float3& b) { return float3(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z)); }
inline float3 max(const float3& a, const float3& b) { return float3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z)); }
inline float3 min(const float3& a, float s) { return float3(min(a.x, s), min(a.y, s), min(a.z, s)); }
inline float3 max(const float3& a, float s) { return float3(max(a.x, s), max(a.y, s), max(a.z, s)); }
inline float3 clamp(const float3& v, const float3& lo, const float3& hi)
{
    return float3(clamp(v.x, lo.x, hi.x), clamp(v.y, lo.y, hi.y), clamp(v.z, lo.z, hi.z));
}
inline float3 clamp(const float3& v, float lo, float hi)
{
    return float3(clamp(v.x, lo, hi), clamp(v.y, lo, hi), clamp(v.z, lo, hi));
}

inline float MathFract(float x) { return x - floor(x); }
inline float MathExp2(float x) { return exp2(x); }
inline float MathPow(float x, float y) { return pow(x, y); }
inline float fract(float x) { return x - floor(x); }
inline float3 fract(const float3& v) { return float3(fract(v.x), fract(v.y), fract(v.z)); }

inline float step(float edge, float x) { return x < edge ? 0.0f : 1.0f; }

inline float smoothstep(float e0, float e1, float x)
{
    float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float3 lerp(const float3& a, const float3& b, float t)
{
    return float3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t));
}

inline float saturate(float x) { return clamp(x, 0.0f, 1.0f); }
inline float3 saturate(const float3& v) { return clamp(v, 0.0f, 1.0f); }

inline float dot(const float3& a, const float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float dot(const float2& a, const float2& b) { return a.x * b.x + a.y * b.y; }
inline float3 cross(const float3& a, const float3& b)
{
    return float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float length(const float3& v) { return sqrt(dot(v, v)); }
inline float length(const float2& v) { return sqrt(dot(v, v)); }
inline float3 normalize(const float3& v)
{
    float l = length(v);
    return l > 1e-12f ? v / l : float3(0.0f, 0.0f, 0.0f);
}
inline float2 normalize(const float2& v)
{
    float l = length(v);
    return l > 1e-12f ? v / l : float2(0.0f, 0.0f);
}

static_assert(sizeof(float3) == 12, "float3 must be tightly packed");
static_assert(sizeof(uint) == 4, "uint must be 32-bit");

// ---- int3/uint3 (mirror Slang integer vector semantics; wraparound) ----
struct int3
{
    int x;
    int y;
    int z;
    int3() : x(0), y(0), z(0) {}
    int3(int s) : x(s), y(s), z(s) {}
    int3(int ax, int ay, int az) : x(ax), y(ay), z(az) {}
    explicit int3(const float3& v) : x(int(v.x)), y(int(v.y)), z(int(v.z)) {}
};
struct uint3
{
    unsigned int x;
    unsigned int y;
    unsigned int z;
    uint3() : x(0u), y(0u), z(0u) {}
    uint3(unsigned int s) : x(s), y(s), z(s) {}
    uint3(unsigned int ax, unsigned int ay, unsigned int az) : x(ax), y(ay), z(az) {}
    explicit uint3(const int3& v) : x(unsigned(v.x)), y(unsigned(v.y)), z(unsigned(v.z)) {}
};
inline uint3 operator+(const uint3& a, const uint3& b) { return uint3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline uint3 operator*(const uint3& a, const uint3& b) { return uint3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline uint3 operator*(const uint3& a, unsigned int s) { return uint3(a.x * s, a.y * s, a.z * s); }
inline uint3 operator^(const uint3& a, const uint3& b) { return uint3(a.x ^ b.x, a.y ^ b.y, a.z ^ b.z); }
inline uint3 operator>>(const uint3& a, unsigned int s) { return uint3(a.x >> s, a.y >> s, a.z >> s); }
inline int3 operator+(const int3& a, const int3& b) { return int3(a.x + b.x, a.y + b.y, a.z + b.z); }

#endif // FRONTIER_SLANG_INTERCHANGE_H
