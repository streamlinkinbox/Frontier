/*==========================================================================================================================================
                                                      PROJECT CONTENT DEFINITION
                                     ------------------------------------------------------------------
                              Project Content/ (with compile-time staged imports) resolves before EngineContent/.
==========================================================================================================================================*/
// 🧩 Resolution order: configured Project Content/ -> FRONTIER_PROJECT_CONTENT_DIR -> ./Content -> EngineContent/.

#include "ProjectContent.h"

#include <cstdlib>
#include <system_error>

namespace Frontier
{

namespace
{

std::filesystem::path ExecutableDirectoryValue;
std::filesystem::path ProjectContentDirectoryValue;

bool IsDirectory(const std::filesystem::path& Path) noexcept
{
    std::error_code Error;
    return !Path.empty() && std::filesystem::is_directory(Path, Error);
}

bool IsFile(const std::filesystem::path& Path) noexcept
{
    std::error_code Error;
    return !Path.empty() && std::filesystem::is_regular_file(Path, Error);
}

} // namespace

void ConfigureContentRoots(std::filesystem::path ExecutableDirectory, std::filesystem::path ProjectContentDirectory)
{
    ExecutableDirectoryValue     = std::move(ExecutableDirectory);
    ProjectContentDirectoryValue = std::move(ProjectContentDirectory);
}

std::filesystem::path QueryEngineContentDirectory()
{
    const std::filesystem::path Staged = ExecutableDirectoryValue / "EngineContent";
    if (IsDirectory(Staged))
    {
        return Staged;
    }

    std::filesystem::path Cursor = ExecutableDirectoryValue;
    for (int Depth = 0; Depth < 5 && !Cursor.empty(); ++Depth)
    {
        const std::filesystem::path Candidate = Cursor / "EngineContent";
        if (IsDirectory(Candidate))
        {
            return Candidate;
        }
        Cursor = Cursor.parent_path();
    }
    return {};
}

std::filesystem::path ResolveProjectContentDirectory(const std::filesystem::path& Requested)
{
    if (Requested.is_absolute())
    {
        return Requested;
    }

    const std::filesystem::path BesideExecutable = ExecutableDirectoryValue / Requested;
    if (IsDirectory(BesideExecutable))
    {
        return BesideExecutable;
    }

    std::error_code             Error;
    const std::filesystem::path FromWorkingDirectory = std::filesystem::current_path(Error) / Requested;
    if (IsDirectory(FromWorkingDirectory))
    {
        return FromWorkingDirectory;
    }
    return BesideExecutable;
}

std::filesystem::path ResolveProjectContentFile(const std::filesystem::path& Relative)
{
    if (Relative.is_absolute())
    {
        return IsFile(Relative) ? Relative : std::filesystem::path{};
    }

    if (!ProjectContentDirectoryValue.empty())
    {
        const std::filesystem::path ProjectCandidate = ProjectContentDirectoryValue / Relative;
        if (IsFile(ProjectCandidate))
        {
            return ProjectCandidate;
        }
    }

    if (const char* EnvContent = std::getenv("FRONTIER_PROJECT_CONTENT_DIR"); EnvContent && *EnvContent)
    {
        const std::filesystem::path EnvCandidate = std::filesystem::path(EnvContent) / Relative;
        if (IsFile(EnvCandidate))
        {
            return EnvCandidate;
        }
    }

    std::error_code             Error;
    const std::filesystem::path CwdContent = std::filesystem::current_path(Error) / "Content" / Relative;
    if (IsFile(CwdContent))
    {
        return CwdContent;
    }

    const std::filesystem::path EngineRoot = QueryEngineContentDirectory();
    if (!EngineRoot.empty())
    {
        const std::filesystem::path EngineCandidate = EngineRoot / Relative;
        if (IsFile(EngineCandidate))
        {
            return EngineCandidate;
        }
    }
    return {};
}

} // namespace Frontier
