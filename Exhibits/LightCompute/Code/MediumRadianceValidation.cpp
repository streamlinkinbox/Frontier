/*==========================================================================================================================================
                                               MEDIUMRADIANCEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validate typed cloud/fog shader inputs, bit-preserved seeds and domain rejection.

#include "MediumRadianceFixtures.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                   TEST CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

void Require(bool Condition, const char* Description)
{
    if (!Condition) throw std::runtime_error(Description);
}

template <class Function> bool Rejected(Function Operation)
{
    try { Operation(); }
    catch (const std::runtime_error&) { return true; }
    return false;
}

void WritePayload(const float* Payload, size_t FloatCount)
{
    Require(std::fwrite(Payload, sizeof(float), FloatCount, stdout) == FloatCount, "Binary output failed");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENCODING CHECKS
//------------------------------------------------------------------------------------------------------------------------

void ValidatePayloads()
{
    for (uint32_t Index = 0; Index < 64; ++Index)
    {
        const auto Specification = ConstructCloudRadianceFixture(Index);
        const auto Cloud = CompileCloudRadiancePayload(Specification);
        ValidateCloudRadiancePayload(Cloud.data(), Cloud.size());
        uint32_t LowBits = 0, HighBits = 0;
        std::memcpy(&LowBits, &Cloud[20], sizeof(LowBits));
        std::memcpy(&HighBits, &Cloud[21], sizeof(HighBits));
        Require(LowBits == uint32_t(Specification.Medium.Seed) &&
                HighBits == uint32_t(Specification.Medium.Seed >> 32), "Cloud seed bits changed");
        Require(Cloud[10] == 40000 && Cloud[11] == float(Index) && Cloud[15] == 6 &&
                Cloud[19] == float(Specification.ObserverHeightM), "Cloud shader positions differ");
    }
    const auto Fog = CompileFogRadiancePayload(ConstructFogRadianceFixture());
    const std::array<float, FogRadianceFloatCount> PriorFog{
        .0015f, 0, 450, .9f, .65f, 2500, 32, 32, .01f, .012f, .015f, 1, 1, 1, 40000, 0};
    Require(Fog == PriorFog, "Fog fixture differs from the former encoded constants");
    auto Disabled = ConstructFogRadianceFixture();
    Disabled.GeometryShadowsEnabled = false;
    Disabled.CloudShadowsEnabled = false;
    Disabled.AnalyticLightsEnabled = false;
    Disabled.FacetSamples = 4;
    const auto Controls = CompileFogRadiancePayload(Disabled);
    Require(Controls[11] == 0 && Controls[12] == 0 && Controls[13] == 0 && Controls[15] == 4,
            "Fog light switches differ");
    ValidateFogRadiancePayload(Controls.data(), Controls.size());
    std::printf("MEDIUM_PAYLOAD cloud_cases=64 seed_bits_exact=1 fog_constants_exact=1 switches=1\n");
}

void ValidateRejectedInputs()
{
    auto Cloud = CompileCloudRadiancePayload(ConstructCloudRadianceFixture(3));
    auto Fog = CompileFogRadiancePayload(ConstructFogRadianceFixture());
    uint32_t Rejections = 0;
    auto RejectCloud = [&](size_t Index, float Replacement)
    {
        auto Invalid = Cloud;
        Invalid[Index] = Replacement;
        Require(Rejected([&] { ValidateCloudRadiancePayload(Invalid.data(), Invalid.size()); }),
                "Malformed cloud input was accepted");
        ++Rejections;
    };
    RejectCloud(0, std::numeric_limits<float>::quiet_NaN());
    RejectCloud(1, Cloud[0]);
    RejectCloud(2, 1.1f);
    RejectCloud(3, -1);
    RejectCloud(4, .2f);
    RejectCloud(5, 1);
    RejectCloud(6, 1000001);
    RejectCloud(7, 17);
    RejectCloud(8, 101);
    RejectCloud(10, 100001);
    RejectCloud(11, 129);
    RejectCloud(11, 2.5f);
    RejectCloud(12, 0);
    RejectCloud(15, 9);
    RejectCloud(16, -1);
    RejectCloud(24, -1);
    Require(Rejected([&] { ValidateCloudRadiancePayload(nullptr, 28); }), "Null cloud span was accepted");
    ++Rejections;
    Require(Rejected([&] { ValidateCloudRadiancePayload(Cloud.data(), 27); }), "Short cloud span was accepted");
    ++Rejections;
    auto InvalidCloud = ConstructCloudRadianceFixture(0);
    InvalidCloud.ObserverHeightM = std::numeric_limits<double>::max();
    Require(Rejected([&] { (void)CompileCloudRadiancePayload(InvalidCloud); }), "Cloud float overflow was accepted");
    ++Rejections;
    auto RejectFog = [&](size_t Index, float Replacement)
    {
        auto Invalid = Fog;
        Invalid[Index] = Replacement;
        Require(Rejected([&] { ValidateFogRadiancePayload(Invalid.data(), Invalid.size()); }),
                "Malformed fog input was accepted");
        ++Rejections;
    };
    RejectFog(0, -1);
    RejectFog(1, 10001);
    RejectFog(2, 0);
    RejectFog(3, 1.1f);
    RejectFog(4, 1);
    RejectFog(5, 0);
    RejectFog(6, 129);
    RejectFog(7, 0);
    RejectFog(8, -1);
    RejectFog(11, .5f);
    RejectFog(12, 2);
    RejectFog(13, -1);
    RejectFog(14, 100001);
    RejectFog(15, 65);
    RejectFog(15, std::numeric_limits<float>::infinity());
    Require(Rejected([&] { ValidateFogRadiancePayload(nullptr, 16); }), "Null fog span was accepted");
    ++Rejections;
    Require(Rejected([&] { ValidateFogRadiancePayload(Fog.data(), 15); }), "Short fog span was accepted");
    ++Rejections;
    auto InvalidFog = ConstructFogRadianceFixture();
    InvalidFog.Medium.DensityAtBase = std::numeric_limits<double>::quiet_NaN();
    Require(Rejected([&] { (void)CompileFogRadiancePayload(InvalidFog); }), "Nonfinite fog input was accepted");
    ++Rejections;
    std::printf("MEDIUM_PAYLOAD_GUARDS rejected=%u cloud_and_fog=1\n", Rejections);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    try
    {
        if (Count == 2 && std::string(Arguments[1]) == "--binary")
        {
            for (uint32_t Index = 0; Index < 64; ++Index)
            {
                const auto Cloud = CompileCloudRadiancePayload(ConstructCloudRadianceFixture(Index));
                WritePayload(Cloud.data(), Cloud.size());
            }
            const auto Fog = CompileFogRadiancePayload(ConstructFogRadianceFixture());
            WritePayload(Fog.data(), Fog.size());
            return 0;
        }
        Require(Count == 1, "Unknown argument");
        ValidatePayloads();
        ValidateRejectedInputs();
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "Medium radiance validation failed: %s\n", Error.what());
        return 1;
    }
}
