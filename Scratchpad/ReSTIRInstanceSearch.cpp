//============================================================================================================================================
// 📦 Scratchpad/ReSTIRInstanceSearch.cpp — proves the binary InstanceOfPrimitive matches the linear walk exactly
//============================================================================================================================================
// #6 replaces an O(instances) walk with an O(log instances) search. Same answer, fewer steps — but "same answer"
//    is precisely the sort of claim that has already failed once this round, so it is tested rather than argued.
//
// The subtle case is the DUPLICATE OFFSET. An instance with no triangles gets its predecessor's offset, because
//    SceneStructure::Finalise assigns the offset from the current size of a growing vector before appending. The
//    linear walk kept advancing while `offset <= slot`, so it settled on the LAST instance of such a run. A binary
//    search that converges to the first equal element would hand the primitive to a different instance — and since
//    the instance decides the material, that is a wrong material on a triangle, not a crash. ReSTIR then reuses
//    that hit across frames, so it would surface as drifting shading nobody traces back to a search rewrite.
//
// So the harness builds scenes that are dense in empty instances, and compares every primitive slot in each.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

uint32_t PcgHash(uint32_t Input) noexcept
{
    uint32_t State = Input * 747796405u + 2891336453u;
    uint32_t Word  = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

// The walk as it stood in ReSTIRViewport.slang before #6.
uint32_t LinearWalk(const std::vector<uint32_t>& Offsets, uint32_t FlatPrimitive) noexcept
{
    uint32_t Found = 0u;
    for (uint32_t I = 0u; I + 1u < static_cast<uint32_t>(Offsets.size()); ++I)
    {
        if (Offsets[I + 1u] <= FlatPrimitive) Found = I + 1u;
        else break;
    }
    return Found;
}

// The search as it stands after #6 — transcribed line for line from the kernel.
uint32_t BinarySearch(const std::vector<uint32_t>& Offsets, uint32_t FlatPrimitive) noexcept
{
    uint32_t Low  = 0u;
    uint32_t High = static_cast<uint32_t>(Offsets.size()) - 1u;
    while (Low < High)
    {
        const uint32_t Mid = Low + (High - Low + 1u) / 2u;
        if (Offsets[Mid] <= FlatPrimitive) Low  = Mid;
        else                               High = Mid - 1u;
    }
    return Low;
}

// Builds a scene's offset table. EmptyChance is the probability an instance carries no triangles, which is what
//    produces the duplicate offsets the two searches could disagree about.
std::vector<uint32_t> BuildScene(uint32_t InstanceCount, uint32_t& Seed, float EmptyChance, uint32_t& OutTotal)
{
    std::vector<uint32_t> Offsets;
    Offsets.reserve(InstanceCount);
    uint32_t Running = 0u;
    for (uint32_t I = 0u; I < InstanceCount; ++I)
    {
        Offsets.push_back(Running);
        Seed = PcgHash(Seed);
        const float U = static_cast<float>(Seed) * (1.0f / 4294967296.0f);
        if (U >= EmptyChance)
        {
            Seed = PcgHash(Seed);
            Running += 1u + (Seed % 40u);
        }
    }
    OutTotal = Running == 0u ? 1u : Running;
    return Offsets;
}

}   // namespace

int main()
{
    struct Case { const char* Name; uint32_t Instances; float EmptyChance; };
    const Case Cases[] = {
        { "Cornell-sized",           2u,   0.00f },
        { "small scene",            16u,   0.00f },
        { "with empty instances",   64u,   0.35f },
        { "dense empties",         256u,   0.70f },
        { "large scene",          4096u,   0.10f },
        { "pathological empties", 1024u,   0.95f },
    };

    uint32_t Seed = 0xC0FFEEu;
    uint64_t Compared = 0u, Mismatches = 0u;
    uint64_t LinearSteps = 0u, BinarySteps = 0u;

    std::printf("[InstanceSearch] linear walk vs binary search, every primitive slot in each scene\n\n");
    std::printf("  %-22s %10s %10s %12s %12s %10s\n",
                "scene", "instances", "slots", "walk steps", "search steps", "mismatch");

    for (const Case& C : Cases)
    {
        uint32_t Total = 0u;
        const std::vector<uint32_t> Offsets = BuildScene(C.Instances, Seed, C.EmptyChance, Total);

        uint64_t CaseLinear = 0u, CaseBinary = 0u, CaseMismatch = 0u;
        for (uint32_t Slot = 0u; Slot < Total; ++Slot)
        {
            const uint32_t A = LinearWalk(Offsets, Slot);
            const uint32_t B = BinarySearch(Offsets, Slot);
            if (A != B) { ++CaseMismatch; ++Mismatches; }
            ++Compared;

            // Step counts, for the cost claim rather than the correctness one.
            for (uint32_t I = 0u; I + 1u < C.Instances; ++I) { ++CaseLinear; if (Offsets[I + 1u] > Slot) break; }
            uint32_t Lo = 0u, Hi = C.Instances - 1u;
            while (Lo < Hi) { const uint32_t M = Lo + (Hi - Lo + 1u) / 2u; if (Offsets[M] <= Slot) Lo = M; else Hi = M - 1u; ++CaseBinary; }
        }
        LinearSteps += CaseLinear; BinarySteps += CaseBinary;

        std::printf("  %-22s %10u %10u %12llu %12llu %10llu\n", C.Name, C.Instances, Total,
                    static_cast<unsigned long long>(CaseLinear), static_cast<unsigned long long>(CaseBinary),
                    static_cast<unsigned long long>(CaseMismatch));
    }

    std::printf("\n  slots compared              %llu\n", static_cast<unsigned long long>(Compared));
    std::printf("  mismatches                  %llu\n", static_cast<unsigned long long>(Mismatches));
    std::printf("  total steps  walk %llu, search %llu  (%.1fx fewer)\n",
                static_cast<unsigned long long>(LinearSteps), static_cast<unsigned long long>(BinarySteps),
                static_cast<double>(LinearSteps) / static_cast<double>(BinarySteps));

    std::printf("\n[InstanceSearch] %s\n", Mismatches == 0u
        ? "IDENTICAL — the search returns exactly what the walk returned"
        : "*** DIVERGENT — the search does NOT match the walk ***");
    return Mismatches == 0u ? 0 : 1;
}
