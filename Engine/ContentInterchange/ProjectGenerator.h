/*==========================================================================================================================================
                                                         PROJECTGENERATOR.H
==========================================================================================================================================*/
// 🧩 Auto-generate C++ application classes, main loop entry point, CMake build configuration, and Default.levelspace scene.

#pragma once

#include <filesystem>
#include <string>

namespace Frontier
{

struct GeneratedProjectLayout
{
    std::string           CppIdentifier;
    std::filesystem::path ProjectRoot;
    std::filesystem::path HeaderPath;
    std::filesystem::path SourcePath;
    std::filesystem::path MainEntryPath;
    std::filesystem::path CMakeListsPath;
    std::filesystem::path DefaultLevelSpacePath;
};

class ProjectGenerator
{
  public:
    [[nodiscard]] static std::string DeriveCppIdentifier(const std::string& DisplayName);

    static bool GenerateProjectScaffolding(const std::filesystem::path& ProjectSpacePath,
                                           const std::string&           DisplayName,
                                           std::string&                 Diagnostic,
                                           GeneratedProjectLayout*      OutLayout = nullptr) noexcept;
};

} // namespace Frontier
