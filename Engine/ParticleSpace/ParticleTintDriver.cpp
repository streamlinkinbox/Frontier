/*============================================================================================================================================
                                                         PARTICLETINTDRIVER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleTintDriver routing all 15 particle scalar channels into Frontier::ColourSpace

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleTintDriver.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

struct ChannelMetadataEntry
{
    ParticleScalarChannel Channel;
    std::string_view      PascalToken;
    std::string_view      HtmlToken;
    std::string_view      DisplayLabel;
};

constexpr std::array<ChannelMetadataEntry, 15u> ChannelRegistry = {{
    { ParticleScalarChannel::ModeDefault,             "ModeDefault",             "modeDefault",  "Mode Default"       },
    { ParticleScalarChannel::NormalizedLifetime,      "NormalizedLifetime",      "lifetime",     "Life (0-1)"         },
    { ParticleScalarChannel::ParticleAgeSeconds,      "ParticleAgeSeconds",      "age",          "Age (s)"            },
    { ParticleScalarChannel::VelocitySpeed,           "VelocitySpeed",           "speed",        "Speed (m/s)"        },
    { ParticleScalarChannel::VerticalVelocity,        "VerticalVelocity",        "velY",         "Vertical Velocity"  },
    { ParticleScalarChannel::OriginAzimuthAngle,      "OriginAzimuthAngle",      "angle",        "Origin Azimuth"     },
    { ParticleScalarChannel::VelocityHeadingAngle,    "VelocityHeadingAngle",    "velAngle",     "Velocity Heading"   },
    { ParticleScalarChannel::DistanceFromOrigin,      "DistanceFromOrigin",      "distOrigin",   "Dist From Origin"   },
    { ParticleScalarChannel::WorldElevation,          "WorldElevation",          "height",       "World Elevation"    },
    { ParticleScalarChannel::SpatialNoise3D,          "SpatialNoise3D",          "noise3D",      "3D Spatial Noise"   },
    { ParticleScalarChannel::PerParticleRandomSeed,   "PerParticleRandomSeed",   "random",       "Per-Particle Seed"  },
    { ParticleScalarChannel::SpinOrientationAngle,    "SpinOrientationAngle",    "spinAngle",    "Spin Orientation"   },
    { ParticleScalarChannel::DistanceToCamera,        "DistanceToCamera",        "distCamera",   "Camera Distance"    },
    { ParticleScalarChannel::ParticleSizeMeters,      "ParticleSizeMeters",      "particleSize", "Particle Size"      },
    { ParticleScalarChannel::AssignedLifespanSeconds, "AssignedLifespanSeconds", "lifeDuration", "Lifespan Duration"  },
}};

[[nodiscard]] bool EqualsIgnoreCase(std::string_view A, std::string_view B) noexcept
{
    if (A.size() != B.size())
    {
        return false;
    }
    for (std::size_t Index = 0u; Index < A.size(); ++Index)
    {
        const auto Ca = static_cast<unsigned char>(A[Index]);
        const auto Cb = static_cast<unsigned char>(B[Index]);
        if (std::tolower(Ca) != std::tolower(Cb))
        {
            return false;
        }
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOKEN RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

std::string_view QueryParticleScalarChannelToken(ParticleScalarChannel Channel) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Channel);
    if (Ordinal < ChannelRegistry.size())
    {
        return ChannelRegistry[Ordinal].PascalToken;
    }
    return "ModeDefault";
}

std::string_view QueryParticleScalarChannelLabel(ParticleScalarChannel Channel) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Channel);
    if (Ordinal < ChannelRegistry.size())
    {
        return ChannelRegistry[Ordinal].DisplayLabel;
    }
    return "Mode Default";
}

ParticleScalarChannel ParseParticleScalarChannelToken(std::string_view Token) noexcept
{
    for (const auto& Entry : ChannelRegistry)
    {
        if (EqualsIgnoreCase(Token, Entry.PascalToken) || EqualsIgnoreCase(Token, Entry.HtmlToken))
        {
            return Entry.Channel;
        }
    }
    return ParticleScalarChannel::ModeDefault;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCALAR & TINT EVALUATION
//------------------------------------------------------------------------------------------------------------------------

float ExtractParticleDriverScalar(
    ParticleScalarChannel             Channel,
    const ParticleChannelSampleInput& Input) noexcept
{
    const float InvTau = 1.0f / Constants::TauFloat;

    // Horizontal plane coordinates differ between Y-Up (X, Z) and +Z Up (X, Y)
    const float HorizDx = Input.PositionX - Input.OriginX;
    const float HorizDy = Input.WorldIsYUp ? (Input.PositionZ - Input.OriginZ) : (Input.PositionY - Input.OriginY);
    const float VelUp   = Input.WorldIsYUp ? Input.VelocityY : Input.VelocityZ;
    const float PosUp   = Input.WorldIsYUp ? Input.PositionY : Input.PositionZ;
    const float VelHorz = Input.WorldIsYUp ? Input.VelocityZ : Input.VelocityY;

    switch (Channel)
    {
    case ParticleScalarChannel::ModeDefault:
        return std::clamp(Input.ModeDefaultCoordinate, 0.0f, 1.0f);

    case ParticleScalarChannel::NormalizedLifetime:
    {
        const float SafeLife = (Input.LifespanSeconds > 1.0e6f)
                                   ? 3.0f
                                   : std::max(0.05f, Input.LifespanSeconds);
        return Input.AgeSeconds / SafeLife;
    }

    case ParticleScalarChannel::ParticleAgeSeconds:
        return Input.AgeSeconds;

    case ParticleScalarChannel::VelocitySpeed:
        return std::sqrt(
            Input.VelocityX * Input.VelocityX +
            Input.VelocityY * Input.VelocityY +
            Input.VelocityZ * Input.VelocityZ);

    case ParticleScalarChannel::VerticalVelocity:
        return VelUp;

    case ParticleScalarChannel::OriginAzimuthAngle:
        return (std::atan2(HorizDy, HorizDx) + Constants::PiFloat) * InvTau;

    case ParticleScalarChannel::VelocityHeadingAngle:
        return (std::atan2(VelHorz, Input.VelocityX) + Constants::PiFloat) * InvTau;

    case ParticleScalarChannel::DistanceFromOrigin:
    {
        const float Dx = Input.PositionX - Input.OriginX;
        const float Dy = Input.PositionY - Input.OriginY;
        const float Dz = Input.PositionZ - Input.OriginZ;
        return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    }

    case ParticleScalarChannel::WorldElevation:
        return PosUp;

    case ParticleScalarChannel::SpatialNoise3D:
    {
        const float Freq = std::max(0.25f, Input.NoiseFrequency);
        const float Drift = Input.ElapsedSeconds * 0.18f;
        const float Nx = Input.PositionX * Freq;
        const float Ny = Input.PositionY * Freq + (Input.WorldIsYUp ? Drift : 0.0f);
        const float Nz = Input.PositionZ * Freq + (Input.WorldIsYUp ? 0.0f : Drift);
        return ColourSpace::SampleLatticeNoise3D(Nx, Ny, Nz);
    }

    case ParticleScalarChannel::PerParticleRandomSeed:
        return ColourSpace::HashDeterministicFloat(Input.RandomSeed, 99u);

    case ParticleScalarChannel::SpinOrientationAngle:
    {
        const float Turns = Input.OrientationRadians * InvTau;
        return Turns - std::floor(Turns);
    }

    case ParticleScalarChannel::DistanceToCamera:
    {
        const float Dx = Input.PositionX - Input.CameraX;
        const float Dy = Input.PositionY - Input.CameraY;
        const float Dz = Input.PositionZ - Input.CameraZ;
        return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    }

    case ParticleScalarChannel::ParticleSizeMeters:
        return Input.SizeMeters;

    case ParticleScalarChannel::AssignedLifespanSeconds:
        return (Input.LifespanSeconds > 1.0e6f) ? 3.0f : Input.LifespanSeconds;

    default:
        return std::clamp(Input.ModeDefaultCoordinate, 0.0f, 1.0f);
    }
}

ColourSpace::LinearRgba EvaluateParticleTint(
    const ColourSpace::ColourSourceProfile& Profile,
    ParticleScalarChannel                   Channel,
    const ParticleChannelSampleInput&       Input) noexcept
{
    if (Channel == ParticleScalarChannel::ModeDefault)
    {
        ColourSpace::SpatialColourSampleInput SpatialInput {};
        SpatialInput.NormalizedCoordinate = std::clamp(Input.ModeDefaultCoordinate, 0.0f, 1.0f);
        SpatialInput.PositionX            = Input.PositionX;
        SpatialInput.PositionY            = Input.PositionY;
        SpatialInput.PositionZ            = Input.PositionZ;
        SpatialInput.ElapsedSeconds       = Input.ElapsedSeconds;
        SpatialInput.DiscreteOrdinal      = static_cast<uint32_t>(std::max(0.0f, Input.RandomSeed));
        return ColourSpace::SampleColourSource(Profile, SpatialInput);
    }

    const float RawReading = ExtractParticleDriverScalar(Channel, Input);
    const float Normalized = ColourSpace::NormalizeScalarToRange(RawReading, Profile.Range);

    ColourSpace::ColourSourceProfile DirectUnitRange = Profile;
    DirectUnitRange.Range.RangeMin = 0.0f;
    DirectUnitRange.Range.RangeMax = 1.0f;

    ColourSpace::SpatialColourSampleInput SpatialInput {};
    SpatialInput.NormalizedCoordinate = Normalized;
    SpatialInput.PositionX            = Input.PositionX;
    SpatialInput.PositionY            = Input.PositionY;
    SpatialInput.PositionZ            = Input.PositionZ;
    SpatialInput.ElapsedSeconds       = Input.ElapsedSeconds;
    SpatialInput.DiscreteOrdinal      = static_cast<uint32_t>(std::max(0.0f, Input.RandomSeed));
    return ColourSpace::SampleColourSource(DirectUnitRange, SpatialInput);
}

} // namespace Frontier::ParticleSpace
