//============================================================================================================================================
//                                                        TEXTUREINDEX.H
//============================================================================================================================================
// 🧩 Resident texture table addressed by slot (CLAUDE.md role 15): the CPU side of the bindless `sampler2D Textures[]`
//    binding. Codecs register images by path (deduplicated) or from memory (GLB buffer views); `Decode()` reads them
//    with stb_image, converts to RGBA8 (or RGBA16F for HDR), builds a box-filtered mip chain and records the colour
//    encoding so the exchange can pick VK_FORMAT_R8G8B8A8_SRGB vs _UNORM. Missing files decode to a 1×1 placeholder
//    (white for colour, flat normal 128,128,255 for data) and one log line.
//
// Budget: `[render] texture_limit` (slot count) and `MaximumEdge` (largest edge kept; larger images are down-sampled
//    on the CPU before upload so a GTX 1060 never faults on Sponza-class content).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

enum class TextureEncoding : uint8_t { Srgb8 = 0, Linear8 = 1, LinearHalf = 2 };

struct TextureDescriptor
{
    std::string      Name;          // path or embedded name (for logs)
    std::string      Path;          // empty for embedded
    TextureEncoding  Encoding  = TextureEncoding::Srgb8;
    uint32_t         Width     = 0u, Height = 0u;
    uint32_t         LevelCount = 0u;
    bool             Placeholder = false;   // decode failed / no file → 1×1
    bool             Linear    = false;     // requested as data (normals, roughness…)
    // Mip chain, level 0 first. Bytes per texel: 4 (Srgb8/Linear8) or 8 (LinearHalf).
    std::vector<uint8_t> Texels;
    std::vector<uint32_t> LevelOffsets;   // byte offset of each level in Texels
    [[nodiscard]] uint32_t TexelBytes() const noexcept { return Encoding == TextureEncoding::LinearHalf ? 8u : 4u; }
};

struct TextureIndexMetrics
{
    uint32_t Count       = 0u;
    uint32_t Placeholders = 0u;
    uint64_t ByteCount   = 0u;    // all levels, all textures
    double   DecodeMilliseconds = 0.0;
};

class TextureIndex
{
public:
    TextureIndex() noexcept = default;
    ~TextureIndex() noexcept = default;
    TextureIndex(const TextureIndex&) = delete;
    TextureIndex& operator=(const TextureIndex&) = delete;

    // Register by file path. The same (path, Linear) pair returns the same slot. Decoding is deferred to Decode().
    uint32_t RegisterPath(const std::string& Path, bool Linear) noexcept;
    // Register an in-memory encoded image (PNG/JPEG bytes — GLB buffer view). Copied.
    uint32_t RegisterEncoded(const std::string& Name, const uint8_t* Bytes, size_t ByteCount, bool Linear) noexcept;

    // Decode everything registered and not yet decoded. Returns the number of failures (placeholders created).
    //    `Report` receives one line per failure and the final summary. MaximumEdge caps the level-0 size (0 = no cap).
    uint32_t Decode(uint32_t MaximumEdge, std::vector<std::string>* Report) noexcept;

    void     Clear() noexcept;

    [[nodiscard]] const std::vector<TextureDescriptor>& QueryTextures() const noexcept { return Textures; }
    [[nodiscard]] const TextureIndexMetrics&            QueryMetrics()  const noexcept { return Metrics; }
    [[nodiscard]] uint32_t                              QueryCount()    const noexcept { return static_cast<uint32_t>(Textures.size()); }

    // Box-filter mip chain over RGBA8 (sRGB-aware when Encoding == Srgb8) or RGBA16F texels. Exposed for the harness.
    static void ConstructLevels(TextureDescriptor& T) noexcept;

private:
    struct Pending { std::vector<uint8_t> Encoded; bool Decoded = false; };
    std::vector<TextureDescriptor> Textures;
    std::vector<Pending>           Sources;
    TextureIndexMetrics            Metrics;
};

} // namespace Frontier
