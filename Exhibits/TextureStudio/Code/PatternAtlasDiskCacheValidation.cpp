/*==========================================================================================================================================
                                                  PATTERNATLASDISKCACHEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Prove the Pattern gallery atlas survives a process relaunch: a fresh panel instance pointed at
// the same library reloads an identical, byte-for-byte atlas from disk instead of re-baking every
// family through ThorVG again, and any content edit is detected rather than silently served stale.

#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include "Engine/Interface/ThumbnailAtlasCache.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition) throw std::runtime_error(Description);
}
size_t CountFiles(const std::filesystem::path &Directory)
{
    if (!std::filesystem::is_directory(Directory)) return 0;
    size_t Count = 0;
    for (const auto &Entry : std::filesystem::directory_iterator(Directory))
        if (Entry.is_regular_file()) ++Count;
    return Count;
}
std::string ReadAll(const std::filesystem::path &Path)
{
    std::ifstream Stream(Path, std::ios::binary);
    std::ostringstream Buffer;
    Buffer << Stream.rdbuf();
    return Buffer.str();
}
}

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    namespace fs = std::filesystem;
    try
    {
        Require(ArgumentCount == 2, "Expected disposable evidence directory");
        const auto Evidence = fs::absolute(Arguments[1]);
        fs::remove_all(Evidence);
        fs::create_directories(Evidence);
        // Isolate this process's cache directory from any real developer cache so the test is hermetic
        // and repeatable (mirrors how the shipped resolver reads XDG_CACHE_HOME on Linux).
#ifdef _WIN32
        _putenv_s("LOCALAPPDATA", Evidence.string().c_str());
#else
        setenv("XDG_CACHE_HOME", Evidence.string().c_str(), 1);
#endif
        const auto CacheDirectory = QueryFrontierCacheDirectory() / "PatternAtlas";

        // --- Real shipped library: cold bake vs. warm (disk-cached) reload ------------------------------------
        {
            PatternWorkspacePanel Cold;
            Require(Cold.ConfigurePresetDirectory("EngineContent/PatternArchives/TextureStudio"),
                    "Atlas panel rejected the pattern library.");
            const auto Start = std::chrono::steady_clock::now();
            const auto &ColdPixels = Cold.QueryPatternAtlasPixels();
            const auto ColdDuration = std::chrono::steady_clock::now() - Start;
            Require(Cold.QueryPatternAtlasRevision() > 0, "Cold bake never advanced its revision.");
            Require(!ColdPixels.empty(), "Cold bake produced no pixels.");
            Require(CountFiles(CacheDirectory) == 1, "Cold bake did not persist exactly one cache file.");
            const std::vector<uint8_t> ColdCopy = ColdPixels;

            PatternWorkspacePanel Warm;
            Require(Warm.ConfigurePresetDirectory("EngineContent/PatternArchives/TextureStudio"),
                    "Second panel instance rejected the same pattern library.");
            const auto WarmStart = std::chrono::steady_clock::now();
            const auto &WarmPixels = Warm.QueryPatternAtlasPixels();
            const auto WarmDuration = std::chrono::steady_clock::now() - WarmStart;
            Require(WarmPixels == ColdCopy, "Disk-cached reload did not match the original bake byte-for-byte.");
            Require(CountFiles(CacheDirectory) == 1, "Reload accumulated a duplicate cache file for the same identity.");
            Require(WarmDuration < ColdDuration,
                    "Disk-cached reload was not faster than the original 103-family ThorVG bake -- cache likely not hit.");
            std::cout << "Cold bake: " << std::chrono::duration_cast<std::chrono::microseconds>(ColdDuration).count()
                      << "us, warm reload: " << std::chrono::duration_cast<std::chrono::microseconds>(WarmDuration).count()
                      << "us\n";
        }

        // --- Synthetic library: content-edit invalidation and historical round trip --------------------------
        const auto Fixture = Evidence / "Fixture";
        fs::create_directories(Fixture);
        const auto SourceA = fs::path("EngineContent/PatternArchives/TextureStudio/starters/backstitch.vector.toml");
        const auto SourceB = fs::path("EngineContent/PatternArchives/TextureStudio/starters/archimedean-counterspirals.vector.toml");
        fs::copy_file(SourceA, Fixture / "alpha.toml");
        fs::copy_file(SourceB, Fixture / "beta.toml");
        const std::string OriginalAlpha = ReadAll(Fixture / "alpha.toml");

        std::vector<uint8_t> OriginalPixels;
        {
            PatternWorkspacePanel Panel;
            Require(Panel.ConfigurePresetDirectory(Fixture), "Synthetic fixture rejected as a pattern library.");
            OriginalPixels = Panel.QueryPatternAtlasPixels();
            Require(!OriginalPixels.empty(), "Synthetic fixture baked no pixels.");
        }
        const size_t FilesAfterOriginal = CountFiles(CacheDirectory);
        Require(FilesAfterOriginal >= 2, "Synthetic fixture did not add its own cache file alongside the real library's.");

        // Edit one family's source file (content changes, geometry does not) -- the digest must move,
        // and a *new* cache file must appear rather than reusing or corrupting the old one.
        {
            std::ofstream Append(Fixture / "alpha.toml", std::ios::app);
            Append << "\n# cache-invalidation-probe\n";
        }
        {
            PatternWorkspacePanel Panel;
            Require(Panel.ConfigurePresetDirectory(Fixture), "Synthetic fixture rejected on second pass.");
            const auto &EditedPixels = Panel.QueryPatternAtlasPixels();
            Require(!EditedPixels.empty(), "Edited fixture baked no pixels.");
        }
        Require(CountFiles(CacheDirectory) == FilesAfterOriginal + 1,
                "Editing a family's source file did not create a distinct new cache entry.");

        // Revert the edit: the digest returns to its original value, so the *original* cache file is
        // the one served again, byte-for-byte, without regenerating or duplicating anything.
        {
            std::ofstream Restore(Fixture / "alpha.toml", std::ios::binary | std::ios::trunc);
            Restore << OriginalAlpha;
        }
        {
            PatternWorkspacePanel Panel;
            Require(Panel.ConfigurePresetDirectory(Fixture), "Synthetic fixture rejected on third pass.");
            const auto &RevertedPixels = Panel.QueryPatternAtlasPixels();
            Require(RevertedPixels == OriginalPixels, "Reverting the edit did not reproduce the original cached atlas.");
        }
        Require(CountFiles(CacheDirectory) == FilesAfterOriginal + 1,
                "Reverting to a previously-cached identity should not create yet another file.");

        std::cout << "PatternAtlasDiskCacheValidation: all checks passed\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "PatternAtlasDiskCacheValidation failed: " << Failure.what() << "\n";
        return 1;
    }
}
