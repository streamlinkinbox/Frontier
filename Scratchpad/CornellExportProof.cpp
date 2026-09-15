//============================================================================================================================================
//                                                   CORNELLEXPORTPROOF.CPP
//============================================================================================================================================
// 🧩 Regenerates Projects/Project-Zero/Content/Scenes/CornellBox.gltf through the exact path the renderer uses —
//    RayTracingSolver → ReSTIRIntegrator::BuildTriangleIndex → SceneCodec::Encode — and then reads it back and
//    checks what came out. GameExecution does this on first run when the file is missing; here it runs without
//    a GPU, which is what makes the committed asset reproducible rather than a thing someone once generated.
//
//    ⚠️ The exported file is COMMITTED, so it does not regenerate itself when the solver changes: the game only
//    writes it when it is absent. That is the trap this tool exists to close. Move the aperture in the solver
//    and forget to run this, and every proof passes against geometry the renderer never loads.
//
//    Build: bash Scratchpad/ExportCornellBox.sh

#include "Projects/Project-Zero/Source/RayTracingSolver.h"
#include "Engine/ContentInterchange/SceneCodec.h"
#include "Engine/ContentInterchange/TextureIndex.h"
#include "Engine/DisplayPresentation/ReSTIRIntegrator.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-62s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

} // namespace

int main()
{
    using namespace Frontier;

    const std::string Path = "Projects/Project-Zero/Content/Scenes/CornellBox.gltf";

    ProjectZero::RayTracingSolver Solver;
    const auto Triangles = ReSTIRIntegrator::BuildTriangleIndex(Solver);
    const auto Materials = ReSTIRIntegrator::BuildMaterialDescriptors(Solver);
    SceneEncodeConfiguration EncodeConfig;
    EncodeConfig.Name  = "CornellBox";
    EncodeConfig.Spans = &Solver.QuerySpans();

    std::printf("[Cornell] exporting %zu triangles, %zu materials, %zu spans\n", Triangles.size(),
                Materials.size(), Solver.QuerySpans().size());
    CheckTrue("the solver names all eleven objects",
              Solver.QuerySpans().size() == 11u && Solver.QuerySpans()[0].Name == "Floor"
                  && Solver.QuerySpans()[10].Name == "Ceiling Luminaire");

    std::string Error;
    if (!SceneCodec::Encode(Path, Triangles, Materials, &Error, EncodeConfig))
    {
        std::printf("  ENCODE FAILED: %s\n", Error.c_str());
        return 1;
    }

    SceneStructure                Reloaded;
    TextureIndex                  Textures;
    SceneDecodeConfiguration      Configuration;
    if (!SceneCodec::Decode(Path, Reloaded, &Textures, Configuration, &Error))
    {
        std::printf("  DECODE FAILED: %s\n", Error.c_str());
        return 1;
    }
    // Flatten, because the flat world-space triangles are what the renderer actually traces — checking the
    //    authored vertices would pass on a scene whose instance transform put the room somewhere else.
    Reloaded.Finalise();

    std::printf("\n[Cornell] what came back off disk\n");
    const auto& Back = Reloaded.QueryFlatTriangles();
    std::printf("  %zu triangles, %u materials\n", Back.size(), Reloaded.QueryMaterials().QueryCount());
    CheckTrue("every triangle survived the round trip", Back.size() == Triangles.size());
    // One more than authored: the decoder inserts its own default at index 0.
    CheckTrue("every material survived it too",
              Reloaded.QueryMaterials().QueryCount() >= static_cast<uint32_t>(Materials.size()));

    // The spans round-trip into one placement (and one instance) per object, dynamics flagged — this is the
    //    shape the outliner walks, so the proof pins it here rather than in the editor.
    const auto& Placements = Reloaded.QueryPlacements();
    uint32_t DynamicPlacements = 0u;
    for (const auto& P : Placements) if (P.Dynamic) ++DynamicPlacements;
    CheckTrue("one placement per scene object", Placements.size() == 11u);
    CheckTrue("placements carry the builder names",
              Placements.size() == 11u && Placements[0].Name == "Floor"
                  && Placements[10].Name == "Ceiling Luminaire");
    CheckTrue("the five loose objects are flagged dynamic", DynamicPlacements == 5u);
    CheckTrue("one instance per object", Reloaded.QueryInstances().size() == 11u);

    // 🔴 The aperture is the reason this file is regenerated at all, so it is what gets checked. The room is
    //    Z-up in the engine; the loader hands back the engine's own convention, so the ceiling is at Z = 3.
    constexpr float HoleX = 0.0f, HoleY = 2.10f, HoleR = 0.75f, TopZ = 3.0f;

    const auto CoveredAt = [&](float Px, float Py)
    {
        int Count = 0;
        for (const auto& T : Back)
        {
            if (std::fabs(T.VertexAlphaZ - TopZ) > 1e-3f || std::fabs(T.VertexBetaZ - TopZ) > 1e-3f
             || std::fabs(T.VertexGammaZ - TopZ) > 1e-3f) continue;
            const auto Side = [&](float Ax, float Ay, float Bx, float By)
                              { return (Px - Bx) * (Ay - By) - (Ax - Bx) * (Py - By); };
            const float D1 = Side(T.VertexAlphaX, T.VertexAlphaY, T.VertexBetaX,  T.VertexBetaY);
            const float D2 = Side(T.VertexBetaX,  T.VertexBetaY,  T.VertexGammaX, T.VertexGammaY);
            const float D3 = Side(T.VertexGammaX, T.VertexGammaY, T.VertexAlphaX, T.VertexAlphaY);
            const bool Negative = (D1 < 0.0f) || (D2 < 0.0f) || (D3 < 0.0f);
            const bool Positive = (D1 > 0.0f) || (D2 > 0.0f) || (D3 > 0.0f);
            if (!(Negative && Positive)) ++Count;
        }
        return Count;
    };

    std::printf("  ceiling coverage: centre %d, just outside the rim %d, far corner %d\n",
                CoveredAt(HoleX, HoleY), CoveredAt(HoleX, HoleY + HoleR + 0.30f), CoveredAt(-1.8f, 0.3f));
    CheckTrue("the aperture is open in the file the renderer loads", CoveredAt(HoleX, HoleY) == 0);
    CheckTrue("the ceiling outside the rim is solid",                CoveredAt(HoleX, HoleY + HoleR + 0.30f) > 0);
    CheckTrue("and the ring reaches the far corner",                 CoveredAt(-1.8f, 0.3f) > 0);

    // The three parametric shapes: the file must carry them, not just the solver.
    // ⚠️ Counted by SIZE, not by slot number. The decoder inserts its own default material at index 0, so every
    //    authored slot shifts by one on the way back in — an assertion keyed to slot 6 would pass on the solver
    //    and fail on the file for a reason that has nothing to do with the geometry.
    std::vector<uint32_t> PerSlot(Reloaded.QueryMaterials().QueryCount() + 1u, 0u);
    for (const auto& T : Back)
    {
        // The slot is a uint REINTERPRETED into a float field, not a float value — converting it numerically
        //    reads every triangle as material 0, which looks like a scene with one material rather than a bug.
        uint32_t Slot = 0u;
        std::memcpy(&Slot, &T.MaterialSlot, sizeof(Slot));
        if (Slot < PerSlot.size()) ++PerSlot[Slot];
    }
    std::printf("  triangles per material slot:");
    for (uint32_t Slot = 0u; Slot < PerSlot.size(); ++Slot)
        if (PerSlot[Slot] > 0u) std::printf(" %u:%u", Slot, PerSlot[Slot]);
    std::printf("\n");

    // The three parametric shapes are the only groups with hundreds of triangles each; the room's quads and
    //    boxes are a dozen at most. Sphere 960, cone 64, torus 1296 as the solver builds them.
    uint32_t Sphere = 0u, Cone = 0u, Torus = 0u;
    for (uint32_t Count : PerSlot)
    {
        if (Count == 960u)  Sphere = Count;
        if (Count == 64u)   Cone   = Count;
        if (Count == 1296u) Torus  = Count;
    }
    // The luminaire is read from the gathered light list rather than from a material field: that list is what
    //    the sampler actually draws from, so it is the thing whose absence would render the room black.
    const size_t Luminaires = Reloaded.QueryLuminaires().size();
    std::printf("  sphere %u, cone %u, torus %u triangles; %zu luminaire triangles, power %.1f\n",
                Sphere, Cone, Torus, Luminaires,
                static_cast<double>(Reloaded.QueryLuminairePower()));
    CheckTrue("the sphere is in the exported scene", Sphere > 0u);
    CheckTrue("the cone is in the exported scene",   Cone   > 0u);
    CheckTrue("the torus is in the exported scene",  Torus  > 0u);
    // Without this the room imports perfectly and then renders pitch black — the failure a geometry-only check
    //    misses entirely.
    CheckTrue("the luminaire is still emissive after the round trip",
              Luminaires > 0u && Reloaded.QueryLuminairePower() > 0.0f);

    std::printf("\n>>> %s (%d failure%s)\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures,
                Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
