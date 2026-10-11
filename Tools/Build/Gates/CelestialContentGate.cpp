//============================================================================================================================================
//                                                     CELESTIALCONTENTGATE.CPP
//============================================================================================================================================
// Celestial content gate — the moon albedos must decode to real images and the star catalogue must not be empty.
//
//    Both of these failed silently in the shipped build, and both looked like renderer bugs. The moon albedos and
//    BrightStars.bin were opened with paths relative to the WORKING DIRECTORY, so they resolved only when the process
//    happened to be launched from the repository root. Running the mirrored Build\Project-Zero.exe failed every open,
//    and each subsystem degraded quietly rather than complaining: the moons fell back to 1x1 white placeholders (a
//    pale, textureless disc) and the star catalogue loaded zero stars (an empty night sky). AssetResolution fixed the
//    resolution; this gate is what stops it regressing, because the failure mode is a slightly wrong PICTURE rather
//    than a crash or a log line anyone reads.
//
//    A placeholder is specifically dangerous here: 1x1 white is a perfectly valid texture, so nothing downstream
//    errors. The moon simply renders as a flat disc and the bug is only visible to someone who knows what Luna is
//    supposed to look like.
//
//    usage: bash Tools/Build/CheckCelestialContent.sh

#include "Engine/ContentInterchange/TextureIndex.h"
#include "Engine/DisplayPresentation/MoonConstantRecord.h"
#include "Engine/GeometricRaster/StarCatalogueIndex.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Frontier;

namespace {

int Failures = 0;

void Check(bool Condition, const char* Message)
{
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Message);
    if (!Condition) ++Failures;
}

} // namespace

int main()
{
    std::printf("================================================================================\n");
    std::printf("   CELESTIAL CONTENT GATE — real moon albedos, a populated star catalogue\n");
    std::printf("================================================================================\n");

    // ── ① every moon in the atlas decodes ───────────────────────────────────────────────────────────────────────
    {
        TextureIndex Textures;
        for (uint32_t M = 0u; M < kMoonAtlasCount; ++M)
            Textures.RegisterPath(std::string(kMoonTextureDirectory) + kMoonAtlas[M].File, /*Linear=*/false);

        std::vector<std::string> Report;
        const uint32_t Decoded = Textures.Decode(2048u, &Report);
        const TextureIndexMetrics& Metrics = Textures.QueryMetrics();

        std::printf("atlas: %u registered, %u resident, %u placeholder\n",
                    kMoonAtlasCount, Metrics.Count, Metrics.Placeholders);

        Check(Metrics.Count == kMoonAtlasCount, "every moon in kMoonAtlas is resident");
        Check(Decoded == 0u,                    "no decode failures");
        Check(Metrics.Placeholders == 0u,       "no 1x1 placeholders — this is the bug that made the moons textureless");

        // A placeholder is 1x1, so a real albedo must be bigger than that in both axes. Checking the dimensions
        //    rather than only the placeholder flag catches a decode that "succeeded" into something degenerate.
        for (uint32_t I = 0u; I < Metrics.Count; ++I)
        {
            const TextureDescriptor& T = Textures.QueryTextures()[I];
            char Line[192];
            std::snprintf(Line, sizeof(Line), "'%s' decoded to %ux%u (not a 1x1 placeholder)",
                          T.Name.c_str(), T.Width, T.Height);
            Check(T.Width > 1u && T.Height > 1u && !T.Placeholder, Line);
        }
    }

    // ── ② the star catalogue is populated ───────────────────────────────────────────────────────────────────────
    {
        StarCatalogueIndex Catalogue;
        const bool Loaded = Catalogue.Load("EngineContent/StarCatalogue/BrightStars.bin");
        std::printf("catalogue: load=%s, %zu stars in %zu cells\n",
                    Loaded ? "yes" : "no", Catalogue.QueryStars().size(), Catalogue.QueryCells().size());

        Check(Loaded,                  "BrightStars.bin opens (it used to depend on the working directory)");
        Check(!Catalogue.Empty(),      "the catalogue is not empty — an empty one is a starless night sky");
        // The shipped catalogue is the ~9.7k bright-star set. A few hundred would mean a truncated or wrong file.
        Check(Catalogue.QueryStars().size() > 1000u, "the catalogue holds the full bright-star set, not a stub");
        Check(!Catalogue.QueryCells().empty(),       "the spatial cells are built, so StarAlong can look stars up");
    }

    std::printf(Failures ? "\nRED — %d check(s) failed\n" : "\nGREEN — the moons are textured and the sky has stars\n", Failures);
    return Failures ? 1 : 0;
}
