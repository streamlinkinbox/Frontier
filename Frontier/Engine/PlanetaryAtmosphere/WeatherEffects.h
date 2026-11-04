//==========================================================================================================================================
//                                                        WEATHER EFFECTS
//==========================================================================================================================================
// 🧩 Analytic precipitation volume + deterministic midpoint-displacement lightning channels with multi-stroke flash envelope.

#pragma once

#include "Engine/Foundation/DeterministicRandom.h"
#include "Engine/Rendering/ClusterNesting.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

struct PrecipitationParameters
{
    double   VolumeExtentM = 40.0;
    double   VolumeHeightM = 25.0;
    double   FallSpeedMps  = 9.0;
    double   WindXMps      = 2.0;
    double   WindYMps      = 0.5;
    uint64_t Seed          = 0x9A17ull;
};

class PrecipitationField
{
  public:
    explicit PrecipitationField(const PrecipitationParameters& Parameters) noexcept : P_(Parameters) {}

    [[nodiscard]] SpatialVector ParticleAt(uint32_t Index, double TimeS, SpatialVector Anchor) const noexcept;
    [[nodiscard]] SpatialVector ParticleVelocity(uint32_t Index) const noexcept;
    [[nodiscard]] double        VerticalPeriod(uint32_t Index) const noexcept;

  private:
    void Hashes(uint32_t Index, double& U0, double& U1, double& U2, double& U3) const noexcept;
    PrecipitationParameters P_;
};

struct LightningBolt
{
    std::vector<SpatialVector>              MainChannel;
    std::vector<std::vector<SpatialVector>> Branches;
};

[[nodiscard]] LightningBolt GenerateLightningBolt(SpatialVector From,
                                                  SpatialVector To,
                                                  uint32_t      Depth,
                                                  double        Roughness,
                                                  uint64_t      Seed,
                                                  uint32_t      MaxBranches = 3);

[[nodiscard]] double LightningIntensity(double TimeS, uint64_t Seed) noexcept;

} // namespace Frontier
