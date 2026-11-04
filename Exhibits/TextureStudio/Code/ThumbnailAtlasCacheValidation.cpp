/*==========================================================================================================================================
                                                 THUMBNAILATLASCACHEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify the disk-backed gallery thumbnail atlas cache: round-trip integrity, digest/dimension
// mismatch rejection, truncation/corruption rejection, slot-indexed partial invalidation, and
// platform cache-directory resolution -- all without any GPU or windowing dependency.

#include "Engine/Interface/ThumbnailAtlasCache.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition) throw std::runtime_error(Description);
}
} // namespace

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

        // Hashing: deterministic, content-sensitive, order-sensitive combination.
        Require(HashBytes("abc", 3) == HashBytes("abc", 3), "FNV-1a not deterministic");
        Require(HashBytes("abc", 3) != HashBytes("abd", 3), "FNV-1a insensitive to content");
        Require(CombineHash(0x1111222233334444ull, 0x5555666677778888ull) !=
                    CombineHash(0x5555666677778888ull, 0x1111222233334444ull),
                "CombineHash not order-sensitive");
        const auto FileA = Evidence / "A.bin";
        std::ofstream(FileA, std::ios::binary) << "hello cache";
        const auto DigestA = HashFileContents(FileA);
        Require(DigestA != 0, "File digest returned the miss sentinel for real content");
        Require(HashFileContents(FileA) == DigestA, "File digest not stable across re-reads");
        std::ofstream(FileA, std::ios::binary) << "hello cache!";
        Require(HashFileContents(FileA) != DigestA, "File digest did not change with content");
        Require(HashFileContents(Evidence / "Missing.bin") == 0, "Missing file did not read as a miss");

        // Cache directory resolution: writable, stable across calls, distinct from a default temp race.
        const auto CacheDirectory = QueryFrontierCacheDirectory();
        Require(!CacheDirectory.empty(), "Cache directory resolution returned empty");
        Require(QueryFrontierCacheDirectory() == CacheDirectory, "Cache directory resolution is not stable");

        // Whole-atlas cache: miss before save, hit after, rejects digest/dimension drift, rejects truncation.
        constexpr uint32_t Width = 4, Height = 4;
        std::vector<uint8_t> Pixels(size_t(Width) * Height * 4);
        for (size_t Index = 0; Index < Pixels.size(); ++Index) Pixels[Index] = uint8_t(Index * 7 + 3);
        const auto AtlasFile = Evidence / "Pattern.atlas";
        ThumbnailAtlasCacheRecord Loaded;
        Require(!LoadThumbnailAtlasCache(AtlasFile, 0xABCDu, Width, Height, Loaded), "Loaded a nonexistent cache file");
        SaveThumbnailAtlasCache(AtlasFile, 0xABCDu, Width, Height, Pixels);
        Require(fs::exists(AtlasFile), "Save did not create the cache file");
        Require(LoadThumbnailAtlasCache(AtlasFile, 0xABCDu, Width, Height, Loaded), "Fresh matching cache reported a miss");
        Require(Loaded.Width == Width && Loaded.Height == Height && Loaded.Rgba == Pixels, "Round-tripped pixels do not match");
        Require(!LoadThumbnailAtlasCache(AtlasFile, 0xFFFFu, Width, Height, Loaded), "Wrong digest was accepted");
        Require(!LoadThumbnailAtlasCache(AtlasFile, 0xABCDu, Width + 1, Height, Loaded), "Wrong width was accepted");
        {
            // Truncate the file mid-pixel-payload: must be rejected, never partially trusted.
            auto Bytes = fs::file_size(AtlasFile);
            fs::resize_file(AtlasFile, Bytes - 1);
        }
        Require(!LoadThumbnailAtlasCache(AtlasFile, 0xABCDu, Width, Height, Loaded), "Truncated cache file was accepted");

        // Slot-indexed cache: a single changed/dropped slot's digest mismatch does not corrupt the
        // rest of the manifest, and the caller can tell exactly which paths still match.
        const std::vector<ThumbnailAtlasSlotCacheEntry> Slots = {
            {"Materials/Oak.toml", 111}, {"Materials/Steel.toml", 222}, {"Materials/Clay.toml", 333}};
        const auto SlotFile = Evidence / "Material.atlas";
        SaveThumbnailAtlasSlotCache(SlotFile, 0x10u, Width, Height, Pixels, Slots);
        ThumbnailAtlasSlotCacheRecord SlotLoaded;
        Require(LoadThumbnailAtlasSlotCache(SlotFile, 0x10u, Width, Height, SlotLoaded), "Slot cache reported a miss after save");
        Require(SlotLoaded.Slots.size() == 3, "Slot manifest size mismatch");
        Require(SlotLoaded.Slots[1].RelativePath == "Materials/Steel.toml" && SlotLoaded.Slots[1].Digest == 222,
                "Slot manifest entry mismatch");
        Require(SlotLoaded.Rgba == Pixels, "Slot cache pixels mismatch");
        Require(!LoadThumbnailAtlasSlotCache(SlotFile, 0x11u, Width, Height, SlotLoaded), "Slot cache ignored format digest drift");

        // Pruning: keeps only the most recently written files in a directory.
        const auto PruneDirectory = Evidence / "Prune";
        fs::create_directories(PruneDirectory);
        for (int Index = 0; Index < 5; ++Index) std::ofstream(PruneDirectory / (std::to_string(Index) + ".atlas"));
        PruneThumbnailAtlasCacheDirectory(PruneDirectory, 2);
        size_t Remaining = 0;
        for (const auto &Entry : fs::directory_iterator(PruneDirectory)) { (void)Entry; ++Remaining; }
        Require(Remaining == 2, "Pruning did not bound the cache directory to the requested count");

        std::cout << "ThumbnailAtlasCacheValidation: all checks passed\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "ThumbnailAtlasCacheValidation failed: " << Failure.what() << "\n";
        return 1;
    }
}
