/*============================================================================================================================================
                                                  STAMPLIBRARYVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Round-trip proof for the shared stamp library: procedural presets are well-formed (opaque art plus
//    transparent surround), a raster saved through SaveRaster reloads pixel-exact through PNG, filename
//    collisions version instead of overwrite, the vector-source sidecar lands beside the PNG, and the
//    bilinear sampler Texture Paint composites with reads back what was saved.

#include "UI/TextureStudio/StampLibrary.h"
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace Frontier;

namespace
{
int Failures = 0;
void Check(bool Condition, const char *Name, const std::string &Evidence = {})
{
    std::printf("[%s] %s%s%s\n", Condition ? "PASS" : "FAIL", Name, Evidence.empty() ? "" : " -- ", Evidence.c_str());
    if (!Condition) ++Failures;
}
} // namespace

int main()
{
    std::printf("StampLibrary validation\n");

    // Built-in presets: present, tintable, each with real art and real transparency.
    const auto Presets = StampLibrary::BuiltinPresets();
    Check(Presets.size() == 24, "twenty-four built-in presets", std::to_string(Presets.size()));
    bool PresetsSane = true;
    for (const auto &Preset : Presets)
    {
        if (!Preset.Builtin || !Preset.Tintable || Preset.Width != 256 || Preset.Height != 256) PresetsSane = false;
        size_t Opaque = 0, Clear = 0;
        for (size_t Pixel = 3; Pixel < Preset.Rgba.size(); Pixel += 4)
        {
            if (Preset.Rgba[Pixel] > 200) ++Opaque;
            if (Preset.Rgba[Pixel] < 10) ++Clear;
        }
        if (Opaque < 1000 || Clear < 1000) PresetsSane = false;
    }
    Check(PresetsSane, "presets carry art + transparent surround");
    // Ring: transparent centre, opaque band.
    if (const auto *Ring = &Presets.front())
    {
        const auto Centre = StampLibrary::Sample(*Ring, 0.5f, 0.5f);
        const auto Band = StampLibrary::Sample(*Ring, 0.5f + 0.34f, 0.5f);
        Check(Centre[3] < 0.05f && Band[3] > 0.9f, "ring preset samples as a ring");
    }

    // Save -> reload round-trip in a scratch directory.
    std::error_code Failure;
    const auto Directory = std::filesystem::temp_directory_path(Failure) / "frontier-stamp-library-validation";
    std::filesystem::remove_all(Directory, Failure);
    constexpr int W = 64, H = 48;
    std::vector<uint8_t> Pixels(size_t(W) * H * 4);
    for (int Y = 0; Y < H; ++Y)
        for (int X = 0; X < W; ++X)
        {
            uint8_t *Pixel = &Pixels[(size_t(Y) * W + X) * 4];
            Pixel[0] = uint8_t(X * 4);
            Pixel[1] = uint8_t(Y * 5);
            Pixel[2] = uint8_t(200 - X * 2);
            Pixel[3] = X < W / 2 ? 255 : 90; // hard alpha edge to sample against
        }
    std::string Id, Diagnostic;
    Check(StampLibrary::SaveRaster(Directory, "My Test Stamp!", W, H, Pixels, "# vector source\n", Id, Diagnostic),
        "SaveRaster writes a stamp", Diagnostic);
    Check(std::filesystem::exists(Directory / "my-test-stamp.png"), "slugified PNG filename");
    Check(std::filesystem::exists(Directory / "my-test-stamp.stamp.toml"), "vector source sidecar written");
    std::string Id2;
    Check(StampLibrary::SaveRaster(Directory, "My Test Stamp!", W, H, Pixels, "", Id2, Diagnostic) && Id2 != Id &&
              std::filesystem::exists(Directory / "my-test-stamp-2.png"),
        "name collision versions instead of overwriting", Id2);

    StampLibrary Library;
    Library.Reload(Directory);
    Check(Library.DiskCount() == 2, "reload finds both Disk stamps", std::to_string(Library.DiskCount()));
    Check(Library.Entries().size() == Presets.size() + 2, "builtins lead, Disk stamps follow");
    const auto *Saved = Library.Find(Id);
    Check(Saved != nullptr, "saved stamp findable by id", Id);
    if (Saved)
    {
        Check(Saved->Width == W && Saved->Height == H, "dimensions round-trip");
        Check(Saved->Rgba == Pixels, "pixels round-trip pixel-exact through PNG");
        Check(Saved->Name == "my test stamp", "display name de-slugified", Saved->Name);
        const auto Left = StampLibrary::Sample(*Saved, 0.25f, 0.5f);
        const auto Right = StampLibrary::Sample(*Saved, 0.75f, 0.5f);
        Check(std::fabs(Left[3] - 1.f) < 0.02f && std::fabs(Right[3] - 90.f / 255.f) < 0.02f,
            "sampler reads the saved alpha edge");
    }
    Check(Library.Find("builtin:star") != nullptr, "builtin findable by id");
    Check(Library.Find("disk:nope.png") == nullptr, "missing id misses cleanly");

    // In-place overwrite (a bound document's Save path): same file/id, new pixels, no new entry.
    std::vector<uint8_t> Updated = Pixels;
    Updated[0] = 77;
    std::string Id3;
    Check(StampLibrary::SaveRasterToFile(Directory / "my-test-stamp.png", W, H, Updated, "# v2\n", Id3, Diagnostic) && Id3 == Id,
        "SaveRasterToFile overwrites in place", Diagnostic);
    StampLibrary Reloaded;
    Reloaded.Reload(Directory);
    const auto *Again = Reloaded.Find(Id);
    Check(Reloaded.DiskCount() == 2 && Again && Again->Rgba == Updated, "overwrite replaces pixels without a new entry");
    Check(std::filesystem::exists(Directory / "my-test-stamp.stamp.toml"), "overwrite refreshes the sidecar too");

    std::filesystem::remove_all(Directory, Failure);
    std::printf(Failures ? "VALIDATION FAILED (%d)\n" : "VALIDATION PASSED\n", Failures);
    return Failures ? 1 : 0;
}
