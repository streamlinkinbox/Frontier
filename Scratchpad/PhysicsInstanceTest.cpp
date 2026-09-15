//============================================================================================================================================
//                                                    PHYSICSINSTANCETEST.CPP
//============================================================================================================================================
// 🧩 The D4 gate. Drops real Jolt bodies into the real showroom collider set and checks the InstanceRecord rows
//    the renderer would upload — the whole path except the GPU.
//
//    The check that matters most is the FIRST one: at the rest pose, every drop instance's World matrix must be
//    the identity. Showroom geometry is baked in world space, so if the pose bridge forgot to subtract the rest
//    origin, frame zero would silently place every ball at double its intended offset. That failure looks like a
//    physics bug and is not one, which is exactly why it is asserted before anything moves.
//
//    Build: bash Scratchpad/CheckPhysicsInstances.sh

#include "../Projects/Project-Zero/Source/PhysicsInstanceSequence.h"
#include "../Projects/Project-Zero/Source/ShowroomStructure.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-64s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

void WriteIdentity(float* M)
{
    for (uint32_t I = 0u; I < 16u; ++I) M[I] = 0.0f;
    M[0] = M[5] = M[10] = M[15] = 1.0f;
}

bool NearlyIdentity(const float* M, float Tolerance)
{
    for (uint32_t C = 0u; C < 4u; ++C)
        for (uint32_t R = 0u; R < 4u; ++R)
        {
            const float Expected = (C == R) ? 1.0f : 0.0f;
            if (std::fabs(M[C * 4u + R] - Expected) > Tolerance) return false;
        }
    return true;
}

} // namespace

int main()
{
    std::printf("\n=== D4 physics → instance transform gate ===\n\n");

    constexpr uint32_t StaticInstances = 11u;
    constexpr uint32_t DropCount       = 12u;

    // Instance rows as the renderer would hold them: identity transforms, geometry already baked in world space.
    std::vector<Frontier::InstanceRecord> Rows(StaticInstances + DropCount);
    for (Frontier::InstanceRecord& Row : Rows) { WriteIdentity(Row.World); WriteIdentity(Row.PreviousWorld); }
    const std::vector<Frontier::InstanceRecord> Original = Rows;

    Frontier::RigidBodyConfiguration SolverConfiguration;
    SolverConfiguration.FixedStepSeconds = 1.0f / 60.0f;
    Frontier::RigidBodySolver Solver;
    CheckTrue("solver comes up", Solver.Bring(SolverConfiguration));

    Frontier::ProjectZero::PhysicsInstanceConfiguration Configuration;
    Configuration.FirstDropInstance = StaticInstances;
    Configuration.DropCount         = DropCount;
    Configuration.BodyRadius        = Frontier::ProjectZero::ShowroomStructure::QueryDropRadius();

    Frontier::ProjectZero::PhysicsInstanceSequence Physics;
    CheckTrue("drop scene constructs", Physics.Construct(Solver, Configuration));
    CheckTrue("one body per drop instance", Physics.QueryBodyCount() == DropCount);

    //──────────────────────────────────────────────────────────────────────────
    // ① Frame zero must be the identity — the rest-pose subtraction check.
    //──────────────────────────────────────────────────────────────────────────
    Physics.AdvancePhysics(Solver, Rows, 0.0f);
    bool RestIsIdentity = true;
    for (uint32_t Body = 0u; Body < DropCount; ++Body)
        if (!NearlyIdentity(Rows[StaticInstances + Body].World, 1e-4f)) { RestIsIdentity = false; break; }
    CheckTrue("at the rest pose every drop transform is the identity", RestIsIdentity);

    bool StaticUntouched = true;
    for (uint32_t I = 0u; I < StaticInstances; ++I)
        for (uint32_t E = 0u; E < 16u; ++E)
            if (Rows[I].World[E] != Original[I].World[E]) { StaticUntouched = false; break; }
    CheckTrue("static showroom instances are never written", StaticUntouched);

    //──────────────────────────────────────────────────────────────────────────
    // ② Free fall, then settling.
    //──────────────────────────────────────────────────────────────────────────
    // Witness the HIGHEST body, not the lowest. QueryLowestHeight() tracks whichever body is currently lowest,
    //    and the lowest one lands after ~0.35 s — comparing its floor-limited travel against free-fall theory is
    //    a broken assertion, which is how the first version of this check failed for the wrong reason.
    const uint32_t Witness = DropCount - 1u;   // top of the stack: still airborne at t = 0.25 s
    const Frontier::Vector3 WitnessRest = Frontier::ProjectZero::ShowroomStructure::QueryDropOrigin(Witness);
    const float WitnessStart = WitnessRest.z;

    constexpr float FallSeconds = 0.25f;
    const uint32_t FallSteps = static_cast<uint32_t>(FallSeconds * 60.0f);
    for (uint32_t Step = 0u; Step < FallSteps; ++Step) Physics.AdvancePhysics(Solver, Rows, 1.0f / 60.0f);

    const float WitnessNow = WitnessRest.z + Rows[StaticInstances + Witness].World[14];
    const float Dropped    = WitnessStart - WitnessNow;
    const float Analytic   = 0.5f * 9.81f * FallSeconds * FallSeconds;
    std::printf("\n  free fall after %.2f s: top body dropped %.3f m (analytic %.3f m)\n",
                static_cast<double>(FallSeconds), Dropped, Analytic);
    // Symplectic Euler at 60 Hz overshoots ½gt² by a couple of percent; the band allows that and nothing more.
    CheckTrue("the airborne body falls at the analytic rate", Dropped > Analytic * 0.9f && Dropped < Analytic * 1.2f);

    bool Moved = false;
    for (uint32_t Body = 0u; Body < DropCount; ++Body)
        if (!NearlyIdentity(Rows[StaticInstances + Body].World, 1e-3f)) { Moved = true; break; }
    CheckTrue("transforms are no longer the identity once bodies move", Moved);

    // PreviousWorld must trail World by exactly one frame.
    std::vector<Frontier::InstanceRecord> Before = Rows;
    Physics.AdvancePhysics(Solver, Rows, 1.0f / 60.0f);
    bool PreviousTrails = true;
    for (uint32_t Body = 0u; Body < DropCount; ++Body)
        for (uint32_t E = 0u; E < 16u; ++E)
            if (Rows[StaticInstances + Body].PreviousWorld[E] != Before[StaticInstances + Body].World[E])
                { PreviousTrails = false; break; }
    CheckTrue("PreviousWorld carries last frame's World (motion vectors)", PreviousTrails);

    //──────────────────────────────────────────────────────────────────────────
    // ③ Run to rest and check the bodies stayed in the room.
    //──────────────────────────────────────────────────────────────────────────
    for (uint32_t Step = 0u; Step < 1200u; ++Step) Physics.AdvancePhysics(Solver, Rows, 1.0f / 60.0f);
    const float Radius = Frontier::ProjectZero::ShowroomStructure::QueryDropRadius();
    const float Resting = Physics.QueryLowestHeight();
    std::printf("  after 20 s: %u bodies awake, lowest centre %.4f m (radius %.2f m)\n",
                Physics.QueryActiveCount(), Resting, Radius);

    // A resting sphere's centre sits one radius above the floor, less Jolt's penetration slop (0.02 m default).
    CheckTrue("no body fell through the floor", Resting > Radius - 0.06f);
    CheckTrue("no body escaped upward",         Resting < 3.0f);
    CheckTrue("the scene settles (all bodies asleep)", Physics.QueryActiveCount() == 0u);

    bool InsideRoom = true;
    for (uint32_t Body = 0u; Body < DropCount; ++Body)
    {
        const float* M = Rows[StaticInstances + Body].World;
        // Translation column plus the baked rest origin gives the body's world centre.
        const Frontier::Vector3 Rest = Frontier::ProjectZero::ShowroomStructure::QueryDropOrigin(Body);
        const float X = M[12] + Rest.x, Y = M[13] + Rest.y;
        if (X < -2.2f || X > 2.2f || Y < -2.2f || Y > 3.2f) { InsideRoom = false; break; }
    }
    CheckTrue("every body is still inside the room", InsideRoom);

    //──────────────────────────────────────────────────────────────────────────
    // ④ Determinism — a fixed step must replay identically.
    //──────────────────────────────────────────────────────────────────────────
    const auto Replay = [&](std::vector<Frontier::InstanceRecord>& Out)
    {
        Out.assign(StaticInstances + DropCount, Frontier::InstanceRecord{});
        for (Frontier::InstanceRecord& Row : Out) { WriteIdentity(Row.World); WriteIdentity(Row.PreviousWorld); }
        Frontier::RigidBodySolver Local;
        (void)Local.Bring(SolverConfiguration);
        Frontier::ProjectZero::PhysicsInstanceSequence Sequence;
        (void)Sequence.Construct(Local, Configuration);
        for (uint32_t Step = 0u; Step < 240u; ++Step) Sequence.AdvancePhysics(Local, Out, 1.0f / 60.0f);
    };
    std::vector<Frontier::InstanceRecord> RunA, RunB;
    Replay(RunA);
    Replay(RunB);
    bool Deterministic = true;
    for (uint32_t I = 0u; I < RunA.size() && Deterministic; ++I)
        for (uint32_t E = 0u; E < 16u; ++E)
            if (RunA[I].World[E] != RunB[I].World[E]) { Deterministic = false; break; }
    CheckTrue("two fixed-step runs produce bit-identical transforms", Deterministic);

    Solver.Retire();
    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
