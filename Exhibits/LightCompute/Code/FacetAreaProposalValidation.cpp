/*============================================================================================================================================
                                                   FACETAREAPROPOSALVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Exercise shader-order RNG/area proposals and capture newly generated native proposals from retained geometry.

#include "Engine/Rendering/FacetAreaProposals.h"
#include "FacetAreaProposalScenes.h"
#include <cstring>
#include <iostream>
#include <limits>

namespace
{
uint32_t Checks = 0;
void Check(bool Accepted)
{
    ++Checks;
    if (!Accepted) throw std::runtime_error("Area proposal validation failed");
}

void ValidateUnits()
{
    const FacetAreaTriangle Small{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const FacetAreaTriangle Large{{0, 0, 1}, {3, 0, 1}, {0, 1, 1}};
    FacetAreaProposals Proposals;
    Check(!Proposals.Draw(0, 0, FacetAreaMeasureCategory::UniformFacet).Accepted);
    Check(!Proposals.Prepare({}));
    Check(!Proposals.Prepare({{}}));
    Check(Proposals.Prepare({Small, Large}));
    Check(!Proposals.Draw(0, 0, FacetAreaMeasureCategory(2)).Accepted);
    Check(HashFacetAreaBits(0) == 0);
    Check(HashFacetAreaBits(1) == 0x688990c0u);
    Check(HashFacetAreaBits(0xffffffffu) == 0x6768824au);
    Check(HashFacetAreaBits(0x80000000u) == 0xcc4b4124u);
    Check(DrawFacetAreaUniforms(0xffffffffu, 0xffffffffu) ==
          std::array<float, 3>{2103824.f / 16777216.f, 10811273.f / 16777216.f, 6776962.f / 16777216.f});
    for (uint32_t Seed : {0u, 1u, 0xffffffffu, 0x80000000u})
        for (uint32_t Draw : {0u, 1u, 7u, 0xffffffffu})
        {
            for (float Uniform : DrawFacetAreaUniforms(Seed, Draw))
                Check(Uniform >= 0 && Uniform < 1 && Uniform * 16777216.f == std::floor(Uniform * 16777216.f));
            for (auto Measure : {FacetAreaMeasureCategory::UniformFacet, FacetAreaMeasureCategory::ProportionalArea})
            {
                const auto Proposal = Proposals.Draw(Seed, Draw, Measure);
                Check(Proposal.Accepted && Proposal.Index < 2);
                Check(Proposal.U >= 0 && Proposal.V >= 0 && Proposal.U + Proposal.V <= 1.000001f);
                Check(Measure == FacetAreaMeasureCategory::UniformFacet ?
                      Proposal.Density == (Proposal.Index == 0 ? 1.f : 1.f / 3.f) : Proposal.Density == .5f);
                Check(Proposal.Point[2] == float(Proposal.Index));
            }
        }
    auto Invalid = Small;
    Invalid.A[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!Proposals.Prepare({Invalid}));
    Check(!Proposals.Draw(1, 0, FacetAreaMeasureCategory::ProportionalArea).Accepted);
    Invalid.A[0] = std::numeric_limits<float>::infinity();
    Check(!Proposals.Prepare({Invalid}));
    Invalid = Small; Invalid.B[0] = std::numeric_limits<float>::max();
    Check(!Proposals.Prepare({Invalid}));
    // 📝 A tiny final area that vanishes in the CDF must be rejected,not silently lose support.
    Invalid = Small; Invalid.B[0] = 1e-10f;
    Check(!Proposals.Prepare({Large, Invalid}));
    Check(Proposals.Prepare({Small}));
    const auto Single = Proposals.Draw(1, 1, FacetAreaMeasureCategory::ProportionalArea);
    Check(Single.Accepted && Single.Index == 0 && Single.Density == 2);
    std::cout << "FACET_AREA_PROPOSAL_UNIT checks=" << Checks << " pass=1\n";
}

void Capture(const char* Output, const char* Geometry)
{
    const uint16_t Endian = 1;
    Check(*reinterpret_cast<const uint8_t*>(&Endian) == 1 && sizeof(float) == 4);
    std::ofstream Samples(Output, std::ios::binary), Corners(Geometry, std::ios::binary);
    Check(bool(Samples) && bool(Corners));
    uint64_t Draws = 0;
    for (uint32_t Corpus = 0; Corpus < 3; ++Corpus)
        for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
        {
            const auto Scene = Corpus == 2 ? ConstructDeformedAreaScene(Fixture) : ConstructGeometricAreaScene(Fixture);
            std::vector<FacetAreaTriangle> Triangles;
            for (const auto& Facet : Scene.Surfaces)
            {
                FacetAreaTriangle Triangle;
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    Triangle.A[Axis] = Facet.A[Axis];
                    Triangle.B[Axis] = Facet.B[Axis];
                    Triangle.C[Axis] = Facet.C[Axis];
                }
                Triangles.push_back(Triangle);
                for (const auto& Point : {Triangle.A, Triangle.B, Triangle.C})
                    Corners.write(reinterpret_cast<const char*>(Point.data()), sizeof(Point));
            }
            Check(Triangles.size() == 16);
            FacetAreaProposals Proposals;
            Check(Proposals.Prepare(Triangles));
            for (uint32_t Measure = 0; Measure < 2; ++Measure)
                for (uint32_t Packet = 0; Packet < 4096; ++Packet)
                    for (uint32_t Draw = 0; Draw < 8; ++Draw)
                    {
                        const uint32_t Seed = HashFacetAreaBits((Packet + std::array<uint32_t, 3>{0x739ab531u, 0x4a615e23u, 0x5e92b341u}[Corpus]) ^ (Fixture * 0x6c8e9cf5u));
                        const auto Proposal = Proposals.Draw(Seed, Draw, FacetAreaMeasureCategory(Measure));
                        Check(Proposal.Accepted);
                        const std::array<uint32_t, 2> Ordinals{Proposal.Index, uint32_t(Proposal.Accepted)};
                        const auto Uniforms = DrawFacetAreaUniforms(Seed, Draw);
                        const std::array<float, 10> Coordinates{Proposal.U, Proposal.V, Proposal.Density,
                            Proposal.Point[0], Proposal.Point[1], Proposal.Point[2],
                            DrawFacetAreaSelectionUniform(Seed, Draw / 2), Uniforms[0], Uniforms[1], Uniforms[2]};
                        Samples.write(reinterpret_cast<const char*>(Ordinals.data()), sizeof(Ordinals));
                        Samples.write(reinterpret_cast<const char*>(Coordinates.data()), sizeof(Coordinates));
                        ++Draws;
                    }
        }
    Samples.close(); Corners.close();
    Check(bool(Samples) && bool(Corners));
    std::cout << "FACET_AREA_PROPOSAL_CAPTURE distinct=" << Draws << " fixtures=9 pass=1\n";
}
}

int main(int Count, char** Arguments)
{
    try
    {
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0) ValidateUnits();
        else if (Count == 4 && std::strcmp(Arguments[1], "--capture") == 0) Capture(Arguments[2], Arguments[3]);
        else throw std::runtime_error("Use --unit or --capture proposals.bin fixtures.bin");
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
