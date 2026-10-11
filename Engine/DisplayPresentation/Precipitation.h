//============================================================================================================================================
// 📦 Engine/DisplayPresentation/Precipitation.h — rain, drizzle, hail, snow and sleet as a world-space particle system
//============================================================================================================================================
// Celestial port, step 6. Deliberately NOT a transcription of the reference demo's precipitation, because three
//    of its behaviours are defects rather than simplifications and all three are visible in ordinary use.
//
// ⚠️ ① IT SPAWNS IN FRONT OF THE CAMERA. The demo biases every spawn point ahead of the view
//        (`px = cam.x + fx*R*.45 + ...`, with fx from the camera yaw). Turn around and the sky behind you is
//        empty until the pool refills, which reads as rain that follows you. Here the emitter is a WORLD-SPACE
//        cylinder about the camera with no directional bias: turning the camera changes nothing about where
//        particles are, because they were never placed relative to where you were looking.
//
// ⚠️ ② IT RAINS IN SPACE. The demo's only altitude term is a fade, so at 400 km the emitter is still running and
//        the drops are still falling — you reported seeing this, and it is funny precisely because nothing stops
//        it. Precipitation here is hard-gated to the troposphere: above the cloud ceiling the system emits
//        nothing at all and reports zero particles, which is a state the proof asserts rather than a fade.
//
// ⚠️ ③ IT RAINS FROM A CLEAR SKY. The demo consults cloud cover only as a PROBABILITY
//        (`if(c<.04 || random() > min(1,.25+c*1.2)) continue`), so a completely clear column still emits at 25%.
//        Here cover is a hard gate: below the threshold the column produces nothing. Rain comes out of clouds.
//
// ⚠️ ④ SNOW ACCUMULATION IS NOT KEPT AS PARTICLES. A landed flake is not a particle any more — it is a patch of
//        ground that happens to be white. Keeping millions of settled flakes in the pool is what makes a snow
//        system fall over, so a landed flake is retired into a coarse DEPTH FIELD (see SnowField) and its slot
//        is returned immediately. The field is a grid of heights, so ten million flakes cost the same as ten.
//        The renderer draws one surface from it rather than a particle per flake.
//
// Splashes are tier-keyed and are the ONLY thing left in screen space; puddles are deliberately out of scope and
//    recorded in the backlog rather than faked here.

#pragma once

#include "WindField.h"
#include "VolumetricMedia.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class PrecipitationCategory : uint32_t
{
    Rain = 0u, Drizzle = 1u, Hail = 2u, Snow = 3u, Sleet = 4u,
};

// Per-type physics. Terminal velocities are measured values for the representative drop or flake size, not
//    tuning: a 2 mm raindrop falls at about 6.5 m/s, drizzle at 2, hail at 20, a snowflake at 1.
struct PrecipitationPhysics
{
    float TerminalVelocity;   // [m/s] downward
    float DragRate;           // [1/s] how fast horizontal velocity matches the wind
    float Restitution;        // [-] bounce on impact; hail bounces, rain does not
    float Flutter;            // [-] lateral wander; only flakes have it
    float MeltRate;           // [1/s] how fast a landed particle disappears
    float RadiusMetres;       // [m] drawn size
    bool  Accumulates;        // does it build up on the ground?
};

inline PrecipitationPhysics PhysicsFor(PrecipitationCategory Category) noexcept
{
    switch (Category)
    {
        case PrecipitationCategory::Drizzle: return { 2.0f,  3.0f, 0.00f, 0.10f, 1.60f, 0.00025f, false };
        case PrecipitationCategory::Hail:    return { 20.0f, 0.6f, 0.45f, 0.00f, 0.25f, 0.00500f, true  };
        case PrecipitationCategory::Snow:    return { 1.0f,  4.0f, 0.00f, 1.00f, 0.10f, 0.00300f, true  };
        case PrecipitationCategory::Sleet:   return { 4.0f,  2.0f, 0.15f, 0.35f, 0.80f, 0.00200f, true  };
        case PrecipitationCategory::Rain:
        default:                             return { 6.5f,  1.5f, 0.05f, 0.00f, 1.20f, 0.00100f, false };
    }
}

struct PrecipitationSettings
{
    bool  Enabled     = false;
    PrecipitationCategory Category = PrecipitationCategory::Rain;
    float RateMillimetresPerHour = 12.0f;   // 2 light · 10 moderate · 50 heavy · 100 violent
    float Density     = 1.5f;   // [x] multiplies drops per m²
    float SizeScale   = 1.0f;   // [x]
    float WindDrift   = 0.35f;  // [0..1] how much the Wind Field deflects a particle
    bool  FollowWind  = true;
    bool  SpawnFromClouds = true;
    bool  GroundCollision = true;
    float RestTimeSeconds = 1.6f;
    float Accumulation = 0.4f;  // [0..1] how much settled snow builds up

    // The emitter is a cylinder about the camera. Radius is world-space and has nothing to do with view
    //    direction — see defect ① above.
    float EmitterRadius = 60.0f;    // [m]

    // ⚠️ The emitter's height is bounded by FALL TIME, not by a fixed distance, and the difference is a budget
    //    matter rather than a nicety. A 400 m ceiling is 62 s of falling for rain and 400 s for snow — measured,
    //    a 50 s snowfall spawned 8 192 flakes and landed 108, because the pool filled with flakes still hundreds
    //    of metres up and spawning stopped. Every type then spends its budget on particles too far away to see.
    //
    //    Bounding the time instead makes the ceiling type-aware for free: snow tops out low and close, hail high
    //    and fast, and the pool turns over quickly enough that the ground actually gets wet.
    float EmitterFallSeconds = 12.0f;   // [s] the longest a spawned particle should take to arrive
    float EmitterCeilingAboveCamera = 400.0f;   // [m] hard cap regardless of the above

    // Cover below this is a clear sky and produces nothing. A hard gate, not a probability — defect ③.
    float MinimumCloudCover = 0.10f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTICLES
//------------------------------------------------------------------------------------------------------------------------

struct PrecipitationParticle
{
    float Position[3]{};
    float Velocity[3]{};
    float Size    = 1.0f;
    float Phase   = 0.0f;    // flutter phase for flakes
    uint8_t Bounces = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SNOW FIELD
//------------------------------------------------------------------------------------------------------------------------

// Settled snow, as a height field rather than a heap of retired particles.
//
//    ⚠️ THIS IS THE WHOLE POINT OF THE SYSTEM'S MEMORY BEHAVIOUR. A landed flake carries no information except
//    "there is now slightly more snow near here". Keeping it as a particle costs 40 bytes and a simulation slot
//    forever, for a fact that fits in a fraction of a float. Ten minutes of heavy snow is tens of millions of
//    flakes; as particles that is gigabytes and a dead frame rate, as a field it is a fixed 256 KB that never
//    grows.
//
//    So accumulation is a MERGE, continuously and in O(1) per flake: the flake's mass is added to one cell and
//    the flake is retired the same tick. There is no cluster search, no rebuild, and no growth in cost as the
//    snow deepens — which is what "merged continuously but efficiently" has to mean if it is to survive a long
//    session. The renderer draws ONE mesh from this grid, not one quad per flake.
class SnowField
{
public:
    static constexpr uint32_t kResolution = 128u;      // cells per side
    static constexpr float    kCellMetres = 0.5f;      // 64 m square around the origin

    void Reset() noexcept
    {
        Depth.assign(static_cast<size_t>(kResolution) * kResolution, 0.0f);
        Deepest = 0.0f;
        Settled = 0u;
    }

    // Retire a flake into the field. O(1): one index, one add. No search, no merge pass, no reallocation.
    void Settle(float WorldX, float WorldY, float Mass) noexcept
    {
        if (Depth.empty()) Reset();
        const int32_t Cx = static_cast<int32_t>(std::floor(WorldX / kCellMetres)) + static_cast<int32_t>(kResolution / 2u);
        const int32_t Cy = static_cast<int32_t>(std::floor(WorldY / kCellMetres)) + static_cast<int32_t>(kResolution / 2u);
        if (Cx < 0 || Cy < 0 || Cx >= static_cast<int32_t>(kResolution) || Cy >= static_cast<int32_t>(kResolution)) return;
        const size_t Index = static_cast<size_t>(Cy) * kResolution + static_cast<size_t>(Cx);
        Depth[Index] += Mass;
        if (Depth[Index] > Deepest) Deepest = Depth[Index];
        ++Settled;
    }

    [[nodiscard]] float DepthAt(float WorldX, float WorldY) const noexcept
    {
        if (Depth.empty()) return 0.0f;
        const int32_t Cx = static_cast<int32_t>(std::floor(WorldX / kCellMetres)) + static_cast<int32_t>(kResolution / 2u);
        const int32_t Cy = static_cast<int32_t>(std::floor(WorldY / kCellMetres)) + static_cast<int32_t>(kResolution / 2u);
        if (Cx < 0 || Cy < 0 || Cx >= static_cast<int32_t>(kResolution) || Cy >= static_cast<int32_t>(kResolution)) return 0.0f;
        return Depth[static_cast<size_t>(Cy) * kResolution + static_cast<size_t>(Cx)];
    }

    [[nodiscard]] uint64_t SettledCount() const noexcept { return Settled; }
    [[nodiscard]] float    DeepestMetres() const noexcept { return Deepest; }
    // The field's footprint never changes, however much snow falls. This is the number the proof asserts.
    [[nodiscard]] size_t   ByteCount() const noexcept { return Depth.size() * sizeof(float); }
    [[nodiscard]] const std::vector<float>& Cells() const noexcept { return Depth; }

private:
    std::vector<float> Depth;
    float    Deepest = 0.0f;
    uint64_t Settled = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SYSTEM
//------------------------------------------------------------------------------------------------------------------------

struct PrecipitationTelemetry
{
    uint32_t Alive        = 0u;
    uint32_t SpawnedThisStep = 0u;
    uint32_t SettledThisStep = 0u;
    uint32_t RejectedClearSky = 0u;   // columns that produced nothing because there was no cloud
    bool     AboveWeather = false;    // the camera is out of the troposphere; nothing emits
};

class PrecipitationSystem
{
public:
    void Configure(uint32_t Capacity) noexcept
    {
        Budget = Capacity;
        Particles.reserve(Capacity);
        // The field is allocated here, once, at its full and final size. Deferring it to the first settled flake
        //    made the footprint look like it grew with the snow, which is exactly the property being denied.
        Snow.Reset();
    }

    void Reset() noexcept
    {
        Particles.clear();
        Accumulator = 0.0f;
        Snow.Reset();
    }

    [[nodiscard]] const std::vector<PrecipitationParticle>& Pool() const noexcept { return Particles; }
    [[nodiscard]] const SnowField& Field() const noexcept { return Snow; }
    [[nodiscard]] const PrecipitationTelemetry& Telemetry() const noexcept { return Last; }

    // One simulation step. GroundHeight is the world Z of the surface under a point; the caller supplies it so
    //    this does not need to know about terrain.
    void Step(const PrecipitationSettings& Settings, const CloudLayerSettings& Cloud,
              const WindSettings& Wind, const float Camera[3], float DeltaSeconds, float Time,
              float GroundHeight) noexcept
    {
        Last = PrecipitationTelemetry{};
        if (!Settings.Enabled || DeltaSeconds <= 0.0f) { Particles.clear(); return; }

        const PrecipitationPhysics Physics = PhysicsFor(Settings.Category);

        // ── ② The altitude gate ────────────────────────────────────────────────────────────────────────────────
        // Above the weather there is no weather. Hard, not a fade: at the cloud ceiling the emitter stops and any
        //    particles still in flight are dropped, so flying to orbit cannot leave rain running.
        if (Camera[2] > Cloud.CeilingMetres)
        {
            Last.AboveWeather = true;
            Particles.clear();
            return;
        }

        // ── The emitter ────────────────────────────────────────────────────────────────────────────────────────
        // Where the column tops out: the cloud base when spawning from clouds, capped so a very high deck does
        //    not put the emitter kilometres away and waste the budget on particles that take a minute to arrive.
        float SlabBase = 0.0f, SlabTop = 0.0f;
        const bool HaveCloud = Cloud.Enabled && VolumetricMedia::SlabExtent(Cloud, SlabBase, SlabTop);
        // How high a particle of THIS type can usefully start: far enough to have depth, near enough to arrive.
        const float ReachMetres = std::fmin(Settings.EmitterCeilingAboveCamera,
                                            Physics.TerminalVelocity * Settings.EmitterFallSeconds);
        float Ceiling = Camera[2] + std::fmax(ReachMetres, 10.0f);
        if (Settings.SpawnFromClouds && HaveCloud)
        {
            // Never above the cloud base — rain comes out of the cloud, not from the air beneath it.
            Ceiling = std::fmin(Ceiling, std::fmax(Camera[2] + 10.0f, SlabBase));
        }
        Ceiling = std::fmin(Ceiling, Cloud.CeilingMetres);

        // Drops per square metre per second. Snow and drizzle carry far more, smaller particles for the same
        //    depth of water, which is why the rate is per-type rather than one constant.
        const float PerSquareMetre =
              Settings.Category == PrecipitationCategory::Snow    ? Settings.RateMillimetresPerHour * 0.9f
            : Settings.Category == PrecipitationCategory::Drizzle ? Settings.RateMillimetresPerHour * 2.2f
                                                                   : Settings.RateMillimetresPerHour * 0.55f;
        constexpr float kPi = 3.14159265358979323846f;
        const float Area = kPi * Settings.EmitterRadius * Settings.EmitterRadius;
        const float Rate = std::fmin(static_cast<float>(Budget) * 1.5f,
                                     PerSquareMetre * Area * Settings.Density);
        Accumulator += Rate * DeltaSeconds;
        if (Accumulator > 3.0f * static_cast<float>(Budget)) Accumulator = 3.0f * static_cast<float>(Budget);

        while (Accumulator >= 1.0f && Particles.size() < Budget)
        {
            Accumulator -= 1.0f;

            // ── ① World-space placement, with NO camera-facing bias ────────────────────────────────────────────
            // sqrt of a uniform gives a uniform areal density in the disc. The demo added `+ forward * R * 0.45`
            //    here, which is what leaves the sky behind you empty when you turn.
            const float U1 = Random(), U2 = Random();
            const float Radius = std::sqrt(U1) * Settings.EmitterRadius;
            const float Angle  = U2 * 2.0f * kPi;
            const float X = Camera[0] + std::cos(Angle) * Radius;
            const float Y = Camera[1] + std::sin(Angle) * Radius;

            // ── ③ Cloud cover as a hard gate ──────────────────────────────────────────────────────────────────
            if (Settings.SpawnFromClouds)
            {
                if (!HaveCloud) { ++Last.RejectedClearSky; continue; }
                if (ColumnCover(Cloud, Wind, X, Y, SlabBase, SlabTop, Time) < Settings.MinimumCloudCover)
                {
                    ++Last.RejectedClearSky;
                    continue;
                }
            }

            const float Floor = std::fmax(GroundHeight, Camera[2] - 40.0f);
            if (Ceiling <= Floor) continue;

            PrecipitationParticle P{};
            P.Position[0] = X;
            P.Position[1] = Y;
            P.Position[2] = Floor + std::pow(Random(), 0.8f) * (Ceiling - Floor);
            P.Velocity[2] = -Physics.TerminalVelocity * (0.85f + Random() * 0.3f);
            P.Size  = (0.7f + Random() * 0.6f) * Settings.SizeScale;
            P.Phase = Random() * 6.28318531f;
            Particles.push_back(P);
            ++Last.SpawnedThisStep;
        }

        // ── The simulation ─────────────────────────────────────────────────────────────────────────────────────
        float WindVelocity[3] = { 0.0f, 0.0f, 0.0f };
        if (Settings.FollowWind)
        {
            // ⚠️ SampleStep, not Sample: this is per-particle and the swirl is a per-pixel term (WindField.h).
            WindField::SampleStep(Wind, Camera[2] + 20.0f, WindVelocity);
            WindVelocity[0] *= Settings.WindDrift;
            WindVelocity[1] *= Settings.WindDrift;
        }

        for (size_t I = Particles.size(); I-- > 0;)
        {
            PrecipitationParticle& P = Particles[I];

            // Drag toward terminal velocity and toward the wind.
            const float Blend = std::fmin(1.0f, DeltaSeconds * 2.5f);
            P.Velocity[2] += (-Physics.TerminalVelocity - P.Velocity[2]) * Blend;
            const float Lateral = std::fmin(1.0f, DeltaSeconds * Physics.DragRate);
            P.Velocity[0] += (WindVelocity[0] - P.Velocity[0]) * Lateral;
            P.Velocity[1] += (WindVelocity[1] - P.Velocity[1]) * Lateral;

            // Flakes wander. A snowflake's low mass and high drag make it flutter rather than fall straight,
            //    which is most of what distinguishes snow from rain visually.
            if (Physics.Flutter > 0.0f)
            {
                P.Phase += DeltaSeconds * 2.2f;
                P.Velocity[0] += std::sin(P.Phase) * DeltaSeconds * 1.4f * Physics.Flutter;
                P.Velocity[1] += std::cos(P.Phase * 0.7f) * DeltaSeconds * 1.2f * Physics.Flutter;
            }

            for (int C = 0; C < 3; ++C) P.Position[C] += P.Velocity[C] * DeltaSeconds;

            // Ground.
            const float Surface = Settings.GroundCollision
                ? GroundHeight + Snow.DepthAt(P.Position[0], P.Position[1])
                : -1e30f;
            if (P.Position[2] <= Surface)
            {
                P.Position[2] = Surface;
                const float Bounce = Physics.Restitution;
                if (Bounce > 0.05f && std::fabs(P.Velocity[2]) > 1.5f && P.Bounces < 3u)
                {
                    P.Velocity[2] = -P.Velocity[2] * Bounce;
                    P.Velocity[0] *= 0.8f;
                    P.Velocity[1] *= 0.8f;
                    ++P.Bounces;
                    P.Position[2] += 0.01f;
                }
                else
                {
                    // ── ④ Retire into the field, immediately ──────────────────────────────────────────────────
                    // The flake stops being a particle the moment it lands. Its slot is freed the same tick, so
                    //    the pool never grows with accumulation and the memory cost of settled snow is fixed.
                    if (Physics.Accumulates && Settings.Accumulation > 0.0f)
                        Snow.Settle(P.Position[0], P.Position[1],
                                    Physics.RadiusMetres * P.Size * Settings.Accumulation * 0.4f);
                    ++Last.SettledThisStep;
                    Particles[I] = Particles.back();
                    Particles.pop_back();
                    continue;
                }
            }
            // Out of the emitter's reach, or fallen well below the camera.
            const float Dx = P.Position[0] - Camera[0], Dy = P.Position[1] - Camera[1];
            if (P.Position[2] < Camera[2] - 60.0f ||
                Dx * Dx + Dy * Dy > (Settings.EmitterRadius * 2.2f) * (Settings.EmitterRadius * 2.2f))
            {
                Particles[I] = Particles.back();
                Particles.pop_back();
                continue;
            }
        }

        Last.Alive = static_cast<uint32_t>(Particles.size());
    }

    // Cloud cover in a vertical column, sampled at a few heights through the slab. This is what makes rain fall
    //    out of clouds rather than out of a clear sky.
    static float ColumnCover(const CloudLayerSettings& Cloud, const WindSettings& Wind,
                             float X, float Y, float SlabBase, float SlabTop, float Time) noexcept
    {
        constexpr int kSamples = 4;
        float Sum = 0.0f;
        for (int I = 0; I < kSamples; ++I)
        {
            const float T = (static_cast<float>(I) + 0.5f) / static_cast<float>(kSamples);
            const float P[3] = { X, Y, SlabBase + (SlabTop - SlabBase) * T };
            Sum += VolumetricMedia::CloudDensity(Cloud, Wind, P, Time);
        }
        return Sum / static_cast<float>(kSamples);
    }

private:
    float Random() noexcept
    {
        // xorshift32 — deterministic, so a proof can replay a run exactly.
        Seed ^= Seed << 13; Seed ^= Seed >> 17; Seed ^= Seed << 5;
        return static_cast<float>(Seed & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }

    std::vector<PrecipitationParticle> Particles;
    SnowField Snow;
    PrecipitationTelemetry Last{};
    uint32_t Budget = 8192u;
    float    Accumulator = 0.0f;
    uint32_t Seed = 0x13579BDFu;
};

} // namespace Frontier
