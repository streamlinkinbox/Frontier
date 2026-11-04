/*==========================================================================================================================================
                                                          LUMINAIREREGISTER.H
==========================================================================================================================================*/
// 🧩 The 12,320-byte clustered luminaire register: street lamps, point lamps, vehicle beams and a transient weather light packed
//    with a 16×16×4 world grid of 64-bit cell masks (LightingSystem.js Update + BuildClusterGrid, ported 1:1).

#pragma once

#include "TransformProjection.h"

#include <optional>
#include <vector>

namespace Frontier::GlobalIllumination
{

enum class LuminaireCategory : uint32_t
{
    Omni       = 0,   // omnidirectional point light, 1/d² windowed falloff
    StreetLamp = 1,   // Type II / III roadway batwing IES distribution
    LowBeam    = 2,   // automotive asymmetric low beam cutoff
    HighBeam   = 3,   // automotive collimated pencil beam
    Fog        = 4,   // low-mounted horizontal cutoff
    Floodlight = 5    // broad multi-lens flood / roof bar
};

enum class AutomotiveBeam : uint32_t
{
    Off      = 0,
    LowBeam  = 1,
    HighBeam = 2,
    Fog      = 3
};

enum class RoofFixtureCategory : uint32_t
{
    SlimLed  = 0,
    RallyPod = 1
};

struct LuminaireProfile
{
    LuminaireCategory Category = LuminaireCategory::Omni;
    Vector3           Position{};               // [m]
    Vector3           Direction{0.0, 0.0, -1.0};
    Vector3           Colour{1.0, 1.0, 1.0};
    double            Intensity = 0.0;          // [-]
    double            Reach     = 0.0;          // [m]
    Vector4           Params{};                 // [-] inner cos, outer cos, IES parameter, active
};

// 📝 A fixture placement (LevelStructure.js ResolveLevelPlacements): PoleHead heads and LampLantern lanterns.
struct LampPlacement
{
    double X   = 0.0;    // [m]
    double Y   = 0.0;    // [m]
    double Z   = 0.0;    // [m] 0 = the fixture's default mounting height
    bool   Lit = true;   // [-] beyond the demo: false when the editor hides the head or lantern (the lamp keeps its slot)
};

inline constexpr uint32_t MaximumLuminaires      = 64;
inline constexpr uint32_t LuminaireEntryWords    = 16;   // [-] 32-bit words per luminaire entry
inline constexpr uint32_t LuminaireRegisterBytes = (MaximumLuminaires * LuminaireEntryWords + 8 + 1024 * 2) * 4;   // [B] 12,320

class LuminaireRegister
{
  public:
    // 📝 Returns the register bytes; VehicleTransform is the body transform (column-major) when a vehicle is present.
    [[nodiscard]] std::vector<uint8_t> Update(const std::optional<Transform>& VehicleTransform, const std::vector<LampPlacement>& Poles,
                                              const std::vector<LampPlacement>& PointLamps,
                                              const std::optional<LuminaireProfile>& TransientLight = std::nullopt);

    AutomotiveBeam                VehicleBeam        = AutomotiveBeam::LowBeam;
    RoofFixtureCategory           RoofFixture        = RoofFixtureCategory::SlimLed;
    bool                          RoofLightEnabled   = false;
    bool                          CullEnabled        = true;
    std::vector<LuminaireProfile> Luminaires{};
    uint32_t                      ActiveClusterCells = 0;
};

// 📝 The demo's DefaultPointLamps, used when no lantern placements are supplied.
[[nodiscard]] const std::vector<LampPlacement>& QueryDefaultPointLamps();

} // namespace Frontier::GlobalIllumination
