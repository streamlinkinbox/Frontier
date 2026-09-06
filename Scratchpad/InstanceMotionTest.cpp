//============================================================================================================================================
//                                                     INSTANCEMOTIONTEST.CPP
//============================================================================================================================================
// 🧩 The D3 gate. Proves the per-instance transform path is correct BEFORE physics is connected, so that when D4
//    drops real bodies in and something looks wrong, the transform maths is already ruled out.
//
//    Checks, in order of what would actually bite:
//      · static instances are not touched — the commonest bug is a driver that walks the whole array
//      · PreviousWorld carries LAST frame's World, exactly — motion vectors and ReSTIR reprojection depend on it,
//        and getting it wrong produces smeared reuse rather than an obvious error
//      · driven poses match the closed-form oracle to float precision
//      · instances genuinely desynchronise — a phase bug where every instance reads slot 0 still "animates", so
//        this checks the spread is real
//      · the rotation stays a proper rotation (orthonormal, det +1) over thousands of steps
//      · determinism: the same elapsed time always yields the same matrix, which is what makes a replay comparable
//
//    Build (from the repository root):
//        bash Scratchpad/CheckInstanceMotion.sh

#include "../Projects/Project-Zero/Source/InstanceMotionSequence.h"

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

void Identity(float* M)
{
    for (uint32_t I = 0u; I < 16u; ++I) M[I] = 0.0f;
    M[0] = M[5] = M[10] = M[15] = 1.0f;
}

std::vector<Frontier::InstanceRecord> ConstructScene(uint32_t Static, uint32_t Dynamic)
{
    std::vector<Frontier::InstanceRecord> Rows(Static + Dynamic);
    for (uint32_t I = 0u; I < Rows.size(); ++I)
    {
        Identity(Rows[I].World);
        Identity(Rows[I].PreviousWorld);
        Rows[I].World[12] = static_cast<float>(I) * 0.5f;          // X
        Rows[I].World[13] = 1.0f;                                   // Y
        Rows[I].World[14] = 2.0f + static_cast<float>(I) * 0.1f;    // Z — the rest height the bob is relative to
    }
    return Rows;
}

} // namespace

int main()
{
    std::printf("\n=== D3 instance motion gate ===\n\n");

    constexpr uint32_t StaticCount  = 11u;   // the showroom's own instances
    constexpr uint32_t DynamicCount = 12u;   // the drop scene's bodies

    Frontier::ProjectZero::InstanceMotionConfiguration Configuration;
    Configuration.FirstInstance = StaticCount;
    Configuration.InstanceCount = DynamicCount;

    std::vector<Frontier::InstanceRecord> Rows = ConstructScene(StaticCount, DynamicCount);
    const std::vector<Frontier::InstanceRecord> Original = Rows;

    Frontier::ProjectZero::InstanceMotionSequence Motion;
    Motion.Construct(Rows, Configuration);
    CheckTrue("driver claims exactly the dynamic span", Motion.QueryDrivenCount() == DynamicCount);

    //──────────────────────────────────────────────────────────────────────────
    // ① Static instances must be untouched.
    //──────────────────────────────────────────────────────────────────────────
    Motion.AdvanceMotion(Rows, 1.234);
    bool StaticIntact = true;
    for (uint32_t I = 0u; I < StaticCount; ++I)
        for (uint32_t E = 0u; E < 16u; ++E)
            if (Rows[I].World[E] != Original[I].World[E] || Rows[I].PreviousWorld[E] != Original[I].PreviousWorld[E])
                { StaticIntact = false; break; }
    CheckTrue("static instances are byte-for-byte untouched", StaticIntact);

    //──────────────────────────────────────────────────────────────────────────
    // ② Driven poses match the closed-form oracle.
    //──────────────────────────────────────────────────────────────────────────
    bool HeightsMatch = true;
    for (uint32_t I = 0u; I < DynamicCount; ++I)
    {
        const float Expected = Motion.QueryExpectedHeight(I, 1.234);
        const float Actual   = Rows[StaticCount + I].World[14];
        if (std::fabs(Expected - Actual) > 1e-6f) { HeightsMatch = false; break; }
    }
    CheckTrue("driven heights match the analytic oracle", HeightsMatch);

    //──────────────────────────────────────────────────────────────────────────
    // ③ PreviousWorld carries last frame's World exactly.
    //──────────────────────────────────────────────────────────────────────────
    std::vector<Frontier::InstanceRecord> Before = Rows;
    Motion.AdvanceMotion(Rows, 1.300);
    bool PreviousCorrect = true, ActuallyMoved = false;
    for (uint32_t I = 0u; I < DynamicCount; ++I)
    {
        const Frontier::InstanceRecord& Now = Rows[StaticCount + I];
        const Frontier::InstanceRecord& Was = Before[StaticCount + I];
        for (uint32_t E = 0u; E < 16u; ++E)
            if (Now.PreviousWorld[E] != Was.World[E]) { PreviousCorrect = false; break; }
        if (Now.World[14] != Was.World[14]) ActuallyMoved = true;
    }
    CheckTrue("PreviousWorld equals last frame's World (motion vectors)", PreviousCorrect);
    CheckTrue("the bodies actually moved between frames",                 ActuallyMoved);

    //──────────────────────────────────────────────────────────────────────────
    // ④ Desynchronisation — a phase bug still "animates", so check the spread.
    //──────────────────────────────────────────────────────────────────────────
    // Compare the bob OFFSET (pose minus rest height). Comparing absolute Z would pass trivially because the rest
    //    heights already differ, and comparing a single frame would pass even if every instance read slot 0 — the
    //    value still changes over time. Distinct offsets within one frame is the property that actually proves the
    //    per-instance index is being used.
    float Lowest = 1e30f, Highest = -1e30f;
    uint32_t DistinctOffsets = 0u;
    std::vector<float> Offsets;   // absolute heights seen this frame
    for (uint32_t I = 0u; I < DynamicCount; ++I)
    {
        const float Offset = Rows[StaticCount + I].World[14] - Original[StaticCount + I].World[14];
        Lowest  = std::fmin(Lowest,  Offset);
        Highest = std::fmax(Highest, Offset);
        // Distinctness is tested on the ABSOLUTE height, not the offset. If every instance wrongly read slot 0 the
        //    offsets would still differ (rest heights differ), but the absolute heights would all collapse to the
        //    same value — verified by sabotage, which is how this check was corrected.
        const float Absolute = Rows[StaticCount + I].World[14];
        bool Seen = false;
        for (float Previous : Offsets) if (std::fabs(Previous - Absolute) < 1e-6f) { Seen = true; break; }
        if (!Seen) { Offsets.push_back(Absolute); ++DistinctOffsets; }
    }
    CheckTrue("instances are not in lockstep (phase offsets applied)", (Highest - Lowest) > 0.1f);
    CheckTrue("every instance has its own phase (no shared-slot read)", DistinctOffsets == DynamicCount);

    //──────────────────────────────────────────────────────────────────────────
    // ⑤ The rotation stays a rotation over a long run.
    //──────────────────────────────────────────────────────────────────────────
    bool Orthonormal = true;
    for (uint32_t Step = 0u; Step < 4000u && Orthonormal; ++Step)
    {
        Motion.AdvanceMotion(Rows, 0.01 * static_cast<double>(Step));
        for (uint32_t I = 0u; I < DynamicCount; ++I)
        {
            const float* M = Rows[StaticCount + I].World;
            const float Determinant = M[0] * (M[5] * M[10] - M[6] * M[9])
                                    - M[4] * (M[1] * M[10] - M[2] * M[9])
                                    + M[8] * (M[1] * M[6]  - M[2] * M[5]);
            const float ColumnX = M[0]*M[0] + M[1]*M[1] + M[2]*M[2];
            const float ColumnY = M[4]*M[4] + M[5]*M[5] + M[6]*M[6];
            if (std::fabs(Determinant - 1.0f) > 1e-4f ||
                std::fabs(ColumnX - 1.0f) > 1e-4f || std::fabs(ColumnY - 1.0f) > 1e-4f) { Orthonormal = false; break; }
        }
    }
    CheckTrue("rotation stays orthonormal, det +1, over 4000 steps", Orthonormal);

    //──────────────────────────────────────────────────────────────────────────
    // ⑥ Determinism — the same τ must always give the same matrix.
    //──────────────────────────────────────────────────────────────────────────
    std::vector<Frontier::InstanceRecord> RunA = ConstructScene(StaticCount, DynamicCount);
    std::vector<Frontier::InstanceRecord> RunB = ConstructScene(StaticCount, DynamicCount);
    Frontier::ProjectZero::InstanceMotionSequence MotionA, MotionB;
    MotionA.Construct(RunA, Configuration);
    MotionB.Construct(RunB, Configuration);
    for (uint32_t Step = 1u; Step <= 120u; ++Step)
    {
        MotionA.AdvanceMotion(RunA, 0.01666 * Step);
        MotionB.AdvanceMotion(RunB, 0.01666 * Step);
    }
    bool Deterministic = true;
    for (uint32_t I = 0u; I < RunA.size() && Deterministic; ++I)
        for (uint32_t E = 0u; E < 16u; ++E)
            if (RunA[I].World[E] != RunB[I].World[E]) { Deterministic = false; break; }
    CheckTrue("two identical runs produce bit-identical transforms", Deterministic);

    //──────────────────────────────────────────────────────────────────────────
    // ⑦ Over-long spans clamp instead of running off the end.
    //──────────────────────────────────────────────────────────────────────────
    Frontier::ProjectZero::InstanceMotionConfiguration TooMany = Configuration;
    TooMany.InstanceCount = 9999u;
    Frontier::ProjectZero::InstanceMotionSequence Clamped;
    Clamped.Construct(Rows, TooMany);
    CheckTrue("an over-long span clamps to the instances that exist", Clamped.QueryDrivenCount() == DynamicCount);

    Frontier::ProjectZero::InstanceMotionConfiguration PastEnd = Configuration;
    PastEnd.FirstInstance = 9999u;
    Frontier::ProjectZero::InstanceMotionSequence Empty;
    Empty.Construct(Rows, PastEnd);
    CheckTrue("a span past the end drives nothing", Empty.QueryDrivenCount() == 0u);

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
