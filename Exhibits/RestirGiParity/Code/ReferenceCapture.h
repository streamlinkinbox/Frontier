/*==========================================================================================================================================
                                                           REFERENCECAPTURE.H
==========================================================================================================================================*/
// 🧩 Reader for a reference capture of the original demo (WriteLog.bin, Commands.txt, raw target dumps) and texel comparison.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Parity
{

// 📝 One host → device input of the demo: a queue write, a mapped-at-creation upload, or a texture write.
struct CapturedWrite
{
    int32_t                 Refresh  = -1;   // [-] refresh index, -1 during bring-up
    uint8_t                 Category = 0;    // [-] 0 region write, 1 region creation upload, 2 texture write
    std::string             Label{};
    uint64_t                Offset   = 0;    // [B] region offset (texture writes: bytes per row)
    std::array<uint32_t, 4> Extra{};         // [-] texture writes: origin xyz, extent (w | h << 16)
    std::vector<uint8_t>    Bytes{};
};

class ReferenceCapture
{
  public:
    bool Load(const std::filesystem::path& Folder, std::string* OutDiagnostic = nullptr);

    [[nodiscard]] std::vector<const CapturedWrite*> QueryWrites(int32_t Refresh, const std::string& Label) const;
    [[nodiscard]] const CapturedWrite*              QueryFirstWrite(const std::string& Label) const;
    bool ReadTarget(const std::string& Tag, const std::string& Label, std::vector<uint8_t>& OutBytes) const;
    [[nodiscard]] std::filesystem::path Folder() const { return Folder_; }

    std::vector<CapturedWrite> Writes{};

  private:
    std::filesystem::path Folder_{};
};

// 📝 Element-wise comparison of two texel streams of the same format.
struct TexelComparison
{
    uint64_t Elements        = 0;     // [-] scalar components compared
    uint64_t Identical       = 0;     // [-] bit-identical components
    double   MaximumAbsolute = 0.0;   // [-]
    double   MeanAbsolute    = 0.0;   // [-]
    double   MeanReference   = 0.0;   // [-] mean |reference|, the scale of the errors
    uint64_t NonFinite       = 0;     // [-] NaN/Inf in either stream
    double   PeakSignalNoise = 0.0;   // [dB] over the reference range
};

[[nodiscard]] float           DecodeHalf(uint16_t Bits) noexcept;
[[nodiscard]] TexelComparison CompareHalfTexels(const std::vector<uint8_t>& Port, const std::vector<uint8_t>& Reference);
[[nodiscard]] TexelComparison CompareFloatTexels(const std::vector<uint8_t>& Port, const std::vector<uint8_t>& Reference);
[[nodiscard]] TexelComparison CompareByteTexels(const std::vector<uint8_t>& Port, const std::vector<uint8_t>& Reference);
[[nodiscard]] std::string     DescribeComparison(const TexelComparison& Comparison);

} // namespace Frontier::Parity
