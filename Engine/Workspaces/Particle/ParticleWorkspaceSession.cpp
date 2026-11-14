/*============================================================================================================================================
                                                      PARTICLEWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of headless ParticleWorkspaceSession coupling live ParticleSimulations, ForceFields, ParticleCodec & RevisionTracker

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include "Engine/Codecs/VectorFieldCodec.h"
#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace Frontier::Workspaces
{

using namespace SimulationSpace::ForceFields;
using namespace SimulationSpace::Particles;

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL ROUTINES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] ParametricOperationParameters ConstructRevisionParams(std::string_view OperationLabel, uint32_t EffectCount) noexcept
{
    ParametricOperationParameters Params{};
    Params.Category      = ParametricOperationCategory::StructuredPayloadDelta;
    Params.TargetChannel = EffectCount;
    const std::size_t CopyLen = std::min<std::size_t>(OperationLabel.size(), sizeof(Params.OperationTag) - 1u);
    if (CopyLen > 0u)
    {
        std::memcpy(Params.OperationTag, OperationLabel.data(), CopyLen);
    }
    Params.OperationTag[CopyLen] = '\0';
    return Params;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTRUCTOR & DEFAULTS
//------------------------------------------------------------------------------------------------------------------------

ParticleWorkspaceSession::ParticleWorkspaceSession()
{
    ForceOverlay.Enabled                      = true;
    ForceOverlay.ShowHeatmap                  = true;
    ForceOverlay.ShowVectors                  = true;
    ForceOverlay.ShowStreamlines              = true;
    ForceOverlay.ShowBounds                   = true;
    ForceOverlay.CrossSectionAnemometerActive = true;
    ForceOverlay.AnemometerWorldMeters        = { 1.5f, 0.5f, 3.0f };

    PopulateDefaultStudioScene();
}

void ParticleWorkspaceSession::PopulateDefaultStudioScene()
{
    ClearAllEntries();

    // Reused C++ ForceFields environment sources (Ambient Wind + Thermal Updraft + Curl + Helical Column + Magnetic Dipole)
    const uint32_t WindToken = AddForceSource(ForceModel::LinearWind);
    if (ForceSourceRecordEntry* Wind = FindForceSource(WindToken))
    {
        Wind->Label        = "Ambient Wind";
        Wind->Strength     = 7.4f;
        Wind->RadiusMeters = 48.0f;
        Wind->CentreMeters = { 0.0f, 0.0f, 4.0f };
        Wind->EulerDegrees = { 0.0f, 0.0f, 32.0f };
    }

    const uint32_t ThermalToken = AddForceSource(ForceModel::FlowStream);
    if (ForceSourceRecordEntry* Thermal = FindForceSource(ThermalToken))
    {
        Thermal->Label         = "Thermal Updraft";
        Thermal->StreamProfile = FlowStreamProfile::ThermalUpdraft;
        Thermal->Strength      = 6.2f;
        Thermal->RadiusMeters  = 16.0f;
        Thermal->CentreMeters  = { -2.5f, 1.5f, 2.0f };
    }

    const uint32_t CurlToken = AddForceSource(ForceModel::CurlNoise);
    if (ForceSourceRecordEntry* Curl = FindForceSource(CurlToken))
    {
        Curl->Label                 = "Atmospheric Curl";
        Curl->Strength              = 4.8f;
        Curl->RadiusMeters          = 24.0f;
        Curl->TurbulenceScaleMeters = 9.5f;
        Curl->OctaveCount           = 3u;
        Curl->CentreMeters          = { 0.0f, 0.0f, 3.0f };
    }

    const uint32_t HelicalToken = AddForceSource(ForceModel::RotationalColumn);
    if (ForceSourceRecordEntry* Helical = FindForceSource(HelicalToken))
    {
        Helical->Label        = "Helical Column";
        Helical->Strength     = 11.2f;
        Helical->RadiusMeters = 18.0f;
        Helical->SwirlRatio   = 0.72f;
        Helical->CentreMeters = { 3.5f, -2.0f, 3.0f };
    }

    const uint32_t MagnetToken = AddForceSource(ForceModel::MagneticDipole);
    if (ForceSourceRecordEntry* Magnet = FindForceSource(MagnetToken))
    {
        Magnet->Label        = "Neodymium Dipole";
        Magnet->Strength     = 14.0f;
        Magnet->RadiusMeters = 12.0f;
        Magnet->CentreMeters = { 0.0f, 0.0f, 1.8f };
        Magnet->Enabled      = false;
        Magnet->Visible      = false;
    }

    // Representative non-quarantined effects spanning diverse active categories
    const uint32_t FerroToken = AddPresetEffect("ferrofluid-spikes");
    (void)AddPresetEffect("fireworks-spectacle");
    (void)AddPresetEffect("curl-fur-field");
    (void)AddPresetEffect("interactive-tree");
    (void)AddPresetEffect("cracked-ice-sheet");
    (void)AddPresetEffect("effector-grid-cubes");
    (void)AddPresetEffect("energy-shield-hex");
    (void)AddPresetEffect("watermist");

    SynchronizeForceFieldsAccumulator();

    // Prime the Beaufort Anemometer rolling trace and initial particle simulation ticks
    for (uint32_t StepIndex = 0u; StepIndex < 36u; ++StepIndex)
    {
        AdvanceSimulationClock(0.05);
    }

    SelectParticleEffect(FerroToken);
    AuthoringRevision = 1u;
    ResetRevisionBaseline("DefaultStudioScene");
}

void ParticleWorkspaceSession::ClearAllEntries()
{
    Effects.clear();
    LiveSimulations.clear();
    ForceSources.clear();
    LinkedVectorFields.clear();
    AnemometerSampleCount = 0u;
    AnemometerRing.fill(0.0f);
    SelectedWaypoint  = -1;
    SelectionDomain   = ParticleSelectionDomain::EnvironmentField;
    SelectedToken     = 0u;
    NextEffectToken   = 1u;
    NextForceToken    = 1u;
    SimulationTimeSec = 0.0;
    PlaybackClock.CurrentSeconds = 0.0f;
    SynchronizeForceFieldsAccumulator();
    IncrementRevision();
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PARTICLE EFFECT LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ParticleEffectDescriptor ParticleWorkspaceSession::ConstructEffectDescriptor(
    const ParticlePresetDescriptor& Preset,
    uint32_t                        AssignedToken) const
{
    ParticleEffectDescriptor Entry{};
    Entry.UniqueToken      = AssignedToken;
    Entry.PresetIdentifier = Preset.Identifier;
    Entry.Title            = Preset.DisplayName;
    Entry.Sublabel         = Preset.Sublabel;
    Entry.Category         = Preset.Category;
    Entry.AuthoringDomain  = Preset.AuthoringDomain;
    Entry.Visible          = true;

    Entry.ReleaseVolume       = Preset.ReleaseVolume;
    Entry.RadiusMeters        = Preset.RadiusMeters;
    Entry.Capacity            = Preset.Capacity;
    Entry.BirthRatePerSec     = Preset.BirthRatePerSec;
    Entry.SpeedMinMs          = Preset.SpeedMinMs;
    Entry.SpeedMaxMs          = Preset.SpeedMaxMs;
    Entry.LifeMinSec          = Preset.LifeMinSec;
    Entry.LifeMaxSec          = Preset.LifeMaxSec;
    Entry.Silhouette          = Preset.Silhouette;
    Entry.BlendCategory       = Preset.BlendCategory;
    Entry.SizeStartMeters     = Preset.SizeStartMeters;
    Entry.SizeEndMeters       = Preset.SizeEndMeters;
    Entry.ColorA              = Preset.ColorA;
    Entry.ColorB              = Preset.ColorB;
    Entry.ColorC              = Preset.ColorC;
    Entry.DragCoefficient     = Preset.DragCoefficient;
    Entry.GravityMultiplier   = Preset.GravityMultiplier;
    Entry.RiggingArchitecture = Preset.RiggingArchitecture;
    Entry.FerrofluidVessel    = Preset.FerrofluidVessel;
    Entry.FracturePattern     = Preset.FracturePattern;
    Entry.EffectorArchitecture = Preset.EffectorArchitecture;
    Entry.FireworkPattern     = static_cast<FireworkPatternCategory>(
        Preset.FireworkPattern < static_cast<uint8_t>(FireworkPatternCategory::Count) ? Preset.FireworkPattern : 0u);
    Entry.DeterministicSeed   = 2026u + AssignedToken * 37u;

    if (Entry.AuthoringDomain == ParticleAuthoringDomain::StrandBotanical)
    {
        Entry.OriginMeters       = { 0.0f, 0.0f, 0.0f };
        Entry.StrandRows         = (Entry.RiggingArchitecture == 3u) ? 24u : 18u;
        Entry.CurveSegments      = 12u;
        Entry.StrandHeightMeters = (Entry.RiggingArchitecture == 7u) ? 4.5f : 2.2f;
        Entry.CatenarySagMeters  = (Entry.RiggingArchitecture <= 2u) ? 0.55f : 0.25f;
        Entry.TearSpeedMs        = (Entry.RiggingArchitecture == 3u) ? 8.5f : 0.0f;
    }
    else if (Entry.AuthoringDomain == ParticleAuthoringDomain::FerrofluidFlux)
    {
        Entry.OriginMeters         = { 0.0f, 0.0f, (Entry.FerrofluidVessel == 2u) ? 1.5f : 0.08f };
        Entry.FerroSpikeCount      = (Entry.FerrofluidVessel == 2u) ? 220u : 192u;
        Entry.MaxSpikeHeightMeters = 0.92f;
        Entry.MagnetOrbitSpeedRadS = 1.15f;
    }
    else if (Entry.AuthoringDomain == ParticleAuthoringDomain::FireworksSpectacle)
    {
        Entry.OriginMeters     = { 0.0f, 0.0f, 1.2f };
        Entry.BurstIntervalSec = 1.65f;
        Entry.BurstCount       = 240u;
        Entry.FireworkSkyText  = "2026";
    }
    else if (Entry.AuthoringDomain == ParticleAuthoringDomain::FractureSeismic)
    {
        Entry.OriginMeters        = { 0.0f, 0.0f, 0.02f };
        Entry.FractureDepthMeters = (Entry.FracturePattern == 2u) ? 0.85f : 0.38f;
        Entry.CrackBranchDensity  = 1.35f;
    }
    else if (Entry.AuthoringDomain == ParticleAuthoringDomain::MoGraphEffector)
    {
        Entry.OriginMeters            = { 0.0f, 0.0f, 0.5f };
        Entry.EffectorFalloffMeters   = 2.2f;
        Entry.EffectorExtrusionMeters = 1.5f;
    }
    else if (Entry.AuthoringDomain == ParticleAuthoringDomain::EnergyShield)
    {
        Entry.OriginMeters      = { 0.0f, 0.0f, 1.4f };
        Entry.ShieldGeometry    = 1u;
        Entry.ShieldRippleSpeed = 2.6f;
        Entry.ShieldCellDensity = 16.0f;
    }
    else if (Entry.AuthoringDomain == ParticleAuthoringDomain::BoidsFlock)
    {
        Entry.OriginMeters          = { 0.0f, 0.0f, 2.2f };
        Entry.BoidsCreatureVariant  = (Entry.PresetIdentifier == "butterflies") ? 1u : 0u;
        Entry.BoidsPerceptionMeters = 1.8f;
        Entry.BoidsCruiseSpeedMs    = 4.6f;
    }
    else
    {
        Entry.OriginMeters = { 0.0f, 0.0f, 1.2f };
    }

    return Entry;
}

uint32_t ParticleWorkspaceSession::AddPresetEffect(std::string_view PresetIdentifier)
{
    const ParticlePresetDescriptor* Preset = ParticlePresetRegistry::FindPreset(PresetIdentifier);
    if (Preset == nullptr)
    {
        return 0u;
    }

    const uint32_t Token = NextEffectToken++;
    Effects.push_back(ConstructEffectDescriptor(*Preset, Token));
    RebuildEffectSimulation(Token);
    SelectionDomain = ParticleSelectionDomain::ParticleEffect;
    SelectedToken   = Token;
    IncrementRevision();
    return Token;
}

uint32_t ParticleWorkspaceSession::DuplicateEffect(uint32_t EffectToken)
{
    const ParticleEffectDescriptor* Existing = FindEffect(EffectToken);
    if (Existing == nullptr)
    {
        return 0u;
    }

    ParticleEffectDescriptor Copy = *Existing;
    Copy.UniqueToken              = NextEffectToken++;
    Copy.Title += " Copy";
    Copy.OriginMeters.X += 0.65f;
    Copy.DeterministicSeed += 97u;
    const uint32_t NewToken = Copy.UniqueToken;
    Effects.push_back(std::move(Copy));
    RebuildEffectSimulation(NewToken);
    SelectionDomain = ParticleSelectionDomain::ParticleEffect;
    SelectedToken   = NewToken;
    IncrementRevision();
    return NewToken;
}

bool ParticleWorkspaceSession::RemoveEffect(uint32_t EffectToken) noexcept
{
    const auto It = std::find_if(
        Effects.begin(),
        Effects.end(),
        [EffectToken](const ParticleEffectDescriptor& Entry) noexcept { return Entry.UniqueToken == EffectToken; });
    if (It == Effects.end())
    {
        return false;
    }

    Effects.erase(It);
    LiveSimulations.erase(EffectToken);
    if (SelectionDomain == ParticleSelectionDomain::ParticleEffect && SelectedToken == EffectToken)
    {
        if (!Effects.empty())
        {
            SelectedToken = Effects.front().UniqueToken;
        }
        else
        {
            SelectionDomain = ParticleSelectionDomain::EnvironmentField;
            SelectedToken   = 0u;
        }
    }
    IncrementRevision();
    return true;
}

bool ParticleWorkspaceSession::ResetEffectPreset(uint32_t EffectToken)
{
    ParticleEffectDescriptor* Target = AccessEffect(EffectToken);
    if (Target == nullptr)
    {
        return false;
    }

    const ParticlePresetDescriptor* Preset = ParticlePresetRegistry::FindPreset(Target->PresetIdentifier);
    if (Preset == nullptr)
    {
        return false;
    }

    const std::string PreservedTitle = Target->Title;
    *Target                          = ConstructEffectDescriptor(*Preset, EffectToken);
    if (!PreservedTitle.empty())
    {
        Target->Title = PreservedTitle;
    }
    RebuildEffectSimulation(EffectToken);
    IncrementRevision();
    return true;
}

void ParticleWorkspaceSession::TriggerEffectBurst(uint32_t EffectToken) noexcept
{
    if (ParticleEffectDescriptor* Target = AccessEffect(EffectToken))
    {
        ++Target->BurstTriggerCounter;
        if (ParticleSimulation* Sim = AccessEffectSimulation(EffectToken))
        {
            SimulationTickInputs Inputs{};
            Inputs.DeltaTimeSeconds = 1.0f / std::max(1.0f, PlaybackClock.FixedStepHz);
            Inputs.TimeSeconds      = SimulationTimeSec;
            Inputs.ForceAccumulator = &ForceAccumulator;
            Inputs.ForceLattice     = &LatticeCascade;
            Sim->Tick(Inputs);
        }
        IncrementRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                            LIVE SIMULATION LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

void ParticleWorkspaceSession::RebuildEffectSimulation(uint32_t EffectToken)
{
    const ParticleEffectDescriptor* Target = FindEffect(EffectToken);
    if (Target == nullptr)
    {
        LiveSimulations.erase(EffectToken);
        return;
    }

    std::optional<ParticleSimulation> Compiled = ParticlePresetCompiler::CompileEffectDescriptor(*Target);
    if (!Compiled.has_value())
    {
        LiveSimulations.erase(EffectToken);
        return;
    }

    LiveSimulations[EffectToken] = std::make_unique<ParticleSimulation>(std::move(*Compiled));
}

void ParticleWorkspaceSession::RebuildAllSimulations()
{
    LiveSimulations.clear();
    for (const ParticleEffectDescriptor& Effect : Effects)
    {
        RebuildEffectSimulation(Effect.UniqueToken);
    }
}

void ParticleWorkspaceSession::AdvanceSimulationClock(double DeltaSeconds)
{
    AdvanceForceFieldsClock(DeltaSeconds);

    const float ClampedDt = static_cast<float>(std::clamp(DeltaSeconds, 0.0, 0.05));
    if (ClampedDt <= 0.0f)
    {
        return;
    }

    SimulationTickInputs Inputs{};
    Inputs.DeltaTimeSeconds = ClampedDt;
    Inputs.TimeSeconds      = SimulationTimeSec;
    Inputs.ForceAccumulator = &ForceAccumulator;
    Inputs.ForceLattice     = &LatticeCascade;

    for (const ParticleEffectDescriptor& Effect : Effects)
    {
        if (!Effect.Visible)
        {
            continue;
        }
        auto SimIt = LiveSimulations.find(Effect.UniqueToken);
        if (SimIt != LiveSimulations.end() && SimIt->second != nullptr)
        {
            SimIt->second->Tick(Inputs);
        }
    }
}

const ParticleSimulation* ParticleWorkspaceSession::QueryEffectSimulation(uint32_t EffectToken) const noexcept
{
    const auto It = LiveSimulations.find(EffectToken);
    return (It != LiveSimulations.end()) ? It->second.get() : nullptr;
}

ParticleSimulation* ParticleWorkspaceSession::AccessEffectSimulation(uint32_t EffectToken) noexcept
{
    const auto It = LiveSimulations.find(EffectToken);
    return (It != LiveSimulations.end()) ? It->second.get() : nullptr;
}

uint32_t ParticleWorkspaceSession::QueryTotalAliveParticles() const noexcept
{
    uint32_t Total = 0u;
    for (const ParticleEffectDescriptor& Effect : Effects)
    {
        if (!Effect.Visible)
        {
            continue;
        }
        if (const ParticleSimulation* Sim = QueryEffectSimulation(Effect.UniqueToken))
        {
            Total += Sim->QueryReadbackSummary().AliveCount;
        }
    }
    return Total;
}

float ParticleWorkspaceSession::QueryPeakParticleSpeedMs() const noexcept
{
    float Peak = 0.0f;
    for (const ParticleEffectDescriptor& Effect : Effects)
    {
        if (!Effect.Visible)
        {
            continue;
        }
        if (const ParticleSimulation* Sim = QueryEffectSimulation(Effect.UniqueToken))
        {
            Peak = std::max(Peak, Sim->QueryReadbackSummary().PeakSpeedMs);
        }
    }
    return Peak;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           REUSED C++ FORCEFIELDS LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

uint32_t ParticleWorkspaceSession::AddForceSource(ForceModel Model)
{
    ForceSourceRecordEntry Entry{};
    Entry.UniqueToken  = NextForceToken++;
    Entry.Model        = Model;
    Entry.Domain       = QueryDomainForModel(Model);
    Entry.Label        = QueryForceModelDisplayName(Model);
    Entry.Enabled      = true;
    Entry.Visible      = true;
    Entry.Strength     = 8.5f;
    Entry.RadiusMeters = 18.0f;
    Entry.CentreMeters = { 0.0f, 0.0f, 2.5f };

    if (Model == ForceModel::SplineForce)
    {
        AssignPresetMotionPath(Entry, MotionPathTopology::CatmullSpline);
    }
    else if (Model == ForceModel::FlowStream)
    {
        Entry.StreamProfile = FlowStreamProfile::SlipstreamJet;
    }

    const uint32_t Token = Entry.UniqueToken;
    ForceSources.push_back(std::move(Entry));
    SynchronizeForceFieldsAccumulator();
    SelectionDomain = ParticleSelectionDomain::ForceSource;
    SelectedToken   = Token;
    IncrementRevision();
    return Token;
}

bool ParticleWorkspaceSession::RemoveForceSource(uint32_t ForceToken) noexcept
{
    const auto It = std::find_if(
        ForceSources.begin(),
        ForceSources.end(),
        [ForceToken](const ForceSourceRecordEntry& Entry) noexcept { return Entry.UniqueToken == ForceToken; });
    if (It == ForceSources.end())
    {
        return false;
    }

    ForceSources.erase(It);
    SynchronizeForceFieldsAccumulator();
    if ((SelectionDomain == ParticleSelectionDomain::ForceSource || SelectionDomain == ParticleSelectionDomain::AmbientWind) &&
        SelectedToken == ForceToken)
    {
        SelectionDomain = ParticleSelectionDomain::EnvironmentField;
        SelectedToken   = 0u;
    }
    IncrementRevision();
    return true;
}

void ParticleWorkspaceSession::SynchronizeForceFieldsAccumulator()
{
    ForceAccumulator.AssignSources(ForceSources);
    ForceAccumulator.StepAnimation(SimulationTimeSec, false);
    LatticeCascade.EvaluateTierOnCpu(0u, ForceAccumulator, SimulationTimeSec);
}

void ParticleWorkspaceSession::AdvanceForceFieldsClock(double DeltaSeconds)
{
    const double ClampedDt = std::max(0.0, DeltaSeconds);
    if (ClampedDt > 0.0)
    {
        SimulationTimeSec += ClampedDt;
        const float Duration = std::max(0.5f, PlaybackClock.DurationSeconds);
        float       NextSec  = PlaybackClock.CurrentSeconds + static_cast<float>(ClampedDt);
        if (PlaybackClock.LoopEnabled && NextSec > Duration)
        {
            NextSec = std::fmod(NextSec, Duration);
        }
        else if (!PlaybackClock.LoopEnabled && NextSec >= Duration)
        {
            NextSec               = Duration;
            PlaybackClock.Playing = false;
        }
        PlaybackClock.CurrentSeconds = std::clamp(NextSec, 0.0f, Duration);
    }
    ForceAccumulator.AssignSources(ForceSources);
    ForceAccumulator.StepAnimation(SimulationTimeSec, false);
    LatticeCascade.EvaluateTierOnCpu(0u, ForceAccumulator, SimulationTimeSec);
    RecordAnemometerSample();
}

void ParticleWorkspaceSession::SeekPlaybackSeconds(float TargetSeconds)
{
    const float Duration         = std::max(0.5f, PlaybackClock.DurationSeconds);
    PlaybackClock.CurrentSeconds = std::clamp(TargetSeconds, 0.0f, Duration);
    SimulationTimeSec            = static_cast<double>(PlaybackClock.CurrentSeconds);
    ForceAccumulator.AssignSources(ForceSources);
    ForceAccumulator.StepAnimation(SimulationTimeSec, false);
    LatticeCascade.EvaluateTierOnCpu(0u, ForceAccumulator, SimulationTimeSec);
    RecordAnemometerSample();
    IncrementRevision();
}

void ParticleWorkspaceSession::ResetPlaybackClock()
{
    PlaybackClock.CurrentSeconds = 0.0f;
    SimulationTimeSec            = 0.0;
    AnemometerSampleCount        = 0u;
    AnemometerRing.fill(0.0f);
    RebuildAllSimulations();
    ForceAccumulator.AssignSources(ForceSources);
    ForceAccumulator.StepAnimation(0.0, false);
    LatticeCascade.EvaluateTierOnCpu(0u, ForceAccumulator, 0.0);
    RecordAnemometerSample();
    IncrementRevision();
}

void ParticleWorkspaceSession::ConfigureScrubbingPlayhead(bool Scrubbing) noexcept
{
    ScrubbingPlayhead = Scrubbing;
}

const ParticlePlaybackClock& ParticleWorkspaceSession::QueryPlaybackClock() const noexcept
{
    return PlaybackClock;
}

ParticlePlaybackClock& ParticleWorkspaceSession::AccessPlaybackClock() noexcept
{
    return PlaybackClock;
}

bool ParticleWorkspaceSession::QueryScrubbingPlayhead() const noexcept
{
    return ScrubbingPlayhead;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  .PARTICLESPACE DOCUMENT & REVISIONTRACKER UNDO / REDO
//------------------------------------------------------------------------------------------------------------------------

void ParticleWorkspaceSession::ResetRevisionBaseline(std::string_view BaselineLabel)
{
    RevisionTracker& Tracker = Document.AccessRevisionTracker();
    Tracker.Reset();
    const std::string Manifest = Codecs::ParticleEncoder::FormatManifestToml(ConstructSpaceDocument());
    const std::span<const std::byte> SnapshotBytes{
        reinterpret_cast<const std::byte*>(Manifest.data()),
        Manifest.size(),
    };
    const ParametricOperationParameters Params = ConstructRevisionParams(BaselineLabel, static_cast<uint32_t>(Effects.size()));
    (void)Tracker.RecordParametricOperation(Params, ++RevisionStampCounter, SnapshotBytes);
    Tracker.MarkSavedAtCurrentCursor();
}

void ParticleWorkspaceSession::CommitRevision(std::string_view OperationLabel)
{
    const std::string Manifest = Codecs::ParticleEncoder::FormatManifestToml(ConstructSpaceDocument());
    const std::span<const std::byte> SnapshotBytes{
        reinterpret_cast<const std::byte*>(Manifest.data()),
        Manifest.size(),
    };
    const ParametricOperationParameters Params = ConstructRevisionParams(OperationLabel, static_cast<uint32_t>(Effects.size()));
    (void)Document.AccessRevisionTracker().RecordParametricOperation(Params, ++RevisionStampCounter, SnapshotBytes);
    IncrementRevision();
}

bool ParticleWorkspaceSession::RestoreRevisionCursor()
{
    const RevisionTracker& Tracker   = Document.InspectRevisionTracker();
    const auto&            History   = Tracker.InspectHistory();
    const uint32_t         CursorIdx = History.QueryCursorIndex();
    const auto             Entries   = History.InspectEntries();
    if (CursorIdx >= Entries.size())
    {
        return false;
    }

    const uint32_t RecIdx  = Entries[CursorIdx].RecordEntryIndex;
    const auto&    Recorder = Tracker.InspectRecorder();
    const auto     Records  = Recorder.InspectRecordEntries();
    if (RecIdx >= Records.size())
    {
        return false;
    }

    const ParametricRecordEntry& Entry = Records[RecIdx];
    const auto RawBytes = Recorder.InspectContentRegion().InspectSpan(Entry.RegionByteOffset, Entry.RegionByteLength);
    if (RawBytes.empty())
    {
        return false;
    }

    const std::string_view ManifestView(reinterpret_cast<const char*>(RawBytes.data()), RawBytes.size());
    Codecs::ParticleSpaceDocument Restored{};
    std::string Diagnostic;
    if (!Codecs::ParticleDecoder::ParseManifestToml(ManifestView, Restored, Diagnostic))
    {
        return false;
    }
    return ApplySpaceDocument(Restored, false);
}

bool ParticleWorkspaceSession::UndoRevision()
{
    RevisionTracker& Tracker = Document.AccessRevisionTracker();
    if (!Tracker.Undo())
    {
        return false;
    }
    return RestoreRevisionCursor();
}

bool ParticleWorkspaceSession::RedoRevision()
{
    RevisionTracker& Tracker = Document.AccessRevisionTracker();
    if (!Tracker.Redo())
    {
        return false;
    }
    return RestoreRevisionCursor();
}

bool ParticleWorkspaceSession::QueryUndoAvailable() const noexcept
{
    const auto&    History   = Document.InspectRevisionTracker().InspectHistory();
    const uint32_t CursorIdx = History.QueryCursorIndex();
    const auto     Entries   = History.InspectEntries();
    if (CursorIdx >= Entries.size())
    {
        return false;
    }
    const uint32_t PrevIdx = Entries[CursorIdx].EnclosingIndex;
    if (PrevIdx == HistoryStepEntry::InvalidIndex || PrevIdx >= Entries.size() || PrevIdx == History.QueryRootIndex())
    {
        return false;
    }
    return Entries[PrevIdx].RecordEntryIndex != HistoryStepEntry::InvalidIndex;
}

bool ParticleWorkspaceSession::QueryRedoAvailable() const noexcept
{
    return Document.InspectRevisionTracker().QueryRedoAvailable();
}

bool ParticleWorkspaceSession::QueryModifiedSinceSave() const noexcept
{
    return Document.InspectRevisionTracker().QueryModifiedSinceSave();
}

Codecs::ParticleSpaceDocument ParticleWorkspaceSession::ConstructSpaceDocument() const
{
    Codecs::ParticleSpaceDocument SpaceDoc{};
    SpaceDoc.DocumentTitle           = Document.QueryTitle();
    SpaceDoc.CoordinateSystem        = "RightHandZUp";
    SpaceDoc.CascadeResolution       = WorkspaceConfig.InspectParticlePreferences().LatticeCascadeResolution;
    SpaceDoc.PlaybackClock           = PlaybackClock;
    SpaceDoc.Effects                 = Effects;
    SpaceDoc.ForceSources            = ForceSources;
    SpaceDoc.LinkedVectorFieldAssets = LinkedVectorFields;
    return SpaceDoc;
}

bool ParticleWorkspaceSession::ApplySpaceDocument(const Codecs::ParticleSpaceDocument& SpaceDoc, bool RecordBaseline)
{
    Effects            = SpaceDoc.Effects;
    ForceSources       = SpaceDoc.ForceSources;
    LinkedVectorFields = SpaceDoc.LinkedVectorFieldAssets;
    PlaybackClock      = SpaceDoc.PlaybackClock;
    SimulationTimeSec  = static_cast<double>(PlaybackClock.CurrentSeconds);

    NextEffectToken = 1u;
    for (ParticleEffectDescriptor& Effect : Effects)
    {
        if (Effect.UniqueToken == 0u)
        {
            Effect.UniqueToken = NextEffectToken++;
        }
        else
        {
            NextEffectToken = std::max(NextEffectToken, Effect.UniqueToken + 1u);
        }
    }

    NextForceToken = 1u;
    for (ForceSourceRecordEntry& Force : ForceSources)
    {
        if (Force.UniqueToken == 0u)
        {
            Force.UniqueToken = NextForceToken++;
        }
        else
        {
            NextForceToken = std::max(NextForceToken, Force.UniqueToken + 1u);
        }
    }

    if (!Effects.empty())
    {
        if (SelectionDomain != ParticleSelectionDomain::ParticleEffect || FindEffect(SelectedToken) == nullptr)
        {
            SelectionDomain = ParticleSelectionDomain::ParticleEffect;
            SelectedToken   = Effects.front().UniqueToken;
        }
    }
    else
    {
        SelectionDomain = ParticleSelectionDomain::EnvironmentField;
        SelectedToken   = 0u;
    }

    SynchronizeForceFieldsAccumulator();
    RebuildAllSimulations();
    IncrementRevision();

    if (RecordBaseline)
    {
        ResetRevisionBaseline("LoadParticleSpace");
    }
    return true;
}

bool ParticleWorkspaceSession::SaveParticleSpace(
    const std::filesystem::path& TargetPath,
    std::string&                 OutDiagnostic,
    bool                         EmbedRevisionHistory)
{
    const Codecs::ParticleSpaceDocument SpaceDoc = ConstructSpaceDocument();
    const RevisionTracker* OptionalRev = EmbedRevisionHistory ? &Document.InspectRevisionTracker() : nullptr;
    if (!Codecs::ParticleEncoder::EncodeFile(TargetPath, SpaceDoc, OutDiagnostic, OptionalRev))
    {
        return false;
    }
    Document.AssignCanonicalPath(TargetPath);
    Document.AccessRevisionTracker().MarkSavedAtCurrentCursor();
    return true;
}

bool ParticleWorkspaceSession::LoadParticleSpace(
    const std::filesystem::path& SourcePath,
    std::string&                 OutDiagnostic)
{
    Codecs::ParticleSpaceDocument Loaded{};
    RevisionTracker               ImportedRevision{};
    if (!Codecs::ParticleDecoder::DecodeFile(SourcePath, Loaded, OutDiagnostic, &ImportedRevision))
    {
        return false;
    }

    const bool HadEmbeddedRevision = ImportedRevision.QueryLiveStepCount() > 1u;
    if (!ApplySpaceDocument(Loaded, !HadEmbeddedRevision))
    {
        OutDiagnostic = "Failed to apply decoded .ParticleSpace document.";
        return false;
    }

    Document.AssignCanonicalPath(SourcePath);
    if (HadEmbeddedRevision)
    {
        Document.AccessRevisionTracker() = std::move(ImportedRevision);
        Document.AccessRevisionTracker().MarkSavedAtCurrentCursor();
    }
    return true;
}

bool ParticleWorkspaceSession::ImportVectorFieldAsset(
    const std::filesystem::path& SourcePath,
    std::string&                 OutDiagnostic)
{
    Codecs::VectorFieldSpaceDocument LoadedField{};
    if (!Codecs::VectorFieldDecoder::DecodeFromFile(SourcePath, LoadedField, OutDiagnostic))
    {
        return false;
    }

    for (ForceSourceRecordEntry& ImportedForce : LoadedField.ForceSources)
    {
        ImportedForce.UniqueToken = NextForceToken++;
        ForceSources.push_back(std::move(ImportedForce));
    }
    LinkedVectorFields.push_back(SourcePath.filename().string());
    SynchronizeForceFieldsAccumulator();
    CommitRevision("ImportVectorField");
    return true;
}

void ParticleWorkspaceSession::RecordAnemometerSample() noexcept
{
    const ForceDiagnosticsReading Diag = QueryAmbientWindDiagnostics();
    const SpatialVector3 Combined{
        Diag.TotalForce.AdvectionVelocity.X + Diag.TotalForce.LinearAcceleration.X * 0.25f,
        Diag.TotalForce.AdvectionVelocity.Y + Diag.TotalForce.LinearAcceleration.Y * 0.25f,
        Diag.TotalForce.AdvectionVelocity.Z + Diag.TotalForce.LinearAcceleration.Z * 0.25f,
    };
    const float WindSpeedMs = std::sqrt(Combined.X * Combined.X + Combined.Y * Combined.Y + Combined.Z * Combined.Z);
    if (AnemometerSampleCount < AnemometerSampleCapacity)
    {
        AnemometerRing[AnemometerSampleCount++] = WindSpeedMs;
    }
    else
    {
        for (std::size_t Idx = 1u; Idx < AnemometerSampleCapacity; ++Idx)
        {
            AnemometerRing[Idx - 1u] = AnemometerRing[Idx];
        }
        AnemometerRing[AnemometerSampleCapacity - 1u] = WindSpeedMs;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                               SELECTION & QUERIES
//------------------------------------------------------------------------------------------------------------------------

void ParticleWorkspaceSession::SelectEnvironmentField() noexcept
{
    SelectionDomain = ParticleSelectionDomain::EnvironmentField;
    SelectedToken   = 0u;
    IncrementRevision();
}

void ParticleWorkspaceSession::SelectAmbientWind() noexcept
{
    SelectionDomain = ParticleSelectionDomain::AmbientWind;
    SelectedToken   = ForceSources.empty() ? 0u : ForceSources.front().UniqueToken;
    IncrementRevision();
}

void ParticleWorkspaceSession::SelectForceSource(uint32_t ForceToken) noexcept
{
    SelectionDomain = ParticleSelectionDomain::ForceSource;
    SelectedToken   = ForceToken;
    IncrementRevision();
}

void ParticleWorkspaceSession::SelectParticleEffect(uint32_t EffectToken) noexcept
{
    SelectionDomain = ParticleSelectionDomain::ParticleEffect;
    SelectedToken   = EffectToken;
    IncrementRevision();
}

void ParticleWorkspaceSession::IncrementRevision() noexcept
{
    ++AuthoringRevision;
}

ParticleSelectionDomain ParticleWorkspaceSession::QuerySelectionDomain() const noexcept { return SelectionDomain; }
uint32_t                ParticleWorkspaceSession::QuerySelectedToken() const noexcept   { return SelectedToken; }
uint64_t                ParticleWorkspaceSession::QueryRevision() const noexcept        { return AuthoringRevision; }
double                  ParticleWorkspaceSession::QuerySimulationTimeSec() const noexcept { return SimulationTimeSec; }

std::span<const ParticleEffectDescriptor> ParticleWorkspaceSession::QueryEffects() const noexcept { return Effects; }
std::span<ParticleEffectDescriptor>       ParticleWorkspaceSession::AccessEffects() noexcept      { return Effects; }

const ParticleEffectDescriptor* ParticleWorkspaceSession::FindEffect(uint32_t EffectToken) const noexcept
{
    for (const ParticleEffectDescriptor& Entry : Effects)
    {
        if (Entry.UniqueToken == EffectToken)
        {
            return &Entry;
        }
    }
    return nullptr;
}

ParticleEffectDescriptor* ParticleWorkspaceSession::AccessEffect(uint32_t EffectToken) noexcept
{
    for (ParticleEffectDescriptor& Entry : Effects)
    {
        if (Entry.UniqueToken == EffectToken)
        {
            return &Entry;
        }
    }
    return nullptr;
}

ParticleEffectDescriptor* ParticleWorkspaceSession::AccessSelectedEffect() noexcept
{
    if (SelectionDomain != ParticleSelectionDomain::ParticleEffect)
    {
        return nullptr;
    }
    return AccessEffect(SelectedToken);
}

const ParticleEffectDescriptor* ParticleWorkspaceSession::QuerySelectedEffect() const noexcept
{
    if (SelectionDomain != ParticleSelectionDomain::ParticleEffect)
    {
        return nullptr;
    }
    return FindEffect(SelectedToken);
}

std::span<const ForceSourceRecordEntry> ParticleWorkspaceSession::QueryForceSources() const noexcept { return ForceSources; }
std::span<ForceSourceRecordEntry>       ParticleWorkspaceSession::AccessForceSources() noexcept      { return ForceSources; }

ForceSourceRecordEntry* ParticleWorkspaceSession::FindForceSource(uint32_t ForceToken) noexcept
{
    for (ForceSourceRecordEntry& Entry : ForceSources)
    {
        if (Entry.UniqueToken == ForceToken)
        {
            return &Entry;
        }
    }
    return nullptr;
}

const ForceSourceRecordEntry* ParticleWorkspaceSession::FindForceSource(uint32_t ForceToken) const noexcept
{
    for (const ForceSourceRecordEntry& Entry : ForceSources)
    {
        if (Entry.UniqueToken == ForceToken)
        {
            return &Entry;
        }
    }
    return nullptr;
}

ForceSourceRecordEntry* ParticleWorkspaceSession::AccessActiveForceInspectorSource() noexcept
{
    if (ForceSources.empty())
    {
        return nullptr;
    }
    if (SelectionDomain == ParticleSelectionDomain::ForceSource || SelectionDomain == ParticleSelectionDomain::AmbientWind)
    {
        if (ForceSourceRecordEntry* Found = FindForceSource(SelectedToken))
        {
            return Found;
        }
    }
    return &ForceSources.front();
}

ForceFieldAccumulator&       ParticleWorkspaceSession::AccessForceAccumulator() noexcept       { return ForceAccumulator; }
const ForceFieldAccumulator& ParticleWorkspaceSession::QueryForceAccumulator() const noexcept  { return ForceAccumulator; }
FieldLatticeCascade&         ParticleWorkspaceSession::AccessLatticeCascade() noexcept         { return LatticeCascade; }
ForceOverlaySettings&        ParticleWorkspaceSession::AccessForceOverlay() noexcept           { return ForceOverlay; }
const ForceOverlaySettings&  ParticleWorkspaceSession::QueryForceOverlay() const noexcept      { return ForceOverlay; }

std::span<const float> ParticleWorkspaceSession::QueryAnemometerSamples() const noexcept
{
    return std::span<const float>(AnemometerRing.data(), AnemometerSampleCount);
}

int32_t* ParticleWorkspaceSession::AccessSelectedWaypointIndex() noexcept { return &SelectedWaypoint; }

WorkspaceDocument&                          ParticleWorkspaceSession::AccessDocument() noexcept                     { return Document; }
const WorkspaceDocument&                    ParticleWorkspaceSession::InspectDocument() const noexcept              { return Document; }
Configuration::WorkspaceConfiguration&      ParticleWorkspaceSession::AccessWorkspaceConfiguration() noexcept       { return WorkspaceConfig; }
const Configuration::WorkspaceConfiguration& ParticleWorkspaceSession::InspectWorkspaceConfiguration() const noexcept { return WorkspaceConfig; }

uint32_t ParticleWorkspaceSession::QueryVisibleCount() const noexcept
{
    uint32_t Count = 0u;
    for (const ForceSourceRecordEntry& Source : ForceSources)
    {
        if (Source.Enabled && Source.Visible) { ++Count; }
    }
    for (const ParticleEffectDescriptor& Effect : Effects)
    {
        if (Effect.Visible) { ++Count; }
    }
    return Count;
}

uint32_t ParticleWorkspaceSession::QueryHiddenCount() const noexcept
{
    return QueryTotalCount() - QueryVisibleCount();
}

uint32_t ParticleWorkspaceSession::QueryTotalCount() const noexcept
{
    return static_cast<uint32_t>(ForceSources.size() + Effects.size());
}

ForceDiagnosticsReading ParticleWorkspaceSession::QueryAmbientWindDiagnostics() const noexcept
{
    return ForceAccumulator.QueryAnemometerDiagnostics(ForceOverlay.AnemometerWorldMeters, SimulationTimeSec);
}

SpatialForceSample ParticleWorkspaceSession::SampleCoupledForce(const SpatialVector3& WorldPoint) const noexcept
{
    return ForceAccumulator.QueryForceFieldSample(WorldPoint, { 0.0f, 0.0f, 0.0f }, SimulationTimeSec);
}

std::string ParticleWorkspaceSession::SerializeSelectedEffectTomlPreview() const
{
    const ParticleEffectDescriptor* Effect = QuerySelectedEffect();
    if (Effect == nullptr)
    {
        return "# Select a Particle Effect in the Outliner to inspect its .ParticleSpace descriptor.\n";
    }

    char Buf[160]{};
    std::ostringstream Out;
    Out << "[ParticleSpace]\n";
    Out << "SchemaVersion = 1\n";
    Out << "PresetSlug = \"" << Effect->PresetIdentifier << "\"\n";
    Out << "Title = \"" << Effect->Title << "\"\n";
    Out << "Category = \"" << ParticlePresetRegistry::QueryCategoryDescriptor(Effect->Category).FullTitle << "\"\n";
    Out << "Silhouette = \"" << ParticlePresetRegistry::QuerySilhouetteLabel(Effect->Silhouette) << "\"\n";
    Out << "Blend = \"" << (Effect->BlendCategory == ParticleBlendCategory::AdditiveGlow ? "AdditiveGlow" : "PremultipliedAlpha") << "\"\n\n";

    Out << "[ReleaseVolume]\n";
    Out << "Shape = \"" << ParticlePresetRegistry::QueryReleaseVolumeLabel(Effect->ReleaseVolume) << "\"\n";
    std::snprintf(Buf, sizeof(Buf), "OriginMeters = [%.2f, %.2f, %.2f]\n", Effect->OriginMeters.X, Effect->OriginMeters.Y, Effect->OriginMeters.Z);
    Out << Buf;
    std::snprintf(Buf, sizeof(Buf), "RadiusMeters = %.2f\n", Effect->RadiusMeters);
    Out << Buf;
    Out << "Capacity = " << Effect->Capacity << "\n";
    std::snprintf(Buf, sizeof(Buf), "BirthRatePerSec = %.1f\n\n", Effect->BirthRatePerSec);
    Out << Buf;

    Out << "[ForceFieldsCoupling]\n";
    Out << "EngineDomain = \"SimulationSpace::ForceFields\"\n";
    std::snprintf(Buf, sizeof(Buf), "CouplingGain = %.2f\n", Effect->ForceCouplingGain);
    Out << Buf;
    Out << "ActiveForceSources = " << ForceSources.size() << "\n";
    Out << "CascadeResolution = " << WorkspaceConfig.InspectParticlePreferences().LatticeCascadeResolution << "\n";
    return Out.str();
}

} // namespace Frontier::Workspaces
