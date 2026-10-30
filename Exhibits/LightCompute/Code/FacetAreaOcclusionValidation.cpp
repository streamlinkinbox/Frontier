/*============================================================================================================================================
                                                    FACETAREAOCCLUSIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Isolate native suffix/connection visibility using predeclared analytic blockers and positive diffuse transport.
//----------------------------------------------------------------------------------------------------------------------
//                                                       INCLUDES
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Rendering/FacetAreaReservoir.h"
#include "Engine/Rendering/FacetAreaTransport.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

//----------------------------------------------------------------------------------------------------------------------
//                                                 TYPES AND CONSTANTS
//----------------------------------------------------------------------------------------------------------------------

using namespace Frontier;
namespace
{
using RayVector = std::array<float, 3>;
using Quartet   = std::array<float, 4>;
uint64_t Checks = 0, Paths = 0, Visibility = 0, Primaries = 0;
void VerifyOcclusion(bool Accepted)
{
    ++Checks;
    if (!Accepted)
        throw std::runtime_error("Analytic occlusion validation failed at check=" + std::to_string(Checks));
}
struct OcclusionLayout
{
    float Scale;
    RayVector Offset, Albedo;
};
const std::array<OcclusionLayout, 3> Layouts{{{1, {0, 0, 0}, {.7f, .4f, .2f}},
                                              {.75f, {-1.5f, .4f, .3f}, {.2f, .7f, .3f}},
                                              {1.5f, {.7f, -.6f, -.2f}, {.25f, .35f, .8f}}}};
struct OcclusionScene
{
    FacetAreaTransport Transport;
    FacetAreaProposals Proposals;
    TriangleVisibilitySpace DoublePrecisionBvh;
    std::vector<FacetAreaShadingTriangle> Surfaces;
    std::array<uint32_t, 2> SourceIndices{};
};
//----------------------------------------------------------------------------------------------------------------------
//                                         GEOMETRY AND INDEPENDENT VISIBILITY
//----------------------------------------------------------------------------------------------------------------------

RayVector TransformWorldPoint(const OcclusionLayout &Layout, float X, float Y, float Z)
{
    return {Layout.Offset[0] + Layout.Scale * X, Layout.Offset[1] + Layout.Scale * Y,
            Layout.Offset[2] + Layout.Scale * Z};
}
SpatialVector ConvertSpatialVector(const RayVector &P)
{
    return {P[0], P[1], P[2]};
}
OcclusionScene ConstructOcclusionScene(uint32_t LayoutIndex, uint32_t Variant)
{
    const auto &Layout = Layouts[LayoutIndex];
    std::vector<FacetAreaTriangle> Triangles;
    auto AssembleRectangle = [&](float LowX, float HighX, float LowY, float HighY, float Z)
    {
        const auto A = TransformWorldPoint(Layout, LowX, LowY, Z), B = TransformWorldPoint(Layout, HighX, LowY, Z);
        const auto C = TransformWorldPoint(Layout, HighX, HighY, Z), D = TransformWorldPoint(Layout, LowX, HighY, Z);
        Triangles.push_back({A, B, C});
        Triangles.push_back({A, C, D});
    };
    AssembleRectangle(-.25f, .25f, -.25f, .25f, 0);
    AssembleRectangle(-.3f, .3f, -.3f, .3f, 2);
    if (Variant & 1)
        AssembleRectangle(-.6f, .6f, -.6f, .6f, 1);
    if (Variant & 2)
        AssembleRectangle(.95f, 1.75f, -.6f, .6f, 1);
    std::vector<std::array<SpatialVector, 3>> DoubleTriangles;
    for (const auto &T : Triangles)
        DoubleTriangles.push_back({ConvertSpatialVector(T.A), ConvertSpatialVector(T.B), ConvertSpatialVector(T.C)});
    OcclusionScene Scene;
    Scene.DoublePrecisionBvh.Build(DoubleTriangles);
    const auto Packed = Scene.DoublePrecisionBvh.PackFloat32();
    for (uint32_t Index = 0; Index < Packed.Corners.size(); ++Index)
    {
        const uint32_t Original = Packed.OriginalFacets[Index];
        FacetAreaShadingTriangle S;
        S.Triangle             = Triangles[Original];
        const RayVector Normal = Original == 2 || Original == 3 ? RayVector{0, 0, -1} : RayVector{0, 0, 1};
        S.Normals              = {Normal, Normal, Normal};
        S.Albedo = Original < 2 ? Layout.Albedo : Original < 4 ? RayVector{.6f, .5f, .4f} : RayVector{0, 0, 0};
        Scene.Surfaces.push_back(S);
        if (Original < 2)
            Scene.SourceIndices[Original] = Index;
    }
    VerifyOcclusion(Scene.Transport.Prepare(Scene.Surfaces, Packed));
    VerifyOcclusion(Scene.Proposals.Prepare({Triangles[0], Triangles[1]}));
    return Scene;
}

void ValidatePath(const OcclusionScene &Scene, const FacetAreaPath &Path, uint32_t Variant)
{
    VerifyOcclusion(Path.Accepted);
    const bool SuffixExpected = (Variant & 2) == 0, ConnectionExpected = (Variant & 1) == 0;
    VerifyOcclusion(Path.SuffixVisible == SuffixExpected && Path.ConnectionVisible == ConnectionExpected);
    const bool SuffixDouble     = !Scene.DoublePrecisionBvh.Occluded(ConvertSpatialVector(Path.Origin),
                                                                     {double(.8f), 0, double(.6f)}, .00001f, 100);
    const bool ConnectionDouble = !Scene.DoublePrecisionBvh.Occluded(
        ConvertSpatialVector(Path.Origin), ConvertSpatialVector(Path.Direction), .00001f, Path.Maximum);
    VerifyOcclusion(SuffixDouble == SuffixExpected && ConnectionDouble == ConnectionExpected);
    ++Paths;
    Visibility += 2;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                  FOCUSED VALIDATION
//----------------------------------------------------------------------------------------------------------------------

void ValidateUnits()
{
    for (uint32_t Layout = 0; Layout < 3; ++Layout)
        for (uint32_t Variant = 0; Variant < 4; ++Variant)
        {
            const auto Scene   = ConstructOcclusionScene(Layout, Variant);
            const auto O       = TransformWorldPoint(Layouts[Layout], 0, 0, 1.5f);
            const auto Primary = Scene.Transport.Primary(O, {0, 0, 1});
            VerifyOcclusion(Primary.Accepted);
            VerifyOcclusion(Scene.Transport.Trace(O, {0, 0, 1}, 0, .1f).Index == UINT32_MAX);
            auto P          = Scene.Proposals.Draw(0x892c41u, 0, FacetAreaMeasureCategory::UniformFacet);
            P.Index         = Scene.SourceIndices[P.Index];
            const auto Path = Scene.Transport.Connect(P, Primary);
            ValidatePath(Scene, Path, Variant);
            const float Plane        = Layouts[Layout].Offset[2] + Layouts[Layout].Scale;
            const float SuffixTravel = (Plane - Path.Origin[2]) / .6f;
            const float SuffixX      = Path.Origin[0] + .8f * SuffixTravel;
            VerifyOcclusion(SuffixTravel > 0 && SuffixX > TransformWorldPoint(Layouts[Layout], .95f, 0, 1)[0] &&
                            SuffixX < TransformWorldPoint(Layouts[Layout], 1.75f, 0, 1)[0]);
            VerifyOcclusion(!Scene.Transport.Trace(O, {0, 0, 0}, 0, 100).Accepted);
            const auto Centre = TransformWorldPoint(Layouts[Layout], 0, 0, 0);
            const auto CentralConnection =
                Scene.Transport.Trace(Centre, {0, 0, 1}, .00001f, 1.5f * Layouts[Layout].Scale);
            VerifyOcclusion(CentralConnection.Accepted && (CentralConnection.Index != UINT32_MAX) == bool(Variant & 1));
            const auto CentralSuffix =
                Scene.Transport.Trace(TransformWorldPoint(Layouts[Layout], 1.35f - 4.f / 3.f, 0, 0), {.8f, 0, .6f},
                                      .00001f, 2.5f * Layouts[Layout].Scale);
            VerifyOcclusion(CentralSuffix.Accepted && (CentralSuffix.Index != UINT32_MAX) == bool(Variant & 2));
        }
    std::cout << "ANALYTIC_OCCLUSION_UNIT checks=" << Checks << " paths=" << Paths << " pass=1\n";
}

//----------------------------------------------------------------------------------------------------------------------
//                                                FRESH NATIVE CAPTURES
//----------------------------------------------------------------------------------------------------------------------

void EncodeCaptures(const std::string &Directory)
{
    constexpr uint32_t Count = 12288;
    std::filesystem::create_directories(Directory);
    for (uint32_t Layout = 0; Layout < 3; ++Layout)
        for (uint32_t Variant = 0; Variant < 4; ++Variant)
        {
            const auto Scene = ConstructOcclusionScene(Layout, Variant);
            std::vector<Quartet> Output(5 * Count);
            for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
            {
                const uint32_t Method = Pixel / 4096, Packet = Pixel % 4096, ImagePixel = Packet % 1024;
                const float X      = -.2f + .4f * (float(ImagePixel % 32) + .5f) / 32;
                const float Y      = -.2f + .4f * (float(ImagePixel / 32) + .5f) / 32;
                const auto Camera  = TransformWorldPoint(Layouts[Layout], X, Y, 1.5f);
                const auto Primary = Scene.Transport.Primary(Camera, {0, 0, 1});
                VerifyOcclusion(Primary.Accepted);
                ++Primaries;
                const auto Expected = TransformWorldPoint(Layouts[Layout], X, Y, 2);
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    VerifyOcclusion(std::abs(Primary.Position[Axis] - Expected[Axis]) < 2e-5);
                    VerifyOcclusion(std::abs(Primary.Normal[Axis] - (Axis == 2 ? -1.f : 0.f)) < 2e-5);
                    VerifyOcclusion(Primary.Albedo[Axis] == std::array<float, 3>{.6f, .5f, .4f}[Axis]);
                    Output[4 * Count + Pixel][Axis] = Primary.Position[Axis];
                }
                const uint32_t Seed = HashFacetAreaBits((Packet + 0x82c4d719u) ^ (Layout * 0x6c8e9cf5u));
                for (uint32_t Lane = 0; Lane < 4; ++Lane)
                {
                    const uint32_t CandidateCount = Method == 1 ? 1 : 2;
                    std::array<FacetAreaProposal, 2> P{};
                    std::array<FacetAreaScore, 2> Scores{};
                    for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
                    {
                        P[Ordinal] =
                            Scene.Proposals.Draw(Seed, 2 * Lane + Ordinal, FacetAreaMeasureCategory::UniformFacet);
                        P[Ordinal].Index  = Scene.SourceIndices[P[Ordinal].Index];
                        const auto Source = Scene.Transport.Source(P[Ordinal]);
                        VerifyOcclusion(Source.Accepted);
                        FacetAreaScoreInput Input;
                        Input.Triangle       = Scene.Surfaces[P[Ordinal].Index].Triangle;
                        Input.U              = P[Ordinal].U;
                        Input.V              = P[Ordinal].V;
                        Input.SourceNormal   = Source.Normal;
                        Input.SourceAlbedo   = Source.Albedo;
                        Input.Receiver       = Primary.Position;
                        Input.ReceiverNormal = Primary.Normal;
                        Input.ReceiverAlbedo = Primary.Albedo;
                        Scores[Ordinal]      = EvaluateFacetAreaScore(Input);
                        VerifyOcclusion(Scores[Ordinal].Accepted);
                    }
                    uint32_t Selected = 0;
                    float Z           = 0;
                    if (Method == 0)
                    {
                        const auto Pair =
                            SelectFacetAreaPair({{{Scores[0].Target, P[0].Density}, {Scores[1].Target, P[1].Density}}},
                                                DrawFacetAreaSelectionUniform(Seed, Lane));
                        VerifyOcclusion(Pair.Accepted);
                        Selected = Pair.Selected;
                        Z        = float(Pair.Normalizer);
                    }
                    for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
                    {
                        if (Method == 0 && Ordinal != Selected)
                            continue;
                        const auto Path = Scene.Transport.Connect(P[Ordinal], Primary);
                        ValidatePath(Scene, Path, Variant);
                        const float Weight = (Method == 0 ? Z : 1 / P[Ordinal].Density) * (Method == 2 ? .5f : 1.f) / 4;
                        for (uint32_t Axis = 0; Axis < 3; ++Axis)
                        {
                            Output[Pixel][Axis] += Path.Rgb[Axis] * Weight;
                            Output[Count + Pixel][Axis] += Path.SuffixOnly[Axis] * Weight;
                            Output[2 * Count + Pixel][Axis] += Path.ConnectionOnly[Axis] * Weight;
                            Output[3 * Count + Pixel][Axis] += Scores[Ordinal].ProxyRgb[Axis] * Weight;
                        }
                    }
                }
                if (Variant)
                    VerifyOcclusion(Output[Pixel][0] == 0 && Output[Pixel][1] == 0 && Output[Pixel][2] == 0);
                else
                    VerifyOcclusion(Output[Pixel][0] > 0 && Output[Pixel][1] > 0 && Output[Pixel][2] > 0);
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    VerifyOcclusion(Output[3 * Count + Pixel][Axis] > 0);
                    if (Variant == 1)
                        VerifyOcclusion(Output[Count + Pixel][Axis] > 0 && Output[2 * Count + Pixel][Axis] == 0);
                    if (Variant == 2)
                        VerifyOcclusion(Output[2 * Count + Pixel][Axis] > 0 && Output[Count + Pixel][Axis] == 0);
                    if (Variant == 3)
                        VerifyOcclusion(Output[Count + Pixel][Axis] == 0 && Output[2 * Count + Pixel][Axis] == 0);
                }
            }
            const std::string Name =
                Directory + "/AnalyticOcclusion" + std::to_string(Layout) + std::to_string(Variant) + ".f32";
            std::ofstream File(Name, std::ios::binary);
            File.write(reinterpret_cast<const char *>(Output.data()), Output.size() * sizeof(Quartet));
            File.close();
            VerifyOcclusion(bool(File));
            std::cout << "ANALYTIC_OCCLUSION_CASE layout=" << Layout << " variant=" << Variant
                      << " packets=12288 pass=1\n";
        }
    VerifyOcclusion(Paths == 786432 && Visibility == 1572864 && Primaries == 147456);
    std::cout << "ANALYTIC_OCCLUSION_TOTAL primary=" << Primaries << " paths=" << Paths << " visibility=" << Visibility
              << " pass=1\n";
}
} // namespace
//----------------------------------------------------------------------------------------------------------------------
//                                                      EXECUTION
//----------------------------------------------------------------------------------------------------------------------

int main(int Count, char **Arguments)
{
    try
    {
        if (Count != 2)
            throw std::runtime_error("Use --unit or output-directory");
        if (std::strcmp(Arguments[1], "--unit") == 0)
            ValidateUnits();
        else
            EncodeCaptures(Arguments[1]);
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
