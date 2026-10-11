//============================================================================================================================================
//                                                       ASSETRESOLUTION.H
//============================================================================================================================================
// 🧩 One answer to "where does this repository-relative asset actually live?", shared by every subsystem that opens a
//    file by a path like "EngineContent/StarCatalogue/BrightStars.bin".
//
//    Why this exists. SwapchainExchange already resolved SPIR-V against the executable's parent chain, but every other
//    content path — the moon albedos, the star catalogue, the font archives, the level files — was passed to fopen()
//    verbatim and therefore only resolved when the process happened to be launched from the repository root. Launching
//    Build\Project-Zero.exe (or the mirrored Output\...\Binary copy) left the working directory somewhere else, so those
//    opens failed and each subsystem degraded quietly: 1x1 placeholder moons, an empty star catalogue, a starless sky.
//    The renderer reported this as six "can't fopen -> 1x1 placeholder" lines and one "Catalogue empty or missing"
//    warning, which reads like missing content but is only ever a missing path root.
//
//    The search order is deliberate: the working directory first (so a developer running from the repository root keeps
//    exactly the old behaviour and can shadow content), then the executable's own folder walking up its parents (so a
//    double-clicked .exe finds the repository it was built into).

#pragma once

#include <filesystem>
#include <string>

namespace Frontier {

// The directory holding the running executable. Empty if the platform refuses to say.
[[nodiscard]] std::filesystem::path QueryExecutableDirectory() noexcept;

// The repository/content root: the nearest ancestor of the working directory or the executable that contains an
//    "EngineContent" folder. Empty when no such ancestor exists (a stripped install); callers then keep relative paths.
[[nodiscard]] std::filesystem::path QueryContentRoot() noexcept;

// Resolves a repository-relative path to something that exists, or returns Relative unchanged when nothing matches
//    (so the caller's own "cannot open" diagnostic still names the path the caller asked for).
[[nodiscard]] std::filesystem::path ResolveAsset(const std::string& Relative) noexcept;

// Same, as a string — convenient for the fopen/stbi_load callers that take const char*.
[[nodiscard]] std::string ResolveAssetText(const std::string& Relative) noexcept;

} // namespace Frontier
