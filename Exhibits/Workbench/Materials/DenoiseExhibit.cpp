//============================================================================================================================================
//                                                        DENOISEEXHIBIT.CPP
//============================================================================================================================================
// 🧩 M9 visual exhibit — the shipped à-trous filter and the shipped reprojection rule, run over a small CPU path-traced
//    scene, written out as labelled sheets into Exhibits/Gallery/Materials/. Not part of the materials gate (sheets are
//    seconds-to-a-minute); driven by RunDenoiseExhibit.sh, which stages the shader with the SAME StageAtrousDenoise.py
//    the gate uses and then compiles this harness against the SAME AtrousDenoiseMirror (AtrousDenoise.slang 1:1 as C++).
//
//    What is real here and what is not:
//      · REAL — the filter. Every filtered tile on every sheet comes out of the shader's own compiled `main()`, driven
//        level by level exactly as SwapchainExchange's dispatcher drives it (StepSize = 1 << level, the engine's
//        NormalPower/DepthScale/LuminanceScale, FinalLevel on the last live level, 8×8 workgroup, ping-pong slots).
//      · REAL — the reprojection rule. The temporal sheet calls the shared ReprojectionMirror.h, the same header the
//        gate's §D checks, which is a line-by-line mirror of ReSTIRViewport.slang's ResolveSurface validation.
//      · REAL — the radiance. An analytic scene (ground with a checker, two spheres, a back wall, an area light) is
//        path-traced on the CPU by a one-bounce estimator (cosine-sampled diffuse / a rough-mirror specular lobe, one
//        secondary ray with NEE-free environment lighting). The noise is the estimator's own, not a synthetic model:
//        flat regions, contact shadows, colour bleeding, glossy fireflies and a delta-light all come out of it. The
//        per-pixel variance the filter divides by is computed by the shader's own accumulation recursion
//        (AtrousDenoiseMirror's Accumulator = ResolveSurface's running mean + first two luminance moments).
//      · NOT REAL — the integrator. This is not the ReSTIR kernel: no reservoirs, no reuse, no BVH, one bounce. The
//        sheets are evidence about the FILTER and the REPROJECTION RULE, which is exactly what M9 set out to prove.
//
//    Sheets (all deterministic — every RNG is seeded from (pixel, sample index) and the scene is analytic):
//      denoise-ab.png        reference · raw 1 spp · filtered 1 spp · both error maps ×8 · the variance the filter sees
//      denoise-fade.png      raw / filtered / |filtered − raw| ×16 / early-out mask, at 1, 16, 128, 2048 spp
//      denoise-levels.png    the same noisy frame after 1, 2, 3, 4 and 5 à-trous levels (step 1, 2, 4, 8, 16 px)
//      denoise-edges.png     engine edge-stopping weights vs slack weights, with a 3× crop of the silhouette
//      denoise-reproject.png camera pan: pre-R7a same-pixel history vs R7a reprojection, motion vectors, rejects
//
//    Build (via the driver): g++ -std=c++20 -O2 -DFRONTIER_CPU_PORT -I Exhibits/Workbench/Materials
//        -I Exhibits/Workbench/Editor -I $STAGE DenoiseExhibit.cpp AtrousDenoiseMirror.cpp -o /tmp/DenoiseExhibit

#include "AtrousDenoiseMirror.h"
#include "ReprojectionMirror.h"
#include "DenoiseStreams.h"
#include "PngWriteCounterpart.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

//============================================================================================================================
//  TINY VECTOR + RNG (the scene's own maths; the shim's types belong to the shader, not to this file)
//============================================================================================================================

struct V3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

static inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static inline V3 operator*(V3 a, V3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
static inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 Normalize(V3 a) { const float L = std::sqrt(Dot(a, a)); return L > 0.0f ? a * (1.0f / L) : a; }

struct Pcg
{
    uint32_t State;
    explicit Pcg(uint32_t Seed) : State(Seed * 747796405u + 2891336453u) {}
    uint32_t Next()
    {
        State = State * 747796405u + 2891336453u;
        uint32_t Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
        return (Word >> 22u) ^ Word;
    }
    float Uniform() { return static_cast<float>(Next() >> 8u) * (1.0f / 16777216.0f); }
};

//============================================================================================================================
//  5×7 BITMAP FONT — every sheet is self-describing, so the tiles carry captions instead of a README key
//============================================================================================================================

struct Glyph
{
    char Key;
    const char* Rows[7];
};

static const Glyph kFont[] = {
    { ' ', { ".....", ".....", ".....", ".....", ".....", ".....", "....." } },
    { 'A', { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
    { 'B', { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." } },
    { 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
    { 'D', { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." } },
    { 'E', { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
    { 'F', { "#####", "#....", "#....", "####.", "#....", "#....", "#...." } },
    { 'G', { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###." } },
    { 'H', { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
    { 'I', { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." } },
    { 'J', { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." } },
    { 'K', { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" } },
    { 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
    { 'M', { "#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#" } },
    { 'N', { "#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#" } },
    { 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
    { 'P', { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
    { 'Q', { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" } },
    { 'R', { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
    { 'S', { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
    { 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
    { 'U', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
    { 'V', { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
    { 'W', { "#...#", "#...#", "#...#", "#...#", "#.#.#", "##.##", "#...#" } },
    { 'X', { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
    { 'Y', { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." } },
    { 'Z', { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
    { '0', { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." } },
    { '1', { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." } },
    { '2', { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
    { '3', { "####.", "....#", "....#", ".###.", "....#", "....#", "####." } },
    { '4', { "#..#.", "#..#.", "#..#.", "#####", "...#.", "...#.", "...#." } },
    { '5', { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." } },
    { '6', { ".###.", "#...#", "#....", "####.", "#...#", "#...#", ".###." } },
    { '7', { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." } },
    { '8', { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
    { '9', { ".###.", "#...#", "#...#", ".####", "....#", "#...#", ".###." } },
    { '.', { ".....", ".....", ".....", ".....", ".....", ".##..", ".##.." } },
    { ',', { ".....", ".....", ".....", ".....", ".##..", ".##..", ".#..." } },
    { ':', { ".....", ".##..", ".##..", ".....", ".##..", ".##..", "....." } },
    { '%', { "##..#", "##.#.", "..#..", ".#...", "#..##", "#..##", "....." } },
    { '-', { ".....", ".....", ".....", "#####", ".....", ".....", "....." } },
    { '+', { ".....", "..#..", "..#..", "#####", "..#..", "..#..", "....." } },
    { '/', { "....#", "...#.", "..#..", "..#..", ".#...", "#....", "#...." } },
    { '(', { "...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#." } },
    { ')', { ".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..." } },
    { '=', { ".....", ".....", "#####", ".....", "#####", ".....", "....." } },
    { '<', { "...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#." } },
    { '>', { ".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..." } },
    { '*', { ".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....." } },
    { '|', { "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
};

static const Glyph* FindGlyph(char C)
{
    if (C >= 'a' && C <= 'z') C = static_cast<char>(C - 'a' + 'A');
    for (const Glyph& G : kFont)
        if (G.Key == C) return &G;
    return &kFont[0];
}

//============================================================================================================================
//  SHEET — an 8-bit RGB canvas with blits, crops and text
//============================================================================================================================

struct Sheet
{
    int W = 0, H = 0;
    std::vector<unsigned char> Pixels;   // RGB8

    void Resize(int Width, int Height)
    {
        W = Width; H = Height;
        Pixels.assign(static_cast<size_t>(W) * H * 3u, 0u);
    }
    void Fill(unsigned char R, unsigned char G, unsigned char B)
    {
        for (size_t I = 0; I + 2u < Pixels.size(); I += 3u) { Pixels[I] = R; Pixels[I + 1u] = G; Pixels[I + 2u] = B; }
    }
    void Set(int X, int Y, unsigned char R, unsigned char G, unsigned char B)
    {
        if (X < 0 || Y < 0 || X >= W || Y >= H) return;
        const size_t I = (static_cast<size_t>(Y) * W + X) * 3u;
        Pixels[I] = R; Pixels[I + 1u] = G; Pixels[I + 2u] = B;
    }
    // One tile: `Size` × `Size` display-linear RGB floats (already tone-mapped / already an error map).
    void Blit(int X, int Y, int Size, const std::vector<float>& Rgb)
    {
        for (int TY = 0; TY < Size; ++TY)
            for (int TX = 0; TX < Size; ++TX)
            {
                const size_t I = (static_cast<size_t>(TY) * Size + TX) * 3u;
                const auto Byte = [](float V) { return static_cast<unsigned char>(std::min(std::max(V, 0.0f), 1.0f) * 255.0f + 0.5f); };
                Set(X + TX, Y + TY, Byte(Rgb[I]), Byte(Rgb[I + 1u]), Byte(Rgb[I + 2u]));
            }
    }
    void Rect(int X, int Y, int Width, int Height, unsigned char R, unsigned char G, unsigned char B)
    {
        for (int TY = 0; TY < Height; ++TY)
            for (int TX = 0; TX < Width; ++TX) Set(X + TX, Y + TY, R, G, B);
    }
    // Draws at `PreferredScale` when the string fits the given width, otherwise one step smaller, otherwise clipped.
    void TextFit(int X, int Y, int MaxWidth, int PreferredScale, const char* Text_, unsigned char R, unsigned char G, unsigned char B)
    {
        const int Length = static_cast<int>(std::strlen(Text_));
        int Scale = PreferredScale;
        while (Scale > 1 && Length * 6 * Scale > MaxWidth) --Scale;
        const int Fits = std::max(MaxWidth / (6 * Scale), 1);
        if (Length <= Fits) { Text(X, Y, Scale, Text_, R, G, B); return; }
        std::string Clipped(Text_, static_cast<size_t>(Fits));
        Text(X, Y, Scale, Clipped.c_str(), R, G, B);
    }
    // Greedy word wrap: up to MaxLines lines of `Scale`, each clipped to MaxWidth. Long explanations thereby stay
    //    readable instead of being truncated at whatever happens to fit.
    void TextWrap(int X, int Y, int MaxWidth, int Scale, const char* Text_, unsigned char R, unsigned char G, unsigned char B, int MaxLines)
    {
        const int PerLine = std::max(MaxWidth / (6 * Scale), 4);
        std::string Word, Line;
        int Row = 0;
        std::string Current;
        const std::string All(Text_);
        size_t Index = 0;
        while (Index < All.size() && Row < MaxLines)
        {
            const size_t Space = All.find(' ', Index);
            Word = All.substr(Index, Space == std::string::npos ? std::string::npos : Space - Index);
            Index = Space == std::string::npos ? All.size() : Space + 1;
            if (Current.empty()) Current = Word;
            else if (static_cast<int>(Current.size() + 1 + Word.size()) <= PerLine) Current += " " + Word;
            else
            {
                Text(X, Y + (Row * (7 + 2)) * Scale, Scale, Current.c_str(), R, G, B);
                ++Row;
                Current = Word;
            }
        }
        if (Row < MaxLines && !Current.empty()) Text(X, Y + (Row * (7 + 2)) * Scale, Scale, Current.c_str(), R, G, B);
    }
    void Text(int X, int Y, int Scale, const char* Text_, unsigned char R, unsigned char G, unsigned char B)
    {
        int Cursor = X;
        for (const char* C = Text_; *C; ++C)
        {
            const Glyph* Letter = FindGlyph(*C);
            for (int Row = 0; Row < 7; ++Row)
                for (int Col = 0; Col < 5; ++Col)
                    if (Letter->Rows[Row][Col] != '.')
                        Rect(Cursor + Col * Scale, Y + Row * Scale, Scale, Scale, R, G, B);
            Cursor += 6 * Scale;
        }
    }
    void Line(int X0, int Y, int Length, unsigned char R, unsigned char G, unsigned char B) { Rect(X0, Y, Length, 1, R, G, B); }
};

//============================================================================================================================
//  SCENE — analytic: a checker ground, two spheres, a back wall, an area light; one-bounce path tracing
//============================================================================================================================

struct Ray
{
    V3 Origin, Direction;
};

struct Hit
{
    bool  Surface = false;
    float T = 0.0f;
    V3    Position, Normal;
    int   Material = 0;      // 0 ground · 1 red diffuse sphere · 2 rough-mirror sphere · 3 back wall · 4 light panel
};

struct Camera
{
    V3    Origin = { 0.0f, 0.10f, 0.0f };
    float HalfFovTan = 0.4452f;   // 2·atan(0.4452) ≈ 48°
};

V3 Sky(V3 Direction)
{
    // A GENTLE gradient on purpose: a strongly directional sky would keep even the ambient-only surfaces noisy at
    //    any sample count, and the sheets need a region that genuinely converges so the early-out can be seen working.
    if (Direction.y > 0.0f) return V3{ 0.30f, 0.38f, 0.52f } * (0.72f + 0.14f * Direction.y);
    return V3{ 0.13f, 0.12f, 0.11f };
}

struct Sphere { V3 Centre; float Radius; int Material; };

static const Sphere kSpheres[2] = {
    { { -0.34f, -0.24f, 1.85f }, 0.38f, 1 },   // red diffuse
    { {  0.42f, -0.30f, 1.55f }, 0.32f, 2 },   // rough mirror (fireflies)
};

constexpr float kGroundY   = -0.62f;
constexpr float kWallZ     = 6.00f;
// A wide, soft, downward-facing panel: it subtends a solid angle of roughly a fifth of the hemisphere, so a 1-spp
//    frame looks like a noisy render rather than a firefly storm. The spheres' rough-mirror lobe still produces the
//    occasional very bright sample, and the ground outside the panel's cone is lit by the sky alone — both wanted.
constexpr float kLightMinX = -1.15f, kLightMaxX = 1.15f;
constexpr float kLightMinZ =  0.55f, kLightMaxZ = 1.75f;
constexpr float kLightY    =  1.55f;
constexpr float kLightEmission = 5.5f;

V3 SurfaceAlbedo(V3 Position, int Material)
{
    switch (Material)
    {
        case 0:   // checker: real luminance detail the filter must NOT smooth away
        {
            const int Cell = static_cast<int>(std::floor(Position.x * 2.4f)) + static_cast<int>(std::floor(Position.z * 2.4f));
            const float Value = ((Cell & 1) == 0) ? 0.52f : 0.30f;
            return { Value, Value, Value * 1.02f };
        }
        case 1: return { 0.74f, 0.22f, 0.18f };
        case 2: return { 0.90f, 0.90f, 0.92f };
        case 3: return { 0.70f, 0.71f, 0.72f };
        default: return { 0.0f, 0.0f, 0.0f };
    }
}

Hit TraceRay(const Ray& In)
{
    Hit Best;
    Best.T = 1.0e30f;

    // Ground plane y = kGroundY, |x| and z inside the box (the wall clips the far end).
    if (In.Direction.y < -1.0e-6f)
    {
        const float T = (kGroundY - In.Origin.y) / In.Direction.y;
        if (T > 1.0e-4f && T < Best.T)
        {
            const V3 P = In.Origin + In.Direction * T;
            if (std::fabs(P.x) < 8.0f && P.z > 0.0f && P.z < kWallZ)
            {
                Best.Surface = true; Best.T = T; Best.Position = P; Best.Normal = { 0.0f, 1.0f, 0.0f }; Best.Material = 0;
            }
        }
    }
    // Back wall z = kWallZ.
    if (In.Direction.z > 1.0e-6f)
    {
        const float T = (kWallZ - In.Origin.z) / In.Direction.z;
        if (T > 1.0e-4f && T < Best.T)
        {
            const V3 P = In.Origin + In.Direction * T;
            if (std::fabs(P.x) < 8.0f && P.y < kLightY + 2.0f)
            {
                Best.Surface = true; Best.T = T; Best.Position = P; Best.Normal = { 0.0f, 0.0f, -1.0f }; Best.Material = 3;
            }
        }
    }
    // Spheres.
    for (const Sphere& S : kSpheres)
    {
        const V3 O = In.Origin - S.Centre;
        const float B = Dot(O, In.Direction);
        const float C = Dot(O, O) - S.Radius * S.Radius;
        const float Disc = B * B - C;
        if (Disc <= 0.0f) continue;
        const float Root = std::sqrt(Disc);
        float T = -B - Root;
        if (T < 1.0e-4f) T = -B + Root;
        if (T < 1.0e-4f || T >= Best.T) continue;
        const V3 P = In.Origin + In.Direction * T;
        Best.Surface = true; Best.T = T; Best.Position = P; Best.Normal = Normalize(P - S.Centre); Best.Material = S.Material;
    }
    // Area light: a downward-facing quad, so it is hit from below and shadows what it lights.
    if (In.Direction.y > 1.0e-6f)
    {
        const float T = (kLightY - In.Origin.y) / In.Direction.y;
        if (T > 1.0e-4f && T < Best.T)
        {
            const V3 P = In.Origin + In.Direction * T;
            if (P.x > kLightMinX && P.x < kLightMaxX && P.z > kLightMinZ && P.z < kLightMaxZ)
            {
                Best.Surface = true; Best.T = T; Best.Position = P; Best.Normal = { 0.0f, -1.0f, 0.0f }; Best.Material = 4;
            }
        }
    }
    return Best;
}

V3 CosineHemisphere(V3 Normal, Pcg& Rng)
{
    const float U1 = Rng.Uniform(), U2 = Rng.Uniform();
    const float R = std::sqrt(U1), Phi = 6.2831853f * U2;
    const V3 Helper = std::fabs(Normal.y) < 0.99f ? V3{ 0.0f, 1.0f, 0.0f } : V3{ 1.0f, 0.0f, 0.0f };
    const V3 Tangent = Normalize(V3{ Helper.y * Normal.z - Helper.z * Normal.y,
                                     Helper.z * Normal.x - Helper.x * Normal.z,
                                     Helper.x * Normal.y - Helper.y * Normal.x });
    const V3 Bitangent = V3{ Normal.y * Tangent.z - Normal.z * Tangent.y,
                             Normal.z * Tangent.x - Normal.x * Tangent.z,
                             Normal.x * Tangent.y - Normal.y * Tangent.x };
    return Normalize(Tangent * (R * std::cos(Phi)) + Bitangent * (R * std::sin(Phi)) + Normal * std::sqrt(std::max(0.0f, 1.0f - U1)));
}

// One sample of the estimator: primary hit → one secondary ray. Unbiased by construction (cosine-weighted diffuse;
//    a rough-mirror lobe whose throughput is the reflectance), so its running mean is the reference the sheets compare
//    against. A primary hit on the light panel returns emission regardless of the sampled direction, i.e. exactly zero
//    variance — the "converged" region the early-out is supposed to recognise.
struct PixelSample
{
    V3    Radiance;
    V3    Normal;
    float Depth = -1.0f;
    bool  Surface = false;
};

PixelSample SamplePixel(const Camera& Cam, int X, int Y, int Size, Pcg& Rng)
{
    const float U = (2.0f * (X + 0.5f) / Size - 1.0f) * Cam.HalfFovTan;
    const float V = (1.0f - 2.0f * (Y + 0.5f) / Size) * Cam.HalfFovTan;
    const Ray Primary = { Cam.Origin, Normalize(V3{ U, V, 1.0f }) };

    const Hit H = TraceRay(Primary);
    PixelSample Out;
    if (!H.Surface) { Out.Radiance = Sky(Primary.Direction); return Out; }

    Out.Surface = true;
    Out.Normal = H.Normal;
    Out.Depth = std::sqrt(Dot(H.Position - Cam.Origin, H.Position - Cam.Origin));

    if (H.Material == 4) { Out.Radiance = V3{ kLightEmission, kLightEmission * 0.97f, kLightEmission * 0.92f }; return Out; }

    const V3 Albedo = SurfaceAlbedo(H.Position, H.Material);
    V3 Direction;
    V3 Throughput = Albedo;
    if (H.Material == 2)
    {
        // Rough mirror: the reflection direction jittered by the roughness. Near-grazing samples can land on the light
        //    panel and come back 26× brighter — the fireflies M9's firefly stream also models.
        const V3 Incident = Primary.Direction;
        const V3 Reflected = Incident - H.Normal * (2.0f * Dot(Incident, H.Normal));
        const float Roughness = 0.18f;
        Direction = Normalize(V3{ Reflected.x + Roughness * (Rng.Uniform() * 2.0f - 1.0f),
                                  Reflected.y + Roughness * (Rng.Uniform() * 2.0f - 1.0f),
                                  Reflected.z + Roughness * (Rng.Uniform() * 2.0f - 1.0f) });
        if (Dot(Direction, H.Normal) <= 0.0f) Direction = H.Normal;
        Throughput = Albedo * 0.98f;
    }
    else
    {
        Direction = CosineHemisphere(H.Normal, Rng);
    }

    const Ray Secondary = { H.Position + Direction * 1.0e-3f, Direction };
    const Hit S = TraceRay(Secondary);
    if (!S.Surface) { Out.Radiance = Throughput * Sky(Direction); return Out; }
    if (S.Material == 4) { Out.Radiance = Throughput * V3{ kLightEmission, kLightEmission * 0.97f, kLightEmission * 0.92f }; return Out; }

    // One bounce of ambient: the second hit's albedo times the sky at its normal. Cheap, but it is what puts the
    //    contact darkening under the spheres and the red bleed onto the ground into the image.
    Out.Radiance = Throughput * SurfaceAlbedo(S.Position, S.Material) * Sky(S.Normal) * 1.15f;
    return Out;
}

//============================================================================================================================
//  THE SHIPPED PIPELINE — the accumulator (ResolveSurface) and the dispatcher's level chain
//============================================================================================================================

struct Settings
{
    int   Levels = 5;                 // the Reference tier's chain: StepSize 1, 2, 4, 8, 16
    float NormalPower = 64.0f;        // σn — Engine/DeviceExchange's Push.NormalPower
    float DepthScale = 0.05f;         // σz — Push.DepthScale
    float LuminanceScale = 4.0f;      // σl — Push.LuminanceScale
    float Exposure = 0.85f;
    float ColourSaturation = 1.0f;
    bool  Enabled = true;
};

struct Frame
{
    int Size = 0;
    std::vector<float> Source;        // 4 floats: xyz = accumulated radiance, w = variance of the mean
    std::vector<float> Surface;       // 4 floats: xyz = normal, w = depth (≤ 0 = none)
};

void ToneMapToTile(const std::vector<float>& Radiance, int Size, const Settings& S, std::vector<float>& Out)
{
    Out.resize(static_cast<size_t>(Size) * Size * 3u);
    for (size_t I = 0, P = 0; I + 2u < Radiance.size(); I += 3u, P += 3u)
    {
        float Rgb[3];
        DenoiseMirror::ToneMap(Radiance[I], Radiance[I + 1u], Radiance[I + 2u], S.Exposure, S.ColourSaturation, Rgb);
        Out[P] = Rgb[0]; Out[P + 1u] = Rgb[1]; Out[P + 2u] = Rgb[2];
    }
}

// The whole chain, level by level, exactly as SwapchainExchange's dispatcher walks its descriptor sets: level L taps
//    2^L pixels apart, ping-pongs one of two slots, and only the last live level tone-maps into the presentation image.
void RunChain(const Settings& S, const Frame& In, int Levels, Frame& Out, std::vector<float>& Presentation,
              std::vector<float>* PerLevelPresentation = nullptr)
{
    std::vector<float> A = In.Source, B(In.Source.size(), 0.0f);
    std::vector<float> Output(static_cast<size_t>(In.Size) * In.Size * 4u, 0.0f);
    std::vector<float>* Read = &A;
    std::vector<float>* Write = &B;

    for (int Level = 0; Level < Levels; ++Level)
    {
        const bool Final = (Level == Levels - 1);
        DenoiseMirror::RunConfiguration Config;
        Config.Extent = static_cast<uint32_t>(In.Size);
        Config.StepSize = 1u << Level;
        Config.Enabled = S.Enabled;
        Config.FinalLevel = Final;
        Config.NormalPower = S.NormalPower;
        Config.DepthScale = S.DepthScale;
        Config.LuminanceScale = S.LuminanceScale;
        Config.Exposure = S.Exposure;
        Config.ColourSaturation = S.ColourSaturation;
        DenoiseMirror::Run(Config, Read->data(), In.Surface.data(), Write->data(), Final ? Output.data() : nullptr);
        std::swap(Read, Write);
        if (PerLevelPresentation != nullptr)
        {
            std::vector<float>& Tile = *PerLevelPresentation;
            Tile.resize(static_cast<size_t>(In.Size) * In.Size * 3u);
            for (size_t I = 0, P = 0; I + 2u < Output.size(); I += 4u, P += 3u)
            {
                Tile[P] = Output[I]; Tile[P + 1u] = Output[I + 1u]; Tile[P + 2u] = Output[I + 2u];
            }
        }
    }
    Out.Source = *Read;                       // the last level's target slot is the filtered radiance + variance
    Out.Surface = In.Surface;
    Out.Size = In.Size;
    Presentation.resize(static_cast<size_t>(In.Size) * In.Size * 3u);
    for (size_t I = 0, P = 0; I + 2u < Output.size(); I += 4u, P += 3u)
    {
        Presentation[P] = Output[I]; Presentation[P + 1u] = Output[I + 1u]; Presentation[P + 2u] = Output[I + 2u];
    }
}

// The shipped convergence test, transcribed from AtrousDenoise.slang (3×3 pre-filtered variance < kEarlyOutVariance).
bool EarlyOutAccepts(const Frame& F, int X, int Y)
{
    double VarianceSum = 0.0, VarianceWeight = 0.0;
    for (int OY = -1; OY <= 1; ++OY)
        for (int OX = -1; OX <= 1; ++OX)
        {
            const int TapX = std::min(std::max(X + OX, 0), F.Size - 1);
            const int TapY = std::min(std::max(Y + OY, 0), F.Size - 1);
            const double W = static_cast<double>(DenoiseMirror::KernelWeight(OX)) * static_cast<double>(DenoiseMirror::KernelWeight(OY));
            VarianceSum += static_cast<double>(F.Source[(static_cast<size_t>(TapY) * F.Size + TapX) * 4u + 3u]) * W;
            VarianceWeight += W;
        }
    const double LocalVariance = VarianceWeight > 0.0 ? std::max(VarianceSum / VarianceWeight, 0.0) : 0.0;
    return LocalVariance < static_cast<double>(DenoiseMirror::EarlyOutVariance());
}

//============================================================================================================================
//  TILE BUILDERS
//============================================================================================================================

std::vector<float> ErrorTile(const std::vector<float>& A, const std::vector<float>& B, float Gain)
{
    std::vector<float> Out(A.size());
    for (size_t I = 0; I < A.size(); ++I) Out[I] = std::min(std::fabs(A[I] - B[I]) * Gain, 1.0f);
    return Out;
}

// The same tile with a gain chosen from the data, so the map reads instead of being near-black: the gain puts the
//    99.5th percentile of |difference| at 0.6, and is reported in the caption so the tile is never a mystery.
std::vector<float> ErrorTileAuto(const std::vector<float>& A, const std::vector<float>& B, float* OutGain)
{
    std::vector<float> Differences(A.size());
    for (size_t I = 0; I < A.size(); ++I) Differences[I] = std::fabs(A[I] - B[I]);
    std::vector<float> Sorted = Differences;
    std::sort(Sorted.begin(), Sorted.end());
    const float Percentile = Sorted[static_cast<size_t>(0.995 * (Sorted.size() - 1u))];
    const float Gain = Percentile > 1.0e-8f ? 0.6f / Percentile : 1.0f;
    if (OutGain != nullptr) *OutGain = Gain;
    std::vector<float> Out(A.size());
    for (size_t I = 0; I < A.size(); ++I) Out[I] = std::min(Differences[I] * Gain, 1.0f);
    return Out;
}

std::vector<float> MaskTile(const Frame& F, bool WantAccepted, int& OutAccepted, int& OutSurfacePixels)
{
    const size_t Pixels = static_cast<size_t>(F.Size) * F.Size;
    std::vector<float> Out(Pixels * 3u, 0.0f);
    OutAccepted = 0; OutSurfacePixels = 0;
    for (int Y = 0; Y < F.Size; ++Y)
        for (int X = 0; X < F.Size; ++X)
        {
            const size_t P = static_cast<size_t>(Y) * F.Size + X;
            const float Depth = F.Surface[P * 4u + 3u];
            const bool Accepted = EarlyOutAccepts(F, X, Y);
            if (Accepted) ++OutAccepted;
            if (Depth > 0.0f) ++OutSurfacePixels;
            float Value = 0.0f;                           // black = the filter ran here
            if (Depth <= 0.0f) Value = 0.16f;             // grey = no surface (passes through, never filtered)
            else if (Accepted == WantAccepted) Value = 1.0f;
            Out[P * 3u] = Value; Out[P * 3u + 1u] = Value; Out[P * 3u + 2u] = Value;
        }
    return Out;
}

std::vector<float> VarianceTile(const Frame& F, float FloorValue, float CeilValue)
{
    const size_t Pixels = static_cast<size_t>(F.Size) * F.Size;
    std::vector<float> Out(Pixels * 3u, 0.0f);
    const float LogFloor = std::log10(FloorValue), LogCeil = std::log10(CeilValue);
    for (size_t P = 0; P < Pixels; ++P)
    {
        const float V = std::max(F.Source[P * 4u + 3u], FloorValue);
        const float T = std::min(std::max((std::log10(V) - LogFloor) / (LogCeil - LogFloor), 0.0f), 1.0f);
        Out[P * 3u] = T;                       // blue → red, so a log-scale heat tile reads without a legend
        Out[P * 3u + 1u] = T * 0.35f;
        Out[P * 3u + 2u] = 1.0f - T;
    }
    return Out;
}

// A ×Zoom nearest-neighbour crop, so a silhouette can be inspected at pixel scale.
std::vector<float> Crop(const std::vector<float>& Tile, int Size, int CentreX, int CentreY, int Span, int Zoom)
{
    const int OutSize = Span * Zoom;
    std::vector<float> Out(static_cast<size_t>(OutSize) * OutSize * 3u, 0.0f);
    for (int Y = 0; Y < OutSize; ++Y)
        for (int X = 0; X < OutSize; ++X)
        {
            const int SX = std::min(std::max(CentreX - Span / 2 + X / Zoom, 0), Size - 1);
            const int SY = std::min(std::max(CentreY - Span / 2 + Y / Zoom, 0), Size - 1);
            const size_t S = (static_cast<size_t>(SY) * Size + SX) * 3u;
            const size_t D = (static_cast<size_t>(Y) * OutSize + X) * 3u;
            Out[D] = Tile[S]; Out[D + 1u] = Tile[S + 1u]; Out[D + 2u] = Tile[S + 2u];
        }
    return Out;
}

//============================================================================================================================
//  THE ACCUMULATION PASS — snapshots at 1 / 16 / 128 / RefSpp, each filtered by the shipped chain
//============================================================================================================================

struct Snapshot
{
    int Spp = 0;
    Frame Raw;                       // accumulated radiance + variance, and the surface
    std::vector<float> RawTile;      // tone-mapped (what the kernel writes with the denoise bit clear)
    std::vector<float> FilteredTile;
    Frame Filtered;
    int Accepted = 0, SurfacePixels = 0;
    int AcceptedInQuiet = 0, QuietPixels = 0;   // the early-out's job is on the calm half of the frame
};

void AccumulateAndSnapshot(int Size, const Settings& S, const std::vector<int>& SppMarks, std::vector<Snapshot>& Out)
{
    const size_t Pixels = static_cast<size_t>(Size) * Size;
    std::vector<DenoiseMirror::Accumulator> State(Pixels);
    std::vector<float> Surface(Pixels * 4u, 0.0f);
    std::vector<Snapshot> BySpp(SppMarks.size());
    const Camera Cam;

    std::vector<float> MinVariance(SppMarks.size(), 1.0e30f), MaxVariance(SppMarks.size(), 0.0f);
    const int FinalSpp = SppMarks.empty() ? 0 : *std::max_element(SppMarks.begin(), SppMarks.end());
    for (int SampleIndex = 0; SampleIndex < FinalSpp; ++SampleIndex)
    {
        std::vector<PixelSample> Sampled(Pixels);
        unsigned Threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
        std::vector<std::thread> Pool;
        for (unsigned T = 0; T < Threads; ++T)
            Pool.emplace_back([&, T]() {
                for (size_t P = T; P < Pixels; P += Threads)
                {
                    const int X = static_cast<int>(P % Size), Y = static_cast<int>(P / Size);
                    Pcg Rng(0x5EED1234u ^ (static_cast<uint32_t>(SampleIndex) * 2654435761u) ^ (static_cast<uint32_t>(P) * 40503u));
                    Sampled[P] = SamplePixel(Cam, X, Y, Size, Rng);
                }
            });
        for (std::thread& Worker : Pool) Worker.join();

        for (size_t P = 0; P < Pixels; ++P)
        {
            float Radiance[3] = { Sampled[P].Radiance.x, Sampled[P].Radiance.y, Sampled[P].Radiance.z };
            float Mean[3], Variance = 0.0f;
            State[P].Resolve(Radiance, Mean, &Variance);
            if (Sampled[P].Surface)
            {
                Surface[P * 4u] = Sampled[P].Normal.x;
                Surface[P * 4u + 1u] = Sampled[P].Normal.y;
                Surface[P * 4u + 2u] = Sampled[P].Normal.z;
                Surface[P * 4u + 3u] = Sampled[P].Depth;
            }
            else
            {
                Surface[P * 4u + 3u] = -1.0f;
            }
        }

        for (size_t M = 0; M < SppMarks.size(); ++M)
        {
            if (SampleIndex + 1 != SppMarks[M]) continue;
            Snapshot& Snap = BySpp[M];
            Snap.Spp = SampleIndex + 1;
            Snap.Raw.Size = Size;
            Snap.Raw.Source.assign(Pixels * 4u, 0.0f);
            Snap.Raw.Surface = Surface;
            for (size_t P = 0; P < Pixels; ++P)
            {
                Snap.Raw.Source[P * 4u] = State[P].Mean[0];
                Snap.Raw.Source[P * 4u + 1u] = State[P].Mean[1];
                Snap.Raw.Source[P * 4u + 2u] = State[P].Mean[2];
                const float Variance = State[P].Moments[1] - State[P].Moments[0] * State[P].Moments[0];
                Snap.Raw.Source[P * 4u + 3u] = State[P].Count < 2.0f
                                             ? State[P].Mean[0] * State[P].Mean[0]
                                             : std::max(Variance, 0.0f) / State[P].Count;
                MinVariance[M] = std::min(MinVariance[M], Snap.Raw.Source[P * 4u + 3u]);
                MaxVariance[M] = std::max(MaxVariance[M], Snap.Raw.Source[P * 4u + 3u]);
            }
            std::vector<float> RawRadiance(Pixels * 3u);
            for (size_t P = 0; P < Pixels; ++P)
                for (int C = 0; C < 3; ++C) RawRadiance[P * 3u + C] = Snap.Raw.Source[P * 4u + C];
            ToneMapToTile(RawRadiance, Size, S, Snap.RawTile);
            RunChain(S, Snap.Raw, S.Levels, Snap.Filtered, Snap.FilteredTile);
            std::vector<float> Mask = MaskTile(Snap.Raw, true, Snap.Accepted, Snap.SurfacePixels);
            (void)Mask;   // built here so the printed percentage and the sheet's mask tile agree by construction
            for (int Y = 0; Y < Size; ++Y)
                for (int X = 0; X < Size; ++X)
                {
                    const size_t P = static_cast<size_t>(Y) * Size + X;
                    const float Depth = Snap.Raw.Surface[P * 4u + 3u];
                    const bool Quiet = Depth > 0.0f &&
                        (Snap.Raw.Source[P * 4u + 3u] > 0.0f) &&
                        (X < Size / 3);   // printed as "left third", the region the scene keeps calm
                    if (!Quiet) continue;
                    ++Snap.QuietPixels;
                    if (EarlyOutAccepts(Snap.Raw, X, Y)) ++Snap.AcceptedInQuiet;
                }
            std::printf("[exhibit] snapshot %6d spp: variance %.3g .. %.3g, early-out accepts %5.2f%% of the frame (%5.2f%% of its %zu surface pixels)\n",
                        Snap.Spp, MinVariance[M], MaxVariance[M], 100.0 * Snap.Accepted / static_cast<double>(Pixels),
                        100.0 * Snap.Accepted / std::max(Snap.SurfacePixels, 1), static_cast<size_t>(Snap.SurfacePixels));
        }
    }
    Out = BySpp;
}

//============================================================================================================================
//  SHEETS
//============================================================================================================================

constexpr int kMargin = 12, kCaption = 22, kGap = 8;

int RunAbSheet(const char* OutPath, int Size, const std::vector<Snapshot>& Snapshots)
{
    const Snapshot& Final = Snapshots.back();
    const Snapshot& First = Snapshots.front();
    const int Tile = Size;
    const int SheetW = 3 * Tile + 4 * kMargin;
    const int SheetH = 2 * (Tile + kCaption + kGap) + 3 * kMargin + 2 * kCaption;
    Sheet Canvas;
    Canvas.Resize(SheetW, SheetH);
    Canvas.Fill(16, 16, 18);

    const int Col[3] = { kMargin, 2 * kMargin + Tile, 3 * kMargin + 2 * Tile };
    const int Row[2] = { kMargin, 2 * kMargin + Tile + kCaption + kGap };
    char Label[160];

    Canvas.Blit(Col[0], Row[0], Tile, Final.RawTile);
    std::snprintf(Label, sizeof(Label), "REFERENCE %d SPP", Final.Spp);
    Canvas.TextFit(Col[0], Row[0] + Tile + 6, Tile, 2, Label, 190, 190, 195);

    Canvas.Blit(Col[1], Row[0], Tile, First.RawTile);
    Canvas.TextFit(Col[1], Row[0] + Tile + 6, Tile, 2, "RAW 1 SPP, DENOISE OFF", 190, 190, 195);

    Canvas.Blit(Col[2], Row[0], Tile, First.FilteredTile);
    Canvas.TextFit(Col[2], Row[0] + Tile + 6, Tile, 2, "FILTERED 1 SPP, 5 LEVELS", 190, 190, 195);

    float RawGain = 1.0f, FilteredGain = 1.0f;
    Canvas.Blit(Col[0], Row[1], Tile, ErrorTileAuto(First.RawTile, Final.RawTile, &RawGain));
    std::snprintf(Label, sizeof(Label), "|RAW - REF| X%.0f", RawGain);
    Canvas.TextFit(Col[0], Row[1] + Tile + 6, Tile, 2, Label, 190, 190, 195);

    Canvas.Blit(Col[1], Row[1], Tile, ErrorTileAuto(First.FilteredTile, Final.RawTile, &FilteredGain));
    std::snprintf(Label, sizeof(Label), "|FILTERED - REF| X%.0f", FilteredGain);
    Canvas.TextFit(Col[1], Row[1] + Tile + 6, Tile, 2, Label, 190, 190, 195);

    Canvas.Blit(Col[2], Row[1], Tile, VarianceTile(First.Raw, 1.0e-6f, 1.0f));
    std::snprintf(Label, sizeof(Label), "VARIANCE OF THE MEAN, BLUE %.0e .. RED %.0e", 1.0e-6, 1.0);
    Canvas.TextFit(Col[2], Row[1] + Tile + 6, Tile, 2, Label, 190, 190, 195);

    double RawError = 0.0, FilteredError = 0.0;
    for (size_t I = 0; I < First.RawTile.size(); ++I)
    {
        RawError += std::fabs(First.RawTile[I] - Final.RawTile[I]);
        FilteredError += std::fabs(First.FilteredTile[I] - Final.RawTile[I]);
    }
    std::snprintf(Label, sizeof(Label), "MEAN |ERROR| VS REFERENCE: RAW %.4f, FILTERED %.4f (%.1f%% REMOVED) - SHIPPED A-TROUS SHADER, CPU PORT",
                  RawError / First.RawTile.size(), FilteredError / First.FilteredTile.size(),
                  100.0 * (1.0 - FilteredError / std::max(RawError, 1.0e-12)));
    Canvas.TextWrap(kMargin, SheetH - 2 * kCaption - kMargin, SheetW - 2 * kMargin, 1, Label, 235, 225, 140, 2);
    std::printf("[exhibit] ab: mean |error| raw %.5f -> filtered %.5f (%.1f %% removed)\n",
                RawError / First.RawTile.size(), FilteredError / First.FilteredTile.size(),
                100.0 * (1.0 - FilteredError / std::max(RawError, 1.0e-12)));

    return PngWriteCounterpart::WritePng(OutPath, Canvas.W, Canvas.H, 3, Canvas.Pixels.data(), Canvas.W * 3);
}

int RunFadeSheet(const char* OutPath, int Size, const std::vector<Snapshot>& Snapshots)
{
    const int Tile = Size;
    const int Cols = static_cast<int>(Snapshots.size());
    const int SheetW = Cols * Tile + (Cols + 1) * kMargin;
    const int SheetH = 4 * (Tile + kCaption + kGap) + 3 * kMargin + 4 * kCaption;
    Sheet Canvas;
    Canvas.Resize(SheetW, SheetH);
    Canvas.Fill(16, 16, 18);
    char Label[192];

    for (int C = 0; C < Cols; ++C)
    {
        const Snapshot& Snap = Snapshots[C];
        const int X = kMargin + C * (Tile + kMargin);
        // Row y-positions, one caption band apart, so every caption sits under its own tile and nowhere else.
        const int Rows[4] = { kMargin,
                              kMargin + (Tile + kCaption + kGap),
                              kMargin + 2 * (Tile + kCaption + kGap),
                              kMargin + 3 * (Tile + kCaption + kGap) };
        Canvas.Blit(X, Rows[0], Tile, Snap.RawTile);
        Canvas.Blit(X, Rows[1], Tile, Snap.FilteredTile);
        Canvas.Blit(X, Rows[2], Tile, ErrorTile(Snap.FilteredTile, Snap.RawTile, 16.0f));
        int Accepted = 0, SurfacePixels = 0;
        Canvas.Blit(X, Rows[3], Tile, MaskTile(Snap.Raw, true, Accepted, SurfacePixels));

        std::snprintf(Label, sizeof(Label), "%d SPP, RAW", Snap.Spp);
        Canvas.TextFit(X, Rows[0] + Tile + 6, Tile, 1, Label, 235, 225, 140);
        Canvas.TextFit(X, Rows[1] + Tile + 6, Tile, 2, "FILTERED", 190, 190, 195);
        Canvas.TextFit(X, Rows[2] + Tile + 6, Tile, 2, "|F - R| X16", 190, 190, 195);
        std::snprintf(Label, sizeof(Label), "EARLY-OUT %d%%", static_cast<int>(100.0 * Accepted / (Tile * static_cast<double>(Tile)) + 0.5));
        Canvas.TextFit(X, Rows[3] + Tile + 6, Tile, 2, Label, 190, 190, 195);
    }

    Canvas.TextWrap(kMargin, SheetH - 2 * kCaption - kMargin, SheetW - 2 * kMargin, 1,
                    "WHITE = THE SHIPPED EARLY-OUT RETURNED THE PIXEL UNTOUCHED. BLACK = THE FILTER RAN. GREY = NO SURFACE (NEVER FILTERED).",
                    190, 190, 195, 2);
    return PngWriteCounterpart::WritePng(OutPath, Canvas.W, Canvas.H, 3, Canvas.Pixels.data(), Canvas.W * 3);
}

int RunLevelsSheet(const char* OutPath, int Size, const Settings& S, const std::vector<Snapshot>& Snapshots)
{
    const Frame& Raw = Snapshots.front().Raw;
    const int Tile = Size;
    const int SheetW = 3 * Tile + 4 * kMargin;
    const int SheetH = 2 * (Tile + kCaption + kGap) + 3 * kMargin;
    Sheet Canvas;
    Canvas.Resize(SheetW, SheetH);
    Canvas.Fill(16, 16, 18);
    char Label[160];

    std::vector<float> RawTile = Snapshots.front().RawTile;
    const int Col[3] = { kMargin, 2 * kMargin + Tile, 3 * kMargin + 2 * Tile };
    const int Row[2] = { kMargin, 2 * kMargin + Tile + kCaption + kGap };

    Canvas.Blit(Col[0], Row[0], Tile, RawTile);
    Canvas.TextFit(Col[0], Row[0] + Tile + 6, Tile, 2, "INPUT 1 SPP, NO LEVELS", 190, 190, 195);

    std::vector<float> PerLevel;
    Frame Scratch;
    for (int Levels = 1; Levels <= S.Levels; ++Levels)
    {
        RunChain(S, Raw, Levels, Scratch, PerLevel);
        const int Index = Levels - 1;
        const int X = Col[(Index + 1) % 3], Y = (Index + 1) < 3 ? Row[0] : Row[1];
        Canvas.Blit(X, Y, Tile, PerLevel);
        std::snprintf(Label, sizeof(Label), "AFTER %d LEVEL%s (STEP %d PX)", Levels, Levels == 1 ? "" : "S", 1 << (Levels - 1));
        Canvas.TextFit(X, Y + Tile + 6, Tile, 2, Label, 190, 190, 195);
    }
    Canvas.TextWrap(kMargin, SheetH - 2 * kCaption - kMargin, SheetW - 2 * kMargin, 1,
                    "EACH LEVEL IS ONE DISPATCH OF THE SHIPPED FILTER (STEP 1, 2, 4, 8, 16 PX), PING-PONGED BETWEEN TWO SLOTS AND TONE-MAPPED AT ITS OWN END. THE WIDEST LEVELS TRADE FINE SPECKLE FOR COARSER BLOTCHES, WHICH IS WHY THE TIER LADDER TIES 5 LEVELS TO ULTRA/REFERENCE AND 4 TO THE REST.",
                    235, 225, 140, 2);
    return PngWriteCounterpart::WritePng(OutPath, Canvas.W, Canvas.H, 3, Canvas.Pixels.data(), Canvas.W * 3);
}

int RunStreamsSheet(const char* OutPath)
{
    // The §E measurement, drawn instead of tabulated — and it is the SAME measurement: MeasureStream is the function
    //    the gate's E1-E4c checks call, so these bars and those checks cannot disagree. Top row: presentation MSE at
    //    one sample, raw against filtered (bars normalised to the raw bar, values printed). Bottom row: how much of
    //    the frame the shipped early-out accepts at 512 / 2048 / 8192 accumulated samples — the fade-out curve.
    const uint32_t Holds[3] = { 512u, 2048u, 8192u };
    constexpr uint32_t kExtent = 48u;   // small on purpose: this sheet is statistics, not imagery
    DenoiseStreams::StreamMeasurement Measured[3];
    for (uint32_t I = 0; I < 3u; ++I)
        Measured[I] = DenoiseStreams::MeasureStream(static_cast<DenoiseStreams::StreamCategory>(I), kExtent, Holds[2], Holds);

    const int PanelW = 300, PanelH = 272;
    const int SheetW = 3 * PanelW + 4 * kMargin;
    Sheet Canvas;
    Canvas.Resize(SheetW, 1000);   // resized to the measured content below
    Canvas.Fill(16, 16, 18);

    const int FootnoteLines = 5;
    const int FootnoteHeight = FootnoteLines * 9 + kCaption;
    const int PanelTop = kMargin;
    const int PanelBottom = PanelTop + 2 * PanelH + kCaption + kGap + 4;
    const int SheetH = PanelBottom + FootnoteHeight + kMargin;
    Canvas.Resize(SheetW, SheetH);
    Canvas.Fill(16, 16, 18);

    char Label[224];
    for (int I = 0; I < 3; ++I)
    {
        const int X0 = kMargin + I * (PanelW + kMargin);
        const int ChartLeft = X0 + 30;
        const int ChartRight = X0 + PanelW - 10;
        const int ChartW = ChartRight - ChartLeft;

        std::snprintf(Label, sizeof(Label), "%s", DenoiseStreams::StreamName(static_cast<DenoiseStreams::StreamCategory>(I)));
        Canvas.TextFit(X0, PanelTop, PanelW, 2, Label, 235, 225, 140);

        //---- chart A: presentation MSE at one sample, raw vs filtered ----------------------------------------------
        const double MseRaw = Measured[I].FirstFrameUnfilteredError / (static_cast<double>(kExtent) * kExtent * 3.0);
        const double MseFiltered = Measured[I].FirstFrameFilteredError / (static_cast<double>(kExtent) * kExtent * 3.0);
        const int A_Top = PanelTop + 34, A_Bottom = PanelTop + PanelH - 34;
        const int A_H = A_Bottom - A_Top;
        Canvas.Line(ChartLeft, A_Bottom, ChartW, 70, 70, 76);
        Canvas.Line(ChartLeft, A_Top, 1, 70, 70, 76);
        // The bars start at the left edge of the chart, not centred: a bar chart with two rather than three columns.
        const int BarW = ChartW / 4;
        Canvas.Rect(ChartLeft + ChartW / 6, A_Bottom - A_H, BarW, A_H, 120, 124, 132);
        const int FilteredH = static_cast<int>(A_H * std::min(MseFiltered / std::max(MseRaw, 1.0e-12), 1.0) + 0.5);
        Canvas.Rect(ChartLeft + ChartW / 6 + BarW, A_Bottom - std::max(FilteredH, 2), BarW, std::max(FilteredH, 2), 235, 180, 90);
        std::snprintf(Label, sizeof(Label), "RAW %.3g", MseRaw);
        Canvas.TextFit(ChartLeft, A_Bottom + 4, ChartW / 2, 1, Label, 170, 172, 180);
        std::snprintf(Label, sizeof(Label), "%.3g %.0f%% LESS", MseFiltered, 100.0 * (1.0 - MseFiltered / std::max(MseRaw, 1.0e-12)));
        Canvas.TextFit(ChartLeft + ChartW / 2, A_Bottom + 4, ChartW / 2, 1, Label, 235, 180, 90);
        std::snprintf(Label, sizeof(Label), "PRESENTATION MSE, 1 SAMPLE, RAW VS FILTERED");
        Canvas.TextFit(ChartLeft, PanelTop + 16, ChartW, 1, Label, 190, 190, 195);

        //---- chart B: the early-out's fade-out curve ----------------------------------------------------------------
        const int B_Top = PanelTop + PanelH + kCaption + kGap + 30, B_Bottom = B_Top + PanelH - 48;
        const int B_H = B_Bottom - B_Top;
        Canvas.Line(ChartLeft, B_Bottom, ChartW, 70, 70, 76);
        Canvas.Line(ChartLeft, B_Top, 1, 70, 70, 76);
        for (int G = 1; G <= 4; ++G)
        {
            Canvas.Line(ChartLeft, B_Bottom - G * B_H / 4, ChartW, 34, 34, 38);
            if (G % 2 == 0)
            {
                std::snprintf(Label, sizeof(Label), "%d%%", G * 25);
                Canvas.TextFit(ChartLeft - 28, B_Bottom - G * B_H / 4 - 3, 26, 1, Label, 120, 120, 126);
            }
        }
        const int WideBar = ChartW / 5;
        for (int H = 0; H < 3; ++H)
        {
            const double Fraction = Measured[I].MilestoneAcceptance[H];
            const int Height = std::max(static_cast<int>(B_H * Fraction + 0.5), 2);
            const int X = ChartLeft + (3 * H + 1) * ChartW / 9;
            Canvas.Rect(X, B_Bottom - Height, WideBar, Height, 120, 190, 235);
            std::snprintf(Label, sizeof(Label), "%u", Holds[H]);
            Canvas.TextFit(X - 6, B_Bottom + 4, WideBar + 12, 1, Label, 170, 172, 180);
            std::snprintf(Label, sizeof(Label), "%.0f%%", 100.0 * Fraction);
            Canvas.TextFit(X - 6, B_Bottom + 13, WideBar + 14, 1, Label, 120, 190, 235);
        }
        Canvas.TextFit(ChartLeft, PanelTop + PanelH + kCaption + kGap + 14, ChartW, 1, "EARLY-OUT ACCEPTANCE BY HOLD (SPP)", 190, 190, 195);
    }

    Canvas.TextWrap(kMargin, PanelBottom, SheetW - 2 * kMargin, 1,
                    "MEASUREMENT, NOT ILLUSTRATION: THESE BARS COME FROM THE SAME MeasureStream THE GATE'S E1-E4c CHECKS CALL (HERE 48x48, 1 SPP PER FRAME, THE SHADER'S OWN ACCUMULATOR AND FILTER), SO THE TWO CANNOT DISAGREE. "
                    "BEFORE CONVERGENCE THE FILTER LOWERS PRESENTATION MSE ON ALL THREE LOBE-LIKE STREAMS; AS THE ESTIMATE SETTLES THE SHIPPED EARLY-OUT TAKES MORE OF THE FRAME OUT OF ITS HANDS. "
                    "THE 1% FIREFLY STREAM IS THE SLOW ONE, HONESTLY: STILL FILTERED AT 512 AND 2048 SPP BECAUSE IT HAS NOT CONVERGED THERE (36X THE DIFFUSE PER-SAMPLE VARIANCE), AND ONLY PAST THE 8192 HOLD DOES THE SHADER'S OWN TEST TAKE OVER. "
                    "THE OTHER HALF OF THE A/B - BIT-IDENTICAL AT CONVERGENCE - IS ON THE IMAGE SHEETS, AND IN ALL NINE CELLS OF THE GATE'S E4: ZERO PIXELS DIFFER.",
                    235, 225, 140, FootnoteLines);

    for (int I = 0; I < 3; ++I)
        std::printf("[exhibit] streams %-12s: MSE 1 spp raw %.4g -> filtered %.4g; acceptance %.1f%% / %.1f%% / %.1f%%\n",
                    DenoiseStreams::StreamName(static_cast<DenoiseStreams::StreamCategory>(I)),
                    Measured[I].FirstFrameUnfilteredError / (static_cast<double>(kExtent) * kExtent * 3.0),
                    Measured[I].FirstFrameFilteredError / (static_cast<double>(kExtent) * kExtent * 3.0),
                    100.0 * Measured[I].MilestoneAcceptance[0], 100.0 * Measured[I].MilestoneAcceptance[1],
                    100.0 * Measured[I].MilestoneAcceptance[2]);
    return PngWriteCounterpart::WritePng(OutPath, Canvas.W, Canvas.H, 3, Canvas.Pixels.data(), Canvas.W * 3);
}

int RunIdentitySheet(const char* OutPath, int Size, const Settings& S, const std::vector<Snapshot>& Snapshots)
{
    // The visual counterpart of the gate's E4: the converged frame, with and without the filter, and the map of the
    //    pixels the shipped early-out took out of the filter's hands. Only the first two tiles use the filter; the
    //    third is their difference at a gain chosen from the data, so "near-black" is a measurement, not an impression.
    const Snapshot& Snap = Snapshots.back();
    const int Tile = Size;
    const int SheetW = 3 * Tile + 4 * kMargin;
    const int SheetH = 2 * (Tile + kCaption + kGap) + 3 * kMargin;
    Sheet Canvas;
    Canvas.Resize(SheetW, SheetH);
    Canvas.Fill(16, 16, 18);
    const int Col[3] = { kMargin, 2 * kMargin + Tile, 3 * kMargin + 2 * Tile };
    const int Row[2] = { kMargin, 2 * kMargin + Tile + kCaption + kGap };
    char Label[224];

    float Gain = 1.0f;
    std::vector<float> Difference = ErrorTileAuto(Snap.FilteredTile, Snap.RawTile, &Gain);

    double Worst = 0.0;
    int WorstX = 0, WorstY = 0, AcceptedBad = 0, Accepted = 0, Surfaces = 0;
    for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const size_t I = (static_cast<size_t>(Y) * Size + X) * 3u;
            double Delta = 0.0;
            for (int C = 0; C < 3; ++C) Delta = std::max(Delta, std::fabs(static_cast<double>(Snap.FilteredTile[I + C]) -
                                                                            static_cast<double>(Snap.RawTile[I + C])));
            if (Delta > Worst) { Worst = Delta; WorstX = X; WorstY = Y; }
            const bool Surface = Snap.Raw.Surface[(static_cast<size_t>(Y) * Size + X) * 4u + 3u] > 0.0f;
            if (!Surface) continue;
            ++Surfaces;
            if (!EarlyOutAccepts(Snap.Raw, X, Y)) continue;
            ++Accepted;
            if (Delta > 0.0) ++AcceptedBad;
        }

    Canvas.Blit(Col[0], Row[0], Tile, Snap.RawTile);
    std::snprintf(Label, sizeof(Label), "%d SPP, DENOISE OFF", Snap.Spp);
    Canvas.TextFit(Col[0], Row[0] + Tile + 6, Tile, 2, Label, 190, 190, 195);

    Canvas.Blit(Col[1], Row[0], Tile, Snap.FilteredTile);
    std::snprintf(Label, sizeof(Label), "%d SPP, DENOISE ON", Snap.Spp);
    Canvas.TextFit(Col[1], Row[0] + Tile + 6, Tile, 2, Label, 190, 190, 195);

    Canvas.Blit(Col[2], Row[0], Tile, Difference);
    std::snprintf(Label, sizeof(Label), "|ON - OFF| X%.0f, WORST PIXEL %.4f", Gain, Worst);
    Canvas.TextFit(Col[2], Row[0] + Tile + 6, Tile, 2, Label, 235, 225, 140);

    const int Span = std::max(Tile / 8, 8), Zoom = 4;
    Canvas.Blit(Col[0], Row[1], Span * Zoom, Crop(Snap.RawTile, Size, WorstX, WorstY, Span, Zoom));
    Canvas.TextFit(Col[0], Row[1] + Tile + 6, Tile, 2, "4X CROP, WORST DIFFERENCE (OFF)", 190, 190, 195);
    Canvas.Blit(Col[1], Row[1], Span * Zoom, Crop(Snap.FilteredTile, Size, WorstX, WorstY, Span, Zoom));
    Canvas.TextFit(Col[1], Row[1] + Tile + 6, Tile, 2, "4X CROP, WORST DIFFERENCE (ON)", 190, 190, 195);
    int MaskAccepted = 0, MaskSurfaces = 0;
    Canvas.Blit(Col[2], Row[1], Tile, MaskTile(Snap.Raw, true, MaskAccepted, MaskSurfaces));
    std::snprintf(Label, sizeof(Label), "EARLY-OUT AT %d SPP: %d PX", Snap.Spp, MaskAccepted);
    Canvas.TextFit(Col[2], Row[1] + Tile + 6, Tile, 2, Label, 190, 190, 195);

    std::snprintf(Label, sizeof(Label),
                  "AT %d SPP THE FILTER IS BIT-IDENTICAL ON EVERY PIXEL ITS OWN EARLY-OUT ACCEPTS (%d OF %d SURFACE PIXELS, %d DIFFER); THE REST IS STILL FILTERED BECAUSE THE ESTIMATE HAS NOT SETTLED THERE.",
                  Snap.Spp, Accepted, Surfaces, AcceptedBad);
    Canvas.TextWrap(kMargin, SheetH - 2 * kCaption - kMargin, SheetW - 2 * kMargin, 1, Label, 235, 225, 140, 2);
    std::printf("[exhibit] identity: %d spp, early-out accepts %d of %d surface pixels, %d of those differ (worst |on-off| %.4f)\n",
                Snap.Spp, Accepted, Surfaces, AcceptedBad, Worst);
    return PngWriteCounterpart::WritePng(OutPath, Canvas.W, Canvas.H, 3, Canvas.Pixels.data(), Canvas.W * 3);
}

int RunEdgesSheet(const char* OutPath, int Size, const Settings& S, const std::vector<Snapshot>& Snapshots)
{
    const Frame& Raw = Snapshots.front().Raw;
    Settings Slack = S;
    Slack.NormalPower = 1.0f;         // no normal term to speak of
    Slack.DepthScale = 1.0e6f;        // σz so large the depth term is flat
    Slack.LuminanceScale = 1.0e6f;    // σl so large the luminance term is flat

    Frame Filtered, SlackFiltered;
    std::vector<float> Tile, SlackTile;
    RunChain(S, Raw, S.Levels, Filtered, Tile);
    RunChain(Slack, Raw, S.Levels, SlackFiltered, SlackTile);

    // The crop is centred on the strongest depth edge in the middle half of the frame: the place where an edge-avoiding
    //    filter and a plain blur must visibly disagree.
    const int TilePx = Size;
    int BestX = 0, BestY = 0;
    double BestEnergy = -1.0;
    for (int Y = Size / 6; Y < Size * 5 / 6; ++Y)
        for (int X = Size / 6; X < Size * 5 / 6; ++X)
        {
            double Energy = 0.0;
            for (int DY = -1; DY <= 1; ++DY)
                for (int DX = -1; DX <= 1; ++DX)
                {
                    const int SX = std::min(std::max(X + DX, 1), Size - 2), SY = std::min(std::max(Y + DY, 1), Size - 2);
                    const float Depth = Raw.Surface[(static_cast<size_t>(SY) * Size + SX) * 4u + 3u];
                    if (Depth <= 0.0f) continue;
                    const size_t A = (static_cast<size_t>(SY) * Size + SX) * 4u, B = (static_cast<size_t>(SY) * Size + SX + 1) * 4u;
                    Energy += std::fabs(Raw.Surface[A + 3u] - Raw.Surface[B + 3u]);
                }
            if (Energy > BestEnergy) { BestEnergy = Energy; BestX = X; BestY = Y; }
        }

    const int Span = Size / 4, Zoom = Size / Span;   // a 4x look at a quarter-width window of the frame
    std::vector<float> CropEngine = Crop(Tile, Size, BestX, BestY, Span, Zoom);
    std::vector<float> CropSlack = Crop(SlackTile, Size, BestX, BestY, Span, Zoom);

    const int SheetW = 4 * TilePx + 5 * kMargin;
    const int FootnoteHeight = 3 * 9 + kCaption;
    const int SheetH = kMargin + TilePx + 6 + kCaption + kMargin + FootnoteHeight + kMargin;
    Sheet Canvas;
    Canvas.Resize(SheetW, SheetH);
    Canvas.Fill(16, 16, 18);

    const int X[4] = { kMargin, 2 * kMargin + TilePx, 3 * kMargin + 2 * TilePx, 4 * kMargin + 3 * TilePx };
    Canvas.Blit(X[0], kMargin, TilePx, Tile);
    Canvas.TextFit(X[0], kMargin + TilePx + 6, TilePx, 1, "ENGINE SIGMA: SN 64, SZ 0.05, SL 4", 190, 190, 195);
    Canvas.Blit(X[1], kMargin, TilePx, SlackTile);
    Canvas.TextFit(X[1], kMargin + TilePx + 6, TilePx, 1, "NO EDGE STOPPING: SN 1, SZ + SL OFF", 190, 190, 195);
    Canvas.Blit(X[2], kMargin, TilePx, CropEngine);
    Canvas.TextFit(X[2], kMargin + TilePx + 6, TilePx, 1, "4X CROP, ENGINE WEIGHTS", 190, 190, 195);
    Canvas.Blit(X[3], kMargin, TilePx, CropSlack);
    Canvas.TextFit(X[3], kMargin + TilePx + 6, TilePx, 1, "4X CROP, NO EDGE STOPPING", 190, 190, 195);

    double Widget = 0.0, SlackW = 0.0;
    for (size_t I = 0; I < Tile.size(); ++I) { Widget += std::fabs(Tile[I]); SlackW += std::fabs(SlackTile[I]); }
    char Label[256];
    std::snprintf(Label, sizeof(Label),
                  "SAME NOISY 1 SPP INPUT, SAME 5-LEVEL CHAIN, ONLY THE THREE EDGE-STOPPING TERMS CHANGED. THE CROP IS CENTRED ON THE DEEPEST DEPTH EDGE (%d, %d). "
                  "WITH THE ENGINE'S WEIGHTS THE SILHOUETTE KEEPS ITS SHAPE AND THE CHECKER STAYS SHARP; WITH THE TERMS FLATTENED THE BALL BLEEDS INTO THE WALL BEHIND IT AND THE FLOOR SMEARS. "
                  "MEAN LEVEL DIFFERS BY %.1f%%, SO THIS IS NOT A BRIGHTNESS TRICK: IT IS WHERE THE ENERGY WENT.",
                  BestX, BestY, 100.0 * std::fabs(SlackW - Widget) / std::max(Widget, 1.0e-9));
    Canvas.TextWrap(kMargin, kMargin + TilePx + 6 + kCaption + kMargin, SheetW - 2 * kMargin, 1, Label, 235, 225, 140, 3);
    std::printf("[exhibit] edges: crop at the deepest depth edge (%d, %d); slack/engine mean-level difference %.2f %%\n",
                BestX, BestY, 100.0 * std::fabs(SlackW - Widget) / std::max(Widget, 1.0e-9));
    return PngWriteCounterpart::WritePng(OutPath, Canvas.W, Canvas.H, 3, Canvas.Pixels.data(), Canvas.W * 3);
}

//============================================================================================================================
//  TEMPORAL SHEET — camera pan, pre-R7a same-pixel history vs R7a reprojection (the shared rule)
//============================================================================================================================

struct TemporalSurface
{
    float Normal[3] = { 0.0f, 0.0f, 1.0f };
    float Depth = -1.0f;
    float Count = 0.0f;
    float Mean[3] = { 0.0f, 0.0f, 0.0f };
};

V3 ProjectToCamera(const Camera& Cam, V3 P, bool& Inside)
{
    const V3 D = P - Cam.Origin;
    if (D.z <= 1.0e-4f) { Inside = false; return { 0.0f, 0.0f, 0.0f }; }
    Inside = true;
    return { D.x / (D.z * Cam.HalfFovTan), D.y / (D.z * Cam.HalfFovTan), 0.0f };
}

int RunReprojectionSheet(const char* OutPath, int Size, const Settings& S, int Frames, float PanPerFrame, int SppPerFrame)
{
    const size_t Pixels = static_cast<size_t>(Size) * Size;
    std::vector<TemporalSurface> SamePixel(Pixels), Reprojected(Pixels);
    std::vector<float> MotionTile(Pixels * 3u, 0.0f), RejectMask(Pixels * 3u, 0.0f);
    std::vector<float> RejectLast(Pixels, -1.0f);   // -1 = no surface · 0 = inherited this frame · 1 = disocclusion here
    std::vector<float> RejectFrames(Pixels, 0.0f);  // how many frames of the run rejected this pixel (printed)
    std::vector<float> ReprojectedMoments(Pixels * 2u, 0.0f), SamePixelMoments(Pixels * 2u, 0.0f);

    for (int Frame = 0; Frame < Frames; ++Frame)
    {
        Camera Cam;
        Cam.Origin.x = Frame * PanPerFrame;
        Camera PrevCam;
        PrevCam.Origin.x = std::max(Frame - 1, 0) * PanPerFrame;

        std::vector<float> Radiance(Pixels * 3u, 0.0f), Normal(Pixels * 3u, 0.0f), Depth(Pixels, -1.0f), Motion(Pixels * 2u, 0.0f);
        std::vector<PixelSample> Sampled(Pixels);
        for (int Spp = 0; Spp < SppPerFrame; ++Spp)
        {
            unsigned Threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
            std::vector<std::thread> Pool;
            for (unsigned T = 0; T < Threads; ++T)
                Pool.emplace_back([&, T]() {
                    for (size_t P = T; P < Pixels; P += Threads)
                    {
                        const int X = static_cast<int>(P % Size), Y = static_cast<int>(P / Size);
                        Pcg Rng(0xC0FFEEu ^ (static_cast<uint32_t>(Frame) * 2246822519u) ^ (static_cast<uint32_t>(Spp) * 3266489917u)
                                ^ (static_cast<uint32_t>(P) * 668265263u));
                        Sampled[P] = SamplePixel(Cam, X, Y, Size, Rng);
                    }
                });
            for (std::thread& Worker : Pool) Worker.join();
        }
        for (size_t P = 0; P < Pixels; ++P)
        {
            Radiance[P * 3u] = Sampled[P].Radiance.x; Radiance[P * 3u + 1u] = Sampled[P].Radiance.y; Radiance[P * 3u + 2u] = Sampled[P].Radiance.z;
            Normal[P * 3u] = Sampled[P].Normal.x; Normal[P * 3u + 1u] = Sampled[P].Normal.y; Normal[P * 3u + 2u] = Sampled[P].Normal.z;
            Depth[P] = Sampled[P].Depth;
            if (!Sampled[P].Surface) continue;
            const V3 Position = Cam.Origin + Normalize(V3{ (2.0f * (static_cast<int>(P % Size) + 0.5f) / Size - 1.0f) * Cam.HalfFovTan,
                                                           (1.0f - 2.0f * (static_cast<int>(P / Size) + 0.5f) / Size) * Cam.HalfFovTan,
                                                           1.0f }) * Sampled[P].Depth;
            bool Inside = false;
            const V3 Prev = ProjectToCamera(PrevCam, Position, Inside);
            if (Inside)
            {
                const float PrevU = (Prev.x + 1.0f) * 0.5f, PrevV = (1.0f - Prev.y) * 0.5f;
                const float CurU = (static_cast<int>(P % Size) + 0.5f) / Size, CurV = (static_cast<int>(P / Size) + 0.5f) / Size;
                Motion[P * 2u] = CurU - PrevU;
                Motion[P * 2u + 1u] = CurV - PrevV;
            }
        }

        // History texels for the shared rule: what the PREVIOUS frame stored at each pixel.
        std::vector<HistoryTexel> SameHistory(Pixels), ReprojHistory(Pixels);
        for (size_t P = 0; P < Pixels; ++P)
        {
            SameHistory[P] = { { SamePixel[P].Normal[0], SamePixel[P].Normal[1], SamePixel[P].Normal[2] }, SamePixel[P].Depth, SamePixel[P].Count };
            ReprojHistory[P] = { { Reprojected[P].Normal[0], Reprojected[P].Normal[1], Reprojected[P].Normal[2] }, Reprojected[P].Depth, Reprojected[P].Count };
        }

        for (size_t P = 0; P < Pixels; ++P)
        {
            const int X = static_cast<int>(P % Size), Y = static_cast<int>(P / Size);
            float Normal3[3] = { Normal[P * 3u], Normal[P * 3u + 1u], Normal[P * 3u + 2u] };
            const float R = Radiance[P * 3u], G = Radiance[P * 3u + 1u], B = Radiance[P * 3u + 2u];

            // (a) pre-R7a: the pixel's own history, no motion, count kept — the accumulator R7a replaced.
            TemporalSurface& Mine = SamePixel[P];
            const float MineCount = Mine.Count + 1.0f;
            Mine.Mean[0] += (R - Mine.Mean[0]) / MineCount;
            Mine.Mean[1] += (G - Mine.Mean[1]) / MineCount;
            Mine.Mean[2] += (B - Mine.Mean[2]) / MineCount;
            Mine.Count = MineCount;
            Mine.Normal[0] = Normal3[0]; Mine.Normal[1] = Normal3[1]; Mine.Normal[2] = Normal3[2];
            Mine.Depth = Depth[P];
            {
                const float Luma = R * 0.2126f + G * 0.7152f + B * 0.0722f;
                SamePixelMoments[P * 2u] += (Luma - SamePixelMoments[P * 2u]) / MineCount;
                SamePixelMoments[P * 2u + 1u] += (Luma * Luma - SamePixelMoments[P * 2u + 1u]) / MineCount;
            }

            // (b) R7a: the shared rule decides where the history comes from, and a rejection restarts at n = 1.
            TemporalSurface& Repro = Reprojected[P];
            const bool Reprojecting = Depth[P] > 0.0f;                  // the feature bit is ON on this sheet
            const ReprojectionAnswer Answer = Reproject(X, Y, Size, Size, Normal3, Depth[P], Motion[P * 2u], Motion[P * 2u + 1u],
                                                        ReprojHistory.data(), Reprojecting);
            float ReproCount = 0.0f, ReproMean[3] = { 0.0f, 0.0f, 0.0f }, ReproMoments[2] = { 0.0f, 0.0f };
            if (Depth[P] <= 0.0f)
            {
                ReproCount = Repro.Count; ReproMean[0] = Repro.Mean[0]; ReproMean[1] = Repro.Mean[1]; ReproMean[2] = Repro.Mean[2];
                ReproMoments[0] = ReprojectedMoments[P * 2u]; ReproMoments[1] = ReprojectedMoments[P * 2u + 1u];
            }
            else if (Answer.Inherited)
            {
                const size_t Src = static_cast<size_t>(Answer.PreviousY) * Size + Answer.PreviousX;
                ReproCount = Reprojected[Src].Count;
                ReproMean[0] = Reprojected[Src].Mean[0]; ReproMean[1] = Reprojected[Src].Mean[1]; ReproMean[2] = Reprojected[Src].Mean[2];
                ReproMoments[0] = ReprojectedMoments[Src * 2u]; ReproMoments[1] = ReprojectedMoments[Src * 2u + 1u];
            }
            // else: a disocclusion — the count restarts at 1 (RejectEver records it below).
            const float NewCount = ReproCount + 1.0f;
            ReproMean[0] += (R - ReproMean[0]) / NewCount;
            ReproMean[1] += (G - ReproMean[1]) / NewCount;
            ReproMean[2] += (B - ReproMean[2]) / NewCount;
            const float Luma = R * 0.2126f + G * 0.7152f + B * 0.0722f;
            ReproMoments[0] += (Luma - ReproMoments[0]) / NewCount;
            ReproMoments[1] += (Luma * Luma - ReproMoments[1]) / NewCount;
            const size_t Destination = P;
            ReprojectedMoments[Destination * 2u] = ReproMoments[0];
            ReprojectedMoments[Destination * 2u + 1u] = ReproMoments[1];
            Repro.Count = NewCount;
            Repro.Mean[0] = ReproMean[0]; Repro.Mean[1] = ReproMean[1]; Repro.Mean[2] = ReproMean[2];
            Repro.Normal[0] = Normal3[0]; Repro.Normal[1] = Normal3[1]; Repro.Normal[2] = Normal3[2];
            Repro.Depth = Depth[P];
            // The mask is the LAST frame's answer: a pixel rejected once must not stay white for the rest of the run.
            //    The count across the run is what the caption quotes.
            RejectLast[P] = Depth[P] > 0.0f ? (Answer.Inherited ? 0.0f : 1.0f) : -1.0f;
            // Frame 0 has no history at all (the shader guards on FrameIndex > 0), so it is not a disocclusion.
            if (Frame > 0 && Depth[P] > 0.0f && !Answer.Inherited) RejectFrames[P] += 1.0f;
        }

        // Motion-vector tile for this frame: red = +x, green = +y, both ×40, so a pixel or two of parallax is legible.
        for (size_t P = 0; P < Pixels; ++P)
        {
            MotionTile[P * 3u] = std::min(std::fabs(Motion[P * 2u]) * 40.0f * (Motion[P * 2u] >= 0.0f ? 1.0f : 0.4f), 1.0f);
            MotionTile[P * 3u + 1u] = std::min(std::fabs(Motion[P * 2u + 1u]) * 40.0f * (Motion[P * 2u + 1u] >= 0.0f ? 1.0f : 0.4f), 1.0f);
            MotionTile[P * 3u + 2u] = 0.10f;
        }
    }

    for (size_t P = 0; P < Pixels; ++P)
    {
        const float Value = RejectLast[P] < 0.0f ? 0.16f : (RejectLast[P] > 0.0f ? 1.0f : 0.0f);
        RejectMask[P * 3u] = Value; RejectMask[P * 3u + 1u] = Value; RejectMask[P * 3u + 2u] = Value;
    }

    auto BuildFrame = [&](const std::vector<TemporalSurface>& State, const std::vector<float>& Moments, Frame& Out) {
        Out.Size = Size;
        Out.Source.assign(Pixels * 4u, 0.0f);
        Out.Surface.assign(Pixels * 4u, 0.0f);
        for (size_t P = 0; P < Pixels; ++P)
        {
            Out.Source[P * 4u] = State[P].Mean[0];
            Out.Source[P * 4u + 1u] = State[P].Mean[1];
            Out.Source[P * 4u + 2u] = State[P].Mean[2];
            const float Variance = Moments[P * 2u + 1u] - Moments[P * 2u] * Moments[P * 2u];
            Out.Source[P * 4u + 3u] = State[P].Count < 2.0f ? State[P].Mean[0] * State[P].Mean[0] : std::max(Variance, 0.0f) / State[P].Count;
            Out.Surface[P * 4u] = State[P].Normal[0];
            Out.Surface[P * 4u + 1u] = State[P].Normal[1];
            Out.Surface[P * 4u + 2u] = State[P].Normal[2];
            Out.Surface[P * 4u + 3u] = State[P].Depth;
        }
    };

    Frame SameFrame, ReproFrame;
    BuildFrame(SamePixel, SamePixelMoments, SameFrame);
    BuildFrame(Reprojected, ReprojectedMoments, ReproFrame);

    std::vector<float> SameRadiance(Pixels * 3u), ReproRadiance(Pixels * 3u);
    for (size_t P = 0; P < Pixels; ++P)
        for (int C = 0; C < 3; ++C)
        {
            SameRadiance[P * 3u + C] = SameFrame.Source[P * 4u + C];
            ReproRadiance[P * 3u + C] = ReproFrame.Source[P * 4u + C];
        }
    std::vector<float> SameTile, ReproTile;
    ToneMapToTile(SameRadiance, Size, S, SameTile);
    ToneMapToTile(ReproRadiance, Size, S, ReproTile);

    Frame Ignored;
    std::vector<float> SameFiltered, ReproFiltered;
    RunChain(S, SameFrame, S.Levels, Ignored, SameFiltered);
    RunChain(S, ReproFrame, S.Levels, Ignored, ReproFiltered);

    float DifferenceGain = 1.0f;
    std::vector<float> SameErr = ErrorTileAuto(SameFiltered, ReproFiltered, &DifferenceGain);

    const int Tile = Size;
    const int SheetW = 3 * Tile + 4 * kMargin;
    const int SheetH = 2 * (Tile + kCaption + kGap) + 3 * kMargin;
    Sheet Canvas;
    Canvas.Resize(SheetW, SheetH);
    Canvas.Fill(16, 16, 18);
    const int Col[3] = { kMargin, 2 * kMargin + Tile, 3 * kMargin + 2 * Tile };
    const int Row[2] = { kMargin, 2 * kMargin + Tile + kCaption + kGap };
    char Label[192];

    Canvas.Blit(Col[0], Row[0], Tile, SameTile);
    Canvas.TextFit(Col[0], Row[0] + Tile + 6, Tile, 2, "SAME-PIXEL HISTORY, PRE-R7A", 190, 190, 195);
    Canvas.Blit(Col[1], Row[0], Tile, ReproTile);
    Canvas.TextFit(Col[1], Row[0] + Tile + 6, Tile, 2, "R7A REPROJECTED + FILTERED", 235, 225, 140);
    Canvas.Blit(Col[2], Row[0], Tile, MotionTile);
    Canvas.TextFit(Col[2], Row[0] + Tile + 6, Tile, 2, "R2 MOTION, X40", 190, 190, 195);

    Canvas.Blit(Col[0], Row[1], Tile, SameErr);
    std::snprintf(Label, sizeof(Label), "|SAME - REPROJECTED| X%.0f", DifferenceGain);
    Canvas.TextFit(Col[0], Row[1] + Tile + 6, Tile, 2, Label, 190, 190, 195);
    Canvas.Blit(Col[1], Row[1], Tile, RejectMask);
    Canvas.TextFit(Col[1], Row[1] + Tile + 6, Tile, 2, "REJECTED: DISOCCLUSION", 190, 190, 195);
    Canvas.Blit(Col[2], Row[1], Tile, ReproFiltered);
    Canvas.TextFit(Col[2], Row[1] + Tile + 6, Tile, 2, "R7A ACCUM, FILTERED", 190, 190, 195);

    double Difference = 0.0;
    for (size_t I = 0; I < SameTile.size(); ++I) Difference += std::fabs(SameFiltered[I] - ReproFiltered[I]);
    Difference /= SameTile.size();
    double RejectRate = 0.0, SurfaceTotal = 0.0;
    for (size_t P = 0; P < Pixels; ++P)
    {
        if (RejectLast[P] < 0.0f) continue;
        SurfaceTotal += 1.0;
        RejectRate += RejectFrames[P] > 0.0f ? 1.0 : 0.0;
    }
    std::snprintf(Label, sizeof(Label),
                  "%d FRAMES, CAMERA PAN %.3f M/FRAME, %d SPP/FRAME - MEAN |SAME-PIXEL - REPROJECTED| %.4f TONE-MAPPED; %.1f%% OF SURFACE PIXELS WERE REJECTED AT LEAST ONCE AFTER FRAME 0",
                  Frames, PanPerFrame, SppPerFrame, Difference, 100.0 * RejectRate / std::max(SurfaceTotal, 1.0));
    Canvas.TextWrap(kMargin, SheetH - 2 * kCaption - kMargin, SheetW - 2 * kMargin, 1, Label, 235, 225, 140, 2);
    std::printf("[exhibit] reproject: %d frames, pan %.3f m/frame, mean |same-pixel - reprojected| %.4f, %.1f %% of surface pixels rejected at least once\n",
                Frames, PanPerFrame, Difference, 100.0 * RejectRate / std::max(SurfaceTotal, 1.0));
    return PngWriteCounterpart::WritePng(OutPath, Canvas.W, Canvas.H, 3, Canvas.Pixels.data(), Canvas.W * 3);
}

//============================================================================================================================
//  CLI
//============================================================================================================================

int main(int argc, char** argv)
{
    std::string Sheet = "all", OutDir = "/tmp";
    int Size = 192;
    int RefSpp = 2048;
    for (int I = 1; I < argc; ++I)
    {
        const std::string Arg = argv[I];
        if (Arg == "--sheet" && I + 1 < argc) Sheet = argv[++I];
        else if (Arg == "--outdir" && I + 1 < argc) OutDir = argv[++I];
        else if (Arg == "--size" && I + 1 < argc) Size = std::atoi(argv[++I]);
        else if (Arg == "--ref" && I + 1 < argc) RefSpp = std::atoi(argv[++I]);
        else { std::printf("usage: %s [--sheet all|ab|identity|fade|levels|edges|reproject|streams] [--outdir DIR] [--size N] [--ref SPP]\n", argv[0]); return 2; }
    }

    Settings S;
    std::printf("[exhibit] size %d, reference %d spp, chain %d levels, sigma n %.0f / z %.3f / l %.1f, exposure %.2f\n",
                Size, RefSpp, S.Levels, S.NormalPower, S.DepthScale, S.LuminanceScale, S.Exposure);

    const auto Want = [&](const char* Name) { return Sheet == "all" || Sheet == Name; };
    std::vector<Snapshot> Snapshots;
    if (Want("ab") || Want("fade") || Want("levels") || Want("edges") || Want("identity"))
    {
        std::vector<int> Marks = { 1, 16, 128, RefSpp };
        std::sort(Marks.begin(), Marks.end());
        Marks.erase(std::unique(Marks.begin(), Marks.end()), Marks.end());
        AccumulateAndSnapshot(Size, S, Marks, Snapshots);
    }

    int Written = 0;
    auto Write = [&](const char* Name, int Result) {
        std::printf("[exhibit] %s -> %s %s\n", Name, OutDir.c_str(), Result ? "ok" : "FAILED");
        Written += Result ? 1 : 0;
    };

    if (Want("ab"))       Write("denoise-ab.png", RunAbSheet((OutDir + "/denoise-ab.png").c_str(), Size, Snapshots));
    if (Want("identity")) Write("denoise-identity.png", RunIdentitySheet((OutDir + "/denoise-identity.png").c_str(), Size, S, Snapshots));
    if (Want("fade"))     Write("denoise-fade.png", RunFadeSheet((OutDir + "/denoise-fade.png").c_str(), Size, Snapshots));
    if (Want("levels"))   Write("denoise-levels.png", RunLevelsSheet((OutDir + "/denoise-levels.png").c_str(), Size, S, Snapshots));
    if (Want("edges"))    Write("denoise-edges.png", RunEdgesSheet((OutDir + "/denoise-edges.png").c_str(), Size, S, Snapshots));
    if (Want("reproject")) Write("denoise-reproject.png", RunReprojectionSheet((OutDir + "/denoise-reproject.png").c_str(), Size, S, 64, 0.012f, 4));
    if (Want("streams"))   Write("denoise-streams.png", RunStreamsSheet((OutDir + "/denoise-streams.png").c_str()));

    if (Written == 0) { std::printf("[exhibit] nothing written — unknown sheet '%s'\n", Sheet.c_str()); return 1; }
    std::printf("[exhibit] wrote %d sheet%s into %s\n", Written, Written == 1 ? "" : "s", OutDir.c_str());
    return 0;
}
