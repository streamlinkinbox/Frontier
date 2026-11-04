/*============================================================================================================================================
                                                  STAMPLIBRARY.H
============================================================================================================================================*/
// 🖨️ The decal library shared by the Stamp workspace (which authors and saves stamps) and the Texture
//    Paint workspace (which places them): built-in procedural presets plus user stamps saved as PNGs in
//    one directory (the host points both panels at <library>/Stamps). A Disk stamp saved from the Stamp
//    workspace also keeps a <name>.stamp.toml sidecar with its vector source, so it stays re-openable.

#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{
struct StampPbrSample
{
    float Coverage = 0.f;
    float Height = 0.f, HeightMask = 0.f;
    float NormalX = 0.f, NormalY = 0.f, NormalZ = 1.f, NormalMask = 0.f;
    float Roughness = .7f, RoughnessMask = 0.f;
    float Metalness = 0.f, MetalnessMask = 0.f;
    float Opacity = 0.f;
    float Occlusion = 1.f, OcclusionMask = 0.f;
    float Transmission = 0.f, TransmissionMask = 0.f;
    float Clearcoat = 0.f, ClearcoatMask = 0.f;
};

// 📝 One decal: straight-alpha RGBA pixels, row-major from the top-left (image v grows downward).
struct StampLibraryEntry
{
    std::string Id;   // "builtin:<slug>" or "disk:<filename>"
    std::string Name; // display name for pickers/status lines
    bool Builtin = false;
    bool Tintable = false; // white art: Texture Paint tints it with the brush colour when placing
    int Width = 0, Height = 0;
    std::vector<uint8_t> Rgba;
    std::filesystem::path File; // PNG path for Disk entries, empty for built-ins
    bool HasPbrMaps = false;
    int PbrExtent = 0;
    std::vector<float> Coverage, HeightMap, NormalX, NormalY, NormalZ;
    std::vector<float> Roughness, Metalness, Opacity, Occlusion, Transmission, Clearcoat;
    std::vector<float> RoughnessMask, MetalnessMask, HeightMask, NormalMask, OcclusionMask, TransmissionMask, ClearcoatMask;
};

class StampLibrary
{
  public:
    // 🔄 (Re)scan Directory for *.png stamps; built-in presets always lead the list. Missing/unreadable
    // files are skipped, never fatal - a half-saved library still opens.
    void Reload(const std::filesystem::path &Directory);
    [[nodiscard]] const std::vector<StampLibraryEntry> &Entries() const noexcept { return LibraryEntries; }
    [[nodiscard]] const StampLibraryEntry *Find(const std::string &Id) const noexcept;
    [[nodiscard]] const std::filesystem::path &Directory() const noexcept { return LibraryDirectory; }
    [[nodiscard]] size_t DiskCount() const noexcept;

    // 💾 Write <slug>.png (and <slug>.stamp.toml when SourceToml is non-empty) into Directory, creating
    // it if needed; filename collisions get a numeric suffix. Returns the new entry's id via OutId.
    static bool SaveRaster(const std::filesystem::path &Directory, const std::string &Name,
        int Width, int Height, const std::vector<uint8_t> &Rgba, const std::string &SourceToml,
        std::string &OutId, std::string &Diagnostic);
    // 💾 Overwrite one existing library PNG in place (plus its sidecar): the update path a bound
    // document's Save takes, so repeated saves revise the same library card instead of piling up
    // numeric-suffix copies. Recreates the file (and parent directory) if it vanished meanwhile.
    static bool SaveRasterToFile(const std::filesystem::path &Target, int Width, int Height,
        const std::vector<uint8_t> &Rgba, const std::string &SourceToml,
        std::string &OutId, std::string &Diagnostic);
    // 🎨 The procedural preset decals (white art at 256px, tintable): rings, stars, badges, marks and ornaments.
    [[nodiscard]] static std::vector<StampLibraryEntry> BuiltinPresets();
    // 🖨️ Re-render one builtin preset at an arbitrary extent: the preset fields are resolution-independent
    // signed-distance functions, so the result is the true stamp at that size, never an upscale of the 256px
    // picker raster. Returns false when Id is not a builtin preset.
    static bool RenderBuiltinAt(const std::string &Id, int Extent, StampLibraryEntry &Out);
    // 🔬 Bilinear sample with uv in [0,1]^2 (v downward from the image top); returns straight rgba 0..1.
    [[nodiscard]] static std::array<float, 4> Sample(const StampLibraryEntry &Entry, float U, float V) noexcept;
    [[nodiscard]] static StampPbrSample SamplePbr(const StampLibraryEntry &Entry, float U, float V) noexcept;

  private:
    std::filesystem::path LibraryDirectory;
    std::vector<StampLibraryEntry> LibraryEntries;
};
} // namespace Frontier
