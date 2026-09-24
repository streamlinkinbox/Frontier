//============================================================================================================================================
//                                                      ASSETRESOLUTION.CPP
//============================================================================================================================================
// See AssetResolution.h.

#include "AssetResolution.h"

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#elif defined(__APPLE__)
#   include <mach-o/dyld.h>
#else
#   include <unistd.h>
#endif

namespace Frontier {

namespace {

// How far up the parent chain to look. Deep enough for Build\Output\Windows\Release\Binary (5) with room to spare,
//    shallow enough that a misconfigured run cannot walk to the filesystem root and start matching stray folders.
constexpr int kMaximumAncestorDepth = 12;

} // namespace

std::filesystem::path QueryExecutableDirectory() noexcept
{
#if defined(_WIN32)
    wchar_t Buffer[MAX_PATH]{};
    const DWORD Length = GetModuleFileNameW(nullptr, Buffer, MAX_PATH);
    if (Length == 0u) return {};
    return std::filesystem::path(Buffer).parent_path();
#elif defined(__APPLE__)
    char     Buffer[4096]{};
    uint32_t Size = sizeof(Buffer);
    if (_NSGetExecutablePath(Buffer, &Size) != 0) return {};
    std::error_code Error;
    const std::filesystem::path Real = std::filesystem::weakly_canonical(std::filesystem::path(Buffer), Error);
    return (Error ? std::filesystem::path(Buffer) : Real).parent_path();
#else
    char Buffer[4096]{};
    const ssize_t Length = readlink("/proc/self/exe", Buffer, sizeof(Buffer) - 1u);
    if (Length <= 0) return {};
    return std::filesystem::path(std::string(Buffer, static_cast<size_t>(Length))).parent_path();
#endif
}

std::filesystem::path QueryContentRoot() noexcept
{
    std::error_code Error;

    // Resolved once. The answer cannot change during a run (neither the executable nor the working directory moves
    //    under us), and this is called per asset — including once per moon texture at bring-up.
    static const std::filesystem::path Cached = []() -> std::filesystem::path
    {
        std::error_code Local;
        const std::filesystem::path Starts[2] = { std::filesystem::current_path(Local), QueryExecutableDirectory() };
        for (const std::filesystem::path& Start : Starts)
        {
            std::filesystem::path Probe = Start;
            for (int Depth = 0; Depth < kMaximumAncestorDepth && !Probe.empty(); ++Depth)
            {
                if (std::filesystem::is_directory(Probe / "EngineContent", Local)) return Probe;
                const std::filesystem::path Parent = Probe.parent_path();
                if (Parent == Probe) break;
                Probe = Parent;
            }
        }
        return {};
    }();

    (void)Error;
    return Cached;
}

std::filesystem::path ResolveAsset(const std::string& Relative) noexcept
{
    std::error_code Error;
    if (Relative.empty()) return Relative;

    // ① Exactly what the caller asked for, relative to the working directory. Checked first so a developer running
    //    from the repository root behaves identically to before this file existed, and so an absolute path short-circuits.
    if (std::filesystem::exists(Relative, Error)) return Relative;

    const std::filesystem::path AsPath(Relative);
    if (AsPath.is_absolute()) return AsPath;   // absolute and missing: nothing to search for, keep the caller's path

    // ② The content root, when one was found.
    const std::filesystem::path Root = QueryContentRoot();
    if (!Root.empty())
    {
        const std::filesystem::path Candidate = Root / AsPath;
        if (std::filesystem::exists(Candidate, Error)) return Candidate;
    }

    // ③ The executable's folder and its parents. Catches layouts with no EngineContent at all (a level file shipped
    //    next to the binary, say) that ② cannot anchor.
    std::filesystem::path Probe = QueryExecutableDirectory();
    for (int Depth = 0; Depth < kMaximumAncestorDepth && !Probe.empty(); ++Depth)
    {
        const std::filesystem::path Candidate = Probe / AsPath;
        if (std::filesystem::exists(Candidate, Error)) return Candidate;
        const std::filesystem::path Parent = Probe.parent_path();
        if (Parent == Probe) break;
        Probe = Parent;
    }

    // Nothing matched. Hand back what was asked for so the caller's diagnostic names the path the caller knows.
    return AsPath;
}

std::string ResolveAssetText(const std::string& Relative) noexcept
{
    return ResolveAsset(Relative).string();
}

} // namespace Frontier
