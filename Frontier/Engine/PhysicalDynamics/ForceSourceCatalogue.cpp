/*==========================================================================================================================================
                                                         FORCESOURCECATALOGUE.CPP
==========================================================================================================================================*/
// 🧩 The sixteen force types of the approved Force Field editor, evaluated as its catalogue does, number for number.

#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier
{

namespace
{

constexpr double π        = 3.14159265358979323846;
constexpr double Radian   = π / 180.0;
constexpr double Infinity = std::numeric_limits<double>::infinity();

double Smooth01(double Share) noexcept
{
    const double Clamped = std::min(1.0, std::max(0.0, Share));
    return Clamped * Clamped * (3.0 - 2.0 * Clamped);
}

double Clamp(double Reading, double Lower, double Upper) noexcept
{
    return std::min(Upper, std::max(Lower, Reading));
}

// 📝 eos Falls(): how much of a field reaches a point, 1 at the centre and 0 at the edge.
double Falls(ForceFalloff Shape, double Distance, double Radius) noexcept
{
    if (Shape == ForceFalloff::None) return Distance < Radius ? 1.0 : 0.0;
    const double Share = Clamp(Distance / std::max(Radius, 1e-3), 0.0, 1.0);
    if (Shape == ForceFalloff::Linear) return 1.0 - Share;
    if (Shape == ForceFalloff::InverseSquare) return (1.0 / (1.0 + 8.0 * Share * Share)) * (Share < 1.0 ? 1.0 : 0.0);
    const double Soft = 1.0 - Share * Share;
    return Soft * Soft;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPE TABLE
//------------------------------------------------------------------------------------------------------------------------

using Configuration = ForceSourceConfiguration;

constexpr ForceParameterDescriptor Speed(double Configuration::*Member, const char* Identifier, const char* Label, double Minimum,
                                         double Maximum, double Step, const char* Unit, int Digits) noexcept
{
    return {Member, Identifier, Label, Minimum, Maximum, Step, Unit, Digits, false};
}

constexpr ForceParameterDescriptor Distance(double Configuration::*Member, const char* Identifier, const char* Label, double Minimum,
                                            double Maximum, double Step) noexcept
{
    return {Member, Identifier, Label, Minimum, Maximum, Step, "m", 1, true};
}

constexpr ForceParameterDescriptor TornadoParameters[] = {
    Speed(&Configuration::Strength, "strength", "Peak tangential speed", 1, 120, 0.5, "m/s", 1),
    Distance(&Configuration::InnerRadiusMetres, "inner_radius_metres", "Inner radius (ground)", 2, 20000, 1),
    Distance(&Configuration::CollarRadiusMetres, "collar_radius_metres", "Collar radius (cloud base)", 2, 40000, 1),
    Speed(&Configuration::InflowMetresPerSecond, "inflow_mps", "Radial inflow", 0, 60, 0.5, "m/s", 1),
    Speed(&Configuration::UpdraftMetresPerSecond, "updraft_mps", "Axial updraft", 0, 80, 0.5, "m/s", 1),
    Distance(&Configuration::HeightMetres, "height_metres", "Column height", 20, 15000, 10),
    Speed(&Configuration::TrackSpeedMetresPerSecond, "track_speed_mps", "Track speed", 0, 30, 0.1, "m/s", 1),
};
constexpr ForceParameterDescriptor GustFrontParameters[] = {
    Speed(&Configuration::Strength, "strength", "Peak surge speed", 1, 60, 0.5, "m/s", 1),
    Speed(&Configuration::FrontSpeedMetresPerSecond, "front_speed_mps", "Front speed", 0, 40, 0.5, "m/s", 1),
    Distance(&Configuration::CrestDepthMetres, "crest_depth_metres", "Crest depth", 5, 10000, 1),
    Distance(&Configuration::HalfWidthMetres, "half_width_metres", "Half width along the front", 10, 60000, 10),
    Distance(&Configuration::HeightMetres, "height_metres", "Gust head height", 5, 5000, 5),
};
constexpr ForceParameterDescriptor MicroburstParameters[] = {
    Speed(&Configuration::Strength, "strength", "Peak outflow speed", 1, 60, 0.5, "m/s", 1),
    Speed(&Configuration::DowndraftMetresPerSecond, "downdraft_mps", "Downdraft speed", 0, 60, 0.5, "m/s", 1),
    Distance(&Configuration::InnerRadiusMetres, "inner_radius_metres", "Column radius", 5, 10000, 1),
    Distance(&Configuration::RingRadiusMetres, "ring_radius_metres", "Outflow ring radius", 10, 20000, 1),
};
constexpr ForceParameterDescriptor ThermalParameters[] = {
    Speed(&Configuration::Strength, "strength", "Peak updraft", 0.5, 40, 0.1, "m/s", 1),
    Speed(&Configuration::InflowMetresPerSecond, "inflow_mps", "Entrainment inflow", 0, 20, 0.1, "m/s", 1),
    Distance(&Configuration::InnerRadiusMetres, "inner_radius_metres", "Warm column radius", 2, 5000, 1),
    Distance(&Configuration::HeightMetres, "height_metres", "Chimney height", 15, 12000, 5),
};
constexpr ForceParameterDescriptor JetParameters[] = {
    Speed(&Configuration::Strength, "strength", "Axial speed", 0.5, 60, 0.5, "m/s", 1),
    Distance(&Configuration::LengthMetres, "length_metres", "Corridor length", 2, 30000, 1),
    Distance(&Configuration::InnerRadiusMetres, "inner_radius_metres", "Corridor radius", 0.5, 5000, 0.5),
};
constexpr ForceParameterDescriptor PuffParameters[] = {
    Speed(&Configuration::Strength, "strength", "Surge speed", 0, 40, 0.1, "m/s", 1),
    Speed(&Configuration::LiftMetresPerSecond, "lift_mps", "Vertical surge", -10, 10, 0.1, "m/s", 1),
    Distance(&Configuration::VerticalRadiusMetres, "vertical_radius_metres", "Vertical radius", 1, 2000, 0.5),
    Speed(&Configuration::Drift, "drift", "Drift with the wind", 0, 1.5, 0.01, "×", 2),
};
constexpr ForceParameterDescriptor BlastParameters[] = {
    Speed(&Configuration::Strength, "strength", "Peak radial speed", 1, 200, 1, "m/s", 0),
    Speed(&Configuration::ExpansionMetresPerSecond, "expansion_mps", "Wavefront speed", 5, 400, 1, "m/s", 0),
    Distance(&Configuration::ThicknessMetres, "thickness_metres", "Shell thickness", 0.5, 2000, 0.5),
    Distance(&Configuration::InitialRadiusMetres, "initial_radius_metres", "Initial radius", 0.5, 1000, 0.5),
};
constexpr ForceParameterDescriptor WakeParameters[] = {
    Speed(&Configuration::Strength, "strength", "Slipstream tow", 0, 40, 0.1, "m/s", 1),
    Speed(&Configuration::CirculationMetresPerSecond, "circulation_mps", "Trailing circulation", 0, 30, 0.1, "m/s", 1),
    Speed(&Configuration::BodySpeedMetresPerSecond, "body_speed_mps", "Body speed", 0, 90, 0.5, "m/s", 1),
    Distance(&Configuration::HalfWidthMetres, "half_width_metres", "Half track", 0.2, 40, 0.05),
    Distance(&Configuration::LengthMetres, "length_metres", "Wake length", 2, 600, 0.5),
    Distance(&Configuration::HeightMetres, "height_metres", "Wake height", 0.3, 60, 0.1),
};
constexpr ForceParameterDescriptor CurrentParameters[] = {
    Speed(&Configuration::Strength, "strength", "Flow speed", 0, 40, 0.1, "m/s", 1),
};
constexpr ForceParameterDescriptor AttractorParameters[] = {
    Speed(&Configuration::Strength, "strength", "Pull", 0, 60, 0.1, "m/s²", 1),
    Speed(&Configuration::Swirl, "swirl", "Swirl", 0, 30, 0.1, "m/s²", 1),
    Speed(&Configuration::SwallowRadiusMetres, "swallow_radius_metres", "Swallow radius", 0, 500, 0.5, "m", 1),
};
constexpr ForceParameterDescriptor RepulsorParameters[] = {Speed(&Configuration::Strength, "strength", "Push", 0, 60, 0.1, "m/s²", 1)};
constexpr ForceParameterDescriptor OrbitParameters[] = {
    Speed(&Configuration::Strength, "strength", "Tangential pull", 0, 40, 0.1, "m/s²", 1),
};
constexpr ForceParameterDescriptor LiftParameters[] = {
    Speed(&Configuration::Strength, "strength", "Upward acceleration", 0, 40, 0.1, "m/s²", 1),
};
constexpr ForceParameterDescriptor MagneticParameters[] = {Speed(&Configuration::Strength, "strength", "Drive", 0, 30, 0.1, "m/s²", 1)};
constexpr ForceParameterDescriptor GravityParameters[] = {
    Speed(&Configuration::Strength, "strength", "Acceleration", 0, 30, 0.01, "m/s²", 2),
};
constexpr ForceParameterDescriptor DragParameters[] = {Speed(&Configuration::Strength, "strength", "Damping rate", 0, 10, 0.01, "1/s", 2)};

constexpr ForceBearingDescriptor TrackBearing[] = {{&Configuration::TrackBearingDegrees, "track_bearing_deg", "Track toward"}};
constexpr ForceBearingDescriptor FrontBearing[] = {{&Configuration::BearingDegrees, "bearing_deg", "Travels toward"}};
constexpr ForceBearingDescriptor AxisBearing[]  = {{&Configuration::BearingDegrees, "bearing_deg", "Axis toward"}};
constexpr ForceBearingDescriptor SurgeBearing[] = {{&Configuration::BearingDegrees, "bearing_deg", "Surge toward"}};
constexpr ForceBearingDescriptor BodyBearing[]  = {{&Configuration::BearingDegrees, "bearing_deg", "Body travels toward"}};
constexpr ForceBearingDescriptor FlowBearing[]  = {{&Configuration::BearingDegrees, "bearing_deg", "Flows toward"}};

using C = ForceSourceCategory;
using R = ForceReach;

// 📝 ForceFieldCatalogue.js Types, in its order (the library and the Add menu list them so).
const ForceSourceDescriptor Descriptors[] = {
    {C::TornadoColumn, "tornado_column", "Tornado column", ForceContribution::Flow, "Tornado", R::Column,
     "A Burgers–Rott column: tangential speed peaks at the inner radius, boundary-layer inflow near the ground, a helical "
     "updraft inside, and a collar that widens toward cloud base. It travels along its track.",
     TornadoParameters, TrackBearing, true},
    {C::GustFront, "gust_front", "Gust front", ForceContribution::Flow, "Front", R::Everywhere,
     "A travelling surge: the nose lifts the air, the wake behind it decays over a few crest depths. At kilometre scale it "
     "is a squall line or a haboob wall.",
     GustFrontParameters, FrontBearing, false},
    {C::Microburst, "microburst", "Microburst", ForceContribution::Flow, "Downburst", R::Column,
     "A convective downdraft column that hits the ground and spreads as a ring of outflow. The centre height is the core "
     "altitude.",
     MicroburstParameters, {}, false},
    {C::ThermalUpdraft, "thermal_updraft", "Thermal updraft", ForceContribution::Flow, "Thermal", R::Column,
     "A buoyant chimney of rising air: entrainment inflow in the lower third, divergence near the top where a cumulus "
     "turret forms.",
     ThermalParameters, {}, false},
    {C::JetCorridor, "jet_corridor", "Jet corridor", ForceContribution::Flow, "Jet", R::Box,
     "A Venturi corridor: flow funnelled along an axis, widening downstream — a canyon, a tunnel draft, a street between "
     "towers.",
     JetParameters, AxisBearing, false},
    {C::GustPuff, "gust_puff", "Gust puff", ForceContribution::Flow, "Puff", R::Sphere,
     "A Gaussian packet of faster air that drifts downstream with the wind at its altitude.", PuffParameters, SurgeBearing, false},
    {C::BlastWave, "blast_wave", "Blast wave", ForceContribution::Flow, "Blast", R::Sphere,
     "An expanding shell r(t) = r₀ + c·t pushing air outward, weakening with distance. Fire it from the timeline or the "
     "Fire button.",
     BlastParameters, {}, false},
    {C::VehicleWake, "vehicle_wake", "Vehicle wake", ForceContribution::Flow, "Wake", R::Box,
     "The slipstream behind a moving body: a forward tow and a counter-rotating pair that lifts spray and dust. Ready for "
     "the vehicle step.",
     WakeParameters, BodyBearing, false},
    {C::Current, "current", "Current", ForceContribution::Flow, "Current", R::Box,
     "A steady flow inside a box: a river, a conveyor of air, an HVAC duct. The eos Current field.", CurrentParameters, FlowBearing,
     false},
    {C::Attractor, "attractor", "Attractor", ForceContribution::Accelerate, "Attract", R::Sphere,
     "Pulls toward its centre. With swirl it behaves like a drain; with a swallow radius it removes what reaches it (the "
     "black hole preset).",
     AttractorParameters, {}, false},
    {C::Repulsor, "repulsor", "Repulsor", ForceContribution::Accelerate, "Repel", R::Sphere,
     "Pushes away from its centre — a fan, a shock-free blast zone, a keep-out field.", RepulsorParameters, {}, false},
    {C::Orbit, "orbit", "Orbit", ForceContribution::Accelerate, "ForceOrbit", R::Sphere,
     "Tangential acceleration around world up, with no inward part: receivers circle the centre.", OrbitParameters, {}, false},
    {C::Lift, "lift", "Lift", ForceContribution::Accelerate, "Lift", R::Sphere,
     "Straight up, fading toward the edge — a column of buoyancy for particles that ignore the wind.", LiftParameters, {}, false},
    {C::MagneticDipole, "magnetic_dipole", "Magnetic dipole", ForceContribution::Accelerate, "Magnet", R::Sphere,
     "Drives receivers along the field lines of a dipole whose axis is world Z.", MagneticParameters, {}, false},
    {C::Gravity, "gravity", "Gravity", ForceContribution::Accelerate, "Gravity", R::Everywhere,
     "Unbounded and an acceleration, so no coupling can soften it. Author “this room is on the Moon” with a sphere reach.",
     GravityParameters, {}, false},
    {C::DragVolume, "drag_volume", "Drag volume", ForceContribution::Damp, "Damp", R::Box,
     "Bleeds speed away inside its volume — water, mud, a slow-motion zone.", DragParameters, {}, false},
};

const ForceContributionDescriptor Contributions[] = {
    {ForceContribution::Flow, "flow", "Flow", "m/s", 0x8fd3c4u,
     "Velocity the air carries. A receiver relaxes toward it at its own coupling, so one gust moves a leaf and a hailstone "
     "differently. Flow sums into one lattice however many fields there are."},
    {ForceContribution::Accelerate, "accelerate", "Accelerate", "m/s²", 0xe8c27au,
     "Added straight to velocity, whatever the receiver’s coupling: gravity, attraction and repulsion. Evaluated per "
     "receiver, so each costs more than a flow field."},
    {ForceContribution::Damp, "damp", "Damp", "1/s", 0xb9a3e8u,
     "Scales velocity down. Removes energy without adding a direction, so it can never start anything moving."},
};

const ForceReceiverDescriptor Receivers[] = {
    {ForceReceiverParticles, "particles", "Particles"},
    {ForceReceiverDebris, "debris", "Debris"},
    {ForceReceiverPrecipitation, "precipitation", "Rain & hail"},
    {ForceReceiverClouds, "clouds", "Clouds"},
    {ForceReceiverPlumes, "plumes", "Plumes"},
    {ForceReceiverFoliage, "foliage", "Foliage"},
    {ForceReceiverVehicles, "vehicles", "Vehicles"},
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

struct BoxExtents
{
    double HalfX   = 0.5;
    double HalfZ   = 0.5;
    double Height  = 1.0;
    double OffsetZ = 0.0;
};

// 📝 Boxes follow the type's geometry: a jet's corridor, a wake's trail, otherwise the authored half extents.
BoxExtents ResolveBoxExtents(const ForceSourceConfiguration& Source) noexcept
{
    if (Source.Category == C::JetCorridor)
        return {Source.InnerRadiusMetres * 3.2, Source.LengthMetres * 0.75, Source.CentreMetres[2] + Source.InnerRadiusMetres * 3.2,
                Source.LengthMetres * 0.5};
    if (Source.Category == C::VehicleWake)
        return {Source.HalfWidthMetres * 4.0, Source.LengthMetres * 1.2, Source.HeightMetres * 2.4, -Source.LengthMetres};
    return {std::max(0.5, Source.HalfExtentsMetres[0]), std::max(0.5, Source.HalfExtentsMetres[1]), std::max(1.0, Source.HeightMetres), 0.0};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLOW TYPES
//------------------------------------------------------------------------------------------------------------------------

void EvaluateFlowSource(const ForceSourceConfiguration& Source,
                        const ForceWindReading&        Wind,
                        double X, double Y, double Z,
                        double Age, double Envelope,
                        SpatialVector Centre,
                        double Out[3]) noexcept
{
    const double Ground = Wind.GroundHeightMetres;
    switch (Source.Category)
    {
    case C::TornadoColumn:
    {
        const double RelativeY = Y - Centre.Y;
        const double SpanH     = std::max(10.0, Source.HeightMetres);
        if (RelativeY < -15.0 || RelativeY > SpanH * 1.3) return;
        const double HeightShare = Clamp(RelativeY / SpanH, 0.0, 1.0);
        const double TopTaper    = RelativeY <= SpanH ? 1.0 : 1.0 - Smooth01((RelativeY - SpanH) / (0.3 * SpanH));
        const double DeltaX = X - Centre.X, DeltaZ = Z - Centre.Z;
        const double Separation  = std::hypot(DeltaX, DeltaZ);
        const double InnerRadius = std::max(1.0, Source.InnerRadiusMetres +
                                                     (Source.CollarRadiusMetres - Source.InnerRadiusMetres) * Smooth01(HeightShare));
        const double ρ          = Separation / InnerRadius;
        const double Profile    = (2.0 * ρ) / (1.0 + ρ * ρ);
        const double Tangential = Envelope * TopTaper * Source.Strength * Profile * (Source.Spin < 0.0 ? -1.0 : 1.0);
        const double InflowBand = std::exp(-std::max(0.0, RelativeY) / std::max(12.0, 0.16 * SpanH)) -
                                  0.35 * Smooth01((HeightShare - 0.72) / 0.28);
        const double Radial  = -Envelope * TopTaper * Source.InflowMetresPerSecond * InflowBand * Profile;
        const double Updraft = Envelope * TopTaper * Source.UpdraftMetresPerSecond * std::exp(-0.42 * ρ * ρ) * Smooth01(RelativeY / 8.0 + 0.15);
        const double UnitX = Separation > 1e-5 ? DeltaX / Separation : 1.0;
        const double UnitZ = Separation > 1e-5 ? DeltaZ / Separation : 0.0;
        // 📝 Spin +1 is cyclonic: counter-clockwise seen from above (east of the centre the air moves north, −Z).
        const double Turn = Source.TrackBearingDegrees * Radian, Carry = Envelope * 0.25 * Source.TrackSpeedMetresPerSecond;
        Out[0] += UnitZ * Tangential + UnitX * Radial + std::sin(Turn) * Carry;
        Out[1] += Updraft;
        Out[2] += -UnitX * Tangential + UnitZ * Radial - std::cos(Turn) * Carry;
        return;
    }
    case C::GustFront:
    {
        const double Turn = Source.BearingDegrees * Radian, PropagationX = std::sin(Turn), PropagationZ = -std::cos(Turn);
        const double Behind  = (Centre.X - X) * PropagationX + (Centre.Z - Z) * PropagationZ;
        const double Lateral = std::abs((X - Centre.X) * -PropagationZ + (Z - Centre.Z) * PropagationX);
        const double Depth = std::max(5.0, Source.CrestDepthMetres), RelativeY = Y - Centre.Y;
        if (Behind < -0.25 * Depth || Behind > 2.4 * Depth || RelativeY < 0.0 || RelativeY > Source.HeightMetres * 1.25 ||
            Lateral > Source.HalfWidthMetres * 1.2)
            return;
        const double NoseRise        = Smooth01((Behind + 0.25 * Depth) / (0.35 * Depth));
        const double WakeDecay       = std::exp(-std::max(0.0, Behind - 0.1 * Depth) / Depth);
        const double HeightEnvelope  = 1.0 - Smooth01(RelativeY / std::max(5.0, Source.HeightMetres));
        const double LateralEnvelope = 1.0 - Smooth01((Lateral - Source.HalfWidthMetres * 0.8) / (Source.HalfWidthMetres * 0.4));
        const double Surge           = Envelope * NoseRise * WakeDecay * HeightEnvelope * LateralEnvelope;
        const double NoseLift        = Envelope * NoseRise * std::exp(-std::abs(Behind) / (0.28 * Depth)) * HeightEnvelope *
                                LateralEnvelope * 0.28 * Source.Strength;
        Out[0] += PropagationX * Source.Strength * Surge;
        Out[1] += NoseLift;
        Out[2] += PropagationZ * Source.Strength * Surge;
        return;
    }
    case C::Microburst:
    {
        const double DeltaX = X - Centre.X, DeltaZ = Z - Centre.Z;
        const double Separation = std::hypot(DeltaX, DeltaZ), RelativeY = Y - Ground;
        const double ColumnRadius = std::max(5.0, Source.InnerRadiusMetres), Ring = std::max(10.0, Source.RingRadiusMetres);
        if (RelativeY < 0.0 || RelativeY > std::max(40.0, Centre.Y * 1.4)) return;
        const double DowndraftColumn = std::exp(-(Separation * Separation) / (ColumnRadius * ColumnRadius)) * Smooth01(RelativeY / 35.0);
        const double OutRing = (Separation / Ring) * std::exp(-0.5 * std::pow(Separation / Ring, 2.0)) *
                               std::exp(-RelativeY / std::max(42.0, Ring * 0.28));
        const double Inverse = Separation > 1e-4 ? 1.0 / Separation : 0.0;
        Out[0] += Envelope * Source.Strength * OutRing * DeltaX * Inverse;
        Out[1] -= Envelope * Source.DowndraftMetresPerSecond * DowndraftColumn;
        Out[2] += Envelope * Source.Strength * OutRing * DeltaZ * Inverse;
        return;
    }
    case C::ThermalUpdraft:
    {
        const double RelativeY = Y - Centre.Y, ColumnHeight = std::max(15.0, Source.HeightMetres);
        if (RelativeY < 0.0 || RelativeY > ColumnHeight * 1.35) return;
        const double HeightShare = Clamp(RelativeY / ColumnHeight, 0.0, 1.35);
        const double DeltaX = X - Centre.X, DeltaZ = Z - Centre.Z, Separation = std::hypot(DeltaX, DeltaZ);
        const double PlumeRadius = std::max(2.0, Source.InnerRadiusMetres * (1.0 + 0.45 * HeightShare));
        const double ρ           = Separation / PlumeRadius;
        if (ρ >= 3.5) return;
        const double ChimneyEnvelope = Envelope * std::exp(-0.5 * ρ * ρ) * Smooth01(RelativeY / 10.0 + 0.15) *
                                       (1.0 - Smooth01((HeightShare - 1.0) / 0.35));
        const double RadialMode = -Smooth01(0.35 - HeightShare) + 0.55 * Smooth01((HeightShare - 0.72) / 0.28);
        const double Inverse    = Separation > 1e-4 ? 1.0 / Separation : 0.0;
        const double Radial     = Envelope * Source.InflowMetresPerSecond * RadialMode * (2.0 * ρ / (1.0 + ρ * ρ));
        Out[0] += DeltaX * Inverse * Radial;
        Out[1] += Source.Strength * ChimneyEnvelope;
        Out[2] += DeltaZ * Inverse * Radial;
        return;
    }
    case C::JetCorridor:
    {
        const double Turn = Source.BearingDegrees * Radian, AxisX = std::sin(Turn), AxisZ = -std::cos(Turn);
        const double RelativeX = X - Centre.X, RelativeY = Y - Centre.Y, RelativeZ = Z - Centre.Z;
        const double Axial  = RelativeX * AxisX + RelativeZ * AxisZ;
        const double Length = std::max(2.0, Source.LengthMetres), CorridorRadius = std::max(0.5, Source.InnerRadiusMetres);
        if (Axial < -0.2 * CorridorRadius || Axial > 1.25 * Length) return;
        const double PerpendicularX = RelativeX - AxisX * Axial, PerpendicularZ = RelativeZ - AxisZ * Axial;
        const double RadialSquared  = PerpendicularX * PerpendicularX + RelativeY * RelativeY + PerpendicularZ * PerpendicularZ;
        const double JetRadius      = CorridorRadius * (1.0 + 0.35 * std::max(0.0, Axial) / Length);
        if (RadialSquared >= 9.0 * JetRadius * JetRadius) return;
        const double AxialEnvelope = Smooth01((Axial + 0.2 * CorridorRadius) / (0.4 * CorridorRadius)) *
                                     (1.0 - Smooth01((Axial - 0.85 * Length) / (0.4 * Length)));
        const double Gain = Envelope * AxialEnvelope * std::exp(-0.5 * RadialSquared / (JetRadius * JetRadius)) * Source.Strength;
        Out[0] += AxisX * Gain;
        Out[2] += AxisZ * Gain;
        return;
    }
    case C::GustPuff:
    {
        const double RadiusXZ = std::max(1.5, Source.ReachMetres), RadiusY = std::max(1.0, Source.VerticalRadiusMetres);
        const double U = (X - Centre.X) / RadiusXZ, V = (Y - Centre.Y) / RadiusY, W = (Z - Centre.Z) / RadiusXZ;
        const double Squared = U * U + V * V + W * W;
        if (Squared >= 9.0) return;
        const double Gauss = Envelope * std::exp(-0.5 * Squared);
        const double Turn  = Source.BearingDegrees * Radian;
        Out[0] += std::sin(Turn) * Source.Strength * Gauss;
        Out[1] += Source.LiftMetresPerSecond * Gauss;
        Out[2] += -std::cos(Turn) * Source.Strength * Gauss;
        return;
    }
    case C::BlastWave:
    {
        const double InitialRadius = std::max(0.5, Source.InitialRadiusMetres);
        const double FrontRadius   = InitialRadius + std::max(0.0, Source.ExpansionMetresPerSecond) * Age;
        const double Thickness     = std::max(0.5, Source.ThicknessMetres);
        const double DeltaX = X - Centre.X, DeltaY = Y - Centre.Y, DeltaZ = Z - Centre.Z;
        const double Separation = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
        const double FrontShare = (Separation - FrontRadius) / Thickness;
        if (std::abs(FrontShare) >= 3.2 || Separation < 1e-4) return;
        const double Attenuation = std::pow(InitialRadius / std::max(InitialRadius, FrontRadius), 0.65);
        const double Outward = Source.Strength * Envelope * Attenuation * std::exp(-0.5 * FrontShare * FrontShare) / Separation;
        Out[0] += DeltaX * Outward;
        Out[1] += DeltaY * Outward;
        Out[2] += DeltaZ * Outward;
        return;
    }
    case C::VehicleWake:
    {
        const double Turn = Source.BearingDegrees * Radian, ForwardX = std::sin(Turn), ForwardZ = -std::cos(Turn);
        const double RightX = -ForwardZ, RightZ = ForwardX;
        const double RelativeX = X - Centre.X, RelativeY = Y - Ground, RelativeZ = Z - Centre.Z;
        const double Trail           = -(RelativeX * ForwardX + RelativeZ * ForwardZ);
        const double LateralDistance = RelativeX * RightX + RelativeZ * RightZ;
        const double WakeLength = std::max(4.0, Source.LengthMetres), HalfWidth = std::max(0.4, Source.HalfWidthMetres);
        const double WakeHeight = std::max(0.8, Source.HeightMetres);
        if (Trail < -0.5 * HalfWidth || Trail > 2.2 * WakeLength || RelativeY < 0.0 || RelativeY > 2.4 * WakeHeight) return;
        const double Spread       = 1.0 + 0.85 * std::max(0.0, Trail) / WakeLength;
        const double LongEnvelope = Smooth01((Trail + 0.5 * HalfWidth) / HalfWidth) * std::exp(-std::max(0.0, Trail) / WakeLength);
        const double LateralShare = LateralDistance / (HalfWidth * Spread);
        const double Weight = Envelope * LongEnvelope * std::exp(-0.5 * LateralShare * LateralShare) *
                              (1.0 - Smooth01(RelativeY / (1.8 * WakeHeight * Spread)));
        const double Upwash     = std::max(0.0, (1.0 - 0.8 * LateralShare * LateralShare) * Source.CirculationMetresPerSecond);
        const double InwardRoll = -LateralShare * 0.65 * Source.CirculationMetresPerSecond;
        const double Tow        = Source.Strength * std::min(1.0, Source.BodySpeedMetresPerSecond / 22.0);
        Out[0] += Weight * (ForwardX * Tow + RightX * InwardRoll);
        Out[1] += Weight * Upwash;
        Out[2] += Weight * (ForwardZ * Tow + RightZ * InwardRoll);
        return;
    }
    case C::Current:
    {
        const double Turn = Source.BearingDegrees * Radian;
        Out[0] += std::sin(Turn) * Source.Strength * Envelope;
        Out[2] += -std::cos(Turn) * Source.Strength * Envelope;
        return;
    }
    default:
        return;
    }
}

// 📝 The eos forcefields.js SampleField for the accelerate types, and the GPU's dipole for the magnetic one.
void EvaluateAccelerateSource(const ForceSourceConfiguration& Source,
                              double X, double Y, double Z,
                              double Envelope, double Share,
                              SpatialVector Centre,
                              double Out[3]) noexcept
{
    const double Scale = Source.Strength * Share * Envelope;
    if (Source.Category == C::Gravity)
    {
        Out[1] -= Scale;
        return;
    }
    if (Source.Category == C::Lift)
    {
        Out[1] += Scale;
        return;
    }
    const double DeltaX = X - Centre.X, DeltaY = Y - Centre.Y, DeltaZ = Z - Centre.Z;
    const double Separation = std::max(1e-3, std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ));
    const double OutX = DeltaX / Separation, OutY = DeltaY / Separation, OutZ = DeltaZ / Separation;
    if (Source.Category == C::Repulsor)
    {
        Out[0] += OutX * Scale;
        Out[1] += OutY * Scale;
        Out[2] += OutZ * Scale;
        return;
    }
    if (Source.Category == C::MagneticDipole)
    {
        const double Clipped = std::max(Separation, 0.4);
        const double UnitX = DeltaX / Clipped, UnitY = DeltaY / Clipped, UnitZ = DeltaZ / Clipped;
        const double Along  = UnitZ * 3.0;
        const double FieldX = Along * UnitX, FieldY = Along * UnitY, FieldZ = Along * UnitZ - 1.0;
        double       Length = std::sqrt(FieldX * FieldX + FieldY * FieldY + FieldZ * FieldZ);
        if (Length == 0.0) Length = 1.0;
        Out[0] += FieldX / Length * Scale;
        Out[1] += FieldY / Length * Scale;
        Out[2] += FieldZ / Length * Scale;
        return;
    }
    double TangentLength = std::hypot(OutZ, OutX);
    if (TangentLength == 0.0) TangentLength = 1.0;
    const double TangentX = -OutZ / TangentLength, TangentZ = OutX / TangentLength;
    if (Source.Category == C::Orbit)
    {
        Out[0] += TangentX * Scale;
        Out[2] += TangentZ * Scale;
        return;
    }
    const double Turn = Source.Swirl * Share * Envelope;
    Out[0] += -OutX * Scale + TangentX * Turn;
    Out[1] += -OutY * Scale;
    Out[2] += -OutZ * Scale + TangentZ * Turn;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GRADIENT NOISE
//------------------------------------------------------------------------------------------------------------------------

uint32_t GradientHash(int64_t ColumnIndex, int64_t RowIndex, uint32_t Seed) noexcept
{
    uint32_t Mixed = (static_cast<uint32_t>(ColumnIndex) * 374761393u) ^ (static_cast<uint32_t>(RowIndex) * 668265263u) ^
                     (Seed * 1274126177u);
    Mixed = (Mixed ^ (Mixed >> 13u)) * 1274126177u;
    return Mixed ^ (Mixed >> 16u);
}

struct GradientDirectionTable
{
    double Directions[32]{};

    GradientDirectionTable() noexcept
    {
        for (int Index = 0; Index < 16; ++Index)
        {
            Directions[Index * 2]     = std::cos((Index + 0.5) / 16.0 * π * 2.0);
            Directions[Index * 2 + 1] = std::sin((Index + 0.5) / 16.0 * π * 2.0);
        }
    }
};

const GradientDirectionTable GradientDirections{};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTORS
//------------------------------------------------------------------------------------------------------------------------

std::span<const ForceSourceDescriptor> EnumerateForceSourceDescriptors() noexcept
{
    return Descriptors;
}

const ForceSourceDescriptor& ResolveForceSourceDescriptor(ForceSourceCategory Category) noexcept
{
    for (const ForceSourceDescriptor& Descriptor : Descriptors)
        if (Descriptor.Category == Category) return Descriptor;
    return Descriptors[5];   // a gust puff stands in for an unknown category
}

const ForceSourceDescriptor* ResolveForceSourceDescriptor(std::string_view Identifier) noexcept
{
    for (const ForceSourceDescriptor& Descriptor : Descriptors)
        if (Identifier == Descriptor.Identifier) return &Descriptor;
    return nullptr;
}

const ForceContributionDescriptor& ResolveForceContributionDescriptor(ForceContribution Contribution) noexcept
{
    return Contributions[std::min<size_t>(static_cast<size_t>(Contribution), 2)];
}

std::span<const ForceReceiverDescriptor> EnumerateForceReceivers() noexcept
{
    return Receivers;
}

ForceSourceConfiguration ConstructForceSource(ForceSourceCategory Category) noexcept
{
    const ForceSourceDescriptor& Descriptor = ResolveForceSourceDescriptor(Category);
    ForceSourceConfiguration     Source;
    Source.Category = Descriptor.Category;
    Source.Reach    = Descriptor.Reach;
    double Altitude = 0.0;
    switch (Descriptor.Category)
    {
    case C::TornadoColumn:
        Source.Strength = 68; Source.InnerRadiusMetres = 20; Source.CollarRadiusMetres = 96; Source.InflowMetresPerSecond = 26;
        Source.UpdraftMetresPerSecond = 42; Source.HeightMetres = 950; Source.Spin = 1; Source.TrackBearingDegrees = 109;
        Source.TrackSpeedMetresPerSecond = 3.7; Source.ReachMetres = 600;
        break;
    case C::GustFront:
        Source.Strength = 29; Source.FrontSpeedMetresPerSecond = 18; Source.CrestDepthMetres = 85; Source.HeightMetres = 140;
        Source.HalfWidthMetres = 900; Source.BearingDegrees = 113; Source.ReachMetres = 900;
        break;
    case C::Microburst:
        Source.Strength = 31; Source.DowndraftMetresPerSecond = 24; Source.InnerRadiusMetres = 65; Source.RingRadiusMetres = 150;
        Source.HeightMetres = 364; Source.ReachMetres = 600; Altitude = 260;
        break;
    case C::ThermalUpdraft:
        Source.Strength = 14.5; Source.InflowMetresPerSecond = 4.8; Source.InnerRadiusMetres = 48; Source.HeightMetres = 620;
        Source.ReachMetres = 170;
        break;
    case C::JetCorridor:
        Source.Strength = 24; Source.LengthMetres = 95; Source.InnerRadiusMetres = 12; Source.BearingDegrees = 90; Source.ReachMetres = 120;
        Altitude = 4;
        break;
    case C::GustPuff:
        Source.Strength = 12; Source.LiftMetresPerSecond = 0.8; Source.ReachMetres = 28; Source.VerticalRadiusMetres = 14;
        Source.Drift = 0.92; Source.BearingDegrees = 112; Altitude = 12;
        break;
    case C::BlastWave:
        Source.Strength = 42; Source.ExpansionMetresPerSecond = 110; Source.ThicknessMetres = 14; Source.InitialRadiusMetres = 3;
        Source.ReachMetres = 1500; Source.Falloff = ForceFalloff::None; Source.Activation = ForceActivation::Window;
        Source.BeginsSeconds = 6; Source.LastsSeconds = 14; Source.RepeatsSeconds = 30; Source.RampInSeconds = 0;
        Source.RampOutSeconds = 2; Altitude = 2;
        break;
    case C::VehicleWake:
        Source.Strength = 13.5; Source.CirculationMetresPerSecond = 7.5; Source.BodySpeedMetresPerSecond = 22; Source.HalfWidthMetres = 1.15;
        Source.LengthMetres = 36; Source.HeightMetres = 3.4; Source.BearingDegrees = 0; Source.ReachMetres = 80; Altitude = 0.65;
        break;
    case C::Current:
        Source.Strength = 4; Source.BearingDegrees = 90; Source.HalfExtentsMetres[0] = 60; Source.HalfExtentsMetres[1] = 20;
        Source.HeightMetres = 30; Source.ReachMetres = 60;
        break;
    case C::Attractor:
        Source.Strength = 6; Source.Swirl = 2; Source.SwallowRadiusMetres = 0; Source.ReachMetres = 260; Altitude = 10;
        break;
    case C::Repulsor: Source.Strength = 8; Source.ReachMetres = 180; Altitude = 10; break;
    case C::Orbit: Source.Strength = 4; Source.ReachMetres = 220; Altitude = 10; break;
    case C::Lift: Source.Strength = 5; Source.ReachMetres = 120; Altitude = 10; break;
    case C::MagneticDipole: Source.Strength = 3; Source.ReachMetres = 160; Altitude = 10; break;
    case C::Gravity: Source.Strength = 9.81; Source.ReachMetres = 500; Source.Falloff = ForceFalloff::None; break;
    case C::DragVolume:
        Source.Strength = 0.8; Source.HalfExtentsMetres[0] = 150; Source.HalfExtentsMetres[1] = 80; Source.HeightMetres = 40;
        Source.ReachMetres = 150; Source.BearingDegrees = 0;
        break;
    default: break;
    }
    Source.CentreMetres[2] = Altitude;
    return Source;
}

const char* ResolveForceReachIdentifier(ForceReach Reach) noexcept
{
    switch (Reach)
    {
    case ForceReach::Everywhere: return "everywhere";
    case ForceReach::Sphere: return "sphere";
    case ForceReach::Column: return "column";
    case ForceReach::Box: return "box";
    }
    return "sphere";
}

const char* ResolveForceFalloffIdentifier(ForceFalloff Falloff) noexcept
{
    switch (Falloff)
    {
    case ForceFalloff::None: return "none";
    case ForceFalloff::Linear: return "linear";
    case ForceFalloff::Smooth: return "smooth";
    case ForceFalloff::InverseSquare: return "inverse_square";
    }
    return "smooth";
}

const char* ResolveForceActivationIdentifier(ForceActivation Activation) noexcept
{
    switch (Activation)
    {
    case ForceActivation::Always: return "always";
    case ForceActivation::Window: return "window";
    case ForceActivation::Trigger: return "trigger";
    }
    return "always";
}

ForceReach ParseForceReach(std::string_view Identifier, ForceReach Fallback) noexcept
{
    for (ForceReach Reach : {ForceReach::Everywhere, ForceReach::Sphere, ForceReach::Column, ForceReach::Box})
        if (Identifier == ResolveForceReachIdentifier(Reach)) return Reach;
    return Fallback;
}

ForceFalloff ParseForceFalloff(std::string_view Identifier, ForceFalloff Fallback) noexcept
{
    for (ForceFalloff Falloff : {ForceFalloff::None, ForceFalloff::Linear, ForceFalloff::Smooth, ForceFalloff::InverseSquare})
        if (Identifier == ResolveForceFalloffIdentifier(Falloff)) return Falloff;
    return Fallback;
}

ForceActivation ParseForceActivation(std::string_view Identifier, ForceActivation Fallback) noexcept
{
    for (ForceActivation Activation : {ForceActivation::Always, ForceActivation::Window, ForceActivation::Trigger})
        if (Identifier == ResolveForceActivationIdentifier(Activation)) return Activation;
    return Fallback;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ACTIVATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Begins / Lasts / Repeats (eos: Lasts 0 = forever, Repeats 0 = once) shaped by the native ramp-in / ramp-out envelope.
//    Trigger sources start at their firing time.
ForceActivationReading ResolveForceActivation(const ForceSourceConfiguration& Source, double Time) noexcept
{
    if (!Source.Enabled) return {0.0, 0.0, ForceSourceStatus::Off};
    if (Source.Activation == ForceActivation::Always) return {1.0, Time, ForceSourceStatus::Active};
    double Start = Source.BeginsSeconds;
    if (Source.Activation == ForceActivation::Trigger)
    {
        if (Source.TriggeredAtSeconds < 0.0) return {0.0, 0.0, ForceSourceStatus::Armed};
        Start = Source.TriggeredAtSeconds;
    }
    double Local = Time - Start;
    if (Local < 0.0) return {0.0, 0.0, ForceSourceStatus::Waiting};
    const bool Repeating = Source.Activation == ForceActivation::Window && Source.RepeatsSeconds > 0.0;
    if (Repeating && Source.LastsSeconds > 0.0) Local = std::fmod(Local, Source.RepeatsSeconds);
    if (Source.LastsSeconds > 0.0 && Local > Source.LastsSeconds)
        return {0.0, Local, Repeating ? ForceSourceStatus::Waiting : ForceSourceStatus::Spent};
    double Envelope = Source.RampInSeconds > 1e-4 ? Smooth01(Local / Source.RampInSeconds) : 1.0;
    if (Source.LastsSeconds > 0.0 && Source.RampOutSeconds > 1e-4)
        Envelope *= 1.0 - Smooth01((Local - (Source.LastsSeconds - Source.RampOutSeconds)) / Source.RampOutSeconds);
    return {Envelope, Local, Envelope > 1e-3 ? ForceSourceStatus::Active : ForceSourceStatus::Waiting};
}

const char* ResolveForceSourceStatusLabel(ForceSourceStatus Status) noexcept
{
    switch (Status)
    {
    case ForceSourceStatus::Off: return "Off";
    case ForceSourceStatus::Active: return "Active";
    case ForceSourceStatus::Waiting: return "Waiting";
    case ForceSourceStatus::Armed: return "Armed";
    case ForceSourceStatus::Spent: return "Spent";
    }
    return "Off";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EVALUATION
//------------------------------------------------------------------------------------------------------------------------

ForceWindReading ResolveForceWindReading(const WindConfiguration& Wind) noexcept
{
    ForceWindReading Reading;
    Reading.Enabled                 = Wind.Enabled;
    Reading.BearingDegrees          = Wind.DirectionDegrees;
    Reading.SpeedMetresPerSecond    = Wind.SpeedMetresPerSecond;
    Reading.VeerDegreesPerKilometre = Wind.VeerDegreesPerKilometre;
    Reading.ShearPerKilometre       = Wind.ShearPerKilometre;
    Reading.GroundHeightMetres      = Wind.GroundHeightMetres;
    return Reading;
}

SpatialVector ConvertSceneToFieldAxes(const double SceneMetres[3]) noexcept
{
    return {SceneMetres[0], SceneMetres[2], -SceneMetres[1]};
}

std::array<double, 3> ConvertFieldToSceneAxes(SpatialVector FieldMetres) noexcept
{
    return {FieldMetres.X, -FieldMetres.Z, FieldMetres.Y};
}

// 📝 Where a moving source is now: tornado columns travel their track, gust puffs drift with the wind at their height,
//    gust fronts advance along their bearing.
SpatialVector ResolveForceSourceCentre(const ForceSourceConfiguration& Source, const ForceWindReading& Wind, double, double Age) noexcept
{
    const SpatialVector Centre = ConvertSceneToFieldAxes(Source.CentreMetres);
    if (Source.Category == C::TornadoColumn && Source.TrackSpeedMetresPerSecond > 0.0)
    {
        const double Turn = Source.TrackBearingDegrees * Radian, Travel = Source.TrackSpeedMetresPerSecond * Age;
        return {Centre.X + std::sin(Turn) * Travel, Centre.Y, Centre.Z - std::cos(Turn) * Travel};
    }
    if (Source.Category == C::GustPuff && Source.Drift > 0.0 && Wind.Enabled)
    {
        const double AltitudeKm = std::max(0.0, Centre.Y - Wind.GroundHeightMetres) / 1000.0;
        const double Turn       = (Wind.BearingDegrees + Wind.VeerDegreesPerKilometre * AltitudeKm) * Radian;
        const double Travel     = Wind.SpeedMetresPerSecond * std::max(0.15, 1.0 + Wind.ShearPerKilometre * std::min(AltitudeKm, 12.0)) *
                              Source.Drift * Age;
        return {Centre.X + std::sin(Turn) * Travel, Centre.Y, Centre.Z - std::cos(Turn) * Travel};
    }
    if (Source.Category == C::GustFront)
    {
        const double Turn = Source.BearingDegrees * Radian, Travel = Source.FrontSpeedMetresPerSecond * Age;
        return {Centre.X + std::sin(Turn) * Travel, Centre.Y, Centre.Z - std::cos(Turn) * Travel};
    }
    return Centre;
}

// 📝 Reach share at a point: Everywhere 1; Sphere by 3D distance; Column by horizontal distance between the base and the
//    top (+30 % taper); Box in the source's own frame turned to its bearing. Falloff shapes the edge (eos Falls).
double ResolveForceReachMask(const ForceSourceConfiguration& Source, SpatialVector Centre, SpatialVector Point, double Ground) noexcept
{
    if (Source.Reach == ForceReach::Everywhere) return 1.0;
    const double DeltaX = Point.X - Centre.X, DeltaZ = Point.Z - Centre.Z;
    // 📝 A microburst's centre is its core altitude; its column stands on the ground and reaches 1.4 × that altitude.
    const double ColumnFoot = Source.Category == C::Microburst ? Ground : Centre.Y;
    const double DeltaY     = Point.Y - ColumnFoot;
    if (Source.Reach == ForceReach::Sphere)
    {
        const double Separation = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
        return Separation >= Source.ReachMetres ? 0.0 : Falls(Source.Falloff, Separation, Source.ReachMetres);
    }
    if (Source.Reach == ForceReach::Column)
    {
        const double Height = std::max(1.0, Source.Category == C::Microburst ? std::max(Source.HeightMetres, (Centre.Y - ColumnFoot) * 1.4)
                                                                               : Source.HeightMetres);
        if (DeltaY < -15.0 || DeltaY > Height * 1.3) return 0.0;
        const double Separation = std::hypot(DeltaX, DeltaZ);
        if (Separation >= Source.ReachMetres) return 0.0;
        const double Taper = DeltaY <= Height ? 1.0 : 1.0 - Smooth01((DeltaY - Height) / (0.3 * Height));
        return Falls(Source.Falloff, Separation, Source.ReachMetres) * Taper;
    }
    const double     Turn   = Source.BearingDegrees * Radian;
    const double     Cosine = std::cos(Turn), Sine = std::sin(Turn);
    const BoxExtents Extents = ResolveBoxExtents(Source);
    const double     LocalX  = DeltaX * Cosine + DeltaZ * Sine;
    const double     LocalZ  = -DeltaX * Sine + DeltaZ * Cosine + Extents.OffsetZ;
    const double Share = std::max({std::abs(LocalX) / Extents.HalfX, std::abs(LocalZ) / Extents.HalfZ,
                                   DeltaY < 0.0 ? -DeltaY / 5.0 : DeltaY / std::max(1.0, Extents.Height)});
    return Share >= 1.0 ? 0.0 : Falls(Source.Falloff, Share, 1.0);
}

// 📝 Everything about a source that depends on time only — envelope, age, where it is now and a horizontal bound for
//    cheap rejection — is resolved once per refresh, not once per sample (a 150 × 95 lattice is 14 000 samples).
PreparedForceSource PrepareForceSource(const ForceSourceConfiguration& Source, const ForceWindReading& Wind, double Time) noexcept
{
    PreparedForceSource Prepared;
    Prepared.Source       = Source;
    Prepared.Contribution = ResolveForceSourceDescriptor(Source.Category).Contribution;
    Prepared.Activation   = ResolveForceActivation(Source, Time);
    Prepared.CentreMetres = ResolveForceSourceCentre(Source, Wind, Time, Prepared.Activation.AgeSeconds);
    double BoundX = Prepared.CentreMetres.X, BoundZ = Prepared.CentreMetres.Z, Bound = Infinity;
    if (Source.Category == C::GustFront)
    {
        const double Turn = Source.BearingDegrees * Radian;
        BoundX -= std::sin(Turn) * Source.CrestDepthMetres * 1.1;
        BoundZ += std::cos(Turn) * Source.CrestDepthMetres * 1.1;
        Bound = std::hypot(Source.HalfWidthMetres * 1.2, Source.CrestDepthMetres * 1.5);
    }
    else if (Source.Category == C::BlastWave)
        Bound = std::min(Source.ReachMetres, Source.InitialRadiusMetres + Source.ExpansionMetresPerSecond * Prepared.Activation.AgeSeconds +
                                                 3.2 * Source.ThicknessMetres);
    else if (Source.Category == C::GustPuff)
        Bound = std::min(Source.ReachMetres * 3.0,
                         Source.Reach == ForceReach::Sphere ? std::max(Source.ReachMetres, Source.ReachMetres * 3.0) : Infinity);
    else if (Source.Reach == ForceReach::Box)
    {
        const BoxExtents Extents = ResolveBoxExtents(Source);
        const double     Turn    = Source.BearingDegrees * Radian;
        BoundX += std::sin(Turn) * Extents.OffsetZ;
        BoundZ -= std::cos(Turn) * Extents.OffsetZ;
        Bound = std::hypot(Extents.HalfX, Extents.HalfZ);
    }
    else if (Source.Reach != ForceReach::Everywhere)
        Bound = Source.ReachMetres;
    Prepared.BoundX       = BoundX;
    Prepared.BoundZ       = BoundZ;
    Prepared.BoundSquared = Bound * Bound;
    return Prepared;
}

bool EvaluatePreparedForceSource(const PreparedForceSource& Prepared, const ForceWindReading& Wind, SpatialVector Point,
                                 SpatialVector& InOutValue) noexcept
{
    if (Prepared.Activation.Envelope <= 1e-5) return false;
    const double DeltaX = Point.X - Prepared.BoundX, DeltaZ = Point.Z - Prepared.BoundZ;
    if (DeltaX * DeltaX + DeltaZ * DeltaZ > Prepared.BoundSquared) return false;
    const ForceSourceConfiguration& Source = Prepared.Source;
    const double Envelope = Prepared.Activation.Envelope, Age = Prepared.Activation.AgeSeconds;
    double       Out[3]   = {InOutValue.X, InOutValue.Y, InOutValue.Z};
    if (Prepared.Contribution == ForceContribution::Flow)
    {
        const double Share = Source.Reach == ForceReach::Everywhere || Source.Category == C::GustFront
                                 ? 1.0
                                 : ResolveForceReachMask(Source, Prepared.CentreMetres, Point, Wind.GroundHeightMetres);
        if (Share <= 0.0) return false;
        if (Share >= 0.999)
            EvaluateFlowSource(Source, Wind, Point.X, Point.Y, Point.Z, Age, Envelope, Prepared.CentreMetres, Out);
        else
        {
            double Scratch[3] = {0.0, 0.0, 0.0};
            EvaluateFlowSource(Source, Wind, Point.X, Point.Y, Point.Z, Age, Envelope, Prepared.CentreMetres, Scratch);
            Out[0] += Scratch[0] * Share;
            Out[1] += Scratch[1] * Share;
            Out[2] += Scratch[2] * Share;
        }
        InOutValue = {Out[0], Out[1], Out[2]};
        return true;
    }
    const double Share = ResolveForceReachMask(Source, Prepared.CentreMetres, Point, Wind.GroundHeightMetres);
    if (Share <= 0.0) return false;
    if (Prepared.Contribution == ForceContribution::Accelerate)
        EvaluateAccelerateSource(Source, Point.X, Point.Y, Point.Z, Envelope, Share, Prepared.CentreMetres, Out);
    else
        Out[0] += Source.Strength * Share * Envelope;
    InOutValue = {Out[0], Out[1], Out[2]};
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROFILE AND BUDGET
//------------------------------------------------------------------------------------------------------------------------

// 📝 The type's radial profile for the inspector's hero plot: the reading against distance from the centre at the slice
//    height, with the source held still (no track, drift or front travel) and fully on.
ForceSourceProfile ResolveForceSourceProfile(const ForceSourceConfiguration& Source, const ForceWindReading& Wind, double SliceHeight,
                                             double) noexcept
{
    const ForceSourceDescriptor& Descriptor = ResolveForceSourceDescriptor(Source.Category);
    const BoxExtents             Extents    = ResolveBoxExtents(Source);
    const SpatialVector          Centre     = ConvertSceneToFieldAxes(Source.CentreMetres);
    const double Reach = Source.Category == C::GustFront   ? Source.CrestDepthMetres * 3.0
                         : Source.Category == C::BlastWave ? std::max(Source.ReachMetres * 0.4, 60.0)
                         : Source.Reach == ForceReach::Box ? std::max(Extents.HalfX, Extents.HalfZ) * 1.2
                                                           : Source.ReachMetres * 1.15;
    ForceSourceConfiguration Probe = Source;
    Probe.Enabled                   = true;
    Probe.Activation                = ForceActivation::Always;
    Probe.TrackSpeedMetresPerSecond = 0.0;
    Probe.Drift                     = 0.0;
    Probe.FrontSpeedMetresPerSecond = 0.0;

    ForceSourceProfile Profile;
    Profile.ReachMetres = Reach;
    Profile.Unit        = ResolveForceContributionDescriptor(Descriptor.Contribution).Unit;
    const double BlastTime =
        Source.Category == C::BlastWave ? std::max(0.0, (Reach * 0.5 - Source.InitialRadiusMetres) / std::max(1.0, Source.ExpansionMetresPerSecond))
                                        : 0.0;
    const PreparedForceSource Prepared = PrepareForceSource(Probe, Wind, BlastTime);
    for (uint32_t Step = 0; Step < kForceProfileSamples; ++Step)
    {
        const double Separation = static_cast<double>(Step) / static_cast<double>(kForceProfileSamples - 1) * Reach;
        double       X = Centre.X + Separation, Z = Centre.Z;
        if (Source.Category == C::GustFront)
        {
            X = Centre.X - std::sin(Source.BearingDegrees * Radian) * (Separation - Reach * 0.2);
            Z = Centre.Z + std::cos(Source.BearingDegrees * Radian) * (Separation - Reach * 0.2);
        }
        else if (Source.Category == C::JetCorridor || Source.Category == C::Current || Source.Category == C::VehicleWake ||
                 Source.Category == C::DragVolume)
        {
            const double Axis = Source.Category == C::VehicleWake   ? Source.BearingDegrees + 180.0
                                : Source.Category == C::JetCorridor ? Source.BearingDegrees
                                                                    : Source.BearingDegrees + 90.0;
            X = Centre.X + std::sin(Axis * Radian) * Separation;
            Z = Centre.Z - std::cos(Axis * Radian) * Separation;
        }
        const bool   AtCentre = Source.Category == C::BlastWave || Descriptor.Contribution != ForceContribution::Flow ||
                              Source.Category == C::GustPuff || Source.Category == C::JetCorridor;
        const double Height = AtCentre ? Centre.Y : SliceHeight;
        SpatialVector Value{};
        (void)EvaluatePreparedForceSource(Prepared, Wind, {X, Height, Z}, Value);
        const double Reading = Descriptor.Contribution == ForceContribution::Damp
                                   ? Value.X
                                   : std::sqrt(Value.X * Value.X + Value.Y * Value.Y + Value.Z * Value.Z);
        Profile.DistanceMetres[Step] = Separation;
        Profile.Magnitude[Step]      = Reading;
        Profile.PeakMagnitude        = std::max(Profile.PeakMagnitude, Reading);
    }
    return Profile;
}

// 📝 Which sources the lattice can hold and which each receiver must evaluate (eos Bakeable): selective flow fields (an
//    Acts-on list) cannot share one texture with the fields they exclude.
ForceFieldBudget ResolveForceFieldBudget(const ForceFieldConfiguration& Field, std::span<const ForceSourceConfiguration> Sources) noexcept
{
    ForceFieldBudget Budget;
    for (const ForceSourceConfiguration& Source : Sources)
    {
        const ForceContribution Contribution = ResolveForceSourceDescriptor(Source.Category).Contribution;
        if (Contribution == ForceContribution::Damp) ++Budget.Damping;
        else if (Contribution == ForceContribution::Flow && Source.ActsOn == 0) ++Budget.Baked;
        else ++Budget.Live;
    }
    Budget.CascadeBytes = static_cast<uint64_t>(Field.CellsPerSide) * Field.CellsPerSide * Field.VerticalCells * 8u;
    Budget.LevelCount   = std::min<uint32_t>(Field.Cascades, kMaximumForceCascades);
    Budget.TotalBytes   = Budget.CascadeBytes * Field.Cascades;
    for (uint32_t Level = 0; Level < Budget.LevelCount; ++Level)
    {
        const double Cell = Field.FinestCellMetres * std::pow(8.0, Level);
        Budget.Levels[Level] = {Cell, Cell * Field.CellsPerSide, static_cast<uint32_t>(std::pow(4.0, Level))};
    }
    return Budget;
}

uint32_t ResolveBeaufortForce(double Speed) noexcept
{
    constexpr double Limits[] = {0.5, 1.6, 3.4, 5.5, 8.0, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7};
    uint32_t         Force    = 0;
    while (Force < 12 && Speed >= Limits[Force]) ++Force;
    return Force;
}

const char* ResolveBeaufortName(uint32_t Force) noexcept
{
    constexpr const char* Names[] = {"Calm",       "Light air",   "Light breeze", "Gentle breeze", "Moderate breeze",
                                     "Fresh breeze", "Strong breeze", "Near gale",  "Gale",          "Strong gale",
                                     "Storm",      "Violent storm", "Hurricane force"};
    return Names[std::min<uint32_t>(Force, 12)];
}

// 📝 2D gradient (Perlin) noise with its analytic gradient and a quintic fade: smooth, free of the value-noise grid
//    blockiness, so the curl of it reads as round eddies. Returns about [−1, 1] and writes ∂/∂u, ∂/∂v into Gradient.
double EvaluateGradientNoise(double U, double V, uint32_t Seed, double Gradient[2]) noexcept
{
    const double  FloorU = std::floor(U), FloorV = std::floor(V);
    const int64_t ColumnIndex = static_cast<int64_t>(FloorU), RowIndex = static_cast<int64_t>(FloorV);
    const double  X = U - FloorU, Y = V - FloorV;
    const double  FadeX = X * X * X * (X * (X * 6.0 - 15.0) + 10.0), FadeY = Y * Y * Y * (Y * (Y * 6.0 - 15.0) + 10.0);
    const double  SlopeX = 30.0 * X * X * (X * (X - 2.0) + 1.0), SlopeY = 30.0 * Y * Y * (Y * (Y - 2.0) + 1.0);
    const uint32_t A = (GradientHash(ColumnIndex, RowIndex, Seed) & 15u) * 2u;
    const uint32_t B = (GradientHash(ColumnIndex + 1, RowIndex, Seed) & 15u) * 2u;
    const uint32_t Cc = (GradientHash(ColumnIndex, RowIndex + 1, Seed) & 15u) * 2u;
    const uint32_t D = (GradientHash(ColumnIndex + 1, RowIndex + 1, Seed) & 15u) * 2u;
    const double* Directions = GradientDirections.Directions;
    const double  AX = Directions[A], AY = Directions[A + 1], BX = Directions[B], BY = Directions[B + 1];
    const double  CX = Directions[Cc], CY = Directions[Cc + 1], DX = Directions[D], DY = Directions[D + 1];
    const double  N00 = AX * X + AY * Y, N10 = BX * (X - 1.0) + BY * Y, N01 = CX * X + CY * (Y - 1.0), N11 = DX * (X - 1.0) + DY * (Y - 1.0);
    const double  Cross   = N00 - N10 - N01 + N11;
    const double  Reading = N00 + FadeX * (N10 - N00) + FadeY * (N01 - N00) + FadeX * FadeY * Cross;
    Gradient[0] = (AX + FadeX * (BX - AX) + FadeY * (CX - AX) + FadeX * FadeY * (AX - BX - CX + DX) + SlopeX * (N10 - N00 + FadeY * Cross)) * 1.41;
    Gradient[1] = (AY + FadeX * (BY - AY) + FadeY * (CY - AY) + FadeX * FadeY * (AY - BY - CY + DY) + SlopeY * (N01 - N00 + FadeX * Cross)) * 1.41;
    return Reading * 1.41;
}

} // namespace Frontier
