/*==========================================================================================================================================
                                               RADIANTFACETVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Analytic probabilities, malformed input rejection and historical distribution comparison.

#include "Engine/Rendering/RadiantFacetDistribution.h"
#include "RadiantFacetFixtures.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                  TEST CONFIGURATION
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

PathReplayFacet ConstructFacet(float Length, float Width, float Brightness)
{
    PathReplayFacet Facet;
    Facet.B = {Length, 0, 0, 0};
    Facet.C = {0, Width, 0, 0};
    Facet.Emissive = {Brightness, Brightness, Brightness, 0};
    return Facet;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DISTRIBUTION CHECKS
//------------------------------------------------------------------------------------------------------------------------

void ValidateDistribution()
{
    std::vector<PathReplayFacet> Facets{
        ConstructFacet(2, 1, 1), ConstructFacet(4, 1, 1), ConstructFacet(2, 1, 0),
        ConstructFacet(0, 1, 8), ConstructFacet(1, 1, 1e-30f)};
    const auto Distribution = CompileRadiantFacetDistribution(Facets);
    Require(Distribution.size() == 13 && Distribution[0] == 3, "Incorrect radiant facet count");
    Require(Distribution[1] == 0 && Distribution[5] == 1 && Distribution[9] == 4,
            "Incorrect source indices");
    Require(Distribution[3] == 1 && Distribution[7] == 2 && Distribution[11] == .5f,
            "Incorrect geometric facet areas");
    const double Tick = 1.0 / RadiantFacetProbabilityTicks;
    Require(std::abs(Distribution[2] - 1.0 / 3.0) < 3 * Tick &&
            std::abs(Distribution[6] - 2.0 / 3.0) < 3 * Tick,
            "Area/luminance probability differs from analytic ratio");
    Require(Distribution[10] == float(Tick) && Distribution[12] == 1,
            "Dim facet lost its one-tick support");
    ValidateRadiantFacetDistribution(Distribution, 0, Facets.size());
    std::vector<float> Atlas{8, 7, 6};
    Atlas.insert(Atlas.end(), Distribution.begin(), Distribution.end());
    ValidateRadiantFacetDistribution(Atlas, 3, Facets.size());
    const auto Empty = CompileRadiantFacetDistribution({});
    Require(Empty.size() == 1 && Empty[0] == 0, "Empty distribution differs from the shader contract");
    ValidateRadiantFacetDistribution(Empty, 0, 0);
    const auto Large = CompileRadiantFacetDistribution(ConstructRadiantFacetFixture());
    ValidateRadiantFacetDistribution(Large, 0, 4096);
    Require(Large == CompileRadiantFacetDistribution(ConstructRadiantFacetFixture()),
            "Repeated compilation differs");
    std::printf("RADIANT_FACETS analytic_count=3 area_ratio=1:2 tiny_ticks=1 generated=%u floats=%zu\n",
                unsigned(Large[0]), Large.size());
}

void ValidateRejectedInputs()
{
    std::vector<PathReplayFacet> Facets{ConstructFacet(1, 1, 1), ConstructFacet(2, 1, 2)};
    const auto Distribution = CompileRadiantFacetDistribution(Facets);
    uint32_t Rejections = 0;
    auto RejectAtlas = [&](size_t Index, float Replacement)
    {
        auto Invalid = Distribution;
        Invalid[Index] = Replacement;
        Require(Rejected([&] { ValidateRadiantFacetDistribution(Invalid, 0, Facets.size()); }),
                "Malformed distribution was accepted");
        ++Rejections;
    };
    RejectAtlas(0, -1);
    RejectAtlas(0, 1.5f);
    RejectAtlas(0, std::numeric_limits<float>::infinity());
    RejectAtlas(0, 3);
    RejectAtlas(1, 0.5f);
    RejectAtlas(1, 2);
    RejectAtlas(2, 0);
    RejectAtlas(2, std::numeric_limits<float>::quiet_NaN());
    RejectAtlas(3, 0);
    RejectAtlas(4, 0);
    RejectAtlas(8, .9f);
    auto Truncated = Distribution;
    Truncated.pop_back();
    Require(Rejected([&] { ValidateRadiantFacetDistribution(Truncated, 0, Facets.size()); }),
            "Truncated distribution was accepted");
    ++Rejections;
    Require(Rejected([&] { ValidateRadiantFacetDistribution(Distribution, Distribution.size(), Facets.size()); }),
            "Out-of-range distribution offset was accepted");
    ++Rejections;
    Facets[0].Emissive[1] = -1;
    Require(Rejected([&] { (void)CompileRadiantFacetDistribution(Facets); }), "Negative radiance was accepted");
    ++Rejections;
    Facets[0].Emissive[1] = std::numeric_limits<float>::infinity();
    Require(Rejected([&] { (void)CompileRadiantFacetDistribution(Facets); }), "Infinite radiance was accepted");
    ++Rejections;
    Facets[0].Emissive[1] = 1;
    Facets[0].A[2] = std::numeric_limits<float>::quiet_NaN();
    Require(Rejected([&] { (void)CompileRadiantFacetDistribution(Facets); }), "Nonfinite position was accepted");
    ++Rejections;
    Facets[0] = ConstructFacet(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 1);
    Require(Rejected([&] { (void)CompileRadiantFacetDistribution(Facets); }), "Overflowing area was accepted");
    ++Rejections;
    Facets[0] = ConstructFacet(std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), 1);
    Require(Rejected([&] { (void)CompileRadiantFacetDistribution(Facets); }), "Unrepresentable area was accepted");
    ++Rejections;
    std::printf("RADIANT_FACET_GUARDS rejected=%u malformed_and_unrepresentable=1\n", Rejections);
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
            const auto Distribution = CompileRadiantFacetDistribution(ConstructRadiantFacetFixture());
            Require(std::fwrite(Distribution.data(), sizeof(float), Distribution.size(), stdout) == Distribution.size(),
                    "Binary distribution output failed");
            return 0;
        }
        Require(Count == 1, "Unknown argument");
        ValidateDistribution();
        ValidateRejectedInputs();
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "Radiant facet validation failed: %s\n", Error.what());
        return 1;
    }
}
