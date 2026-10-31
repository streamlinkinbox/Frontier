/*==========================================================================================================================================
                                              MATERIALATLASDISKCACHEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Prove the Material gallery atlas survives a process relaunch: a fresh rendering instance pointed at
// the same library and cache loads an identical, byte-for-byte atlas from disk instead of re-baking every
// visible card through the PBR pipeline again, and any source edit is detected rather than silently stale.

#include "AtlasReadback.h"
#include "Engine/Interface/ThumbnailAtlasCache.h"
#include "Engine/Render/MaterialLibraryRendering.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
using namespace Frontier;

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

// 📝 Drive the real submission loop (identical shape to MaterialWorkspaceEvidence.cpp) for a fixed
// number of frames; callers size MaxFrames for either a cold bake-to-settled run or a single warm poll.
std::vector<unsigned char> DriveUntilSettled(AtlasValidationDevice &Device, MaterialLibraryRendering &Rendering,
    uint64_t &Serial, uint32_t MaxFrames)
{
    std::vector<unsigned char> Pixels;
    for (uint32_t Frame = 0; Frame < MaxFrames; ++Frame)
    {
        ++Serial;
        const uint32_t Extent = Rendering.QueryAtlasExtent();
        Pixels = Device.Capture(Rendering.QueryAtlasImage(), {Extent, Extent},
            [&](VkCommandBuffer Commands) { Rendering.ConstructCommands(Commands, Serial - 1, Serial); },
            [&]() { Rendering.ConfirmSubmission(); });
    }
    return Pixels;
}
} // namespace

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(Count == 5, "Expected driver, shaders, library and disposable evidence directory.");
        const std::filesystem::path Shaders(Arguments[2]), Library = std::filesystem::absolute(Arguments[3]);
        const auto Evidence = std::filesystem::absolute(Arguments[4]);
        std::filesystem::remove_all(Evidence);
        std::filesystem::create_directories(Evidence);
        // Isolate this process's cache directory from any real developer cache so the test is hermetic
        // and repeatable (mirrors PatternAtlasDiskCacheValidation.cpp and the shipped XDG resolver).
#ifdef _WIN32
        _putenv_s("LOCALAPPDATA", Evidence.string().c_str());
#else
        setenv("XDG_CACHE_HOME", Evidence.string().c_str(), 1);
#endif
        const auto CacheDirectory = QueryFrontierCacheDirectory() / "MaterialAtlas";
        const uint64_t FormatDigest = 0x4D41544C41533100ull;

        const std::vector<std::filesystem::path> Visible{"racing-green.material", "24k-gold.material",
            "brass-alloy.material", "bronze-alloy.material"};

        std::vector<unsigned char> ColdPixels;
        {
            AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
            MaterialLibraryRendering Rendering;
            Rendering.Initialize(Device.QueryAccess(), Shaders);
            Rendering.ConfigureAtlasCache(CacheDirectory / "MaterialAtlas.atlas", FormatDigest);
            Rendering.RequestLibrary(Library, Visible);
            uint64_t Serial = 0;
            // 📝 One frame per dirty tile to bake, plus headroom for the readback + disk save to settle.
            ColdPixels = DriveUntilSettled(Device, Rendering, Serial, uint32_t(Visible.size()) + 6);
            AssertRasterCondition(Rendering.QueryBakeCount() == Visible.size(), "Cold run did not bake every visible card.");
            AssertRasterCondition(Rendering.QueryAtlasCacheSettled(), "Cold run never settled its disk cache.");
            AssertRasterCondition(CountFiles(CacheDirectory) == 1, "Cold bake did not persist exactly one cache file.");
            (void)Device.DeviceWaitIdle(Device.Device);
        }

        // --- Relaunch: a brand new rendering instance must reload the atlas from disk instead of rebaking ---
        {
            AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
            MaterialLibraryRendering Rendering;
            Rendering.Initialize(Device.QueryAccess(), Shaders);
            Rendering.ConfigureAtlasCache(CacheDirectory / "MaterialAtlas.atlas", FormatDigest);
            Rendering.RequestLibrary(Library, Visible);
            uint64_t Serial = 0;
            const auto WarmPixels = DriveUntilSettled(Device, Rendering, Serial, 1);
            AssertRasterCondition(Rendering.QueryBakeCount() == 0,
                "Warm reload baked a card through the PBR pipeline instead of trusting the disk cache.");
            AssertRasterCondition(WarmPixels == ColdPixels, "Disk-cached reload did not match the original bake byte-for-byte.");
            AssertRasterCondition(CountFiles(CacheDirectory) == 1, "Reload accumulated a duplicate cache file for the same identity.");
            (void)Device.DeviceWaitIdle(Device.Device);
        }

        // --- Synthetic fixture: content-edit invalidation and historical round trip ---------------------------
        const auto Fixture = Evidence / "Fixture";
        std::filesystem::create_directories(Fixture / "Textures");
        std::filesystem::copy_file(Library / "racing-green.material", Fixture / "alpha.material");
        std::filesystem::copy_file(Library / "24k-gold.material", Fixture / "beta.material");
        const std::string OriginalAlpha = ReadAll(Fixture / "alpha.material");
        const std::vector<std::filesystem::path> FixtureVisible{"alpha.material", "beta.material"};

        std::vector<unsigned char> OriginalPixels;
        {
            AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
            MaterialLibraryRendering Rendering;
            Rendering.Initialize(Device.QueryAccess(), Shaders);
            Rendering.ConfigureAtlasCache(CacheDirectory / "Fixture.atlas", FormatDigest);
            Rendering.RequestLibrary(Fixture, FixtureVisible);
            uint64_t Serial = 0;
            OriginalPixels = DriveUntilSettled(Device, Rendering, Serial, uint32_t(FixtureVisible.size()) + 6);
            AssertRasterCondition(Rendering.QueryBakeCount() == FixtureVisible.size(), "Synthetic fixture did not bake its cards.");
            (void)Device.DeviceWaitIdle(Device.Device);
        }
        const size_t FilesAfterOriginal = CountFiles(CacheDirectory);
        AssertRasterCondition(FilesAfterOriginal >= 2, "Synthetic fixture did not add its own cache file alongside the real library's.");

        // Edit one card's source file (content changes, geometry does not) -- the digest must move, and
        // a *new* cache file must appear rather than reusing or corrupting the old one.
        {
            std::ofstream Append(Fixture / "alpha.material", std::ios::app);
            Append << "\n# cache-invalidation-probe\n";
        }
        {
            AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
            MaterialLibraryRendering Rendering;
            Rendering.Initialize(Device.QueryAccess(), Shaders);
            Rendering.ConfigureAtlasCache(CacheDirectory / "Fixture.atlas", FormatDigest);
            Rendering.RequestLibrary(Fixture, FixtureVisible);
            uint64_t Serial = 0;
            DriveUntilSettled(Device, Rendering, Serial, uint32_t(FixtureVisible.size()) + 6);
            AssertRasterCondition(Rendering.QueryBakeCount() == 1, "Editing one card's source rebaked more (or fewer) than that one card.");
            (void)Device.DeviceWaitIdle(Device.Device);
        }
        AssertRasterCondition(CountFiles(CacheDirectory) == FilesAfterOriginal + 1,
            "Editing a card's source file did not create a distinct new cache entry.");

        // Revert the edit: the digest returns to its original value, so the *original* cache file is
        // the one served again, byte-for-byte, without regenerating or duplicating anything.
        {
            std::ofstream Restore(Fixture / "alpha.material", std::ios::binary | std::ios::trunc);
            Restore << OriginalAlpha;
        }
        {
            AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
            MaterialLibraryRendering Rendering;
            Rendering.Initialize(Device.QueryAccess(), Shaders);
            Rendering.ConfigureAtlasCache(CacheDirectory / "Fixture.atlas", FormatDigest);
            Rendering.RequestLibrary(Fixture, FixtureVisible);
            uint64_t Serial = 0;
            const auto RevertedPixels = DriveUntilSettled(Device, Rendering, Serial, 1);
            AssertRasterCondition(Rendering.QueryBakeCount() == 0, "Reverting the edit did not reproduce the original cache hit.");
            AssertRasterCondition(RevertedPixels == OriginalPixels, "Reverting the edit did not reproduce the original cached atlas.");
            (void)Device.DeviceWaitIdle(Device.Device);
        }
        AssertRasterCondition(CountFiles(CacheDirectory) == FilesAfterOriginal + 1,
            "Reverting to a previously-cached identity should not create yet another file.");

        std::cout << "MaterialAtlasDiskCacheValidation: all checks passed\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "MaterialAtlasDiskCacheValidation failed: " << Failure.what() << "\n";
        return 1;
    }
}
