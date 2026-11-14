/*============================================================================================================================================
                                                      PARTICLEWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleWorkspaceSession directly coupling Frontier's C++ ForceFields accumulator, cascade, and 93 active presets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace Frontier::Workspaces
{

using namespace SimulationSpace::ForceFields;

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

    // Reused C++ ForceFields environment sources (Ambient Wind + Curl Swirl + Helical Column + Attractor)
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
    const uint32_t FerroToken     = AddEffectFromPreset("ferrofluid-spikes");
    (void)AddEffectFromPreset("fireworks-spectacle");
    (void)AddEffectFromPreset("curl-fur-field");
    (void)AddEffectFromPreset("interactive-tree");
    (void)AddEffectFromPreset("cracked-ice-sheet");
    (void)AddEffectFromPreset("effector-grid-cubes");
    (void)AddEffectFromPreset("energy-shield-hex");
    (void)AddEffectFromPreset("watermist");

    SynchronizeForceFieldsAccumulator();

    // Prime the Beaufort Anemometer rolling trace from real ForceFieldAccumulator evaluations
    for (uint32_t StepIndex = 0u; StepIndex < 36u; ++StepIndex)
    {
        AdvanceForceFieldsClock(0.125);
    }

    SelectParticleEffect(FerroToken);
    AuthoringRevision = 1u;
}

void ParticleWorkspaceSession::ClearAllEntries()
{
    Effects.clear();
    ForceSources.clear();
    ForceOutlinerMirror.ClearAll();
    AnemometerRing.fill(0.0f);
    AnemometerSampleCount = 0u;
    SelectedWaypoint      = -1;
    SelectionDomain       = ParticleSelectionDomain::EnvironmentField;
    SelectedToken         = 0u;
    NextEffectToken       = 1u;
    NextForceToken        = 1u;
    SimulationTimeSec     = 0.0;
    SynchronizeForceFieldsAccumulator();
    IncrementRevision();
}

//------------------------------------------------------------------------------------------------------------------------
//                                            PARTICLE EFFECT OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

ParticleEffectRecordEntry ParticleWorkspaceSession::ConstructEffectFromDescriptor(
    const ParticlePresetDescriptor& Descriptor,
    uint32_t                        AssignedToken) const
{
    ParticleEffectRecordEntry Entry{};
    Entry.UniqueToken          = AssignedToken;
    Entry.PresetIdentifier     = Descriptor.Identifier;
    Entry.Title                = Descriptor.DisplayName;
    Entry.Sublabel             = Descriptor.Sublabel;
    Entry.Category             = Descriptor.Category;
    Entry.AuthoringDomain      = Descriptor.AuthoringDomain;
    Entry.Visible              = true;

    Entry.ReleaseVolume        = Descriptor.ReleaseVolume;
    Entry.OriginMeters         = { 0.0f, 0.0f, 1.2f };
    Entry.LaunchDirection      = { 0.0f, 0.0f, 1.0f };
    Entry.RadiusMeters         = Descriptor.RadiusMeters;
    Entry.ConeSpreadRad        = 0.55f;
    Entry.Capacity             = Descriptor.Capacity;
    Entry.BirthRatePerSec      = Descriptor.BirthRatePerSec;
    Entry.SpeedMinMs           = Descriptor.SpeedMinMs;
    Entry.SpeedMaxMs           = Descriptor.SpeedMaxMs;
    Entry.LifeMinSec           = std::min(Descriptor.LifeMinSec, 20.0f);
    Entry.LifeMaxSec           = std::min(Descriptor.LifeMaxSec, 20.0f);

    Entry.Silhouette           = Descriptor.Silhouette;
    Entry.BlendCategory        = Descriptor.BlendCategory;
    Entry.SizeStartMeters      = Descriptor.SizeStartMeters;
    Entry.SizeEndMeters        = Descriptor.SizeEndMeters;
    Entry.SizeMultiplier       = 1.0f;
    Entry.ColorA               = Descriptor.ColorA;
    Entry.ColorB               = Descriptor.ColorB;
    Entry.ColorC               = Descriptor.ColorC;
    Entry.ColorBy              = ParticleColorByCategory::LifetimeRatio;
    Entry.RampWrap             = ParticleRampWrapCategory::Clamp;
    Entry.RampInterp           = ParticleRampInterpCategory::Smooth;
    Entry.RampPhaseOffset      = 0.0f;

    Entry.GravityMultiplier    = Descriptor.GravityMultiplier;
    Entry.DragCoefficient      = Descriptor.DragCoefficient;
    Entry.ForceCouplingGain    = 1.0f;
    Entry.CurlAmplitude        = (Descriptor.Category == ParticleEffectCategory::SilkAndCurl) ? 1.85f : 0.45f;
    Entry.CurlFrequency        = (Descriptor.Category == ParticleEffectCategory::SilkAndCurl) ? 1.25f : 0.85f;
    Entry.FloorRestitution     = 0.35f;
    Entry.SpinRateRadSec       = 1.2f;
    Entry.BallInteractionEnabled = true;
    Entry.RibbonTrailsEnabled  = (Descriptor.Category == ParticleEffectCategory::SilkAndCurl);

    Entry.RiggingArchitecture  = Descriptor.RiggingArchitecture;
    Entry.FerrofluidVessel     = Descriptor.FerrofluidVessel;
    Entry.FracturePattern      = Descriptor.FracturePattern;
    Entry.EffectorArchitecture = Descriptor.EffectorArchitecture;
    Entry.FireworkPattern      = static_cast<FireworkPatternCategory>(
        Descriptor.FireworkPattern < static_cast<uint8_t>(FireworkPatternCategory::Count)
            ? Descriptor.FireworkPattern
            : 0u);

    if (Entry.AuthoringDomain == ParticleAuthoringDomain::FireworksSpectacle)
    {
        Entry.BurstIntervalSec = 1.6f;
        Entry.BurstCount       = 140u;
        Entry.BurstAtOrigin    = false;
        Entry.FireworkSkyText  = "2026";
    }
    else if (Entry.AuthoringDomain == ParticleAuthoringDomain::FerrofluidFlux)
    {
        Entry.FerroSpikeCount      = (Descriptor.FerrofluidVessel == 2u) ? 240u : 192u;
        Entry.MagnetOrbitSpeedRadS = 1.15f;
        Entry.MaxSpikeHeightMeters = 0.92f;
    }
    else if (Entry.AuthoringDomain == ParticleAuthoringDomain::RiggingAndFlora)
    {
        Entry.StrandRows         = 18u;
        Entry.CurveSegments      = 12u;
        Entry.StrandHeightMeters = 2.4f;
        Entry.CatenarySagMeters  = 0.28f;
        Entry.ElasticStiffness   = 1.15f;
        Entry.WaveSpeedHz        = 1.65f;
        Entry.TearSpeedMs        = 9.5f;
    }
    else if (Entry.AuthoringDomain == ParticleAuthoringDomain::EnergyShield)
    {
        Entry.ShieldGeometry    = (std::string_view(Descriptor.Identifier) == "energy-shield-prism") ? 2u : 1u;
        Entry.ShieldRippleSpeed = 2.4f;
        Entry.ShieldCellDensity = 16.0f;
    }

    Entry.DeterministicSeed = 2026u + AssignedToken * 37u;
    return Entry;
}

uint32_t ParticleWorkspaceSession::AddEffectFromPreset(std::string_view PresetIdentifier)
{
    // Strictly refuse any retired/quarantined slug absent from ParticlePresetRegistry
    const ParticlePresetDescriptor* Descriptor = ParticlePresetRegistry::FindPresetByIdentifier(PresetIdentifier);
    if (Descriptor == nullptr)
    {
        return 0u;
    }

    const uint32_t AssignedToken = NextEffectToken++;
    ParticleEffectRecordEntry Entry = ConstructEffectFromDescriptor(*Descriptor, AssignedToken);

    // Ensure unique display title when adding duplicate preset instances
    uint32_t Suffix = 1u;
    bool     Collision = true;
    while (Collision)
    {
        Collision = false;
        for (const ParticleEffectRecordEntry& Existing : Effects)
        {
            if (Existing.Title == Entry.Title)
            {
                Collision = true;
                ++Suffix;
                Entry.Title = std::string(Descriptor->DisplayName) + " " + std::to_string(Suffix);
                break;
            }
        }
    }

    Effects.push_back(std::move(Entry));
    SelectParticleEffect(AssignedToken);
    IncrementRevision();
    return AssignedToken;
}

uint32_t ParticleWorkspaceSession::DuplicateEffect(uint32_t EffectToken)
{
    const ParticleEffectRecordEntry* Source = FindEffect(EffectToken);
    if (Source == nullptr)
    {
        return 0u;
    }

    ParticleEffectRecordEntry Copy = *Source;
    Copy.UniqueToken               = NextEffectToken++;
    Copy.Title                     = Source->Title + " Copy";
    Copy.DeterministicSeed         = Source->DeterministicSeed + 101u;
    const uint32_t AssignedToken   = Copy.UniqueToken;
    Effects.push_back(std::move(Copy));
    SelectParticleEffect(AssignedToken);
    IncrementRevision();
    return AssignedToken;
}

bool ParticleWorkspaceSession::RemoveEffect(uint32_t EffectToken) noexcept
{
    const auto It = std::find_if(
        Effects.begin(),
        Effects.end(),
        [EffectToken](const ParticleEffectRecordEntry& Entry) { return Entry.UniqueToken == EffectToken; });
    if (It == Effects.end())
    {
        return false;
    }

    Effects.erase(It);
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

bool ParticleWorkspaceSession::ResetEffectToPreset(uint32_t EffectToken)
{
    ParticleEffectRecordEntry* Target = AccessEffect(EffectToken);
    if (Target == nullptr)
    {
        return false;
    }
    const ParticlePresetDescriptor* Descriptor = ParticlePresetRegistry::FindPresetByIdentifier(Target->PresetIdentifier);
    if (Descriptor == nullptr)
    {
        return false;
    }

    const std::string PreservedTitle = Target->Title;
    *Target                          = ConstructEffectFromDescriptor(*Descriptor, EffectToken);
    Target->Title                    = PreservedTitle;
    IncrementRevision();
    return true;
}

void ParticleWorkspaceSession::TriggerEffectBurst(uint32_t EffectToken) noexcept
{
    if (ParticleEffectRecordEntry* Target = AccessEffect(EffectToken))
    {
        ++Target->BurstTriggerCounter;
        IncrementRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                       REUSED C++ FORCEFIELDS OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

uint32_t ParticleWorkspaceSession::AddForceSource(ForceModel Model)
{
    ForceSourceRecordEntry Entry{};
    Entry.UniqueToken  = NextForceToken++;
    Entry.Model        = Model;
    Entry.Domain       = QueryDomainForModel(Model);
    Entry.Label        = QueryForceModelDisplayName(Model);
    Entry.CentreMeters = { 0.0f, 0.0f, 3.0f };
    Entry.RadiusMeters = 24.0f;
    Entry.Strength     = 12.0f;
    Entry.SwirlRatio   = 0.6f;
    Entry.Enabled      = true;
    Entry.Visible      = true;

    if (Model == ForceModel::SplineForce)
    {
        AssignPresetMotionPath(Entry, MotionPathTopology::CatmullSpline);
        Entry.DynamicMotion = true;
    }
    else if (Model == ForceModel::FlowStream)
    {
        Entry.StreamProfile = FlowStreamProfile::ThermalUpdraft;
    }

    const uint32_t AssignedToken = Entry.UniqueToken;
    ForceSources.push_back(std::move(Entry));
    SynchronizeForceFieldsAccumulator();
    SelectForceSource(AssignedToken);
    IncrementRevision();
    return AssignedToken;
}

bool ParticleWorkspaceSession::RemoveForceSource(uint32_t ForceToken) noexcept
{
    const auto It = std::find_if(
        ForceSources.begin(),
        ForceSources.end(),
        [ForceToken](const ForceSourceRecordEntry& Entry) { return Entry.UniqueToken == ForceToken; });
    if (It == ForceSources.end())
    {
        return false;
    }

    ForceSources.erase(It);
    SynchronizeForceFieldsAccumulator();
    if (SelectionDomain == ParticleSelectionDomain::ForceSource && SelectedToken == ForceToken)
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
    ForceAccumulator.StepAnimation(SimulationTimeSec, true);
    SyncForceOutlinerMirror();
}

void ParticleWorkspaceSession::AdvanceForceFieldsClock(double DeltaSeconds)
{
    if (DeltaSeconds > 0.0 && PlaybackClock.Playing && !ScrubbingPlayhead)
    {
        SimulationTimeSec += DeltaSeconds * static_cast<double>(PlaybackClock.SpeedMultiplier);
        const double Duration = std::max(0.1, static_cast<double>(PlaybackClock.LoopDurationSeconds));
        PlaybackClock.ElapsedSeconds = static_cast<float>(std::fmod(SimulationTimeSec, Duration));
    }
    ForceAccumulator.AssignSources(ForceSources);
    ForceAccumulator.StepAnimation(SimulationTimeSec, true);
    RecordAnemometerSample();
}

void ParticleWorkspaceSession::ApplyPlaybackInteraction(const PlaybackScrubber::PlaybackScrubberInteraction& Interaction)
{
    ScrubbingPlayhead = Interaction.DraggingPlayhead;

    if (Interaction.ResetTriggered)
    {
        ResetPlaybackClock();
    }
    if (Interaction.PlayPauseToggled)
    {
        PlaybackClock.Playing = !PlaybackClock.Playing;
    }
    if (Interaction.StepForwardTriggered)
    {
        PlaybackClock.Playing = false;
        const float StepSec   = 1.0f / std::max(PlaybackClock.FrameRateHz, 1.0f);
        SeekPlaybackSeconds(PlaybackClock.ElapsedSeconds + StepSec);
    }
    if (Interaction.SpeedCycled)
    {
        PlaybackClock.SpeedMultiplier = Interaction.UpdatedSpeedMultiplier;
    }
    if (Interaction.Scrubbed)
    {
        SeekPlaybackSeconds(Interaction.UpdatedSeconds);
    }
    if (Interaction.ActionTriggered)
    {
        if (ParticleEffectRecordEntry* Selected = AccessSelectedEffect())
        {
            TriggerEffectBurst(Selected->UniqueToken);
        }
        else if (!Effects.empty())
        {
            TriggerEffectBurst(Effects.front().UniqueToken);
        }
    }
}

void ParticleWorkspaceSession::SeekPlaybackSeconds(float TargetSeconds)
{
    const float Duration         = std::max(PlaybackClock.LoopDurationSeconds, 0.1f);
    const float Clamped          = std::clamp(TargetSeconds, 0.0f, Duration);
    PlaybackClock.ElapsedSeconds = Clamped;
    SimulationTimeSec            = static_cast<double>(Clamped);
    ForceAccumulator.AssignSources(ForceSources);
    ForceAccumulator.StepAnimation(SimulationTimeSec, true);
    RecordAnemometerSample();
}

void ParticleWorkspaceSession::ResetPlaybackClock()
{
    PlaybackClock.ElapsedSeconds = 0.0f;
    SimulationTimeSec            = 0.0;
    ForceAccumulator.AssignSources(ForceSources);
    ForceAccumulator.StepAnimation(0.0, true);
    RecordAnemometerSample();
}

const PlaybackScrubber::PlaybackClockParameters& ParticleWorkspaceSession::QueryPlaybackClock() const noexcept
{
    return PlaybackClock;
}

PlaybackScrubber::PlaybackClockParameters& ParticleWorkspaceSession::AccessPlaybackClock() noexcept
{
    return PlaybackClock;
}

bool ParticleWorkspaceSession::QueryScrubbingPlayhead() const noexcept
{
    return ScrubbingPlayhead;
}

void ParticleWorkspaceSession::SyncForceOutlinerMirror()
{
    ForceOutlinerMirror.ClearAll();
    OutlinerConstructTemplate ForceTemplate{};
    ForceTemplate.DropdownLabel = "Add Force";
    ForceTemplate.DefaultTitle  = "Force";
    ForceTemplate.CategoryTitle = "Force Field";
    ForceTemplate.Category      = ConstructSlotCategory::ForceField;
    ForceOutlinerMirror.ConfigureConstructTemplates(std::span<const OutlinerConstructTemplate>(&ForceTemplate, 1u));

    for (ForceSourceRecordEntry& Source : ForceSources)
    {
        const OutlinerSlotToken Slot = ForceOutlinerMirror.ConstructFromTemplate(0u);
        Source.OutlinerTokenId       = Slot.Identifier;
        if (OutlinerRecordEntry* Row = ForceOutlinerMirror.AccessEntry(Slot))
        {
            Row->Title                = Source.Label;
            Row->CategoryLabel        = QueryForceModelDisplayName(Source.Model);
            Row->PositionX            = Source.CentreMeters.X;
            Row->PositionY            = Source.CentreMeters.Y;
            Row->PositionZ            = Source.CentreMeters.Z;
            Row->RotationX            = Source.EulerDegrees.X;
            Row->RotationY            = Source.EulerDegrees.Y;
            Row->RotationZ            = Source.EulerDegrees.Z;
            Row->VisibilityEnabled    = Source.Visible;
            Row->Locked               = Source.Locked;
            Row->DynamicMotion        = Source.DynamicMotion;
            Row->ForceLawOrdinal      = static_cast<uint32_t>(Source.Model);
            Row->ForceFormulaOverride = Source.FormulaEnabled;
        }
    }
}

void ParticleWorkspaceSession::RecordAnemometerSample() noexcept
{
    const ForceDiagnosticsReading Reading = ForceAccumulator.QueryAnemometerDiagnostics(
        ForceOverlay.AnemometerWorldMeters,
        SimulationTimeSec);
    const float WindSpeed = VectorLength(Reading.TotalForce.AdvectionVelocity);

    if (AnemometerSampleCount < AnemometerSampleCapacity)
    {
        AnemometerRing[AnemometerSampleCount++] = WindSpeed;
    }
    else
    {
        for (std::size_t Index = 1u; Index < AnemometerSampleCapacity; ++Index)
        {
            AnemometerRing[Index - 1u] = AnemometerRing[Index];
        }
        AnemometerRing[AnemometerSampleCapacity - 1u] = WindSpeed;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SELECTION & QUERY ACCESSORS
//------------------------------------------------------------------------------------------------------------------------

void ParticleWorkspaceSession::SelectEnvironmentField() noexcept
{
    SelectionDomain = ParticleSelectionDomain::EnvironmentField;
    SelectedToken   = 0u;
}

void ParticleWorkspaceSession::SelectAmbientWind() noexcept
{
    SelectionDomain = ParticleSelectionDomain::AmbientWind;
    SelectedToken   = !ForceSources.empty() ? ForceSources.front().UniqueToken : 0u;
}

void ParticleWorkspaceSession::SelectForceSource(uint32_t ForceToken) noexcept
{
    SelectionDomain = ParticleSelectionDomain::ForceSource;
    SelectedToken   = ForceToken;
}

void ParticleWorkspaceSession::SelectParticleEffect(uint32_t EffectToken) noexcept
{
    SelectionDomain = ParticleSelectionDomain::ParticleEffect;
    SelectedToken   = EffectToken;
}

void ParticleWorkspaceSession::IncrementRevision() noexcept
{
    ++AuthoringRevision;
}

ParticleSelectionDomain ParticleWorkspaceSession::QuerySelectionDomain() const noexcept
{
    return SelectionDomain;
}

uint32_t ParticleWorkspaceSession::QuerySelectedToken() const noexcept
{
    return SelectedToken;
}

uint64_t ParticleWorkspaceSession::QueryRevision() const noexcept
{
    return AuthoringRevision;
}

double ParticleWorkspaceSession::QuerySimulationTimeSec() const noexcept
{
    return SimulationTimeSec;
}

std::span<const ParticleEffectRecordEntry> ParticleWorkspaceSession::QueryEffects() const noexcept
{
    return Effects;
}

std::span<ParticleEffectRecordEntry> ParticleWorkspaceSession::AccessEffects() noexcept
{
    return Effects;
}

const ParticleEffectRecordEntry* ParticleWorkspaceSession::FindEffect(uint32_t EffectToken) const noexcept
{
    for (const ParticleEffectRecordEntry& Entry : Effects)
    {
        if (Entry.UniqueToken == EffectToken)
        {
            return &Entry;
        }
    }
    return nullptr;
}

ParticleEffectRecordEntry* ParticleWorkspaceSession::AccessEffect(uint32_t EffectToken) noexcept
{
    for (ParticleEffectRecordEntry& Entry : Effects)
    {
        if (Entry.UniqueToken == EffectToken)
        {
            return &Entry;
        }
    }
    return nullptr;
}

ParticleEffectRecordEntry* ParticleWorkspaceSession::AccessSelectedEffect() noexcept
{
    if (SelectionDomain != ParticleSelectionDomain::ParticleEffect)
    {
        return nullptr;
    }
    return AccessEffect(SelectedToken);
}

const ParticleEffectRecordEntry* ParticleWorkspaceSession::QuerySelectedEffect() const noexcept
{
    if (SelectionDomain != ParticleSelectionDomain::ParticleEffect)
    {
        return nullptr;
    }
    return FindEffect(SelectedToken);
}

std::span<const ForceSourceRecordEntry> ParticleWorkspaceSession::QueryForceSources() const noexcept
{
    return ForceSources;
}

std::span<ForceSourceRecordEntry> ParticleWorkspaceSession::AccessForceSources() noexcept
{
    return ForceSources;
}

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

ForceSourceRecordEntry* ParticleWorkspaceSession::AccessActiveForceForInspector() noexcept
{
    if (SelectionDomain == ParticleSelectionDomain::ForceSource)
    {
        if (ForceSourceRecordEntry* Matched = FindForceSource(SelectedToken))
        {
            return Matched;
        }
    }
    if (!ForceSources.empty())
    {
        return &ForceSources.front();
    }
    return nullptr;
}

ForceFieldAccumulator& ParticleWorkspaceSession::AccessForceAccumulator() noexcept
{
    return ForceAccumulator;
}

const ForceFieldAccumulator& ParticleWorkspaceSession::QueryForceAccumulator() const noexcept
{
    return ForceAccumulator;
}

FieldLatticeCascade& ParticleWorkspaceSession::AccessLatticeCascade() noexcept
{
    return LatticeCascade;
}

ForceOverlaySettings& ParticleWorkspaceSession::AccessForceOverlay() noexcept
{
    return ForceOverlay;
}

const ForceOverlaySettings& ParticleWorkspaceSession::QueryForceOverlay() const noexcept
{
    return ForceOverlay;
}

OutlinerPanel& ParticleWorkspaceSession::AccessForceOutlinerMirror() noexcept
{
    return ForceOutlinerMirror;
}

OutlinerRecordEntry* ParticleWorkspaceSession::AccessActiveForceOutlinerEntry() noexcept
{
    ForceSourceRecordEntry* ActiveForce = AccessActiveForceForInspector();
    if (ActiveForce == nullptr || ActiveForce->OutlinerTokenId == 0u)
    {
        return nullptr;
    }
    return ForceOutlinerMirror.AccessEntry(OutlinerSlotToken{ ActiveForce->OutlinerTokenId });
}

std::span<const float> ParticleWorkspaceSession::QueryAnemometerSamples() const noexcept
{
    return std::span<const float>(AnemometerRing.data(), AnemometerSampleCount);
}

int32_t* ParticleWorkspaceSession::AccessSelectedWaypointIndex() noexcept
{
    return &SelectedWaypoint;
}

uint32_t ParticleWorkspaceSession::QueryVisibleRecordCount() const noexcept
{
    uint32_t Count = 0u;
    for (const ParticleEffectRecordEntry& Effect : Effects)
    {
        if (Effect.Visible)
        {
            ++Count;
        }
    }
    for (const ForceSourceRecordEntry& Force : ForceSources)
    {
        if (Force.Enabled && Force.Visible)
        {
            ++Count;
        }
    }
    return Count;
}

uint32_t ParticleWorkspaceSession::QueryHiddenRecordCount() const noexcept
{
    const uint32_t Total   = static_cast<uint32_t>(Effects.size() + ForceSources.size());
    const uint32_t Visible = QueryVisibleRecordCount();
    return Total >= Visible ? (Total - Visible) : 0u;
}

uint32_t ParticleWorkspaceSession::QueryTotalRecordCount() const noexcept
{
    return static_cast<uint32_t>(Effects.size() + ForceSources.size() + 2u);
}

ForceDiagnosticsReading ParticleWorkspaceSession::QueryAmbientWindDiagnostics() const noexcept
{
    return ForceAccumulator.QueryAnemometerDiagnostics(ForceOverlay.AnemometerWorldMeters, SimulationTimeSec);
}

SpatialForceSample ParticleWorkspaceSession::SampleCoupledForceAt(const SpatialVector3& WorldPoint) const noexcept
{
    return ForceAccumulator.QueryForceFieldSample(WorldPoint, {}, SimulationTimeSec);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      .PARTICLESPACE TOML PREVIEW SERIALIZER
//------------------------------------------------------------------------------------------------------------------------

std::string ParticleWorkspaceSession::SerializeSelectedEffectTomlPreview() const
{
    const ParticleEffectRecordEntry* Effect = QuerySelectedEffect();
    if (Effect == nullptr)
    {
        return "# Select a particle effect to inspect its .ParticleSpace descriptor.\n";
    }

    const ParticleCategoryDescriptor& CategoryDesc = ParticlePresetRegistry::QueryCategoryDescriptor(Effect->Category);
    char Buffer[128]{};
    std::ostringstream Out;
    Out << "[ParticleSpace]\n";
    Out << "SchemaVersion = 1\n";
    Out << "PresetSlug = \"" << Effect->PresetIdentifier << "\"\n";
    Out << "Title = \"" << Effect->Title << "\"\n";
    Out << "Category = \"" << CategoryDesc.FullTitle << "\"\n";
    Out << "Silhouette = \"" << ParticlePresetRegistry::QuerySilhouetteLabel(Effect->Silhouette) << "\"\n";
    Out << "Blend = \""
        << (Effect->BlendCategory == ParticleBlendCategory::AdditiveGlow ? "AdditiveGlow" : "PremultipliedAlpha")
        << "\"\n\n";

    Out << "[ReleaseVolume]\n";
    Out << "Shape = \"" << ParticlePresetRegistry::QueryReleaseVolumeLabel(Effect->ReleaseVolume) << "\"\n";
    std::snprintf(Buffer, sizeof(Buffer), "[%.2f, %.2f, %.2f]", Effect->OriginMeters.X, Effect->OriginMeters.Y, Effect->OriginMeters.Z);
    Out << "OriginMeters = " << Buffer << "\n";
    std::snprintf(Buffer, sizeof(Buffer), "%.2f", Effect->RadiusMeters);
    Out << "RadiusMeters = " << Buffer << "\n";
    Out << "Capacity = " << Effect->Capacity << "\n";
    std::snprintf(Buffer, sizeof(Buffer), "%.1f", Effect->BirthRatePerSec);
    Out << "BirthRatePerSec = " << Buffer << "\n\n";

    Out << "[ForceFieldsCoupling]\n";
    Out << "EngineDomain = \"SimulationSpace::ForceFields\"\n";
    Out << "ActiveForceSources = " << ForceSources.size() << "\n";
    std::snprintf(Buffer, sizeof(Buffer), "%.2f", Effect->ForceCouplingGain);
    Out << "CouplingGain = " << Buffer << "\n";
    std::snprintf(Buffer, sizeof(Buffer), "%.2f", Effect->DragCoefficient);
    Out << "DragCoefficient = " << Buffer << "\n";
    std::snprintf(Buffer, sizeof(Buffer), "%.2f", Effect->GravityMultiplier);
    Out << "GravityMultiplier = " << Buffer << "\n";

    if (Effect->AuthoringDomain == ParticleAuthoringDomain::FireworksSpectacle)
    {
        Out << "\n[FireworksSpectacle]\n";
        Out << "Pattern = \"" << ParticlePresetRegistry::QueryFireworkPatternLabel(Effect->FireworkPattern) << "\"\n";
        Out << "SkyText = \"" << Effect->FireworkSkyText << "\"\n";
    }
    else if (Effect->AuthoringDomain == ParticleAuthoringDomain::FerrofluidFlux)
    {
        Out << "\n[FerrofluidFlux]\n";
        Out << "Vessel = \"" << (Effect->FerrofluidVessel == 2u ? "FloatingMagneticOrb" : "PetriDishSpikes") << "\"\n";
        Out << "SpikeCount = " << Effect->FerroSpikeCount << "\n";
    }
    else if (Effect->AuthoringDomain == ParticleAuthoringDomain::RiggingAndFlora)
    {
        Out << "\n[RiggingAndFlora]\n";
        Out << "Architecture = \"" << ParticlePresetRegistry::QueryRiggingArchitectureLabel(Effect->RiggingArchitecture) << "\"\n";
        Out << "StrandRows = " << Effect->StrandRows << "\n";
        Out << "CurveSegments = " << Effect->CurveSegments << "\n";
    }

    return Out.str();
}

} // namespace Frontier::Workspaces
