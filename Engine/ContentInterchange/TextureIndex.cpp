//============================================================================================================================================
//                                                       TEXTUREINDEX.CPP
//============================================================================================================================================
// 🧩 stb_image decode, sRGB-aware box mips, placeholders. The only TU that defines STB_IMAGE_IMPLEMENTATION.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include "TextureIndex.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

// sRGB ⇄ linear tables so the mip filter averages light, not code values (a 50 % grey checker must stay 50 % grey).
struct SrgbTables
{
    float ToLinear[256];
    SrgbTables()
    {
        for (int I = 0; I < 256; ++I)
        {
            const float C = I / 255.0f;
            ToLinear[I] = C <= 0.04045f ? C / 12.92f : std::pow((C + 0.055f) / 1.055f, 2.4f);
        }
    }
    static uint8_t FromLinear(float L)
    {
        L = std::clamp(L, 0.0f, 1.0f);
        const float C = L <= 0.0031308f ? L * 12.92f : 1.055f * std::pow(L, 1.0f / 2.4f) - 0.055f;
        return static_cast<uint8_t>(C * 255.0f + 0.5f);
    }
};
const SrgbTables& Tables() { static SrgbTables T; return T; }

uint16_t FloatToHalf(float F)
{
    uint32_t X; std::memcpy(&X, &F, 4u);
    const uint32_t Sign = (X >> 16) & 0x8000u;
    int32_t  Exponent = static_cast<int32_t>((X >> 23) & 0xFFu) - 127 + 15;
    uint32_t Mantissa = X & 0x7FFFFFu;
    if (Exponent <= 0) return static_cast<uint16_t>(Sign);
    if (Exponent >= 31) return static_cast<uint16_t>(Sign | 0x7C00u);
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exponent) << 10) | (Mantissa >> 13));
}
float HalfToFloat(uint16_t H)
{
    const uint32_t Sign = (H & 0x8000u) << 16;
    uint32_t Exponent = (H >> 10) & 0x1Fu, Mantissa = H & 0x3FFu;
    if (Exponent == 0) { if (Mantissa == 0) { float F; uint32_t X = Sign; std::memcpy(&F, &X, 4u); return F; } float F = Mantissa / 1024.0f * std::pow(2.0f, -14.0f); return Sign ? -F : F; }
    uint32_t X = Sign | ((Exponent + 112u) << 23) | (Mantissa << 13);
    float F; std::memcpy(&F, &X, 4u); return F;
}

// Down-sample one level (RGBA, 4 or 8 bytes per texel) by 2 with a box filter; odd edges clamp.
void Downsample(const uint8_t* Src, uint32_t W, uint32_t H, uint8_t* Dst, uint32_t DW, uint32_t DH, TextureEncoding Encoding)
{
    const SrgbTables& T = Tables();
    for (uint32_t Y = 0; Y < DH; ++Y)
        for (uint32_t X = 0; X < DW; ++X)
        {
            const uint32_t X0 = std::min(X * 2u, W - 1u), X1 = std::min(X * 2u + 1u, W - 1u);
            const uint32_t Y0 = std::min(Y * 2u, H - 1u), Y1 = std::min(Y * 2u + 1u, H - 1u);
            const uint32_t Corners[4] = { Y0 * W + X0, Y0 * W + X1, Y1 * W + X0, Y1 * W + X1 };
            float Sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            for (uint32_t Corner : Corners)
            {
                if (Encoding == TextureEncoding::LinearHalf)
                {
                    const uint16_t* P = reinterpret_cast<const uint16_t*>(Src) + Corner * 4u;
                    for (int C = 0; C < 4; ++C) Sum[C] += HalfToFloat(P[C]);
                }
                else
                {
                    const uint8_t* P = Src + Corner * 4u;
                    for (int C = 0; C < 3; ++C) Sum[C] += Encoding == TextureEncoding::Srgb8 ? T.ToLinear[P[C]] : P[C] / 255.0f;
                    Sum[3] += P[3] / 255.0f;
                }
            }
            for (float& S : Sum) S *= 0.25f;
            if (Encoding == TextureEncoding::LinearHalf)
            {
                uint16_t* Q = reinterpret_cast<uint16_t*>(Dst) + (Y * DW + X) * 4u;
                for (int C = 0; C < 4; ++C) Q[C] = FloatToHalf(Sum[C]);
            }
            else
            {
                uint8_t* Q = Dst + (Y * DW + X) * 4u;
                for (int C = 0; C < 3; ++C) Q[C] = Encoding == TextureEncoding::Srgb8 ? SrgbTables::FromLinear(Sum[C]) : static_cast<uint8_t>(std::clamp(Sum[C], 0.0f, 1.0f) * 255.0f + 0.5f);
                Q[3] = static_cast<uint8_t>(std::clamp(Sum[3], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
}

void MakePlaceholder(TextureDescriptor& T)
{
    T.Placeholder = true;
    T.Encoding    = T.Linear ? TextureEncoding::Linear8 : TextureEncoding::Srgb8;
    T.Width = T.Height = 1u;
    T.Texels.assign(T.Linear ? std::initializer_list<uint8_t>{ 128u, 128u, 255u, 255u } : std::initializer_list<uint8_t>{ 255u, 255u, 255u, 255u });
    T.LevelOffsets = { 0u };
    T.LevelCount   = 1u;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

uint32_t TextureIndex::RegisterPath(const std::string& Path, bool Linear) noexcept
{
    for (uint32_t I = 0u; I < Textures.size(); ++I)
        if (!Textures[I].Path.empty() && Textures[I].Path == Path && Textures[I].Linear == Linear) return I;
    TextureDescriptor T;
    T.Name = Path; T.Path = Path; T.Linear = Linear;
    T.Encoding = Linear ? TextureEncoding::Linear8 : TextureEncoding::Srgb8;
    Textures.push_back(std::move(T));
    Sources.emplace_back();
    return static_cast<uint32_t>(Textures.size() - 1u);
}

uint32_t TextureIndex::RegisterEncoded(const std::string& Name, const uint8_t* Bytes, size_t ByteCount, bool Linear) noexcept
{
    for (uint32_t I = 0u; I < Textures.size(); ++I)
        if (Textures[I].Path.empty() && Textures[I].Name == Name && Textures[I].Linear == Linear && Sources[I].Encoded.size() == ByteCount
            && std::memcmp(Sources[I].Encoded.data(), Bytes, ByteCount) == 0) return I;
    TextureDescriptor T;
    T.Name = Name; T.Linear = Linear;
    T.Encoding = Linear ? TextureEncoding::Linear8 : TextureEncoding::Srgb8;
    Textures.push_back(std::move(T));
    Pending P; P.Encoded.assign(Bytes, Bytes + ByteCount);
    Sources.push_back(std::move(P));
    return static_cast<uint32_t>(Textures.size() - 1u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          DECODE
//------------------------------------------------------------------------------------------------------------------------

void TextureIndex::ConstructLevels(TextureDescriptor& T) noexcept
{
    const uint32_t Bytes = T.TexelBytes();
    std::vector<uint8_t> Chain;
    std::vector<uint32_t> Offsets;
    uint32_t W = T.Width, H = T.Height;
    Chain.insert(Chain.end(), T.Texels.begin(), T.Texels.begin() + static_cast<ptrdiff_t>(W * H * Bytes));
    Offsets.push_back(0u);
    std::vector<uint8_t> Current(T.Texels.begin(), T.Texels.begin() + static_cast<ptrdiff_t>(W * H * Bytes));
    while (W > 1u || H > 1u)
    {
        const uint32_t DW = std::max(1u, W / 2u), DH = std::max(1u, H / 2u);
        std::vector<uint8_t> Next(static_cast<size_t>(DW) * DH * Bytes);
        Downsample(Current.data(), W, H, Next.data(), DW, DH, T.Encoding);
        Offsets.push_back(static_cast<uint32_t>(Chain.size()));
        Chain.insert(Chain.end(), Next.begin(), Next.end());
        Current.swap(Next);
        W = DW; H = DH;
    }
    T.Texels       = std::move(Chain);
    T.LevelOffsets = std::move(Offsets);
    T.LevelCount   = static_cast<uint32_t>(T.LevelOffsets.size());
}

uint32_t TextureIndex::Decode(uint32_t MaximumEdge, std::vector<std::string>* Report) noexcept
{
    const auto Start = std::chrono::steady_clock::now();
    uint32_t Failures = 0u;
    for (uint32_t I = 0u; I < Textures.size(); ++I)
    {
        Pending& P = Sources[I];
        if (P.Decoded) continue;
        P.Decoded = true;
        TextureDescriptor& T = Textures[I];

        int W = 0, H = 0, Channels = 0;
        const bool Hdr = T.Path.empty() ? stbi_is_hdr_from_memory(P.Encoded.data(), static_cast<int>(P.Encoded.size())) != 0
                                        : stbi_is_hdr(T.Path.c_str()) != 0;
        void* Pixels = nullptr;
        if (Hdr)
            Pixels = T.Path.empty() ? static_cast<void*>(stbi_loadf_from_memory(P.Encoded.data(), static_cast<int>(P.Encoded.size()), &W, &H, &Channels, 4))
                                    : static_cast<void*>(stbi_loadf(T.Path.c_str(), &W, &H, &Channels, 4));
        else
            Pixels = T.Path.empty() ? static_cast<void*>(stbi_load_from_memory(P.Encoded.data(), static_cast<int>(P.Encoded.size()), &W, &H, &Channels, 4))
                                    : static_cast<void*>(stbi_load(T.Path.c_str(), &W, &H, &Channels, 4));
        if (!Pixels || W <= 0 || H <= 0)
        {
            if (Report) Report->push_back("texture '" + T.Name + "': " + (stbi_failure_reason() ? stbi_failure_reason() : "decode failed") + " -> 1x1 placeholder");
            MakePlaceholder(T);
            ++Failures;
            continue;
        }
        T.Width = static_cast<uint32_t>(W); T.Height = static_cast<uint32_t>(H);
        if (Hdr)
        {
            T.Encoding = TextureEncoding::LinearHalf;
            T.Texels.resize(static_cast<size_t>(W) * H * 8u);
            const float* F = static_cast<const float*>(Pixels);
            uint16_t* Out = reinterpret_cast<uint16_t*>(T.Texels.data());
            for (size_t K = 0; K < static_cast<size_t>(W) * H * 4u; ++K) Out[K] = FloatToHalf(F[K]);
        }
        else
        {
            T.Encoding = T.Linear ? TextureEncoding::Linear8 : TextureEncoding::Srgb8;
            T.Texels.assign(static_cast<const uint8_t*>(Pixels), static_cast<const uint8_t*>(Pixels) + static_cast<size_t>(W) * H * 4u);
        }
        stbi_image_free(Pixels);
        P.Encoded.clear(); P.Encoded.shrink_to_fit();

        ConstructLevels(T);

        // Budget: drop level 0.. until the largest edge fits.
        if (MaximumEdge)
        {
            uint32_t Drop = 0u;
            uint32_t E = std::max(T.Width, T.Height);
            while (E > MaximumEdge && Drop + 1u < T.LevelCount) { E = std::max(1u, E / 2u); ++Drop; }
            if (Drop)
            {
                const uint32_t Offset = T.LevelOffsets[Drop];
                T.Texels.erase(T.Texels.begin(), T.Texels.begin() + Offset);
                T.LevelOffsets.erase(T.LevelOffsets.begin(), T.LevelOffsets.begin() + Drop);
                for (uint32_t& O : T.LevelOffsets) O -= Offset;
                T.Width = std::max(1u, T.Width >> Drop); T.Height = std::max(1u, T.Height >> Drop);
                T.LevelCount = static_cast<uint32_t>(T.LevelOffsets.size());
            }
        }
    }

    Metrics = TextureIndexMetrics{};
    Metrics.Count = static_cast<uint32_t>(Textures.size());
    for (const TextureDescriptor& T : Textures) { Metrics.ByteCount += T.Texels.size(); Metrics.Placeholders += T.Placeholder ? 1u : 0u; }
    Metrics.DecodeMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    if (Report)
    {
        char Line[192];
        std::snprintf(Line, sizeof(Line), "Textures: %u resident (%u placeholder), %.1f MB with mips, decoded in %.0f ms",
                      Metrics.Count, Metrics.Placeholders, Metrics.ByteCount / (1024.0 * 1024.0), Metrics.DecodeMilliseconds);
        Report->emplace_back(Line);
    }
    return Failures;
}

void TextureIndex::Clear() noexcept
{
    Textures.clear(); Sources.clear(); Metrics = TextureIndexMetrics{};
}

} // namespace Frontier
