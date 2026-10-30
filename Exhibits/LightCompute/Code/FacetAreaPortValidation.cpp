/*============================================================================================================================================
                                                       FACETAREAPORTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Verify the opt-in engine selector,analytic expectations and capture-driven pair equivalence.

#include "Engine/Rendering/FacetAreaReservoir.h"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using Frontier::FacetAreaMeasure;
using Frontier::SelectFacetAreaPair;

//------------------------------------------------------------------------------------------------------------------------
//                                                    FOCUSED CHECKS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
uint32_t Checks = 0;

void Require(bool Accepted)
{
    ++Checks;
    if (!Accepted) throw std::runtime_error("Facet-area selector validation failed");
}

void ValidateUnits()
{
    const std::array<FacetAreaMeasure, 2> Pair{{{2, .25}, {3, .5}}};
    const double Threshold = 8. / 14.;
    const auto First = SelectFacetAreaPair(Pair, std::nextafter(Threshold, 0.));
    const auto Second = SelectFacetAreaPair(Pair, Threshold);
    Require(First.Accepted && Second.Accepted && First.Selected == 0 && Second.Selected == 1);
    Require(First.CandidateCount == 2 && First.WeightSum == 14 && First.Normalizer == 3.5);
    Require(Second.Normalizer == 14. / 6.);
    const double Expectation = Threshold * 5 * First.Normalizer + (1 - Threshold) * 7 * Second.Normalizer;
    Require(std::abs(Expectation - (5 / .25 + 7 / .5) / 2) < 1e-13);

    const auto Empty = SelectFacetAreaPair({{{0, 0}, {0, 1}}}, .7);
    Require(Empty.Accepted && Empty.Selected == UINT32_MAX && Empty.WeightSum == 0 && Empty.Normalizer == 0);
    const auto NullFirst = SelectFacetAreaPair({{{0, 0}, {2, .25}}}, 0);
    const auto NullSecond = SelectFacetAreaPair({{{2, .25}, {0, 0}}}, std::nextafter(1., 0.));
    Require(NullFirst.Accepted && NullFirst.Selected == 1 && NullFirst.Normalizer == 2);
    Require(NullSecond.Accepted && NullSecond.Selected == 0 && NullSecond.Normalizer == 2);

    const double Infinity = std::numeric_limits<double>::infinity();
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    for (double Uniform : {-1., 1., Infinity, -Infinity, NaN})
        Require(!SelectFacetAreaPair(Pair, Uniform).Accepted);
    for (double Invalid : {-1., Infinity, -Infinity, NaN})
    {
        Require(!SelectFacetAreaPair({{{Invalid, 1}, {1, 1}}}, .5).Accepted);
        Require(!SelectFacetAreaPair({{{1, Invalid}, {1, 1}}}, .5).Accepted);
    }
    Require(!SelectFacetAreaPair({{{1, 0}, {1, 1}}}, .5).Accepted);
    const double Maximum = std::numeric_limits<double>::max();
    const double Minimum = std::numeric_limits<double>::denorm_min();
    Require(!SelectFacetAreaPair({{{Maximum, Minimum}, {1, 1}}}, .5).Accepted);
    Require(!SelectFacetAreaPair({{{Minimum, Maximum}, {1, 1}}}, .5).Accepted);
    Require(!SelectFacetAreaPair({{{Maximum, 1}, {Maximum, 1}}}, .5).Accepted);
    Require(!SelectFacetAreaPair({{{Minimum, Minimum}, {1, 1}}}, 0).Accepted);
    const auto VanishingProbability = SelectFacetAreaPair({{{1, Maximum}, {1e308, 1}}}, 0);
    Require(VanishingProbability.Accepted && VanishingProbability.Selected == 0);
    const auto Tiny = SelectFacetAreaPair({{{Minimum, 1}, {Minimum, 1}}}, .5);
    Require(Tiny.Accepted && Tiny.Selected == 1 && Tiny.Normalizer == 1);
    const auto Huge = SelectFacetAreaPair({{{1e308, 1e308}, {1e308, 1e308}}}, .5);
    Require(Huge.Accepted && Huge.Normalizer > 0 && std::isfinite(Huge.Normalizer));
    for (int Exponent = -200; Exponent <= 200; ++Exponent)
    {
        const double Scale = std::ldexp(1., Exponent);
        const auto Scaled = SelectFacetAreaPair({{{2 * Scale, .25}, {3 * Scale, .5}}}, .3);
        Require(Scaled.Accepted && Scaled.Selected == First.Selected);
        Require(std::abs(Scaled.Normalizer - First.Normalizer) < 1e-14);
    }
    // 📝 Midpoint grid integrates the exact two-point law;it is not a Monte Carlo confidence test.
    double Integrated = 0;
    for (uint32_t Index = 0; Index < 14000; ++Index)
    {
        const auto Chosen = SelectFacetAreaPair(Pair, (Index + .5) / 14000);
        Require(Chosen.Accepted);
        Integrated += (Chosen.Selected == 0 ? 5 : 7) * Chosen.Normalizer;
    }
    Require(std::abs(Integrated / 14000 - 17) < 1e-10);
    std::cout << "FACET_AREA_PORT_UNIT checks=" << Checks << " pass=1\n";
}

void Replay(const char* Input, const char* Output)
{
    const uint16_t Endian = 1;
    Require(*reinterpret_cast<const uint8_t*>(&Endian) == 1 && sizeof(double) == 8);
    std::ifstream Source(Input, std::ios::binary);
    std::ofstream Capture(Output, std::ios::binary);
    Require(bool(Source) && bool(Capture));
    uint64_t Pairs = 0;
    std::array<double, 5> Row{};
    while (Source.read(reinterpret_cast<char*>(Row.data()), sizeof(Row)))
    {
        const auto Chosen = SelectFacetAreaPair({{{Row[0], Row[1]}, {Row[2], Row[3]}}}, Row[4]);
        const std::array<uint32_t, 4> Ordinals{Chosen.Selected, uint32_t(Chosen.Accepted), Chosen.CandidateCount, 0};
        const std::array<double, 2> Measures{Chosen.WeightSum, Chosen.Normalizer};
        Capture.write(reinterpret_cast<const char*>(Ordinals.data()), sizeof(Ordinals));
        Capture.write(reinterpret_cast<const char*>(Measures.data()), sizeof(Measures));
        ++Pairs;
    }
    Require(Source.eof() && Source.gcount() == 0 && bool(Capture));
    std::cout << "FACET_AREA_PORT_REPLAY pairs=" << Pairs << " pass=1\n";
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    try
    {
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0) ValidateUnits();
        else if (Count == 3) Replay(Arguments[1], Arguments[2]);
        else throw std::runtime_error("Use --unit or input.bin output.bin");
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
