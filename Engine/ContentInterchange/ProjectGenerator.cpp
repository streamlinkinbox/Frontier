/*==========================================================================================================================================
                                                        PROJECTGENERATOR.CPP
==========================================================================================================================================*/
// 🧩 Auto-generate C++ application classes, main loop entry point, CMake build configuration, and Default.levelspace scene.

#include "Engine/ContentInterchange/ProjectGenerator.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace Frontier
{

namespace
{

bool WriteTextFileIfAbsent(const std::filesystem::path& FilePath, const std::string& Contents, std::string& Diagnostic) noexcept
{
    std::error_code FsErr;
    if (FilePath.has_parent_path())
    {
        std::filesystem::create_directories(FilePath.parent_path(), FsErr);
        if (FsErr)
        {
            Diagnostic = "Unable to create directory for generated project file.";
            return false;
        }
    }

    if (std::filesystem::exists(FilePath, FsErr))
    {
        return true;
    }

    std::ofstream Output(FilePath, std::ios::binary | std::ios::trunc);
    if (!Output)
    {
        Diagnostic = "Unable to write generated project file.";
        return false;
    }
    Output << Contents;
    if (!Output.good())
    {
        Diagnostic = "Write error while emitting generated project file.";
        return false;
    }
    return true;
}

std::string BuildHeaderContents(const std::string& Identifier)
{
    std::ostringstream Stream;
    Stream << "/*==========================================================================================================================================\n"
           << "                                                       " << Identifier << "Application.h\n"
           << "==========================================================================================================================================*/\n"
           << "// Auto-generated Frontier C++ project runtime class owning SceneWorld and the simulation main loop.\n\n"
           << "#pragma once\n\n"
           << "#include \"Engine/Scene/SceneWorld.h\"\n\n"
           << "#include <cstdint>\n"
           << "#include <filesystem>\n"
           << "#include <string>\n\n"
           << "namespace " << Identifier << "\n"
           << "{\n\n"
           << "class " << Identifier << "Application\n"
           << "{\n"
           << "  public:\n"
           << "    " << Identifier << "Application() = default;\n"
           << "    ~" << Identifier << "Application() = default;\n\n"
           << "    bool Initialize(const std::filesystem::path& ProjectSpacePath, std::string* OutDiagnostic = nullptr);\n"
           << "    void StepFrame(float DeltaSeconds);\n"
           << "    int  RunMainLoop(uint64_t MaxFrames = 0);\n"
           << "    void Shutdown();\n\n"
           << "    [[nodiscard]] Frontier::SceneWorld&       AccessWorld() noexcept { return World; }\n"
           << "    [[nodiscard]] const Frontier::SceneWorld& QueryWorld() const noexcept { return World; }\n"
           << "    [[nodiscard]] uint64_t                    QuerySimulatedFrames() const noexcept { return SimulatedFrames; }\n"
           << "    [[nodiscard]] bool                        QueryRunning() const noexcept { return Running; }\n\n"
           << "  private:\n"
           << "    void RegisterProjectTypes();\n\n"
           << "    Frontier::SceneWorld  World;\n"
           << "    std::filesystem::path ActiveProjectPath;\n"
           << "    uint64_t              SimulatedFrames = 0;\n"
           << "    bool                  Running         = false;\n"
           << "};\n\n"
           << "} // namespace " << Identifier << "\n";
    return Stream.str();
}

std::string BuildSourceContents(const std::string& Identifier)
{
    std::ostringstream Stream;
    Stream << "/*==========================================================================================================================================\n"
           << "                                                      " << Identifier << "Application.cpp\n"
           << "==========================================================================================================================================*/\n"
           << "// Auto-generated Frontier C++ project runtime implementation.\n\n"
           << "#include \"" << Identifier << "Application.h\"\n\n"
           << "#include <chrono>\n\n"
           << "namespace " << Identifier << "\n"
           << "{\n\n"
           << "void " << Identifier << "Application::RegisterProjectTypes()\n"
           << "{\n"
           << "    // Register project-specific SceneRecordBase subclasses and RecordInspector bindings here.\n"
           << "}\n\n"
           << "bool " << Identifier << "Application::Initialize(const std::filesystem::path& ProjectSpacePath, std::string* OutDiagnostic)\n"
           << "{\n"
           << "    ActiveProjectPath = ProjectSpacePath;\n"
           << "    RegisterProjectTypes();\n"
           << "    if (!World.LoadStartupLevelFromProject(ProjectSpacePath, OutDiagnostic))\n"
           << "    {\n"
           << "        return false;\n"
           << "    }\n"
           << "    SimulatedFrames = 0;\n"
           << "    Running         = true;\n"
           << "    return true;\n"
           << "}\n\n"
           << "void " << Identifier << "Application::StepFrame(float DeltaSeconds)\n"
           << "{\n"
           << "    if (!Running)\n"
           << "    {\n"
           << "        return;\n"
           << "    }\n"
           << "    World.AdvanceSimulation(DeltaSeconds);\n"
           << "    ++SimulatedFrames;\n"
           << "}\n\n"
           << "int " << Identifier << "Application::RunMainLoop(uint64_t MaxFrames)\n"
           << "{\n"
           << "    using Clock = std::chrono::steady_clock;\n"
           << "    Clock::time_point Previous = Clock::now();\n"
           << "    while (Running)\n"
           << "    {\n"
           << "        const Clock::time_point Now = Clock::now();\n"
           << "        float DeltaSeconds = std::chrono::duration<float>(Now - Previous).count();\n"
           << "        Previous = Now;\n"
           << "        if (DeltaSeconds <= 0.0f || DeltaSeconds > 0.25f)\n"
           << "        {\n"
           << "            DeltaSeconds = 1.0f / 60.0f;\n"
           << "        }\n"
           << "        StepFrame(DeltaSeconds);\n"
           << "        if (MaxFrames > 0 && SimulatedFrames >= MaxFrames)\n"
           << "        {\n"
           << "            Running = false;\n"
           << "        }\n"
           << "    }\n"
           << "    return 0;\n"
           << "}\n\n"
           << "void " << Identifier << "Application::Shutdown()\n"
           << "{\n"
           << "    Running = false;\n"
           << "    World.Clear();\n"
           << "}\n\n"
           << "} // namespace " << Identifier << "\n";
    return Stream.str();
}

std::string BuildMainEntryContents(const std::string& Identifier, const std::string& ProjectFileName)
{
    std::ostringstream Stream;
    Stream << "/*==========================================================================================================================================\n"
           << "                                                              MAIN.CPP\n"
           << "==========================================================================================================================================*/\n"
           << "// Auto-generated executable entry point for " << Identifier << ".\n\n"
           << "#include \"" << Identifier << "Application.h\"\n\n"
           << "#include <filesystem>\n"
           << "#include <iostream>\n\n"
           << "int main(int ArgumentCount, char** ArgumentValues)\n"
           << "{\n"
           << "    const std::filesystem::path ProjectPath = (ArgumentCount > 1)\n"
           << "        ? std::filesystem::path(ArgumentValues[1])\n"
           << "        : std::filesystem::path(\"" << ProjectFileName << "\");\n\n"
           << "    " << Identifier << "::" << Identifier << "Application Application;\n"
           << "    std::string Diagnostic;\n"
           << "    if (!Application.Initialize(ProjectPath, &Diagnostic))\n"
           << "    {\n"
           << "        std::cerr << \"[\" << \"" << Identifier << "\" << \"] Startup failed: \" << Diagnostic << \"\\n\";\n"
           << "        return 1;\n"
           << "    }\n"
           << "    const int ExitCode = Application.RunMainLoop(1);\n"
           << "    Application.Shutdown();\n"
           << "    return ExitCode;\n"
           << "}\n";
    return Stream.str();
}

std::string BuildCMakeListsContents(const std::string& Identifier, const std::string& ProjectFileName)
{
    std::ostringstream Stream;
    Stream << "# Auto-generated CMake build configuration for " << Identifier << "\n"
           << "cmake_minimum_required(VERSION 3.24)\n\n"
           << "include(\"${CMAKE_SOURCE_DIR}/Automation/StageProjectContent.cmake\")\n\n"
           << "add_library(" << Identifier << "Runtime STATIC\n"
           << "    Source/" << Identifier << "Application.cpp\n"
           << ")\n"
           << "target_include_directories(" << Identifier << "Runtime PUBLIC\n"
           << "    ${CMAKE_CURRENT_SOURCE_DIR}/Source\n"
           << ")\n"
           << "target_link_libraries(" << Identifier << "Runtime PUBLIC FrontierEngine)\n\n"
           << "frontier_stage_project_content(\n"
           << "    " << Identifier << "Runtime\n"
           << "    \"${CMAKE_CURRENT_SOURCE_DIR}/" << ProjectFileName << "\"\n"
           << "    \"${CMAKE_SOURCE_DIR}/EngineContent\"\n"
           << "    \"${CMAKE_CURRENT_SOURCE_DIR}/Content\"\n"
           << ")\n";
    return Stream.str();
}

std::string BuildDefaultLevelSpaceContents(const std::string& DisplayName)
{
    std::string EscapedName;
    EscapedName.reserve(DisplayName.size());
    for (const char Ch : DisplayName)
    {
        if (Ch == '"' || Ch == '\\')
        {
            EscapedName.push_back('_');
        }
        else if (static_cast<unsigned char>(Ch) >= 32)
        {
            EscapedName.push_back(Ch);
        }
    }
    if (EscapedName.empty())
    {
        EscapedName = "Default";
    }

    std::ostringstream Stream;
    Stream << "[level]\n"
           << "format_version = 1\n"
           << "name = \"" << EscapedName << " — Default Scene\"\n\n"
           << "[[records]]\n"
           << "identity = \"rec_pipeline\"\n"
           << "type = \"RenderPipeline\"\n"
           << "name = \"Render Pipeline\"\n"
           << "[records.configuration]\n"
           << "enabled = true\n"
           << "hardware_ray_query = true\n"
           << "quality_tier = 2\n"
           << "exposure_ev = 0.10\n"
           << "bloom_intensity = 0.25\n"
           << "global_illumination = true\n"
           << "temporal_aa = true\n\n"
           << "[[records]]\n"
           << "identity = \"rec_solar\"\n"
           << "type = \"Solar\"\n"
           << "name = \"Primary Directional Sun\"\n"
           << "[records.configuration]\n"
           << "enabled = true\n"
           << "elevation_deg = 42.0\n"
           << "azimuth_deg = 135.0\n"
           << "illuminance_klx = 96.0\n"
           << "kelvin = 5778.0\n\n"
           << "[[records]]\n"
           << "identity = \"rec_atmosphere\"\n"
           << "type = \"Atmosphere\"\n"
           << "name = \"Sky Atmosphere\"\n"
           << "[records.configuration]\n"
           << "enabled = true\n"
           << "rayleigh = 1.0\n"
           << "mie_turbidity = 2.2\n"
           << "ozone = 1.0\n\n"
           << "[[records]]\n"
           << "identity = \"rec_clouds\"\n"
           << "type = \"Clouds\"\n"
           << "name = \"Cumulus Cloud Deck\"\n"
           << "parent = \"rec_atmosphere\"\n"
           << "[records.configuration]\n"
           << "enabled = true\n"
           << "local_volume = false\n"
           << "coverage = 0.38\n"
           << "base_altitude_m = 1800.0\n"
           << "thickness_m = 1200.0\n\n"
           << "[[records]]\n"
           << "identity = \"rec_origin\"\n"
           << "type = \"SpatialNode\"\n"
           << "name = \"World Origin\"\n"
           << "[records.configuration]\n"
           << "position_m = [0.0, 0.0, 0.0]\n"
           << "orientation_deg = [0.0, 0.0, 0.0]\n"
           << "scale = [1.0, 1.0, 1.0]\n\n"
           << "[[records]]\n"
           << "identity = \"rec_ground\"\n"
           << "type = \"RigidBody\"\n"
           << "name = \"Reference Ground Slab\"\n"
           << "parent = \"rec_origin\"\n"
           << "[records.configuration]\n"
           << "position_m = [0.0, 0.0, 0.0]\n"
           << "orientation_deg = [0.0, 0.0, 0.0]\n"
           << "scale = [50.0, 0.5, 50.0]\n"
           << "physics_enabled = true\n"
           << "mass_kg = 2500.0\n"
           << "density_kg_m3 = 2400.0\n"
           << "volume_m3 = 1.0417\n"
           << "dynamic_body = false\n"
           << "gravity_coupled = false\n"
           << "optics_enabled = true\n"
           << "albedo_rgb = [0.72, 0.74, 0.77]\n"
           << "roughness = 0.55\n"
           << "metallic = 0.0\n";
    return Stream.str();
}

} // namespace

std::string ProjectGenerator::DeriveCppIdentifier(const std::string& DisplayName)
{
    std::string Identifier;
    bool        CapitalizeNext = true;
    for (const unsigned char Raw : DisplayName)
    {
        if (Raw < 128 && std::isalnum(Raw))
        {
            const char Ch = static_cast<char>(CapitalizeNext ? std::toupper(Raw) : Raw);
            Identifier.push_back(Ch);
            CapitalizeNext = false;
        }
        else
        {
            CapitalizeNext = true;
        }
    }

    if (Identifier.empty())
    {
        Identifier = "FrontierProject";
    }
    else if (std::isdigit(static_cast<unsigned char>(Identifier.front())))
    {
        Identifier.insert(0, "Project");
    }
    return Identifier;
}

bool ProjectGenerator::GenerateProjectScaffolding(const std::filesystem::path& ProjectSpacePath,
                                                  const std::string&           DisplayName,
                                                  std::string&                 Diagnostic,
                                                  GeneratedProjectLayout*      OutLayout) noexcept
{
    try
    {
        const std::filesystem::path Root       = ProjectSpacePath.has_parent_path() ? ProjectSpacePath.parent_path() : std::filesystem::path(".");
        const std::string           FallbackNm = DisplayName.empty() ? ProjectSpacePath.stem().string() : DisplayName;
        const std::string           Identifier = DeriveCppIdentifier(FallbackNm);
        const std::string           FileName   = ProjectSpacePath.filename().string();

        GeneratedProjectLayout Layout;
        Layout.CppIdentifier         = Identifier;
        Layout.ProjectRoot           = Root;
        Layout.HeaderPath            = Root / "Source" / (Identifier + "Application.h");
        Layout.SourcePath            = Root / "Source" / (Identifier + "Application.cpp");
        Layout.MainEntryPath         = Root / "Source" / "Main.cpp";
        Layout.CMakeListsPath        = Root / "CMakeLists.txt";
        Layout.DefaultLevelSpacePath = Root / "Content" / "Levels" / "Default.levelspace";

        if (!WriteTextFileIfAbsent(Layout.HeaderPath, BuildHeaderContents(Identifier), Diagnostic))
        {
            return false;
        }
        if (!WriteTextFileIfAbsent(Layout.SourcePath, BuildSourceContents(Identifier), Diagnostic))
        {
            return false;
        }
        if (!WriteTextFileIfAbsent(Layout.MainEntryPath, BuildMainEntryContents(Identifier, FileName), Diagnostic))
        {
            return false;
        }
        if (!WriteTextFileIfAbsent(Layout.CMakeListsPath, BuildCMakeListsContents(Identifier, FileName), Diagnostic))
        {
            return false;
        }
        if (!WriteTextFileIfAbsent(Layout.DefaultLevelSpacePath, BuildDefaultLevelSpaceContents(FallbackNm), Diagnostic))
        {
            return false;
        }

        if (OutLayout)
        {
            *OutLayout = std::move(Layout);
        }
        return true;
    }
    catch (...)
    {
        Diagnostic = "Unexpected failure while generating C++ project scaffolding.";
        return false;
    }
}

} // namespace Frontier
