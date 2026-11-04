/*==========================================================================================================================================
                                             PLANETARY PROJECTION DEFINITION
==========================================================================================================================================*/
// 🧩 Real 6-body 2K sRGB lunar atlas (Luna, Ember, Glacier, Sulfur, Shroud, Shard), 32x32 octahedral
//    Hipparcos star index, Kolmogorov scintillation, Milky Way dust lane, ringed 3D planets, and GPU dispatch.

#include "Engine/PlanetaryAtmosphere/PlanetaryProjection.h"

#include "Engine/PlanetaryAtmosphere/AtmosphereProjection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace Frontier
{
namespace
{

constexpr float π     = 3.14159265358979323846f;
constexpr float Twoπ  = 6.28318530717958647692f;

inline Vec3 Add3(Vec3 A, Vec3 B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
inline Vec3 Sub3(Vec3 A, Vec3 B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
inline Vec3 Mul3(Vec3 A, float Scalar) noexcept { return {A.X * Scalar, A.Y * Scalar, A.Z * Scalar}; }
inline Vec3 Mul3Comp(Vec3 A, Vec3 B) noexcept { return {A.X * B.X, A.Y * B.Y, A.Z * B.Z}; }
inline float Dot3(Vec3 A, Vec3 B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
inline Vec3 Cross3(Vec3 A, Vec3 B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}
inline float Length3(Vec3 Vector) noexcept { return std::sqrt(Dot3(Vector, Vector)); }
inline Vec3 Normalize3(Vec3 Vector) noexcept
{
    const float Len = Length3(Vector);
    return Len > 1e-12f ? Mul3(Vector, 1.0f / Len) : Vec3{0.0f, 0.0f, 1.0f};
}
inline Vec3 Lerp3(Vec3 A, Vec3 B, float Factor) noexcept
{
    return {A.X + (B.X - A.X) * Factor, A.Y + (B.Y - A.Y) * Factor, A.Z + (B.Z - A.Z) * Factor};
}
inline float Smoothstep(float Edge0, float Edge1, float Value) noexcept
{
    const float Span = Edge1 - Edge0;
    if (std::abs(Span) <= 1.0e-12f)
    {
        return Value >= Edge1 ? 1.0f : 0.0f;
    }
    const float Clamped = std::clamp((Value - Edge0) / Span, 0.0f, 1.0f);
    return Clamped * Clamped * (3.0f - 2.0f * Clamped);
}

inline float SrgbByteToLinear(uint8_t Byte) noexcept
{
    const float Encoded = static_cast<float>(Byte) / 255.0f;
    return Encoded <= 0.04045f ? (Encoded / 12.92f) : std::pow((Encoded + 0.055f) / 1.055f, 2.4f);
}

inline uint8_t LinearToSrgbByte(float Linear) noexcept
{
    const float Clamped = std::clamp(Linear, 0.0f, 1.0f);
    const float Encoded = Clamped <= 0.0031308f ? (12.92f * Clamped) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(Encoded * 255.0f), 0l, 255l));
}

//------------------------------------------------------------------------------------------------------------------------
//                           DETERMINISTIC 3D HASH, VALUE FBM & PROCEDURAL PLANETARY HELPERS
//------------------------------------------------------------------------------------------------------------------------

uint32_t HashMix32(uint32_t Word) noexcept
{
    Word ^= Word >> 16u;
    Word *= 0x7FEB352Du;
    Word ^= Word >> 15u;
    Word *= 0x846CA68Bu;
    Word ^= Word >> 16u;
    return Word;
}

float HashUnit3D(int32_t X, int32_t Y, int32_t Z, uint32_t Seed) noexcept
{
    uint32_t Mixed = Seed;
    Mixed ^= HashMix32(static_cast<uint32_t>(X) * 374761393u);
    Mixed ^= HashMix32(static_cast<uint32_t>(Y) * 668265263u);
    Mixed ^= HashMix32(static_cast<uint32_t>(Z) * 2147483647u);
    return static_cast<float>(HashMix32(Mixed) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

float ValueNoise3D(Vec3 Point, uint32_t Seed) noexcept
{
    const int32_t X0 = static_cast<int32_t>(std::floor(Point.X));
    const int32_t Y0 = static_cast<int32_t>(std::floor(Point.Y));
    const int32_t Z0 = static_cast<int32_t>(std::floor(Point.Z));
    const float   Tx = Point.X - static_cast<float>(X0);
    const float   Ty = Point.Y - static_cast<float>(Y0);
    const float   Tz = Point.Z - static_cast<float>(Z0);
    const float   Ux = Tx * Tx * Tx * (Tx * (Tx * 6.0f - 15.0f) + 10.0f);
    const float   Uy = Ty * Ty * Ty * (Ty * (Ty * 6.0f - 15.0f) + 10.0f);
    const float   Uz = Tz * Tz * Tz * (Tz * (Tz * 6.0f - 15.0f) + 10.0f);

    const auto Blend = [](float A, float B, float T) noexcept { return A + (B - A) * T; };
    const float C00 = Blend(HashUnit3D(X0, Y0, Z0, Seed), HashUnit3D(X0 + 1, Y0, Z0, Seed), Ux);
    const float C10 = Blend(HashUnit3D(X0, Y0 + 1, Z0, Seed), HashUnit3D(X0 + 1, Y0 + 1, Z0, Seed), Ux);
    const float C01 = Blend(HashUnit3D(X0, Y0, Z0 + 1, Seed), HashUnit3D(X0 + 1, Y0, Z0 + 1, Seed), Ux);
    const float C11 = Blend(HashUnit3D(X0, Y0 + 1, Z0 + 1, Seed), HashUnit3D(X0 + 1, Y0 + 1, Z0 + 1, Seed), Ux);
    return Blend(Blend(C00, C10, Uy), Blend(C01, C11, Uy), Uz);
}

float Fbm3D(Vec3 Point, uint32_t Octaves, uint32_t Seed) noexcept
{
    float Value     = 0.0f;
    float Amplitude = 0.5f;
    float WeightSum = 0.0f;
    for (uint32_t Octave = 0u; Octave < Octaves; ++Octave)
    {
        Value += Amplitude * ValueNoise3D(Point, Seed + Octave * 977u);
        WeightSum += Amplitude;
        Point     = {Point.X * 2.03f + 7.1f, Point.Y * 2.03f + 3.7f, Point.Z * 2.03f + 11.3f};
        Amplitude *= 0.5f;
    }
    return WeightSum > 0.0f ? (Value / WeightSum) : 0.0f;
}

float RidgedFbm3D(Vec3 Point, uint32_t Octaves, uint32_t Seed) noexcept
{
    float Value     = 0.0f;
    float Amplitude = 0.5f;
    float WeightSum = 0.0f;
    for (uint32_t Octave = 0u; Octave < Octaves; ++Octave)
    {
        const float Sample = 1.0f - std::abs(2.0f * ValueNoise3D(Point, Seed + Octave * 1319u) - 1.0f);
        Value += Amplitude * Sample * Sample;
        WeightSum += Amplitude;
        Point     = {Point.X * 2.11f + 5.3f, Point.Y * 2.11f + 9.2f, Point.Z * 2.11f + 2.7f};
        Amplitude *= 0.5f;
    }
    return WeightSum > 0.0f ? (Value / WeightSum) : 0.0f;
}

bool LoadPpmRgb8(const std::string& Path, uint32_t& OutW, uint32_t& OutH, std::vector<uint8_t>& OutRgb)
{
    FILE* File = std::fopen(Path.c_str(), "rb");
    if (!File)
    {
        return false;
    }
    auto NextToken = [&File](char* TokenOut, size_t Capacity) -> bool {
        int Byte = std::fgetc(File);
        while (Byte != EOF)
        {
            if (Byte == '#')
            {
                while (Byte != EOF && Byte != '\n') Byte = std::fgetc(File);
            }
            else if (Byte == ' ' || Byte == '\t' || Byte == '\r' || Byte == '\n')
            {
                Byte = std::fgetc(File);
            }
            else
            {
                break;
            }
        }
        size_t Count = 0u;
        while (Byte != EOF && Byte != ' ' && Byte != '\t' && Byte != '\r' && Byte != '\n' && Count + 1u < Capacity)
        {
            TokenOut[Count++] = static_cast<char>(Byte);
            Byte              = std::fgetc(File);
        }
        TokenOut[Count] = '\0';
        return Count > 0u;
    };
    char Token[32]{};
    if (!NextToken(Token, sizeof(Token)) || std::strcmp(Token, "P6") != 0)
    {
        std::fclose(File);
        return false;
    }
    if (!NextToken(Token, sizeof(Token))) { std::fclose(File); return false; }
    OutW = static_cast<uint32_t>(std::strtoul(Token, nullptr, 10));
    if (!NextToken(Token, sizeof(Token))) { std::fclose(File); return false; }
    OutH = static_cast<uint32_t>(std::strtoul(Token, nullptr, 10));
    if (!NextToken(Token, sizeof(Token))) { std::fclose(File); return false; }
    const uint32_t MaxVal = static_cast<uint32_t>(std::strtoul(Token, nullptr, 10));
    if (OutW < 4u || OutH < 2u || MaxVal == 0u || MaxVal > 255u)
    {
        std::fclose(File);
        return false;
    }
    OutRgb.resize(static_cast<size_t>(OutW) * OutH * 3u);
    const size_t Read = std::fread(OutRgb.data(), 1u, OutRgb.size(), File);
    std::fclose(File);
    return Read == OutRgb.size();
}

void WriteResampledSrgbLayer(const std::vector<uint8_t>& SrcRgb,
                             uint32_t                    SrcW,
                             uint32_t                    SrcH,
                             uint32_t                    DstW,
                             uint32_t                    DstH,
                             uint8_t*                    DstRgbaSlice) noexcept
{
    if (SrcW == DstW && SrcH == DstH)
    {
        const size_t PixelCount = static_cast<size_t>(DstW) * DstH;
        for (size_t Index = 0u; Index < PixelCount; ++Index)
        {
            DstRgbaSlice[4u * Index + 0u] = SrcRgb[3u * Index + 0u];
            DstRgbaSlice[4u * Index + 1u] = SrcRgb[3u * Index + 1u];
            DstRgbaSlice[4u * Index + 2u] = SrcRgb[3u * Index + 2u];
            DstRgbaSlice[4u * Index + 3u] = 255u;
        }
        return;
    }

    for (uint32_t Row = 0u; Row < DstH; ++Row)
    {
        const float CoordV = (static_cast<float>(Row) + 0.5f) / static_cast<float>(DstH);
        const float SrcY   = std::clamp(CoordV * static_cast<float>(SrcH) - 0.5f, 0.0f, static_cast<float>(SrcH - 1u));
        const uint32_t Y0  = static_cast<uint32_t>(SrcY);
        const uint32_t Y1  = std::min(Y0 + 1u, SrcH - 1u);
        const float    Ty  = SrcY - static_cast<float>(Y0);

        for (uint32_t Col = 0u; Col < DstW; ++Col)
        {
            const float    CoordU = (static_cast<float>(Col) + 0.5f) / static_cast<float>(DstW);
            const float    SrcX   = CoordU * static_cast<float>(SrcW) - 0.5f;
            const int32_t  Xi     = static_cast<int32_t>(std::floor(SrcX));
            const float    Tx     = SrcX - static_cast<float>(Xi);
            const uint32_t X0     = static_cast<uint32_t>(((Xi % static_cast<int32_t>(SrcW)) + static_cast<int32_t>(SrcW)) % static_cast<int32_t>(SrcW));
            const uint32_t X1     = (X0 + 1u) % SrcW;

            const auto SampleSrc = [&](uint32_t X, uint32_t Y) noexcept -> Vec3 {
                const size_t Offset = (static_cast<size_t>(Y) * SrcW + X) * 3u;
                return {SrgbByteToLinear(SrcRgb[Offset + 0u]),
                        SrgbByteToLinear(SrcRgb[Offset + 1u]),
                        SrgbByteToLinear(SrcRgb[Offset + 2u])};
            };
            const Vec3 Top    = Lerp3(SampleSrc(X0, Y0), SampleSrc(X1, Y0), Tx);
            const Vec3 Bottom = Lerp3(SampleSrc(X0, Y1), SampleSrc(X1, Y1), Tx);
            const Vec3 Linear = Lerp3(Top, Bottom, Ty);

            const size_t OutOffset        = (static_cast<size_t>(Row) * DstW + Col) * 4u;
            DstRgbaSlice[OutOffset + 0u]  = LinearToSrgbByte(Linear.X);
            DstRgbaSlice[OutOffset + 1u]  = LinearToSrgbByte(Linear.Y);
            DstRgbaSlice[OutOffset + 2u]  = LinearToSrgbByte(Linear.Z);
            DstRgbaSlice[OutOffset + 3u]  = 255u;
        }
    }
}

void SynthesizeFallbackMoonSlice(uint32_t LayerIndex, uint32_t DstW, uint32_t DstH, uint8_t* DstRgbaSlice) noexcept
{
    for (uint32_t Row = 0u; Row < DstH; ++Row)
    {
        const float CoordV = (static_cast<float>(Row) + 0.5f) / static_cast<float>(DstH);
        const float φLat   = (0.5f - CoordV) * π;
        const float cosφ   = std::cos(φLat);
        const float sinφ   = std::sin(φLat);

        for (uint32_t Col = 0u; Col < DstW; ++Col)
        {
            const float CoordU = (static_cast<float>(Col) + 0.5f) / static_cast<float>(DstW);
            const float λLon   = (CoordU - 0.5f) * Twoπ;
            const Vec3  Sphere{cosφ * std::cos(λLon), cosφ * std::sin(λLon), sinφ};

            const float Maria   = Fbm3D(Mul3(Sphere, 2.6f), 4u, 701u + LayerIndex * 97u);
            const float Craters = RidgedFbm3D(Mul3(Sphere, 9.5f), 4u, 991u + LayerIndex * 131u);
            const float Tone    = std::clamp(0.08f + 0.14f * Smoothstep(0.35f, 0.72f, Maria) + 0.08f * Craters, 0.02f, 0.85f);

            Vec3 Tint{1.0f, 0.98f, 0.94f};
            if (LayerIndex == 1u) Tint = {1.35f, 0.72f, 0.45f};
            else if (LayerIndex == 2u) Tint = {1.15f, 1.35f, 1.55f};
            else if (LayerIndex == 3u) Tint = {1.55f, 1.28f, 0.42f};
            else if (LayerIndex == 4u) Tint = {1.25f, 0.95f, 0.58f};
            else if (LayerIndex == 5u) Tint = {0.72f, 0.74f, 0.82f};

            const size_t OutOffset       = (static_cast<size_t>(Row) * DstW + Col) * 4u;
            DstRgbaSlice[OutOffset + 0u] = LinearToSrgbByte(Tone * Tint.X);
            DstRgbaSlice[OutOffset + 1u] = LinearToSrgbByte(Tone * Tint.Y);
            DstRgbaSlice[OutOffset + 2u] = LinearToSrgbByte(Tone * Tint.Z);
            DstRgbaSlice[OutOffset + 3u] = 255u;
        }
    }
}

bool IntersectRayUnitSphere(Vec3 Origin, Vec3 Direction, float Radius, float& OutNear, float& OutFar) noexcept
{
    const float HalfB        = Dot3(Origin, Direction);
    const float ConstantTerm = Dot3(Origin, Origin) - Radius * Radius;
    const float Discriminant = HalfB * HalfB - ConstantTerm;
    if (Discriminant < 0.0f)
    {
        return false;
    }
    const float Root = std::sqrt(Discriminant);
    OutNear          = -HalfB - Root;
    OutFar           = -HalfB + Root;
    return OutFar > 0.0f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                1. 6-BODY LUNAR ATLAS IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

float ResolveLunarAtmosphereHaze(const LunarBodyDescriptor& Moon) noexcept
{
    if (Moon.AtmosphereHaze >= 0.0f)
    {
        return std::clamp(Moon.AtmosphereHaze, 0.0f, 1.0f);
    }
    switch (Moon.Preset)
    {
        case LunarBodyPreset::Luna:
            return 0.04f; // Tenuous lunar exospheric dust corona
        case LunarBodyPreset::Ember:
            return 0.36f; // Volcanic ash & thermal plume haze
        case LunarBodyPreset::Glacier:
            return 0.34f; // Sublimating icy cryosphere & water-vapour plume haze
        case LunarBodyPreset::Sulfur:
            return 0.42f; // Dense volcanic sulfur-dioxide haze
        case LunarBodyPreset::Shroud:
            return 0.68f; // Thick Titan-class photochemical amber/cyan haze shell
        case LunarBodyPreset::Shard:
            return 0.07f; // Faint silicate regolith veil
    }
    return 0.05f;
}

Vec3 ResolveLunarAtmosphereTint(const LunarBodyDescriptor& Moon) noexcept
{
    if (Moon.AtmosphereTintRgb.X > 1.0e-4f || Moon.AtmosphereTintRgb.Y > 1.0e-4f || Moon.AtmosphereTintRgb.Z > 1.0e-4f)
    {
        return Moon.AtmosphereTintRgb;
    }
    switch (Moon.Preset)
    {
        case LunarBodyPreset::Luna:
            return {0.76f, 0.84f, 1.00f};
        case LunarBodyPreset::Ember:
            return {1.00f, 0.56f, 0.32f};
        case LunarBodyPreset::Glacier:
            return {0.60f, 0.86f, 1.00f};
        case LunarBodyPreset::Sulfur:
            return {1.00f, 0.84f, 0.38f};
        case LunarBodyPreset::Shroud:
            return {0.96f, 0.74f, 0.44f};
        case LunarBodyPreset::Shard:
            return {0.74f, 0.80f, 0.92f};
    }
    return {0.76f, 0.84f, 1.00f};
}

bool DecodeSrgbImageRgb8(const std::string&    FilePath,
                         uint32_t&             OutWidth,
                         uint32_t&             OutHeight,
                         std::vector<uint8_t>& OutRgbBytes)
{
    OutWidth  = 0u;
    OutHeight = 0u;
    OutRgbBytes.clear();
    if (FilePath.empty())
    {
        return false;
    }
    if (FilePath.size() >= 4u && FilePath.substr(FilePath.size() - 4u) == ".ppm")
    {
        return LoadPpmRgb8(FilePath, OutWidth, OutHeight, OutRgbBytes);
    }
    std::ifstream Stream(FilePath, std::ios::binary | std::ios::ate);
    if (!Stream)
    {
        return false;
    }
    const std::streamsize ByteSize = Stream.tellg();
    if (ByteSize <= 0)
    {
        return false;
    }
    Stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> Encoded(static_cast<size_t>(ByteSize));
    if (!Stream.read(reinterpret_cast<char*>(Encoded.data()), ByteSize))
    {
        return false;
    }
    int DecodedW = 0;
    int DecodedH = 0;
    int Channels = 0;
    stbi_uc* Pixels = stbi_load_from_memory(
        Encoded.data(), static_cast<int>(Encoded.size()), &DecodedW, &DecodedH, &Channels, 3);
    if (!Pixels || DecodedW < 4 || DecodedH < 2)
    {
        if (Pixels) stbi_image_free(Pixels);
        return false;
    }
    OutWidth  = static_cast<uint32_t>(DecodedW);
    OutHeight = static_cast<uint32_t>(DecodedH);
    OutRgbBytes.assign(Pixels, Pixels + static_cast<size_t>(OutWidth) * OutHeight * 3u);
    stbi_image_free(Pixels);
    return true;
}

namespace
{

std::filesystem::path ResolveCelestialAssetPath(const std::string& RequestedPath, const char* RelativeFilename)
{
    std::error_code       Ec;
    std::filesystem::path Direct(RequestedPath);
    if (!RequestedPath.empty() && std::filesystem::is_regular_file(Direct, Ec))
    {
        return Direct;
    }

    const std::filesystem::path RelSubpath =
        std::filesystem::path("EnvironmentArchives") / "CelestialMaps" / RelativeFilename;

    if (!RequestedPath.empty())
    {
        if (std::filesystem::is_directory(Direct, Ec))
        {
            const std::filesystem::path ProjCand = Direct / "Content" / RelSubpath;
            if (std::filesystem::is_regular_file(ProjCand, Ec)) return ProjCand;
            const std::filesystem::path DirectSub = Direct / RelSubpath;
            if (std::filesystem::is_regular_file(DirectSub, Ec)) return DirectSub;
            const std::filesystem::path EngCand = Direct / "EngineContent" / RelSubpath;
            if (std::filesystem::is_regular_file(EngCand, Ec)) return EngCand;
        }
        // Handle EngineContent/... paths redirected to staged <Project>/Content/... when EngineContent is absent
        const std::string Generic = Direct.generic_string();
        const auto        EngPos  = Generic.find("EngineContent/");
        if (EngPos != std::string::npos)
        {
            const std::filesystem::path Prefix  = Direct.generic_string().substr(0u, EngPos);
            const std::filesystem::path SubTail = Direct.generic_string().substr(EngPos + 14u);
            const std::filesystem::path Staged  = Prefix / "Content" / SubTail;
            if (std::filesystem::is_regular_file(Staged, Ec)) return Staged;
        }
    }

    if (const char* EnvContent = std::getenv("FRONTIER_PROJECT_CONTENT_DIR"); EnvContent && *EnvContent)
    {
        const std::filesystem::path EnvCand = std::filesystem::path(EnvContent) / RelSubpath;
        if (std::filesystem::is_regular_file(EnvCand, Ec)) return EnvCand;
    }
    const std::filesystem::path LocalContent = std::filesystem::path("Content") / RelSubpath;
    if (std::filesystem::is_regular_file(LocalContent, Ec)) return LocalContent;
    const std::filesystem::path LocalEngine = std::filesystem::path("EngineContent") / RelSubpath;
    if (std::filesystem::is_regular_file(LocalEngine, Ec)) return LocalEngine;
    return Direct;
}

} // namespace

bool LunarSixBodyAtlas::BuildFromLunaSource(const std::string& LunaJpegOrPpmPath,
                                            uint32_t           TargetWidth,
                                            uint32_t           TargetHeight)
{
    if (TargetWidth < 16u || TargetHeight < 8u)
    {
        return false;
    }
    Width_  = TargetWidth;
    Height_ = TargetHeight;
    Rgba8Srgb_.assign(TotalByteSize(), 255u);

    const std::filesystem::path ResolvedLuna =
        ResolveCelestialAssetPath(LunaJpegOrPpmPath, "MoonAlbedoSource2048.jpg");
    const std::filesystem::path ParentDir =
        ResolvedLuna.empty() ? std::filesystem::path{} : ResolvedLuna.parent_path();

    static constexpr std::array<const char*, LunarAtlasLayerCount> kSiblingFilenames{{
        "MoonAlbedoSource2048.jpg",
        "Ember2048.jpg",
        "Glacier2048.jpg",
        "Sulfur2048.jpg",
        "Shroud2048.jpg",
        "Shard2048.jpg",
    }};

    const size_t LayerBytes = LayerByteSize();
    for (uint32_t Layer = 0u; Layer < LunarAtlasLayerCount; ++Layer)
    {
        std::string CandidatePath;
        if (Layer == 0u && !ResolvedLuna.empty())
        {
            CandidatePath = ResolvedLuna.string();
        }
        else if (!ParentDir.empty())
        {
            CandidatePath = (ParentDir / kSiblingFilenames[Layer]).string();
        }
        else
        {
            CandidatePath = ResolveCelestialAssetPath(LunaJpegOrPpmPath, kSiblingFilenames[Layer]).string();
        }

        uint32_t             SrcW = 0u;
        uint32_t             SrcH = 0u;
        std::vector<uint8_t> SrcRgb;
        uint8_t*             SliceOut = Rgba8Srgb_.data() + static_cast<size_t>(Layer) * LayerBytes;
        if (DecodeSrgbImageRgb8(CandidatePath, SrcW, SrcH, SrcRgb))
        {
            WriteResampledSrgbLayer(SrcRgb, SrcW, SrcH, Width_, Height_, SliceOut);
        }
        else
        {
            SynthesizeFallbackMoonSlice(Layer, Width_, Height_, SliceOut);
        }
    }
    return true;
}

const uint8_t* LunarSixBodyAtlas::LayerData(LunarBodyPreset Preset) const noexcept
{
    if (!Valid())
    {
        return nullptr;
    }
    const uint32_t Layer = std::min(static_cast<uint32_t>(Preset), LunarAtlasLayerCount - 1u);
    return Rgba8Srgb_.data() + static_cast<size_t>(Layer) * LayerByteSize();
}

Vec3 LunarSixBodyAtlas::SampleLinearAlbedo(LunarBodyPreset Preset, float LongitudeRad, float LatitudeRad) const noexcept
{
    const uint8_t* Slice = LayerData(Preset);
    if (!Slice)
    {
        return {0.12f, 0.12f, 0.12f};
    }
    float CoordU = (LongitudeRad + π) / Twoπ;
    CoordU -= std::floor(CoordU);
    const float CoordV = std::clamp((0.5f * π - LatitudeRad) / π, 0.0f, 1.0f);

    const float   ScaledX = CoordU * static_cast<float>(Width_) - 0.5f;
    const float   ScaledY = std::clamp(CoordV * static_cast<float>(Height_) - 0.5f, 0.0f, static_cast<float>(Height_ - 1u));
    const int32_t Xi      = static_cast<int32_t>(std::floor(ScaledX));
    const int32_t Yi      = static_cast<int32_t>(std::floor(ScaledY));
    const float   Tx      = ScaledX - static_cast<float>(Xi);
    const float   Ty      = ScaledY - static_cast<float>(Yi);

    const auto Fetch = [&](int32_t Col, int32_t Row) noexcept -> Vec3 {
        const uint32_t WrappedX = static_cast<uint32_t>(((Col % static_cast<int32_t>(Width_)) + static_cast<int32_t>(Width_)) % static_cast<int32_t>(Width_));
        const uint32_t ClampedY = static_cast<uint32_t>(std::clamp(Row, 0, static_cast<int32_t>(Height_ - 1u)));
        const size_t   Offset   = (static_cast<size_t>(ClampedY) * Width_ + WrappedX) * 4u;
        return {SrgbByteToLinear(Slice[Offset + 0u]),
                SrgbByteToLinear(Slice[Offset + 1u]),
                SrgbByteToLinear(Slice[Offset + 2u])};
    };

    const Vec3 Top    = Lerp3(Fetch(Xi, Yi), Fetch(Xi + 1, Yi), Tx);
    const Vec3 Bottom = Lerp3(Fetch(Xi, Yi + 1), Fetch(Xi + 1, Yi + 1), Tx);
    return Lerp3(Top, Bottom, Ty);
}

//------------------------------------------------------------------------------------------------------------------------
//                      2. OCTAHEDRAL HIPPARCOS INDEX, SCINTILLATION & MILKY WAY
//------------------------------------------------------------------------------------------------------------------------

void PlanetaryDomeProjection::EncodeOctahedral(Vec3 DirectionUnit, float& OutU, float& OutV) noexcept
{
    const Vec3  Unit  = Normalize3(DirectionUnit);
    const float InvL1 = 1.0f / std::max(std::abs(Unit.X) + std::abs(Unit.Y) + std::abs(Unit.Z), 1e-12f);
    float       OctX  = Unit.X * InvL1;
    float       OctY  = Unit.Y * InvL1;
    if (Unit.Z < 0.0f)
    {
        const float WrappedX = (1.0f - std::abs(OctY)) * (OctX >= 0.0f ? 1.0f : -1.0f);
        const float WrappedY = (1.0f - std::abs(OctX)) * (OctY >= 0.0f ? 1.0f : -1.0f);
        OctX                 = WrappedX;
        OctY                 = WrappedY;
    }
    OutU = std::clamp(0.5f * OctX + 0.5f, 0.0f, 1.0f);
    OutV = std::clamp(0.5f * OctY + 0.5f, 0.0f, 1.0f);
}

Vec3 PlanetaryDomeProjection::BvColourIndexToRgb(float BvIndex) noexcept
{
    const float ClampedBv = std::clamp(BvIndex, -0.40f, 2.00f);
    const float Kelvin =
        4600.0f * (1.0f / (0.92f * ClampedBv + 1.70f) + 1.0f / (0.92f * ClampedBv + 0.62f));
    return KelvinToBlackbodyChromaticity(Kelvin);
}

Vec3 PlanetaryDomeProjection::KelvinToBlackbodyChromaticity(float TemperatureKelvin) noexcept
{
    const float TempHecto = std::clamp(TemperatureKelvin, 1200.0f, 40000.0f) * 0.01f;
    float       Red       = 1.0f;
    float       Green     = 1.0f;
    float       Blue      = 1.0f;

    if (TempHecto <= 66.0f)
    {
        Red   = 1.0f;
        Green = std::clamp((99.4708025861f * std::log(std::max(TempHecto, 1.0f)) - 161.1195681661f) / 255.0f, 0.0f, 1.0f);
        Blue  = TempHecto <= 19.0f
                    ? 0.0f
                    : std::clamp((138.5177312231f * std::log(TempHecto - 10.0f) - 305.0447927307f) / 255.0f, 0.0f, 1.0f);
    }
    else
    {
        Red   = std::clamp(329.698727446f * std::pow(TempHecto - 60.0f, -0.1332047592f) / 255.0f, 0.0f, 1.0f);
        Green = std::clamp(288.1221695283f * std::pow(TempHecto - 60.0f, -0.0755148492f) / 255.0f, 0.0f, 1.0f);
        Blue  = 1.0f;
    }

    const Vec3  Linear{ std::pow(Red, 2.2f), std::pow(Green, 2.2f), std::pow(Blue, 2.2f) };
    const float Lum = 0.2126f * Linear.X + 0.7152f * Linear.Y + 0.0722f * Linear.Z;
    return Lum > 1e-5f ? Mul3(Linear, 1.0f / Lum) : Vec3{1.0f, 1.0f, 1.0f};
}

void PlanetaryDomeProjection::BinStarRecords(const std::vector<StarRecord>& Records)
{
    SourceStarCount_ = static_cast<uint32_t>(Records.size());
    StarCells_.assign(OctahedralCellCount, OctahedralCellSpan{});
    BinnedStars_.clear();
    if (Records.empty())
    {
        return;
    }

    std::vector<std::vector<BinnedCatalogueStar>> Buckets(OctahedralCellCount);
    for (size_t Index = 0u; Index < Records.size(); ++Index)
    {
        const StarRecord& Record = Records[Index];
        const Vec3        Dir    = Normalize3({static_cast<float>(Record.Direction.X),
                                               static_cast<float>(Record.Direction.Y),
                                               static_cast<float>(Record.Direction.Z)});
        float OctU = 0.5f, OctV = 0.5f;
        EncodeOctahedral(Dir, OctU, OctV);
        const uint32_t CellX = std::min(OctahedralGridDim - 1u, static_cast<uint32_t>(OctU * OctahedralGridDim));
        const uint32_t CellY = std::min(OctahedralGridDim - 1u, static_cast<uint32_t>(OctV * OctahedralGridDim));

        BinnedCatalogueStar Binned{};
        Binned.DirectionUnit   = Dir;
        Binned.Magnitude       = static_cast<float>(Record.Magnitude);
        Binned.ChromaticityRgb = BvColourIndexToRgb(static_cast<float>(Record.ColourIndex));
        Binned.PhaseSeed       = static_cast<float>(HashMix32(static_cast<uint32_t>(Index) * 2654435761u) & 0xFFFFu) / 65535.0f;
        Buckets[CellY * OctahedralGridDim + CellX].push_back(Binned);
    }

    BinnedStars_.reserve(Records.size());
    for (uint32_t Cell = 0u; Cell < OctahedralCellCount; ++Cell)
    {
        auto& Bucket = Buckets[Cell];
        std::sort(Bucket.begin(), Bucket.end(), [](const BinnedCatalogueStar& A, const BinnedCatalogueStar& B) {
            return A.Magnitude < B.Magnitude;
        });
        StarCells_[Cell].First = static_cast<uint32_t>(BinnedStars_.size());
        StarCells_[Cell].Count = static_cast<uint32_t>(Bucket.size());
        BinnedStars_.insert(BinnedStars_.end(), Bucket.begin(), Bucket.end());
    }
}

bool PlanetaryDomeProjection::LoadHipparcosCatalogueCsv(const std::string& CsvPath,
                                                        double             JulianUt1,
                                                        double             LatitudeDeg,
                                                        double             LongitudeDeg)
{
    const std::filesystem::path ResolvedCsv = ResolveCelestialAssetPath(CsvPath, "Hipparcos8.csv");
    std::ifstream Stream(ResolvedCsv);
    if (!Stream)
    {
        return false;
    }
    const std::vector<StarRecord> Decoded = DecodeHipparcosCatalogue(Stream, JulianUt1, LatitudeDeg, LongitudeDeg);
    if (Decoded.empty())
    {
        return false;
    }
    BinStarRecords(Decoded);
    return true;
}

void PlanetaryDomeProjection::BuildProceduralStarCatalogue(uint32_t StarCount, uint64_t Seed)
{
    BinStarRecords(GenerateStarCatalogue(StarCount, Seed));
}

bool PlanetaryDomeProjection::LoadLunarAtlas(const std::string& LunaJpegOrPpmPath,
                                             uint32_t           AtlasWidth,
                                             uint32_t           AtlasHeight)
{
    return LunarAtlas_.BuildFromLunaSource(LunaJpegOrPpmPath, AtlasWidth, AtlasHeight);
}

// 📝 PlanetaryProjection.slang EvaluateScintillationGpu, term for term: the demo's three-frequency signal on the twinkle clock, the
//    depth the zenith's peak swing, deepened with the airmass toward the horizon.
float PlanetaryDomeProjection::EvaluateStarScintillation(float AltitudeSine,
                                                         float PhaseSeed,
                                                         float TwinkleClock,
                                                         float Depth) noexcept
{
    if (Depth <= 0.0f)
    {
        return 1.0f;
    }
    const float Phase        = PhaseSeed * Twoπ;
    const float Signal       = 0.60f * std::sin(TwinkleClock * Twoπ + Phase) +
                               0.28f * std::sin(TwinkleClock * 11.71f + Phase * 1.7f) +
                               0.12f * std::sin(TwinkleClock * 19.37f + Phase * 2.3f);
    const float AirmassDepth = std::min(2.5f, std::sqrt(1.15f / (std::max(AltitudeSine, 0.0f) + 0.15f)));
    return std::max(0.05f, 1.0f + Depth * AirmassDepth * Signal);
}

Vec3 PlanetaryDomeProjection::EvaluateMilkyWayBandRadiance(Vec3 DirectionUnit, float Strength) noexcept
{
    if (Strength <= 0.0f)
    {
        return {0.0f, 0.0f, 0.0f};
    }
    const Vec3 Dir = Normalize3(DirectionUnit);
    const Vec3 GalacticNorth  = Normalize3({-0.4838f, 0.4560f, 0.7470f});
    const Vec3 GalacticCenter = Normalize3({0.8210f, 0.5620f, -0.0990f});
    const Vec3 GalacticEast   = Normalize3(Cross3(GalacticNorth, GalacticCenter));

    const float sinβ     = std::clamp(Dot3(Dir, GalacticNorth), -1.0f, 1.0f);
    const float βLat     = std::asin(sinβ);
    const float λLon     = std::atan2(Dot3(Dir, GalacticEast), Dot3(Dir, GalacticCenter));
    const float CoreBulge = std::exp(-0.5f * ((λLon * λLon) / (0.62f * 0.62f) + (βLat * βLat) / (0.19f * 0.19f)));
    const float DiscBand  = std::exp(-0.5f * (βLat * βLat) / (0.14f * 0.14f));
    if (DiscBand < 1e-4f && CoreBulge < 1e-4f)
    {
        return {0.0f, 0.0f, 0.0f};
    }

    const float Structure = 0.55f + 0.75f * Fbm3D(Mul3(Dir, 5.2f), 4u, 7717u);
    const float RiftPlane = std::exp(-0.5f * (βLat * βLat) / (0.048f * 0.048f));
    const float DustNoise = Smoothstep(0.38f, 0.72f, RidgedFbm3D(Mul3(Dir, 8.6f), 4u, 9923u));
    const float DustAtten = 1.0f - 0.76f * RiftPlane * DustNoise;
    const float Intensity = Strength * (0.0018f * DiscBand + 0.0029f * CoreBulge) * Structure * DustAtten;
    const Vec3  Tint      = Lerp3({0.74f, 0.82f, 1.00f}, {1.08f, 0.94f, 0.76f}, std::clamp(CoreBulge * 1.25f, 0.0f, 1.0f));
    return Mul3(Tint, Intensity);
}

//------------------------------------------------------------------------------------------------------------------------
//                           3. EXOGENOUS 3D PLANETS, 6-BODY MOONS & CUSTOM STARS
//------------------------------------------------------------------------------------------------------------------------

Vec3 PlanetaryDomeProjection::EvaluatePlanetaryBody(const PlanetaryBodyDescriptor& Planet,
                                                    Vec3                           ViewDirectionUnit,
                                                    Vec3                           SunDirectionUnit,
                                                    Vec3                           SolarIrradianceRgb,
                                                    float                          PixelAngularSizeRad,
                                                    float&                         OutCoverage) const noexcept
{
    OutCoverage = 0.0f;
    if (Planet.AngularRadiusRad <= 1e-6f)
    {
        return {};
    }
    const Vec3  ViewDir   = Normalize3(ViewDirectionUnit);
    const Vec3  CenterDir = Normalize3(Planet.DirectionUnit);
    const Vec3  SunDir    = Normalize3(SunDirectionUnit);
    const Vec3  SpinAxis  = Normalize3(Planet.SpinAxisUnit);

    const float MaxOuterRatio = Planet.Rings.Enabled ? std::max(1.15f, Planet.Rings.OuterRadiusRatio + 0.1f) : 1.20f;
    const float MaxConeRad    = Planet.AngularRadiusRad * MaxOuterRatio + 3.0f * PixelAngularSizeRad;
    const float cosθ          = std::clamp(Dot3(ViewDir, CenterDir), -1.0f, 1.0f);
    if (std::acos(cosθ) > MaxConeRad)
    {
        return {};
    }

    const float     Distance    = 1.0f / std::max(std::sin(Planet.AngularRadiusRad), 1e-5f);
    const Vec3      RayOrigin   = Mul3(CenterDir, -Distance);
    const Vec3      FallbackRef = std::abs(SpinAxis.Z) < 0.9f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3      BasisX      = Normalize3(Cross3(FallbackRef, SpinAxis));
    const Vec3      BasisY      = Cross3(SpinAxis, BasisX);

    float SphereNear = 0.0f, SphereFar = 0.0f;
    const bool HitSphere = IntersectRayUnitSphere(RayOrigin, ViewDir, 1.0f, SphereNear, SphereFar) && SphereNear > 0.0f;

    float RingT        = 1e30f;
    float RingAlpha    = 0.0f;
    Vec3  RingRadiance = {};
    if (Planet.Rings.Enabled && Planet.Rings.OuterRadiusRatio > Planet.Rings.InnerRadiusRatio)
    {
        const float Denom = Dot3(ViewDir, SpinAxis);
        if (std::abs(Denom) > 1e-5f)
        {
            const float HitT = -Dot3(RayOrigin, SpinAxis) / Denom;
            if (HitT > 0.0f)
            {
                const Vec3  HitPt  = Add3(RayOrigin, Mul3(ViewDir, HitT));
                const float RingR  = Length3(HitPt);
                if (RingR >= Planet.Rings.InnerRadiusRatio && RingR <= Planet.Rings.OuterRadiusRatio)
                {
                    RingT = HitT;
                    const float NormU = (RingR - Planet.Rings.InnerRadiusRatio) /
                                        std::max(Planet.Rings.OuterRadiusRatio - Planet.Rings.InnerRadiusRatio, 1e-4f);
                    const float CassiniGap = 1.0f - 0.88f * std::exp(-std::pow((NormU - 0.58f) / 0.032f, 2.0f));
                    const float EnckeGap   = 1.0f - 0.65f * std::exp(-std::pow((NormU - 0.86f) / 0.014f, 2.0f));
                    const float Ringlets   = 0.72f + 0.28f * std::sin(NormU * 94.0f) * std::cos(NormU * 37.0f);
                    const float τRing      = std::max(0.0f, Planet.Rings.PeakOpticalDepth * CassiniGap * EnckeGap * Ringlets);
                    RingAlpha              = 1.0f - std::exp(-τRing / std::max(std::abs(Denom), 0.18f));

                    float ShadowNear = 0.0f, ShadowFar = 0.0f;
                    const bool InPlanetUmbra =
                        IntersectRayUnitSphere(HitPt, SunDir, 1.0f, ShadowNear, ShadowFar) && ShadowNear > 0.0f;
                    const float SunLit    = InPlanetUmbra ? 0.03f : 1.0f;
                    const float SunTilt   = 0.28f + 0.72f * std::abs(Dot3(SpinAxis, SunDir));
                    const Vec3  RingTint  = Lerp3(Planet.Rings.InnerTintRgb, Planet.Rings.OuterTintRgb, NormU);
                    RingRadiance          = Mul3(Mul3Comp(RingTint, SolarIrradianceRgb), RingAlpha * SunLit * SunTilt * 0.42f * Planet.SurfaceAlbedoScale);
                }
            }
        }
    }

    Vec3  BodyRadiance = {};
    float BodyAlpha    = 0.0f;
    if (HitSphere)
    {
        const Vec3  SurfacePt = Add3(RayOrigin, Mul3(ViewDir, SphereNear));
        const Vec3  Normal    = Normalize3(SurfacePt);
        const float sinφ      = std::clamp(Dot3(Normal, SpinAxis), -1.0f, 1.0f);
        const float φLat      = std::asin(sinφ);
        const float λLon      = std::atan2(Dot3(Normal, BasisY), Dot3(Normal, BasisX)) + Planet.SpinPhaseRad;
        const Vec3  RotPt     = {std::cos(φLat) * std::cos(λLon), std::cos(φLat) * std::sin(λLon), sinφ};

        Vec3 Albedo{0.6f, 0.6f, 0.6f};
        switch (Planet.Archetype)
        {
            case PlanetarySurfaceArchetype::BandedGasGiant:
            {
                const float Warp  = 0.14f * Fbm3D(Mul3(RotPt, 4.5f), 4u, Planet.ProceduralSeed);
                const float Bands = 0.5f + 0.5f * std::sin((φLat + Warp) * 18.0f);
                const float Fine  = Fbm3D({RotPt.X * 6.0f, RotPt.Y * 6.0f, RotPt.Z * 16.0f}, 3u, Planet.ProceduralSeed + 19u);
                const Vec3  Zone  = {0.86f, 0.78f, 0.66f};
                const Vec3  Belt  = {0.58f, 0.39f, 0.26f};
                Albedo            = Lerp3(Belt, Zone, std::clamp(0.5f + (0.7f * Bands + 0.3f * Fine - 0.5f) * Planet.BandContrast, 0.0f, 1.0f));
                const float SpotD = std::hypot(λLon - 0.5f, (φLat + 0.38f) * 2.2f);
                if (SpotD < 0.28f)
                {
                    Albedo = Lerp3(Albedo, {0.82f, 0.34f, 0.22f}, Smoothstep(0.28f, 0.08f, SpotD));
                }
                break;
            }
            case PlanetarySurfaceArchetype::OceanicTerrestrial:
            {
                const float Continent = Fbm3D(Mul3(RotPt, 2.8f), 5u, Planet.ProceduralSeed);
                const Vec3  Ocean     = {0.06f, 0.16f, 0.38f};
                const Vec3  Land      = Lerp3({0.14f, 0.32f, 0.12f}, {0.54f, 0.44f, 0.28f}, Fbm3D(Mul3(RotPt, 7.0f), 3u, Planet.ProceduralSeed + 7u));
                Albedo                = Continent > 0.51f ? Land : Ocean;
                const float IceCap    = Smoothstep(1.05f, 1.32f, std::abs(φLat));
                Albedo                = Lerp3(Albedo, {0.88f, 0.91f, 0.95f}, IceCap);
                const float Clouds    = Smoothstep(0.48f, 0.74f, Fbm3D(Mul3(RotPt, 5.4f), 4u, Planet.ProceduralSeed + 53u));
                Albedo                = Lerp3(Albedo, {0.90f, 0.92f, 0.95f}, 0.75f * Clouds);
                break;
            }
            case PlanetarySurfaceArchetype::RustyDesertWorld:
            {
                const float Maria = Fbm3D(Mul3(RotPt, 3.2f), 4u, Planet.ProceduralSeed);
                const float Ridge = RidgedFbm3D(Mul3(RotPt, 8.5f), 4u, Planet.ProceduralSeed + 31u);
                Albedo            = Lerp3({0.32f, 0.16f, 0.10f}, {0.72f, 0.38f, 0.20f}, std::clamp(0.65f * Maria + 0.35f * Ridge, 0.0f, 1.0f));
                Albedo = Lerp3({.52f,.27f,.15f},Albedo,Planet.SurfaceDetail);
                break;
            }
            case PlanetarySurfaceArchetype::CloudVeiledWorld:
            {
                const float Clouds = Fbm3D(Mul3(RotPt,5),4u,Planet.ProceduralSeed+997u);
                Albedo = Lerp3({.60f,.40f,.18f},{.96f,.87f,.66f},std::clamp(.25f+Clouds*Planet.CloudVeil,0.0f,1.0f));
                break;
            }
            case PlanetarySurfaceArchetype::MethaneIceGiant:
            {
                const float Haze = 0.5f + 0.5f * std::cos(φLat * 5.0f + 0.25f * Fbm3D(Mul3(RotPt, 3.0f), 3u, Planet.ProceduralSeed));
                Albedo           = Lerp3({0.20f, 0.52f, 0.74f}, {0.42f, 0.78f, 0.88f}, Haze);
                break;
            }
        }
        Albedo = Mul3(Mul3Comp(Albedo,Planet.SurfaceTint), Planet.SurfaceAlbedoScale);

        float RingShadow = 1.0f;
        if (Planet.Rings.Enabled)
        {
            const float Denom = Dot3(SunDir, SpinAxis);
            if (std::abs(Denom) > 1e-5f)
            {
                const float ShadowT = -Dot3(SurfacePt, SpinAxis) / Denom;
                if (ShadowT > 0.0f)
                {
                    const Vec3  RingCross = Add3(SurfacePt, Mul3(SunDir, ShadowT));
                    const float CrossR    = Length3(RingCross);
                    if (CrossR >= Planet.Rings.InnerRadiusRatio && CrossR <= Planet.Rings.OuterRadiusRatio)
                    {
                        const float NormU = (CrossR - Planet.Rings.InnerRadiusRatio) /
                                            std::max(Planet.Rings.OuterRadiusRatio - Planet.Rings.InnerRadiusRatio, 1e-4f);
                        const float CassiniGap = 1.0f - 0.88f * std::exp(-std::pow((NormU - 0.58f) / 0.032f, 2.0f));
                        RingShadow             = std::exp(-0.75f * Planet.Rings.PeakOpticalDepth * CassiniGap);
                    }
                }
            }
        }

        const float μ₀     = std::max(0.0f, Dot3(Normal, SunDir));
        const float μView  = std::max(0.05f, Dot3(Normal, Mul3(ViewDir, -1.0f)));
        const float Minnaert = std::pow(μ₀, 0.78f) * std::pow(μView, -0.22f);
        const float Diffuse  = (μ₀ > 0.0f ? (Minnaert * RingShadow / π) : 0.0f) + 0.0025f;
        BodyRadiance         = Mul3(Mul3Comp(Albedo, SolarIrradianceRgb), Diffuse);

        const float θEdge = std::acos(cosθ);
        BodyAlpha         = Smoothstep(Planet.AngularRadiusRad + 0.6f * PixelAngularSizeRad,
                                       std::max(0.0f, Planet.AngularRadiusRad - 0.6f * PixelAngularSizeRad),
                                       θEdge);
    }

    Vec3 AtmoRadiance{};
    if (Planet.Atmosphere.Enabled && Planet.Atmosphere.ShellHeightFraction > 0.0f)
    {
        const float ShellRadius = 1.0f + Planet.Atmosphere.ShellHeightFraction;
        float       ShellNear = 0.0f, ShellFar = 0.0f;
        if (IntersectRayUnitSphere(RayOrigin, ViewDir, ShellRadius, ShellNear, ShellFar))
        {
            const float SegEnd = HitSphere ? std::min(ShellFar, SphereNear) : ShellFar;
            const float Path   = std::max(0.0f, SegEnd - std::max(0.0f, ShellNear));
            if (Path > 0.0f)
            {
                const Vec3  MidPt   = Normalize3(Add3(RayOrigin, Mul3(ViewDir, 0.5f * (std::max(0.0f, ShellNear) + SegEnd))));
                const float SunTerm = Smoothstep(-0.22f, 0.45f, Dot3(MidPt, SunDir));
                const float LimbOpt = std::clamp(Path / (2.0f * std::sqrt(ShellRadius * ShellRadius - 1.0f)), 0.0f, 1.4f);
                const float Glow    = Planet.SurfaceAlbedoScale * Planet.Atmosphere.LimbGlowStrength * LimbOpt * LimbOpt * SunTerm * 0.35f;
                AtmoRadiance        = Mul3(Mul3Comp(Planet.Atmosphere.RayleighTintRgb, SolarIrradianceRgb), Glow);
            }
        }
    }

    Vec3 Combined{};
    if (RingAlpha > 0.0f && BodyAlpha > 0.0f)
    {
        if (RingT < SphereNear)
        {
            Combined    = Add3(RingRadiance, Mul3(Add3(BodyRadiance, AtmoRadiance), 1.0f - RingAlpha));
            OutCoverage = RingAlpha + BodyAlpha * (1.0f - RingAlpha);
        }
        else
        {
            Combined    = Add3(Mul3(Add3(BodyRadiance, AtmoRadiance), BodyAlpha), Mul3(RingRadiance, 1.0f - BodyAlpha));
            OutCoverage = BodyAlpha + RingAlpha * (1.0f - BodyAlpha);
        }
    }
    else if (BodyAlpha > 0.0f)
    {
        Combined    = Add3(Mul3(BodyRadiance, BodyAlpha), AtmoRadiance);
        OutCoverage = BodyAlpha;
    }
    else
    {
        Combined    = Add3(RingRadiance, AtmoRadiance);
        OutCoverage = RingAlpha;
    }
    return Combined;
}

Vec3 PlanetaryDomeProjection::EvaluateLunarBody(const LunarBodyDescriptor& Moon,
                                                Vec3                       ViewDirectionUnit,
                                                Vec3                       SunDirectionUnit,
                                                Vec3                       SolarIrradianceRgb,
                                                float                      PixelAngularSizeRad,
                                                float&                     OutCoverage) const noexcept
{
    if (Moon.OverridePhase) SolarIrradianceRgb = Moon.PhaseIrradiance;
    OutCoverage = 0.0f;
    if (Moon.AngularRadiusRad <= 1e-6f)
    {
        return {};
    }
    const Vec3  ViewDir  = Normalize3(ViewDirectionUnit);
    const Vec3  MoonDir  = Normalize3(Moon.DirectionUnit);
    const Vec3  SunDir   = Normalize3(Moon.OverridePhase ? Moon.PhaseLightDirection : SunDirectionUnit);
    const float cosθ     = std::clamp(Dot3(ViewDir, MoonDir), -1.0f, 1.0f);
    const float θRad     = std::acos(cosθ);
    const float HalfAa   = std::max(0.55f * PixelAngularSizeRad, 1e-5f);
    const float Haze     = ResolveLunarAtmosphereHaze(Moon);
    const Vec3  HazeTint = ResolveLunarAtmosphereTint(Moon);
    const float OuterLimb = Moon.AngularRadiusRad * (1.0f + 0.14f * std::max(Haze, 0.08f)) + HalfAa;
    if (θRad > OuterLimb)
    {
        return {};
    }

    const Vec3  ObserverDir = Mul3(MoonDir, -1.0f);
    const Vec3  RefUp       = std::abs(MoonDir.Z) < 0.92f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Vec3  UnrolledE   = Normalize3(Cross3(MoonDir, RefUp));
    const Vec3  UnrolledN   = Cross3(UnrolledE, MoonDir);
    const float cosRoll     = std::cos(Moon.RollAngleRad);
    const float sinRoll     = std::sin(Moon.RollAngleRad);
    const Vec3  East        = Add3(Mul3(UnrolledE, cosRoll), Mul3(UnrolledN, sinRoll));
    const Vec3  North       = Sub3(Mul3(UnrolledN, cosRoll), Mul3(UnrolledE, sinRoll));

    const float EdgeInner = Moon.AngularRadiusRad * (0.985f - 0.105f * Haze) - HalfAa;
    const float EdgeOuter = Moon.AngularRadiusRad * (1.0f + 0.065f * Haze) + HalfAa;
    OutCoverage           = Smoothstep(EdgeOuter, std::max(0.0f, EdgeInner), θRad);

    // Outer sunlit atmospheric shell halo extending smoothly past the solid lunar limb
    Vec3 RimShellRadiance{};
    if (Haze > 1.0e-3f)
    {
        const float RimRise  = Smoothstep(Moon.AngularRadiusRad * 0.78f, Moon.AngularRadiusRad, θRad);
        const float RimDecay = 1.0f - Smoothstep(Moon.AngularRadiusRad, OuterLimb, θRad);
        const float RimMask  = RimRise * RimDecay * Haze;
        if (RimMask > 1.0e-5f)
        {
            const Vec3  TangDir   = Normalize3(Sub3(ViewDir, Mul3(MoonDir, cosθ)));
            const Vec3  ShellNorm = Normalize3(Add3(Mul3(TangDir, 0.94f), Mul3(ObserverDir, 0.34f)));
            const float ShellLit  = Smoothstep(-0.28f, 0.45f, Dot3(ShellNorm, SunDir));
            const float RimScale  = (0.042f * Moon.SurfaceBrightness / π) * (0.18f + 0.82f * ShellLit) * RimMask;
            RimShellRadiance      = Mul3Comp(HazeTint, Mul3(SolarIrradianceRgb, RimScale));
        }
    }

    if (OutCoverage <= 0.0f)
    {
        return RimShellRadiance;
    }

    const float SinRadius = std::max(std::sin(Moon.AngularRadiusRad), 1e-6f);
    const Vec3  Offset    = Sub3(ViewDir, Mul3(MoonDir, cosθ));
    const float DiscA     = std::clamp(Dot3(Offset, East) / SinRadius, -0.999f, 0.999f);
    const float DiscB     = std::clamp(Dot3(Offset, North) / SinRadius, -0.999f, 0.999f);
    const float ρSq       = std::min(0.999f, DiscA * DiscA + DiscB * DiscB);
    const float μ         = std::sqrt(std::max(1e-4f, 1.0f - ρSq));

    const Vec3  Normal  = Normalize3(Add3(Add3(Mul3(East, DiscA), Mul3(North, DiscB)), Mul3(ObserverDir, μ)));
    const float sinφ₀   = std::sin(Moon.LibrationLatitudeRad);
    const float cosφ₀   = std::cos(Moon.LibrationLatitudeRad);
    const float φLat    = std::asin(std::clamp(μ * sinφ₀ + DiscB * cosφ₀, -1.0f, 1.0f));
    const float λLon    = Moon.SpinLongitudeRad + std::atan2(DiscA, μ * cosφ₀ - DiscB * sinφ₀);

    const Vec3 Albedo = LunarAtlas_.Valid()
                            ? LunarAtlas_.SampleLinearAlbedo(Moon.Preset, λLon, φLat)
                            : Vec3{0.12f, 0.118f, 0.112f};

    const float RawDotSun       = Dot3(Normal, SunDir);
    const float Wrap            = 0.30f * Haze;
    const float μ₀              = std::max(0.0f, (RawDotSun + Wrap) / (1.0f + Wrap));
    const float PhaseAngle      = std::acos(std::clamp(Dot3(ObserverDir, SunDir), -1.0f, 1.0f));
    const float LommelSeeliger  = μ₀ > 0.0f ? (2.0f * μ₀ / (μ₀ + μ)) : 0.0f;
    const float OppositionSurge = 1.0f + 0.38f * std::exp(-PhaseAngle / 0.18f);
    const float PhaseCurve      = std::exp(-0.95f * PhaseAngle) * OppositionSurge;
    const float LimbOptical     = (1.0f - μ) * (1.0f - μ) * Haze;
    const float SurfaceAtten    = 1.0f - 0.52f * LimbOptical;
    const float LitFactor       = (LommelSeeliger * PhaseCurve / π) * Moon.SurfaceBrightness * SurfaceAtten;

    const Vec3  DirectLit  = Mul3(SolarIrradianceRgb, LitFactor);
    const Vec3  Earthshine = {Moon.EarthshineStrength * 0.58f, Moon.EarthshineStrength * 0.72f, Moon.EarthshineStrength * 1.00f};
    Vec3        OnDisc     = Mul3Comp(Albedo, Add3(DirectLit, Earthshine));
    if (Haze > 1.0e-3f && μ₀ > 0.0f)
    {
        const float AtmoInScatter = (0.048f * Moon.SurfaceBrightness / π) * LimbOptical * (0.25f + 0.75f * μ₀);
        OnDisc = Add3(OnDisc, Mul3Comp(HazeTint, Mul3(SolarIrradianceRgb, AtmoInScatter)));
    }
    return Add3(Mul3(OnDisc, OutCoverage), RimShellRadiance);
}

Vec3 PlanetaryDomeProjection::EvaluateCustomStar(const CustomStarDescriptor& Star,
                                                 Vec3                        ViewDirectionUnit,
                                                 float                       PixelAngularSizeRad) noexcept
{
    if (Star.RadianceGain <= 0.0f)
    {
        return {};
    }
    const Vec3  ViewDir = Normalize3(ViewDirectionUnit);
    const Vec3  StarDir = Normalize3(Star.DirectionUnit);
    const float cosθ    = std::clamp(Dot3(ViewDir, StarDir), -1.0f, 1.0f);
    const float θRad    = std::acos(cosθ);
    const float σCore   = std::max(Star.AngularCoreRad, 0.65f * PixelAngularSizeRad);
    if (θRad > 24.0f * σCore)
    {
        return {};
    }

    const Vec3  Tint    = KelvinToBlackbodyChromaticity(Star.TemperatureKelvin);
    const float Core    = std::exp(-0.5f * (θRad * θRad) / (σCore * σCore));
    const float Corona  = Star.CoronaGlowStrength * 0.18f / (1.0f + std::pow(θRad / (2.5f * σCore), 2.2f));
    float       Spikes  = 0.0f;
    if (Star.SpikeStrength > 0.0f && θRad > 1e-6f)
    {
        const Vec3  RefUp = std::abs(StarDir.Z) < 0.9f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
        const Vec3  TangX = Normalize3(Cross3(RefUp, StarDir));
        const Vec3  TangY = Cross3(StarDir, TangX);
        const float ϕ     = std::atan2(Dot3(ViewDir, TangY), Dot3(ViewDir, TangX));
        const float Blade = std::pow(std::abs(std::cos(3.0f * ϕ)), 28.0f);
        Spikes            = Star.SpikeStrength * Blade * std::exp(-θRad / (8.0f * σCore));
    }
    return Mul3(Tint, Star.RadianceGain * (Core + Corona + Spikes));
}

//------------------------------------------------------------------------------------------------------------------------
//                           4. COMPLETE CELESTIAL DOME DIRECTION EVALUATION
//------------------------------------------------------------------------------------------------------------------------

PlanetaryDomeSample PlanetaryDomeProjection::EvaluateDirection(const PlanetaryDomeSpecification& Specification,
                                                               Vec3                              ViewDirectionUnit) const noexcept
{
    PlanetaryDomeSample Sample{};
    const Vec3  ViewDir  = Normalize3(ViewDirectionUnit);
    const Vec3  SunDir   = Normalize3(Specification.PrimarySunDirection);
    const float PixelRad = std::max(Specification.PixelAngularSizeRad, 2.5e-4f);

    Vec3 Background = EvaluateMilkyWayBandRadiance(ViewDir, Specification.MilkyWayStrength);

    if (!StarCells_.empty() && !BinnedStars_.empty())
    {
        float OctU = 0.5f, OctV = 0.5f;
        EncodeOctahedral(ViewDir, OctU, OctV);
        const int32_t CenterX = std::clamp(static_cast<int32_t>(OctU * OctahedralGridDim), 0, static_cast<int32_t>(OctahedralGridDim - 1u));
        const int32_t CenterY = std::clamp(static_cast<int32_t>(OctV * OctahedralGridDim), 0, static_cast<int32_t>(OctahedralGridDim - 1u));
        const float   σPsf    = 0.72f * PixelRad;
        const float   Cutoff  = 4.2f * σPsf;
        const float   CosCut  = std::cos(Cutoff);

        for (int32_t Dy = -1; Dy <= 1; ++Dy)
        {
            for (int32_t Dx = -1; Dx <= 1; ++Dx)
            {
                const int32_t CellX = (CenterX + Dx + static_cast<int32_t>(OctahedralGridDim)) % static_cast<int32_t>(OctahedralGridDim);
                const int32_t CellY = std::clamp(CenterY + Dy, 0, static_cast<int32_t>(OctahedralGridDim - 1u));
                const OctahedralCellSpan Span = StarCells_[static_cast<size_t>(CellY) * OctahedralGridDim + static_cast<size_t>(CellX)];
                for (uint32_t Idx = 0u; Idx < Span.Count; ++Idx)
                {
                    const BinnedCatalogueStar& Star = BinnedStars_[Span.First + Idx];
                    if (Star.Magnitude > Specification.LimitingMagnitude)
                    {
                        break;
                    }
                    const float cosθ = Dot3(ViewDir, Star.DirectionUnit);
                    if (cosθ < CosCut)
                    {
                        continue;
                    }
                    const float θRad    = std::acos(std::clamp(cosθ, -1.0f, 1.0f));
                    const float Psf     = std::exp(-0.5f * (θRad * θRad) / (σPsf * σPsf)) / (Twoπ * σPsf * σPsf);
                    const float Flux    = 2.2e-7f * std::pow(10.0f, -0.4f * Star.Magnitude);
                    const float Twinkle = EvaluateStarScintillation(
                        ViewDir.Z, Star.PhaseSeed, Specification.TimeSeconds, Specification.ScintillationStrength);
                    Background = Add3(Background, Mul3(Star.ChromaticityRgb, Flux * Psf * Twinkle));
                }
            }
        }
    }

    for (const CustomStarDescriptor& Custom : Specification.CustomStars)
    {
        Background = Add3(Background, EvaluateCustomStar(Custom, ViewDir, PixelRad));
    }

    Vec3  Accumulated  = Background;
    float TotalOpacity = 0.0f;

    for (const PlanetaryBodyDescriptor& Planet : Specification.Planets)
    {
        float Coverage = 0.0f;
        const Vec3 PlanetRad = EvaluatePlanetaryBody(
            Planet, ViewDir, SunDir, Specification.SolarIrradianceRgb, PixelRad, Coverage);
        if (Coverage > 0.0f || (PlanetRad.X + PlanetRad.Y + PlanetRad.Z) > 0.0f)
        {
            Accumulated  = Add3(Mul3(Accumulated, 1.0f - Coverage), PlanetRad);
            TotalOpacity = TotalOpacity + Coverage * (1.0f - TotalOpacity);
        }
    }

    for (const LunarBodyDescriptor& Moon : Specification.Moons)
    {
        float Coverage = 0.0f;
        const Vec3 MoonRad = EvaluateLunarBody(
            Moon, ViewDir, SunDir, Specification.SolarIrradianceRgb, PixelRad, Coverage);
        if (Coverage > 0.0f)
        {
            Accumulated  = Add3(Mul3(Accumulated, 1.0f - Coverage), MoonRad);
            TotalOpacity = TotalOpacity + Coverage * (1.0f - TotalOpacity);
        }
    }

    Sample.ExoAtmosphericRadiance = Accumulated;
    Sample.OpaqueBodyCoverage     = std::clamp(TotalOpacity, 0.0f, 1.0f);
    return Sample;
}

//------------------------------------------------------------------------------------------------------------------------
//                           5. GPU PLANETARY SKY RASTERIZATION IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
struct PushHeader
{
    uint32_t Counts[4];
    float    Tuning[4];
};
} // namespace

bool PlanetarySkyRasterization::Initialize(VulkanComputeExchange& Exchange,
                                           const std::string&     ShaderBinaryDirectory,
                                           const std::string&     LunaSourceJpgPath,
                                           const std::string&     HipparcosCsvPath,
                                           std::string*           OutDiagnostic)
{
    const std::array<VkDescriptorType, 9> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // Ensemble
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // StarCellSpans
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // StarRecords
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // TransLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // MultiLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // SkyViewLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // LunarAtlas (2048x1024x6 sRGB)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // VisibilityProjection
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // HdrFrame
    }};

    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/RasterizePlanetaryProjection.spv",
                                     "RasterizePlanetaryProjectionMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     Program_,
                                     OutDiagnostic))
    {
        return false;
    }

    if (!HostDome_.LoadLunarAtlas(LunaSourceJpgPath, 2048u, 1024u))
    {
        (void)HostDome_.LoadLunarAtlas("", 512u, 256u);
    }

    const LunarSixBodyAtlas& Atlas = HostDome_.LunarAtlas();
    if (!Exchange.AllocateSrgb8Array(Atlas.Width(), Atlas.Height(), LunarAtlasLayerCount, LunarAtlasImage_) ||
        !Exchange.UploadSrgb8Array(LunarAtlasImage_, Atlas.Rgba8SrgbBytes()))
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Failed to upload 6-body sRGB Lunar Atlas to Vulkan 2D-Array image";
        }
        return false;
    }

    if (!HipparcosCsvPath.empty())
    {
        if (!HostDome_.LoadHipparcosCatalogueCsv(HipparcosCsvPath))
        {
            HostDome_.BuildProceduralStarCatalogue(8000u);
        }
    }
    else
    {
        HostDome_.BuildProceduralStarCatalogue(8000u);
    }

    const auto& Cells = HostDome_.StarCells();
    const auto& Stars = HostDome_.BinnedStars();
    IndexedStarCount_ = HostDome_.SourceStarCount();

    std::vector<std::array<uint32_t, 2>> CellSpans(PlanetaryDomeProjection::OctahedralCellCount);
    for (size_t Index = 0u; Index < Cells.size(); ++Index)
    {
        CellSpans[Index] = {Cells[Index].First, Cells[Index].Count};
    }

    std::vector<std::array<float, 4>> PackedStars;
    PackedStars.reserve(std::max<size_t>(2u, Stars.size() * 2u));
    for (size_t Index = 0u; Index < Stars.size(); ++Index)
    {
        const BinnedCatalogueStar& Star = Stars[Index];
        const float                Seed = std::fmod(static_cast<float>(Index) * 0.6180339887f, 1.0f);
        PackedStars.push_back({Star.DirectionUnit.X, Star.DirectionUnit.Y, Star.DirectionUnit.Z, Star.Magnitude});
        PackedStars.push_back({Star.ChromaticityRgb.X, Star.ChromaticityRgb.Y, Star.ChromaticityRgb.Z, Seed});
    }
    if (PackedStars.empty())
    {
        PackedStars.push_back({0.0f, 1.0f, 0.0f, 20.0f});
        PackedStars.push_back({0.0f, 0.0f, 0.0f, 0.0f});
    }

    if (!Exchange.UploadMemorySpan(StarCellSpan_, CellSpans.data(), CellSpans.size() * sizeof(uint32_t) * 2u) ||
        !Exchange.UploadMemorySpan(StarRecordSpan_, PackedStars.data(), PackedStars.size() * sizeof(float) * 4u))
    {
        return false;
    }

    return true;
}

void PlanetarySkyRasterization::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseMemorySpan(StarRecordSpan_);
    Exchange.ReleaseMemorySpan(StarCellSpan_);
    Exchange.ReleaseImage(LunarAtlasImage_);
    Exchange.ReleaseProgram(Program_);
    IndexedStarCount_ = 0u;
}

bool PlanetarySkyRasterization::Execute(VulkanComputeExchange&      Exchange,
                                        const DeviceMemorySpan&     EnsembleSpan,
                                        const AtmosphereProjection& AtmosphereLuts,
                                        const DeviceImageArraySlot& VisibilityImage,
                                        DeviceImageArraySlot&       HdrFrameTarget,
                                        uint32_t                    ViewportWidth,
                                        uint32_t                    ViewportHeight)
{
    const PushHeader Push{{ViewportWidth, ViewportHeight, 0u, 0u}, {0.0f, 0.0f, 0.0f, 0.0f}};
    const std::array<ComputeDescriptorArgument, 9> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleSpan, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &StarCellSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &StarRecordSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.TransmittanceArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.MultiScatteringArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.SkyViewArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &LunarAtlasImage_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &VisibilityImage},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HdrFrameTarget},
    }};
    return Exchange.DispatchCompute(
        Program_, Args, &Push, sizeof(Push), (ViewportWidth + 7u) / 8u, (ViewportHeight + 7u) / 8u, 1u);
}

} // namespace Frontier
