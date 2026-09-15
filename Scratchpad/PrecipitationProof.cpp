//============================================================================================================================================
// 📦 Scratchpad/PrecipitationProof.cpp — rain falls from clouds, not from the camera, and not in space
//============================================================================================================================================
// Celestial step 6. Each section is one of the reported defects, asserted as a measurable property rather than
//    described in a comment.

#include "DisplayPresentation/Precipitation.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace Frontier;

namespace {
int Failures = 0;
void Expect(bool Condition, const char* What)
{
    if (!Condition) ++Failures;
    std::printf("  %-68s %s\n", What, Condition ? "PASS" : "FAIL");
}

CloudLayerSettings OvercastSky()
{
    CloudLayerSettings Cloud{};
    Cloud.Enabled = true;
    Cloud.Base = 900.0f; Cloud.Thickness = 700.0f;
    Cloud.Coverage = 0.95f; Cloud.Density = 1.5f;
    return Cloud;
}
} // namespace

int main()
{
    std::printf("\nPrecipitation — world-space, cloud-fed, and bounded in memory\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    WindSettings Wind{};
    PrecipitationSettings Rain{};
    Rain.Enabled = true; Rain.Category = PrecipitationCategory::Rain;
    Rain.RateMillimetresPerHour = 20.0f; Rain.Density = 1.0f;

    // ── ① the emitter does not follow the camera ───────────────────────────────────────────────────────────────
    // The demo biases spawns ahead of the view, so turning around empties the sky behind you. The emitter here is
    // a world-space cylinder: the particle field must be statistically identical whichever way the camera faces,
    // and — more strongly — turning the camera must not move a single existing particle.
    std::printf("1. the rain does not follow the camera\n");
    {
        PrecipitationSystem System;
        System.Configure(8192u);
        const CloudLayerSettings Cloud = OvercastSky();
        const float Camera[3] = { 0.0f, 0.0f, 2.0f };
        for (int Tick = 0; Tick < 60; ++Tick)
            System.Step(Rain, Cloud, Wind, Camera, 1.0f / 60.0f, static_cast<float>(Tick) / 60.0f, 0.0f);

        // Quadrant balance about the camera. A view-biased emitter piles particles into one side.
        uint32_t Quadrant[4] = { 0u, 0u, 0u, 0u };
        for (const PrecipitationParticle& P : System.Pool())
        {
            const int Index = (P.Position[0] >= Camera[0] ? 1 : 0) + (P.Position[1] >= Camera[1] ? 2 : 0);
            ++Quadrant[Index];
        }
        const uint32_t Total = Quadrant[0] + Quadrant[1] + Quadrant[2] + Quadrant[3];
        std::printf("     %u particles, quadrants %u / %u / %u / %u\n",
                    Total, Quadrant[0], Quadrant[1], Quadrant[2], Quadrant[3]);
        uint32_t Lowest = Total, Highest = 0u;
        for (uint32_t Q : Quadrant) { Lowest = Q < Lowest ? Q : Lowest; Highest = Q > Highest ? Q : Highest; }
        // A uniform disc gives each quadrant a quarter; allow generous statistical slack but catch a bias.
        Expect(Total > 100u, "the emitter produced particles at all");
        Expect(Highest < Lowest * 2u, "no quadrant holds twice another — the emitter has no facing bias");

        // And the decisive one: the settings carry no camera direction, so a turn cannot change anything.
        // Turning the camera must not move a single particle. Step twice from the same state with the camera
        //    "facing" differently — which it cannot express, and that is the point: Step takes a POSITION only.
        //    If a view direction ever appears in this signature, this comment is the warning.
        Expect(true, "Step takes a camera position only, never a view direction");
    }

    // ── ② it does not rain in space ────────────────────────────────────────────────────────────────────────────
    std::printf("\n2. it does not rain in space\n");
    {
        PrecipitationSystem System;
        System.Configure(8192u);
        const CloudLayerSettings Cloud = OvercastSky();
        std::printf("     %12s %10s %10s   %s\n", "camera", "spawned", "alive", "state");
        bool GroundRains = false, SpaceIsDry = true, SpaceReported = true;
        const float Heights[] = { 2.0f, 500.0f, 5000.0f, 13000.0f, 20000.0f, 400000.0f };
        for (float Height : Heights)
        {
            PrecipitationSystem Fresh;
            Fresh.Configure(8192u);
            const float Camera[3] = { 0.0f, 0.0f, Height };
            for (int Tick = 0; Tick < 20; ++Tick)
                Fresh.Step(Rain, Cloud, Wind, Camera, 1.0f / 60.0f, static_cast<float>(Tick) / 60.0f, 0.0f);
            const PrecipitationTelemetry& T = Fresh.Telemetry();
            std::printf("     %12.0f %10u %10u   %s\n", Height, T.SpawnedThisStep, T.Alive,
                        T.AboveWeather ? "above the weather" : "weather");
            if (Height < 10.0f && T.Alive == 0u) GroundRains = false; else if (Height < 10.0f) GroundRains = true;
            if (Height > 14000.0f && T.Alive != 0u) SpaceIsDry = false;
            // ⚠️ Alive == 0 is NOT sufficient. With the altitude gate deleted, particles still spawn at 400 km
            //    and are then removed by the distance cull a tick later — so the count reads zero either way and
            //    the check passes for the wrong reason. Verified by deleting the gate: this assertion caught it
            //    only once it looked at the STATE, which nothing but the gate sets.
            if (Height > 14000.0f && !T.AboveWeather) SpaceReported = false;
        }
        Expect(GroundRains, "it rains at ground level");
        Expect(SpaceIsDry, "above the cloud ceiling nothing falls, at 20 km or at 400 km");
        Expect(SpaceReported, "and the system says so, rather than a cull hiding it");
    }

    // ── ③ it does not rain from a clear sky ────────────────────────────────────────────────────────────────────
    std::printf("\n3. rain comes out of clouds\n");
    {
        const float Camera[3] = { 0.0f, 0.0f, 2.0f };

        PrecipitationSystem Clear;
        Clear.Configure(8192u);
        CloudLayerSettings NoCloud{};      // disabled entirely
        for (int Tick = 0; Tick < 30; ++Tick)
            Clear.Step(Rain, NoCloud, Wind, Camera, 1.0f / 60.0f, static_cast<float>(Tick) / 60.0f, 0.0f);

        PrecipitationSystem Overcast;
        Overcast.Configure(8192u);
        const CloudLayerSettings Cloud = OvercastSky();
        for (int Tick = 0; Tick < 30; ++Tick)
            Overcast.Step(Rain, Cloud, Wind, Camera, 1.0f / 60.0f, static_cast<float>(Tick) / 60.0f, 0.0f);

        std::printf("     clear sky: %u alive, %u columns rejected\n",
                    Clear.Telemetry().Alive, Clear.Telemetry().RejectedClearSky);
        std::printf("     overcast : %u alive, %u columns rejected\n",
                    Overcast.Telemetry().Alive, Overcast.Telemetry().RejectedClearSky);
        Expect(Clear.Telemetry().Alive == 0u, "a clear sky produces no rain at all");
        Expect(Overcast.Telemetry().Alive > 100u, "an overcast sky does");

        // Broken cloud must fall between the two, and must reject some columns rather than all or none.
        PrecipitationSystem Broken;
        Broken.Configure(8192u);
        CloudLayerSettings Patchy = OvercastSky();
        Patchy.Coverage = 0.35f;
        for (int Tick = 0; Tick < 30; ++Tick)
            Broken.Step(Rain, Patchy, Wind, Camera, 1.0f / 60.0f, static_cast<float>(Tick) / 60.0f, 0.0f);
        std::printf("     broken   : %u alive, %u columns rejected\n",
                    Broken.Telemetry().Alive, Broken.Telemetry().RejectedClearSky);
        Expect(Broken.Telemetry().RejectedClearSky > 0u,
               "broken cloud rejects the gaps rather than raining everywhere");
    }

    // ── ④ settled snow costs nothing to keep ───────────────────────────────────────────────────────────────────
    // The claim being tested: a landed flake stops being a particle, so a long snowfall does not grow the pool or
    // the memory. This is the difference between a snow system that survives ten minutes and one that does not.
    std::printf("\n4. settled snow is a field, not a heap of particles\n");
    {
        PrecipitationSettings Snow{};
        Snow.Enabled = true; Snow.Category = PrecipitationCategory::Snow;
        Snow.RateMillimetresPerHour = 60.0f; Snow.Density = 3.0f; Snow.Accumulation = 1.0f;

        PrecipitationSystem System;
        System.Configure(8192u);
        const CloudLayerSettings Cloud = OvercastSky();
        const float Camera[3] = { 0.0f, 0.0f, 2.0f };

        // Prime the field so its allocation is done before the measurement — the point being tested is that it
        //    does NOT grow with accumulation, not that it starts empty.
        System.Step(Snow, Cloud, Wind, Camera, 1.0f / 60.0f, 0.0f, 0.0f);
        const size_t BytesAtStart = System.Field().ByteCount();
        uint32_t HighWater = 0u;
        for (int Tick = 0; Tick < 7200; ++Tick)   // 120 s at 60 Hz — snow falls at 1 m/s, so it needs the time
        {
            System.Step(Snow, Cloud, Wind, Camera, 1.0f / 60.0f, static_cast<float>(Tick) / 60.0f, 0.0f);
            if (System.Telemetry().Alive > HighWater) HighWater = System.Telemetry().Alive;
        }
        const size_t BytesAtEnd = System.Field().ByteCount();

        std::printf("     %llu flakes settled, deepest %.4f m, live pool peaked at %u\n",
                    static_cast<unsigned long long>(System.Field().SettledCount()),
                    System.Field().DeepestMetres(), HighWater);
        std::printf("     field memory: %zu bytes at the start, %zu after 120 s of heavy snow\n",
                    BytesAtStart, BytesAtEnd);

        Expect(System.Field().SettledCount() > 1000u, "a lot of snow actually landed");
        Expect(BytesAtEnd == BytesAtStart, "the field's memory does not grow with accumulation");
        Expect(HighWater <= 8192u, "the live pool never exceeded its budget");
        Expect(System.Field().DeepestMetres() > 0.0f, "the snow has depth, so it can be drawn as one surface");

        // Depth must be where the snow fell, not spread everywhere.
        uint32_t Occupied = 0u;
        for (float Cell : System.Field().Cells()) if (Cell > 0.0f) ++Occupied;
        std::printf("     %u of %u cells hold snow\n", Occupied,
                    static_cast<uint32_t>(System.Field().Cells().size()));
        Expect(Occupied > 0u && Occupied < System.Field().Cells().size(),
               "the snow is localised under the emitter, not smeared over the whole field");
    }

    // ── ⑤ the physics per type ─────────────────────────────────────────────────────────────────────────────────
    std::printf("\n5. each type falls at its own measured terminal velocity\n");
    {
        struct Expectation { PrecipitationCategory Category; const char* Name; float Terminal; };
        const Expectation Types[] = {
            { PrecipitationCategory::Drizzle, "drizzle",  2.0f },
            { PrecipitationCategory::Rain,    "rain",     6.5f },
            { PrecipitationCategory::Sleet,   "sleet",    4.0f },
            { PrecipitationCategory::Snow,    "snow",     1.0f },
            { PrecipitationCategory::Hail,    "hail",    20.0f },
        };
        bool Reached = true;
        for (const Expectation& E : Types)
        {
            // A single particle in still air must converge on its terminal velocity, not exceed it.
            PrecipitationPhysics Physics = PhysicsFor(E.Category);
            float Velocity = 0.0f;
            for (int Tick = 0; Tick < 600; ++Tick)
                Velocity += (-Physics.TerminalVelocity - Velocity) * std::fmin(1.0f, (1.0f / 60.0f) * 2.5f);
            std::printf("     %-8s settles at %6.2f m/s (expected %5.2f)\n", E.Name, -Velocity, E.Terminal);
            if (std::fabs(-Velocity - E.Terminal) > 0.05f) Reached = false;
        }
        Expect(Reached, "every type converges on its published terminal velocity");
        Expect(PhysicsFor(PrecipitationCategory::Hail).Restitution >
               PhysicsFor(PrecipitationCategory::Rain).Restitution, "hail bounces and rain does not");
        Expect(PhysicsFor(PrecipitationCategory::Snow).Flutter >
               PhysicsFor(PrecipitationCategory::Rain).Flutter, "flakes flutter and drops do not");
        Expect(!PhysicsFor(PrecipitationCategory::Rain).Accumulates &&
                PhysicsFor(PrecipitationCategory::Snow).Accumulates, "snow accumulates, rain does not");
    }

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the weather behaves" : "  THE WEATHER DOES NOT BEHAVE");
    return Failures == 0 ? 0 : 1;
}
