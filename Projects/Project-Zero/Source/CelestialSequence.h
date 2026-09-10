//============================================================================================================================================
//                                                  CELESTIALSEQUENCE.H
//============================================================================================================================================
// 🧩 The sky, the weather and everything that carries them, as one piece of project state.
//
//    The Celestial port built each system as a settings struct plus a pure evaluator — CelestialSolver,
//    AtmosphereModel, Twilight, StarCatalogueIndex, WindField, VolumetricMedia, Precipitation,
//    AtmosphericOptics. None of them knew about a scene, an outliner or a frame loop, which is what let each be
//    proved on its own. This is where they become a world.
//
//    Three jobs, and the split matters:
//        · HOLD the state. One struct per entity, named as the reference panel names them, so the eventual
//          panel is a projection of this rather than a translation of it.
//        · ADVANCE it. One Tick that moves the clock, the wind phase and the precipitation pool, and re-solves
//          the ephemeris. Everything time-dependent happens in one place and in a fixed order.
//        · PROJECT it. Rows for the outliner and sheets for the inspector, built from the same state the
//          renderer reads — so what the editor shows is what the frame drew, not a parallel description of it.
//
//    ⚠️ The tier is NOT read here. CelestialTier owns the only translation from a quality tier to celestial
//    budgets (CheckCelestialTiers enforces that), so this takes a CelestialBudget and never a FidelityCriteria.

#pragma once

#include "../../../Engine/DisplayPresentation/CelestialSolver.h"
#include "../../../Engine/DisplayPresentation/CelestialTier.h"
#include "../../../Engine/DisplayPresentation/AtmosphereModel.h"
#include "../../../Engine/DisplayPresentation/AtmosphericOptics.h"
#include "../../../Engine/DisplayPresentation/Precipitation.h"
#include "../../../Engine/DisplayPresentation/VolumetricMedia.h"
#include "../../../Engine/DisplayPresentation/WindField.h"
#include "../../../Engine/Editor/EditorInstance.h"
#include "../../../Engine/GeometricRaster/StarCatalogueIndex.h"
#include "../../../Engine/GeometricRaster/VisibilityRaster.h"
#include "../../../Engine/SpatialInterface/VolumeMarker.h"

#include <cstdint>

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ENTITIES
//------------------------------------------------------------------------------------------------------------------------

// Every celestial entity the outliner can show, in the order it shows them. Mirrors the reference panel's
//    outliner, minus the lights (which the scene already owns) and the Cine Camera (a rig, not weather).
enum class CelestialEntity : uint32_t
{
    Atmosphere = 0u,
    Sun,
    Sky,
    Stars,
    Moons,
    HeightFog,
    AtmosphericFog,
    CloudLayer,
    LocalCloud,
    LocalFog,
    Wind,
    Precipitation,
    Rainbow,
    LensFlare,
    Count
};

constexpr uint32_t kCelestialEntityCount = static_cast<uint32_t>(CelestialEntity::Count);

const char* CelestialEntityName(CelestialEntity Entity) noexcept;
const char* CelestialEntityKind(CelestialEntity Entity) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE WORLD
//------------------------------------------------------------------------------------------------------------------------

// The clock. Kept apart from the observation because the panel drives these two differently: the date and place
//    are set once, the time of day is scrubbed and animated.
struct CelestialClock
{
    bool  Animate   = false;
    float SpeedTimes = 8.0f;   // 1, 8, 30, 100 — the reference panel's Speed segment
};

class CelestialSequence
{
public:
    void Prepare() noexcept;

    // One frame. Order is fixed and deliberate: the clock moves, the ephemeris re-solves from it, the wind phase
    //    advances, and only then does precipitation step — because the emitter reads the cloud layer, which the
    //    wind has just moved.
    void Tick(float DeltaSeconds, const float Camera[3], float GroundHeight) noexcept;

    // Hand the raster everything it needs to draw the sky. One call, so a caller cannot wire half of it.
    void ApplyTo(VisibilityRaster& Raster, const CelestialBudget& Budget) const noexcept;

    //--------------------------------------------------------------------------------------------------------------------
    //                                              THE OUTLINER FEED
    //--------------------------------------------------------------------------------------------------------------------

    // Appends the celestial rows under one "Celestial" folder, after whatever the scene already wrote. Returns
    //    the number of rows written. Honours the roster cap rather than assuming room.
    uint32_t AppendRoster(EditorInstance* Instances, uint32_t Written, uint32_t Capacity) const noexcept;

    // True when this roster index belongs to the celestial block, and which entity it is.
    [[nodiscard]] bool Owns(uint32_t RosterIndex, uint32_t FirstRow, CelestialEntity& Entity) const noexcept;

    // The inspector sheet for one entity, filled live from the state below.
    void BuildSheet(CelestialEntity Entity, EditorSheet& Sheet) const noexcept;

    // Write an edited sheet back. The panel edits a mirror; this is the one seam where it returns.
    void ApplySheet(CelestialEntity Entity, const EditorSheet& Sheet) noexcept;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                THE MARKERS
    //--------------------------------------------------------------------------------------------------------------------

    // Billboards for the volumes that have a position and no surface. Returns how many were written.
    uint32_t CollectMarkers(VolumeMarker* Markers, uint32_t Capacity) const noexcept;
    void     MoveMarker(uint32_t Identifier, const float World[3]) noexcept;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  THE STATE
    //--------------------------------------------------------------------------------------------------------------------

    CelestialObservation Observation{};
    CelestialClock       Clock{};
    AtmosphereMedium     Medium{};
    AtmosphereLight      Light{};
    TwilightSettings     Twilight{};
    WindSettings         Wind{};
    CloudLayerSettings   Cloud{};
    LocalVolumeSettings  LocalCloud{};
    LocalVolumeSettings  LocalFog{};
    FogSettings          Fog{};
    PrecipitationSettings Precip{};
    RainbowSettings      Rainbow{};
    AtmosphericOptics::LensFlareSettings Flare{};

    // Sky appearance, which the reference panel exposes separately from the medium.
    float SkyTint[3]     = { 1.0f, 1.0f, 1.0f };
    float SkyBrightness  = 1.0f;
    float GroundAlbedo[3] = { 0.19f, 0.17f, 0.14f };
    float StarBrightness = 1.0f;
    float StarSize       = 1.0f;
    bool  Enabled        = true;      // the whole celestial system, off by default in the raster

    // Per-entity visibility, so the outliner's eye toggles do something.
    bool Shown[kCelestialEntityCount] = {};

    // What the active quality tier granted. Set by the project from CelestialTier::BudgetFor so the inspector
    //    can show the budget the frame is actually spending rather than a default.
    CelestialBudget Budget{};

    [[nodiscard]] const CelestialFrame& Frame() const noexcept { return Solved; }
    [[nodiscard]] const PrecipitationSystem& Weather() const noexcept { return Rain; }
    [[nodiscard]] const StarCatalogueIndex& Stars() const noexcept { return Catalogue; }

private:
    CelestialFrame      Solved{};
    PrecipitationSystem Rain{};
    StarCatalogueIndex  Catalogue{};
    float               ElapsedHours = 0.0f;
};

} // namespace Frontier::ProjectZero
