//============================================================================================================================================
// 📦 Exhibits/Workbench/Sky/SkyProbeProof.cpp — The Sky Probe Bake Against The Analytic March (roadmap #26 stage A)
//============================================================================================================================================
// The question this proof answers with pictures: can a baked probe replace the per-ray atmosphere march
//    WITHOUT the realism loss the owner saw in earlier bakes? The answer is engineered in, not hoped for:
//
//    1. The probe is HDR float, never quantised — an LDR bake crushes the circumsolar gradient first.
//    2. POINT FEATURES NEVER BAKE. The sun's disc and aureole stay analytic (texels inside the solar cone
//       are inpainted from the cone's boundary so no disc energy smears into the bilinear neighbourhood),
//       and the stars and the moon are LEFT OUT of the bake entirely — the engine already draws them from
//       their own passes (the star catalogue at binding 23, the moon texture slots), so the probe only ever
//       answers for the smooth dome, which is the part a bilinear fetch reproduces faithfully. This is the
//       exact rule that preserves the realism earlier bakes lost: discs and stars are sub-texel features no
//       256² sheet can hold, so they are never asked to.
//    3. The A/B is same-exposure, same-tonemap, same-camera: each sheet stacks the analytic march (top)
//       over the probe composite (bottom), so any difference the eye finds is the bake and nothing else.
//
//    Six times of day render both ways — dawn, morning, noon, afternoon, the showcase's 17.93 h sunset,
//    night — each staged through SkyFogIntegrator::AssignSunHour, the game build's own path. Per hour the
//    proof prints the probe-vs-dome relative error (mean / P95 / share past 10 %), the stacked panels' RMSE
//    outside the solar cone, and the measured per-ray cost of the march against the fetch.
//
//    Where this slots into the product (per the owner's call, unchanged here): the EDITOR stays analytic
//    always; the GAME fetches the probe for escaped rays. These sheets are the evidence that split rests on.
//
// The proof compiles the game's own translation unit (SkyFogIntegrator.cpp) INTO this file: the bake staging
//    is the game's staging with the star and moon layers off — same solar solve, same atmosphere, same
//    constants, nothing re-implemented and nothing in Projects/ touched.
//============================================================================================================================================

#include "SkyFogIntegrator.cpp"   // the game's TU: SolveSolarState, MakeSkyDefaults, SkyRadianceCompute, CoreSky
#include "PngWriteCounterpart.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace {

using Frontier::Vector3;
using Frontier::ProjectZero::SkyFogIntegrator;
using namespace Frontier::ProjectZero;   // the game TU's seats: CoreSky, ToFloat3/ToVector3, SkyRadianceCompute

uint32_t Failures = 0u;

void Expect(bool Condition, const char* Caption)
{
    std::fprintf(stderr, "  %s %s\n", Condition ? "PASS" : "FAIL", Caption);
    if (!Condition) ++Failures;
}

//------------------------------------------------------------------------------------------------------------------------
//                                    OCTAHEDRAL SEATING (Y-up render axes, full sphere)
//------------------------------------------------------------------------------------------------------------------------
// One square holds the whole sphere: upper hemisphere in the diamond, lower folded into the corners. Y is
//    the sky's up in the render axes SkyFogIntegrator speaks.

float SignNotZero(float V) { return V >= 0.0f ? 1.0f : -1.0f; }

void OctFromDirection(const Vector3& Dir, float& U, float& V)
{
    const float Den = std::fabs(Dir.x) + std::fabs(Dir.y) + std::fabs(Dir.z);
    float Px = Dir.x / Den;
    float Pz = Dir.z / Den;
    if (Dir.y < 0.0f)
    {
        const float Qx = (1.0f - std::fabs(Pz)) * SignNotZero(Px);
        const float Qz = (1.0f - std::fabs(Px)) * SignNotZero(Pz);
        Px = Qx; Pz = Qz;
    }
    U = Px * 0.5f + 0.5f;
    V = Pz * 0.5f + 0.5f;
}

Vector3 DirectionFromOct(float U, float V)
{
    const float Fx = U * 2.0f - 1.0f;
    const float Fz = V * 2.0f - 1.0f;
    float X = Fx, Z = Fz;
    float Y = 1.0f - std::fabs(Fx) - std::fabs(Fz);
    if (Y < 0.0f)
    {
        X = (1.0f - std::fabs(Fz)) * SignNotZero(Fx);
        Z = (1.0f - std::fabs(Fx)) * SignNotZero(Fz);
    }
    const float L = std::sqrt(X * X + Y * Y + Z * Z);
    return Vector3{ X / L, Y / L, Z / L };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                THE PROBE
//------------------------------------------------------------------------------------------------------------------------

struct SkyDomeSheet
{
    uint32_t             Side = 0u;
    std::vector<float>   Texels;   // [side*side*3] linear HDR RGB
    std::vector<uint8_t> Upper;    // [side*side] 1 = the texel's centre looks above the horizon

    // The fetch never blends across the horizon: the sky/ground edge is the sharpest line the whole sphere
    //    owns, and it lies exactly on the octahedral diamond boundary — a plain bilinear there mixes bright
    //    sky into dark ground and detonates the error. Each tap's weight is gated on the texel's hemisphere
    //    matching the ray's, then the weights renormalise. One compare per tap; the same rule the game's
    //    fetch will carry.
    [[nodiscard]] Vector3 Fetch(const Vector3& Dir) const
    {
        float U = 0.0f, V = 0.0f;
        OctFromDirection(Dir, U, V);
        const float Tx = std::clamp(U * static_cast<float>(Side) - 0.5f, 0.0f, static_cast<float>(Side - 1u));
        const float Ty = std::clamp(V * static_cast<float>(Side) - 0.5f, 0.0f, static_cast<float>(Side - 1u));
        const uint32_t X0 = static_cast<uint32_t>(Tx), Y0 = static_cast<uint32_t>(Ty);
        const uint32_t X1 = std::min(X0 + 1u, Side - 1u), Y1 = std::min(Y0 + 1u, Side - 1u);
        const float Ax = Tx - static_cast<float>(X0), Ay = Ty - static_cast<float>(Y0);
        const uint8_t RayUpper = Dir.y >= 0.0f ? 1u : 0u;
        const uint32_t TapX[4] = { X0, X1, X0, X1 };
        const uint32_t TapY[4] = { Y0, Y0, Y1, Y1 };
        const float    TapW[4] = { (1.0f - Ax) * (1.0f - Ay), Ax * (1.0f - Ay), (1.0f - Ax) * Ay, Ax * Ay };
        Vector3 Sum{ 0.0f, 0.0f, 0.0f };
        float   Weight = 0.0f;
        for (uint32_t K = 0u; K < 4u; ++K)
        {
            const size_t I = static_cast<size_t>(TapY[K]) * Side + TapX[K];
            if (Upper[I] != RayUpper) continue;
            const float* T = Texels.data() + I * 3u;
            Sum += Vector3{ T[0], T[1], T[2] } * TapW[K];
            Weight += TapW[K];
        }
        if (Weight < 1e-6f)
        {
            // Every neighbour sits across the horizon: a ray grazing within a fraction of a degree of the
            //    exact horizon can land where all four texel centres belong to the OTHER hemisphere. The
            //    answer is never a texel from the wrong world — re-aim the ray one degree deeper into its
            //    own hemisphere and fetch there. The recursion terminates: at one degree of elevation the
            //    2×2 always holds same-hemisphere texels at 256².
            const float Deeper = RayUpper != 0u ? 0.0174524f : -0.0174524f;   // sin/cos of ±1°
            const float Flat = std::sqrt(std::max(1e-12f, Dir.x * Dir.x + Dir.z * Dir.z));
            const float Level = 0.9998477f / Flat;
            return Fetch(Vector3{ Dir.x * Level, Deeper, Dir.z * Level });
        }
        return Sum * (1.0f / Weight);
    }
};

// The angular gap the analytic sun keeps to itself: disc ≈ 0.27° plus the aureole's core. Texels inside are
//    inpainted from the cone's boundary; at render time every direction inside evaluates the analytic sky.
constexpr float kSunConeRadians = 4.0f * 3.14159265f / 180.0f;

// The horizon band the analytic march keeps to itself, for the same reason as the sun: the planet's edge is
//    a radiance DISCONTINUITY (measured at 21 h: 1.2e-7 against 3.4e-4 half a degree apart), and a texel
//    that straddles it answers wrong no matter the resolution. Rays within one degree of the exact horizon
//    march analytically; the band is ~1.7 % of the sphere, so the fetch still carries ~98 % of escapes.
constexpr float kHorizonBandSine = 0.017452f;   // sin(1°)

Vector3 PushOutOfSunCone(const Vector3& Dir, const Vector3& Sun)
{
    const float Along = Dir.x * Sun.x + Dir.y * Sun.y + Dir.z * Sun.z;
    Vector3 Perp{ Dir.x - Sun.x * Along, Dir.y - Sun.y * Along, Dir.z - Sun.z * Along };
    float L = std::sqrt(Perp.x * Perp.x + Perp.y * Perp.y + Perp.z * Perp.z);
    if (L < 1e-6f)
    {
        // Dead centre of the disc: any tangent serves.
        Perp = Vector3{ Sun.y, -Sun.x, 0.0f };
        L = std::sqrt(Perp.x * Perp.x + Perp.y * Perp.y + Perp.z * Perp.z);
        if (L < 1e-6f) { Perp = Vector3{ 0.0f, Sun.z, -Sun.y }; L = std::sqrt(Perp.y * Perp.y + Perp.z * Perp.z); }
    }
    const float C = std::cos(kSunConeRadians * 1.05f), S = std::sin(kSunConeRadians * 1.05f);
    return Vector3{ Sun.x * C + Perp.x / L * S, Sun.y * C + Perp.y / L * S, Sun.z * C + Perp.z / L * S };
}

// The DOME the probe holds: the game's staging with the star and moon layers off. Same solar solve, same
//    atmosphere, same media — only the point features the engine draws from their own passes are absent.
Vector3 DomeRadiance(const SkyConfiguration& DomeSky, const Vector3& Dir)
{
    return ToVector3(SkyRadianceCompute(ToFloat3(Dir), DomeSky));
}

SkyDomeSheet BakeSkyDome(const SkyConfiguration& DomeSky, uint32_t Side)
{
    const Vector3 Sun = ToVector3(DomeSky.sunDir);
    const float ConeCos = std::cos(kSunConeRadians);
    SkyDomeSheet Sheet;
    Sheet.Side = Side;
    Sheet.Texels.resize(static_cast<size_t>(Side) * Side * 3u);
    Sheet.Upper.resize(static_cast<size_t>(Side) * Side);
    // Each texel is a 2×2 supersample CONFINED to the texel centre's hemisphere: the average smooths the
    //    dome's gradients without ever mixing sky into ground — the horizon mix is what made earlier bakes
    //    look wrong at dawn and dusk. Four marches per texel is bake-time cost, never render-time cost.
    for (uint32_t Y = 0u; Y < Side; ++Y)
        for (uint32_t X = 0u; X < Side; ++X)
        {
            const float CentreU = (static_cast<float>(X) + 0.5f) / static_cast<float>(Side);
            const float CentreV = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Side);
            const Vector3 Centre = DirectionFromOct(CentreU, CentreV);
            const bool CentreUpper = Centre.y >= 0.0f;
            Vector3 Sum{ 0.0f, 0.0f, 0.0f };
            uint32_t Kept = 0u;
            for (uint32_t S = 0u; S < 4u; ++S)
            {
                const float U = (static_cast<float>(X) + 0.25f + 0.5f * static_cast<float>(S % 2u)) / static_cast<float>(Side);
                const float V = (static_cast<float>(Y) + 0.25f + 0.5f * static_cast<float>(S / 2u)) / static_cast<float>(Side);
                Vector3 Dir = DirectionFromOct(U, V);
                if ((Dir.y >= 0.0f) != CentreUpper) continue;   // never cross the horizon inside a texel
                if (Dir.x * Sun.x + Dir.y * Sun.y + Dir.z * Sun.z > ConeCos)
                    Dir = PushOutOfSunCone(Dir, Sun);   // the inpaint: boundary sky, never the disc
                Sum += DomeRadiance(DomeSky, Dir);
                ++Kept;
            }
            if (Kept == 0u) { Sum = DomeRadiance(DomeSky, Centre); Kept = 1u; }
            const float Inv = 1.0f / static_cast<float>(Kept);
            float* T = Sheet.Texels.data() + (static_cast<size_t>(Y) * Side + X) * 3u;
            T[0] = Sum.x * Inv; T[1] = Sum.y * Inv; T[2] = Sum.z * Inv;
            Sheet.Upper[static_cast<size_t>(Y) * Side + X] = CentreUpper ? 1u : 0u;
        }
    return Sheet;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        THE CAMERA AND THE TONE CURVE
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t kViewW = 640u, kViewH = 360u;
constexpr float    kFieldOfViewDegrees = 70.0f;

Vector3 PixelDirection(uint32_t X, uint32_t Y, const Vector3& Forward, const Vector3& Right, const Vector3& Up)
{
    const float TanHalf = std::tan(kFieldOfViewDegrees * 0.5f * 3.14159265f / 180.0f);
    const float Nx = ((static_cast<float>(X) + 0.5f) / static_cast<float>(kViewW) * 2.0f - 1.0f)
                   * TanHalf * (static_cast<float>(kViewW) / static_cast<float>(kViewH));
    const float Ny = (1.0f - (static_cast<float>(Y) + 0.5f) / static_cast<float>(kViewH) * 2.0f) * TanHalf;
    Vector3 D{ Forward.x + Right.x * Nx + Up.x * Ny,
               Forward.y + Right.y * Nx + Up.y * Ny,
               Forward.z + Right.z * Nx + Up.z * Ny };
    const float L = std::sqrt(D.x * D.x + D.y * D.y + D.z * D.z);
    return Vector3{ D.x / L, D.y / L, D.z / L };
}

float LuminanceOf(const Vector3& C) { return 0.212671f * C.x + 0.715160f * C.y + 0.072169f * C.z; }

// One exposure per hour, measured on the ANALYTIC panel and applied to both — the fairness rule of the A/B.
unsigned char ToneChannel(float C, float Scale)
{
    const float S = std::max(0.0f, C) * Scale;
    const float Mapped = S / (1.0f + S);
    return static_cast<unsigned char>(std::clamp(std::pow(Mapped, 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f + 0.5f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 ONE HOUR'S STORY
//------------------------------------------------------------------------------------------------------------------------

struct HourFigures
{
    double MeanRelativeError  = 0.0;   // [-] probe fetch vs analytic dome, outside the solar cone
    double P95RelativeError   = 0.0;
    double SharePastTenPct    = 0.0;   // [-] fraction of directions past 10 % relative error
    double MarchMicroseconds  = 0.0;   // [µs] per analytic full-sky march
    double FetchMicroseconds  = 0.0;   // [µs] per probe bilinear fetch
    double PanelsRmse         = 0.0;   // [-] tone-mapped 8-bit RMSE between the panels, cone excluded
};

HourFigures RenderHourSheet(const SkyFogIntegrator& Sky, double Hour, const char* SheetPath)
{
    HourFigures Figures;

    // The two stagings this hour: the FULL sky (stars, moon, discs — what the eye must see) and the DOME
    //    (the probe's charge: everything smooth). CoreSky is the game TU's own seat, filled by AssignSunHour.
    const SkyConfiguration FullSky = CoreSky;
    SkyConfiguration DomeSky = CoreSky;
    DomeSky.starsOn = 0u;   // the star catalogue is its own pass in the engine (binding 23) — sub-texel points
    DomeSky.moonOn  = 0u;   // the moon rides its texture slots; its disc and glow stay analytic
    DomeSky.cloudOn = 0u;   // the cirrus ANIMATES (timeSeconds drives it): a static bake would freeze it, and
                            //    against a night sky its contrast spans four decades — the detail layer keeps it

    const Vector3 Sun = ToVector3(FullSky.sunDir);
    const float ConeCos = std::cos(kSunConeRadians);

    // The bake — 256², HDR float, sun inpainted, stars and moon never present.
    const SkyDomeSheet Dome = BakeSkyDome(DomeSky, 256u);

    // The error census: 20 000 quasi-random directions, solar cone excluded (analytic there by the rule).
    //    The denominator floors at 1 % of the dome's mean luminance: a texel of black night sky that reads
    //    0.0002 against 0.0001 is not a 100 % error anyone can see, and an unfloored ratio would let those
    //    invisible texels drown the figures that matter. The panel RMSE below stays unfloored — the eye's
    //    own metric keeps the census honest.
    {
        uint32_t Seed = 0x9E3779B9u;
        const auto NextUnit = [&Seed]() {
            Seed = Seed * 1664525u + 1013904223u;
            return static_cast<float>(Seed >> 8u) / 16777216.0f;
        };
        struct DirSample { Vector3 Dir; float MarchLum; float FetchLum; };
        std::vector<DirSample> Samples;
        Samples.reserve(20000u);
        double LumSum = 0.0;
        while (Samples.size() < 20000u)
        {
            const float Zc = NextUnit() * 2.0f - 1.0f;
            const float Az = NextUnit() * 6.2831853f;
            const float R = std::sqrt(std::max(0.0f, 1.0f - Zc * Zc));
            const Vector3 Dir{ R * std::cos(Az), Zc, R * std::sin(Az) };
            if (Dir.x * Sun.x + Dir.y * Sun.y + Dir.z * Sun.z > ConeCos) continue;
            if (std::fabs(Dir.y) < kHorizonBandSine) continue;   // the horizon band marches analytically
            const float Lm = LuminanceOf(DomeRadiance(DomeSky, Dir));
            const float Lf = LuminanceOf(Dome.Fetch(Dir));
            Samples.push_back(DirSample{ Dir, Lm, Lf });
            LumSum += static_cast<double>(Lm);
        }
        const double Floor = std::max(1e-6, LumSum / static_cast<double>(Samples.size()) * 0.01);
        std::vector<double> Errors;
        Errors.reserve(Samples.size());
        double Sum = 0.0;
        for (const DirSample& S : Samples)
        {
            const double Err = std::fabs(static_cast<double>(S.FetchLum - S.MarchLum))
                             / std::max(Floor, static_cast<double>(S.MarchLum));
            Errors.push_back(Err);
            Sum += Err;
        }
        std::sort(Errors.begin(), Errors.end());
        Figures.MeanRelativeError = Sum / static_cast<double>(Errors.size());
        Figures.P95RelativeError  = Errors[static_cast<size_t>(static_cast<double>(Errors.size()) * 0.95)];
        Figures.SharePastTenPct   = static_cast<double>(Errors.end() - std::upper_bound(Errors.begin(), Errors.end(), 0.10))
                                  / static_cast<double>(Errors.size());
    }

    // The cost of each estimator, measured here (the report's 6.7 µs march figure, re-taken today).
    {
        uint32_t Seed = 0xB5297A4Du;
        const auto NextUnit = [&Seed]() {
            Seed = Seed * 1664525u + 1013904223u;
            return static_cast<float>(Seed >> 8u) / 16777216.0f;
        };
        std::vector<Vector3> Dirs(20000u);
        for (Vector3& D : Dirs)
        {
            const float Zc = NextUnit() * 2.0f - 1.0f;
            const float Az = NextUnit() * 6.2831853f;
            const float R = std::sqrt(std::max(0.0f, 1.0f - Zc * Zc));
            D = Vector3{ R * std::cos(Az), Zc, R * std::sin(Az) };
        }
        volatile float Sink = 0.0f;
        const auto MarchStart = std::chrono::steady_clock::now();
        for (const Vector3& D : Dirs) Sink = Sink + Sky.ComputeSkyRadiance(D).x;
        const auto MarchEnd = std::chrono::steady_clock::now();
        for (const Vector3& D : Dirs) Sink = Sink + Dome.Fetch(D).x;
        const auto FetchEnd = std::chrono::steady_clock::now();
        Figures.MarchMicroseconds = std::chrono::duration<double, std::micro>(MarchEnd - MarchStart).count()
                                  / static_cast<double>(Dirs.size());
        Figures.FetchMicroseconds = std::chrono::duration<double, std::micro>(FetchEnd - MarchEnd).count()
                                  / static_cast<double>(Dirs.size());
    }

    // The two panels. The camera faces the sun's azimuth (north at night when the sun is set), pitched ten
    //    degrees above the horizon so the sheet carries dome, horizon band and a strip of ground.
    Vector3 Level{ Sun.x, 0.0f, Sun.z };
    float HorizontalLength = std::sqrt(Level.x * Level.x + Level.z * Level.z);
    if (Sun.y <= 0.0f || HorizontalLength < 1e-4f) { Level = Vector3{ 0.0f, 0.0f, -1.0f }; HorizontalLength = 1.0f; }
    Level = Vector3{ Level.x / HorizontalLength, 0.0f, Level.z / HorizontalLength };
    const float Pitch = 10.0f * 3.14159265f / 180.0f;
    const Vector3 Forward{ Level.x * std::cos(Pitch), std::sin(Pitch), Level.z * std::cos(Pitch) };
    const Vector3 Right{ -Level.z, 0.0f, Level.x };
    const Vector3 Up{ Right.y * Forward.z - Right.z * Forward.y,
                      Right.z * Forward.x - Right.x * Forward.z,
                      Right.x * Forward.y - Right.y * Forward.x };

    std::vector<Vector3> MarchPanel(static_cast<size_t>(kViewW) * kViewH);
    std::vector<Vector3> FetchPanel(static_cast<size_t>(kViewW) * kViewH);
    std::vector<uint8_t> InsideCone(static_cast<size_t>(kViewW) * kViewH, 0u);
    double LuminanceSum = 0.0;
    for (uint32_t Y = 0u; Y < kViewH; ++Y)
        for (uint32_t X = 0u; X < kViewW; ++X)
        {
            const Vector3 Dir = PixelDirection(X, Y, Forward, Right, Up);
            const size_t I = static_cast<size_t>(Y) * kViewW + X;
            const bool SunZone = Dir.x * Sun.x + Dir.y * Sun.y + Dir.z * Sun.z > ConeCos
                              || std::fabs(Dir.y) < kHorizonBandSine;
            InsideCone[I] = SunZone ? 1u : 0u;
            const Vector3 Full = Sky.ComputeSkyRadiance(Dir);
            MarchPanel[I] = Full;
            if (SunZone)
                FetchPanel[I] = Full;   // the rule: analytic inside the solar cone and the horizon band
            else
            {
                // The game composite: one probe fetch for the dome, plus the detail layer — stars, the
                //    moon's disc and glow, the animated cirrus — evaluated analytically, exactly as the
                //    engine's own passes deliver them. The layer is SIGNED: a cloud can darken the sky it
                //    covers, so the delta must be allowed to subtract, and only the composite floors at 0.
                const Vector3 Fetched  = Dome.Fetch(Dir);
                const Vector3 DomePart = DomeRadiance(DomeSky, Dir);
                FetchPanel[I] = Vector3{ std::max(0.0f, Fetched.x + Full.x - DomePart.x),
                                         std::max(0.0f, Fetched.y + Full.y - DomePart.y),
                                         std::max(0.0f, Fetched.z + Full.z - DomePart.z) };
            }
            LuminanceSum += static_cast<double>(LuminanceOf(Full));
        }

    const float MeanLuminance = static_cast<float>(LuminanceSum / static_cast<double>(MarchPanel.size()));
    const float Scale = 0.35f / std::max(1e-5f, MeanLuminance);

    // The stacked sheet: analytic march on top, probe composite below, a dark rule between.
    constexpr uint32_t kRule = 4u;
    const uint32_t SheetH = kViewH * 2u + kRule;
    std::vector<unsigned char> Sheet(static_cast<size_t>(kViewW) * SheetH * 3u, 24u);
    double SquaredSum = 0.0; size_t Census = 0u;
    for (uint32_t Y = 0u; Y < kViewH; ++Y)
        for (uint32_t X = 0u; X < kViewW; ++X)
        {
            const size_t I = static_cast<size_t>(Y) * kViewW + X;
            unsigned char* TopPx = Sheet.data() + (static_cast<size_t>(Y) * kViewW + X) * 3u;
            unsigned char* BotPx = Sheet.data() + (static_cast<size_t>(Y + kViewH + kRule) * kViewW + X) * 3u;
            const unsigned char Tr = ToneChannel(MarchPanel[I].x, Scale), Tg = ToneChannel(MarchPanel[I].y, Scale), Tb = ToneChannel(MarchPanel[I].z, Scale);
            const unsigned char Br = ToneChannel(FetchPanel[I].x, Scale), Bg = ToneChannel(FetchPanel[I].y, Scale), Bb = ToneChannel(FetchPanel[I].z, Scale);
            TopPx[0] = Tr; TopPx[1] = Tg; TopPx[2] = Tb;
            BotPx[0] = Br; BotPx[1] = Bg; BotPx[2] = Bb;
            if (!InsideCone[I])
            {
                SquaredSum += (static_cast<double>(Tr) - Br) * (static_cast<double>(Tr) - Br)
                            + (static_cast<double>(Tg) - Bg) * (static_cast<double>(Tg) - Bg)
                            + (static_cast<double>(Tb) - Bb) * (static_cast<double>(Tb) - Bb);
                Census += 3u;
            }
        }
    Figures.PanelsRmse = std::sqrt(SquaredSum / static_cast<double>(Census));

    if (PngWriteCounterpart::WritePng(SheetPath, static_cast<int>(kViewW), static_cast<int>(SheetH), 3,
                                      Sheet.data(), static_cast<int>(kViewW) * 3) == 0)
    {
        std::fprintf(stderr, "  FAIL could not write %s\n", SheetPath);
        ++Failures;
    }
    else
        std::fprintf(stderr, "  wrote %s\n", SheetPath);

    std::fprintf(stderr,
        "  %05.2f h | rel err mean %.3f%% P95 %.3f%% past-10%% %.2f%% | march %.2f us fetch %.3f us (%.0fx) | panel RMSE %.2f\n",
        Hour, Figures.MeanRelativeError * 100.0, Figures.P95RelativeError * 100.0, Figures.SharePastTenPct * 100.0,
        Figures.MarchMicroseconds, Figures.FetchMicroseconds,
        Figures.MarchMicroseconds / std::max(1e-9, Figures.FetchMicroseconds), Figures.PanelsRmse);
    return Figures;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::fprintf(stderr, "[SkyProbeProof] the bake against the march, six times of day, every point feature analytic\n");

    struct HourSheet { double Hour; const char* Png; };
    const HourSheet Hours[6] = {
        {  6.00, "Exhibits/Gallery/Sky/SkyProbeProof_06h00.png" },
        {  9.00, "Exhibits/Gallery/Sky/SkyProbeProof_09h00.png" },
        { 12.00, "Exhibits/Gallery/Sky/SkyProbeProof_12h00.png" },
        { 15.00, "Exhibits/Gallery/Sky/SkyProbeProof_15h00.png" },
        { 17.93, "Exhibits/Gallery/Sky/SkyProbeProof_17h56.png" },
        { 21.00, "Exhibits/Gallery/Sky/SkyProbeProof_21h00.png" },
    };

    SkyFogIntegrator Sky;
    double WorstMean = 0.0, WorstRmse = 0.0, WorstFetchSpeedup = 1e9;

    for (const HourSheet& H : Hours)
    {
        Sky.AssignSunHour(H.Hour);
        const HourFigures F = RenderHourSheet(Sky, H.Hour, H.Png);
        WorstMean         = std::max(WorstMean, F.MeanRelativeError);
        WorstRmse         = std::max(WorstRmse, F.PanelsRmse);
        WorstFetchSpeedup = std::min(WorstFetchSpeedup, F.MarchMicroseconds / std::max(1e-9, F.FetchMicroseconds));
    }

    // The gates: the probe answers only for the smooth dome, so the figures must hold at EVERY hour — dawn
    //    and night included, because the point features that once wrecked night bakes are no longer baked.
    Expect(WorstMean < 0.02, "probe error: mean relative error under 2% at every hour of the six");
    Expect(WorstRmse < 3.0,  "pictures: tone-mapped panel RMSE under 3 of 255 outside the solar cone, every hour");
    Expect(WorstFetchSpeedup > 10.0, "the fetch beats the march more than tenfold at every hour");

    std::fprintf(stderr, "[SkyProbeProof] %s\n", Failures == 0u ? "every figure agrees" : "FAILURES above");
    return Failures == 0u ? 0 : 1;
}
