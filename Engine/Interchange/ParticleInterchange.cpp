/*============================================================================================================================================
                                                        PARTICLEINTERCHANGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of HTML Particle Studio JSON interchange, Y-Up <-> +Z Up coordinate bridging, handoff packets & 3D surface import

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Interchange/ParticleInterchange.h"

#include "Engine/Interchange/ColourInterchange.h"
#include "Engine/Interchange/GeometryInterchange.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <vector>

namespace Frontier::Interchange
{

namespace
{

using namespace Frontier::ParticleSpace;

[[nodiscard]] Mathematics::Axis3D HtmlYUpToFrontierZUp(Mathematics::Axis3D P) noexcept
{
    return Mathematics::ConvertVectorBetweenBases(
        P,
        Mathematics::WorldCoordinateBasis::RightHandedYUp,
        Mathematics::WorldCoordinateBasis::RightHandedZUp);
}

[[nodiscard]] Mathematics::Axis3D FrontierZUpToHtmlYUp(Mathematics::Axis3D P) noexcept
{
    return Mathematics::ConvertVectorBetweenBases(
        P,
        Mathematics::WorldCoordinateBasis::RightHandedZUp,
        Mathematics::WorldCoordinateBasis::RightHandedYUp);
}

// BoxHalf / Euler magnitude conversion: positive half-extents swap Y and Z without negating
[[nodiscard]] Mathematics::Axis3D SwapExtentsYUpToZUp(Mathematics::Axis3D E) noexcept
{
    return { E.X, E.Z, E.Y };
}

[[nodiscard]] Mathematics::Axis3D SwapExtentsZUpToYUp(Mathematics::Axis3D E) noexcept
{
    return { E.X, E.Z, E.Y };
}

[[nodiscard]] bool FindJsonKeyPos(std::string_view Json, std::string_view Key, std::size_t& OutValPos) noexcept
{
    const std::string Needle = "\"" + std::string(Key) + "\"";
    std::size_t SearchPos    = 0u;
    while (SearchPos < Json.size())
    {
        const std::size_t Found = Json.find(Needle, SearchPos);
        if (Found == std::string_view::npos)
        {
            return false;
        }
        std::size_t Pos = Found + Needle.size();
        while (Pos < Json.size() && std::isspace(static_cast<unsigned char>(Json[Pos]))) ++Pos;
        if (Pos < Json.size() && Json[Pos] == ':')
        {
            ++Pos;
            while (Pos < Json.size() && std::isspace(static_cast<unsigned char>(Json[Pos]))) ++Pos;
            OutValPos = Pos;
            return true;
        }
        SearchPos = Found + Needle.size();
    }
    return false;
}

[[nodiscard]] bool ParseJsonNumber(std::string_view Json, std::string_view Key, float& OutNum) noexcept
{
    std::size_t Pos = 0u;
    if (!FindJsonKeyPos(Json, Key, Pos) || Pos >= Json.size())
    {
        return false;
    }
    const char* StartPtr = Json.data() + Pos;
    char*       EndPtr   = nullptr;
    const float Parsed   = std::strtof(StartPtr, &EndPtr);
    if (EndPtr == StartPtr)
    {
        return false;
    }
    OutNum = Parsed;
    return true;
}

[[nodiscard]] bool ParseJsonUint(std::string_view Json, std::string_view Key, uint32_t& OutVal) noexcept
{
    float Num = 0.0f;
    if (!ParseJsonNumber(Json, Key, Num))
    {
        return false;
    }
    OutVal = static_cast<uint32_t>(std::max(0.0f, Num));
    return true;
}

[[nodiscard]] bool ParseJsonBool(std::string_view Json, std::string_view Key, bool& OutVal) noexcept
{
    std::size_t Pos = 0u;
    if (!FindJsonKeyPos(Json, Key, Pos) || Pos >= Json.size())
    {
        return false;
    }
    if (Json.substr(Pos, 4u) == "true")
    {
        OutVal = true;
        return true;
    }
    if (Json.substr(Pos, 5u) == "false")
    {
        OutVal = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseJsonString(std::string_view Json, std::string_view Key, std::string& OutStr)
{
    std::size_t Pos = 0u;
    if (!FindJsonKeyPos(Json, Key, Pos) || Pos >= Json.size() || Json[Pos] != '"')
    {
        return false;
    }
    ++Pos;
    std::string Built;
    while (Pos < Json.size() && Json[Pos] != '"')
    {
        if (Json[Pos] == '\\' && Pos + 1u < Json.size())
        {
            ++Pos;
        }
        Built.push_back(Json[Pos++]);
    }
    OutStr = std::move(Built);
    return true;
}

[[nodiscard]] bool ParseJsonVec3(std::string_view Json, std::string_view Key, Mathematics::Axis3D& OutVec) noexcept
{
    std::size_t Pos = 0u;
    if (!FindJsonKeyPos(Json, Key, Pos) || Pos >= Json.size() || Json[Pos] != '[')
    {
        return false;
    }
    ++Pos;
    float Vals[3] { 0.0f, 0.0f, 0.0f };
    int   Idx = 0;
    while (Pos < Json.size() && Idx < 3)
    {
        while (Pos < Json.size() && (std::isspace(static_cast<unsigned char>(Json[Pos])) || Json[Pos] == ',')) ++Pos;
        if (Pos >= Json.size() || Json[Pos] == ']') break;
        const char* StartPtr = Json.data() + Pos;
        char*       EndPtr   = nullptr;
        const float Num      = std::strtof(StartPtr, &EndPtr);
        if (EndPtr == StartPtr) break;
        Vals[Idx++] = Num;
        Pos         = static_cast<std::size_t>(EndPtr - Json.data());
    }
    if (Idx < 3)
    {
        return false;
    }
    OutVec = { Vals[0], Vals[1], Vals[2] };
    return true;
}

[[nodiscard]] bool ExtractJsonSubObject(std::string_view Json, std::string_view Key, std::string_view& OutSlice) noexcept
{
    std::size_t Pos = 0u;
    if (!FindJsonKeyPos(Json, Key, Pos) || Pos >= Json.size() || Json[Pos] != '{')
    {
        return false;
    }
    const std::size_t Start = Pos;
    int               Depth = 0;
    bool              InStr = false;
    for (std::size_t I = Start; I < Json.size(); ++I)
    {
        const char Ch = Json[I];
        if (Ch == '"' && (I == 0u || Json[I - 1u] != '\\'))
        {
            InStr = !InStr;
            continue;
        }
        if (InStr)
        {
            continue;
        }
        if (Ch == '{') ++Depth;
        else if (Ch == '}')
        {
            --Depth;
            if (Depth == 0)
            {
                OutSlice = Json.substr(Start, I - Start + 1u);
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::string EscapeJson(std::string_view Text)
{
    std::string Out;
    Out.reserve(Text.size());
    for (char Ch : Text)
    {
        if (Ch == '"' || Ch == '\\')
        {
            Out.push_back('\\');
        }
        Out.push_back(Ch);
    }
    return Out;
}

[[nodiscard]] ParticleScalarChannel ParseHtmlColorByToken(std::string_view Token) noexcept
{
    if (Token == "modeDefault")  return ParticleScalarChannel::ModeDefault;
    if (Token == "lifetime")     return ParticleScalarChannel::NormalizedLifetime;
    if (Token == "age")          return ParticleScalarChannel::ParticleAgeSeconds;
    if (Token == "speed")        return ParticleScalarChannel::VelocitySpeed;
    if (Token == "velY")         return ParticleScalarChannel::VerticalVelocity;
    if (Token == "angle")        return ParticleScalarChannel::OriginAzimuthAngle;
    if (Token == "velAngle")     return ParticleScalarChannel::VelocityHeadingAngle;
    if (Token == "distOrigin")   return ParticleScalarChannel::DistanceFromOrigin;
    if (Token == "height")       return ParticleScalarChannel::WorldElevation;
    if (Token == "noise3D")      return ParticleScalarChannel::SpatialNoise3D;
    if (Token == "random")       return ParticleScalarChannel::PerParticleRandomSeed;
    if (Token == "spinAngle")    return ParticleScalarChannel::SpinOrientationAngle;
    if (Token == "distCamera")   return ParticleScalarChannel::DistanceToCamera;
    if (Token == "particleSize") return ParticleScalarChannel::ParticleSizeMeters;
    if (Token == "lifeDuration") return ParticleScalarChannel::AssignedLifespanSeconds;
    return ParticleScalarChannel::NormalizedLifetime;
}

[[nodiscard]] std::string_view FormatHtmlColorByToken(ParticleScalarChannel Channel) noexcept
{
    switch (Channel)
    {
    case ParticleScalarChannel::ModeDefault:             return "modeDefault";
    case ParticleScalarChannel::NormalizedLifetime:      return "lifetime";
    case ParticleScalarChannel::ParticleAgeSeconds:      return "age";
    case ParticleScalarChannel::VelocitySpeed:           return "speed";
    case ParticleScalarChannel::VerticalVelocity:        return "velY";
    case ParticleScalarChannel::OriginAzimuthAngle:      return "angle";
    case ParticleScalarChannel::VelocityHeadingAngle:    return "velAngle";
    case ParticleScalarChannel::DistanceFromOrigin:      return "distOrigin";
    case ParticleScalarChannel::WorldElevation:          return "height";
    case ParticleScalarChannel::SpatialNoise3D:          return "noise3D";
    case ParticleScalarChannel::PerParticleRandomSeed:   return "random";
    case ParticleScalarChannel::SpinOrientationAngle:    return "spinAngle";
    case ParticleScalarChannel::DistanceToCamera:        return "distCamera";
    case ParticleScalarChannel::ParticleSizeMeters:      return "particleSize";
    case ParticleScalarChannel::AssignedLifespanSeconds: return "lifeDuration";
    default:                                             return "lifetime";
    }
}

void WriteVec3Json(std::ostringstream& Oss, std::string_view Key, Mathematics::Axis3D V)
{
    Oss << "\"" << Key << "\":[" << V.X << "," << V.Y << "," << V.Z << "]";
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              HTML STUDIO JSON INTERCHANGE
//------------------------------------------------------------------------------------------------------------------------

bool ParticleInterchange::ImportHtmlStudioPresetJson(
    std::string_view PresetJson,
    std::string_view PresetId,
    std::string_view RigTitle,
    bool             Quarantined,
    ParticleRig&     OutRig,
    std::string&     OutDiagnostic)
{
    OutRig                = ParticleRig{};
    OutRig.PresetOriginId = std::string(PresetId);
    OutRig.RigTitle       = RigTitle.empty() ? std::string(PresetId) : std::string(RigTitle);
    OutRig.Quarantined    = Quarantined;

    uint32_t KindId     = 0u;
    uint32_t ShapeId    = 0u;
    uint32_t LeafMode   = 0u;
    uint32_t EmitShape  = 0u;
    uint32_t GridMode   = 0u;
    uint32_t GridSurface = 0u;
    uint32_t ObjectShape = 0u;
    bool     HasObjShape = false;

    (void)ParseJsonUint(PresetJson, "kind", KindId);
    (void)ParseJsonUint(PresetJson, "shape", ShapeId);
    (void)ParseJsonUint(PresetJson, "leafMode", LeafMode);
    (void)ParseJsonUint(PresetJson, "emitShape", EmitShape);
    (void)ParseJsonUint(PresetJson, "gridMode", GridMode);
    (void)ParseJsonUint(PresetJson, "gridSurface", GridSurface);
    HasObjShape = ParseJsonUint(PresetJson, "objectShape", ObjectShape);
    (void)ParseJsonBool(PresetJson, "visible", OutRig.Enabled);

    // Sizing
    (void)ParseJsonUint(PresetJson, "capacity", OutRig.Sizing.CapacitySlots);
    (void)ParseJsonNumber(PresetJson, "rate", OutRig.Sizing.ReleaseRatePerSec);
    (void)ParseJsonNumber(PresetJson, "lifeMin", OutRig.Sizing.LifetimeMinSec);
    (void)ParseJsonNumber(PresetJson, "lifeMax", OutRig.Sizing.LifetimeMaxSec);
    (void)ParseJsonNumber(PresetJson, "sizeStart", OutRig.Sizing.SizeStartMeters);
    (void)ParseJsonNumber(PresetJson, "sizeEnd", OutRig.Sizing.SizeEndMeters);
    (void)ParseJsonNumber(PresetJson, "sizeScale", OutRig.Sizing.OpacityPeak);
    (void)ParseJsonNumber(PresetJson, "curlAmp", OutRig.Sizing.TurbulenceAmp);
    (void)ParseJsonNumber(PresetJson, "curlFreq", OutRig.Sizing.TurbulenceFreq);

    // Ballistics
    Mathematics::Axis3D OriginYUp { 0.0f, 0.5f, 0.0f };
    Mathematics::Axis3D DirYUp    { 0.0f, 1.0f, 0.0f };
    Mathematics::Axis3D BoxHalfYUp{ 0.0f, 0.0f, 0.0f };
    float Radius = 0.1f;
    (void)ParseJsonVec3(PresetJson, "origin", OriginYUp);
    (void)ParseJsonVec3(PresetJson, "dir", DirYUp);
    (void)ParseJsonVec3(PresetJson, "boxHalf", BoxHalfYUp);
    (void)ParseJsonNumber(PresetJson, "radius", Radius);

    const Mathematics::Axis3D OriginZUp  = HtmlYUpToFrontierZUp(OriginYUp);
    const Mathematics::Axis3D DirZUp     = HtmlYUpToFrontierZUp(DirYUp);
    const Mathematics::Axis3D BoxHalfZUp = SwapExtentsYUpToZUp(BoxHalfYUp);

    OutRig.Dynamics.Family = static_cast<ParticleSimulationFamily>(KindId);
    OutRig.Dynamics.Ballistics.DirectionAxis = DirZUp;
    (void)ParseJsonNumber(PresetJson, "spread", OutRig.Dynamics.Ballistics.SpreadRadians);
    (void)ParseJsonNumber(PresetJson, "speedMin", OutRig.Dynamics.Ballistics.SpeedMin);
    (void)ParseJsonNumber(PresetJson, "speedMax", OutRig.Dynamics.Ballistics.SpeedMax);
    (void)ParseJsonNumber(PresetJson, "drag", OutRig.Dynamics.Ballistics.DragPerSecond);
    (void)ParseJsonNumber(PresetJson, "gravity", OutRig.Dynamics.Ballistics.GravityFactor);
    (void)ParseJsonNumber(PresetJson, "buoyancy", OutRig.Dynamics.Ballistics.BuoyancyAccel);
    (void)ParseJsonNumber(PresetJson, "bounce", OutRig.Dynamics.Ballistics.BounceRestitution);
    (void)ParseJsonNumber(PresetJson, "flutter", OutRig.Dynamics.Ballistics.FlutterAmp);
    (void)ParseJsonNumber(PresetJson, "burstEvery", OutRig.Dynamics.Ballistics.BurstIntervalSec);
    (void)ParseJsonUint(PresetJson, "burstCount", OutRig.Dynamics.Ballistics.BurstCount);
    (void)ParseJsonBool(PresetJson, "burstAtOrigin", OutRig.Dynamics.Ballistics.BurstAtOrigin);

    // Force coupling & magnetic dipole
    (void)ParseJsonNumber(PresetJson, "windCoupling", OutRig.Dynamics.ForceLink.WindCouplingRate);
    (void)ParseJsonBool(PresetJson, "magneticField", OutRig.Dynamics.ForceLink.LocalMagneticActive);
    (void)ParseJsonNumber(PresetJson, "magStrength", OutRig.Dynamics.ForceLink.LocalMagneticStrength);
    (void)ParseJsonNumber(PresetJson, "magGuide", OutRig.Dynamics.ForceLink.LocalMagneticGuide);
    (void)ParseJsonNumber(PresetJson, "magRadius", OutRig.Dynamics.ForceLink.LocalMagneticRadius);

    // Curl manifold
    uint32_t CurlModeInt = 0u;
    (void)ParseJsonUint(PresetJson, "curlMode", CurlModeInt);
    OutRig.Dynamics.Curl.Mode = static_cast<CurlManifoldMode>(std::min(4u, CurlModeInt));
    (void)ParseJsonNumber(PresetJson, "curlFreq", OutRig.Dynamics.Curl.SpatialFreq);
    (void)ParseJsonNumber(PresetJson, "curlAmp", OutRig.Dynamics.Curl.DriveSpeed);
    (void)ParseJsonNumber(PresetJson, "curlRate", OutRig.Dynamics.Curl.EvolutionRate);
    (void)ParseJsonNumber(PresetJson, "shellRadius", OutRig.Dynamics.Curl.ShellRadius);

    // Strands / cloth / foliage
    uint32_t DynModeInt = 0u;
    (void)ParseJsonUint(PresetJson, "dynMode", DynModeInt);
    OutRig.Dynamics.Strands.Mode = static_cast<StrandDynamicsMode>(std::min(9u, DynModeInt));
    (void)ParseJsonUint(PresetJson, "dynSub", OutRig.Dynamics.Strands.SubVariant);
    (void)ParseJsonUint(PresetJson, "dynRows", OutRig.Dynamics.Strands.RowCount);
    (void)ParseJsonUint(PresetJson, "dynSegs", OutRig.Dynamics.Strands.SegmentCount);
    (void)ParseJsonNumber(PresetJson, "dynStiff", OutRig.Dynamics.Strands.StiffnessRate);
    (void)ParseJsonNumber(PresetJson, "dynTearThresh", OutRig.Dynamics.Strands.TearThresholdSpeed);
    (void)ParseJsonNumber(PresetJson, "dynHeight", OutRig.Dynamics.Strands.HeightMeters);
    (void)ParseJsonNumber(PresetJson, "dynSag", OutRig.Dynamics.Strands.SagMeters);
    (void)ParseJsonNumber(PresetJson, "dynWaveSpeed", OutRig.Dynamics.Strands.WaveSpeedHz);
    (void)ParseJsonNumber(PresetJson, "dynAnemThick", OutRig.Dynamics.Strands.AnemoneThickness);
    (void)ParseJsonUint(PresetJson, "beadsMode", OutRig.Dynamics.Strands.BeadsDrapeMode);
    (void)ParseJsonUint(PresetJson, "jellyMode", OutRig.Dynamics.Strands.JellyTendrilMode);
    (void)ParseJsonBool(PresetJson, "dynCurveDebug", OutRig.Dynamics.Strands.CurveDebugEnabled);

    // Botanical, Spiderweb, Ferro, Fracture, Splash
    (void)ParseJsonUint(PresetJson, "seedHeadMode", OutRig.Dynamics.Botanical.SeedHeadMode);
    (void)ParseJsonUint(PresetJson, "webMode", OutRig.Dynamics.Spiderweb.WebMode);
    (void)ParseJsonUint(PresetJson, "webSpokes", OutRig.Dynamics.Spiderweb.SpokeCount);
    (void)ParseJsonUint(PresetJson, "webRings", OutRig.Dynamics.Spiderweb.RingCount);
    (void)ParseJsonNumber(PresetJson, "webSpinRate", OutRig.Dynamics.Spiderweb.SpinRate);
    (void)ParseJsonNumber(PresetJson, "webSag", OutRig.Dynamics.Spiderweb.SagMeters);
    (void)ParseJsonNumber(PresetJson, "webDew", OutRig.Dynamics.Spiderweb.DewHighlight);

    (void)ParseJsonUint(PresetJson, "ferroMode", OutRig.Dynamics.Ferrofluid.FerroMode);
    (void)ParseJsonUint(PresetJson, "ferroSpikes", OutRig.Dynamics.Ferrofluid.SpikeCount);
    (void)ParseJsonNumber(PresetJson, "ferroMagnetSpeed", OutRig.Dynamics.Ferrofluid.MagnetOrbitSpeed);
    (void)ParseJsonBool(PresetJson, "ferroMagnetFree", OutRig.Dynamics.Ferrofluid.FreeDraggableMagnet);

    (void)ParseJsonUint(PresetJson, "crackMode", OutRig.Dynamics.Fracture.CrackMode);
    (void)ParseJsonNumber(PresetJson, "crackSpeed", OutRig.Dynamics.Fracture.PropagationSpeed);
    (void)ParseJsonNumber(PresetJson, "crackDepth", OutRig.Dynamics.Fracture.FissureHeaveMeters);

    (void)ParseJsonUint(PresetJson, "mudSplashMode", OutRig.Dynamics.WaterSplash.SplashMode);
    (void)ParseJsonNumber(PresetJson, "carSpeed", OutRig.Dynamics.WaterSplash.VehicleSpeed);
    (void)ParseJsonNumber(PresetJson, "carTrackRadius", OutRig.Dynamics.WaterSplash.TrackRadiusMeters);

    // Effector grid
    OutRig.Dynamics.EffectorGrid.Active = (GridMode > 0u);
    uint32_t GridShape = 2u, GridEff = 0u, EffShapeA = 0u, EffShapeB = 0u;
    (void)ParseJsonUint(PresetJson, "gridShape", GridShape);
    (void)ParseJsonUint(PresetJson, "gridEffector", GridEff);
    (void)ParseJsonUint(PresetJson, "gridCols", OutRig.Dynamics.EffectorGrid.Columns);
    (void)ParseJsonUint(PresetJson, "gridRows", OutRig.Dynamics.EffectorGrid.Rows);
    (void)ParseJsonNumber(PresetJson, "gridMinScale", OutRig.Dynamics.EffectorGrid.MinScale);
    (void)ParseJsonNumber(PresetJson, "gridMaxHeight", OutRig.Dynamics.EffectorGrid.MaxHeightMeters);
    (void)ParseJsonNumber(PresetJson, "gridContrast", OutRig.Dynamics.EffectorGrid.FalloffContrast);
    uint32_t BillboardInt = 0u;
    (void)ParseJsonUint(PresetJson, "gridBillboard", BillboardInt);
    OutRig.Dynamics.EffectorGrid.CameraBillboarded = (BillboardInt > 0u);
    (void)ParseJsonBool(PresetJson, "effBlend", OutRig.Dynamics.EffectorGrid.BlendPlaceable);
    OutRig.Dynamics.EffectorGrid.GridShape = static_cast<MoGraphGridShape>(std::min(6u, GridShape));
    OutRig.Dynamics.EffectorGrid.Driver    = static_cast<MoGraphEffectorDriver>(std::min(5u, GridEff));

    Mathematics::Axis3D EffAYUp{ 0.0f, 1.5f, 0.0f };
    Mathematics::Axis3D EffBYUp{ 2.5f, 1.5f, 0.0f };
    (void)ParseJsonVec3(PresetJson, "effPos", EffAYUp);
    (void)ParseJsonVec3(PresetJson, "effPos2", EffBYUp);
    (void)ParseJsonUint(PresetJson, "effShape", EffShapeA);
    (void)ParseJsonUint(PresetJson, "effShape2", EffShapeB);
    (void)ParseJsonNumber(PresetJson, "effSize", OutRig.Dynamics.EffectorGrid.PrimaryEffector.SizeMeters);
    (void)ParseJsonNumber(PresetJson, "effSize2", OutRig.Dynamics.EffectorGrid.SecondaryEffector.SizeMeters);
    OutRig.Dynamics.EffectorGrid.PrimaryEffector.CentreMeters   = HtmlYUpToFrontierZUp(EffAYUp);
    OutRig.Dynamics.EffectorGrid.PrimaryEffector.Shape          = static_cast<EffectorVolumeShape>(std::min(4u, EffShapeA));
    OutRig.Dynamics.EffectorGrid.SecondaryEffector.CentreMeters = HtmlYUpToFrontierZUp(EffBYUp);
    OutRig.Dynamics.EffectorGrid.SecondaryEffector.Shape        = static_cast<EffectorVolumeShape>(std::min(4u, EffShapeB));

    // Boids & Molecular
    OutRig.Dynamics.Boids.Active = (KindId == 6u);
    (void)ParseJsonNumber(PresetJson, "swarmRadius", OutRig.Dynamics.Boids.NeighbourRadius);
    (void)ParseJsonNumber(PresetJson, "boidsCohere", OutRig.Dynamics.Boids.CohesionWeight);
    (void)ParseJsonNumber(PresetJson, "boidsAlign", OutRig.Dynamics.Boids.AlignmentWeight);
    (void)ParseJsonNumber(PresetJson, "boidsSeparate", OutRig.Dynamics.Boids.SeparationWeight);
    (void)ParseJsonNumber(PresetJson, "boidsSpeed", OutRig.Dynamics.Boids.TargetSpeed);
    (void)ParseJsonNumber(PresetJson, "pulseHz", OutRig.Dynamics.Boids.PulseHz);
    (void)ParseJsonNumber(PresetJson, "pulseDepth", OutRig.Dynamics.Boids.PulseDepth);

    OutRig.Dynamics.Molecular.Active = (KindId == 3u || KindId == 4u);
    (void)ParseJsonNumber(PresetJson, "temperature", OutRig.Dynamics.Molecular.Temperature);
    (void)ParseJsonNumber(PresetJson, "epsilon", OutRig.Dynamics.Molecular.Epsilon);
    (void)ParseJsonNumber(PresetJson, "sigma", OutRig.Dynamics.Molecular.Sigma);
    (void)ParseJsonNumber(PresetJson, "damping", OutRig.Dynamics.Molecular.LangevinDamping);
    (void)ParseJsonNumber(PresetJson, "reactRate", OutRig.Dynamics.Molecular.ReactionRate);
    (void)ParseJsonNumber(PresetJson, "dissociation", OutRig.Dynamics.Molecular.DissociationRate);
    (void)ParseJsonNumber(PresetJson, "reactRadius", OutRig.Dynamics.Molecular.ReactionRadius);
    (void)ParseJsonNumber(PresetJson, "fracA", OutRig.Dynamics.Molecular.InitialFractionA);
    (void)ParseJsonNumber(PresetJson, "simDt", OutRig.Dynamics.Molecular.FixedSubstepSec);

    // Shield & Pyro
    OutRig.Dynamics.Shield.Active = (ShapeId == 16u);
    uint32_t ShieldFormInt = 0u;
    (void)ParseJsonUint(PresetJson, "shieldForm", ShieldFormInt);
    OutRig.Dynamics.Shield.Form = static_cast<ShieldFormCategory>(std::min(5u, ShieldFormInt));
    (void)ParseJsonUint(PresetJson, "shieldMask", OutRig.Dynamics.Shield.MotifBitmask);
    (void)ParseJsonNumber(PresetJson, "shieldSpin", OutRig.Dynamics.Shield.SpinRate);
    (void)ParseJsonNumber(PresetJson, "glitchRate", OutRig.Dynamics.Shield.GlitchRateHz);
    OutRig.Dynamics.Transition.GlitchRateHz = OutRig.Dynamics.Shield.GlitchRateHz;

    (void)ParseJsonUint(PresetJson, "fireworkMode", OutRig.Dynamics.Pyro.FireworkMode);
    (void)ParseJsonUint(PresetJson, "flareMode", OutRig.Dynamics.Pyro.FlareSalvoMode);
    float TrailModeF = 0.0f;
    (void)ParseJsonNumber(PresetJson, "trailMode", TrailModeF);
    OutRig.Dynamics.Pyro.TrailLengthSec = TrailModeF;
    (void)ParseJsonNumber(PresetJson, "beamMode", OutRig.Dynamics.Pyro.BeamModeActive);

    // Nested objects: transition, attractor, blackHole, heatShimmer
    std::string_view SubSlice;
    if (ExtractJsonSubObject(PresetJson, "transition", SubSlice))
    {
        OutRig.Dynamics.Transition.Mode = TransitionDerezMode::DerezCube;
        (void)ParseJsonNumber(SubSlice, "holdBase", OutRig.Dynamics.Transition.HoldBaseSec);
        (void)ParseJsonNumber(SubSlice, "holdSpread", OutRig.Dynamics.Transition.HoldDurationSec);
        (void)ParseJsonNumber(SubSlice, "burstShare", OutRig.Dynamics.Transition.BurstShare);
        (void)ParseJsonBool(SubSlice, "fragment", OutRig.Dynamics.Transition.FragmentOnFloor);
        (void)ParseJsonNumber(SubSlice, "childLife", OutRig.Dynamics.Transition.ChildLifeSec);
        (void)ParseJsonNumber(SubSlice, "shell", OutRig.Dynamics.Transition.ShellRadiusMeters);
        uint32_t Assemble = 0u;
        if (ParseJsonUint(SubSlice, "assemble", Assemble))
        {
            OutRig.Dynamics.Transition.AssembleMode = Assemble;
            if (Assemble == 1u) OutRig.Dynamics.Transition.Mode = TransitionDerezMode::ReverseDerez;
            if (Assemble == 2u) OutRig.Dynamics.Transition.Mode = TransitionDerezMode::CoinStack;
        }
        std::string WaveStr, AfterStr;
        if (ParseJsonString(SubSlice, "wave", WaveStr))
        {
            if (WaveStr == "melt") OutRig.Dynamics.Transition.WavePattern = 1u;
            else if (WaveStr == "glitch")
            {
                OutRig.Dynamics.Transition.WavePattern = 2u;
                OutRig.Dynamics.Transition.Mode = TransitionDerezMode::GlitchDissolve;
            }
        }
        if (ParseJsonString(SubSlice, "after", AfterStr))
        {
            if (AfterStr == "none") OutRig.Dynamics.Transition.AfterTransform = 1u;
            else if (AfterStr == "butterfly")
            {
                OutRig.Dynamics.Transition.AfterTransform = 2u;
                OutRig.Dynamics.Transition.Mode = TransitionDerezMode::DissolveButterfly;
            }
        }
    }

    if (ExtractJsonSubObject(PresetJson, "attractor", SubSlice))
    {
        (void)ParseJsonBool(SubSlice, "enabled", OutRig.Dynamics.LocalAttractor.Enabled);
        (void)ParseJsonNumber(SubSlice, "strength", OutRig.Dynamics.LocalAttractor.Strength);
        (void)ParseJsonNumber(SubSlice, "radius", OutRig.Dynamics.LocalAttractor.RadiusMeters);
        (void)ParseJsonNumber(SubSlice, "swirl", OutRig.Dynamics.LocalAttractor.SwirlSpeed);
        (void)ParseJsonNumber(SubSlice, "swallow", OutRig.Dynamics.LocalAttractor.SwallowRadius);
    }

    if (ExtractJsonSubObject(PresetJson, "blackHole", SubSlice))
    {
        OutRig.Dynamics.BlackHole.Enabled = true;
        (void)ParseJsonNumber(SubSlice, "horizon", OutRig.Dynamics.BlackHole.HorizonRadius);
        (void)ParseJsonNumber(SubSlice, "einstein", OutRig.Dynamics.BlackHole.EinsteinGain);
    }

    if (ExtractJsonSubObject(PresetJson, "heatShimmer", SubSlice))
    {
        OutRig.Dynamics.HeatShimmer.Enabled = true;
        (void)ParseJsonNumber(SubSlice, "radius", OutRig.Dynamics.HeatShimmer.RadiusMeters);
        (void)ParseJsonNumber(SubSlice, "strength", OutRig.Dynamics.HeatShimmer.Strength);
        (void)ParseJsonNumber(SubSlice, "freq", OutRig.Dynamics.HeatShimmer.SpatialFreq);
        (void)ParseJsonNumber(SubSlice, "rate", OutRig.Dynamics.HeatShimmer.DriftRate);
    }

    // BackingSurface
    uint32_t ObjStyle = 0u, ObjSampleMode = 0u;
    Mathematics::Axis3D ObjRot{ 0.0f, 0.0f, 0.0f };
    (void)ParseJsonUint(PresetJson, "objStyle", ObjStyle);
    (void)ParseJsonUint(PresetJson, "objSampleMode", ObjSampleMode);
    (void)ParseJsonVec3(PresetJson, "objRot", ObjRot);
    const uint32_t ActiveSurfaceIdx = HasObjShape ? ObjectShape : GridSurface;
    OutRig.BackingSurface.FinishStyle   = static_cast<BackingFinishCategory>(std::min(3u, ObjStyle));
    OutRig.BackingSurface.SurfacePreset = static_cast<SurfacePresetCategory>(std::min(16u, ActiveSurfaceIdx));
    OutRig.BackingSurface.SampleMode    = static_cast<SurfaceSampleMode>(std::min(2u, ObjSampleMode));
    OutRig.BackingSurface.EulerDegrees  = ObjRot;
    OutRig.BackingSurface.VehicleHullMode = (OutRig.Dynamics.WaterSplash.SplashMode == 2u);

    // Modular ReleaseOrigin selection
    uint32_t ArcMode = 0u;
    (void)ParseJsonUint(PresetJson, "arcMode", ArcMode);
    if (OutRig.Dynamics.Pyro.FireworkMode > 0u)
    {
        FireworkPatternOriginSpec Fw{};
        uint32_t FwShape = 0u;
        (void)ParseJsonUint(PresetJson, "fireworkShape", FwShape);
        Fw.Shape        = static_cast<FireworkShapeCategory>(std::min(9u, FwShape));
        Fw.OriginMeters = OriginZUp;
        (void)ParseJsonString(PresetJson, "fireworkText", Fw.SkyText);
        (void)ParseJsonNumber(PresetJson, "fireworkScale", Fw.PatternScale);
        Fw.ShellMode    = OutRig.Dynamics.Pyro.FireworkMode;
        OutRig.Origin   = std::move(Fw);
    }
    else if (OutRig.Dynamics.Spiderweb.WebMode > 0u)
    {
        OutRig.Origin = SpiderwebOriginSpec{
            OriginZUp,
            OutRig.Dynamics.Spiderweb.WebMode,
            OutRig.Dynamics.Spiderweb.SpokeCount,
            OutRig.Dynamics.Spiderweb.RingCount,
            Radius > 0.1f ? Radius : 2.2f,
        };
    }
    else if (OutRig.Dynamics.Strands.Mode >= StrandDynamicsMode::CablesChains &&
             OutRig.Dynamics.Strands.Mode <= StrandDynamicsMode::FlagSail)
    {
        OutRig.Origin = ClothGridOriginSpec{
            OriginZUp,
            OutRig.Dynamics.Strands.RowCount,
            OutRig.Dynamics.Strands.SegmentCount,
            2.6f,
            OutRig.Dynamics.Strands.HeightMeters,
        };
    }
    else if (OutRig.Dynamics.Strands.Mode >= StrandDynamicsMode::GrassWheat &&
             OutRig.Dynamics.Strands.Mode <= StrandDynamicsMode::SeaAnemone)
    {
        if (OutRig.Dynamics.Strands.Mode == StrandDynamicsMode::CanopyTree)
        {
            OutRig.Origin = CanopySkeletonOriginSpec{
                OriginZUp,
                OutRig.Dynamics.Strands.RowCount,
                OutRig.Dynamics.Strands.SegmentCount,
                OutRig.Dynamics.Strands.HeightMeters,
                1.8f,
            };
        }
        else
        {
            OutRig.Origin = BotanicalRootOriginSpec{
                OriginZUp,
                OutRig.Dynamics.Strands.RowCount,
                OutRig.Dynamics.Strands.SegmentCount,
                2.4f,
                OutRig.Dynamics.Strands.HeightMeters,
            };
        }
    }
    else if (OutRig.Dynamics.Pyro.BeamModeActive > 0.0f || ArcMode > 0u)
    {
        Mathematics::Axis3D TargetYUp{ OriginYUp.X + 4.0f, OriginYUp.Y, OriginYUp.Z };
        (void)ParseJsonVec3(PresetJson, "targetPos", TargetYUp);
        float TargetRad = 0.25f;
        bool  ShowEl    = false;
        (void)ParseJsonNumber(PresetJson, "targetRadius", TargetRad);
        (void)ParseJsonBool(PresetJson, "showElectrodes", ShowEl);
        OutRig.Origin = DualElectrodeOriginSpec{
            OriginZUp,
            HtmlYUpToFrontierZUp(TargetYUp),
            Radius,
            TargetRad,
            ArcMode,
            ShowEl,
        };
    }
    else if (GridMode > 0u || HasObjShape)
    {
        OutRig.Origin = SurfaceAnchorOriginSpec{
            OutRig.BackingSurface.SurfacePreset,
            OutRig.BackingSurface.SampleMode,
            OriginZUp,
            ObjRot,
            Radius > 0.2f ? Radius : 1.5f,
            OutRig.Dynamics.EffectorGrid.Columns,
            OutRig.Dynamics.EffectorGrid.Rows,
            "",
        };
    }
    else if (EmitShape == 1u)
    {
        OutRig.Origin = SphereVolumeOriginSpec{ OriginZUp, Radius, false };
    }
    else if (EmitShape == 2u)
    {
        OutRig.Origin = DiscOriginSpec{ OriginZUp, { 0.0f, 0.0f, 1.0f }, Radius };
    }
    else if (EmitShape == 3u)
    {
        OutRig.Origin = BoxVolumeOriginSpec{ OriginZUp, BoxHalfZUp };
    }
    else if (EmitShape == 4u)
    {
        OutRig.Origin = SurfaceLatticeOriginSpec{
            OriginZUp,
            OutRig.Dynamics.EffectorGrid.Columns,
            OutRig.Dynamics.EffectorGrid.Rows,
            std::max({ BoxHalfZUp.X, BoxHalfZUp.Y, BoxHalfZUp.Z, 0.6f }),
        };
    }
    else
    {
        OutRig.Origin = PointOriginSpec{ OriginZUp };
    }

    // Modular VisualCarrier selection
    if (OutRig.Dynamics.BlackHole.Enabled || OutRig.Dynamics.HeatShimmer.Enabled)
    {
        OutRig.Carrier = RefractionLensCarrier{
            OutRig.Dynamics.BlackHole.Enabled ? RefractionLensCategory::GravitationalAccretion
                                              : RefractionLensCategory::ThermalHeatShimmer,
            OutRig.Dynamics.BlackHole.Enabled ? OutRig.Dynamics.BlackHole.EinsteinGain
                                              : OutRig.Dynamics.HeatShimmer.Strength,
            OutRig.Dynamics.BlackHole.Enabled ? OutRig.Dynamics.BlackHole.HorizonRadius
                                              : OutRig.Dynamics.HeatShimmer.RadiusMeters,
            OutRig.Dynamics.LocalAttractor.SwirlSpeed,
        };
    }
    else if (OutRig.Dynamics.Pyro.BeamModeActive > 0.0f || OutRig.Dynamics.Spiderweb.WebMode > 0u || ArcMode > 0u)
    {
        DielectricBeamCarrier Beam{};
        Beam.ArcCategory = (OutRig.Dynamics.Spiderweb.WebMode > 0u) ? DielectricArcCategory::SpiderwebSilk
                         : (ArcMode == 2u)                          ? DielectricArcCategory::PlasmaCageArc
                         : (ArcMode > 0u)                           ? DielectricArcCategory::TeslaSphereArc
                                                                    : DielectricArcCategory::LaserBeam;
        (void)ParseJsonNumber(PresetJson, "beamWidth", Beam.CoreRadiusMeters);
        (void)ParseJsonNumber(PresetJson, "beamPulse", Beam.PulseSpeedHz);
        Beam.DewIntensity = OutRig.Dynamics.Spiderweb.DewHighlight;
        OutRig.Carrier    = Beam;
    }
    else if (OutRig.Dynamics.Strands.Mode != StrandDynamicsMode::Off || OutRig.Dynamics.Pyro.TrailLengthSec > 0.0f)
    {
        RibbonStrandCarrier Ribbon{};
        Ribbon.SegmentCount   = OutRig.Dynamics.Strands.SegmentCount;
        Ribbon.ThicknessScale = OutRig.Dynamics.Strands.AnemoneThickness;
        Ribbon.CurveDebugPins = OutRig.Dynamics.Strands.CurveDebugEnabled;
        OutRig.Carrier        = Ribbon;
    }
    else if (KindId == 6u || ShapeId == 13u)
    {
        WingedCreatureCarrier Winged{};
        Winged.Species = (LeafMode == 1u) ? WingedSpeciesCategory::Butterfly
                       : (LeafMode == 2u) ? WingedSpeciesCategory::BioluminescentFish
                                          : WingedSpeciesCategory::Songbird;
        OutRig.Carrier = Winged;
    }
    else if (OutRig.Dynamics.Transition.Mode != TransitionDerezMode::Off || OutRig.Dynamics.EffectorGrid.Active ||
             OutRig.Dynamics.Ferrofluid.FerroMode > 0u || OutRig.Dynamics.Fracture.CrackMode > 0u)
    {
        OrientedSolidCarrier Solid{};
        Solid.GeometryKind = (ShapeId == 5u)                                ? SolidGeometryCategory::MetallicCoin
                           : (ShapeId == 18u)                               ? SolidGeometryCategory::GlassShardCrystal
                           : (OutRig.Dynamics.Ferrofluid.FerroMode > 0u)    ? SolidGeometryCategory::FerrofluidSpikeCone
                           : (OutRig.Dynamics.Fracture.CrackMode > 0u)      ? SolidGeometryCategory::VoronoiFractureSlab
                           : (OutRig.Dynamics.EffectorGrid.Active)          ? SolidGeometryCategory::MoGraphExtrudedPrism
                                                                            : SolidGeometryCategory::TumblingCube;
        OutRig.Carrier = Solid;
    }
    else
    {
        SpriteBillboardCarrier Billboard{};
        Billboard.CutoutShape = static_cast<SpriteCutoutCategory>(std::min(19u, ShapeId));
        Billboard.LeafStyle   = static_cast<LeafSubMode>(std::min(3u, LeafMode));
        uint32_t HeadStyle    = 0u;
        (void)ParseJsonUint(PresetJson, "headStyle", HeadStyle);
        Billboard.BokehAperture = static_cast<BokehApertureCategory>(std::min(3u, HeadStyle));
        (void)ParseJsonNumber(PresetJson, "headScale", Billboard.HeadScale);
        (void)ParseJsonNumber(PresetJson, "streakThin", Billboard.StreakThinness);
        OutRig.Carrier = Billboard;
    }

    // Colour block delegated straight to ColourInterchange
    std::string ColourDiag;
    if (!ColourInterchange::ImportHtmlStudioJsonBlock(PresetJson, OutRig.TintBinding.SourceProfile, ColourDiag))
    {
        OutDiagnostic = "ColourInterchange failed: " + ColourDiag;
        return false;
    }
    std::string ColorByTok;
    if (ParseJsonString(PresetJson, "colorBy", ColorByTok))
    {
        OutRig.TintBinding.ScalarChannel = ParseHtmlColorByToken(ColorByTok);
    }
    else
    {
        OutRig.TintBinding.ScalarChannel = ParticleScalarChannel::ModeDefault;
    }

    OutRig.RebuildBakedColourLut();
    OutRig.RebuildOriginCaches(1337u);
    OutDiagnostic.clear();
    return true;
}

std::string ParticleInterchange::ExportHtmlStudioPresetJson(const ParticleRig& Rig)
{
    std::ostringstream Oss;
    Oss << std::fixed << std::setprecision(5);

    const std::string ColourJson = ColourInterchange::ExportHtmlStudioJsonBlock(Rig.TintBinding.SourceProfile);
    // Strip outer braces from ColourJson so fields sit cleanly inside the flat preset JSON object
    std::string_view ColourInner(ColourJson);
    if (ColourInner.size() >= 2u && ColourInner.front() == '{' && ColourInner.back() == '}')
    {
        ColourInner = ColourInner.substr(1u, ColourInner.size() - 2u);
    }

    uint32_t EmitShape = 0u;
    Mathematics::Axis3D OriginZUp { 0.0f, 0.0f, 0.5f };
    Mathematics::Axis3D BoxHalfZUp{ 0.0f, 0.0f, 0.0f };
    float Radius = 0.1f;

    std::visit(
        [&](const auto& Entry)
        {
            using T = std::decay_t<decltype(Entry)>;
            if constexpr (std::is_same_v<T, DualElectrodeOriginSpec>)
            {
                OriginZUp = Entry.CathodeMeters;
                Radius    = Entry.SpreadRadius;
            }
            else
            {
                OriginZUp = Entry.OriginMeters;
            }

            if constexpr (std::is_same_v<T, SphereVolumeOriginSpec>)
            {
                EmitShape = 1u;
                Radius    = Entry.RadiusMeters;
            }
            else if constexpr (std::is_same_v<T, DiscOriginSpec>)
            {
                EmitShape = 2u;
                Radius    = Entry.RadiusMeters;
            }
            else if constexpr (std::is_same_v<T, BoxVolumeOriginSpec>)
            {
                EmitShape  = 3u;
                BoxHalfZUp = Entry.HalfExtentsMeters;
            }
            else if constexpr (std::is_same_v<T, SurfaceLatticeOriginSpec>)
            {
                EmitShape  = 4u;
                BoxHalfZUp = { Entry.ExtentMeters, Entry.ExtentMeters, Entry.ExtentMeters };
            }
        },
        Rig.Origin);

    uint32_t ShapeId  = 0u;
    uint32_t LeafMode = 0u;
    float    HeadScale = 1.0f;
    uint32_t HeadStyle = 0u;
    float    StreakThin = 1.0f;
    if (const auto* B = std::get_if<SpriteBillboardCarrier>(&Rig.Carrier))
    {
        ShapeId    = static_cast<uint32_t>(B->CutoutShape);
        LeafMode   = static_cast<uint32_t>(B->LeafStyle);
        HeadStyle  = static_cast<uint32_t>(B->BokehAperture);
        HeadScale  = B->HeadScale;
        StreakThin = B->StreakThinness;
    }
    else if (const auto* W = std::get_if<WingedCreatureCarrier>(&Rig.Carrier))
    {
        ShapeId  = 13u;
        LeafMode = (W->Species == WingedSpeciesCategory::Butterfly)          ? 1u
                 : (W->Species == WingedSpeciesCategory::BioluminescentFish) ? 2u
                                                                             : 0u;
    }
    else if (const auto* S = std::get_if<OrientedSolidCarrier>(&Rig.Carrier))
    {
        ShapeId = (S->GeometryKind == SolidGeometryCategory::MetallicCoin)      ? 5u
                : (S->GeometryKind == SolidGeometryCategory::GlassShardCrystal) ? 18u
                                                                                : 4u;
    }

    Oss << "{";
    Oss << "\"kind\":" << static_cast<uint32_t>(Rig.Dynamics.Family) << ",";
    Oss << "\"shape\":" << ShapeId << ",";
    Oss << "\"leafMode\":" << LeafMode << ",";
    Oss << "\"capacity\":" << Rig.Sizing.CapacitySlots << ",";
    Oss << "\"rate\":" << Rig.Sizing.ReleaseRatePerSec << ",";
    Oss << "\"emitShape\":" << EmitShape << ",";
    WriteVec3Json(Oss, "origin", FrontierZUpToHtmlYUp(OriginZUp));
    Oss << ",";
    Oss << "\"radius\":" << Radius << ",";
    WriteVec3Json(Oss, "boxHalf", SwapExtentsZUpToYUp(BoxHalfZUp));
    Oss << ",";
    WriteVec3Json(Oss, "dir", FrontierZUpToHtmlYUp(Rig.Dynamics.Ballistics.DirectionAxis));
    Oss << ",";
    Oss << "\"spread\":" << Rig.Dynamics.Ballistics.SpreadRadians << ",";
    Oss << "\"speedMin\":" << Rig.Dynamics.Ballistics.SpeedMin << ",";
    Oss << "\"speedMax\":" << Rig.Dynamics.Ballistics.SpeedMax << ",";
    Oss << "\"drag\":" << Rig.Dynamics.Ballistics.DragPerSecond << ",";
    Oss << "\"gravity\":" << Rig.Dynamics.Ballistics.GravityFactor << ",";
    Oss << "\"buoyancy\":" << Rig.Dynamics.Ballistics.BuoyancyAccel << ",";
    Oss << "\"bounce\":" << Rig.Dynamics.Ballistics.BounceRestitution << ",";
    Oss << "\"flutter\":" << Rig.Dynamics.Ballistics.FlutterAmp << ",";
    Oss << "\"lifeMin\":" << Rig.Sizing.LifetimeMinSec << ",";
    Oss << "\"lifeMax\":" << Rig.Sizing.LifetimeMaxSec << ",";
    Oss << "\"sizeStart\":" << Rig.Sizing.SizeStartMeters << ",";
    Oss << "\"sizeEnd\":" << Rig.Sizing.SizeEndMeters << ",";
    Oss << "\"sizeScale\":" << Rig.Sizing.OpacityPeak << ",";
    Oss << "\"windCoupling\":" << Rig.Dynamics.ForceLink.WindCouplingRate << ",";
    Oss << "\"streakThin\":" << StreakThin << ",";
    Oss << "\"headStyle\":" << HeadStyle << ",";
    Oss << "\"headScale\":" << HeadScale << ",";
    Oss << "\"curlMode\":" << static_cast<uint32_t>(Rig.Dynamics.Curl.Mode) << ",";
    Oss << "\"curlFreq\":" << Rig.Dynamics.Curl.SpatialFreq << ",";
    Oss << "\"curlAmp\":" << Rig.Dynamics.Curl.DriveSpeed << ",";
    Oss << "\"curlRate\":" << Rig.Dynamics.Curl.EvolutionRate << ",";
    Oss << "\"shellRadius\":" << Rig.Dynamics.Curl.ShellRadius << ",";
    Oss << "\"webMode\":" << Rig.Dynamics.Spiderweb.WebMode << ",";
    Oss << "\"dynMode\":" << static_cast<uint32_t>(Rig.Dynamics.Strands.Mode) << ",";
    Oss << "\"dynRows\":" << Rig.Dynamics.Strands.RowCount << ",";
    Oss << "\"dynSegs\":" << Rig.Dynamics.Strands.SegmentCount << ",";
    Oss << "\"gridMode\":" << (Rig.Dynamics.EffectorGrid.Active ? 1u : 0u) << ",";
    Oss << "\"gridSurface\":" << static_cast<uint32_t>(Rig.BackingSurface.SurfacePreset) << ",";
    Oss << "\"colorBy\":\"" << FormatHtmlColorByToken(Rig.TintBinding.ScalarChannel) << "\",";
    Oss << "\"visible\":" << (Rig.Enabled ? "true" : "false");
    if (!ColourInner.empty())
    {
        Oss << "," << ColourInner;
    }
    Oss << "}";
    return Oss.str();
}

bool ParticleInterchange::ImportHtmlStudioSceneJson(
    std::string_view  SceneJson,
    ParticleDocument& OutDocument,
    std::string&      OutDiagnostic)
{
    OutDocument = ParticleDocument{};
    (void)ParseJsonString(SceneJson, "title", OutDocument.DocumentTitle);
    (void)ParseJsonString(SceneJson, "activePresetId", OutDocument.ActivePresetId);
    (void)ParseJsonUint(SceneJson, "seed", OutDocument.RandomSeed);
    (void)ParseJsonNumber(SceneJson, "timeScale", OutDocument.TimeScale);

    ParticleRig Rig{};
    if (!ImportHtmlStudioPresetJson(SceneJson, OutDocument.ActivePresetId, OutDocument.DocumentTitle, false, Rig, OutDiagnostic))
    {
        return false;
    }
    OutDocument.Rigs.push_back(std::move(Rig));
    OutDiagnostic.clear();
    return true;
}

std::string ParticleInterchange::ExportHtmlStudioSceneJson(const ParticleDocument& Document)
{
    std::ostringstream Oss;
    Oss << "{\"title\":\"" << EscapeJson(Document.DocumentTitle) << "\","
        << "\"activePresetId\":\"" << EscapeJson(Document.ActivePresetId) << "\","
        << "\"seed\":" << Document.RandomSeed << ","
        << "\"timeScale\":" << Document.TimeScale;
    if (!Document.Rigs.empty())
    {
        const std::string RigJson = ExportHtmlStudioPresetJson(Document.Rigs.front());
        if (RigJson.size() >= 2u)
        {
            Oss << "," << std::string_view(RigJson).substr(1u, RigJson.size() - 2u);
        }
    }
    Oss << "}";
    return Oss.str();
}

ParticleHandoffPacket ParticleInterchange::BuildHandoffPacket(const ParticleRig& Rig)
{
    ParticleHandoffPacket Packet{};
    Packet.PresetId               = Rig.PresetOriginId;
    Packet.RigTitle               = Rig.RigTitle;
    Packet.ParameterOverridesJson = ExportHtmlStudioPresetJson(Rig);
    return Packet;
}

bool ParticleInterchange::ApplyHandoffPacket(
    const ParticleHandoffPacket& Packet,
    ParticleRig&                 TargetRig,
    std::string&                 OutDiagnostic)
{
    const uint32_t PreservedId = TargetRig.RigId;
    if (!ImportHtmlStudioPresetJson(
            Packet.ParameterOverridesJson,
            Packet.PresetId,
            Packet.RigTitle,
            TargetRig.Quarantined,
            TargetRig,
            OutDiagnostic))
    {
        return false;
    }
    TargetRig.RigId = PreservedId;
    return true;
}

bool ParticleInterchange::ImportExternalBackingSurface(
    const std::filesystem::path& SourcePath,
    ParticleRig&                 TargetRig,
    std::string&                 OutDiagnostic)
{
    GeometrySpace::ConstructedSampleAsset Asset{};
    if (!GeometryInterchange::ImportAssetFromFile(SourcePath, Asset, OutDiagnostic))
    {
        return false;
    }

    SurfaceAnchorOriginSpec Spec{};
    Spec.SurfacePreset         = SurfacePresetCategory::CustomPolygonNetwork;
    Spec.SampleMode            = TargetRig.BackingSurface.SampleMode;
    Spec.EulerRotationDeg      = TargetRig.BackingSurface.EulerDegrees;
    Spec.CustomAssetIdentifier = SourcePath.filename().string();

    TargetRig.BackingSurface.SurfacePreset   = SurfacePresetCategory::CustomPolygonNetwork;
    TargetRig.BackingSurface.CustomAssetPath = SourcePath.string();
    TargetRig.SampledAnchors = SampleSurfaceAnchors(
        Spec,
        std::max(512u, TargetRig.Sizing.CapacitySlots),
        &Asset.Network,
        Mathematics::WorldCoordinateBasis::RightHandedZUp);

    OutDiagnostic.clear();
    return true;
}

} // namespace Frontier::Interchange
