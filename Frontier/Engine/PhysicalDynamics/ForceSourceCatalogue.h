/*==========================================================================================================================================
                                                          FORCESOURCECATALOGUE.H
==========================================================================================================================================*/
// 🧩 The localized force types: what each reads, when it acts, where it stands and what it adds at a point.

#pragma once

#include "Engine/Rendering/ClusterNesting.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <span>
#include <string_view>

namespace Frontier
{

// 📝 A line-for-line port of Experimental/ForceFieldEditor/ForceFieldCatalogue.js (the approved prototype). Every
//    evaluation here works in the field's axes (X east, Y up, Z south, as ForceFieldSpace); the records keep scene axes
//    (x east, y north, z up) and PrepareForceSource converts once.

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTORS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One slider of a type's Strength section. Distance sliders are logarithmic and read in m below a kilometre, km above.
struct ForceParameterDescriptor
{
    double ForceSourceConfiguration::*Member = nullptr;
    const char* Identifier = "";   // levelspace key
    const char* Label      = "";
    double      Minimum    = 0.0;
    double      Maximum    = 1.0;
    double      Step       = 0.1;
    const char* Unit       = "";
    int         Digits     = 1;
    bool        Distance   = false;
};

struct ForceBearingDescriptor
{
    double ForceSourceConfiguration::*Member = nullptr;
    const char* Identifier = "";
    const char* Label      = "";
};

struct ForceSourceDescriptor
{
    ForceSourceCategory                       Category     = ForceSourceCategory::None;
    const char*                               Identifier   = "";   // levelspace category (snake_case)
    const char*                               Name         = "";
    ForceContribution                         Contribution = ForceContribution::Flow;
    const char*                               Glyph        = "";   // FrontierIcons name
    ForceReach                                Reach        = ForceReach::Sphere;
    const char*                               Blurb        = "";
    std::span<const ForceParameterDescriptor> Parameters{};
    std::span<const ForceBearingDescriptor>   Bearings{};
    bool                                      Spin         = false;
};

struct ForceContributionDescriptor
{
    ForceContribution Contribution = ForceContribution::Flow;
    const char*       Identifier   = "";
    const char*       Label        = "";
    const char*       Unit         = "";
    uint32_t          ColourRgb    = 0;   // 0xRRGGBB
    const char*       Blurb        = "";
};

struct ForceReceiverDescriptor
{
    ForceReceiver Receiver   = ForceReceiverParticles;
    const char*   Identifier = "";
    const char*   Label      = "";
};

// 📝 The sixteen types in the library's order: nine flows, six accelerations, one damping.
[[nodiscard]] std::span<const ForceSourceDescriptor> EnumerateForceSourceDescriptors() noexcept;
[[nodiscard]] const ForceSourceDescriptor& ResolveForceSourceDescriptor(ForceSourceCategory Category) noexcept;
[[nodiscard]] const ForceSourceDescriptor* ResolveForceSourceDescriptor(std::string_view Identifier) noexcept;
[[nodiscard]] const ForceContributionDescriptor& ResolveForceContributionDescriptor(ForceContribution Contribution) noexcept;
[[nodiscard]] std::span<const ForceReceiverDescriptor> EnumerateForceReceivers() noexcept;

// 📝 A new source of a type: the type's parameter defaults and reach, at the given scene-axis centre.
[[nodiscard]] ForceSourceConfiguration ConstructForceSource(ForceSourceCategory Category) noexcept;

// 📝 The levelspace spellings of the reach, falloff and activation choices.
[[nodiscard]] const char* ResolveForceReachIdentifier(ForceReach Reach) noexcept;
[[nodiscard]] const char* ResolveForceFalloffIdentifier(ForceFalloff Falloff) noexcept;
[[nodiscard]] const char* ResolveForceActivationIdentifier(ForceActivation Activation) noexcept;
[[nodiscard]] ForceReach      ParseForceReach(std::string_view Identifier, ForceReach Fallback) noexcept;
[[nodiscard]] ForceFalloff    ParseForceFalloff(std::string_view Identifier, ForceFalloff Fallback) noexcept;
[[nodiscard]] ForceActivation ParseForceActivation(std::string_view Identifier, ForceActivation Fallback) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    ACTIVATION
//------------------------------------------------------------------------------------------------------------------------

enum class ForceSourceStatus : uint8_t
{
    Off     = 0,
    Active  = 1,
    Waiting = 2,
    Armed   = 3,
    Spent   = 4,
};

struct ForceActivationReading
{
    double            Envelope   = 0.0;   // [0..1] smooth ramp in, hold, ramp out
    double            AgeSeconds = 0.0;   // [s] since this cycle began
    ForceSourceStatus Status     = ForceSourceStatus::Off;
};

[[nodiscard]] ForceActivationReading ResolveForceActivation(const ForceSourceConfiguration& Source, double TimeSeconds) noexcept;
[[nodiscard]] const char* ResolveForceSourceStatusLabel(ForceSourceStatus Status) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    EVALUATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The wind terms a source reads: a gust puff drifts with the wind at its own height, a tornado's track carries, the
//    microburst and the wake stand on the ground.
struct ForceWindReading
{
    bool   Enabled                 = true;
    double BearingDegrees          = 45.0;    // [°] compass of travel at the surface
    double SpeedMetresPerSecond    = 6.5;     // [m/s] at the 10 m reference height
    double VeerDegreesPerKilometre = 8.0;
    double ShearPerKilometre       = 0.15;
    double GroundHeightMetres      = 0.0;
};

[[nodiscard]] ForceWindReading ResolveForceWindReading(const WindConfiguration& Wind) noexcept;

// 📝 One source resolved for a moment: its envelope, its centre then (the tornado and the puff move) and its bounds.
struct PreparedForceSource
{
    ForceSourceConfiguration Source{};
    ForceContribution        Contribution  = ForceContribution::Flow;
    ForceActivationReading   Activation{};
    SpatialVector            CentreMetres{};      // field axes
    double                   BoundX        = 0.0; // [m] half extent of the reach's rectangle across X
    double                   BoundZ        = 0.0; // [m] across Z
    double                   BoundSquared  = 0.0; // [m²] reach² of the sphere and column
};

[[nodiscard]] SpatialVector         ConvertSceneToFieldAxes(const double SceneMetres[3]) noexcept;
[[nodiscard]] std::array<double, 3> ConvertFieldToSceneAxes(SpatialVector FieldMetres) noexcept;

// 📝 Where the source stands at a time (field axes) and the 0..1 mask of its reach at a point.
[[nodiscard]] SpatialVector ResolveForceSourceCentre(const ForceSourceConfiguration& Source,
                                                     const ForceWindReading&        Wind,
                                                     double                         TimeSeconds,
                                                     double                         AgeSeconds) noexcept;
[[nodiscard]] double ResolveForceReachMask(const ForceSourceConfiguration& Source,
                                           SpatialVector                   CentreMetres,
                                           SpatialVector                   PointMetres,
                                           double                          GroundHeightMetres) noexcept;

[[nodiscard]] PreparedForceSource PrepareForceSource(const ForceSourceConfiguration& Source,
                                                     const ForceWindReading&        Wind,
                                                     double                         TimeSeconds) noexcept;

// 📝 Adds what one prepared source contributes at a field-axis point: m/s for a flow, m/s² for an acceleration, and for
//    damping the rate [1/s] in X. Returns false where the source does not reach.
bool EvaluatePreparedForceSource(const PreparedForceSource& Prepared,
                                 const ForceWindReading&    Wind,
                                 SpatialVector              PointMetres,
                                 SpatialVector&             InOutValue) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROFILE AND BUDGET
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kForceProfileSamples = 73;

// 📝 The magnitude a source adds against the distance from its centre on the slice, out to 1.15 × its reach.
struct ForceSourceProfile
{
    std::array<double, kForceProfileSamples> DistanceMetres{};
    std::array<double, kForceProfileSamples> Magnitude{};
    double                                   ReachMetres = 0.0;
    double                                   PeakMagnitude = 0.0;
    const char*                              Unit = "";
};

[[nodiscard]] ForceSourceProfile ResolveForceSourceProfile(const ForceSourceConfiguration& Source,
                                                           const ForceWindReading&        Wind,
                                                           double                         SliceHeightMetres,
                                                           double                         TimeSeconds) noexcept;

inline constexpr uint32_t kMaximumForceCascades = 8;

struct ForceCascadeLevel
{
    double   CellMetres   = 0.0;
    double   ExtentMetres = 0.0;
    uint32_t RefreshFrames = 1;   // the level re-bakes every RefreshFrames frames
};

// 📝 What the proposed cascades cost and which forces fit them: an unselective flow bakes into the lattice, a selective
//    one or any acceleration is evaluated per receiver, damping scales velocity apart.
struct ForceFieldBudget
{
    uint32_t                                           Baked        = 0;
    uint32_t                                           Live         = 0;
    uint32_t                                           Damping      = 0;
    uint32_t                                           Disabled     = 0;
    uint64_t                                           CascadeBytes = 0;
    uint64_t                                           TotalBytes   = 0;
    uint32_t                                           GpuSlots     = 16;
    uint32_t                                           LevelCount   = 0;
    std::array<ForceCascadeLevel, kMaximumForceCascades> Levels{};
};

[[nodiscard]] ForceFieldBudget ResolveForceFieldBudget(const ForceFieldConfiguration&                Field,
                                                       std::span<const ForceSourceConfiguration> Sources) noexcept;

// 📝 The Beaufort force (0 – 12) of a speed and its name, for the readouts.
[[nodiscard]] uint32_t    ResolveBeaufortForce(double SpeedMetresPerSecond) noexcept;
[[nodiscard]] const char* ResolveBeaufortName(uint32_t Force) noexcept;

// 📝 The shared gradient noise the wind's eddies and mesoscale read: value and ∂/∂U, ∂/∂V (uint32 hash as Math.imul).
[[nodiscard]] double EvaluateGradientNoise(double U, double V, uint32_t Seed, double Gradient[2]) noexcept;

} // namespace Frontier
