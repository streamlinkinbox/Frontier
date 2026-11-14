/*============================================================================================================================================
                                                           PARTICLECODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of Two-Tier Hybrid .ParticleSpace encoder & decoder reusing VectorFieldCodec inline ForceSources & RevisionCodec

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ParticleCodec.h"

#include <algorithm>
#include <sstream>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL ROUTINES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] toml::array PackVec3(const SimulationSpace::ForceFields::SpatialVector3& Vec)
{
    toml::array Out;
    Out.push_back(static_cast<double>(Vec.X));
    Out.push_back(static_cast<double>(Vec.Y));
    Out.push_back(static_cast<double>(Vec.Z));
    return Out;
}

[[nodiscard]] SimulationSpace::ForceFields::SpatialVector3 UnpackVec3(
    const toml::array*                                  Arr,
    const SimulationSpace::ForceFields::SpatialVector3& DefaultVec) noexcept
{
    if (Arr == nullptr || Arr->size() < 3u)
    {
        return DefaultVec;
    }
    return {
        static_cast<float>((*Arr)[0].value_or(static_cast<double>(DefaultVec.X))),
        static_cast<float>((*Arr)[1].value_or(static_cast<double>(DefaultVec.Y))),
        static_cast<float>((*Arr)[2].value_or(static_cast<double>(DefaultVec.Z))),
    };
}

[[nodiscard]] toml::array PackRgba(Workspaces::ParticleRgba8 Color)
{
    toml::array Out;
    Out.push_back(static_cast<int64_t>(Color.Red));
    Out.push_back(static_cast<int64_t>(Color.Green));
    Out.push_back(static_cast<int64_t>(Color.Blue));
    Out.push_back(static_cast<int64_t>(Color.Alpha));
    return Out;
}

[[nodiscard]] Workspaces::ParticleRgba8 UnpackRgba(
    const toml::array*        Arr,
    Workspaces::ParticleRgba8 DefaultColor) noexcept
{
    if (Arr == nullptr || Arr->size() < 4u)
    {
        return DefaultColor;
    }
    auto ClampByte = [](int64_t V) noexcept -> uint8_t
    {
        return static_cast<uint8_t>(std::clamp<int64_t>(V, 0, 255));
    };
    return {
        ClampByte((*Arr)[0].value_or(static_cast<int64_t>(DefaultColor.Red))),
        ClampByte((*Arr)[1].value_or(static_cast<int64_t>(DefaultColor.Green))),
        ClampByte((*Arr)[2].value_or(static_cast<int64_t>(DefaultColor.Blue))),
        ClampByte((*Arr)[3].value_or(static_cast<int64_t>(DefaultColor.Alpha))),
    };
}

[[nodiscard]] toml::table SerializeEffectTable(const Workspaces::ParticleEffectDescriptor& Effect)
{
    toml::table Entry;
    Entry.insert_or_assign("UniqueToken", static_cast<int64_t>(Effect.UniqueToken));
    Entry.insert_or_assign("PresetIdentifier", Effect.PresetIdentifier);
    Entry.insert_or_assign("Title", Effect.Title);
    Entry.insert_or_assign("Sublabel", Effect.Sublabel);
    Entry.insert_or_assign("Category", static_cast<int64_t>(Effect.Category));
    Entry.insert_or_assign("AuthoringDomain", static_cast<int64_t>(Effect.AuthoringDomain));
    Entry.insert_or_assign("Visible", Effect.Visible);

    Entry.insert_or_assign("ReleaseVolume", static_cast<int64_t>(Effect.ReleaseVolume));
    Entry.insert_or_assign("OriginMeters", PackVec3(Effect.OriginMeters));
    Entry.insert_or_assign("LaunchDirection", PackVec3(Effect.LaunchDirection));
    Entry.insert_or_assign("RadiusMeters", static_cast<double>(Effect.RadiusMeters));
    Entry.insert_or_assign("ConeSpreadRad", static_cast<double>(Effect.ConeSpreadRad));
    Entry.insert_or_assign("Capacity", static_cast<int64_t>(Effect.Capacity));
    Entry.insert_or_assign("BirthRatePerSec", static_cast<double>(Effect.BirthRatePerSec));
    Entry.insert_or_assign("SpeedMinMs", static_cast<double>(Effect.SpeedMinMs));
    Entry.insert_or_assign("SpeedMaxMs", static_cast<double>(Effect.SpeedMaxMs));
    Entry.insert_or_assign("LifeMinSec", static_cast<double>(Effect.LifeMinSec));
    Entry.insert_or_assign("LifeMaxSec", static_cast<double>(Effect.LifeMaxSec));
    Entry.insert_or_assign("BurstIntervalSec", static_cast<double>(Effect.BurstIntervalSec));
    Entry.insert_or_assign("BurstCount", static_cast<int64_t>(Effect.BurstCount));
    Entry.insert_or_assign("BurstOriginAnchored", Effect.BurstOriginAnchored);

    Entry.insert_or_assign("Silhouette", static_cast<int64_t>(Effect.Silhouette));
    Entry.insert_or_assign("BlendCategory", static_cast<int64_t>(Effect.BlendCategory));
    Entry.insert_or_assign("SizeStartMeters", static_cast<double>(Effect.SizeStartMeters));
    Entry.insert_or_assign("SizeEndMeters", static_cast<double>(Effect.SizeEndMeters));
    Entry.insert_or_assign("SizeMultiplier", static_cast<double>(Effect.SizeMultiplier));
    Entry.insert_or_assign("ColorA", PackRgba(Effect.ColorA));
    Entry.insert_or_assign("ColorB", PackRgba(Effect.ColorB));
    Entry.insert_or_assign("ColorC", PackRgba(Effect.ColorC));
    Entry.insert_or_assign("ColorDriver", static_cast<int64_t>(Effect.ColorDriver));
    Entry.insert_or_assign("RampWrap", static_cast<int64_t>(Effect.RampWrap));
    Entry.insert_or_assign("RampInterp", static_cast<int64_t>(Effect.RampInterp));
    Entry.insert_or_assign("RampPhaseOffset", static_cast<double>(Effect.RampPhaseOffset));

    Entry.insert_or_assign("GravityMultiplier", static_cast<double>(Effect.GravityMultiplier));
    Entry.insert_or_assign("DragCoefficient", static_cast<double>(Effect.DragCoefficient));
    Entry.insert_or_assign("ForceCouplingGain", static_cast<double>(Effect.ForceCouplingGain));
    Entry.insert_or_assign("CurlAmplitude", static_cast<double>(Effect.CurlAmplitude));
    Entry.insert_or_assign("CurlFrequency", static_cast<double>(Effect.CurlFrequency));
    Entry.insert_or_assign("FloorRestitution", static_cast<double>(Effect.FloorRestitution));
    Entry.insert_or_assign("SpinRateRadSec", static_cast<double>(Effect.SpinRateRadSec));
    Entry.insert_or_assign("BallInteractionEnabled", Effect.BallInteractionEnabled);
    Entry.insert_or_assign("RibbonTrailsEnabled", Effect.RibbonTrailsEnabled);

    Entry.insert_or_assign("RiggingArchitecture", static_cast<int64_t>(Effect.RiggingArchitecture));
    Entry.insert_or_assign("StrandRows", static_cast<int64_t>(Effect.StrandRows));
    Entry.insert_or_assign("CurveSegments", static_cast<int64_t>(Effect.CurveSegments));
    Entry.insert_or_assign("StrandHeightMeters", static_cast<double>(Effect.StrandHeightMeters));
    Entry.insert_or_assign("CatenarySagMeters", static_cast<double>(Effect.CatenarySagMeters));
    Entry.insert_or_assign("ElasticStiffness", static_cast<double>(Effect.ElasticStiffness));
    Entry.insert_or_assign("WaveSpeedHz", static_cast<double>(Effect.WaveSpeedHz));
    Entry.insert_or_assign("TearSpeedMs", static_cast<double>(Effect.TearSpeedMs));
    Entry.insert_or_assign("CurveDebugEnabled", Effect.CurveDebugEnabled);

    Entry.insert_or_assign("FerrofluidVessel", static_cast<int64_t>(Effect.FerrofluidVessel));
    Entry.insert_or_assign("FerroSpikeCount", static_cast<int64_t>(Effect.FerroSpikeCount));
    Entry.insert_or_assign("MagnetOrbitSpeedRadS", static_cast<double>(Effect.MagnetOrbitSpeedRadS));
    Entry.insert_or_assign("MaxSpikeHeightMeters", static_cast<double>(Effect.MaxSpikeHeightMeters));
    Entry.insert_or_assign("ManualMagnetPole", Effect.ManualMagnetPole);

    Entry.insert_or_assign("FireworkPattern", static_cast<int64_t>(Effect.FireworkPattern));
    Entry.insert_or_assign("FireworkSkyText", Effect.FireworkSkyText);

    Entry.insert_or_assign("FracturePattern", static_cast<int64_t>(Effect.FracturePattern));
    Entry.insert_or_assign("FractureDepthMeters", static_cast<double>(Effect.FractureDepthMeters));
    Entry.insert_or_assign("CrackBranchDensity", static_cast<double>(Effect.CrackBranchDensity));

    Entry.insert_or_assign("EffectorArchitecture", static_cast<int64_t>(Effect.EffectorArchitecture));
    Entry.insert_or_assign("EffectorFalloffMeters", static_cast<double>(Effect.EffectorFalloffMeters));
    Entry.insert_or_assign("EffectorExtrusionMeters", static_cast<double>(Effect.EffectorExtrusionMeters));

    Entry.insert_or_assign("ShieldGeometry", static_cast<int64_t>(Effect.ShieldGeometry));
    Entry.insert_or_assign("ShieldRippleSpeed", static_cast<double>(Effect.ShieldRippleSpeed));
    Entry.insert_or_assign("ShieldCellDensity", static_cast<double>(Effect.ShieldCellDensity));

    Entry.insert_or_assign("BoidsCreatureVariant", static_cast<int64_t>(Effect.BoidsCreatureVariant));
    Entry.insert_or_assign("BoidsPerceptionMeters", static_cast<double>(Effect.BoidsPerceptionMeters));
    Entry.insert_or_assign("BoidsCruiseSpeedMs", static_cast<double>(Effect.BoidsCruiseSpeedMs));

    Entry.insert_or_assign("TransitionHoldSec", static_cast<double>(Effect.TransitionHoldSec));
    Entry.insert_or_assign("DistortionStrength", static_cast<double>(Effect.DistortionStrength));
    Entry.insert_or_assign("DeterministicSeed", static_cast<int64_t>(Effect.DeterministicSeed));
    return Entry;
}

[[nodiscard]] Workspaces::ParticleEffectDescriptor DeserializeEffectTable(const toml::table& Entry)
{
    Workspaces::ParticleEffectDescriptor Effect{};
    Effect.UniqueToken      = static_cast<uint32_t>(Entry["UniqueToken"].value_or(int64_t{ 0 }));
    Effect.PresetIdentifier = Entry["PresetIdentifier"].value_or(std::string{});
    Effect.Title            = Entry["Title"].value_or(std::string("Particle Effect"));
    Effect.Sublabel         = Entry["Sublabel"].value_or(std::string{});
    Effect.Category         = static_cast<Workspaces::ParticleEffectCategory>(
        std::clamp<int64_t>(Entry["Category"].value_or(int64_t{ 10 }), 0, static_cast<int64_t>(Workspaces::ParticleEffectCategory::Count) - 1));
    Effect.AuthoringDomain  = static_cast<Workspaces::ParticleAuthoringDomain>(
        std::clamp<int64_t>(Entry["AuthoringDomain"].value_or(int64_t{ 0 }), 0, 9));
    Effect.Visible          = Entry["Visible"].value_or(true);

    Effect.ReleaseVolume       = static_cast<Workspaces::ParticleReleaseVolume>(
        std::clamp<int64_t>(Entry["ReleaseVolume"].value_or(int64_t{ 0 }), 0, static_cast<int64_t>(Workspaces::ParticleReleaseVolume::Count) - 1));
    Effect.OriginMeters        = UnpackVec3(Entry["OriginMeters"].as_array(), Effect.OriginMeters);
    Effect.LaunchDirection     = UnpackVec3(Entry["LaunchDirection"].as_array(), Effect.LaunchDirection);
    Effect.RadiusMeters        = static_cast<float>(Entry["RadiusMeters"].value_or(static_cast<double>(Effect.RadiusMeters)));
    Effect.ConeSpreadRad       = static_cast<float>(Entry["ConeSpreadRad"].value_or(static_cast<double>(Effect.ConeSpreadRad)));
    Effect.Capacity            = static_cast<uint32_t>(std::max<int64_t>(16, Entry["Capacity"].value_or(static_cast<int64_t>(Effect.Capacity))));
    Effect.BirthRatePerSec     = static_cast<float>(Entry["BirthRatePerSec"].value_or(static_cast<double>(Effect.BirthRatePerSec)));
    Effect.SpeedMinMs          = static_cast<float>(Entry["SpeedMinMs"].value_or(static_cast<double>(Effect.SpeedMinMs)));
    Effect.SpeedMaxMs          = static_cast<float>(Entry["SpeedMaxMs"].value_or(static_cast<double>(Effect.SpeedMaxMs)));
    Effect.LifeMinSec          = static_cast<float>(Entry["LifeMinSec"].value_or(static_cast<double>(Effect.LifeMinSec)));
    Effect.LifeMaxSec          = static_cast<float>(Entry["LifeMaxSec"].value_or(static_cast<double>(Effect.LifeMaxSec)));
    Effect.BurstIntervalSec    = static_cast<float>(Entry["BurstIntervalSec"].value_or(static_cast<double>(Effect.BurstIntervalSec)));
    Effect.BurstCount          = static_cast<uint32_t>(std::max<int64_t>(0, Entry["BurstCount"].value_or(static_cast<int64_t>(Effect.BurstCount))));
    Effect.BurstOriginAnchored = Entry["BurstOriginAnchored"].value_or(bool(Effect.BurstOriginAnchored));

    Effect.Silhouette      = static_cast<Workspaces::ParticleVisualSilhouette>(
        std::clamp<int64_t>(Entry["Silhouette"].value_or(int64_t{ 0 }), 0, static_cast<int64_t>(Workspaces::ParticleVisualSilhouette::Count) - 1));
    Effect.BlendCategory   = static_cast<Workspaces::ParticleBlendCategory>(
        std::clamp<int64_t>(Entry["BlendCategory"].value_or(int64_t{ 0 }), 0, 1));
    Effect.SizeStartMeters = static_cast<float>(Entry["SizeStartMeters"].value_or(static_cast<double>(Effect.SizeStartMeters)));
    Effect.SizeEndMeters   = static_cast<float>(Entry["SizeEndMeters"].value_or(static_cast<double>(Effect.SizeEndMeters)));
    Effect.SizeMultiplier  = static_cast<float>(Entry["SizeMultiplier"].value_or(static_cast<double>(Effect.SizeMultiplier)));
    Effect.ColorA          = UnpackRgba(Entry["ColorA"].as_array(), Effect.ColorA);
    Effect.ColorB          = UnpackRgba(Entry["ColorB"].as_array(), Effect.ColorB);
    Effect.ColorC          = UnpackRgba(Entry["ColorC"].as_array(), Effect.ColorC);
    Effect.ColorDriver     = static_cast<Workspaces::ParticleColorDriver>(
        std::clamp<int64_t>(Entry["ColorDriver"].value_or(int64_t{ 1 }), 0, static_cast<int64_t>(Workspaces::ParticleColorDriver::Count) - 1));
    Effect.RampWrap        = static_cast<Workspaces::ParticleRampWrapCategory>(
        std::clamp<int64_t>(Entry["RampWrap"].value_or(int64_t{ 0 }), 0, 2));
    Effect.RampInterp      = static_cast<Workspaces::ParticleRampInterpCategory>(
        std::clamp<int64_t>(Entry["RampInterp"].value_or(int64_t{ 1 }), 0, 2));
    Effect.RampPhaseOffset = static_cast<float>(Entry["RampPhaseOffset"].value_or(static_cast<double>(Effect.RampPhaseOffset)));

    Effect.GravityMultiplier      = static_cast<float>(Entry["GravityMultiplier"].value_or(static_cast<double>(Effect.GravityMultiplier)));
    Effect.DragCoefficient        = static_cast<float>(Entry["DragCoefficient"].value_or(static_cast<double>(Effect.DragCoefficient)));
    Effect.ForceCouplingGain      = static_cast<float>(Entry["ForceCouplingGain"].value_or(static_cast<double>(Effect.ForceCouplingGain)));
    Effect.CurlAmplitude          = static_cast<float>(Entry["CurlAmplitude"].value_or(static_cast<double>(Effect.CurlAmplitude)));
    Effect.CurlFrequency          = static_cast<float>(Entry["CurlFrequency"].value_or(static_cast<double>(Effect.CurlFrequency)));
    Effect.FloorRestitution       = static_cast<float>(Entry["FloorRestitution"].value_or(static_cast<double>(Effect.FloorRestitution)));
    Effect.SpinRateRadSec         = static_cast<float>(Entry["SpinRateRadSec"].value_or(static_cast<double>(Effect.SpinRateRadSec)));
    Effect.BallInteractionEnabled = Entry["BallInteractionEnabled"].value_or(bool(Effect.BallInteractionEnabled));
    Effect.RibbonTrailsEnabled    = Entry["RibbonTrailsEnabled"].value_or(bool(Effect.RibbonTrailsEnabled));

    Effect.RiggingArchitecture = static_cast<uint8_t>(std::clamp<int64_t>(Entry["RiggingArchitecture"].value_or(int64_t{ 0 }), 0, 9));
    Effect.StrandRows          = static_cast<uint32_t>(std::max<int64_t>(1, Entry["StrandRows"].value_or(static_cast<int64_t>(Effect.StrandRows))));
    Effect.CurveSegments       = static_cast<uint32_t>(std::max<int64_t>(2, Entry["CurveSegments"].value_or(static_cast<int64_t>(Effect.CurveSegments))));
    Effect.StrandHeightMeters  = static_cast<float>(Entry["StrandHeightMeters"].value_or(static_cast<double>(Effect.StrandHeightMeters)));
    Effect.CatenarySagMeters   = static_cast<float>(Entry["CatenarySagMeters"].value_or(static_cast<double>(Effect.CatenarySagMeters)));
    Effect.ElasticStiffness    = static_cast<float>(Entry["ElasticStiffness"].value_or(static_cast<double>(Effect.ElasticStiffness)));
    Effect.WaveSpeedHz         = static_cast<float>(Entry["WaveSpeedHz"].value_or(static_cast<double>(Effect.WaveSpeedHz)));
    Effect.TearSpeedMs         = static_cast<float>(Entry["TearSpeedMs"].value_or(static_cast<double>(Effect.TearSpeedMs)));
    Effect.CurveDebugEnabled   = Entry["CurveDebugEnabled"].value_or(bool(Effect.CurveDebugEnabled));

    Effect.FerrofluidVessel     = static_cast<uint8_t>(std::clamp<int64_t>(Entry["FerrofluidVessel"].value_or(int64_t{ 0 }), 0, 2));
    Effect.FerroSpikeCount      = static_cast<uint32_t>(std::max<int64_t>(8, Entry["FerroSpikeCount"].value_or(static_cast<int64_t>(Effect.FerroSpikeCount))));
    Effect.MagnetOrbitSpeedRadS = static_cast<float>(Entry["MagnetOrbitSpeedRadS"].value_or(static_cast<double>(Effect.MagnetOrbitSpeedRadS)));
    Effect.MaxSpikeHeightMeters = static_cast<float>(Entry["MaxSpikeHeightMeters"].value_or(static_cast<double>(Effect.MaxSpikeHeightMeters)));
    Effect.ManualMagnetPole     = Entry["ManualMagnetPole"].value_or(bool(Effect.ManualMagnetPole));

    Effect.FireworkPattern = static_cast<Workspaces::FireworkPatternCategory>(
        std::clamp<int64_t>(Entry["FireworkPattern"].value_or(int64_t{ 9 }), 0, static_cast<int64_t>(Workspaces::FireworkPatternCategory::Count) - 1));
    Effect.FireworkSkyText = Entry["FireworkSkyText"].value_or(std::string(Effect.FireworkSkyText));

    Effect.FracturePattern     = static_cast<uint8_t>(std::clamp<int64_t>(Entry["FracturePattern"].value_or(int64_t{ 0 }), 0, 2));
    Effect.FractureDepthMeters = static_cast<float>(Entry["FractureDepthMeters"].value_or(static_cast<double>(Effect.FractureDepthMeters)));
    Effect.CrackBranchDensity  = static_cast<float>(Entry["CrackBranchDensity"].value_or(static_cast<double>(Effect.CrackBranchDensity)));

    Effect.EffectorArchitecture    = static_cast<uint8_t>(std::clamp<int64_t>(Entry["EffectorArchitecture"].value_or(int64_t{ 0 }), 0, 5));
    Effect.EffectorFalloffMeters   = static_cast<float>(Entry["EffectorFalloffMeters"].value_or(static_cast<double>(Effect.EffectorFalloffMeters)));
    Effect.EffectorExtrusionMeters = static_cast<float>(Entry["EffectorExtrusionMeters"].value_or(static_cast<double>(Effect.EffectorExtrusionMeters)));

    Effect.ShieldGeometry    = static_cast<uint8_t>(std::clamp<int64_t>(Entry["ShieldGeometry"].value_or(int64_t{ 0 }), 0, 3));
    Effect.ShieldRippleSpeed = static_cast<float>(Entry["ShieldRippleSpeed"].value_or(static_cast<double>(Effect.ShieldRippleSpeed)));
    Effect.ShieldCellDensity = static_cast<float>(Entry["ShieldCellDensity"].value_or(static_cast<double>(Effect.ShieldCellDensity)));

    Effect.BoidsCreatureVariant  = static_cast<uint8_t>(std::clamp<int64_t>(Entry["BoidsCreatureVariant"].value_or(int64_t{ 0 }), 0, 2));
    Effect.BoidsPerceptionMeters = static_cast<float>(Entry["BoidsPerceptionMeters"].value_or(static_cast<double>(Effect.BoidsPerceptionMeters)));
    Effect.BoidsCruiseSpeedMs    = static_cast<float>(Entry["BoidsCruiseSpeedMs"].value_or(static_cast<double>(Effect.BoidsCruiseSpeedMs)));

    Effect.TransitionHoldSec  = static_cast<float>(Entry["TransitionHoldSec"].value_or(static_cast<double>(Effect.TransitionHoldSec)));
    Effect.DistortionStrength = static_cast<float>(Entry["DistortionStrength"].value_or(static_cast<double>(Effect.DistortionStrength)));
    Effect.DeterministicSeed  = static_cast<uint32_t>(Entry["DeterministicSeed"].value_or(static_cast<int64_t>(Effect.DeterministicSeed)));
    return Effect;
}

[[nodiscard]] toml::table SerializeDocumentManifest(const ParticleSpaceDocument& Document)
{
    toml::table Manifest;

    toml::table Header;
    Header.insert_or_assign("DocumentTitle", Document.DocumentTitle);
    Header.insert_or_assign("CoordinateSystem", Document.CoordinateSystem);
    Header.insert_or_assign("CascadeResolution", static_cast<int64_t>(Document.CascadeResolution));
    Header.insert_or_assign("EffectCount", static_cast<int64_t>(Document.Effects.size()));
    Manifest.insert_or_assign("ParticleSpace", std::move(Header));

    toml::table Playback;
    Playback.insert_or_assign("CurrentSeconds", static_cast<double>(Document.PlaybackClock.CurrentSeconds));
    Playback.insert_or_assign("DurationSeconds", static_cast<double>(Document.PlaybackClock.DurationSeconds));
    Playback.insert_or_assign("PlaybackRate", static_cast<double>(Document.PlaybackClock.PlaybackRate));
    Playback.insert_or_assign("FixedStepHz", static_cast<double>(Document.PlaybackClock.FixedStepHz));
    Playback.insert_or_assign("Playing", Document.PlaybackClock.Playing);
    Playback.insert_or_assign("LoopEnabled", Document.PlaybackClock.LoopEnabled);
    Manifest.insert_or_assign("PlaybackClock", std::move(Playback));

    if (!Document.Effects.empty())
    {
        toml::array EffectsArray;
        for (const Workspaces::ParticleEffectDescriptor& Effect : Document.Effects)
        {
            EffectsArray.push_back(SerializeEffectTable(Effect));
        }
        Manifest.insert_or_assign("ParticleEffects", std::move(EffectsArray));
    }

    SerializeInlineForceSourcesToManifest(Document.ForceSources, Document.LinkedVectorFieldAssets, Manifest);
    return Manifest;
}

bool PopulateDocumentManifest(
    const toml::table&     Manifest,
    ParticleSpaceDocument& OutDocument,
    std::string&           OutDiagnostic)
{
    OutDocument = ParticleSpaceDocument{};

    if (const toml::table* Header = Manifest["ParticleSpace"].as_table())
    {
        OutDocument.DocumentTitle     = (*Header)["DocumentTitle"].value_or(std::string(OutDocument.DocumentTitle));
        OutDocument.CoordinateSystem  = (*Header)["CoordinateSystem"].value_or(std::string("RightHandZUp"));
        OutDocument.CascadeResolution = static_cast<uint32_t>(std::clamp<int64_t>((*Header)["CascadeResolution"].value_or(int64_t{ 16 }), 8, 64));
    }

    if (const toml::table* Playback = Manifest["PlaybackClock"].as_table())
    {
        OutDocument.PlaybackClock.CurrentSeconds  = static_cast<float>((*Playback)["CurrentSeconds"].value_or(0.0));
        OutDocument.PlaybackClock.DurationSeconds = static_cast<float>((*Playback)["DurationSeconds"].value_or(12.0));
        OutDocument.PlaybackClock.PlaybackRate    = static_cast<float>((*Playback)["PlaybackRate"].value_or(1.0));
        OutDocument.PlaybackClock.FixedStepHz     = static_cast<float>((*Playback)["FixedStepHz"].value_or(60.0));
        OutDocument.PlaybackClock.Playing         = (*Playback)["Playing"].value_or(true);
        OutDocument.PlaybackClock.LoopEnabled     = (*Playback)["LoopEnabled"].value_or(true);
    }

    if (const toml::array* EffectsArray = Manifest["ParticleEffects"].as_array())
    {
        OutDocument.Effects.reserve(EffectsArray->size());
        for (const toml::node& EntrySlot : *EffectsArray)
        {
            if (const toml::table* EntryTable = EntrySlot.as_table())
            {
                OutDocument.Effects.push_back(DeserializeEffectTable(*EntryTable));
            }
        }
    }

    DeserializeInlineForceSourcesFromManifest(Manifest, OutDocument.ForceSources, OutDocument.LinkedVectorFieldAssets);
    OutDiagnostic.clear();
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 PARTICLE CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload ParticleEncoder::ConstructPayload(
    const ParticleSpaceDocument&       Document,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::ParticleSpace;
    Payload.SchemaVersion      = 1u;
    Payload.DocumentTitle      = Document.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.ParticleWorkspace";
    Payload.DomainManifest     = SerializeDocumentManifest(Document);

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool ParticleEncoder::EncodeFile(
    const std::filesystem::path&       TargetPath,
    const ParticleSpaceDocument&       Document,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, ConstructPayload(Document, OptionalRevision), OutDiagnostic);
}

std::string ParticleEncoder::FormatManifestToml(const ParticleSpaceDocument& Document)
{
    std::ostringstream Stream;
    Stream << SerializeDocumentManifest(Document);
    return Stream.str();
}

bool ParticleDecoder::ExtractDocument(
    const SpaceContainerPayload& Payload,
    ParticleSpaceDocument&       OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::ParticleSpace)
    {
        OutDiagnostic = "Expected .ParticleSpace container category.";
        return false;
    }

    if (!PopulateDocumentManifest(Payload.DomainManifest, OutDocument, OutDiagnostic))
    {
        return false;
    }
    if (OutDocument.DocumentTitle.empty())
    {
        OutDocument.DocumentTitle = Payload.DocumentTitle;
    }

    if (OptionalOutRevision != nullptr)
    {
        std::string RevisionDiagnostic;
        (void)RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, RevisionDiagnostic);
    }
    return true;
}

bool ParticleDecoder::DecodeFile(
    const std::filesystem::path& SourcePath,
    ParticleSpaceDocument&       OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDocument(Payload, OutDocument, OutDiagnostic, OptionalOutRevision);
}

bool ParticleDecoder::ParseManifestToml(
    std::string_view       ManifestToml,
    ParticleSpaceDocument& OutDocument,
    std::string&           OutDiagnostic)
{
    try
    {
        const toml::table Parsed = toml::parse(ManifestToml);
        return PopulateDocumentManifest(Parsed, OutDocument, OutDiagnostic);
    }
    catch (const toml::parse_error& Error)
    {
        OutDiagnostic = std::string("Failed to parse .ParticleSpace TOML manifest: ") + std::string(Error.description());
        return false;
    }
}

} // namespace Frontier::Codecs
