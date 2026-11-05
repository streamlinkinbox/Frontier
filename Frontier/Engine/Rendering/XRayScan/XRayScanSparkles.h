/*==========================================================================================================================================
                                                         XRAYSCANSPARKLES.H
==========================================================================================================================================*/
// 🧩 The scan's particles: drei's Sparkles as the reference uses them (130 points uniform in a box about the car) with a seeded generator, so a
//    session and a test see the same cloud. A port of ConstructSparkleCloud (GarageView.js).

#pragma once

#include "XRayScanSpecification.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::XRayScan
{

inline constexpr uint32_t SparkleSeed = 0x9e3779b9u;

// 📐 The generator of the cloud: mulberry32, one value in [0, 1) per call.
class SparkleGenerator
{
public:
    explicit SparkleGenerator(uint32_t Seed = SparkleSeed) : State_(Seed) {}
    [[nodiscard]] double Next();

private:
    uint32_t State_;
};

// 📝 count points uniform in a box (randFloatSpread per axis), the group lifted to the box's height. Per point: along, up (above the ground),
//    right; one spare. Four floats per point.
[[nodiscard]] std::vector<float> ConstructSparkleCloud(uint32_t Count = ScanSpecification.ParticleCount,
                                                       const std::array<double, 3>& Box = ScanSpecification.ParticleBox,
                                                       double Centre = ScanSpecification.ParticleCentre);

} // namespace Frontier::XRayScan
