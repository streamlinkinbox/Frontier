// Minimal GLSL/Slang value-type shim so Engine/Shaders/*.slang compile as C++ for port-discipline harnesses.
// Swizzles are member functions: harness build rewrites ".xyz" -> ".xyz()" etc. before inclusion.
#pragma once
#include <cmath>
#include <algorithm>
struct vec2 { float x, y; vec2() : x(0), y(0) {} vec2(float a) : x(a), y(a) {} vec2(float a, float b) : x(a), y(b) {} };
struct vec3 { float x, y, z; vec3() : x(0), y(0), z(0) {} vec3(float a) : x(a), y(a), z(a) {} vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    vec2 xy() const { return {x, y}; } vec2 yz() const { return {y, z}; } vec2 xz() const { return {x, z}; } };
struct vec4 { float x, y, z, w; vec4() : x(0), y(0), z(0), w(0) {} vec4(float a) : x(a), y(a), z(a), w(a) {} vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    vec4(vec3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {} vec3 xyz() const { return {x, y, z}; } vec2 xy() const { return {x, y}; } vec2 yz() const { return {y, z}; } vec2 zw() const { return {z, w}; } };
struct mat3 { vec3 c[3]; mat3() {} mat3(vec3 a, vec3 b, vec3 d) { c[0] = a; c[1] = b; c[2] = d; } };
#define V2OP(op) inline vec2 operator op(vec2 a, vec2 b) { return {a.x op b.x, a.y op b.y}; } inline vec2 operator op(vec2 a, float b) { return {a.x op b, a.y op b}; } inline vec2 operator op(float a, vec2 b) { return {a op b.x, a op b.y}; }
#define V3OP(op) inline vec3 operator op(vec3 a, vec3 b) { return {a.x op b.x, a.y op b.y, a.z op b.z}; } inline vec3 operator op(vec3 a, float b) { return {a.x op b, a.y op b, a.z op b}; } inline vec3 operator op(float a, vec3 b) { return {a op b.x, a op b.y, a op b.z}; }
#define V4OP(op) inline vec4 operator op(vec4 a, vec4 b) { return {a.x op b.x, a.y op b.y, a.z op b.z, a.w op b.w}; } inline vec4 operator op(vec4 a, float b) { return {a.x op b, a.y op b, a.z op b, a.w op b}; } inline vec4 operator op(float a, vec4 b) { return {a op b.x, a op b.y, a op b.z, a op b.w}; }
V2OP(+) V2OP(-) V2OP(*) V2OP(/) V3OP(+) V3OP(-) V3OP(*) V3OP(/) V4OP(+) V4OP(-) V4OP(*) V4OP(/)
inline vec3 operator-(vec3 a) { return {-a.x, -a.y, -a.z}; } inline vec2 operator-(vec2 a) { return {-a.x, -a.y}; }
inline vec3& operator+=(vec3& a, vec3 b) { a = a + b; return a; } inline vec3& operator*=(vec3& a, vec3 b) { a = a * b; return a; } inline vec3& operator*=(vec3& a, float b) { a = a * b; return a; }
inline vec3& operator-=(vec3& a, vec3 b) { a = a - b; return a; } inline vec3& operator/=(vec3& a, float b) { a = a / b; return a; }
inline vec3 operator*(mat3 m, vec3 v) { return m.c[0] * v.x + m.c[1] * v.y + m.c[2] * v.z; }
inline mat3 transpose(mat3 m) { return mat3(vec3(m.c[0].x, m.c[1].x, m.c[2].x), vec3(m.c[0].y, m.c[1].y, m.c[2].y), vec3(m.c[0].z, m.c[1].z, m.c[2].z)); }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; } inline float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(vec3 a) { return std::sqrt(dot(a, a)); } inline float length(vec2 a) { return std::sqrt(dot(a, a)); }
inline vec3 normalize(vec3 a) { return a / length(a); } inline vec2 normalize(vec2 a) { return a / length(a); }
inline vec3 cross(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline vec3 reflect(vec3 i, vec3 n) { return i - 2.0f * dot(n, i) * n; }
inline float inversesqrt(float a) { return 1.0f / std::sqrt(a); }
inline float mix(float a, float b, float t) { return a + (b - a) * t; } inline vec3 mix(vec3 a, vec3 b, float t) { return a + (b - a) * t; } inline vec3 mix(vec3 a, vec3 b, vec3 t) { return a + (b - a) * t; }
inline vec2 mix(vec2 a, vec2 b, float t) { return a + (b - a) * t; }
inline float clamp(float a, float lo, float hi) { return std::min(std::max(a, lo), hi); } inline vec3 clamp(vec3 a, float lo, float hi) { return {clamp(a.x, lo, hi), clamp(a.y, lo, hi), clamp(a.z, lo, hi)}; }
inline vec3 clamp(vec3 a, vec3 lo, vec3 hi) { return {clamp(a.x, lo.x, hi.x), clamp(a.y, lo.y, hi.y), clamp(a.z, lo.z, hi.z)}; }
inline vec2 clamp(vec2 a, float lo, float hi) { return {clamp(a.x, lo, hi), clamp(a.y, lo, hi)}; }
inline float saturate(float a) { return clamp(a, 0.0f, 1.0f); }
inline float smoothstep(float e0, float e1, float x) { float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
inline float sign(float a) { return a > 0 ? 1.0f : (a < 0 ? -1.0f : 0.0f); }
inline float step(float e, float x) { return x < e ? 0.0f : 1.0f; }
using std::sqrt; using std::cos; using std::sin; using std::exp; using std::pow; using std::acos; using std::abs; using std::floor;
inline float max(float a, float b) { return std::max(a, b); } inline float min(float a, float b) { return std::min(a, b); }
inline vec3 max(vec3 a, vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; } inline vec3 max(vec3 a, float b) { return max(a, vec3(b)); }
inline vec3 min(vec3 a, vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; } inline vec3 min(vec3 a, float b) { return min(a, vec3(b)); }
inline vec2 max(vec2 a, float b) { return {std::max(a.x, b), std::max(a.y, b)}; } inline vec2 max(vec2 a, vec2 b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
inline vec3 sqrt(vec3 a) { return {std::sqrt(a.x), std::sqrt(a.y), std::sqrt(a.z)}; } inline vec3 exp(vec3 a) { return {std::exp(a.x), std::exp(a.y), std::exp(a.z)}; }
inline vec3 cos(vec3 a) { return {std::cos(a.x), std::cos(a.y), std::cos(a.z)}; } inline vec3 sin(vec3 a) { return {std::sin(a.x), std::sin(a.y), std::sin(a.z)}; }
inline vec3 pow(vec3 a, vec3 b) { return {std::pow(a.x, b.x), std::pow(a.y, b.y), std::pow(a.z, b.z)}; } inline vec3 pow(vec3 a, float b) { return pow(a, vec3(b)); }
inline vec3 abs(vec3 a) { return {std::abs(a.x), std::abs(a.y), std::abs(a.z)}; }
inline float luminance(vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
// ---- extras for kernel-section ports (ReSTIRViewport.slang ResolveMaterial) ----
#include <vector>
#include <cstring>
typedef unsigned int uint;
struct ivec2 { int x, y; };
struct uvec2 { uint x, y; };
struct uvec4 { uint x, y, z, w; uint& operator[](int i) { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; } uint operator[](int i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; } };
struct mat4 { vec4 c[4]; vec4& operator[](int i) { return c[i]; } const vec4& operator[](int i) const { return c[i]; } };
inline mat3 mat3_from(const mat4& m) { return mat3(m.c[0].xyz(), m.c[1].xyz(), m.c[2].xyz()); }
inline vec3 vec3_from(vec2 a, float b) { return vec3(a.x, a.y, b); }
inline vec4 vec4_from(vec2 a, vec2 b) { return vec4(a.x, a.y, b.x, b.y); }
inline float uintBitsToFloat(uint u) { float f; std::memcpy(&f, &u, 4); return f; }
inline uint floatBitsToUint(float f) { uint u; std::memcpy(&u, &f, 4); return u; }
inline float log2(float a) { return std::log2(a); }
// ---- extras for the spatial-interface port (InterfaceSignedDistance.slang) ----
inline float atan(float y, float x) { return std::atan2(y, x); }   // GLSL's two-argument atan
inline float atan(float x) { return std::atan(x); }
inline vec2 abs(vec2 a) { return {std::abs(a.x), std::abs(a.y)}; }
inline vec2 min(vec2 a, vec2 b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
inline vec2 min(vec2 a, float b) { return {std::min(a.x, b), std::min(a.y, b)}; }
#define nonuniformEXT(x) (x)
struct Sampler { int Width = 1, Height = 1; vec4 Constant = vec4(1); float LastLod = 0; };
inline ivec2 textureSize(Sampler& s, int) { return { s.Width, s.Height }; }
inline vec4 textureLod(Sampler& s, vec2, float lod) { s.LastLod = lod; return s.Constant; }
inline vec3 vec3_from2(float a, vec2 b) { return vec3(a, b.x, b.y); }
inline vec3 yzw_of(vec4 v) { return vec3(v.y, v.z, v.w); }
