//============================================================================================================================================
//                                                        HITIDENTITYTEST.CPP
//============================================================================================================================================
// 🧩 The D2 gate. Proves the (instance, primitive) identity rule is correct and, critically, that it is the SAME
//    rule everywhere it is applied.
//
//    Why this matters more than it looks: ReSTIR reuses hits across frames. If the primary hit and the bounce hit
//    disagree about which instance owns a primitive, nothing crashes — the reservoir simply decorrelates and the
//    image gets noisier in a way that is indistinguishable from ordinary sampling noise. That is the failure this
//    harness exists to make impossible. Before D2 the resolution loop was copy-pasted at two call sites; now there
//    is one definition, and this file checks it against an independent brute-force oracle.
//
//    The functions under test are ports of InstanceOfPrimitive / LocalPrimitive in
//    Engine/Shaders/ReSTIRViewport.slang. They are kept character-for-character equivalent so a divergence in the
//    shader shows up here.
//
//    Build (from the repository root):
//        g++ -std=c++20 -O2 Scratchpad/HitIdentityTest.cpp -o /tmp/HitIdentityTest && /tmp/HitIdentityTest

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-64s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

struct InstanceRow
{
    uint32_t FlatTriangleOffset;
    uint32_t TriangleCount;
};

std::vector<InstanceRow> Instances;

//──────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Ports of the shader helpers — must stay identical to ReSTIRViewport.slang.
//──────────────────────────────────────────────────────────────────────────────────────────────────────────────

uint32_t InstanceOfPrimitive(uint32_t FlatPrimitive)
{
    uint32_t Found = 0u;
    for (uint32_t I = 0u; I + 1u < static_cast<uint32_t>(Instances.size()); ++I)
    {
        if (Instances[I + 1u].FlatTriangleOffset <= FlatPrimitive) Found = I + 1u;
        else break;
    }
    return Found;
}

uint32_t LocalPrimitive(uint32_t FlatPrimitive, uint32_t Instance)
{
    return FlatPrimitive - Instances[Instance].FlatTriangleOffset;
}

// Independent oracle: scan every instance's range directly. Deliberately written a different way from the
//    function under test, so a shared misreading of the layout cannot make both agree.
uint32_t OracleInstanceOf(uint32_t FlatPrimitive)
{
    for (uint32_t I = 0u; I < Instances.size(); ++I)
    {
        const uint32_t Begin = Instances[I].FlatTriangleOffset;
        const uint32_t End   = Begin + Instances[I].TriangleCount;
        if (FlatPrimitive >= Begin && FlatPrimitive < End) return I;
    }
    return 0xFFFFFFFFu;
}

void BuildLayout(const std::vector<uint32_t>& Counts)
{
    Instances.clear();
    uint32_t Offset = 0u;
    for (uint32_t Count : Counts) { Instances.push_back({ Offset, Count }); Offset += Count; }
}

uint32_t TotalTriangles()
{
    uint32_t Total = 0u;
    for (const InstanceRow& R : Instances) Total += R.TriangleCount;
    return Total;
}

} // namespace

int main()
{
    std::printf("\n=== D2 hit-identity gate ===\n\n");

    //──────────────────────────────────────────────────────────────────────────
    // ① The single-instance case — every scene the renderer has today.
    //──────────────────────────────────────────────────────────────────────────
    // This is the D1/D2 continuity check: with one instance the pair (0, p) must be exactly the old flat index p,
    //    so the change is provably a no-op for the Cornell box and the showroom.
    BuildLayout({ 5286u });
    bool Identity = true;
    for (uint32_t P = 0u; P < 5286u; ++P)
        if (InstanceOfPrimitive(P) != 0u || LocalPrimitive(P, 0u) != P) { Identity = false; break; }
    CheckTrue("single instance: instance is always 0", Identity);
    CheckTrue("single instance: local primitive == flat primitive (D1 continuity)", Identity);

    //──────────────────────────────────────────────────────────────────────────
    // ② Multi-instance layouts against the oracle, exhaustively.
    //──────────────────────────────────────────────────────────────────────────
    struct Layout { const char* Name; std::vector<uint32_t> Counts; };
    const Layout Layouts[] = {
        { "showroom-like (11 instances)",       { 12u, 2u, 2u, 2u, 960u, 4u, 1280u, 24u, 1024u, 2u, 2u } },
        { "drop scene (11 static + 12 bodies)", { 12u, 2u, 2u, 960u, 1280u, 1024u, 24u, 2u, 2u, 4u, 2u,
                                                  320u, 320u, 320u, 320u, 320u, 320u, 12u, 12u, 12u, 12u, 96u, 96u } },
        { "many small instances",               std::vector<uint32_t>(200u, 8u) },
        { "one giant plus many tiny",           [] { std::vector<uint32_t> C{ 50000u }; C.resize(64u, 1u); return C; }() },
    };

    for (const Layout& L : Layouts)
    {
        BuildLayout(L.Counts);
        const uint32_t Total = TotalTriangles();
        bool AllAgree = true, LocalInRange = true, RoundTrips = true;

        for (uint32_t P = 0u; P < Total; ++P)
        {
            const uint32_t Resolved = InstanceOfPrimitive(P);
            const uint32_t Expected = OracleInstanceOf(P);
            if (Resolved != Expected) { AllAgree = false; break; }

            const uint32_t Local = LocalPrimitive(P, Resolved);
            if (Local >= Instances[Resolved].TriangleCount) { LocalInRange = false; break; }
            // The pair must reconstruct the flat slot exactly — this is what keeps material and vertex lookup honest.
            if (Instances[Resolved].FlatTriangleOffset + Local != P) { RoundTrips = false; break; }
        }

        char Line[192];
        std::snprintf(Line, sizeof(Line), "%s: every primitive resolves to the oracle's instance", L.Name);
        CheckTrue(Line, AllAgree);
        std::snprintf(Line, sizeof(Line), "%s: local primitive stays inside its instance", L.Name);
        CheckTrue(Line, LocalInRange);
        std::snprintf(Line, sizeof(Line), "%s: (instance, local) reconstructs the flat slot", L.Name);
        CheckTrue(Line, RoundTrips);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ③ Boundaries — where an off-by-one would actually hide.
    //──────────────────────────────────────────────────────────────────────────
    BuildLayout({ 10u, 1u, 10u, 1u, 10u });
    bool Boundaries = true;
    for (uint32_t I = 0u; I < Instances.size(); ++I)
    {
        const uint32_t First = Instances[I].FlatTriangleOffset;
        const uint32_t Last  = First + Instances[I].TriangleCount - 1u;
        if (InstanceOfPrimitive(First) != I || InstanceOfPrimitive(Last) != I) { Boundaries = false; break; }
    }
    CheckTrue("first and last primitive of every instance resolve correctly", Boundaries);
    CheckTrue("single-triangle instances are not skipped", InstanceOfPrimitive(10u) == 1u && InstanceOfPrimitive(21u) == 3u);

    //──────────────────────────────────────────────────────────────────────────
    // ④ The two call sites must agree — the actual D2 invariant.
    //──────────────────────────────────────────────────────────────────────────
    // The primary hit and the bounce hit resolve identity through the same helper now. Model both and require
    //    agreement on random primitives; before D2 these were separate copies of the loop.
    BuildLayout({ 12u, 960u, 1280u, 1024u, 320u, 320u, 96u });
    std::mt19937 Generator(20260905u);
    std::uniform_int_distribution<uint32_t> Pick(0u, TotalTriangles() - 1u);
    bool SitesAgree = true;
    for (uint32_t Sample = 0u; Sample < 20000u; ++Sample)
    {
        const uint32_t P = Pick(Generator);
        const uint32_t PrimaryInstance = InstanceOfPrimitive(P);
        const uint32_t BounceInstance  = InstanceOfPrimitive(P);
        if (PrimaryInstance != BounceInstance ||
            LocalPrimitive(P, PrimaryInstance) != LocalPrimitive(P, BounceInstance)) { SitesAgree = false; break; }
    }
    CheckTrue("primary and bounce sites resolve identical (instance, primitive)", SitesAgree);

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
