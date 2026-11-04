/*==========================================================================================================================================
                                                              PORTALRECORD.H
==========================================================================================================================================*/
// 🧩 A portal aperture: a one-sided rectangle in the record's local XY plane (its front normal is local +Z) that shows the world
//    behind its linked peer. Two apertures named for each other form a pair; a body crossing one emerges from the other, scaled
//    by the ratio of their extents. The Portals extension resolves the pairs and runs the transit; the viewport draws the
//    aperture and the clipped clone of a crossing body.

#pragma once

#include "Frontier/Engine/Scene/EngineConfigurations.h"
#include "Frontier/Engine/Scene/WorldSpace.h"

#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

struct PortalConfiguration : RevisionCounter
{
    bool                 Enabled              = true;
    float                ApertureWidthMetres  = 2.0f;    // [m] full width of the aperture (local X)
    float                ApertureHeightMetres = 3.0f;    // [m] full height of the aperture (local Y)
    std::string          LinkedPortalName{};             // [-] the peer aperture's record name; empty = unpaired
    SpatialConfiguration Spatial{};                      // [-] the aperture's centre, orientation and scale

    // 📝 The aperture's half extents in its own plane.
    [[nodiscard]] float HalfWidth() const noexcept { return 0.5f * ApertureWidthMetres; }
    [[nodiscard]] float HalfHeight() const noexcept { return 0.5f * ApertureHeightMetres; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PORTAL RECORD
//------------------------------------------------------------------------------------------------------------------------

class PortalRecord final : public SceneRecordBase
{
  public:
    explicit PortalRecord(std::string InDisplayName);

    [[nodiscard]] PortalConfiguration&       AccessConfiguration() noexcept { return Parameters; }
    [[nodiscard]] const PortalConfiguration& QueryConfiguration() const noexcept { return Parameters; }

    void SerializeConfiguration(toml::table& OutTable) const override;
    void DeserializeConfiguration(const toml::table& InTable) override;

  private:
    PortalConfiguration Parameters{};
};

} // namespace Frontier
