/*==========================================================================================================================================
                                         PROJECT CONTENT STAGING VALIDATION
==========================================================================================================================================*/
// Verifies compile-time automatic EngineContent/ -> <Project>/Content/ staging and standalone runtime execution
// in an isolated directory tree where NO EngineContent/ folder exists anywhere on disk (including fonts, icons,
// 6-body 2K celestial maps, Hipparcos stars, ControlVehicle .facet geometry, AND pre-baked .sdfspace SDF volumes).

#include "Engine/ContentInterchange/ProjectSpaceCodec.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"
#include "Engine/Geometry/PrimitiveGeometryGenerator.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/ProjectContent.h"
#include "Engine/PlanetaryAtmosphere/PlanetaryProjection.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

using namespace Frontier;

void RequireStaging(bool Condition, const char* Expr, const char* Detail, int Line)
{
    if (!Condition)
    {
        std::cerr << "[ProjectContentStagingValidation] FAILED at line " << Line << ": " << Detail
                  << " (" << Expr << ")\n";
        std::exit(1);
    }
}

#define REQUIRE_STG(Cond, Msg) RequireStaging((Cond), #Cond, (Msg), __LINE__)

void SetContentEnv(const std::string& Value)
{
#ifdef _WIN32
    _putenv_s("FRONTIER_PROJECT_CONTENT_DIR", Value.c_str());
#else
    setenv("FRONTIER_PROJECT_CONTENT_DIR", Value.c_str(), 1);
#endif
}

} // namespace

int main(int Argc, char** Argv)
{
    const std::filesystem::path EverestRoot = (Argc > 1) ? Argv[1] : "Projects/Everest";
    const std::filesystem::path SandboxBase = (Argc > 2) ? Argv[2] : "/tmp/frontier-standalone-content-sandbox";

    // 1. Verify Everest.projectspace [engine.content_imports] schema & compile-time staged manifest
    std::cout << "[1/4] Verifying Everest.projectspace [engine.content_imports] & Compile-Time Staged Manifest...\n";
    ProjectSpaceDocument Doc{};
    std::string          Diag;
    REQUIRE_STG(ProjectSpaceCodec::Decode(EverestRoot / "Everest.projectspace", Doc, Diag),
                ("ProjectSpaceCodec::Decode failed: " + Diag).c_str());
    const std::vector<std::string> Imports = ProjectSpaceCodec::QueryEngineContentImports(Doc);
    REQUIRE_STG(Imports.size() >= 6u,
                "Everest.projectspace must declare fonts, icons, celestial maps, vehicle geometry/SDFs, and material imports");

    const std::filesystem::path StagedManifest = EverestRoot / "Content/.staged_manifest.json";
    REQUIRE_STG(std::filesystem::is_regular_file(StagedManifest),
                "Compile-time StageProjectContent must emit Projects/Everest/Content/.staged_manifest.json");
    REQUIRE_STG(std::filesystem::is_regular_file(EverestRoot / "Content/FontArchives/Inter/Inter-Regular.otf") &&
                    std::filesystem::is_regular_file(EverestRoot / "Content/FontArchives/DMSans/DMSans-Regular.ttf") &&
                    std::filesystem::is_regular_file(EverestRoot / "Content/EnvironmentArchives/ControlVehicle/ControlVehicleBody.facet") &&
                    std::filesystem::is_regular_file(EverestRoot / "Content/EnvironmentArchives/ControlVehicle/ControlVehicleBody.sdfspace") &&
                    std::filesystem::is_regular_file(EverestRoot / "Content/EnvironmentArchives/ControlVehicle/ControlVehicleRim.sdfspace") &&
                    std::filesystem::is_regular_file(EverestRoot / "Content/EnvironmentArchives/ControlVehicle/ControlVehicleTyre.sdfspace") &&
                    std::filesystem::is_regular_file(EverestRoot / "Content/EnvironmentArchives/CelestialMaps/MoonAlbedoSource2048.jpg") &&
                    std::filesystem::is_directory(EverestRoot / "Content/Icons"),
                "Compile-time staging must automatically populate <Project>/Content/ with fonts, icons, celestial maps, .facet geometry, AND pre-baked .sdfspace SDFs");

    // 2. Construct an Isolated Sandbox Outside Repo Root (ZERO EngineContent/ in Sandbox or Any Parent Directory!)
    std::cout << "[2/4] Copying Staged Project Into Isolated EngineContent-Free Sandbox (" << SandboxBase << ")...\n";
    std::error_code Ec;
    std::filesystem::remove_all(SandboxBase, Ec);
    const std::filesystem::path StandaloneProj = SandboxBase / "Nested" / "EverestStandalone";
    std::filesystem::create_directories(StandaloneProj, Ec);

    std::filesystem::copy_file(EverestRoot / "Everest.projectspace", StandaloneProj / "Everest.projectspace", Ec);
    REQUIRE_STG(!Ec, "Failed copying Everest.projectspace into standalone sandbox");
    std::filesystem::copy(EverestRoot / "Content",
                          StandaloneProj / "Content",
                          std::filesystem::copy_options::recursive,
                          Ec);
    REQUIRE_STG(!Ec, "Failed copying staged Content/ directory into standalone sandbox");

    REQUIRE_STG(!std::filesystem::exists(SandboxBase / "EngineContent") &&
                    !std::filesystem::exists(StandaloneProj / "EngineContent"),
                "Standalone sandbox must contain ZERO EngineContent directories");

    // 3. Verify Font, Icon, & Level Resolution Purely From <Project>/Content/
    std::cout << "[3/4] Verifying Standalone Font, Icon, & ContentDirectory Resolution Without EngineContent...\n";
    ConfigureContentRoots(StandaloneProj, StandaloneProj / "Content");
    SetContentEnv((StandaloneProj / "Content").string());

    REQUIRE_STG(QueryEngineContentDirectory().empty(),
                "QueryEngineContentDirectory must return empty when no EngineContent/ exists");

    const std::filesystem::path ResolvedInter  = ResolveProjectContentFile("FontArchives/Inter/Inter-Regular.otf");
    const std::filesystem::path ResolvedDmSans = ResolveProjectContentFile("FontArchives/DMSans/DMSans-Regular.ttf");
    REQUIRE_STG(std::filesystem::is_regular_file(ResolvedInter) && std::filesystem::is_regular_file(ResolvedDmSans),
                "ResolveProjectContentFile must resolve real staged fonts from <Project>/Content/FontArchives without EngineContent");

    const std::filesystem::path ResolvedFontsDir = ResolveContentDirectory("EngineContent/FontArchives");
    const std::filesystem::path ResolvedIconsDir = ResolveContentDirectory("EngineContent/Icons");
    REQUIRE_STG(std::filesystem::is_directory(ResolvedFontsDir) &&
                    ResolvedFontsDir.generic_string().find("EverestStandalone/Content/FontArchives") != std::string::npos &&
                    std::filesystem::is_directory(ResolvedIconsDir) &&
                    ResolvedIconsDir.generic_string().find("EverestStandalone/Content/Icons") != std::string::npos,
                "ResolveContentDirectory must seamlessly redirect EngineContent/{FontArchives,Icons} to staged <Project>/Content/");

    // 4. Verify Standalone Celestial Maps, Hipparcos Stars, ControlVehicle .facet, & Pre-Baked .sdfspace SDF Loading!
    std::cout << "[4/4] Verifying Standalone 6-Moon Atlas, Hipparcos Stars, .facet Geometry, & .sdfspace SDF Loading...\n";
    PlanetaryDomeProjection StandaloneDome{};
    REQUIRE_STG(StandaloneDome.LoadLunarAtlas(StandaloneProj.string(), 256u, 128u),
                "LoadLunarAtlas must load staged 6-body moon textures from <Project>/Content/ without EngineContent");
    REQUIRE_STG(StandaloneDome.LoadHipparcosCatalogueCsv(StandaloneProj.string(), 2461344.5, -26.18, 28.32) &&
                    StandaloneDome.SourceStarCount() > 1000u,
                "LoadHipparcosCatalogueCsv must load staged Hipparcos stars from <Project>/Content/ without EngineContent");

    FacetField              StandaloneVehicle{};
    ControlVehiclePlacement StandalonePlace{};
    StandalonePlace.ArchiveDirectory = "EnvironmentArchives/ControlVehicle";
    REQUIRE_STG(AssembleControlVehicle(StandaloneVehicle, StandalonePlace, &Diag) &&
                    StandaloneVehicle.Classification.size() > 14000u,
                ("Standalone AssembleControlVehicle failed without EngineContent: " + Diag).c_str());

    VolumeDistanceField StandaloneVehicleSdf{};
    REQUIRE_STG(ComposeControlVehicleDistanceVolume(StandaloneVehicleSdf, StandalonePlace, {48u, 28u, 64u}, true, &Diag) &&
                    StandaloneVehicleSdf.Distances.size() == 48u * 28u * 64u,
                ("Standalone ComposeControlVehicleDistanceVolume (.sdfspace) failed without EngineContent: " + Diag).c_str());

    std::filesystem::remove_all(SandboxBase, Ec);
    std::cout << "[ProjectContentStagingValidation] ALL COMPILE-TIME STAGING & ENGINECONTENT-FREE RUNTIME CHECKS PASSED.\n";
    return 0;
}
