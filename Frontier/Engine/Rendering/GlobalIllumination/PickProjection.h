/*==========================================================================================================================================
                                                            PICKPROJECTION.H
==========================================================================================================================================*/
// 🧩 Beyond the demo: GPU picking for the editor. The occupants drawn at and around one screen pixel, found by rasterising the
//    scene's own facet sets and occupant profiles from a camera's unjittered view-projection into a small target centred on that
//    pixel. It runs only when asked (a click), never inside a refresh.

#pragma once

#include "SceneStructure.h"
#include "ShaderInterchange.h"
#include "TransformProjection.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace Frontier::GlobalIllumination
{

// 📝 What one pick saw: the occupant drawn at every texel of the (2 Reach + 1)² neighbourhood of the pixel, row by row from its
//    top-left, the picked pixel in the middle; NoOccupant where nothing is drawn.
struct OccupantPick
{
    static constexpr uint32_t NoOccupant = 0xFFFFFFFFu;

    uint32_t                Reach = 0;              // [px] texels on each side of the picked pixel
    std::array<uint32_t, 2> Pixel{0, 0};            // [px] the picked screen pixel, from the top-left
    std::vector<uint32_t>   Occupants{};            // [-]  (2 Reach + 1)² occupant indices
    double                  Milliseconds = 0.0;     // [ms] from recording to readback

    [[nodiscard]] uint32_t QuerySpan() const noexcept { return 2u * Reach + 1u; }
    // 📝 The occupant at an offset from the picked pixel (|offset| ≤ Reach), NoOccupant outside the neighbourhood.
    [[nodiscard]] uint32_t QueryOccupant(int32_t OffsetX, int32_t OffsetY) const noexcept;
};

class PickProjection
{
  public:
    static constexpr uint32_t Reach = 4;   // [px] the target spans the pixel and four pixels on each side of it

    PickProjection(DeviceExchange& Exchange, ShaderInterchange& Shaders);

    // 📝 The pick program, its parameter region and the target; throws when the SPIR-V folder lacks PickRaster's entries.
    void Construct(const std::shared_ptr<DeviceRegion>& OccupantRegion);

    // 📝 Rasterises every draw run of the scene for Pixel of an image of ScreenExtent pixels seen through ViewProjection (unjittered,
    //    as the refresh encodes it), submits and reads the target back (waits for both).
    bool Pick(const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion, const Transform& ViewProjection,
              std::array<uint32_t, 2> ScreenExtent, std::array<uint32_t, 2> Pixel, OccupantPick& Outcome, std::string* OutDiagnostic = nullptr);

  private:
    DeviceExchange&                    Exchange_;
    ShaderInterchange&                 Shaders_;
    std::shared_ptr<DescriptorLayout>  Layout_{};
    std::shared_ptr<DescriptorGroup>   Group_{};
    std::shared_ptr<DeviceProgram>     Program_{};
    std::shared_ptr<DeviceRegion>      ProfileRegion_{};    // [-] PickProfile: view-projection, pixel centre, extent ratio
    std::shared_ptr<DeviceRegion>      InstanceRegion_{};   // [-] identity occupant list (each draw run's first instance onwards)
    uint32_t                           InstanceCount_ = 0;
    std::shared_ptr<DeviceTexture>     OccupantTexture_{}, DepthTexture_{};
    std::shared_ptr<DeviceTextureView> OccupantView_{}, DepthView_{};
};

} // namespace Frontier::GlobalIllumination
