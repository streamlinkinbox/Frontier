/*==========================================================================================================================================
                                                             PROJECT CONTENT
                                     ------------------------------------------------------------------
                         Locate project and engine content independently of the working directory; projects
                                                          declare imports, builds stage.
==========================================================================================================================================*/
// 🧩 Locate project and engine content independently of the working directory; projects declare imports and builds stage into Content/.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Frontier
{

inline std::string EncodeUtf8Path(const std::filesystem::path& Path)
{
#if __cplusplus >= 202002L
    const std::u8string Utf8 = Path.u8string();
    return std::string(reinterpret_cast<const char*>(Utf8.data()), Utf8.size());
#else
    return Path.u8string();
#endif
}

inline std::filesystem::path DecodeUtf8Path(std::string_view Path)
{
    return std::filesystem::u8path(Path.begin(), Path.end());
}

// Configured by the host at start-up: where the executable lives, and the opened project's own Content folder.
// Project Content/ (including compile-time staged EngineContent imports) always takes precedence.
void ConfigureContentRoots(std::filesystem::path ExecutableDirectory, std::filesystem::path ProjectContentDirectory);

[[nodiscard]] std::filesystem::path QueryEngineContentDirectory();
[[nodiscard]] std::filesystem::path ResolveProjectContentDirectory(const std::filesystem::path& Requested);

// Resolves one content-relative file ("FontArchives/Inter/Inter-Regular.ttf", "Icons/...",
// "EnvironmentArchives/ControlVehicle/ControlVehicleBody.facet"): project Content/ first, EngineContent/ second.
[[nodiscard]] std::filesystem::path ResolveProjectContentFile(const std::filesystem::path& Relative);

} // namespace Frontier
