/*==========================================================================================================================================
                                              TYRESURFACEPROJECTIONPROBE.CPP
==========================================================================================================================================*/
// 🧩 Evaluate the standalone Engine tyre vertex projection for reference-shader comparisons.

#include "Engine/Physics/TyreSurfaceProjection.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    BINARY FIXTURE
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceProbeInput
{
    std::array<float, 4> Deformation{};
    std::array<uint32_t, 4> Selection{};
    std::array<std::array<float, 4>, Frontier::TyreContinuumParticleCount> Displacements{};
};

struct SurfaceProbeVertex
{
    std::array<float, 4> Position{};
    std::array<float, 4> Normal{};
};
static_assert(sizeof(SurfaceProbeInput) == 32 + 160 * 16);
static_assert(sizeof(SurfaceProbeVertex) == 32);

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    try
    {
        if (Count != 3) throw std::runtime_error("Expected input and output paths");
        std::ifstream Input(Arguments[1], std::ios::binary);
        Input.exceptions(std::ios::badbit | std::ios::failbit);
        SurfaceProbeInput Fixture;
        Input.read(reinterpret_cast<char*>(&Fixture), sizeof(Fixture));
        if (Fixture.Selection[0] > 1 || Fixture.Selection[1] > 1 || Fixture.Selection[2] > 4096)
            throw std::runtime_error("Probe selection exceeds the fixture domain");
        std::vector<SurfaceProbeVertex> Samples(Fixture.Selection[2]);
        Input.read(reinterpret_cast<char*>(Samples.data()), std::streamsize(Samples.size() * sizeof(SurfaceProbeVertex)));
        Frontier::TyreDeformationRecord Deformation;
        Deformation.ContactAngle = Fixture.Deformation[0];
        Deformation.Deflection = Fixture.Deformation[1];
        Deformation.GaugePressure = Fixture.Deformation[2];
        Deformation.LateralDistortion = Fixture.Deformation[3];
        Deformation.Shredded = Fixture.Selection[0] != 0;
        Frontier::TyreContinuumPositions Rest{}, Local{};
        for (int ParticleIndex = 0; ParticleIndex < Frontier::TyreContinuumParticleCount; ++ParticleIndex)
            for (int Axis = 0; Axis < 3; ++Axis)
                Local[ParticleIndex * 3 + Axis] = Fixture.Displacements[ParticleIndex][Axis];
        std::vector<SurfaceProbeVertex> Projected(Samples.size());
        for (std::size_t VertexIndex = 0; VertexIndex < Samples.size(); ++VertexIndex)
        {
            const auto& Sample = Samples[VertexIndex];
            auto& Result = Projected[VertexIndex];
            const bool TyreSurface = Sample.Position[3] != 0;
            if (Fixture.Selection[1] != 0)
                Frontier::ResolveContinuumWheelVertex(Deformation, TyreSurface, Rest, Local,
                    Sample.Position.data(), Sample.Normal.data(), Result.Position.data(), Result.Normal.data());
            else
                Frontier::ResolveDeformedWheelVertex(Deformation, TyreSurface, Sample.Position.data(),
                    Sample.Normal.data(), Result.Position.data(), Result.Normal.data());
        }
        std::ofstream Output(Arguments[2], std::ios::binary);
        Output.exceptions(std::ios::badbit | std::ios::failbit);
        Output.write(reinterpret_cast<const char*>(Projected.data()),
                     std::streamsize(Projected.size() * sizeof(SurfaceProbeVertex)));
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "Tyre projection probe failed: %s\n", Error.what());
        return 1;
    }
}
