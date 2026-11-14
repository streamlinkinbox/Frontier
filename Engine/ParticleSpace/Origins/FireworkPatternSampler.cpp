/*============================================================================================================================================
                                                      FIREWORKPATTERNSAMPLER.CPP
============================================================================================================================================*/
// 🧩 Implementation of all 10 FireworkShape pyrotechnic pattern samplers including 5x7 UTF-8 SkyText font

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/FireworkPatternSampler.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

[[nodiscard]] float FireworkHash(int32_t N, int32_t K) noexcept
{
    const double Raw = std::sin(static_cast<double>(N) * 12.9898 + static_cast<double>(K) * 78.233) * 43758.5453;
    return static_cast<float>(Raw - std::floor(Raw));
}

struct RgbTriplet { float R, G, B; };

constexpr std::array<std::array<RgbTriplet, 3u>, 6u> FireworkTintSets = {{
    {{ { 1.00f, 0.86f, 0.34f }, { 1.00f, 0.24f, 0.34f }, { 1.00f, 0.96f, 0.78f } }},
    {{ { 0.24f, 0.66f, 1.00f }, { 0.58f, 0.96f, 1.00f }, { 1.00f, 0.88f, 0.48f } }},
    {{ { 1.00f, 0.22f, 0.76f }, { 0.68f, 0.32f, 1.00f }, { 1.00f, 0.92f, 0.42f } }},
    {{ { 0.18f, 0.98f, 0.56f }, { 1.00f, 0.84f, 0.30f }, { 0.76f, 1.00f, 0.86f } }},
    {{ { 1.00f, 0.36f, 0.14f }, { 1.00f, 0.74f, 0.22f }, { 1.00f, 0.98f, 0.92f } }},
    {{ { 0.14f, 0.94f, 0.88f }, { 1.00f, 0.46f, 0.62f }, { 1.00f, 0.95f, 0.65f } }},
}};

constexpr std::array<FireworkShapeCategory, 8u> GrandFinaleSequence = {{
    FireworkShapeCategory::SkyText,
    FireworkShapeCategory::Heart,
    FireworkShapeCategory::Saturn,
    FireworkShapeCategory::Star,
    FireworkShapeCategory::Smiley,
    FireworkShapeCategory::Crown,
    FireworkShapeCategory::Butterfly,
    FireworkShapeCategory::Bouquet,
}};

struct BitmapCharacter5x7
{
    uint32_t                   CodePoint;
    std::array<const char*, 7u> Rows;
};

constexpr std::array<BitmapCharacter5x7, 47u> BitmapFont5x7 = {{
    { 'A', {{ "01110", "10001", "10001", "11111", "10001", "10001", "10001" }} },
    { 'B', {{ "11110", "10001", "10001", "11110", "10001", "10001", "11110" }} },
    { 'C', {{ "01111", "10000", "10000", "10000", "10000", "10000", "01111" }} },
    { 'D', {{ "11110", "10001", "10001", "10001", "10001", "10001", "11110" }} },
    { 'E', {{ "11111", "10000", "10000", "11110", "10000", "10000", "11111" }} },
    { 'F', {{ "11111", "10000", "10000", "11110", "10000", "10000", "10000" }} },
    { 'G', {{ "01111", "10000", "10000", "10111", "10001", "10001", "01110" }} },
    { 'H', {{ "10001", "10001", "10001", "11111", "10001", "10001", "10001" }} },
    { 'I', {{ "11111", "00100", "00100", "00100", "00100", "00100", "11111" }} },
    { 'J', {{ "00111", "00010", "00010", "00010", "10010", "10010", "01100" }} },
    { 'K', {{ "10001", "10010", "10100", "11000", "10100", "10010", "10001" }} },
    { 'L', {{ "10000", "10000", "10000", "10000", "10000", "10000", "11111" }} },
    { 'M', {{ "10001", "11011", "10101", "10101", "10001", "10001", "10001" }} },
    { 'N', {{ "10001", "11001", "10101", "10011", "10001", "10001", "10001" }} },
    { 'O', {{ "01110", "10001", "10001", "10001", "10001", "10001", "01110" }} },
    { 'P', {{ "11110", "10001", "10001", "11110", "10000", "10000", "10000" }} },
    { 'Q', {{ "01110", "10001", "10001", "10001", "10101", "10010", "01101" }} },
    { 'R', {{ "11110", "10001", "10001", "11110", "10100", "10010", "10001" }} },
    { 'S', {{ "01111", "10000", "10000", "01110", "00001", "00001", "11110" }} },
    { 'T', {{ "11111", "00100", "00100", "00100", "00100", "00100", "00100" }} },
    { 'U', {{ "10001", "10001", "10001", "10001", "10001", "10001", "01110" }} },
    { 'V', {{ "10001", "10001", "10001", "10001", "10001", "01010", "00100" }} },
    { 'W', {{ "10001", "10001", "10001", "10101", "10101", "11011", "10001" }} },
    { 'X', {{ "10001", "10001", "01010", "00100", "01010", "10001", "10001" }} },
    { 'Y', {{ "10001", "10001", "01010", "00100", "00100", "00100", "00100" }} },
    { 'Z', {{ "11111", "00001", "00010", "00100", "01000", "10000", "11111" }} },
    { '0', {{ "01110", "10011", "10101", "10101", "11001", "10001", "01110" }} },
    { '1', {{ "00100", "01100", "00100", "00100", "00100", "00100", "01110" }} },
    { '2', {{ "01110", "10001", "00001", "00110", "01000", "10000", "11111" }} },
    { '3', {{ "11110", "00001", "00001", "01110", "00001", "00001", "11110" }} },
    { '4', {{ "00010", "00110", "01010", "10010", "11111", "00010", "00010" }} },
    { '5', {{ "11111", "10000", "11110", "00001", "00001", "10001", "01110" }} },
    { '6', {{ "01110", "10000", "11110", "10001", "10001", "10001", "01110" }} },
    { '7', {{ "11111", "00001", "00010", "00100", "01000", "01000", "01000" }} },
    { '8', {{ "01110", "10001", "10001", "01110", "10001", "10001", "01110" }} },
    { '9', {{ "01110", "10001", "10001", "01111", "00001", "00010", "01100" }} },
    { '!', {{ "00100", "00100", "00100", "00100", "00100", "00000", "00100" }} },
    { '?', {{ "01110", "10001", "00010", "00100", "00100", "00000", "00100" }} },
    { '&', {{ "01100", "10010", "10100", "01000", "10101", "10010", "01101" }} },
    { '+', {{ "00000", "00100", "00100", "11111", "00100", "00100", "00000" }} },
    { '-', {{ "00000", "00000", "00000", "11111", "00000", "00000", "00000" }} },
    { '.', {{ "00000", "00000", "00000", "00000", "00000", "01100", "01100" }} },
    { ':', {{ "00000", "00100", "00100", "00000", "00100", "00100", "00000" }} },
    { '*', {{ "00100", "00100", "11111", "01110", "01010", "10001", "00000" }} },
    { 0x2605u, {{ "00100", "00100", "11111", "01110", "01010", "10001", "00000" }} },
    { 0x2665u, {{ "01010", "11111", "11111", "11111", "01110", "00100", "00000" }} },
    { ' ', {{ "00000", "00000", "00000", "00000", "00000", "00000", "00000" }} },
}};

[[nodiscard]] const BitmapCharacter5x7& FindFontBitmap(uint32_t CodePoint) noexcept
{
    for (const auto& Entry : BitmapFont5x7)
    {
        if (Entry.CodePoint == CodePoint)
        {
            return Entry;
        }
    }
    return BitmapFont5x7[44u]; // Star fallback
}

[[nodiscard]] std::vector<uint32_t> DecodeUtf8Upper(std::string_view Raw)
{
    std::vector<uint32_t> Out {};
    std::size_t I = 0u;
    while (I < Raw.size() && Out.size() < 28u)
    {
        const auto Byte = static_cast<unsigned char>(Raw[I]);
        if (Byte < 0x80u)
        {
            Out.push_back(static_cast<uint32_t>(std::toupper(Byte)));
            ++I;
        }
        else if ((Byte & 0xE0u) == 0xC0u && I + 1u < Raw.size())
        {
            const uint32_t Cp = ((Byte & 0x1Fu) << 6u) | (static_cast<unsigned char>(Raw[I + 1u]) & 0x3Fu);
            Out.push_back(Cp);
            I += 2u;
        }
        else if ((Byte & 0xF0u) == 0xE0u && I + 2u < Raw.size())
        {
            const uint32_t Cp = ((Byte & 0x0Fu) << 12u) |
                                ((static_cast<unsigned char>(Raw[I + 1u]) & 0x3Fu) << 6u) |
                                (static_cast<unsigned char>(Raw[I + 2u]) & 0x3Fu);
            Out.push_back(Cp);
            I += 3u;
        }
        else
        {
            ++I;
        }
    }
    return Out;
}

void SampleSkyTextPoints(
    std::string_view               RawText,
    uint32_t                       Count,
    int32_t                        ShellIdx,
    std::vector<FireworkStarPoint>& OutPoints)
{
    std::vector<uint32_t> CodePoints = DecodeUtf8Upper(RawText);
    if (CodePoints.empty())
    {
        CodePoints = { '2', '0', '2', '6' };
    }

    // Wrap into at most 3 lines of <= 9 characters
    std::vector<std::vector<uint32_t>> Lines {};
    if (CodePoints.size() <= 8u)
    {
        Lines.push_back(std::move(CodePoints));
    }
    else
    {
        std::vector<uint32_t> Current {};
        for (uint32_t Cp : CodePoints)
        {
            if (Cp == ' ' && Current.size() >= 5u && Lines.size() < 2u)
            {
                Lines.push_back(std::move(Current));
                Current.clear();
            }
            else if (Current.size() >= 9u && Lines.size() < 2u)
            {
                Lines.push_back(std::move(Current));
                Current.clear();
                Current.push_back(Cp);
            }
            else
            {
                Current.push_back(Cp);
            }
        }
        if (!Current.empty())
        {
            Lines.push_back(std::move(Current));
        }
    }

    struct Dot { float Px, Py; std::size_t Ci; };
    std::vector<Dot> Dots {};
    const float LineGap = 9.0f;
    const float TotalH  = static_cast<float>(Lines.size() * 7u + (Lines.size() - 1u) * 2u);
    std::size_t MaxW    = 1u;
    for (const auto& Line : Lines)
    {
        MaxW = std::max<std::size_t>(MaxW, Line.size() * 6u - 1u);
    }

    for (std::size_t Li = 0u; Li < Lines.size(); ++Li)
    {
        const auto& Line = Lines[Li];
        const float LW   = static_cast<float>(Line.size() * 6u - 1u);
        const float YOff = TotalH * 0.5f - static_cast<float>(Li) * LineGap;
        for (std::size_t Ci = 0u; Ci < Line.size(); ++Ci)
        {
            const auto& Bitmap = FindFontBitmap(Line[Ci]);
            const float XOff   = -LW * 0.5f + static_cast<float>(Ci) * 6.0f;
            for (std::size_t Row = 0u; Row < 7u; ++Row)
            {
                for (std::size_t Col = 0u; Col < 5u; ++Col)
                {
                    if (Bitmap.Rows[Row][Col] == '1')
                    {
                        Dots.push_back({ XOff + static_cast<float>(Col) + 0.5f, YOff - static_cast<float>(Row) - 0.5f, Ci });
                    }
                }
            }
        }
    }
    if (Dots.empty())
    {
        Dots.push_back({ 0.0f, 0.0f, 0u });
    }

    const float NormScale = 2.0f / std::max({ static_cast<float>(MaxW), TotalH * 1.15f, 11.0f });
    const auto& TintSet   = FireworkTintSets[static_cast<std::size_t>(std::abs(ShellIdx)) % FireworkTintSets.size()];
    const uint32_t TextN  = static_cast<uint32_t>(static_cast<float>(Count) * 0.82f);

    OutPoints.reserve(Count);
    for (uint32_t I = 0u; I < Count; ++I)
    {
        if (I < TextN)
        {
            const Dot& D    = Dots[I % Dots.size()];
            const int32_t Seed = static_cast<int32_t>(I) * 17 + ShellIdx * 31;
            const float Jx  = (FireworkHash(Seed, 1) - 0.5f) * 0.42f;
            const float Jy  = (FireworkHash(Seed, 2) - 0.5f) * 0.42f;
            const float Jz  = (FireworkHash(Seed, 3) - 0.5f) * 0.14f;
            const auto& Col = ((I % 5u) == 0u) ? TintSet[2] : (((D.Ci % 2u) == 0u) ? TintSet[0] : TintSet[1]);
            OutPoints.push_back({ (D.Px + Jx) * NormScale, (D.Py + Jy) * NormScale, Jz, Col.R, Col.G, Col.B, 1u, 0.015f * FireworkHash(static_cast<int32_t>(I), 4) });
        }
        else
        {
            const float U   = static_cast<float>(I - TextN) / static_cast<float>(std::max(1u, Count - TextN));
            const float Ang = U * Constants::TauFloat + static_cast<float>(ShellIdx) * 0.4f;
            const float Rad = 1.12f + 0.16f * FireworkHash(static_cast<int32_t>(I), 5);
            OutPoints.push_back({ std::cos(Ang) * Rad * 1.18f, std::sin(Ang) * Rad * 0.72f, (FireworkHash(static_cast<int32_t>(I), 6) - 0.5f) * 0.45f, TintSet[2].R, TintSet[2].G, TintSet[2].B, 2u, 0.04f + 0.06f * FireworkHash(static_cast<int32_t>(I), 7) });
        }
    }
}

void SampleShapePoints(
    FireworkShapeCategory           Shape,
    uint32_t                        Count,
    int32_t                         ShellIdx,
    std::vector<FireworkStarPoint>& OutPoints,
    bool&                           OutWillow,
    bool&                           OutStrobe)
{
    const auto& TintSet = FireworkTintSets[static_cast<std::size_t>(std::abs(ShellIdx)) % FireworkTintSets.size()];
    OutPoints.reserve(Count);
    OutWillow = false;
    OutStrobe = true;

    if (Shape == FireworkShapeCategory::Bouquet)
    {
        const int32_t Style  = std::abs(ShellIdx) % 4;
        OutWillow            = (Style == 1 || Style == 2);
        OutStrobe            = (Style == 0 || Style == 3);
        const uint32_t OuterN = static_cast<uint32_t>(static_cast<float>(Count) * 0.68f);
        const uint32_t CoreN  = static_cast<uint32_t>(static_cast<float>(Count) * 0.24f);
        for (uint32_t I = 0u; I < Count; ++I)
        {
            if (I < OuterN)
            {
                if (Style == 2)
                {
                    const uint32_t Frond = I % 14u;
                    const float    FPhi  = std::acos(1.0f - (2.0f * (static_cast<float>(Frond) + 0.5f)) / 14.0f);
                    const float    FTh   = static_cast<float>(Frond) * 2.3999632f + static_cast<float>(ShellIdx);
                    const float    Frac  = (static_cast<float>(I / 14u) + 0.5f) / static_cast<float>(std::max(1u, OuterN / 14u));
                    const float    RadP  = 0.25f + 0.85f * Frac;
                    OutPoints.push_back({
                        std::sin(FPhi) * std::cos(FTh) * RadP + (FireworkHash(static_cast<int32_t>(I), 2) - 0.5f) * 0.09f,
                        std::cos(FPhi) * RadP + (FireworkHash(static_cast<int32_t>(I), 3) - 0.5f) * 0.09f,
                        std::sin(FPhi) * std::sin(FTh) * RadP + (FireworkHash(static_cast<int32_t>(I), 4) - 0.5f) * 0.09f,
                        1.0f, 0.82f - 0.18f * Frac, 0.28f + 0.25f * (1.0f - Frac), 1u, 0.02f * Frac,
                    });
                    continue;
                }
                if (Style == 3)
                {
                    const uint32_t Clust = I % 24u;
                    const float    CPhi  = std::acos(1.0f - (2.0f * (static_cast<float>(Clust) + 0.5f)) / 24.0f);
                    const float    CTh   = static_cast<float>(Clust) * 2.3999632f;
                    const float    Cx    = std::sin(CPhi) * std::cos(CTh) * 0.92f;
                    const float    Cy    = std::cos(CPhi) * 0.92f;
                    const float    Cz    = std::sin(CPhi) * std::sin(CTh) * 0.92f;
                    const auto&    Col   = ((Clust % 2u) == 0u) ? TintSet[0] : TintSet[1];
                    OutPoints.push_back({
                        Cx + (FireworkHash(static_cast<int32_t>(I), 5) - 0.5f) * 0.28f,
                        Cy + (FireworkHash(static_cast<int32_t>(I), 6) - 0.5f) * 0.28f,
                        Cz + (FireworkHash(static_cast<int32_t>(I), 7) - 0.5f) * 0.28f,
                        Col.R, Col.G, Col.B, 1u, 0.04f * FireworkHash(static_cast<int32_t>(I), 8),
                    });
                    continue;
                }
                const float K     = static_cast<float>(I) + 0.5f;
                const float Phi   = std::acos(1.0f - (2.0f * K) / static_cast<float>(std::max(1u, OuterN)));
                const float Theta = Constants::PiFloat * (1.0f + std::sqrt(5.0f)) * K + static_cast<float>(ShellIdx) * 1.3f;
                const float R     = 0.92f + 0.16f * FireworkHash(static_cast<int32_t>(I) + ShellIdx * 13, 1);
                const RgbTriplet Col = (Style == 1) ? RgbTriplet{ 1.0f, 0.84f, 0.32f } : (((I % 3u) == 0u) ? TintSet[2] : TintSet[0]);
                OutPoints.push_back({ std::sin(Phi) * std::cos(Theta) * R, std::cos(Phi) * R, std::sin(Phi) * std::sin(Theta) * R, Col.R, Col.G, Col.B, 1u, 0.015f * FireworkHash(static_cast<int32_t>(I), 2) });
            }
            else if (I < OuterN + CoreN)
            {
                const float J     = static_cast<float>(I - OuterN) + 0.5f;
                const float Phi   = std::acos(1.0f - (2.0f * J) / static_cast<float>(std::max(1u, CoreN)));
                const float Theta = Constants::PiFloat * (1.0f + std::sqrt(5.0f)) * J;
                const float R     = 0.44f + 0.08f * FireworkHash(static_cast<int32_t>(I), 3);
                OutPoints.push_back({ std::sin(Phi) * std::cos(Theta) * R, std::cos(Phi) * R, std::sin(Phi) * std::sin(Theta) * R, TintSet[1].R, TintSet[1].G, TintSet[1].B, 2u, 0.01f });
            }
            else
            {
                const float U   = static_cast<float>(I - OuterN - CoreN) / static_cast<float>(std::max(1u, Count - OuterN - CoreN));
                const float Ang = U * Constants::TauFloat;
                const float R   = 1.06f + 0.08f * FireworkHash(static_cast<int32_t>(I), 4);
                OutPoints.push_back({ std::cos(Ang) * R, (FireworkHash(static_cast<int32_t>(I), 5) - 0.5f) * 0.14f, std::sin(Ang) * R, TintSet[2].R, TintSet[2].G, TintSet[2].B, 2u, 0.03f });
            }
        }
        return;
    }

    if (Shape == FireworkShapeCategory::Heart)
    {
        const uint32_t RimN = static_cast<uint32_t>(static_cast<float>(Count) * 0.72f);
        for (uint32_t I = 0u; I < Count; ++I)
        {
            if (I < RimN)
            {
                const float T     = (static_cast<float>(I) / static_cast<float>(std::max(1u, RimN))) * Constants::TauFloat;
                const float Layer = ((I % 3u) == 0u) ? 0.76f : 1.0f;
                const float Hx    = 16.0f * std::pow(std::sin(T), 3.0f) / 16.5f;
                const float Hy    = (13.0f * std::cos(T) - 5.0f * std::cos(2.0f * T) - 2.0f * std::cos(3.0f * T) - std::cos(4.0f * T)) / 16.5f + 0.12f;
                const RgbTriplet Col = (Layer < 0.9f) ? RgbTriplet{ 1.0f, 0.82f, 0.42f } : RgbTriplet{ 1.0f, 0.18f + 0.14f * std::sin(T * 3.0f), 0.44f };
                OutPoints.push_back({ Hx * Layer + (FireworkHash(static_cast<int32_t>(I), 1) - 0.5f) * 0.05f, Hy * Layer + (FireworkHash(static_cast<int32_t>(I), 2) - 0.5f) * 0.05f, (FireworkHash(static_cast<int32_t>(I), 3) - 0.5f) * 0.18f, Col.R, Col.G, Col.B, 1u, 0.015f * FireworkHash(static_cast<int32_t>(I), 4) });
            }
            else
            {
                const float T  = FireworkHash(static_cast<int32_t>(I), 5) * Constants::TauFloat;
                const float Sc = 0.25f + 0.55f * std::sqrt(FireworkHash(static_cast<int32_t>(I), 6));
                const float Hx = 16.0f * std::pow(std::sin(T), 3.0f) / 16.5f;
                const float Hy = (13.0f * std::cos(T) - 5.0f * std::cos(2.0f * T) - 2.0f * std::cos(3.0f * T) - std::cos(4.0f * T)) / 16.5f + 0.12f;
                OutPoints.push_back({ Hx * Sc, Hy * Sc, (FireworkHash(static_cast<int32_t>(I), 7) - 0.5f) * 0.25f, 1.0f, 0.92f, 0.72f, 2u, 0.03f * FireworkHash(static_cast<int32_t>(I), 8) });
            }
        }
        return;
    }

    if (Shape == FireworkShapeCategory::Star)
    {
        std::array<std::pair<float, float>, 10u> Verts {};
        for (uint32_t K = 0u; K < 10u; ++K)
        {
            const float Ang = Constants::PiFloat * 0.5f + (static_cast<float>(K) / 10.0f) * Constants::TauFloat;
            const float R   = ((K % 2u) == 0u) ? 1.05f : 0.44f;
            Verts[K]        = { std::cos(Ang) * R, std::sin(Ang) * R };
        }
        const uint32_t RimN = static_cast<uint32_t>(static_cast<float>(Count) * 0.76f);
        for (uint32_t I = 0u; I < Count; ++I)
        {
            if (I < RimN)
            {
                const uint32_t Seg   = I % 10u;
                const float    U     = FireworkHash(static_cast<int32_t>(I), 1);
                const auto&    A     = Verts[Seg];
                const auto&    B     = Verts[(Seg + 1u) % 10u];
                const float    Layer = ((I % 4u) == 0u) ? 0.58f : 1.0f;
                const RgbTriplet Col = (Layer < 0.8f) ? RgbTriplet{ 0.36f, 0.92f, 1.0f } : RgbTriplet{ 1.0f, 0.88f, 0.30f };
                OutPoints.push_back({ (A.first + (B.first - A.first) * U) * Layer + (FireworkHash(static_cast<int32_t>(I), 2) - 0.5f) * 0.04f, (A.second + (B.second - A.second) * U) * Layer + (FireworkHash(static_cast<int32_t>(I), 3) - 0.5f) * 0.04f, (FireworkHash(static_cast<int32_t>(I), 4) - 0.5f) * 0.16f, Col.R, Col.G, Col.B, 1u, 0.01f });
            }
            else
            {
                const float Ang = FireworkHash(static_cast<int32_t>(I), 5) * Constants::TauFloat;
                const float R   = 1.15f + 0.14f * FireworkHash(static_cast<int32_t>(I), 6);
                OutPoints.push_back({ std::cos(Ang) * R, std::sin(Ang) * R, (FireworkHash(static_cast<int32_t>(I), 7) - 0.5f) * 0.32f, 1.0f, 0.98f, 0.82f, 2u, 0.04f });
            }
        }
        return;
    }

    if (Shape == FireworkShapeCategory::Smiley)
    {
        const uint32_t FaceN = static_cast<uint32_t>(static_cast<float>(Count) * 0.52f);
        const uint32_t EyeN  = static_cast<uint32_t>(static_cast<float>(Count) * 0.18f);
        for (uint32_t I = 0u; I < Count; ++I)
        {
            if (I < FaceN)
            {
                const float T = (static_cast<float>(I) / static_cast<float>(std::max(1u, FaceN))) * Constants::TauFloat;
                const float R = 0.96f + (FireworkHash(static_cast<int32_t>(I), 1) - 0.5f) * 0.07f;
                OutPoints.push_back({ std::cos(T) * R, std::sin(T) * R, (FireworkHash(static_cast<int32_t>(I), 2) - 0.5f) * 0.12f, 1.0f, 0.88f, 0.22f, 1u, 0.01f });
            }
            else if (I < FaceN + EyeN)
            {
                const float Side = ((I % 2u) == 0u) ? -1.0f : 1.0f;
                const float A    = FireworkHash(static_cast<int32_t>(I), 3) * Constants::TauFloat;
                const float R    = std::sqrt(FireworkHash(static_cast<int32_t>(I), 4)) * 0.14f;
                OutPoints.push_back({ Side * 0.35f + std::cos(A) * R, 0.30f + std::sin(A) * R, (FireworkHash(static_cast<int32_t>(I), 5) - 0.5f) * 0.10f, 0.42f, 0.95f, 1.0f, 1u, 0.01f });
            }
            else
            {
                const float U   = static_cast<float>(I - FaceN - EyeN) / static_cast<float>(std::max(1u, Count - FaceN - EyeN));
                const float Ang = -Constants::PiFloat * 0.84f + U * (Constants::PiFloat * 0.68f);
                const float R   = 0.55f + (FireworkHash(static_cast<int32_t>(I), 6) - 0.5f) * 0.06f;
                OutPoints.push_back({ std::cos(Ang) * R, std::sin(Ang) * R - 0.02f, (FireworkHash(static_cast<int32_t>(I), 7) - 0.5f) * 0.10f, 1.0f, 0.34f, 0.42f, 1u, 0.01f });
            }
        }
        return;
    }

    if (Shape == FireworkShapeCategory::Saturn)
    {
        const uint32_t CoreN = static_cast<uint32_t>(static_cast<float>(Count) * 0.46f);
        constexpr float Tilt = 0.42f;
        const float Cs = std::cos(Tilt);
        const float Sn = std::sin(Tilt);
        for (uint32_t I = 0u; I < Count; ++I)
        {
            if (I < CoreN)
            {
                const float K     = static_cast<float>(I) + 0.5f;
                const float Phi   = std::acos(1.0f - (2.0f * K) / static_cast<float>(std::max(1u, CoreN)));
                const float Theta = Constants::PiFloat * (1.0f + std::sqrt(5.0f)) * K;
                const float R     = 0.50f + 0.05f * FireworkHash(static_cast<int32_t>(I), 1);
                OutPoints.push_back({ std::sin(Phi) * std::cos(Theta) * R, std::cos(Phi) * R, std::sin(Phi) * std::sin(Theta) * R, 1.0f, 0.72f + 0.18f * std::cos(Phi), 0.22f, 1u, 0.01f });
            }
            else
            {
                const uint32_t J    = I - CoreN;
                const float    Ang  = (static_cast<float>(J) / static_cast<float>(std::max(1u, Count - CoreN))) * Constants::TauFloat;
                const float    Band = ((J % 3u) == 0u) ? 1.18f : (0.86f + 0.22f * FireworkHash(static_cast<int32_t>(I), 2));
                const float    Rx   = std::cos(Ang) * Band;
                const float    Rz   = std::sin(Ang) * Band;
                const float    Ry   = (FireworkHash(static_cast<int32_t>(I), 3) - 0.5f) * 0.04f;
                const RgbTriplet Col = ((J % 3u) == 0u) ? RgbTriplet{ 0.95f, 0.98f, 1.0f } : RgbTriplet{ 0.24f, 0.88f, 1.0f };
                OutPoints.push_back({ Rx * Cs - Ry * Sn, Rx * Sn + Ry * Cs + Rz * 0.22f, Rz * 0.92f, Col.R, Col.G, Col.B, 1u, 0.02f });
            }
        }
        return;
    }

    if (Shape == FireworkShapeCategory::Crown)
    {
        static constexpr std::array<std::pair<float, float>, 8u> Poly = {{
            { -0.86f, -0.52f }, { 0.86f, -0.52f }, { 0.96f, 0.44f }, { 0.46f, -0.02f },
            {  0.00f,  0.78f }, { -0.46f, -0.02f }, { -0.96f, 0.44f }, { -0.86f, -0.52f },
        }};
        static constexpr std::array<std::pair<float, float>, 3u> Jewels = {{
            { 0.0f, 0.86f }, { -0.98f, 0.52f }, { 0.98f, 0.52f },
        }};
        const uint32_t RimN = static_cast<uint32_t>(static_cast<float>(Count) * 0.80f);
        for (uint32_t I = 0u; I < Count; ++I)
        {
            if (I < RimN)
            {
                const uint32_t Seg = I % 7u;
                const float    U   = FireworkHash(static_cast<int32_t>(I), 1);
                OutPoints.push_back({
                    Poly[Seg].first + (Poly[Seg + 1u].first - Poly[Seg].first) * U + (FireworkHash(static_cast<int32_t>(I), 2) - 0.5f) * 0.05f,
                    Poly[Seg].second + (Poly[Seg + 1u].second - Poly[Seg].second) * U + (FireworkHash(static_cast<int32_t>(I), 3) - 0.5f) * 0.05f,
                    (FireworkHash(static_cast<int32_t>(I), 4) - 0.5f) * 0.16f,
                    1.0f, 0.84f, 0.24f, 1u, 0.01f,
                });
            }
            else
            {
                const auto& J = Jewels[I % 3u];
                const float A = FireworkHash(static_cast<int32_t>(I), 5) * Constants::TauFloat;
                const float R = std::sqrt(FireworkHash(static_cast<int32_t>(I), 6)) * 0.14f;
                const RgbTriplet Col = ((I % 3u) == 0u) ? RgbTriplet{ 0.42f, 0.94f, 1.0f } : RgbTriplet{ 1.0f, 0.22f, 0.36f };
                OutPoints.push_back({ J.first + std::cos(A) * R, J.second + std::sin(A) * R, (FireworkHash(static_cast<int32_t>(I), 7) - 0.5f) * 0.14f, Col.R, Col.G, Col.B, 2u, 0.02f });
            }
        }
        return;
    }

    if (Shape == FireworkShapeCategory::Butterfly)
    {
        for (uint32_t I = 0u; I < Count; ++I)
        {
            const float T     = (static_cast<float>(I) / static_cast<float>(std::max(1u, Count))) * Constants::TauFloat;
            const float RRaw  = std::exp(std::cos(T)) - 2.0f * std::cos(4.0f * T) + std::pow(std::sin(T / 12.0f), 5.0f);
            const float Scale = ((I % 5u) == 0u) ? 0.65f : 1.0f;
            const float Bx    = std::sin(T) * RRaw * 0.27f * Scale;
            const float By    = std::cos(T) * RRaw * 0.27f * Scale;
            const RgbTriplet Col = (Scale < 0.8f) ? RgbTriplet{ 1.0f, 0.90f, 0.42f } : (std::abs(Bx) > 0.45f ? RgbTriplet{ 0.22f, 0.92f, 1.0f } : RgbTriplet{ 0.88f, 0.32f, 1.0f });
            OutPoints.push_back({ Bx + (FireworkHash(static_cast<int32_t>(I), 1) - 0.5f) * 0.04f, By + (FireworkHash(static_cast<int32_t>(I), 2) - 0.5f) * 0.04f, std::abs(Bx) * 0.24f + (FireworkHash(static_cast<int32_t>(I), 3) - 0.5f) * 0.10f, Col.R, Col.G, Col.B, 1u, 0.015f * FireworkHash(static_cast<int32_t>(I), 4) });
        }
        return;
    }

    // Spiral galaxy
    OutWillow = true;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const uint32_t Arm = I % 2u;
        const float    U   = static_cast<float>(I) / static_cast<float>(std::max(1u, Count));
        const float    Ang = U * Constants::PiFloat * 5.2f + static_cast<float>(Arm) * Constants::PiFloat;
        const float    R   = 0.08f + std::pow(U, 0.78f) * 1.04f;
        const RgbTriplet Col = (U < 0.25f) ? RgbTriplet{ 1.0f, 0.96f, 0.78f } : ((Arm == 0u) ? TintSet[0] : TintSet[1]);
        OutPoints.push_back({ std::cos(Ang) * R + (FireworkHash(static_cast<int32_t>(I), 1) - 0.5f) * 0.06f, std::sin(Ang) * R + (FireworkHash(static_cast<int32_t>(I), 2) - 0.5f) * 0.06f, (FireworkHash(static_cast<int32_t>(I), 3) - 0.5f) * 0.22f, Col.R, Col.G, Col.B, 1u, 0.02f * U });
    }
}

} // namespace

FireworkShellSample SampleFireworkShell(
    FireworkShapeCategory             Shape,
    std::string_view                  SkyText,
    uint32_t                          StarCount,
    int32_t                           ShellIndex,
    Mathematics::WorldCoordinateBasis TargetBasis)
{
    FireworkShellSample Result {};
    const uint32_t SafeCount = std::max(64u, StarCount);

    Result.ResolvedShape = (Shape == FireworkShapeCategory::GrandFinale)
                               ? GrandFinaleSequence[static_cast<std::size_t>(std::abs(ShellIndex)) % GrandFinaleSequence.size()]
                               : Shape;

    if (Result.ResolvedShape == FireworkShapeCategory::SkyText)
    {
        SampleSkyTextPoints(SkyText.empty() ? "2026" : SkyText, SafeCount, ShellIndex, Result.Points);
        Result.ShellCaption  = "Sky Text";
        Result.WillowTrails  = false;
        Result.StrobeTwinkle = true;
    }
    else
    {
        SampleShapePoints(Result.ResolvedShape, SafeCount, ShellIndex, Result.Points, Result.WillowTrails, Result.StrobeTwinkle);
        Result.ShellCaption = std::string(QueryFireworkShapeLabel(Result.ResolvedShape));
    }

    // Rotate points from Y-Up pattern coordinates to TargetBasis (default RightHandedZUp)
    for (auto& Pt : Result.Points)
    {
        const Mathematics::Axis3D Converted = Mathematics::ConvertVectorBetweenBases(
            { Pt.OffsetX, Pt.OffsetY, Pt.OffsetZ },
            Mathematics::WorldCoordinateBasis::RightHandedYUp,
            TargetBasis);
        Pt.OffsetX = Converted.X;
        Pt.OffsetY = Converted.Y;
        Pt.OffsetZ = Converted.Z;
    }

    Result.PackedSnap.resize(SafeCount);
    for (uint32_t I = 0u; I < SafeCount; ++I)
    {
        const auto& Pt   = Result.Points[I % Result.Points.size()];
        auto&       Slot = Result.PackedSnap[I];
        Slot.PositionAge[0]      = Pt.OffsetX;
        Slot.PositionAge[1]      = Pt.OffsetY;
        Slot.PositionAge[2]      = Pt.OffsetZ;
        Slot.PositionAge[3]      = static_cast<float>(Pt.RoleCode);
        Slot.VelocityLifetime[3] = 1.0f;
        Slot.ColourRgba[0]       = Pt.Red;
        Slot.ColourRgba[1]       = Pt.Green;
        Slot.ColourRgba[2]       = Pt.Blue;
        Slot.ColourRgba[3]       = 1.0f;
        Slot.Attributes[0]       = Pt.DelaySec;
        Slot.Attributes[1]       = Result.WillowTrails ? 1.0f : 0.0f;
        Slot.Attributes[2]       = Result.StrobeTwinkle ? 1.0f : 0.0f;
        Slot.Attributes[3]       = static_cast<float>(Result.ResolvedShape);
        Slot.PhaseTiming[0]      = 1.0f;
        Slot.PhaseTiming[3]      = 1.0f;
    }

    return Result;
}

} // namespace Frontier::ParticleSpace
