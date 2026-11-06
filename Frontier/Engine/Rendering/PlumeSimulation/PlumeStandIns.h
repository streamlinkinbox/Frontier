/*==========================================================================================================================================
                                                         PLUMESTANDINS.H
==========================================================================================================================================*/
// 🧩 What the plume's programs bind beside their own lattices when no scene stands behind them: the smallest resources of the right kind.
//    The illumination port's scene distance field, shadow cascades and sky are not part of the plume: the transport and the march bind
//    them, so a host that has no scene (the Smoke & fire editor's viewport, the plume exhibit) gives the programs a distance field of zero
//    cascades (the analytic ground plane at z = 0 answers every sample and no occupant is ever indexed), a surface with no depth, a shadow
//    with no cascade (the sun is always seen) and a one-texel sky.

#pragma once

#include "PlumeSimulationSequence.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Frontier::PlumeSimulation
{

struct PlumeStandIns
{
    std::shared_ptr<DescriptorLayout>  DistanceLayout{};
    std::shared_ptr<DescriptorGroup>   DistanceGroup{};
    std::shared_ptr<DeviceRegion>      Occupants{};
    std::shared_ptr<DeviceRegion>      Refresh{};
    std::shared_ptr<DeviceTexture>     DepthTexture{};
    std::shared_ptr<DeviceTextureView> DepthView{};
    PlumeMarchInputs                   March{};
};

// 📝 Builds every stand-in for a picture of Screen pixels. PlumeParityGpu.mjs builds the same resources in the same order, sizes and contents.
[[nodiscard]] bool ConstructPlumeStandIns(DeviceExchange& Exchange, std::array<uint32_t, 2> Screen, PlumeStandIns& StandIns, std::string& Diagnostic);

// 📝 Only the surface depth, for a picture of another size: cleared to the reversed far plane, so nothing occludes. The caller rebuilds the march
//    targets (PlumeSimulationSequence::ConstructTargets) afterwards, because they bind the new view.
[[nodiscard]] bool ConstructStandInDepth(DeviceExchange& Exchange, std::array<uint32_t, 2> Screen, PlumeStandIns& StandIns, std::string& Diagnostic);

} // namespace Frontier::PlumeSimulation
