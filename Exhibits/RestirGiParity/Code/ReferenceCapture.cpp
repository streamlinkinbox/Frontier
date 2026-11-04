/*==========================================================================================================================================
                                                          REFERENCECAPTURE.CPP
==========================================================================================================================================*/
// 🧩 Reader for a reference capture of the original demo (WriteLog.bin, Commands.txt, raw target dumps) and texel comparison.

#include "ReferenceCapture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace Frontier::Parity
{

namespace
{

std::string SanitizeLabel(const std::string& Label)
{
    // 📝 Same rule as CaptureHooks.mjs: every run of non-alphanumeric characters becomes one underscore.
    std::string Result;
    bool        Gap = false;
    for (unsigned char Character : Label)
    {
        const bool Alphanumeric = (Character >= '0' && Character <= '9') || (Character >= 'A' && Character <= 'Z') ||
                                  (Character >= 'a' && Character <= 'z');
        if (Alphanumeric)
        {
            Result.push_back(static_cast<char>(Character));
            Gap = false;
        }
        else if (!Gap)
        {
            Result.push_back('_');
            Gap = true;
        }
    }
    return Result;
}

bool ReadFile(const std::filesystem::path& Path, std::vector<uint8_t>& OutBytes)
{
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream) return false;
    OutBytes.assign(std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

bool ReferenceCapture::Load(const std::filesystem::path& Folder, std::string* OutDiagnostic)
{
    Folder_ = Folder;
    std::vector<uint8_t> Bytes;
    if (!ReadFile(Folder / "WriteLog.bin", Bytes))
    {
        if (OutDiagnostic) *OutDiagnostic = "WriteLog.bin missing in " + Folder.string();
        return false;
    }
    size_t Cursor = 0;
    auto Take = [&](void* Target, size_t Count) {
        if (Cursor + Count > Bytes.size()) return false;
        std::memcpy(Target, Bytes.data() + Cursor, Count);
        Cursor += Count;
        return true;
    };
    while (Cursor < Bytes.size())
    {
        CapturedWrite Write;
        uint16_t      LabelLength = 0;
        uint32_t      ByteCount   = 0;
        if (!Take(&Write.Refresh, 4) || !Take(&Write.Category, 1) || !Take(&LabelLength, 2)) break;
        Write.Label.resize(LabelLength);
        if (!Take(Write.Label.data(), LabelLength) || !Take(&Write.Offset, 8) || !Take(Write.Extra.data(), 16) || !Take(&ByteCount, 4)) break;
        Write.Bytes.resize(ByteCount);
        if (!Take(Write.Bytes.data(), ByteCount)) break;
        Writes.push_back(std::move(Write));
    }
    return true;
}

std::vector<const CapturedWrite*> ReferenceCapture::QueryWrites(int32_t Refresh, const std::string& Label) const
{
    std::vector<const CapturedWrite*> Result;
    for (const CapturedWrite& Write : Writes)
    {
        if (Write.Refresh == Refresh && Write.Label == Label) Result.push_back(&Write);
    }
    return Result;
}

const CapturedWrite* ReferenceCapture::QueryFirstWrite(const std::string& Label) const
{
    for (const CapturedWrite& Write : Writes)
    {
        if (Write.Label == Label) return &Write;
    }
    return nullptr;
}

bool ReferenceCapture::ReadTarget(const std::string& Tag, const std::string& Label, std::vector<uint8_t>& OutBytes) const
{
    const std::string Stem = Tag + "_" + SanitizeLabel(Label);
    return ReadFile(Folder_ / (Stem + ".raw"), OutBytes) || ReadFile(Folder_ / (Stem + ".bin"), OutBytes);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   COMPARISON
//------------------------------------------------------------------------------------------------------------------------

float DecodeHalf(uint16_t Bits) noexcept
{
    const uint32_t Sign     = (Bits >> 15) & 1u;
    const uint32_t Exponent = (Bits >> 10) & 31u;
    const uint32_t Mantissa = Bits & 1023u;
    float Magnitude;
    if (Exponent == 0) Magnitude = std::ldexp(static_cast<float>(Mantissa), -24);
    else if (Exponent == 31) Magnitude = Mantissa ? NAN : INFINITY;
    else Magnitude = std::ldexp(static_cast<float>(Mantissa | 1024u), static_cast<int>(Exponent) - 25);
    return Sign ? -Magnitude : Magnitude;
}

namespace
{

template <typename DecodeFunction, typename Word>
TexelComparison CompareWith(const std::vector<uint8_t>& Port, const std::vector<uint8_t>& Reference, DecodeFunction Decode)
{
    TexelComparison Result;
    const size_t Count = std::min(Port.size(), Reference.size()) / sizeof(Word);
    double SumAbsolute  = 0.0;
    double SumReference = 0.0;
    double SumSquared   = 0.0;
    double Lowest       = INFINITY;
    double Highest      = -INFINITY;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        Word PortWord, ReferenceWord;
        std::memcpy(&PortWord, Port.data() + Index * sizeof(Word), sizeof(Word));
        std::memcpy(&ReferenceWord, Reference.data() + Index * sizeof(Word), sizeof(Word));
        ++Result.Elements;
        if (PortWord == ReferenceWord) ++Result.Identical;
        const double PortValue      = Decode(PortWord);
        const double ReferenceValue = Decode(ReferenceWord);
        if (!std::isfinite(PortValue) || !std::isfinite(ReferenceValue))
        {
            if (PortWord != ReferenceWord) ++Result.NonFinite;
            continue;
        }
        const double Absolute = std::fabs(PortValue - ReferenceValue);
        Result.MaximumAbsolute = std::max(Result.MaximumAbsolute, Absolute);
        SumAbsolute += Absolute;
        SumSquared += Absolute * Absolute;
        SumReference += std::fabs(ReferenceValue);
        Lowest  = std::min(Lowest, ReferenceValue);
        Highest = std::max(Highest, ReferenceValue);
    }
    if (Result.Elements > 0)
    {
        Result.MeanAbsolute  = SumAbsolute / static_cast<double>(Result.Elements);
        Result.MeanReference = SumReference / static_cast<double>(Result.Elements);
        const double MeanSquared = SumSquared / static_cast<double>(Result.Elements);
        const double Range       = std::max(1.0e-12, Highest - Lowest);
        Result.PeakSignalNoise   = MeanSquared > 0.0 ? 10.0 * std::log10(Range * Range / MeanSquared) : INFINITY;
    }
    return Result;
}

} // namespace

TexelComparison CompareHalfTexels(const std::vector<uint8_t>& Port, const std::vector<uint8_t>& Reference)
{
    return CompareWith<float (*)(uint16_t), uint16_t>(Port, Reference, [](uint16_t Bits) { return DecodeHalf(Bits); });
}

TexelComparison CompareFloatTexels(const std::vector<uint8_t>& Port, const std::vector<uint8_t>& Reference)
{
    return CompareWith<float (*)(uint32_t), uint32_t>(Port, Reference, [](uint32_t Bits) {
        float Value;
        std::memcpy(&Value, &Bits, 4);
        return Value;
    });
}

TexelComparison CompareByteTexels(const std::vector<uint8_t>& Port, const std::vector<uint8_t>& Reference)
{
    return CompareWith<float (*)(uint8_t), uint8_t>(Port, Reference, [](uint8_t Byte) { return static_cast<float>(Byte) / 255.0f; });
}

std::string DescribeComparison(const TexelComparison& Comparison)
{
    char Text[256];
    std::snprintf(Text, sizeof(Text), "%llu values, %.4f%% identical, max |d| %.6g, mean |d| %.6g (mean |ref| %.6g), PSNR %.2f dB, non-finite %llu",
                  static_cast<unsigned long long>(Comparison.Elements),
                  Comparison.Elements ? 100.0 * static_cast<double>(Comparison.Identical) / static_cast<double>(Comparison.Elements) : 0.0,
                  Comparison.MaximumAbsolute, Comparison.MeanAbsolute, Comparison.MeanReference, Comparison.PeakSignalNoise,
                  static_cast<unsigned long long>(Comparison.NonFinite));
    return Text;
}

} // namespace Frontier::Parity
