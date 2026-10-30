/*============================================================================================================================================
                                                     FACETAREASCOREVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Verify compensated projection algebra,invalid intake and retained-fixture native target batches.
#include "Engine/Rendering/FacetAreaScore.h"
#include "FacetAreaProposalScenes.h"
#include <cstring>
#include <iostream>
#include <iomanip>
#include <limits>

namespace
{
uint32_t Checks = 0;
void Check(bool Accepted)
{
    ++Checks;
    if (!Accepted) throw std::runtime_error("Facet-area score validation failed");
}

long double ReferenceGeometry(const FacetAreaScoreInput& Input)
{
    long double Receiver = 0, Source = 0, Squared = 0;
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        const long double Delta = (static_cast<long double>(Input.Receiver[Axis]) - Input.Triangle.A[Axis]) -
            static_cast<long double>(Input.U) * (static_cast<long double>(Input.Triangle.B[Axis]) - Input.Triangle.A[Axis]) -
            static_cast<long double>(Input.V) * (static_cast<long double>(Input.Triangle.C[Axis]) - Input.Triangle.A[Axis]);
        Receiver -= Input.ReceiverNormal[Axis] * Delta;
        Source += Input.SourceNormal[Axis] * Delta;
        Squared += Delta * Delta;
    }
    return std::max(0.L, Receiver) * std::max(0.L, Source) / (std::acos(-1.L) * Squared * Squared);
}

void ValidateUnits()
{
    FacetAreaScoreInput Input;
    Input.Triangle = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    Input.Receiver = {0, 0, 1};
    const auto Original = EvaluateFacetAreaScore(Input);
    Check(Original.Accepted && std::abs(Original.Geometry[0] - 1 / std::acos(-1.)) < 2e-7);
    Check(Original.Target > Input.TargetFloor && Original.ReceiverProjection == 1 && Original.SquaredDistance == 1);
    auto Invalid = Input;
    Invalid.Receiver = {0, 0, 0};Check(!EvaluateFacetAreaScore(Invalid).Accepted);
    Invalid = Input;Invalid.U = -1;Check(!EvaluateFacetAreaScore(Invalid).Accepted);
    Invalid = Input;Invalid.U = Invalid.V = .6f;Check(!EvaluateFacetAreaScore(Invalid).Accepted);
    Invalid = Input;Invalid.TargetFloor = 0;Check(!EvaluateFacetAreaScore(Invalid).Accepted);
    Invalid = Input;Invalid.SourceNormal = {};Check(!EvaluateFacetAreaScore(Invalid).Accepted);
    for (float Bad : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        Invalid = Input;Invalid.SourceAlbedo[0] = Bad;Check(!EvaluateFacetAreaScore(Invalid).Accepted);
        Invalid = Input;Invalid.ReceiverAlbedo[1] = Bad;Check(!EvaluateFacetAreaScore(Invalid).Accepted);
        Invalid = Input;Invalid.LightRadiance[2] = Bad;Check(!EvaluateFacetAreaScore(Invalid).Accepted);
    }
    Invalid = Input;Invalid.Triangle.A[0] = std::numeric_limits<float>::infinity();
    Check(!EvaluateFacetAreaScore(Invalid).Accepted);
    Invalid = Input;Invalid.LightRadiance[0] = Invalid.SourceAlbedo[0] = std::numeric_limits<float>::max();
    Check(!EvaluateFacetAreaScore(Invalid).Accepted);
    for (uint32_t Mode = 0; Mode < 3; ++Mode)
    {
        auto Dark = Input;
        if (Mode == 0) Dark.SourceAlbedo = {};
        if (Mode == 1) Dark.SourceNormal = {0, 0, -1};
        if (Mode == 2) Dark.ReceiverNormal = {0, 0, 1};
        const auto Score = EvaluateFacetAreaScore(Dark);
        Check(Score.Accepted && Score.Target == Dark.TargetFloor && Score.ProxyRgb[0] == 0);
    }
    for (int Exponent = -4; Exponent <= 4; ++Exponent)
    {
        auto Scaled = Input;
        const float Scale = std::ldexp(1.f, Exponent);
        Scaled.Receiver[2] *= Scale;
        for (auto* Corner : {&Scaled.Triangle.A, &Scaled.Triangle.B, &Scaled.Triangle.C})
            for (float& Coordinate : *Corner) Coordinate *= Scale;
        const auto Score = EvaluateFacetAreaScore(Scaled);
        Check(Score.Accepted && std::abs(Score.Geometry[0] * Scale * Scale - Original.Geometry[0]) < 2e-7);
    }
    // 📝 Rounded world-point subtraction doubles the true unit separation at this translated triangle.
    auto Translated = Input;
    Translated.Triangle = {{0, 0, 16777216}, {2, 0, 16777218}, {0, 2, 16777216}};
    Translated.U = .5f;Translated.V = .25f;Translated.Receiver = {1, .5f, 16777218};
    const auto Stable = EvaluateFacetAreaScore(Translated);
    Check(Stable.Accepted && Stable.SquaredDistance == 1);
    const float RoundedPoint = Translated.Triangle.A[2] + Translated.U *
        (Translated.Triangle.B[2] - Translated.Triangle.A[2]);
    Check(Translated.Receiver[2] - RoundedPoint == 2);
    Check(std::abs(Stable.Geometry[0] / ReferenceGeometry(Translated) - 1) < 2e-6L);
    // 📝 Rounded dot products cancel to zero;the product low words retain a positive grazing projection.
    auto Grazing = Input;
    Grazing.Receiver = {4, -3, 0};Grazing.ReceiverNormal = {-.6f, -.8f, 0};Grazing.SourceNormal = {1, 0, 0};
    const auto Compensated = EvaluateFacetAreaScore(Grazing);
    const float Naive = (.6f * 4.f) + (.8f * -3.f);
    Check(Compensated.Accepted && Compensated.ReceiverProjection > 0 && Naive == 0);
    Check(std::abs(Compensated.Geometry[0] / ReferenceGeometry(Grazing) - 1) < 2e-6L);
    std::cout << std::setprecision(17) << "FACET_AREA_SCORE_CANCELLATION compensated_distance=1 naive_distance="
              << Translated.Receiver[2] - RoundedPoint << " grazing_projection=" << Compensated.ReceiverProjection
              << " naive_projection=" << Naive << " geometry_relative_error="
              << std::abs(Compensated.Geometry[0] / ReferenceGeometry(Grazing) - 1) << '\n';
    Invalid = Input;Invalid.SourceAlbedo[0] = Invalid.LightRadiance[0] = std::numeric_limits<float>::denorm_min();
    Check(!EvaluateFacetAreaScore(Invalid).Accepted);
    for (uint32_t Index = 0; Index < 1024; ++Index)
    {
        auto Sample = Input;
        Sample.U = float(Index % 17) / 64;Sample.V = float(Index % 23) / 64;
        Sample.Receiver = {float(int(Index % 31) - 15) / 8, float(int(Index % 29) - 14) / 8,
                           .125f + float(Index % 37) / 8};
        const auto Score = EvaluateFacetAreaScore(Sample);
        Check(Score.Accepted && std::abs(Score.Geometry[0] / ReferenceGeometry(Sample) - 1) < 2e-5L);
    }
    std::cout << "FACET_AREA_SCORE_UNIT checks=" << Checks << " pass=1\n";
}

void ExportFixtures(const char* Path)
{
    std::ofstream Output(Path, std::ios::binary);
    for (uint32_t Corpus = 0; Corpus < 3; ++Corpus)
        for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
        {
            const auto Scene = Corpus == 2 ? ConstructDeformedAreaScene(Fixture) : ConstructGeometricAreaScene(Fixture);
            for (const auto& Facet : Scene.Surfaces)
            {
                Check(Facet.Emissive[3] == 0);
                for (const auto& Vector : {Facet.A, Facet.B, Facet.C, Facet.NormalA, Facet.NormalB, Facet.NormalC,
                                          Facet.AlbedoRoughness})
                    Output.write(reinterpret_cast<const char*>(Vector.data()), 12);
            }
        }
    Output.close();Check(bool(Output));
    std::cout << "FACET_AREA_SCORE_FIXTURES count=9 facets=144 material_override=0 pass=1\n";
}

void Replay(const char* InputPath, const char* OutputPath)
{
    const uint16_t Endian = 1;
    Check(*reinterpret_cast<const uint8_t*>(&Endian) == 1 && sizeof(float) == 4);
    std::ifstream Source(InputPath, std::ios::binary);
    std::ofstream Output(OutputPath, std::ios::binary);
    Check(bool(Source) && bool(Output));
    std::array<float, 26> Row{};
    uint64_t Pairs = 0;
    while (Source.read(reinterpret_cast<char*>(Row.data()), sizeof(Row)))
    {
        FacetAreaScoreInput Input;
        uint32_t Offset = 0;
        for (auto* Vector : {&Input.Triangle.A, &Input.Triangle.B, &Input.Triangle.C, &Input.SourceNormal,
                            &Input.SourceAlbedo, &Input.Receiver, &Input.ReceiverNormal, &Input.ReceiverAlbedo})
            for (float& Coordinate : *Vector) Coordinate = Row[Offset++];
        Input.U = Row[24];Input.V = Row[25];
        const auto Score = EvaluateFacetAreaScore(Input);
        const std::array<uint32_t, 2> Ordinals{uint32_t(Score.Accepted), 0};
        const std::array<float, 10> Measures{Score.Geometry[0], Score.Geometry[1], Score.Geometry[2],
            Score.ProxyRgb[0], Score.ProxyRgb[1], Score.ProxyRgb[2], Score.Target, Score.ReceiverProjection,
            Score.SourceProjection, Score.SquaredDistance};
        Output.write(reinterpret_cast<const char*>(Ordinals.data()), sizeof(Ordinals));
        Output.write(reinterpret_cast<const char*>(Measures.data()), sizeof(Measures));
        ++Pairs;
    }
    Check(Source.eof() && Source.gcount() == 0);
    Output.close();Check(bool(Output));
    std::cout << "FACET_AREA_SCORE_REPLAY proposals=" << Pairs << " pass=1\n";
}
}

int main(int Count, char** Arguments)
{
    try
    {
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0) ValidateUnits();
        else if (Count == 3 && std::strcmp(Arguments[1], "--fixtures") == 0) ExportFixtures(Arguments[2]);
        else if (Count == 3) Replay(Arguments[1], Arguments[2]);
        else throw std::runtime_error("Use --unit,--fixtures output.bin,or input.bin output.bin");
        return 0;
    }
    catch (const std::exception& Error) { std::cerr << Error.what() << '\n';return 1; }
}
