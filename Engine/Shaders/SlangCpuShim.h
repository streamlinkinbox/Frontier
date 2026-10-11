//============================================================================================================================================
//                                                      SLANGCPUSHIM.H
//============================================================================================================================================
// 🧩 Dependency-free GLSL-style vector/matrix vocabulary so Engine/Shaders/MaterialEvaluation.slang compiles 1:1 as
//    C++ (FRONTIER_CPU_PORT). Only what the lobe code uses: vec2/3/4, mat3 (column-major), swizzle READS
//    (.xy / .yz / .xyz — the file performs no swizzle writes), component-wise arithmetic, and the builtins below.
//
//    Swizzles are pointer proxies rebound by every constructor; reads on temporaries (SheenSample(...).xyz) are safe
//    because the temporary outlives the full expression. M9 added .r/.g/.b/.a and vec4.rgb for AtrousDenoise.slang's
//    tone map — same proxy discipline, read-only. Float builtins are new overloads (the C library only declares
//    the double versions globally), so double intermediates from un-suffixed literals still resolve to ::sqrt etc.

#pragma once

// Any TU that includes this shim is, by definition, compiling shader text as C++ — so the shim itself turns the
//    port on. Without this, a TU that forgot -DFRONTIER_CPU_PORT (or a build system that dropped it, which is
//    exactly what broke the Windows Project-Zero link of ShaderballExhibit.cpp) compiles MaterialEvaluation.slang's
//    GLSL branch (`layout(binding = 13) uniform sampler2D ...`) as C++ and fails on the first `layout(`.
#ifndef FRONTIER_CPU_PORT
#define FRONTIER_CPU_PORT 1
#endif

#include <cmath>

typedef unsigned int uint;   // M3: the selection/channel table in MaterialEvaluation.slang uses uint (native in GLSL)

struct vec2
{
    float x, y;
    vec2() : x(0.0f), y(0.0f) {}
    vec2(float s) : x(s), y(s) {}
    vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct vec3
{
    float x, y, z;
    struct Read2 { const float* a; const float* b; operator vec2() const { return vec2(*a, *b); } };
    struct Read1 { const float* a; operator float() const { return *a; } };   // M9: the atrous filter's Hdr.r/.g/.b
    Read2 xy, yz;
    Read1 r, g, b;
    vec3() : x(0.0f), y(0.0f), z(0.0f), xy{ &x, &y }, yz{ &y, &z }, r{ &x }, g{ &y }, b{ &z } {}
    vec3(float s) : x(s), y(s), z(s), xy{ &x, &y }, yz{ &y, &z }, r{ &x }, g{ &y }, b{ &z } {}
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_), xy{ &x, &y }, yz{ &y, &z }, r{ &x }, g{ &y }, b{ &z } {}
    vec3(const vec2& a, float b_) : x(a.x), y(a.y), z(b_), xy{ &x, &y }, yz{ &y, &z }, r{ &x }, g{ &y }, b{ &z } {}
    vec3(const vec3& o) : x(o.x), y(o.y), z(o.z), xy{ &x, &y }, yz{ &y, &z }, r{ &x }, g{ &y }, b{ &z } {}
    vec3(vec3&& o) noexcept : x(o.x), y(o.y), z(o.z), xy{ &x, &y }, yz{ &y, &z }, r{ &x }, g{ &y }, b{ &z } {}
    vec3& operator=(const vec3& o) { x = o.x; y = o.y; z = o.z; return *this; }
    vec3& operator=(vec3&& o) noexcept { x = o.x; y = o.y; z = o.z; return *this; }
};

struct vec4
{
    float x, y, z, w;
    // The three-lane view reads as a vec3 and now also writes (the atrous firefly clamp's `TapColour.rgb *= s`):
    //    the pointers are non-const, the write forms mirror GLSL's swizzle-assign, and reading is unchanged.
    struct Read3
    {
        float* a; float* b; float* c;
        operator vec3() const { return vec3(*a, *b, *c); }
        Read3& operator*=(float s) { *a *= s; *b *= s; *c *= s; return *this; }
        Read3& operator=(const vec3& v) { *a = v.x; *b = v.y; *c = v.z; return *this; }
        Read3& operator+=(const vec3& v) { *a += v.x; *b += v.y; *c += v.z; return *this; }
    };
    struct Read1 { const float* a; operator float() const { return *a; } };   // M9: the atrous filter's TapColour.a
    Read3 xyz, rgb;
    Read1 r, g, b, a;
    vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f), xyz{ &x, &y, &z }, rgb{ &x, &y, &z }, r{ &x }, g{ &y }, b{ &z }, a{ &w } {}
    vec4(float s) : x(s), y(s), z(s), w(s), xyz{ &x, &y, &z }, rgb{ &x, &y, &z }, r{ &x }, g{ &y }, b{ &z }, a{ &w } {}
    vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_), xyz{ &x, &y, &z }, rgb{ &x, &y, &z }, r{ &x }, g{ &y }, b{ &z }, a{ &w } {}
    vec4(const vec3& a_, float b_) : x(a_.x), y(a_.y), z(a_.z), w(b_), xyz{ &x, &y, &z }, rgb{ &x, &y, &z }, r{ &x }, g{ &y }, b{ &z }, a{ &w } {}
    vec4(const vec4& o) : x(o.x), y(o.y), z(o.z), w(o.w), xyz{ &x, &y, &z }, rgb{ &x, &y, &z }, r{ &x }, g{ &y }, b{ &z }, a{ &w } {}
    vec4(vec4&& o) noexcept : x(o.x), y(o.y), z(o.z), w(o.w), xyz{ &x, &y, &z }, rgb{ &x, &y, &z }, r{ &x }, g{ &y }, b{ &z }, a{ &w } {}
    vec4& operator=(const vec4& o) { x = o.x; y = o.y; z = o.z; w = o.w; return *this; }
    vec4& operator=(vec4&& o) noexcept { x = o.x; y = o.y; z = o.z; w = o.w; return *this; }
};

struct mat3
{
    vec3 c0, c1, c2;   // columns
    mat3() : c0(1.0f, 0.0f, 0.0f), c1(0.0f, 1.0f, 0.0f), c2(0.0f, 0.0f, 1.0f) {}
    mat3(const vec3& a, const vec3& b, const vec3& c) : c0(a), c1(b), c2(c) {}
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

inline vec2 operator+(vec2 a, vec2 b) { return vec2(a.x + b.x, a.y + b.y); }
inline vec2 operator-(vec2 a, vec2 b) { return vec2(a.x - b.x, a.y - b.y); }
inline vec2 operator*(vec2 a, vec2 b) { return vec2(a.x * b.x, a.y * b.y); }
inline vec2 operator*(vec2 a, float s) { return vec2(a.x * s, a.y * s); }
inline vec2 operator*(float s, vec2 a) { return vec2(s * a.x, s * a.y); }
inline vec2 operator/(vec2 a, float s) { return vec2(a.x / s, a.y / s); }
inline vec2 operator-(vec2 a) { return vec2(-a.x, -a.y); }

inline vec3 operator+(vec3 a, vec3 b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline vec3 operator-(vec3 a, vec3 b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline vec3 operator*(vec3 a, vec3 b) { return vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline vec3 operator*(vec3 a, float s) { return vec3(a.x * s, a.y * s, a.z * s); }
inline vec3 operator*(float s, vec3 a) { return vec3(s * a.x, s * a.y, s * a.z); }
inline vec3 operator/(vec3 a, vec3 b) { return vec3(a.x / b.x, a.y / b.y, a.z / b.z); }
inline vec3 operator/(vec3 a, float s) { return vec3(a.x / s, a.y / s, a.z / s); }
inline vec3 operator-(vec3 a) { return vec3(-a.x, -a.y, -a.z); }
inline vec3& operator+=(vec3& a, vec3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline vec3& operator-=(vec3& a, vec3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline vec3& operator*=(vec3& a, vec3 b) { a.x *= b.x; a.y *= b.y; a.z *= b.z; return a; }
inline vec3& operator*=(vec3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }
inline vec3& operator/=(vec3& a, float s) { a.x /= s; a.y /= s; a.z /= s; return a; }

inline vec3 operator*(const mat3& m, const vec3& v)
{
    return vec3(m.c0.x * v.x + m.c1.x * v.y + m.c2.x * v.z,
                m.c0.y * v.x + m.c1.y * v.y + m.c2.y * v.z,
                m.c0.z * v.x + m.c1.z * v.y + m.c2.z * v.z);
}
inline mat3 transpose(const mat3& m)
{
    return mat3(vec3(m.c0.x, m.c1.x, m.c2.x), vec3(m.c0.y, m.c1.y, m.c2.y), vec3(m.c0.z, m.c1.z, m.c2.z));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        BUILTINS
//------------------------------------------------------------------------------------------------------------------------

inline float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
inline float length(vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline vec2  normalize(vec2 a) { float l = length(a); return l > 0.0f ? a / l : vec2(0.0f); }
inline vec3  normalize(vec3 a) { float l = length(a); return l > 0.0f ? a / l : vec3(0.0f); }
inline vec3  reflect(vec3 i, vec3 n) { return i - 2.0f * dot(i, n) * n; }
inline float inversesqrt(float x) { return 1.0f / std::sqrt(x); }

inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline vec2  mix(vec2 a, vec2 b, float t) { return vec2(mix(a.x, b.x, t), mix(a.y, b.y, t)); }
inline vec3  mix(vec3 a, vec3 b, float t) { return vec3(mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t)); }

inline float clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline vec2  clamp(vec2 x, float lo, float hi)
{
    return vec2(clamp(x.x, lo, hi), clamp(x.y, lo, hi));
}
inline vec3  clamp(vec3 x, vec3 lo, vec3 hi)
{
    return vec3(clamp(x.x, lo.x, hi.x), clamp(x.y, lo.y, hi.y), clamp(x.z, lo.z, hi.z));
}
inline float min(float a, float b) { return a < b ? a : b; }
inline float max(float a, float b) { return a > b ? a : b; }
inline vec2  min(vec2 a, vec2 b) { return vec2(min(a.x, b.x), min(a.y, b.y)); }
inline vec2  max(vec2 a, vec2 b) { return vec2(max(a.x, b.x), max(a.y, b.y)); }
inline vec3  min(vec3 a, vec3 b) { return vec3(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z)); }
inline vec3  max(vec3 a, vec3 b) { return vec3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z)); }
// MSVC's UCRT already defines the global float overloads of abs/sqrt/cos/sin/exp/pow in <cmath> (inline, noexcept,
//    "strengthened") — redefining them here is C2382. glibc declares only the C double versions globally, so the
//    shim must supply the float set there. Same call sites, same semantics either way.
#ifndef _MSC_VER
inline float abs(float x) { return x < 0.0f ? -x : x; }   // M4: |cos| in the transmission half-vector (GLSL abs, 1:1)
#endif
inline vec3  abs(vec3 x) { return vec3(abs(x.x), abs(x.y), abs(x.z)); }
inline vec3  log(vec3 x) { return vec3(std::log(x.x), std::log(x.y), std::log(x.z)); }   // M4: σ = −ln(color)/depth

#ifndef _MSC_VER
inline float sqrt(float x) { return std::sqrt(x); }
#endif
inline vec3  sqrt(vec3 x) { return vec3(std::sqrt(x.x), std::sqrt(x.y), std::sqrt(x.z)); }
#ifndef _MSC_VER
inline float cos(float x) { return std::cos(x); }
#endif
inline vec3  cos(vec3 x) { return vec3(std::cos(x.x), std::cos(x.y), std::cos(x.z)); }
#ifndef _MSC_VER
inline float sin(float x) { return std::sin(x); }   // M2: aniso-basis construction
#endif
inline vec3  cross(vec3 a, vec3 b)   // M2: coat bitangent
{
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
#ifndef _MSC_VER
inline float exp(float x) { return std::exp(x); }
#endif
inline vec3  exp(vec3 x) { return vec3(std::exp(x.x), std::exp(x.y), std::exp(x.z)); }
#ifndef _MSC_VER
inline float pow(float x, float y) { return std::pow(x, y); }
#endif
inline vec3  pow(vec3 x, vec3 y) { return vec3(std::pow(x.x, y.x), std::pow(x.y, y.y), std::pow(x.z, y.z)); }
inline float smoothstep(float e0, float e1, float x)
{
    float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
