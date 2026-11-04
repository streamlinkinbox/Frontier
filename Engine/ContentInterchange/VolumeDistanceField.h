/*============================================================================================================================================
                                                       VOLUMEDISTANCEFIELD.H
============================================================================================================================================*/
// 🧩 Float32 volume distance field sampled from triangle surfaces using separable Euclidean distance transforms.
#pragma once
#include "Engine/ContentInterchange/BakeMesh.h"
#include <array>
#include <cstdint>
#include <vector>
#include <filesystem>

namespace Frontier
{
struct VolumeDistanceField
{
    std::array<uint32_t, 3> Dimensions{};
    std::array<float, 3> Minimum{};
    std::array<float, 3> Maximum{};
    std::array<float, 3> Step{};
    std::vector<float> Distances;
    std::vector<float> Confidence;
    float MinimumDistance = 0;
    float MaximumDistance = 0;
    bool ClosedManifold = false;
    bool Signed = true;
};

void ExportVolumeDistanceFieldNrrd(const VolumeDistanceField &Field, const std::filesystem::path &Path);

[[nodiscard]] VolumeDistanceField BakeVolumeDistanceField(const BakeMeshSet &Surface,
    std::array<uint32_t, 3> Dimensions, float Padding, bool Signed, float NarrowBand = 0.f);
}
