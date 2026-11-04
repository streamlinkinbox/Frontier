/*============================================================================================================================================
                                                        VOLUMEPREVIEW.H
============================================================================================================================================*/
// 🧩 Bounded CPU rasterization of a baked Float32 volume, including composited cutting planes.
#pragma once
#include "Engine/ContentInterchange/VolumeDistanceField.h"
#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{
enum class VolumePreviewMode { IsoCut, Triplane, Stack };
enum class VolumePreviewPalette { Bands, Contours, Inside, Normalized, Confidence };
struct VolumePreviewOptions
{
    VolumePreviewMode Mode = VolumePreviewMode::IsoCut;
    VolumePreviewPalette Palette = VolumePreviewPalette::Bands;
    uint32_t Axis = 2;
    float Slice = .5f;
    float Yaw = 0.f, Pitch = 0.f, Zoom = 1.f;
    uint32_t Resolution = 72;
};
// RGBA colour for a voxel, derived from the prototype's volume display modes.
[[nodiscard]] std::array<uint8_t, 4> VolumePreviewColour(const VolumeDistanceField &Volume, size_t Index, VolumePreviewPalette Palette);
// Returns square RGBA8 pixels, including the dark preview background. Throws on malformed volume input.
[[nodiscard]] std::vector<uint8_t> RenderVolumePreview(const VolumeDistanceField &Volume, const VolumePreviewOptions &Options);
}
