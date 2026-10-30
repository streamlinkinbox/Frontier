/*============================================================================================================================================
                                                     FACETCDFSHADERVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Verify typed profile extents and run the unchanged GPU corpus with maintained CDF shaders.

#include "FacetCdfShaderComparisons.h"

namespace
{
void ValidateShaderProfileUnits()
{
    uint32_t Checks = 0;
    const auto Validate = [&](bool Condition) {
        ++Checks;
        Require(Condition, "Typed facet CDF header differs");
    };
    Validate(sizeof(FacetCdfProfileHeader) == 16 && alignof(FacetCdfProfileHeader) == 4);
    const std::array<uint32_t, 6> Sizes{1, 3, 7, 16, 31, 65};
    const std::array<uint32_t, 6> Capacities{1, 1, 2, 4, 8, 17};
    for (uint32_t Ordinal = 0; Ordinal < Sizes.size(); ++Ordinal)
    {
        const auto Header = ConstructFacetCdfHeader(Sizes[Ordinal], 1024);
        Validate(Header.SourceCount == Sizes[Ordinal] && Header.CameraCount == 1024);
        Validate(Header.SourceStart == 1025 && Header.QuartetCapacity == Capacities[Ordinal]);
        std::array<uint32_t, 4> Words{};
        std::memcpy(Words.data(), &Header, sizeof(Header));
        Validate(Words == std::array<uint32_t, 4>{Sizes[Ordinal], 1024, 1025, Capacities[Ordinal]});
    }
    const auto SingleCamera = ConstructFacetCdfHeader(65, 1);
    Validate(SingleCamera.SourceCount > SingleCamera.CameraCount && SingleCamera.SourceStart == 2);
    Validate(ConstructFacetCdfHeader(1, 0).SourceStart == 1);
    Validate(ConstructFacetCdfHeader(16777216, 0).QuartetCapacity == 4194304);
    Validate(ConstructFacetCdfHeader(1, UINT32_MAX - 2).SourceStart == UINT32_MAX - 1);
    for (const auto Counts : {std::array<uint32_t, 2>{0, 1024}, {16777217, 0}, {UINT32_MAX, 0},
                              {1, UINT32_MAX}, {1, UINT32_MAX - 1}, {65, UINT32_MAX - 17}})
    {
        bool Rejected = false;
        try { ConstructFacetCdfHeader(Counts[0], Counts[1]); }
        catch (const std::invalid_argument &) { Rejected = true; }
        Validate(Rejected);
    }
    std::cout << "CDF_SHADER_UNIT checks=" << Checks << " pass=1\n";
}
} // namespace

int main(int Count, char **Arguments)
{
    try
    {
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0)
        {
            ValidateShaderProfileUnits();
            return 0;
        }
        return ExecuteSubmissionCorpus(Count, Arguments);
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 2;
    }
}
