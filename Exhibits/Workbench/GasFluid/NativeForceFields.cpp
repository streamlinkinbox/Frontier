//============================================================================================================================================
// 📦 Exhibits/Workbench/GasFluid/NativeForceFields.cpp — the ported taxonomy, against the browser's numbers
//============================================================================================================================================
// The browser is the reference and this is the port, so the claims here are deliberately the SAME claims
//    Experimental/ParticleEditor/CheckForceFields.mjs makes, with the same numbers. Two files agreeing on
//    prose is worth nothing; two files agreeing on arithmetic is the only thing that makes this a port.

#include "../../../Engine/VolumetricDynamics/ForceFieldSet.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace Frontier;

namespace {

int Claims = 0;

void Check(bool Condition, const char* Claim)
{
    ++Claims;
    if (!Condition)
    {
        // 🔴 stdout is lost when a proof aborts through terminate, so the failing claim goes to stderr first.
        std::fprintf(stderr, "  FAIL  %s\n", Claim);
        throw std::runtime_error(Claim);
    }
    std::printf("  PASS  %s\n", Claim);
}

void Banner(const char* Title)
{
    std::printf("\n%s\n", Title);
}

bool Near(float A, float B, float Slack = 1e-5f)
{
    return std::fabs(A - B) <= Slack;
}

ForceField Made(ForceFieldKind Kind)
{
    ForceField Field;
    Field.Kind = Kind;
    const ForceFieldKindFacts& Facts = FactsOf(Kind);
    std::snprintf(Field.Name, sizeof(Field.Name), "%.*s", int(sizeof(Field.Name) - 1), Facts.Name);
    Field.Reaches  = Facts.Reaches;
    Field.Strength = (Kind == ForceFieldKind::Gravity) ? 9.81f : 5.0f;
    return Field;
}

} // namespace

int main()
{
    std::printf("NativeForceFields - the ported taxonomy against the browser's own numbers\n");

    //---------------------------------------------------------------------------------------------------------
    Banner("The umbrella describes what already exists");
    {
        int Flows = 0, Accelerates = 0, Damps = 0;
        for (int At = 0; At < int(ForceFieldKind::Count); ++At)
        {
            const ForceFieldKindFacts& Facts = FactsOf(ForceFieldKind(At));
            if (Facts.Give == ForceContribution::Flow)             ++Flows;
            else if (Facts.Give == ForceContribution::Accelerate)  ++Accelerates;
            else                                                   ++Damps;
        }
        Check(int(ForceFieldKind::Count) == 12, "twelve kinds, as the browser lists twelve");
        Check(Flows == 5 && Accelerates == 6 && Damps == 1,
              "five flow, six acceleration, one damp - the same split the reference has");

        int Lattice = 0, Forces = 0;
        for (int At = 0; At < 4; ++At)  if (FactsOf(ForceFieldKind(At)).Lattice == At) ++Lattice;
        for (int At = 4; At < 8; ++At)  if (FactsOf(ForceFieldKind(At)).Force == At - 4) ++Forces;
        Check(Lattice == 4, "the four wind components keep the lattice slots they already had");
        Check(Forces == 4, "and the four force types keep the shader slots they already had");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("\xf0\x9f\x94\xb4 Why gravity cannot simply be added to the wind");
    {
        const float Step = 1.0f / 60.0f;
        const float Leaf = 6.0f, Stone = 0.05f;
        const float At[3] = { 0.0f, 2.0f, 0.0f };

        ForceField Right = Made(ForceFieldKind::Gravity);
        const ForceCrossing Acting = ResolveForces(&Right, 1u, At, 0.0f, 0u);

        float LeafRight[3] = { 0, 0, 0 }, StoneRight[3] = { 0, 0, 0 };
        AdvanceByForces(Acting, Leaf, Step, LeafRight);
        AdvanceByForces(Acting, Stone, Step, StoneRight);
        Check(Near(LeafRight[1], -9.81f * Step) && Near(StoneRight[1], -9.81f * Step),
              "as an acceleration, a leaf and a hailstone change by the SAME amount in one step");

        ForceField Wrong = Made(ForceFieldKind::Prevailing);
        Wrong.Direction[0] = 0.0f; Wrong.Direction[1] = -1.0f; Wrong.Direction[2] = 0.0f;
        Wrong.Strength = 9.81f;
        Wrong.Reaches = ForceReach::Everywhere;
        const ForceCrossing Mistaken = ResolveForces(&Wrong, 1u, At, 0.0f, 0u);

        float LeafWrong[3] = { 0, 0, 0 }, StoneWrong[3] = { 0, 0, 0 }, Deaf[3] = { 0, 0, 0 };
        AdvanceByForces(Mistaken, Leaf, Step, LeafWrong);
        AdvanceByForces(Mistaken, Stone, Step, StoneWrong);
        AdvanceByForces(Mistaken, 0.0f, Step, Deaf);
        Check(std::fabs(LeafWrong[1]) > std::fabs(StoneWrong[1]) * 50.0f,
              "\xe2\x9d\x8c as a flow, the leaf falls more than fifty times faster, which is backwards");
        Check(Near(Deaf[1], 0.0f),
              "\xe2\x9d\x8c and a receiver with no coupling ignores smuggled-in gravity completely");

        float Heard[3] = { 0, 0, 0 };
        AdvanceByForces(Acting, 0.0f, Step, Heard);
        Check(Near(Heard[1], -9.81f * Step), "\xe2\x9c\x94\xef\xb8\x8f whereas real gravity reaches it, as it must");

        // Terminal speed is built into a flow and absent from an acceleration.
        ForceField Air = Made(ForceFieldKind::Prevailing);
        Air.Reaches = ForceReach::Everywhere;
        Air.Strength = 8.0f;
        ForceAlong(90.0f, Air.Direction);                      // due east, so the motion is in X
        ForceField Thrust = Made(ForceFieldKind::Gravity);
        Thrust.Strength = 8.0f;

        const float Origin[3] = { 0.0f, 0.0f, 0.0f };
        const ForceCrossing Wind = ResolveForces(&Air, 1u, Origin, 0.0f, 0u);
        const ForceCrossing Push = ResolveForces(&Thrust, 1u, Origin, 0.0f, 0u);
        float Blown[3] = { 0, 0, 0 }, Pushed[3] = { 0, 0, 0 };
        for (int Tick = 0; Tick < 600; ++Tick)
        {
            AdvanceByForces(Wind, 2.0f, Step, Blown);
            AdvanceByForces(Push, 2.0f, Step, Pushed);
        }
        Check(Near(Blown[0], 8.0f, 0.01f),
              "\xe2\x9c\x94\xef\xb8\x8f ten seconds in an 8 m/s wind leaves a receiver at 8 m/s and no faster");
        Check(std::fabs(Pushed[1]) > 70.0f,
              "\xe2\x9c\x94\xef\xb8\x8f while the same number as an acceleration is past 70 m/s and still climbing");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("\xf0\x9f\x94\xb4 The absence of wind is not a wind of zero");
    {
        const float Origin[3] = { 0.0f, 0.0f, 0.0f };

        const ForceCrossing Nothing = ResolveForces(nullptr, 0u, Origin, 0.0f, 0u);
        Check(!Nothing.Flowing, "with no fields at all, nothing is flowing");
        float Coasting[3] = { 10.0f, 0.0f, 0.0f };
        AdvanceByForces(Nothing, 5.0f, 0.5f, Coasting);
        Check(Near(Coasting[0], 10.0f), "so a fast receiver coasts untouched - empty space does not brake it");

        ForceField Distant = Made(ForceFieldKind::Gust);
        Distant.Centre[0] = 500.0f;
        Distant.Radius = 10.0f;
        const ForceCrossing Far = ResolveForces(&Distant, 1u, Origin, 0.0f, 0u);
        float Still[3] = { 10.0f, 0.0f, 0.0f };
        AdvanceByForces(Far, 5.0f, 0.5f, Still);
        Check(!Far.Flowing && Near(Still[0], 10.0f), "and neither does a gust a long way away");

        ForceField Calm = Made(ForceFieldKind::Prevailing);
        Calm.Reaches = ForceReach::Everywhere;
        Calm.Strength = 0.0f;
        const ForceCrossing Air = ResolveForces(&Calm, 1u, Origin, 0.0f, 0u);
        float Slowed[3] = { 10.0f, 0.0f, 0.0f };
        AdvanceByForces(Air, 5.0f, 0.5f, Slowed);
        Check(Air.Flowing && Slowed[0] < 10.0f,
              "\xe2\x9c\x94\xef\xb8\x8f but air that is deliberately still IS flowing, and does slow a receiver");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Damping is the third, and is not either of the others");
    {
        const float Origin[3] = { 0.0f, 0.0f, 0.0f };
        ForceField Water = Made(ForceFieldKind::Drag);
        Water.Reaches = ForceReach::Everywhere;
        Water.Strength = 4.0f;
        const ForceCrossing Acting = ResolveForces(&Water, 1u, Origin, 0.0f, 0u);
        Check(Near(Acting.Damp, 4.0f) && !Acting.Flowing,
              "a drag volume contributes a rate and no direction, and is not air");

        float Slowed[3] = { 10.0f, 0.0f, 0.0f };
        AdvanceByForces(Acting, 0.0f, 0.25f, Slowed);
        Check(Near(Slowed[0], 10.0f * std::exp(-1.0f), 1e-4f), "and scales velocity down exponentially");

        float Resting[3] = { 0.0f, 0.0f, 0.0f };
        AdvanceByForces(Acting, 0.0f, 0.25f, Resting);
        Check(Resting[0] == 0.0f && Resting[1] == 0.0f && Resting[2] == 0.0f,
              "\xf0\x9f\x94\xb4 damping something at rest does nothing - unlike a force it cannot start motion");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Reach, falloff and the time window");
    {
        ForceField Ball = Made(ForceFieldKind::Attract);
        Ball.Radius = 10.0f;
        Ball.Strength = 4.0f;
        Ball.Fades = ForceFalloff::None;

        float Out[3];
        const float Outside[3] = { 20.0f, 0.0f, 0.0f };
        SampleForceField(Ball, Outside, 0.0f, Out);
        Check(Out[0] == 0.0f && Out[1] == 0.0f && Out[2] == 0.0f, "outside the radius a field gives nothing");

        const float Inside[3] = { 5.0f, 0.0f, 0.0f };
        SampleForceField(Ball, Inside, 0.0f, Out);
        Check(Near(Out[0], -4.0f), "inside it, an attractor pulls toward its centre at full strength");

        ForceField All = Made(ForceFieldKind::Gravity);
        const float Distant[3] = { 9999.0f, 9999.0f, 9999.0f };
        SampleForceField(All, Distant, 0.0f, Out);
        Check(Near(Out[1], -9.81f),
              "\xf0\x9f\x94\xb4 Everywhere is not a large sphere: it reaches a point ten kilometres out");

        Check(Near(ForceFalls(ForceFalloff::None, 9.0f, 10.0f), 1.0f), "None holds full strength to the edge");
        Check(Near(ForceFalls(ForceFalloff::Linear, 5.0f, 10.0f), 0.5f), "Linear is half way at half way");
        Check(Near(ForceFalls(ForceFalloff::Smooth, 10.0f, 10.0f), 0.0f), "Smooth runs to zero at the edge");
        for (int Shape = 0; Shape <= 3; ++Shape)
        {
            Check(ForceFalls(ForceFalloff(Shape), 11.0f, 10.0f) >= 0.0f,
                  "no falloff goes negative past the edge, which would push instead of pull");
        }

        ForceField Timed = Made(ForceFieldKind::Repel);
        Timed.Begins = 2.0f; Timed.Lasts = 1.0f; Timed.Repeats = 5.0f;
        Check(!ForceAwake(Timed, 1.9f) && ForceAwake(Timed, 2.5f) && !ForceAwake(Timed, 3.5f),
              "a timed field is awake only inside its window");
        Check(ForceAwake(Timed, 7.5f) && !ForceAwake(Timed, 9.0f), "and again one period later");
        Timed.Enabled = false;
        Check(!ForceAwake(Timed, 2.5f), "a disabled field is never awake");
        Check(ForceAwake(Made(ForceFieldKind::Gravity), 1e6f), "and one with no duration never stops");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Who responds is configuration, not code");
    {
        const float Origin[3] = { 0.0f, 0.0f, 0.0f };
        ForceField Everything = Made(ForceFieldKind::Gravity);
        Check(ForceActsOn(Everything, 0u) && ForceActsOn(Everything, 0x1u),
              "a field naming no channels acts on everything, so the simple case stays simple");

        ForceField OnlyRubble = Made(ForceFieldKind::Gravity);
        OnlyRubble.Acts = 0x2u;
        Check(ForceActsOn(OnlyRubble, 0x2u) && !ForceActsOn(OnlyRubble, 0x1u),
              "\xf0\x9f\x94\xb4 and one naming a channel acts only on receivers in it - authored, never compiled in");

        ForceField Both[2] = { OnlyRubble, Made(ForceFieldKind::Lift) };
        Both[1].Reaches = ForceReach::Everywhere;
        Both[1].Strength = 2.0f;
        const ForceCrossing Smoke = ResolveForces(Both, 2u, Origin, 0.0f, 0x1u);
        Check(Near(Smoke.Accelerate[1], 2.0f),
              "resolving for smoke gets the lift and not the rubble-only gravity, in one pass over one list");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("What can be summed into the lattice, and what cannot");
    {
        ForceField Wind = Made(ForceFieldKind::Prevailing);
        ForceField Twister = Made(ForceFieldKind::Tornado);
        ForceField Down = Made(ForceFieldKind::Gravity);
        ForceField Pull = Made(ForceFieldKind::Attract);
        ForceField Stream = Made(ForceFieldKind::Current);
        ForceField Picky = Made(ForceFieldKind::Prevailing);
        Picky.Acts = 0x4u;

        Check(ForceBakeable(Wind) && ForceBakeable(Twister),
              "the unselective flow fields sum into one lattice, however many there are");
        Check(!ForceBakeable(Down) && !ForceBakeable(Pull),
              "the acceleration fields stay live - a bounded texture cannot hold an unbounded pull");
        Check(!ForceBakeable(Picky),
              "\xf0\x9f\x92\xa1 and a flow field that names channels also stays live, because one shared "
              "texture cannot both include and exclude the same receiver");
        Check(!ForceBakeable(Stream) && !ForcePackable(ForceFieldKind::Current),
              "a kind with no shader path is not bakeable and says so");
        Check(!ForcePackable(ForceFieldKind::Drag), "nor is the damp kind, yet");
        for (int At = 0; At < 10; ++At)
        {
            Check(ForcePackable(ForceFieldKind(At)), "every kind with a shader slot reports that it packs");
        }
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Reaching the shaders");
    {
        ForceAccelerationEntry Entry;
        Check(PackForceAcceleration(Made(ForceFieldKind::Gravity), Entry), "gravity packs");
        Check(Entry.Radius == 0.0f,
              "\xf0\x9f\x94\xb4 a radius of 0 is what the shader reads as everywhere - packing the authored "
              "8 m would have made gravity a small ball");
        Check(Near(Entry.Strength, -9.81f), "and it packs as a downward lift; the sign is the whole mapping");
        Check(Near(Entry.Lasts, 1e9f), "with no duration packing as effectively forever");

        ForceAccelerationEntry Up;
        PackForceAcceleration(Made(ForceFieldKind::Lift), Up);
        Check(Up.Strength > 0.0f, "while lift itself still points up");

        ForceLatticeEntry Component;
        ForceField Gust = Made(ForceFieldKind::Gust);
        ForceAlong(70.0f, Gust.Direction);
        Gust.Centre[0] = -4.0f;
        Gust.Radius = 4.0f;
        Gust.Strength = 5.0f;
        Gust.Rate = 0.3f;
        Check(PackForceLattice(Gust, Component), "a gust packs into a lattice component");
        Check(Near(Component.Bearing, 70.0f, 1e-3f) && Near(Component.X, -4.0f)
              && Near(Component.Strength, 5.0f) && Near(Component.Rate, 0.3f) && Component.Kind == 1.0f,
              "carrying the same eight readings the hand-written wind component carried");
        Check(!PackForceLattice(Made(ForceFieldKind::Gravity), Component),
              "and an acceleration field refuses to pack as one rather than packing as nonsense");

        for (float Degrees : { 0.0f, 70.0f, 90.0f, 180.0f, 269.0f })
        {
            ForceField One = Made(ForceFieldKind::Prevailing);
            ForceAlong(Degrees, One.Direction);
            Check(Near(ForceBearing(One), Degrees, 1e-3f), "a bearing survives the trip through a direction");
        }
        ForceField Back = Made(ForceFieldKind::Prevailing);
        ForceAlong(-90.0f, Back.Direction);
        Check(Near(ForceBearing(Back), 270.0f, 1e-3f), "and comes back on the compass, never negative");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Order of application is fixed, because it is observable");
    {
        const float Origin[3] = { 0.0f, 0.0f, 0.0f };
        ForceField Pair[2] = { Made(ForceFieldKind::Prevailing), Made(ForceFieldKind::Gravity) };
        Pair[0].Reaches = ForceReach::Everywhere;
        Pair[0].Strength = 10.0f;
        ForceAlong(90.0f, Pair[0].Direction);
        Pair[1].Strength = 10.0f;

        const ForceCrossing Acting = ResolveForces(Pair, 2u, Origin, 0.0f, 0u);
        float Velocity[3] = { 0.0f, 0.0f, 0.0f };
        AdvanceByForces(Acting, 1.0f, 0.5f, Velocity);
        Check(Near(Velocity[0], 5.0f) && Near(Velocity[1], -5.0f),
              "flow first, then acceleration, then damping: 5 m/s across and 5 m/s down in a half-second "
              "step, and any other order gives different numbers");
    }

    std::printf("\nPASS %d\n", Claims);
    return 0;
}
