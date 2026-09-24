//============================================================================================================================================
//                                                 CELESTIALSEQUENCE.CPP
//============================================================================================================================================

#include "CelestialSequence.h"
#include "DisplayPresentation/WeatherDiagnostics.h"
#include "DisplayPresentation/StarFieldControls.h"
#include "../../../Engine/DisplayPresentation/SunColourTemperature.h"

#include "../../../Engine/DisplayPresentation/SkyDomeSheet.h"   // roadmap #26 stage A: the bake + the staleness rule
#include "../../../Engine/ContentInterchange/SpaceExport.h"     // #26c: the persisted bake rides an .environment (ENVR + PROB)
#include "../../../Engine/ContentInterchange/SpaceCodec.h"

#include <atomic>
#include <limits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::ProjectZero {

namespace {

uint64_t NextSkyPreviewRevision() noexcept { static std::atomic<uint64_t> Revision{0};return Revision.fetch_add(1,std::memory_order_relaxed)+1; }

// Row tints, taken from the reference panel's entity colours so the outliner reads the same way.
constexpr float kTintAtmosphere[3] = { 0.353f, 0.663f, 1.000f };   // #5aa9ff
constexpr float kTintSun[3]        = { 1.000f, 0.706f, 0.329f };   // #ffb454
constexpr float kTintSky[3]        = { 0.404f, 0.910f, 0.976f };   // #67e8f9
constexpr float kTintStars[3]      = { 0.769f, 0.710f, 0.992f };   // #c4b5fd
constexpr float kTintFog[3]        = { 0.624f, 0.690f, 0.753f };   // #9fb0c0
constexpr float kTintCloud[3]      = { 0.878f, 0.906f, 0.941f };
constexpr float kTintWind[3]       = { 0.608f, 0.827f, 0.706f };
constexpr float kTintPrecip[3]     = { 0.490f, 0.827f, 0.988f };   // #7dd3fc
constexpr float kTintOptics[3]     = { 1.000f, 0.541f, 0.396f };   // #ff8a65
constexpr float kTintFolder[3]     = { 0.545f, 0.596f, 0.663f };

void CopyTint(float Out[3], const float In[3]) noexcept
{
    Out[0] = In[0]; Out[1] = In[1]; Out[2] = In[2];
}

EditorProperty MakeSlider(const char* Label, float Minimum, float Maximum, float Figure,
                          uint32_t Decimals, const char* Unit) noexcept
{
    EditorProperty P{};
    std::snprintf(P.Label, sizeof(P.Label), "%s", Label);
    P.Category = EditorPropertyCategory::Slider;
    P.Minimum = Minimum; P.Maximum = Maximum; P.Figure = Figure; P.Decimals = Decimals;
    if (Unit != nullptr) std::snprintf(P.Unit, sizeof(P.Unit), "%s", Unit);
    return P;
}

EditorProperty MakeSwitch(const char* Label, bool On) noexcept
{
    EditorProperty P{};
    std::snprintf(P.Label, sizeof(P.Label), "%s", Label);
    P.Category = EditorPropertyCategory::Switch;
    P.On = On;
    return P;
}

EditorProperty MakeReadout(const char* Label, const char* Text) noexcept
{
    EditorProperty P{};
    std::snprintf(P.Label, sizeof(P.Label), "%s", Label);
    P.Category = EditorPropertyCategory::Readout;
    std::snprintf(P.Text, sizeof(P.Text), "%s", Text);
    return P;
}

EditorProperty MakeColour(const char* Label, const float Tint[3]) noexcept
{
    EditorProperty P{};
    std::snprintf(P.Label, sizeof(P.Label), "%s", Label);
    P.Category = EditorPropertyCategory::Colour;
    P.ColourTint[0] = Tint[0]; P.ColourTint[1] = Tint[1]; P.ColourTint[2] = Tint[2];
    return P;
}

EditorProperty MakeAxes(const char* Label, const float Axes[3], float Step) noexcept
{
    EditorProperty P{};
    std::snprintf(P.Label, sizeof(P.Label), "%s", Label);
    P.Category = EditorPropertyCategory::AxisVec3;
    P.Axes[0] = Axes[0]; P.Axes[1] = Axes[1]; P.Axes[2] = Axes[2];
    P.AxisStep = Step;
    return P;
}

EditorProperty MakeSelect(const char* Label, const char* const* Options, uint32_t Count, uint32_t Picked) noexcept
{
    EditorProperty P{};
    std::snprintf(P.Label, sizeof(P.Label), "%s", Label);
    P.Category = EditorPropertyCategory::Select;
    P.OptionCount = Count > kMaxEditorOptions ? kMaxEditorOptions : Count;
    for (uint32_t I = 0; I < P.OptionCount; ++I)
        std::snprintf(P.Options[I], kMaxEditorOptionChars, "%s", Options[I]);
    P.Picked = Picked < P.OptionCount ? Picked : 0u;
    return P;
}

EditorPropertyGroup& OpenGroup(EditorSheet& Sheet, const char* Title) noexcept
{
    if (Sheet.GroupCount >= kMaxEditorSheetGroups) Sheet.GroupCount = kMaxEditorSheetGroups - 1u;
    EditorPropertyGroup& G = Sheet.Groups[Sheet.GroupCount++];
    // BuildSheet already reset each slot in place; avoid another group-sized temporary.
    std::snprintf(G.Title, sizeof(G.Title), "%s", Title);
    return G;
}

void Push(EditorPropertyGroup& Group, const EditorProperty& Property) noexcept
{
    if (Group.PropertyCount >= kMaxEditorGroupProps) return;
    Group.Properties[Group.PropertyCount++] = Property;
}

// Reads a property back by label, so ApplySheet does not depend on the order BuildSheet happened to write.
//    Order-dependent write-back is how a sheet edit lands on the wrong field after someone inserts a row.
const EditorProperty* Find(const EditorSheet& Sheet, const char* Label) noexcept
{
    for (uint32_t G = 0; G < Sheet.GroupCount; ++G)
        for (uint32_t P = 0; P < Sheet.Groups[G].PropertyCount; ++P)
            if (std::strcmp(Sheet.Groups[G].Properties[P].Label, Label) == 0)
                return &Sheet.Groups[G].Properties[P];
    return nullptr;
}

float ReadSlider(const EditorSheet& Sheet, const char* Label, float Fallback) noexcept
{
    const EditorProperty* P = Find(Sheet, Label);
    return P != nullptr ? P->Figure : Fallback;
}

bool ReadSwitch(const EditorSheet& Sheet, const char* Label, bool Fallback) noexcept
{
    const EditorProperty* P = Find(Sheet, Label);
    return P != nullptr ? P->On : Fallback;
}

uint32_t ReadSelect(const EditorSheet& Sheet, const char* Label, uint32_t Fallback) noexcept
{
    const EditorProperty* P = Find(Sheet, Label);
    return P != nullptr ? P->Picked : Fallback;
}

void ReadAxes(const EditorSheet& Sheet, const char* Label, float Out[3]) noexcept
{
    const EditorProperty* P = Find(Sheet, Label);
    if (P == nullptr) return;
    Out[0] = P->Axes[0]; Out[1] = P->Axes[1]; Out[2] = P->Axes[2];
}

// Horizon to unit vector, transcribed from CelestialSolver's ToDirection (east/north/up, azimuth clockwise from
//    north). The solver owns the ephemeris, but a placed moon is project presentation, not astronomy.
void AzElevToDirection(float ElevationDegrees, float AzimuthDegrees, float Out[3]) noexcept
{
    constexpr float kDeg = 3.14159265358979323846f / 180.0f;
    const float CosE = std::cos(ElevationDegrees * kDeg);
    Out[0] = CosE * std::sin(AzimuthDegrees * kDeg);   // east
    Out[1] = CosE * std::cos(AzimuthDegrees * kDeg);   // north
    Out[2] = std::sin(ElevationDegrees * kDeg);        // up
}

// The roster becomes a draw list. The ONE resolver both ApplyTo and PackMoonRecord call, so the raster and the
//    kernel cannot be handed different moons: visibility, the linked-Luna ephemeris read, the phase conversion
//    and the preset skinning happen here or nowhere.
void ResolveMoonDrawList(const MoonSlotState* Slots, const CelestialFrame& Solved, const MoonAlbedoView* Views,
                         const uint32_t* TextureSlots, MoonDrawList& Out) noexcept
{
    Out = MoonDrawList{};
    if (Slots == nullptr || Views == nullptr || TextureSlots == nullptr) return;
    constexpr float kDeg = 3.14159265358979323846f / 180.0f;
    for (uint32_t S = 0u; S < kMoonDrawCount; ++S)
    {
        const MoonSlotState& M = Slots[S];
        if (!M.Visible) continue;
        const uint32_t P = M.Preset < kMoonAtlasCount ? M.Preset : 0u;
        const MoonAtlasPreset& A = kMoonAtlas[P];
        MoonDrawEntry& E = Out.Entries[Out.Count];
        ++Out.Count;
        if (M.FollowSky)
        {
            // ⚠️ The direction comes from the SOLVED frame — the same first-frame rule PackSkyRecord records
            //    for the sun. Solved is written by Prepare as well as by Tick, so it is always right.
            E.Direction[0] = Solved.Moon.Direction[0];
            E.Direction[1] = Solved.Moon.Direction[1];
            E.Direction[2] = Solved.Moon.Direction[2];
            E.Phase = MoonPhaseToReference(Solved.MoonPhase);
        }
        else
        {
            AzElevToDirection(M.Elevation, M.Azimuth, E.Direction);
            E.Phase = MoonPhaseToReference(M.Phase);
        }
        E.Tint[0] = A.Tint[0]; E.Tint[1] = A.Tint[1]; E.Tint[2] = A.Tint[2];
        E.AngularRadius = (M.Size > 0.0f ? M.Size : 0.0f) * kDeg * 0.5f;
        E.Brightness = M.Bright;
        E.Glow = M.Glow;
        E.Spin = 0.0f;
        // Reference pitch rotates features forward; the texture lookup needs its inverse.
        E.Tilt = (A.TiltDegrees - M.Pitch) * kDeg;
        E.Roll = M.Roll * kDeg;
        E.Haze = A.Haze;
        E.Gamma = A.Gamma;
        E.Albedo = Views[P];
        E.TextureSlot = TextureSlots[P];
    }
}

// One label scheme for the four slot groups, shared by BuildSheet and ApplySheet: Find reads back BY LABEL, so
//    four groups sharing "Azimuth" would all read slot one's value. The M1..M4 prefix keeps every label on the
//    sheet unique.
void MoonPropLabel(uint32_t Slot, const char* Leaf, char* Out, size_t OutSize) noexcept
{
    std::snprintf(Out, OutSize, "M%u %s", Slot + 1u, Leaf);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------

const char* CelestialEntityName(CelestialEntity Entity) noexcept
{
    switch (Entity)
    {
    case CelestialEntity::Atmosphere:     return "Atmosphere";
    case CelestialEntity::Sun:            return "Sun";
    case CelestialEntity::Sky:            return "Sky";
    case CelestialEntity::Stars:          return "Stars";
    case CelestialEntity::Moons:          return "Moons";
    case CelestialEntity::HeightFog:      return "Height Fog";
    case CelestialEntity::AtmosphericFog: return "Atmospheric Fog";
    case CelestialEntity::CloudLayer:     return "Cloud Layer";
    case CelestialEntity::LocalCloud:     return "Local Cloud";
    case CelestialEntity::LocalFog:       return "Local Volumetric Fog";
    case CelestialEntity::Wind:           return "Wind";
    case CelestialEntity::Precipitation:  return "Precipitation";
    case CelestialEntity::Rainbow:        return "Rainbow";
    case CelestialEntity::LensFlare:      return "Lens Flare";
    default:                              return "?";
    }
}

const char* CelestialEntityKind(CelestialEntity Entity) noexcept
{
    switch (Entity)
    {
    case CelestialEntity::Atmosphere:     return "Atmosphere";
    case CelestialEntity::Sun:            return "Directional Light";
    case CelestialEntity::Sky:            return "Sky Atmosphere";
    case CelestialEntity::Stars:          return "Star Field";
    case CelestialEntity::Moons:          return "Atlas";
    case CelestialEntity::HeightFog:      return "Volumetrics";
    case CelestialEntity::AtmosphericFog: return "Aerial Perspective";
    case CelestialEntity::CloudLayer:     return "Clouds";
    case CelestialEntity::LocalCloud:     return "Cloud Volume";
    case CelestialEntity::LocalFog:       return "Fog Volume";
    case CelestialEntity::Wind:           return "Wind Field";
    case CelestialEntity::Precipitation:  return "Component";
    case CelestialEntity::Rainbow:        return "Optics";
    case CelestialEntity::LensFlare:      return "Component";
    default:                              return "?";
    }
}

//------------------------------------------------------------------------------------------------------------------------

void CelestialSequence::Prepare() noexcept
{
    for (uint32_t I = 0; I < kCelestialEntityCount; ++I) Shown[I] = true;

    // A believable default sky rather than a blank one: mid-afternoon at the project's own latitude, broken
    //    cumulus, a light breeze, no rain. Somebody opening the editor should see weather, not a switch to find.
    // The 10th was new-moon day: Luna a 0.3%-lit sliver lost in daylight and set at night, so the default
    //    sky showed no moon at all. The 19th is first quarter — a half-lit moon high in the default afternoon
    //    (el +51, az 102) and up all evening. The ephemeris stays exact; this only picks a date with a moon.
    Observation.Year = 2026; Observation.Month = 9; Observation.Day = 19;
    // ⚠️ Mid-afternoon, NOT sunset. The 17.93 h staging put the sun ~1° above the horizon: through twenty air
    //    masses its direct term is a deep-red trickle, and a shadow it casts is hundreds of metres long and
    //    dimmer than the sky fill — every run "had no shadows" when in fact the shadows were simply invisible.
    //    At 15.5 h the sun stands ~40° up, its direct light dominates the sky fill, and both the ReSTIR sun
    //    shadows (GI on) and the R10 shadow maps (GI off) read unmistakably. Sunset is one Weather-panel
    //    slider away; the DEFAULT must be the hour that proves the lighting works.
    Observation.LocalHours = 15.5f; Observation.UtcOffset = 2.0f;
    Observation.Latitude = -26.19f; Observation.Longitude = 28.32f;

    Cloud.Enabled = true;
    Cloud.Type = CloudTypeCategory::Cumulus;
    Cloud.Base = 1400.0f; Cloud.Thickness = 1100.0f;
    // Calibrated against the march, not guessed: the old 0.52/1.4/1.0 put 80 of 81 zenith columns under cloud
    //    (a white sky — the fbm piles samples mid-range, so 0.52 thresholded nearly everything). Swept twice:
    //    zenith columns want 0.45, but a level camera's rays take ~17 samples to the zenith's 8, so the frames
    //    stayed overcast; swept at frame-top geometry (27 deg rays) the 4-octave field wanted 0.38. Dropping
    //    the unresolvable fourth octave smoothed the field toward broader cloud, so swept a third time: 0.34
    //    gives 10 clear, 13 broken, 4 opaque in 27 — blue gaps overhead, veiling toward the horizon, opaque
    //    cores. The horizon whitens by path length, which is what real broken skies do.
    Cloud.Coverage = 0.34f; Cloud.Density = 2.4f; Cloud.Scale = 0.35f;

    Wind.Speed = 7.0f; Wind.Bearing = 250.0f;

    // A 200 m puff parked 300 m out with 40 m features. The first box (120 m at 160 m) took ~1 slab-paced
    //    step per ray and rendered a smooth white blob; the second (300 m at 160 m) filled the frame and stared
    //    back as a whiteout. Two hundred metres at 300 m takes 2 steps and subtends ~35 deg: a soft distant
    //    puff, which is what slab-paced sampling can honestly draw — per-medium steps are the follow-up that
    //    would texture a near box. Probed 9h-13h at 0.8/2.5: real body throughout (0.43-0.53 mean), best at 11h.
    //    Parked off by default — the showcase and the pins enable it where they need it.
    LocalCloud.Centre[0] = -100.0f; LocalCloud.Centre[1] = 260.0f; LocalCloud.Centre[2] = 130.0f;
    LocalCloud.HalfSize[0] = 100.0f; LocalCloud.HalfSize[1] = 100.0f; LocalCloud.HalfSize[2] = 50.0f;
    LocalCloud.Coverage = 0.8f; LocalCloud.Density = 2.5f; LocalCloud.Scale = 40.0f;
    LocalFog.Centre[0] = 70.0f; LocalFog.Centre[1] = 90.0f; LocalFog.Centre[2] = 14.0f;
    LocalFog.HalfSize[0] = 70.0f; LocalFog.HalfSize[1] = 70.0f; LocalFog.HalfSize[2] = 14.0f;

    Precip.Enabled = false;
    Rain.Configure(8192u);

    // The catalogue is optional: a missing asset must leave a working sky rather than refusing to start, so the
    //    result is deliberately not checked. StarCatalogueIndex reports Empty() and the star loop skips.
    (void)Catalogue.Load("EngineContent/StarCatalogue/BrightStars.bin");

    // The roster opens the way the reference panel does: one moon, Luna — except ours follows the solved lunar
    //    frame rather than sitting at a fixed chart position, because this engine HAS an ephemeris. The other
    //    three slots are parked where the panel would put them (az 300+i*47, elev 28-i*6) and hidden, one
    //    inspector toggle away.
    for (uint32_t I = 0u; I < kMoonDrawCount; ++I) MoonSlots[I] = MoonSlotState{};
    MoonSlots[0].Preset = 0u; MoonSlots[0].FollowSky = true; MoonSlots[0].Visible = true;
    MoonSlots[0].Size = kMoonAtlas[0].SizeDegrees;
    const uint32_t Parked[3] = { 1u, 2u, 3u };
    for (uint32_t K = 0u; K < 3u; ++K)
    {
        MoonSlotState& M = MoonSlots[K + 1u];
        M.Preset = Parked[K];
        M.Visible = false;
        M.Azimuth = static_cast<float>((300u + (K + 1u) * 47u) % 360u);
        M.Elevation = 28.0f - static_cast<float>(K + 1u) * 6.0f;
        M.Size = kMoonAtlas[Parked[K]].SizeDegrees;
    }

    Solved = CelestialSolver::Solve(Observation);
}

//------------------------------------------------------------------------------------------------------------------------

void CelestialSequence::Tick(float DeltaSeconds, const float Camera[3], float GroundHeight) noexcept
{
    if (!std::isfinite(DeltaSeconds) || DeltaSeconds < 0.0f) DeltaSeconds = 0.0f;
    StarSeconds += DeltaSeconds;
    WeatherSeconds += DeltaSeconds;

    // ① The clock. Wrapped rather than clamped, so a fast day cycle rolls into the next morning.
    if (Clock.Animate)
    {
        ElapsedHours += DeltaSeconds * Clock.SpeedTimes / 3600.0f;
        Observation.LocalHours += ElapsedHours;
        ElapsedHours = 0.0f;
        while (Observation.LocalHours >= 24.0f) Observation.LocalHours -= 24.0f;
        while (Observation.LocalHours < 0.0f)   Observation.LocalHours += 24.0f;
    }

    if (SunUseTemperature)
    {
        SunTemperatureKelvin = SunColourTemperature::Clamp(SunTemperatureKelvin);
        const auto Tint = SunColourTemperature::LinearRgb(SunTemperatureKelvin);
        for (int C = 0; C < 3; ++C) Light.Colour[C] = Tint[C];
    }

    // ② The ephemeris, from whatever the clock now says.
    Solved = CelestialSolver::Solve(Observation);
    for (int C = 0; C < 3; ++C) Light.Direction[C] = Solved.Sun.Direction[C];

    // ③ The wind's gust phase. Everything downstream advects by this, so it moves before they do.
    Wind.GustPhase += DeltaSeconds * 0.35f;
    if (Wind.GustPhase > 6.28318531f * 1024.0f) Wind.GustPhase -= 6.28318531f * 1024.0f;

    // ④ Precipitation last: its emitter reads the cloud layer, which the wind has just moved.
    const bool Falling = Enabled && Precip.Enabled && Shown[static_cast<uint32_t>(CelestialEntity::Precipitation)];
    PrecipitationSettings Active = Precip;
    Active.Enabled = Falling;
    Rain.Step(Active, Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] ? Cloud : CloudLayerSettings{},
              Wind, Camera, DeltaSeconds, static_cast<float>(WeatherSeconds), GroundHeight);
}

//------------------------------------------------------------------------------------------------------------------------

// ⚠️ The parameter is named `Limits`, not `Budget`: the class has a `Budget` member too (the sequence's own tier
//    spend, read by the editor's Tier Budget group), and MSVC's C4458 flagged the parameter as hiding it. The
//    function is *handed* a budget by its caller, so the parameter is the one that should win — naming it
//    differently makes that unambiguous instead of accidental.
void CelestialSequence::ApplyTo(VisibilityRaster& Raster, const CelestialBudget& Limits) const noexcept
{
    VisibilityRaster::CelestialSettings Settings{};
    Settings.Enabled = Enabled;
    Settings.Medium  = Medium;
    Settings.Light   = Light;

    // ⚠️ The direction comes from the SOLVED frame, not from Light. Light.Direction is a cache the tick fills,
    //    and a caller that renders before its first tick would otherwise get the struct default (straight up)
    //    instead of the sun — a wrong first frame, and one that corrects itself a frame later, which is the
    //    hardest kind to notice. Solved is written by Prepare as well as by Tick, so it is always right.
    for (int C = 0; C < 3; ++C) Settings.Light.Direction[C] = Solved.Sun.Direction[C];

    // Sky brightness and tint are the panel's, applied to the sun's radiance rather than to the medium, so the
    //    physical coefficients stay physical and the artistic controls stay separable.
    Settings.Light.Intensity *= SkyBrightness;
    for (int C = 0; C < 3; ++C) Settings.Light.Colour[C] *= SkyTint[C];

    Settings.Twilight = Twilight;
    if (!Shown[static_cast<uint32_t>(CelestialEntity::Sun)])
    {
        // A hidden sun is night, not a black sky: the direction still exists, the radiance does not.
        Settings.Light.Intensity = 0.0f;
    }

    Settings.CameraHeight     = 2.0f;
    Settings.SampleCount      = Limits.AtmosphereSamples;
    Settings.LightSampleCount = Limits.AtmosphereLightSamples;

    const bool WantStars = Shown[static_cast<uint32_t>(CelestialEntity::Stars)] && !Catalogue.Empty();
    Settings.Stars             = WantStars ? &Catalogue : nullptr;
    Settings.StarBrightness    = StarBrightness;
    Settings.StarSize          = StarSize;
    Settings.LocalSiderealTime = Solved.LocalSiderealTime + StarRotation;
    Settings.StarMinimumLuminance=StarMinimumLuminance(StarMagnitude);
    Settings.StarDepth=StarTwinkle?StarDepth*.01f:0.f;Settings.StarRate=StarRate;
    Settings.StarSeconds=static_cast<float>(std::fmod(StarSeconds,100.0/static_cast<double>(StarRate)));
    Settings.Latitude          = Observation.Latitude;
    for (int C = 0; C < 3; ++C) Settings.GroundAlbedo[C] = GroundAlbedo[C];

    // The moons resolve from the same roster PackMoonRecord packs, through the same resolver — the raster and
    //    the kernel cannot be handed different moons. Gated like the stars: the list is lent only when the
    //    system is on, the entity is shown, and an atlas was actually assigned; anything else lends null, which
    //    is the raster's default and what every existing proof renders against.
    MoonDraw_ = MoonDrawList{};
    const bool WantMoons = Enabled && AtlasAssigned_ && Shown[static_cast<uint32_t>(CelestialEntity::Moons)];
    if (WantMoons)
        ResolveMoonDrawList(MoonSlots, Solved, MoonViews_, MoonTextureSlots_, MoonDraw_);
    Settings.Moons = (WantMoons && MoonDraw_.Count > 0u) ? &MoonDraw_ : nullptr;

    // The clouds ride by value, gated the way the moons are: the system on, the entity shown. Anything else
    //    lends a disabled struct, which is the march's own early-out — the raster never has to ask.
    const bool WantClouds = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)];
    const bool WantLocalCloud = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::LocalCloud)];
    const bool WantLocalFog = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::LocalFog)];
    Settings.CloudLayer = (WantClouds && Cloud.Enabled) ? Cloud : CloudLayerSettings{};
    Settings.LocalCloud = (WantLocalCloud && LocalCloud.Enabled) ? LocalCloud : LocalVolumeSettings{};
    Settings.LocalFog = (WantLocalFog && LocalFog.Enabled) ? LocalFog : LocalVolumeSettings{};
    Settings.Fog=Fog;
    Settings.Fog.HeightEnabled=Enabled&&Shown[static_cast<uint32_t>(CelestialEntity::HeightFog)]&&Fog.HeightEnabled;
    Settings.Fog.AerialEnabled=Enabled&&Shown[static_cast<uint32_t>(CelestialEntity::AtmosphericFog)]&&Fog.AerialEnabled;
    Settings.Wind = Wind;
    if(!Shown[static_cast<uint32_t>(CelestialEntity::Wind)])Settings.Wind.Speed=0;
    Settings.CloudBudget = Limits.Volumetrics;
    Settings.CloudTime = static_cast<float>(WeatherSeconds);

    Raster.AssignCelestial(Settings);
}

namespace {

// Fold the shadow drift exactly as the CPU showcase folds it (SkyFogIntegrator::AssignCloudShadow): rebuild
//    the panel layer from the staging, take the mid-slab wind, scale by (time * 0.8) — the parentheses are
//    load-bearing (the CPU split proof caught a 1-ulp reassociation). Disabled/empty slabs fold to zero drift.
void FoldShadowDrift(const CloudShadowStaging& Staging, float TimeSeconds, float& DriftX, float& DriftY) noexcept
{
    DriftX = 0.0f;
    DriftY = 0.0f;
    CloudLayerSettings Layer{};
    Layer.Enabled = Staging.Enabled;
    Layer.Type = static_cast<CloudTypeCategory>(Staging.Type > 5u ? 2u : Staging.Type);
    Layer.Base = Staging.Base;
    Layer.Thickness = Staging.Thickness;
    Layer.Coverage = Staging.Coverage;
    Layer.Density = Staging.Density;
    Layer.Scale = Staging.Scale;
    Layer.Anvil = Staging.Anvil;
    Layer.CeilingMetres = Staging.CeilingMetres;
    float SlabBase = 0.0f, SlabTop = 0.0f;
    if (Staging.Enabled && VolumetricMedia::SlabExtent(Layer, SlabBase, SlabTop))
    {
        WindSettings Wind{};
        Wind.Speed = Staging.WindSpeed;
        Wind.Bearing = Staging.WindBearing;
        float Flow[3] = { 0.0f, 0.0f, 0.0f };
        WindField::SampleStep(Wind, (SlabBase + SlabTop) * 0.5f, Flow);
        DriftX = Flow[0] * (TimeSeconds * 0.8f);
        DriftY = Flow[1] * (TimeSeconds * 0.8f);
    }
}

} // namespace

void CelestialSequence::AssignCloudShadowStaging(const CloudShadowStaging& Staging) noexcept
{
    ShadowStaging = Staging;
    ShadowTimeSeconds = Staging.TimeSeconds;
}

void CelestialSequence::AssignCloudShadowTime(float TimeSeconds) noexcept
{
    ShadowTimeSeconds = TimeSeconds;
}

SkyConstantRecord CelestialSequence::PackSkyRecord() const noexcept
{
    // ⚠️ Every adjustment here mirrors ApplyTo above, for the reasons recorded there. The direction is the SOLVED
    //    frame's, not Light's cache — a caller that packs before its first tick must still get the real sun, not
    //    the struct default. The tint and brightness ride on the radiance, not the medium. A hidden sun removes
    //    the radiance, not the sky. The eye height is the same fixed 2 m: the kernel's integral, like the
    //    raster's, is evaluated for one observer, not per ray.
    AtmosphereLight Effective = Light;
    for (int C = 0; C < 3; ++C) Effective.Direction[C] = Solved.Sun.Direction[C];
    Effective.Intensity *= SkyBrightness;
    for (int C = 0; C < 3; ++C) Effective.Colour[C] *= SkyTint[C];
    if (!Shown[static_cast<uint32_t>(CelestialEntity::Sun)])
        Effective.Intensity = 0.0f;

    float ShadowDriftX = 0.0f, ShadowDriftY = 0.0f;
    FoldShadowDrift(ShadowStaging, ShadowTimeSeconds, ShadowDriftX, ShadowDriftY);
    SkyConstantRecord Record = PackSkyConstants(Medium, Effective, Twilight, Solved.Sun.Elevation, /*CameraHeightMetres=*/2.0f,
                                                Budget.AtmosphereSamples, Budget.AtmosphereLightSamples, Enabled, SunDirect,
                                                ShadowStaging, ShadowDriftX, ShadowDriftY);
    // The baked dome (roadmap #26 stage A): SkyControl.w carries the bindless slot + 1 — but ONLY while the
    //    boolean is on, a sheet is resident, and the staging the bake was taken from still matches the record
    //    just packed. A scrubbed sun, a changed medium or a re-budgeted sample count silently drops the frame
    //    back to the analytic march; nothing ever renders against yesterday's air. Control[3] is otherwise 0,
    //    so the OFF path packs bytes identical to the pre-bake build (the A/B's identity).
    if (SkyDomeBaked && SkyDomeSeated && SkyDomeSlot != kNoSkyDomeSlot
        && SkyDomeStagingMatches(SkyDomeRecord, Record))
        Record.Control[3] = SkyDomeSlot + 1u;
    return Record;
}

void CelestialSequence::AssignSkyDomeSlot(uint32_t BindlessSlot) noexcept
{
    SkyDomeSlot = BindlessSlot;
}

bool CelestialSequence::QuerySkyDomeLive() const noexcept
{
    if (!SkyDomeBaked || !SkyDomeSeated || SkyDomeSlot == kNoSkyDomeSlot) return false;
    return SkyDomeStagingMatches(SkyDomeRecord, PackSkyRecord());
}

// #26c — the persisted bake. The PROB blob is the staging record (144 B, the same bytes the staleness rule
//    compares) followed by the RGBA16F halves; the ENVR row carries the staging figures a browsing tool shows.
//    One layout, written and read by these two bodies only, verified by SkyDomeKernelProof's persistence gates.
bool CelestialSequence::SaveSkyDome(const std::string& Path, const std::vector<uint16_t>& Halves) const noexcept
{
    if (!SkyDomeSeated || Halves.empty()) return false;

    std::vector<uint8_t> Probe(sizeof(SkyConstantRecord) + Halves.size() * sizeof(uint16_t));
    std::memcpy(Probe.data(), &SkyDomeRecord, sizeof(SkyConstantRecord));
    std::memcpy(Probe.data() + sizeof(SkyConstantRecord), Halves.data(), Halves.size() * sizeof(uint16_t));

    SpaceEnvironmentRow Row{};
    std::snprintf(Row.Name, sizeof(Row.Name), "Sky Dome");
    Row.SunHour         = Observation.LocalHours;
    Row.FogDensity      = 0.0f;                       // the dome bakes no fog — the march keeps volumetrics
    Row.AtmosphereScale = Medium.RayleighStrength;
    Row.MoonPhase       = 0.0f;                       // the moon never bakes (its slots draw it)
    Row.TerrainRef  = 0xFFFFFFFFu;
    Row.TerrainBlob = 0xFFFFFFFFu;

    SpaceExportContext Context;
    Context.Exporter = "CelestialSequence";
    std::vector<uint8_t> Bytes;
    std::string ExportError;
    if (!SpaceExportEnvironment(Context, Row, "Sky Dome", Probe, /*SkyProbeLevels=*/1u, Bytes, ExportError))
        return false;

    std::FILE* File = std::fopen(Path.c_str(), "wb");
    if (File == nullptr) return false;
    const bool Written = std::fwrite(Bytes.data(), 1u, Bytes.size(), File) == Bytes.size();
    std::fclose(File);
    return Written;
}

bool CelestialSequence::LoadSkyDome(const std::string& Path, std::vector<uint16_t>& OutHalves) noexcept
{
    OutHalves.clear();
    std::string ReadError;
    SpaceReader Reader;
    if (!Reader.OpenFile(Path, ReadError)) return false;
    const std::vector<SpaceEnvironmentRow> Rows = Reader.Rows<SpaceEnvironmentRow>(kTagEnvr, ReadError);
    if (Rows.empty() || Rows[0].SkyProbeBlob == 0xFFFFFFFFu) return false;
    if (!Reader.ReadBlobs(ReadError)) return false;
    std::vector<uint8_t> Probe;
    if (!Reader.BlobBytes(Rows[0].SkyProbeBlob, Probe, ReadError)) return false;

    constexpr size_t kSheetHalves = size_t(kSkyDomeSide) * kSkyDomeSide * 2u * 4u;
    if (Probe.size() != sizeof(SkyConstantRecord) + kSheetHalves * sizeof(uint16_t)) return false;

    // The staleness rule at the file boundary: the file's recorded staging against THIS staging, packed the
    //    same way the bake would pack it. A moved sun, a changed medium, a re-budgeted sample count — the
    //    file yields and the lazy runtime bake takes over, exactly as a stale in-memory bake would.
    SkyConstantRecord FileRecord{};
    std::memcpy(&FileRecord, Probe.data(), sizeof(SkyConstantRecord));
    SkyConstantRecord Current = PackSkyRecord();
    Current.Control[3] = 0u;
    if (!SkyDomeStagingMatches(FileRecord, Current)) return false;

    OutHalves.resize(kSheetHalves);
    std::memcpy(OutHalves.data(), Probe.data() + sizeof(SkyConstantRecord), kSheetHalves * sizeof(uint16_t));
    SkyDomeRecord = FileRecord;
    SkyDomeSeated = true;
    SkyPreviewHalves=OutHalves;SkyPreviewRevision=NextSkyPreviewRevision();SkyDomeSlot=kNoSkyDomeSlot;
    return true;
}

void CelestialSequence::BakeSkyDome(std::vector<uint16_t>& OutHalves) noexcept
{
    // The bake integrates the SAME effective light PackSkyRecord packs (solved direction, tint and brightness
    //    on the radiance, a hidden sun as night), at the SAME budget — so the staging-match compare below is
    //    against exactly the arithmetic that filled the sheet.
    AtmosphereLight Effective = Light;
    for (int C = 0; C < 3; ++C) Effective.Direction[C] = Solved.Sun.Direction[C];
    Effective.Intensity *= SkyBrightness;
    for (int C = 0; C < 3; ++C) Effective.Colour[C] *= SkyTint[C];
    if (!Shown[static_cast<uint32_t>(CelestialEntity::Sun)])
        Effective.Intensity = 0.0f;
    BakeSkyDomeSheet(Medium, Effective, Budget.AtmosphereSamples == 0u ? 1u : Budget.AtmosphereSamples,
                     Budget.AtmosphereLightSamples == 0u ? 1u : Budget.AtmosphereLightSamples, OutHalves);
    // Remember the staging WITHOUT the dome lane: the compare runs against freshly packed records whose
    //    Control[3] is still 0 at compare time (the lane is seated after the match).
    SkyDomeRecord = PackSkyRecord();
    SkyDomeRecord.Control[3] = 0u;
    SkyDomeSeated = true;
    SkyPreviewHalves=OutHalves;SkyPreviewRevision=NextSkyPreviewRevision();SkyDomeSlot=kNoSkyDomeSlot;
}

void CelestialSequence::AssignMoonAtlas(const uint32_t Slots[kMoonAtlasCount], const TextureIndex& Textures) noexcept
{
    const std::vector<TextureDescriptor>& All = Textures.QueryTextures();
    for (uint32_t I = 0u; I < kMoonAtlasCount; ++I)
    {
        MoonTextureSlots_[I] = Slots[I];
        MoonViews_[I] = MoonAlbedoView{};
        // A slot past the index, or a descriptor with no level-0 pixels yet (registered but never decoded),
        //    lends an empty view — which samples as white, the placeholder's own colour — rather than a
        //    dangling pointer. The kernel side needs no such guard: an unassigned atlas packs a zero count in
        //    PackMoonRecord, and zero moons sample no slots.
        if (Slots[I] < All.size() && All[Slots[I]].TexelBytes() == 4u && !All[Slots[I]].Texels.empty())
        {
            const TextureDescriptor& T = All[Slots[I]];
            const size_t Level0 = T.LevelOffsets.empty() ? 0u : T.LevelOffsets[0];
            if (Level0 < T.Texels.size())
            {
                MoonViews_[I].Texels = T.Texels.data() + Level0;
                MoonViews_[I].Width = T.Width;
                MoonViews_[I].Height = T.Height;
                MoonViews_[I].TexelBytes = T.TexelBytes();
            }
        }
    }
    static std::atomic<uint64_t> Revision{0};MoonAtlasRevision_=Revision.fetch_add(1,std::memory_order_relaxed)+1;
    AtlasAssigned_ = true;
}

MoonConstantRecord CelestialSequence::PackMoonRecord() const noexcept
{
    // ⚠️ Every gate here mirrors ApplyTo above, for the reasons recorded there. A hidden Moons entity is a
    //    moonless sky, not a black one: the count goes to zero and the kernel's early-out leaves the stale
    //    bytes unread. An unassigned atlas packs the same zero — a caller without textures gets no moons, not
    //    slot 0's material.
    MoonDrawList Draw{};
    const bool WantMoons = Enabled && AtlasAssigned_ && Shown[static_cast<uint32_t>(CelestialEntity::Moons)];
    if (WantMoons)
        ResolveMoonDrawList(MoonSlots, Solved, MoonViews_, MoonTextureSlots_, Draw);
    return PackMoonConstants(Draw.Entries, Draw.Count);
}

PostConstantRecord CelestialSequence::PackPostRecord(const float CameraForward[3], const float CameraRight[3],
                                                     const float CameraUp[3], float TanHalfFieldOfView,
                                                     float AspectRatio, uint32_t ViewportHeightPx,
                                                     float SunVisibility) const noexcept
{
    // Stars ride the solved sidereal time and the observer's latitude; a hidden Stars entity or a missing
    //    catalogue packs zero brightness, which is the kernel's early-out (the tables upload never ran, but the
    //    bring-up zeros stand and the brightness gate never touches them).
    const bool WantStars = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::Stars)] && !Catalogue.Empty();
    const float StarsScale = WantStars ? CelestialSequence::StarBrightness : 0.0f;   // the member, gated by the entity
                                                                                     //    (the local used to be named after
                                                                                     //    the member — C4458, same as above)
    // The star core floors at half a pixel: the shader's PixelSpreadAngle(), computed here because the post
    //    file reads no push constants. A zero height is a caller bug — guard rather than divide.
    const float PixelSpread = ViewportHeightPx > 0u && TanHalfFieldOfView > 0.0f
                            ? 2.0f * TanHalfFieldOfView / static_cast<float>(ViewportHeightPx) : 0.0f;

    // The sun to screen UV: the kernel's ray reconstruction inverted. direction ∝ F + R·(ndc.x·t·a) − U·(ndc.y·t),
    //    so ndc.x = (s·R/f)/(t·a) and ndc.y = −(s·U/f)/t with f = s·F. A sun behind the camera (f ≤ 0) parks at
    //    (−10, −10), outside the flare's edge fade — framing, not occlusion, kills that flare.
    float SunU = -10.0f, SunV = -10.0f;
    const float F = Solved.Sun.Direction[0] * CameraForward[0] + Solved.Sun.Direction[1] * CameraForward[1]
                  + Solved.Sun.Direction[2] * CameraForward[2];
    if (F > 1e-6f && TanHalfFieldOfView > 0.0f && AspectRatio > 0.0f)
    {
        const float R = Solved.Sun.Direction[0] * CameraRight[0] + Solved.Sun.Direction[1] * CameraRight[1]
                      + Solved.Sun.Direction[2] * CameraRight[2];
        const float U = Solved.Sun.Direction[0] * CameraUp[0] + Solved.Sun.Direction[1] * CameraUp[1]
                      + Solved.Sun.Direction[2] * CameraUp[2];
        SunU = ((R / F) / (TanHalfFieldOfView * AspectRatio)) * 0.5f + 0.5f;
        SunV = ((-(U / F)) / TanHalfFieldOfView) * 0.5f + 0.5f;
    }
    const bool WantFlare = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::LensFlare)] && Flare.Enabled;

    // Rain visibility rises with the fall rate (10 mm/h moderate = full column); drizzle earns half, snow and
    //    hail earn nothing — ice makes halos, not bows. Default off with the precipitation itself.
    const float RainVisibility = WeatherDiagnostics::RainVisibility(Precip, Enabled && Shown[static_cast<uint32_t>(CelestialEntity::Precipitation)]);
    const bool WantBow = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::Rainbow)] && Rainbow.Enabled;

    // The horizon fade for the flare's visibility, −2..0° of sun elevation. The model's SmoothStep is private
    //    to its class, so the three lines are restated here rather than reached for.
    const float ElevT = (Solved.Sun.Elevation + 2.0f) / 2.0f;
    const float ElevC = ElevT < 0.0f ? 0.0f : (ElevT > 1.0f ? 1.0f : ElevT);
    const float ElevationFade = ElevC * ElevC * (3.0f - 2.0f * ElevC);

    float ShadowDriftX = 0.0f, ShadowDriftY = 0.0f;
    FoldShadowDrift(ShadowStaging, ShadowTimeSeconds, ShadowDriftX, ShadowDriftY);
    auto Record = PackPostConstants(Solved.LocalSiderealTime + StarRotation, Observation.Latitude, StarSize, StarsScale, PixelSpread,
                             static_cast<uint32_t>(Flare.Category), Flare.GhostCount, Flare.Intensity,
                             Flare.HaloRadius, Flare.Chromatic, Flare.StreakGain, Flare.ApertureBlades,
                             // Below-horizon suns flare nothing (no direct light enters the lens); faded over
                             // −2..0° so the ghosts leave with the sunset rather than popping.
                             SunVisibility * ElevationFade,
                             SunU, SunV, WantFlare,
                             Rainbow.Intensity, Rainbow.Width, Rainbow.SecondaryGain, Rainbow.AlexanderBand,
                             Rainbow.MinimumPathMetres, RainVisibility, WantBow,
                             ShadowStaging, ShadowDriftX, ShadowDriftY);
    Record.PostStarEffects[0]=StarMinimumLuminance(StarMagnitude);
    Record.PostStarEffects[1]=StarTwinkle?StarDepth*.01f:0.f;Record.PostStarEffects[2]=StarRate;
    Record.PostStarEffects[3]=StarsScale>0&&StarTwinkle&&StarDepth>0?static_cast<float>(std::fmod(StarSeconds,100.0/static_cast<double>(StarRate))):0.f;
    const auto& P=Flare.Layers;
    Record.PostFlareUv[3]=Flare.CustomMix?1.0f:0.0f;
    Record.PostLayers[0]=Flare.Intensity;
    Record.PostLayers[1]=P.Spread;
    Record.PostLayers[2]=P.Anamorphic;
    Record.PostLayers[3]=P.Streaks;
    Record.PostLayers[4]=P.Burst;
    Record.PostLayers[5]=P.Rotation;
    Record.PostLayers[6]=P.RayPairs;
    Record.PostLayers[7]=static_cast<float>(Flare.GhostCount);
    Record.PostLayers[8]=P.GhostGain;
    Record.PostLayers[9]=P.GhostSpacing;
    Record.PostLayers[10]=P.GhostSides;
    Record.PostLayers[11]=P.HaloGain;
    Record.PostLayers[12]=Flare.HaloRadius;
    Record.PostLayers[13]=P.HaloWidth;
    Record.PostLayers[14]=Flare.Chromatic;
    Record.PostLayers[15]=Flare.StreakGain;
    return Record;
}

//------------------------------------------------------------------------------------------------------------------------

namespace {

const char* CompassOf(float Bearing) noexcept
{
    static const char* kCompass[16] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
    float B = std::fmod(Bearing, 360.0f);
    if (B < 0.0f) B += 360.0f;
    const int Slot = static_cast<int>(B / 22.5f + 0.5f) % 16;
    return kCompass[Slot];
}

const char* CloudTypeWord(CloudTypeCategory Type) noexcept
{
    switch (Type)
    {
    case CloudTypeCategory::Stratus:       return "Stratus";
    case CloudTypeCategory::Stratocumulus: return "Stratocumulus";
    case CloudTypeCategory::Cumulus:       return "Cumulus";
    case CloudTypeCategory::Cumulonimbus:  return "Cumulonimbus";
    case CloudTypeCategory::Altostratus:   return "Altostratus";
    case CloudTypeCategory::Cirrus:        return "Cirrus";
    default:                               return "Cloud";
    }
}

// Kasten–Young air mass, the reference panel's own formula.
float AirMassOf(float ElevationDegrees) noexcept
{
    const float Z = 90.0f - ElevationDegrees;
    if (Z >= 96.0f) return 40.0f;
    const float Am = 1.0f / (std::cos(Z * 0.01745329f) + 0.50572f * std::pow(96.07995f - Z, -1.6364f));
    return Am < 40.0f ? Am : 40.0f;
}

} // namespace

namespace {

constexpr CelestialEntity kOutlinerEntities[] = {
    CelestialEntity::Atmosphere,
    CelestialEntity::Sun,
    CelestialEntity::Sky,
    CelestialEntity::Stars,
    CelestialEntity::Moons,
    CelestialEntity::LensFlare,
    CelestialEntity::Wind,
    CelestialEntity::CloudLayer,
    CelestialEntity::Precipitation,
    CelestialEntity::LocalCloud,
    CelestialEntity::HeightFog,
    CelestialEntity::AtmosphericFog,
    CelestialEntity::LocalFog,
    CelestialEntity::Rainbow
};
constexpr uint32_t kOutlinerEntityCount = static_cast<uint32_t>(sizeof(kOutlinerEntities) / sizeof(kOutlinerEntities[0]));

} // namespace

uint32_t CelestialSequence::AppendRoster(EditorInstance* Instances, uint32_t Written, uint32_t Capacity) const noexcept
{
    if (Instances == nullptr || Written >= Capacity) return 0u;

    uint32_t Count = 0u;
    // The folder, then its entities one level deeper — the same preorder the scene rows use. The folder is the
    //    reference page's World: globe glyph, blue accent, pinned (no drag, no eye).
    EditorInstance& Folder = Instances[Written];
    Folder = EditorInstance{};Folder.InspectorKey=0x300000000ull;
    std::snprintf(Folder.Label, sizeof(Folder.Label), "Environment");
    Folder.Depth    = 0u;
    Folder.Category = EditorInstanceCategory::Folder;
    Folder.KidCount = 0u;
    Folder.Visible  = Enabled;
    Folder.Glyph    = EditorGlyph::Globe;
    Folder.Artwork  = IconSymbol::FolderEnvironment;
    Folder.Pinned   = false;
    Folder.Shut     = false;
    CopyTint(Folder.Tint, kTintFolder);
    ++Count;

    for (uint32_t I = 0; I < kOutlinerEntityCount; ++I)
    {
        if (Written + Count >= Capacity) break;
        const CelestialEntity Entity = kOutlinerEntities[I];
        const uint32_t EntityIdx = static_cast<uint32_t>(Entity);
        EditorInstance& Row = Instances[Written + Count];
        Row = EditorInstance{};Row.InspectorKey=0x200000000ull+EntityIdx+1;
        std::snprintf(Row.Label, sizeof(Row.Label), "%s", CelestialEntityName(Entity));
        Row.Depth   = Entity == CelestialEntity::Precipitation ? 2u : 1u;
        Row.Visible = Shown[EntityIdx];
        // A Light row for the sun (it is one), Geometry for the rest; the narrowing pills read the page's own
        //    groups (Sky for the medium, Bodies for the moons, Lights for the sun).
        Row.Category = (Entity == CelestialEntity::Sun) ? EditorInstanceCategory::Light
                                                        : EditorInstanceCategory::Geometry;
        Row.Narrowing = (Entity == CelestialEntity::Sun)   ? EditorNarrowing::Lights
                      : (Entity == CelestialEntity::Moons) ? EditorNarrowing::Bodies
                                                           : EditorNarrowing::Sky;
        Row.Dynamic = (Entity == CelestialEntity::Sun || Entity == CelestialEntity::Stars || Entity == CelestialEntity::Moons);
        RefreshRow(Entity, Row);
        ++Count;
        if(Entity == CelestialEntity::Precipitation) ++Instances[Written + Count - 2u].KidCount;
        else ++Folder.KidCount;
    }
    return Count;
}

// The page's ENTITIES colours and icons, and its metaShort / statusOf, from the live state. Called at fill and
//    every tick after (the metas move with the clock).
void CelestialSequence::RefreshRow(CelestialEntity Entity, EditorInstance& Row) const noexcept
{
    Row.Standing = EditorStanding::Auto;
    Row.StandingNote[0] = '\0';
    switch (Entity)
    {
    case CelestialEntity::Atmosphere:
        CopyTint(Row.Tint, kTintAtmosphere); Row.Glyph = EditorGlyph::Atmosphere;Row.Artwork=IconSymbol::SkyScattering;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "AM %.2f", static_cast<double>(AirMassOf(Solved.Sun.Elevation)));
        break;
    case CelestialEntity::Sun:
        CopyTint(Row.Tint, kTintSun); Row.Glyph = EditorGlyph::Sun; Row.Artwork = IconSymbol::Sun;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%.1f\xc2\xb0", static_cast<double>(Solved.Sun.Elevation));
        if (Solved.Sun.Elevation < -0.8f)
        {
            Row.Standing = EditorStanding::Warn;
            std::snprintf(Row.StandingNote, sizeof(Row.StandingNote), "Below horizon");
        }
        break;
    case CelestialEntity::Sky:
    {
        CopyTint(Row.Tint, kTintSky); Row.Glyph = EditorGlyph::Sky; Row.Artwork = IconSymbol::SkyScattering;
        const float E = std::sin(Solved.Sun.Elevation * 0.01745329f);
        const float Lum = (0.02f + 8.0f * std::pow(E > 0.0f ? E : 0.0f, 0.8f)) * SkyBrightness * Light.Intensity / 22.0f;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%.2f kcd", static_cast<double>(Lum));
        break;
    }
    case CelestialEntity::Stars:
    {
        CopyTint(Row.Tint, kTintStars); Row.Glyph = EditorGlyph::Stars; Row.Artwork = IconSymbol::OutlinerStars;
        // Naked-eye limit against the sky: 6.6 at night, falling with the sun's height.
        const float E = Solved.Sun.Elevation;
        const float Mag = E < -18.0f ? 6.6f : (E < 0.0f ? 6.6f + E * 0.35f : 0.3f - E * 0.12f);
        const float Lim = Mag < -4.5f ? -4.5f : Mag;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "mag %.1f", static_cast<double>(Lim));
        if (Lim < 0.0f)
        {
            Row.Standing = EditorStanding::Warn;
            std::snprintf(Row.StandingNote, sizeof(Row.StandingNote), "Washed out by sky");
        }
        break;
    }
    case CelestialEntity::Moons:
    {
        CopyTint(Row.Tint, kTintStars); Row.Glyph = EditorGlyph::Moon; Row.Artwork = IconSymbol::Moon;
        uint32_t Seated = 0u;
        for (uint32_t S = 0u; S < kMoonDrawCount; ++S) if (MoonSlots[S].Visible) ++Seated;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%u/%u", Seated, static_cast<uint32_t>(kMoonDrawCount));
        if (Seated > 0u && MoonSlots[0].FollowSky && Solved.Moon.Elevation <= 0.0f)
        {
            Row.Standing = EditorStanding::Warn;
            std::snprintf(Row.StandingNote, sizeof(Row.StandingNote), "Set");
        }
        break;
    }
    case CelestialEntity::HeightFog:
        CopyTint(Row.Tint, kTintFog); Row.Glyph = EditorGlyph::Fog; Row.Artwork = IconSymbol::Fog;
        if (!Fog.HeightEnabled) std::snprintf(Row.Meta, sizeof(Row.Meta), "off");
        else if(Fog.HeightDensity<=0)std::snprintf(Row.Meta,sizeof(Row.Meta),"clear");
        else std::snprintf(Row.Meta, sizeof(Row.Meta), "%.0f m",
                           static_cast<double>(std::sqrt(-std::log(0.02f)) / (Fog.HeightDensity > 1e-4f ? Fog.HeightDensity : 1e-4f)));
        break;
    case CelestialEntity::AtmosphericFog:
    {
        CopyTint(Row.Tint, kTintFog); Row.Glyph = EditorGlyph::AerialFog; Row.Artwork = IconSymbol::Fog;
        const float Beta=Fog.AerialDensity*((1-Fog.AerialMie)*Medium.RayleighScattering[1]*Medium.RayleighStrength+Fog.AerialMie*Medium.MieScattering*Medium.MieStrength);
        if(!Fog.AerialEnabled)std::snprintf(Row.Meta,sizeof(Row.Meta),"off");
        else if(Beta<=0)std::snprintf(Row.Meta,sizeof(Row.Meta),"clear");
        else std::snprintf(Row.Meta,sizeof(Row.Meta),"%.0f km",static_cast<double>((Fog.AerialStart+3.912f/Beta)/1000));
        break;
    }
    case CelestialEntity::LocalFog:
        CopyTint(Row.Tint, kTintFog); Row.Glyph = EditorGlyph::VolumeFog; Row.Artwork = IconSymbol::LocalFog;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%.0f\xc3\x97%.0f m",
                      static_cast<double>(LocalFog.HalfSize[0] * 2.0f), static_cast<double>(LocalFog.HalfSize[1] * 2.0f));
        break;
    case CelestialEntity::CloudLayer:
        CopyTint(Row.Tint, kTintCloud); Row.Glyph = EditorGlyph::VolumeClouds; Row.Artwork = IconSymbol::Clouds;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%s \xc2\xb7 %.0f%%", CloudTypeWord(Cloud.Type), static_cast<double>(Cloud.Coverage * 100.0f));
        break;
    case CelestialEntity::LocalCloud:
        CopyTint(Row.Tint, kTintCloud); Row.Glyph = EditorGlyph::LocalCloud; Row.Artwork = IconSymbol::LocalCloud;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%.0f\xc3\x97%.0f m",
                      static_cast<double>(LocalCloud.HalfSize[0] * 2.0f), static_cast<double>(LocalCloud.HalfSize[1] * 2.0f));
        break;
    case CelestialEntity::Wind:
        CopyTint(Row.Tint, kTintWind); Row.Glyph = EditorGlyph::Wind; Row.Artwork = IconSymbol::Wind;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%.1f m/s %s", static_cast<double>(Wind.Speed), CompassOf(Wind.Bearing));
        break;
    case CelestialEntity::Precipitation:
        CopyTint(Row.Tint, kTintPrecip); Row.Glyph = EditorGlyph::Rain; Row.Artwork = IconSymbol::OutlinerPrecipitation;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%s %.0f mm/h",
                      Precip.Category == PrecipitationCategory::Rain ? "Rain"
                    : Precip.Category == PrecipitationCategory::Drizzle ? "Drizzle"
                    : Precip.Category == PrecipitationCategory::Hail ? "Hail"
                    : Precip.Category == PrecipitationCategory::Sleet ? "Sleet" : "Snow",
                      static_cast<double>(Precip.RateMillimetresPerHour));
        break;
    case CelestialEntity::Rainbow:
        CopyTint(Row.Tint, kTintOptics); Row.Glyph = EditorGlyph::Rainbow; Row.Artwork = IconSymbol::Rainbow;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%.0f%%", static_cast<double>(Rainbow.Intensity * 100.0f));
        break;
    case CelestialEntity::LensFlare:
    default:
        CopyTint(Row.Tint, kTintOptics); Row.Glyph = EditorGlyph::Flare; Row.Artwork = IconSymbol::LensFlare;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%u ghosts", Flare.GhostCount);
        break;
    }
}

void CelestialSequence::RefreshRoster(EditorInstance* Instances, uint32_t FirstRow, uint32_t InstanceCount) const noexcept
{
    if (Instances == nullptr) return;
    for (uint32_t I = 0u; I < kOutlinerEntityCount; ++I)
    {
        const uint32_t Row = FirstRow + 1u + I;
        if (Row >= InstanceCount) break;
        const CelestialEntity Entity = kOutlinerEntities[I];
        if (std::strcmp(Instances[Row].Label, CelestialEntityName(Entity)) != 0) continue;
        RefreshRow(Entity, Instances[Row]);
    }
}

bool CelestialSequence::Owns(uint32_t RosterIndex, uint32_t FirstRow, CelestialEntity& Entity) const noexcept
{
    // FirstRow is the folder; the entities follow it.
    if (RosterIndex <= FirstRow) return false;
    const uint32_t Offset = RosterIndex - FirstRow - 1u;
    if (Offset >= kOutlinerEntityCount) return false;
    Entity = kOutlinerEntities[Offset];
    return true;
}

//------------------------------------------------------------------------------------------------------------------------

void CelestialSequence::BuildSheet(CelestialEntity Entity, EditorSheet& Sheet) const noexcept
{
    // Clear in place, one property at a time: do not materialize a whole sheet on the stack.
    Sheet.Appearance = EditorSheetAppearance::Generic;
    Sheet.SkyImage = {};Sheet.StarPreview={};Sheet.FogPreview={};Sheet.WeatherPreview={};
    for(auto& Body:Sheet.MoonBodies)Body=EditorMoonBody{};Sheet.MoonAtlasRevision=0;
    Sheet.MoonAzimuth=Sheet.MoonElevation=Sheet.MoonPhase=0;
    Sheet.GroupCount = 0;
    for (auto& Group : Sheet.Groups)
    {
        Group.Title[0] = Group.Caption[0] = '\0';
        Group.PropertyCount = 0;
        Group.StackedLabels = Group.Clock24 = false;
        for (auto& Prop : Group.Properties) Prop = EditorProperty{};
    }

    if (Entity == CelestialEntity::Sun) BuildSunSheet(Sheet);
    else if (Entity == CelestialEntity::LensFlare) BuildFlareSheet(Sheet);
    else if (Entity == CelestialEntity::Atmosphere || Entity == CelestialEntity::Sky) BuildAtmosphereSkySheet(Sheet);
    else if (Entity == CelestialEntity::Moons) BuildMoonSheet(Sheet);
    else if (Entity == CelestialEntity::Stars) BuildStarsSheet(Sheet);
    else if(Entity==CelestialEntity::Wind)BuildWindSheet(Sheet);
    else if(Entity==CelestialEntity::Precipitation)BuildPrecipitationSheet(Sheet);
    else if(Entity==CelestialEntity::Rainbow)BuildRainbowSheet(Sheet);
    else if(Entity==CelestialEntity::HeightFog||Entity==CelestialEntity::AtmosphericFog||Entity==CelestialEntity::LocalFog)BuildFogSheet(Entity,Sheet);
    else if (Entity == CelestialEntity::CloudLayer || Entity == CelestialEntity::LocalCloud) BuildCloudSheet(Entity==CelestialEntity::LocalCloud,Sheet);
    else BuildOtherSheet(Entity, Sheet);
}

void CelestialSequence::BuildSunSheet(EditorSheet& Sheet) const noexcept
{
    Sheet.Appearance = EditorSheetAppearance::Sun;
    char Text[48];
        EditorPropertyGroup& Bake = OpenGroup(Sheet, "Bake / image");
        std::snprintf(Bake.Caption, sizeof(Bake.Caption), "Sun-specific Bake and Use baked image are not supported. The existing bake belongs to Sky, not Sun.");
        EditorPropertyGroup& Disk = OpenGroup(Sheet, "Sun Disk");
        Push(Disk, MakeSlider("Angular Diameter", 0.1f, 5.0f, SunDiskSize, 2, "deg"));
        Push(Disk, MakeColour("Sun Tint", SunUseTemperature ? SunRgbTint : Light.Colour));
        static const char* const ColourSources[] = { "RGB tint", "Temperature" };
        Push(Disk, MakeSelect("Colour source", ColourSources, 2u, SunUseTemperature ? 1u : 0u));
        Push(Disk, MakeSlider("Temperature", SunColourTemperature::MinimumKelvin,
            SunColourTemperature::MaximumKelvin, SunTemperatureKelvin, 0, "K"));
        EditorPropertyGroup& Radiance = OpenGroup(Sheet, "Sunlight gain");
        std::snprintf(Radiance.Caption, sizeof(Radiance.Caption), "Native multipliers, not lux. Colour source selects manual RGB tint or Kelvin temperature.");
        Push(Radiance, MakeSlider("Intensity", 0.0f, 60.0f, Light.Intensity, 1, "x"));
        Push(Radiance, MakeSlider("Direct", 0.0f, 5.0f, SunDirect, 2, "x"));

        EditorPropertyGroup& When = OpenGroup(Sheet, "Day cycle");
        When.Clock24 = true;
        std::snprintf(When.Caption, sizeof(When.Caption), "24-hour local clock, not a manual direction control. Use the native slider to change time.");
        Push(When, MakeSlider("Local Hours", 0.0f, 24.0f, Observation.LocalHours, 2, "h"));
        Push(When, MakeSwitch("Animate", Clock.Animate));
        Push(When, MakeSlider("Day duration", 0.01f, 168.0f, 24.0f / Clock.SpeedTimes, 2, "h"));
        {
            static const char* const Speeds[] = { "x1", "x8", "x30", "x100", "Custom" };
            const float Rates[4] = { 1.0f, 8.0f, 30.0f, 100.0f };
            uint32_t Picked = 4u;
            for (uint32_t I = 0; I < 4u; ++I) if (std::fabs(Clock.SpeedTimes - Rates[I]) < 0.01f) Picked = I;
            Push(When, MakeSelect("Speed", Speeds, 5u, Picked));
        }

        EditorPropertyGroup& Where = OpenGroup(Sheet, "Observer / date");
        Push(Where, MakeSlider("Latitude", -90.0f, 90.0f, Observation.Latitude, 2, "deg"));
        Push(Where, MakeSlider("Longitude", -180.0f, 180.0f, Observation.Longitude, 2, "deg"));
        Push(Where, MakeSlider("Day of Month", 1.0f, 31.0f, static_cast<float>(Observation.Day), 0, ""));
        Push(Where, MakeSlider("Month", 1.0f, 12.0f, static_cast<float>(Observation.Month), 0, ""));

        // Read-outs rather than sliders: these are SOLVED, and offering to edit them would imply the solver
        //    could be overridden, which it cannot.
        EditorPropertyGroup& Live = OpenGroup(Sheet, "Solved direction");
        std::snprintf(Live.Caption, sizeof(Live.Caption), "Read-only astronomy. Manual azimuth/elevation and independent disk/light switches are not exposed.");
        std::snprintf(Text, sizeof(Text), "%+.2f deg", static_cast<double>(Solved.Sun.Elevation));
        Push(Live, MakeReadout("Elevation", Text));
        Live.Properties[0].Figure = Solved.Sun.Elevation;
        std::snprintf(Text, sizeof(Text), "%.1f deg", static_cast<double>(Solved.Sun.Azimuth));
        Push(Live, MakeReadout("Azimuth", Text));
        Live.Properties[1].Figure = Solved.Sun.Azimuth;
        std::snprintf(Text, sizeof(Text), "%+.2f deg", static_cast<double>(Solved.Declination));
        Push(Live, MakeReadout("Declination", Text));
        std::snprintf(Text, sizeof(Text), "%+.1f min", static_cast<double>(Solved.EquationOfTime));
        Push(Live, MakeReadout("Equation of Time", Text));
        for (uint32_t I = 0; I < Sheet.GroupCount; ++I) Sheet.Groups[I].StackedLabels = true;
}

void CelestialSequence::BuildStarsSheet(EditorSheet& Sheet) const noexcept
{
    char Text[48];
        EditorPropertyGroup& Field = OpenGroup(Sheet, "Field");
        Push(Field, MakeSlider("Brightness", 0.0f, 4.0f, StarBrightness, 2, "x"));
        Push(Field, MakeSlider("Point Size", 0.4f, 3.0f, StarSize, 2, "x"));
        Sheet.Appearance=EditorSheetAppearance::Stars;
        Push(Field,MakeSwitch("Star field",Shown[static_cast<uint32_t>(CelestialEntity::Stars)]));
        Sheet.StarPreview={Catalogue.QuerySourceStars().data(),Catalogue.QuerySourceCount(),static_cast<float>(std::fmod(StarSeconds,100.0/static_cast<double>(StarRate)))};
        Push(Field,MakeSlider("Limiting magnitude",0,8,StarMagnitude,1,"mag"));
        Push(Field,MakeSwitch("Twinkle",StarTwinkle));
        Push(Field,MakeSlider("Twinkle depth",0,100,StarDepth,0,"%"));
        Push(Field,MakeSlider("Twinkle rate",.2f,3,StarRate,2,"Hz"));
        Push(Field,MakeSlider("Celestial rotation",0,360,StarRotation,1,"deg"));
        EditorPropertyGroup& Live = OpenGroup(Sheet, "Catalogue");
        std::snprintf(Text, sizeof(Text), "%u stars", Catalogue.QuerySourceCount());
        Push(Live, MakeReadout("Loaded", Text));
        std::snprintf(Text, sizeof(Text), "%.1f deg", static_cast<double>(Solved.LocalSiderealTime));
        Push(Live, MakeReadout("Sidereal Time", Text));
}

void CelestialSequence::BuildCloudShadowSheet(EditorSheet& Sheet) const noexcept
{
        // The GPU shadow weather (CloudShadow.slang): the staged instant the level owner seated at load —
        //    FIN3 diorama deck on the showcase, panel kilometre deck elsewhere. Labels carry the Shadow
        //    prefix because sheet reads are by label: bare "Coverage"/"Type" would land on Shape's rows.
        //    Every edit restarts the accumulation through the ④d record compare, so the shade lands at once.
        EditorPropertyGroup& Shade = OpenGroup(Sheet, "Cloud Shadows");
        Push(Shade, MakeSwitch("Shadow Enabled", ShadowStaging.Enabled));
        {
            static const char* const Types[] = { "Stratus", "Stratocumulus", "Cumulus", "Cumulonimbus", "Altostratus", "Cirrus" };
            Push(Shade, MakeSelect("Shadow Type", Types, 6u, ShadowStaging.Type < 6u ? ShadowStaging.Type : 2u));
        }
        Push(Shade, MakeSlider("Shadow Coverage", 0.0f, 1.0f, ShadowStaging.Coverage, 2, ""));
        Push(Shade, MakeSlider("Shadow Density", 0.0f, 6.0f, ShadowStaging.Density, 2, "x"));
        Push(Shade, MakeSlider("Shadow Scale", 0.01f, 1.0f, ShadowStaging.Scale, 3, ""));
        Push(Shade, MakeSlider("Shadow Anvil", 0.0f, 1.0f, ShadowStaging.Anvil, 2, ""));
        Push(Shade, MakeSlider("Shadow Base", 0.0f, 3000.0f, ShadowStaging.Base, 0, "m"));
        Push(Shade, MakeSlider("Shadow Thick", 50.0f, 3000.0f, ShadowStaging.Thickness, 0, "m"));
        Push(Shade, MakeSlider("Shadow Ceil", 4000.0f, 20000.0f, ShadowStaging.CeilingMetres, 0, "m"));

        // The weather instant + the drift wind: the scrub slider 0005 promised. Time is seconds since local
        //    midnight (FIN3 froze t = 40 s); scrubbing re-folds the drift and re-converges the frame.
        EditorPropertyGroup& ShadeClock = OpenGroup(Sheet, "Shadow Clock");
        Push(ShadeClock, MakeSlider("Shadow Time", 0.0f, 86399.0f, ShadowTimeSeconds, 0, "s"));
        Push(ShadeClock, MakeSlider("Shadow Wind", 0.0f, 40.0f, ShadowStaging.WindSpeed, 1, "m/s"));
        Push(ShadeClock, MakeSlider("Shadow Dir", 0.0f, 360.0f, ShadowStaging.WindBearing, 0, "deg"));
}
void CelestialSequence::BuildCloudSheet(bool Local,EditorSheet& Sheet) const noexcept
{
    Sheet.Appearance=Local?EditorSheetAppearance::LocalCloud:EditorSheetAppearance::GlobalCloud;
    if(Local){
        EditorPropertyGroup& Transform = OpenGroup(Sheet, "Transform");
        Push(Transform, MakeSwitch("Enabled", LocalCloud.Enabled));
        Push(Transform, MakeAxes("Centre", LocalCloud.Centre, 1.0f));
        Push(Transform, MakeAxes("Half Size", LocalCloud.HalfSize, 1.0f));

        EditorPropertyGroup& Body = OpenGroup(Sheet, "Body");
        Push(Body, MakeSlider("Density", 0.0f, 4.0f, LocalCloud.Density, 2, "x"));
        Push(Body, MakeSlider("Coverage", 0.0f, 1.0f, LocalCloud.Coverage, 2, ""));
        Push(Body, MakeSlider("Feature Scale", 10.0f, 600.0f, LocalCloud.Scale, 0, "m"));
        Push(Body, MakeSlider("Anisotropy", -0.9f, 0.9f, LocalCloud.Anisotropy, 2, ""));
        Push(Body, MakeSwitch("Follow Wind", LocalCloud.FollowWind));
        return;
    }
    char Text[48];
        EditorPropertyGroup& Shape = OpenGroup(Sheet, "Shape");
        Push(Shape, MakeSwitch("Enabled", Cloud.Enabled));
        {
            static const char* const Types[] = { "Stratus", "Stratocumulus", "Cumulus", "Cumulonimbus", "Altostratus", "Cirrus" };
            Push(Shape, MakeSelect("Type", Types, 6u, static_cast<uint32_t>(Cloud.Type)));
        }
        Push(Shape, MakeSlider("Coverage", 0.0f, 1.0f, Cloud.Coverage, 2, ""));
        Push(Shape, MakeSlider("Density", 0.0f, 4.0f, Cloud.Density, 2, "x"));
        Push(Shape, MakeSlider("Feature Scale", 0.2f, 3.0f, Cloud.Scale, 2, "x"));

        EditorPropertyGroup& Slab = OpenGroup(Sheet, "Altitude");
        // Ranges stop at the ceiling rather than above it, so no slider can propose a cloud in orbit.
        Push(Slab, MakeSlider("Base", 100.0f, Cloud.CeilingMetres, Cloud.Base, 0, "m"));
        Push(Slab, MakeSlider("Thickness", 100.0f, 6000.0f, Cloud.Thickness, 0, "m"));
        Push(Slab, MakeSlider("Ceiling", 4000.0f, 20000.0f, Cloud.CeilingMetres, 0, "m"));
        Push(Slab, MakeSwitch("Follow Wind", Cloud.FollowWind));

        EditorPropertyGroup& Spend = OpenGroup(Sheet, "Tier Budget");
        std::snprintf(Text, sizeof(Text), "%u steps", Budget.Volumetrics.CloudSteps);
        Push(Spend, MakeReadout("Cloud March", Text));
        std::snprintf(Text, sizeof(Text), "%u taps", Budget.Volumetrics.LightTaps);
        Push(Spend, MakeReadout("Light Taps", Text));

    Push(Shape,MakeSlider("Anvil",0,1,Cloud.Anvil,2,""));
    Push(Shape,MakeSlider("Anisotropy",-.9f,.9f,Cloud.Anisotropy,2,""));
    BuildCloudShadowSheet(Sheet);
}

void CelestialSequence::BuildFogSheet(CelestialEntity Entity,EditorSheet& Sheet) const noexcept
{
    Sheet.Appearance=Entity==CelestialEntity::HeightFog?EditorSheetAppearance::HeightFog:Entity==CelestialEntity::AtmosphericFog?EditorSheetAppearance::AerialFog:EditorSheetAppearance::LocalFog;
    for(int C=0;C<3;++C)Sheet.FogPreview.Rayleigh[C]=Medium.RayleighScattering[C]*Medium.RayleighStrength;
    Sheet.FogPreview.Mie=Medium.MieScattering*Medium.MieStrength;Sheet.FogPreview.RayleighHeight=Medium.RayleighScaleHeight;Sheet.FogPreview.MieHeight=Medium.MieScaleHeight;
    switch(Entity){
    case CelestialEntity::HeightFog:
    {
        EditorPropertyGroup& Medium2 = OpenGroup(Sheet, "Medium");
        Push(Medium2, MakeSwitch("Enabled", Fog.HeightEnabled));
        Push(Medium2, MakeSlider("Density", 0.0f, 0.2f, Fog.HeightDensity, 4, "1/m"));
        Push(Medium2, MakeSlider("Falloff Height", 10.0f, 3000.0f, Fog.FalloffHeight, 0, "m"));
        Push(Medium2, MakeSlider("Sun Scatter", 0.0f, 2.0f, Fog.SunScatter, 2, "x"));
        Push(Medium2, MakeColour("Colour", Fog.HeightColour));
        break;
    }
    case CelestialEntity::AtmosphericFog:
    {
        EditorPropertyGroup& Aerial = OpenGroup(Sheet, "Aerial Perspective");
        Push(Aerial, MakeSwitch("Enabled", Fog.AerialEnabled));
        Push(Aerial, MakeSlider("Density", 0.0f, 4.0f, Fog.AerialDensity, 2, "x"));
        Push(Aerial, MakeSlider("Start", 0.0f, 2000.0f, Fog.AerialStart, 0, "m"));
        Push(Aerial, MakeSlider("Mie Blend", 0.0f, 1.0f, Fog.AerialMie, 2, ""));
        break;
    }
    case CelestialEntity::LocalFog:
    {
        const LocalVolumeSettings& V = LocalFog;
        EditorPropertyGroup& Transform = OpenGroup(Sheet, "Transform");
        Push(Transform, MakeSwitch("Enabled", V.Enabled));
        Push(Transform, MakeAxes("Centre", V.Centre, 1.0f));
        Push(Transform, MakeAxes("Half Size", V.HalfSize, 1.0f));

        EditorPropertyGroup& Body = OpenGroup(Sheet, "Body");
        Push(Body, MakeSlider("Density", 0.0f, 4.0f, V.Density, 2, "x"));
        Push(Body, MakeSlider("Coverage", 0.0f, 1.0f, V.Coverage, 2, ""));
        Push(Body, MakeSlider("Feature Scale", 10.0f, 600.0f, V.Scale, 0, "m"));
        Push(Body, MakeSlider("Anisotropy", -0.9f, 0.9f, V.Anisotropy, 2, ""));
        Push(Body, MakeSwitch("Follow Wind", V.FollowWind));
        break;
    }
    default:break;
    }
}

void CelestialSequence::BuildWindSheet(EditorSheet& Sheet) const noexcept
{
    Sheet.Appearance=EditorSheetAppearance::Wind;
 char Text[48];
    auto& P=Sheet.WeatherPreview;
    P.Wind[0]=Wind.Speed;P.Wind[1]=Wind.Bearing;P.Wind[2]=Wind.Shear;P.Wind[3]=Wind.Veer;P.Wind[4]=Wind.Gust;P.Wind[5]=Wind.Turbulence;P.Wind[6]=Wind.Steadiness;P.Wind[7]=Wind.GustPhase;
    P.Alive=Rain.Telemetry().Alive;P.SnowDepth=Rain.Field().DeepestMetres();P.AboveWeather=Rain.Telemetry().AboveWeather;P.RainVisibility=WeatherDiagnostics::RainVisibility(Precip,Enabled&&Shown[static_cast<uint32_t>(CelestialEntity::Precipitation)]);

        EditorPropertyGroup& Flow = OpenGroup(Sheet, "Flow");
        Push(Flow, MakeSlider("Speed", 0.0f, 40.0f, Wind.Speed, 1, "m/s"));
        Push(Flow, MakeSlider("Bearing", 0.0f, 360.0f, Wind.Bearing, 0, "deg"));
        Push(Flow, MakeSlider("Shear", 0.0f, 2.0f, Wind.Shear, 2, "/km"));
        Push(Flow, MakeSlider("Veer", -60.0f, 60.0f, Wind.Veer, 0, "d/km"));

        EditorPropertyGroup& Gust = OpenGroup(Sheet, "Gust");
        Push(Gust, MakeSlider("Gust", 0.0f, 1.0f, Wind.Gust, 2, ""));
        Push(Gust, MakeSlider("Turbulence", 0.0f, 1.0f, Wind.Turbulence, 2, ""));
        Push(Gust, MakeSlider("Steadiness", 0.0f, 1.0f, Wind.Steadiness, 2, ""));

        EditorPropertyGroup& Live = OpenGroup(Sheet, "Beaufort");
        const uint32_t Force = WindField::BeaufortForce(Wind.Speed);
        std::snprintf(Text, sizeof(Text), "%u %s", Force, WindField::BeaufortName(Force));
        Push(Live, MakeReadout("Force", Text));
}

void CelestialSequence::BuildPrecipitationBehaviour(EditorSheet& Sheet) const noexcept
{
 char Text[48];
        EditorPropertyGroup& Drift = OpenGroup(Sheet, "Wind");
        Push(Drift, MakeSlider("Wind Drift", 0.0f, 1.0f, Precip.WindDrift, 2, ""));
        Push(Drift, MakeSwitch("Follow Wind", Precip.FollowWind));
        Push(Drift, MakeSwitch("Spawn from Clouds", Precip.SpawnFromClouds));

        EditorPropertyGroup& Land = OpenGroup(Sheet, "Collision");
        Push(Land, MakeSwitch("Ground Collision", Precip.GroundCollision));
        Push(Land, MakeSlider("Accumulation", 0.0f, 1.0f, Precip.Accumulation, 2, ""));

        EditorPropertyGroup& Live = OpenGroup(Sheet, "Live");
        std::snprintf(Text, sizeof(Text), "%u alive", Rain.Telemetry().Alive);
        Push(Live, MakeReadout("Particles", Text));
        std::snprintf(Text, sizeof(Text), "%.3f m", static_cast<double>(Rain.Field().DeepestMetres()));
        Push(Live, MakeReadout("Snow Depth", Text));
}
void CelestialSequence::BuildPrecipitationSheet(EditorSheet& Sheet) const noexcept
{
    Sheet.Appearance=EditorSheetAppearance::Precipitation;
    auto& P=Sheet.WeatherPreview;
    P.Wind[0]=Wind.Speed;P.Wind[1]=Wind.Bearing;P.Wind[2]=Wind.Shear;P.Wind[3]=Wind.Veer;P.Wind[4]=Wind.Gust;P.Wind[5]=Wind.Turbulence;P.Wind[6]=Wind.Steadiness;P.Wind[7]=Wind.GustPhase;
    P.Alive=Rain.Telemetry().Alive;P.SnowDepth=Rain.Field().DeepestMetres();P.AboveWeather=Rain.Telemetry().AboveWeather;P.RainVisibility=WeatherDiagnostics::RainVisibility(Precip,Enabled&&Shown[static_cast<uint32_t>(CelestialEntity::Precipitation)]);

        EditorPropertyGroup& Kind = OpenGroup(Sheet, "Type");
        Push(Kind, MakeSwitch("Enabled", Precip.Enabled));
        {
            static const char* const Types[] = { "Rain", "Drizzle", "Hail", "Snow", "Sleet" };
            Push(Kind, MakeSelect("Precipitation", Types, 5u, static_cast<uint32_t>(Precip.Category)));
        }
        Push(Kind, MakeSlider("Intensity", 0.0f, 100.0f, Precip.RateMillimetresPerHour, 1, "mm/h"));
        Push(Kind, MakeSlider("Density", 0.1f, 6.0f, Precip.Density, 2, "x"));
        Push(Kind, MakeSlider("Particle Size", 0.3f, 3.0f, Precip.SizeScale, 2, "x"));

        BuildPrecipitationBehaviour(Sheet);
}

void CelestialSequence::BuildRainbowSheet(EditorSheet& Sheet) const noexcept
{
    Sheet.Appearance=EditorSheetAppearance::Rainbow;
    auto& P=Sheet.WeatherPreview;
    P.Wind[0]=Wind.Speed;P.Wind[1]=Wind.Bearing;P.Wind[2]=Wind.Shear;P.Wind[3]=Wind.Veer;P.Wind[4]=Wind.Gust;P.Wind[5]=Wind.Turbulence;P.Wind[6]=Wind.Steadiness;P.Wind[7]=Wind.GustPhase;
    P.Alive=Rain.Telemetry().Alive;P.SnowDepth=Rain.Field().DeepestMetres();P.AboveWeather=Rain.Telemetry().AboveWeather;P.RainVisibility=WeatherDiagnostics::RainVisibility(Precip,Enabled&&Shown[static_cast<uint32_t>(CelestialEntity::Precipitation)]);

        EditorPropertyGroup& Bow = OpenGroup(Sheet, "Bow");
        Push(Bow, MakeSwitch("Enabled", Rainbow.Enabled));
        Push(Bow, MakeSlider("Intensity", 0.0f, 3.0f, Rainbow.Intensity, 2, "x"));
        Push(Bow, MakeSlider("Width", 0.1f, 4.0f, Rainbow.Width, 2, "x"));
        Push(Bow, MakeSlider("Secondary", 0.0f, 1.0f, Rainbow.SecondaryGain, 2, ""));
        Push(Bow, MakeSwitch("Alexander's Band", Rainbow.AlexanderBand));
        Push(Bow, MakeSlider("Minimum Path", 1.0f, 2000.0f, Rainbow.MinimumPathMetres, 0, "m"));
}

void CelestialSequence::BuildOtherSheet(CelestialEntity, EditorSheet&) const noexcept {}

//------------------------------------------------------------------------------------------------------------------------

void CelestialSequence::ApplySheet(CelestialEntity Entity, const EditorSheet& Sheet) noexcept
{
    switch (Entity)
    {
    case CelestialEntity::Atmosphere:
    case CelestialEntity::Sky:
    {
        auto Scalar=[&](const char* Name,float Old,float Lo,float Hi){float V=ReadSlider(Sheet,Name,Old);return std::isfinite(V)?std::clamp(V,Lo,Hi):Old;};
        Medium.RayleighStrength    = Scalar("Rayleigh", Medium.RayleighStrength, 0.0f, 4.0f);
        Medium.MieStrength         = Scalar("Mie", Medium.MieStrength, 0.0f, 6.0f);
        Medium.MieAnisotropy       = Scalar("Mie Anisotropy", Medium.MieAnisotropy, 0.0f, 0.99f);
        Medium.OzoneStrength       = Scalar("Ozone", Medium.OzoneStrength, 0.0f, 4.0f);
        Medium.RayleighScaleHeight = Scalar("Rayleigh Scale H", Medium.RayleighScaleHeight, 2000.0f, 15000.0f);
        Medium.MieScaleHeight      = Scalar("Mie Scale H", Medium.MieScaleHeight, 300.0f, 4000.0f);
        Medium.AtmosphereHeight    = Scalar("Atmosphere", Medium.AtmosphereHeight, 10000.0f, 120000.0f);
        Twilight.GlowIntensity     = Scalar("Horizon Glow", Twilight.GlowIntensity, 0.0f, 3.0f);
        Twilight.LineIntensity     = Scalar("White Line", Twilight.LineIntensity, 0.0f, 3.0f);
        Twilight.LineAtCivilOnly   = ReadSwitch(Sheet, "Line at civil only", Twilight.LineAtCivilOnly);
        const EditorProperty* Tint = Find(Sheet, "Sky Tint");
        if (Tint != nullptr) for (int C = 0; C < 3; ++C) if(std::isfinite(Tint->ColourTint[C])) SkyTint[C] = Tint->ColourTint[C];
        SkyBrightness = Scalar("Sky Brightness", SkyBrightness, 0.0f, 3.0f);
        const EditorProperty* Ground = Find(Sheet, "Ground Albedo");
        if (Ground != nullptr) for (int C = 0; C < 3; ++C) if(std::isfinite(Ground->ColourTint[C])) GroundAlbedo[C] = Ground->ColourTint[C];
        SkyDomeBaked = ReadSwitch(Sheet, "Fetch Baked Dome", SkyDomeBaked);
        if(Sheet.SkyImage.RequestBake) SkyPreviewBakeRequested=true;
        break;
    }
    case CelestialEntity::Sun:
    {
        SunDiskSize            = ReadSlider(Sheet, "Angular Diameter", SunDiskSize);
        const EditorProperty* SunTint = Find(Sheet, "Sun Tint");
        float Kelvin = ReadSlider(Sheet, "Temperature", SunTemperatureKelvin);
        if (!std::isfinite(Kelvin)) Kelvin = SunTemperatureKelvin;
        Kelvin = SunColourTemperature::Clamp(Kelvin);
        const uint32_t RequestedSource = ReadSelect(Sheet, "Colour source", SunUseTemperature ? 1u : 0u);
        const bool RequestedTemperature = RequestedSource <= 1u ? RequestedSource == 1u : SunUseTemperature;
        bool TintChanged = false, TintFinite = SunTint != nullptr;
        for (int C = 0; C < 3 && SunTint != nullptr; ++C)
        {
            TintFinite = TintFinite && std::isfinite(SunTint->ColourTint[C]);
            TintChanged = TintChanged || SunTint->ColourTint[C] != (SunUseTemperature ? SunRgbTint[C] : Light.Colour[C]);
        }
        bool NextTemperature = SunUseTemperature;
        // Explicit manual edits win; then the selector; then a changed Kelvin value.
        if (TintFinite && TintChanged) NextTemperature = false;
        else if (RequestedTemperature != SunUseTemperature) NextTemperature = RequestedTemperature;
        else if (Kelvin != SunTemperatureKelvin) NextTemperature = true;
        if (!SunUseTemperature)
            for (int C = 0; C < 3; ++C) SunRgbTint[C] = Light.Colour[C];
        if (TintFinite && TintChanged)
            for (int C = 0; C < 3; ++C) SunRgbTint[C] = SunTint->ColourTint[C];
        SunUseTemperature = NextTemperature;
        SunTemperatureKelvin = Kelvin;
        if (SunUseTemperature)
        {
            const auto Tint = SunColourTemperature::LinearRgb(SunTemperatureKelvin);
            for (int C = 0; C < 3; ++C) Light.Colour[C] = Tint[C];
        }
        else for (int C = 0; C < 3; ++C) Light.Colour[C] = SunRgbTint[C];
        Observation.LocalHours = ReadSlider(Sheet, "Local Hours", Observation.LocalHours);
        Clock.Animate          = ReadSwitch(Sheet, "Animate", Clock.Animate);
        const float Rates[4] = { 1.0f, 8.0f, 30.0f, 100.0f };
        const float CurrentDuration = 24.0f / Clock.SpeedTimes;
        const float Duration = ReadSlider(Sheet, "Day duration", CurrentDuration);
        uint32_t CurrentPreset = 4u;
        for (uint32_t I=0; I<4u; ++I) if (std::fabs(Clock.SpeedTimes-Rates[I])<0.01f) CurrentPreset=I;
        const uint32_t Preset = ReadSelect(Sheet, "Speed", CurrentPreset);
        if (std::isfinite(Duration) && Duration != CurrentDuration)
            Clock.SpeedTimes = 24.0f / std::clamp(Duration, 0.01f, 168.0f);
        else if (Preset < 4u && Preset != CurrentPreset) Clock.SpeedTimes = Rates[Preset];
        Observation.Latitude   = ReadSlider(Sheet, "Latitude", Observation.Latitude);
        Observation.Longitude  = ReadSlider(Sheet, "Longitude", Observation.Longitude);
        Observation.Day        = static_cast<int32_t>(ReadSlider(Sheet, "Day of Month", static_cast<float>(Observation.Day)));
        Observation.Month      = static_cast<int32_t>(ReadSlider(Sheet, "Month", static_cast<float>(Observation.Month)));
        Light.Intensity        = ReadSlider(Sheet, "Intensity", Light.Intensity);
        SunDirect              = ReadSlider(Sheet, "Direct", SunDirect);
        break;
    }
    case CelestialEntity::Stars:
        {
        const auto Bounded=[&](const char* Name,float Old,float Low,float High){float V=ReadSlider(Sheet,Name,Old);return std::isfinite(V)?std::clamp(V,Low,High):Old;};
        StarBrightness=Bounded("Brightness",StarBrightness,0,4);StarSize=Bounded("Point Size",StarSize,.4f,3);
        StarMagnitude=Bounded("Limiting magnitude",StarMagnitude,0,8);StarDepth=Bounded("Twinkle depth",StarDepth,0,100);
        StarRate=Bounded("Twinkle rate",StarRate,.2f,3);StarRotation=Bounded("Celestial rotation",StarRotation,0,360);
        StarTwinkle=ReadSwitch(Sheet,"Twinkle",StarTwinkle);
        Shown[static_cast<uint32_t>(CelestialEntity::Stars)]=ReadSwitch(Sheet,"Star field",Shown[static_cast<uint32_t>(CelestialEntity::Stars)]);
        }
        break;
    case CelestialEntity::Moons:
    {
        char Label[28];
        auto Finite=[&](const char* Name,float Previous,float Low,float High){float V=ReadSlider(Sheet,Name,Previous);return std::isfinite(V)?std::clamp(V,Low,High):Previous;};
        for (uint32_t S = 0u; S < kMoonDrawCount; ++S)
        {
            MoonSlotState& M = MoonSlots[S];
            MoonPropLabel(S, "Visible", Label, sizeof(Label));
            M.Visible = ReadSwitch(Sheet, Label, M.Visible);
            MoonPropLabel(S, "Preset", Label, sizeof(Label));
            const uint32_t Picked = ReadSelect(Sheet, Label, M.Preset);
            // A new body is a fresh body: the size resets to the preset's own, the way the panel's
            //    applyPreset re-skins it — and the sheet's stale size slider is NOT read back over the reset,
            //    because the sheet was built for the body just replaced. Haze, tilt and gamma need no reset
            //    (the resolver reads them from the atlas live), and bright, glow and phase are the slot's own
            //    and survive the switch.
            const bool Reskinned = Picked < kMoonAtlasCount && Picked != M.Preset;
            if (Reskinned)
            {
                M.Preset = Picked;
                M.Size = kMoonAtlas[Picked].SizeDegrees;
            }
            MoonPropLabel(S, "Follow Sky", Label, sizeof(Label));
            M.FollowSky = ReadSwitch(Sheet, Label, M.FollowSky);
            MoonPropLabel(S, "Azimuth", Label, sizeof(Label));
            M.Azimuth = Finite(Label,M.Azimuth,0,360);
            MoonPropLabel(S, "Elevation", Label, sizeof(Label));
            M.Elevation = Finite(Label,M.Elevation,-90,90);
            MoonPropLabel(S, "Size", Label, sizeof(Label));
            if (!Reskinned) M.Size = Finite(Label,M.Size,.1f,std::numeric_limits<float>::max());
            MoonPropLabel(S, "Bright", Label, sizeof(Label));
            M.Bright = Finite(Label,M.Bright,0,6);
            MoonPropLabel(S, "Glow", Label, sizeof(Label));
            M.Glow = Finite(Label,M.Glow,0,3);
            MoonPropLabel(S, "Phase", Label, sizeof(Label));
            M.Phase = Finite(Label,M.Phase,0,1);
            MoonPropLabel(S,"Roll",Label,sizeof(Label));M.Roll=Finite(Label,M.Roll,0,360);
            MoonPropLabel(S,"Pitch",Label,sizeof(Label));M.Pitch=Finite(Label,M.Pitch,-180,180);
        }
        break;
    }
    case CelestialEntity::HeightFog:
    case CelestialEntity::AtmosphericFog:
    {
        auto Safe=[&](const char* N,float Old,float Low,float High){float V=ReadSlider(Sheet,N,Old);return std::isfinite(V)?std::clamp(V,Low,High):Old;};
        if(Entity==CelestialEntity::HeightFog){
            Fog.HeightEnabled=ReadSwitch(Sheet,"Enabled",Fog.HeightEnabled);Fog.HeightDensity=Safe("Density",Fog.HeightDensity,0,.2f);
            Fog.FalloffHeight=Safe("Falloff Height",Fog.FalloffHeight,10,3000);Fog.SunScatter=Safe("Sun Scatter",Fog.SunScatter,0,2);
            if(const auto* P=Find(Sheet,"Colour"))for(int C=0;C<3;++C)if(std::isfinite(P->ColourTint[C]))Fog.HeightColour[C]=std::clamp(P->ColourTint[C],0.f,1.f);
        }else{Fog.AerialEnabled=ReadSwitch(Sheet,"Enabled",Fog.AerialEnabled);Fog.AerialDensity=Safe("Density",Fog.AerialDensity,0,4);
            Fog.AerialStart=Safe("Start",Fog.AerialStart,0,2000);Fog.AerialMie=Safe("Mie Blend",Fog.AerialMie,0,1);}
        break;
    }
    case CelestialEntity::CloudLayer:
    {
        auto Safe=[&](const char* N,float Old,float Low,float High){float V=ReadSlider(Sheet,N,Old);return std::isfinite(V)?std::clamp(V,Low,High):Old;};
        Cloud.Enabled       = ReadSwitch(Sheet, "Enabled", Cloud.Enabled);
        Cloud.Type          = static_cast<CloudTypeCategory>(std::min(5u,ReadSelect(Sheet,"Type",static_cast<uint32_t>(Cloud.Type))));
        Cloud.Coverage      = Safe("Coverage",Cloud.Coverage,0,1);
        Cloud.Density       = Safe("Density",Cloud.Density,0,4);
        Cloud.Scale         = Safe("Feature Scale",Cloud.Scale,0.2,3);
        Cloud.Base          = Safe("Base",Cloud.Base,100,20000);
        Cloud.Thickness     = Safe("Thickness",Cloud.Thickness,100,6000);
        Cloud.CeilingMetres = Safe("Ceiling",Cloud.CeilingMetres,4000,20000);
        Cloud.FollowWind    = ReadSwitch(Sheet, "Follow Wind", Cloud.FollowWind);
        ShadowStaging.Enabled   = ReadSwitch(Sheet, "Shadow Enabled", ShadowStaging.Enabled);
        ShadowStaging.Type      = ReadSelect(Sheet, "Shadow Type", ShadowStaging.Type);
        if (ShadowStaging.Type > 5u) ShadowStaging.Type = 2u;
        ShadowStaging.Coverage  = Safe("Shadow Coverage",ShadowStaging.Coverage,0,1);
        ShadowStaging.Density   = Safe("Shadow Density",ShadowStaging.Density,0,6);
        ShadowStaging.Scale     = Safe("Shadow Scale",ShadowStaging.Scale,0.01,1);
        ShadowStaging.Anvil     = Safe("Shadow Anvil",ShadowStaging.Anvil,0,1);
        ShadowStaging.Base      = Safe("Shadow Base",ShadowStaging.Base,0,3000);
        ShadowStaging.Thickness = Safe("Shadow Thick",ShadowStaging.Thickness,50,3000);
        ShadowStaging.CeilingMetres = Safe("Shadow Ceil",ShadowStaging.CeilingMetres,4000,20000);
        ShadowTimeSeconds       = Safe("Shadow Time",ShadowTimeSeconds,0,86399);
        ShadowStaging.WindSpeed   = Safe("Shadow Wind",ShadowStaging.WindSpeed,0,40);
        ShadowStaging.WindBearing = Safe("Shadow Dir",ShadowStaging.WindBearing,0,360);
        Cloud.Base=std::min(Cloud.Base,Cloud.CeilingMetres);
        Cloud.Anvil=Safe("Anvil",Cloud.Anvil,0,1);Cloud.Anisotropy=Safe("Anisotropy",Cloud.Anisotropy,-.9f,.9f);
        break;
    }
    case CelestialEntity::LocalCloud:
    {
        auto Safe=[&](const char* N,float Old,float Low,float High){float V=ReadSlider(Sheet,N,Old);return std::isfinite(V)?std::clamp(V,Low,High):Old;};
        LocalCloud.Enabled=ReadSwitch(Sheet,"Enabled",LocalCloud.Enabled);LocalCloud.FollowWind=ReadSwitch(Sheet,"Follow Wind",LocalCloud.FollowWind);
        LocalCloud.Density=Safe("Density",LocalCloud.Density,0,4);LocalCloud.Coverage=Safe("Coverage",LocalCloud.Coverage,0,1);
        LocalCloud.Scale=Safe("Feature Scale",LocalCloud.Scale,10,600);LocalCloud.Anisotropy=Safe("Anisotropy",LocalCloud.Anisotropy,-.9f,.9f);
        float Centre[3],Half[3];for(int I=0;I<3;++I){Centre[I]=LocalCloud.Centre[I];Half[I]=LocalCloud.HalfSize[I];}
        ReadAxes(Sheet,"Centre",Centre);ReadAxes(Sheet,"Half Size",Half);
        for(int I=0;I<3;++I){if(std::isfinite(Centre[I]))LocalCloud.Centre[I]=std::clamp(Centre[I],-1000000.f,1000000.f);if(std::isfinite(Half[I]))LocalCloud.HalfSize[I]=std::clamp(Half[I],.1f,100000.f);}
        break;
    }
    case CelestialEntity::LocalFog:
    {
        auto Safe=[&](const char* N,float Old,float Low,float High){float V=ReadSlider(Sheet,N,Old);return std::isfinite(V)?std::clamp(V,Low,High):Old;};
        LocalFog.Enabled=ReadSwitch(Sheet,"Enabled",LocalFog.Enabled);LocalFog.FollowWind=ReadSwitch(Sheet,"Follow Wind",LocalFog.FollowWind);
        LocalFog.Density=Safe("Density",LocalFog.Density,0,4);LocalFog.Coverage=Safe("Coverage",LocalFog.Coverage,0,1);
        LocalFog.Scale=Safe("Feature Scale",LocalFog.Scale,10,600);LocalFog.Anisotropy=Safe("Anisotropy",LocalFog.Anisotropy,-.9f,.9f);
        float Centre[3],Half[3];for(int I=0;I<3;++I){Centre[I]=LocalFog.Centre[I];Half[I]=LocalFog.HalfSize[I];}
        ReadAxes(Sheet,"Centre",Centre);ReadAxes(Sheet,"Half Size",Half);
        for(int I=0;I<3;++I){if(std::isfinite(Centre[I]))LocalFog.Centre[I]=std::clamp(Centre[I],-1000000.f,1000000.f);if(std::isfinite(Half[I]))LocalFog.HalfSize[I]=std::clamp(Half[I],.1f,10000.f);}
        break;
    }
    case CelestialEntity::Wind:
    {
        auto Safe=[&](const char* N,float Old,float Lo,float Hi){float V=ReadSlider(Sheet,N,Old);return std::isfinite(V)?std::clamp(V,Lo,Hi):Old;};
        Wind.Speed      = Safe("Speed", Wind.Speed, 0.0f, 40.0f);
        Wind.Bearing    = Safe("Bearing", Wind.Bearing, 0.0f, 360.0f);
        Wind.Shear      = Safe("Shear", Wind.Shear, 0.0f, 2.0f);
        Wind.Veer       = Safe("Veer", Wind.Veer, -60.0f, 60.0f);
        Wind.Gust       = Safe("Gust", Wind.Gust, 0.0f, 1.0f);
        Wind.Turbulence = Safe("Turbulence", Wind.Turbulence, 0.0f, 1.0f);
        Wind.Steadiness = Safe("Steadiness", Wind.Steadiness, 0.0f, 1.0f);
        break;
    }
    case CelestialEntity::Precipitation:
    {
        auto Safe=[&](const char* N,float Old,float Lo,float Hi){float V=ReadSlider(Sheet,N,Old);return std::isfinite(V)?std::clamp(V,Lo,Hi):Old;};
        Precip.Enabled                = ReadSwitch(Sheet, "Enabled", Precip.Enabled);
        const uint32_t Type=ReadSelect(Sheet,"Precipitation",static_cast<uint32_t>(Precip.Category));
        if(Type<5u)Precip.Category=static_cast<PrecipitationCategory>(Type);
        Precip.RateMillimetresPerHour = Safe("Intensity", Precip.RateMillimetresPerHour, 0.0f, 100.0f);
        Precip.Density                = Safe("Density", Precip.Density, 0.1f, 6.0f);
        Precip.SizeScale              = Safe("Particle Size", Precip.SizeScale, 0.3f, 3.0f);
        Precip.WindDrift              = Safe("Wind Drift", Precip.WindDrift, 0.0f, 1.0f);
        Precip.FollowWind             = ReadSwitch(Sheet, "Follow Wind", Precip.FollowWind);
        Precip.SpawnFromClouds        = ReadSwitch(Sheet, "Spawn from Clouds", Precip.SpawnFromClouds);
        Precip.GroundCollision        = ReadSwitch(Sheet, "Ground Collision", Precip.GroundCollision);
        Precip.Accumulation           = Safe("Accumulation", Precip.Accumulation, 0.0f, 1.0f);
        break;
    }
    case CelestialEntity::Rainbow:
    {
        auto Safe=[&](const char* N,float Old,float Lo,float Hi){float V=ReadSlider(Sheet,N,Old);return std::isfinite(V)?std::clamp(V,Lo,Hi):Old;};
        Rainbow.Enabled       = ReadSwitch(Sheet, "Enabled", Rainbow.Enabled);
        Rainbow.Intensity     = Safe("Intensity", Rainbow.Intensity, 0.0f, 3.0f);
        Rainbow.Width         = Safe("Width", Rainbow.Width, 0.1f, 4.0f);
        Rainbow.SecondaryGain = Safe("Secondary", Rainbow.SecondaryGain, 0.0f, 1.0f);
        Rainbow.MinimumPathMetres=Safe("Minimum Path",Rainbow.MinimumPathMetres,1,2000);
        Rainbow.AlexanderBand = ReadSwitch(Sheet, "Alexander's Band", Rainbow.AlexanderBand);
        break;
    }
    case CelestialEntity::LensFlare:
    {
        auto Bounded=[&](const char* Label,float Old,float Lo,float Hi){float V=ReadSlider(Sheet,Label,Old);return std::isfinite(V)?std::clamp(V,Lo,Hi):Old;};
        const uint32_t OldMode=Flare.CustomMix?4u:static_cast<uint32_t>(Flare.Category);
        const uint32_t Mode=ReadSelect(Sheet,"Type",OldMode);
        bool Changed=false;
        auto Set=[&](float& Field,float Value){Changed=Changed||Field!=Value;Field=Value;};
        Set(Flare.Layers.Spread,Bounded("Spread",Flare.Layers.Spread,0.25f,2.0f));
        Set(Flare.Layers.Rotation,Bounded("Rotation",Flare.Layers.Rotation,0.0f,180.0f));
        Set(Flare.Layers.RayPairs,Bounded("Ray pairs",Flare.Layers.RayPairs,2.0f,12.0f));
        Set(Flare.Layers.GhostGain,Bounded("Ghost brightness",Flare.Layers.GhostGain,0.0f,2.0f));
        Set(Flare.Layers.GhostSpacing,Bounded("Ghost spacing",Flare.Layers.GhostSpacing,0.0f,2.0f));
        Set(Flare.Layers.HaloGain,Bounded("Halo brightness",Flare.Layers.HaloGain,0.0f,3.0f));
        Set(Flare.Layers.HaloWidth,Bounded("Halo width",Flare.Layers.HaloWidth,0.005f,0.2f));
        Set(Flare.Layers.Anamorphic,ReadSwitch(Sheet,"Anamorphic",Flare.Layers.Anamorphic>0)?1.0f:0.0f);
        Set(Flare.Layers.Streaks,ReadSwitch(Sheet,"Streaks",Flare.Layers.Streaks>0)?1.0f:0.0f);
        Set(Flare.Layers.Burst,ReadSwitch(Sheet,"Starburst",Flare.Layers.Burst>0)?1.0f:0.0f);
        uint32_t Shape=ReadSelect(Sheet,"Ghost shape",Flare.Layers.GhostSides==6?1u:Flare.Layers.GhostSides==8?2u:0u);
        if(Shape<3u)Set(Flare.Layers.GhostSides,Shape==1?6.0f:Shape==2?8.0f:0.0f);
        if(Changed)Flare.CustomMix=true;
        if(Mode<=4u && Mode!=OldMode){Flare.CustomMix=Mode==4u;if(Mode<4u)Flare.Category=static_cast<AtmosphericOptics::LensFlareCategory>(Mode);}
        Flare.Enabled=ReadSwitch(Sheet,"Enabled",Flare.Enabled);
        Flare.Intensity=Bounded("Intensity",Flare.Intensity,0,3);
        Flare.GhostCount=static_cast<uint32_t>(std::round(Bounded("Ghosts",float(Flare.GhostCount),0,24)));
        Flare.HaloRadius=Bounded("Halo Radius",Flare.HaloRadius,0.1f,1.2f);
        Flare.Chromatic=Bounded("Chromatic",Flare.Chromatic,0,1);
        Flare.StreakGain=Bounded("Streak gain",Flare.StreakGain,0,3);
        Flare.ApertureBlades=static_cast<uint32_t>(std::round(Bounded("Aperture Blades",float(Flare.ApertureBlades),3,12)));
        Flare.Layers.RayPairs=std::round(Flare.Layers.RayPairs);
        FlarePreviewX=Bounded("Preview X",FlarePreviewX,0.06f,0.94f);
        FlarePreviewY=Bounded("Preview Y",FlarePreviewY,0.08f,0.92f);
        break;
    }
    default:
        break;
    }
}

//------------------------------------------------------------------------------------------------------------------------

uint32_t CelestialSequence::CollectMarkers(VolumeMarker* Markers, uint32_t Capacity) const noexcept
{
    if (Markers == nullptr) return 0u;
    uint32_t Count = 0u;
    if (Count < Capacity && LocalCloud.Enabled)
    {
        VolumeMarker& M = Markers[Count++];
        M = VolumeMarker{};
        M.Category = VolumeMarkerCategory::LocalCloud;
        for (int C = 0; C < 3; ++C) M.World[C] = LocalCloud.Centre[C];
        M.Identifier = static_cast<uint32_t>(CelestialEntity::LocalCloud);
        M.Visible = Shown[static_cast<uint32_t>(CelestialEntity::LocalCloud)];
    }
    if (Count < Capacity && LocalFog.Enabled)
    {
        VolumeMarker& M = Markers[Count++];
        M = VolumeMarker{};
        M.Category = VolumeMarkerCategory::LocalFog;
        for (int C = 0; C < 3; ++C) M.World[C] = LocalFog.Centre[C];
        M.Identifier = static_cast<uint32_t>(CelestialEntity::LocalFog);
        M.Visible = Shown[static_cast<uint32_t>(CelestialEntity::LocalFog)];
    }
    return Count;
}

void CelestialSequence::MoveMarker(uint32_t Identifier, const float World[3]) noexcept
{
    if (Identifier == static_cast<uint32_t>(CelestialEntity::LocalCloud))
        for (int C = 0; C < 3; ++C) LocalCloud.Centre[C] = World[C];
    else if (Identifier == static_cast<uint32_t>(CelestialEntity::LocalFog))
        for (int C = 0; C < 3; ++C) LocalFog.Centre[C] = World[C];
}

} // namespace Frontier::ProjectZero

void Frontier::ProjectZero::CelestialSequence::BuildFlareSheet(EditorSheet& Sheet) const noexcept
{
        Sheet.Appearance=EditorSheetAppearance::LensFlare;
        EditorPropertyGroup& Lens = OpenGroup(Sheet, "Lens");
        Push(Lens, MakeSwitch("Enabled", Flare.Enabled));
        {
            static const char* const Types[] = { "Cinematic", "Anamorphic", "Starburst", "Halo", "Custom layers" };
            Push(Lens, MakeSelect("Type", Types, 5u, Flare.CustomMix?4u:static_cast<uint32_t>(Flare.Category)));
        }
        Push(Lens, MakeSlider("Intensity", 0.0f, 3.0f, Flare.Intensity, 2, "x"));
        Push(Lens, MakeSlider("Ghosts", 0.0f, 24.0f, static_cast<float>(Flare.GhostCount), 0, ""));
        Push(Lens, MakeSlider("Halo Radius", 0.1f, 1.2f, Flare.HaloRadius, 2, ""));
        Push(Lens, MakeSlider("Chromatic", 0.0f, 1.0f, Flare.Chromatic, 2, ""));
        Push(Lens, MakeSlider("Aperture Blades", 3.0f, 12.0f, static_cast<float>(Flare.ApertureBlades), 0, ""));
        Push(Lens, MakeSlider("Streak gain",0.0f,3.0f,Flare.StreakGain,2,"x"));
        auto& Layers=OpenGroup(Sheet,"Layers");
        Push(Layers,MakeSwitch("Anamorphic",Flare.Layers.Anamorphic>0));
        Push(Layers,MakeSwitch("Streaks",Flare.Layers.Streaks>0));
        Push(Layers,MakeSwitch("Starburst",Flare.Layers.Burst>0));
        Push(Layers,MakeSlider("Spread",0.25f,2.0f,Flare.Layers.Spread,2,"x"));
        Push(Layers,MakeSlider("Rotation",0.0f,180.0f,Flare.Layers.Rotation,1,"deg"));
        Push(Layers,MakeSlider("Ray pairs",2.0f,12.0f,Flare.Layers.RayPairs,0,""));
        auto& Ghost=OpenGroup(Sheet,"Ghosts / halo");
        Push(Ghost,MakeSlider("Ghost brightness",0.0f,2.0f,Flare.Layers.GhostGain,2,"x"));
        Push(Ghost,MakeSlider("Ghost spacing",0.0f,2.0f,Flare.Layers.GhostSpacing,2,"x"));
        Push(Ghost,MakeSlider("Halo brightness",0.0f,3.0f,Flare.Layers.HaloGain,2,"x"));
        Push(Ghost,MakeSlider("Halo width",0.005f,0.2f,Flare.Layers.HaloWidth,3,""));
        static const char* const Shapes[]={"Round","Hexagon","Octagon"};
        Push(Ghost,MakeSelect("Ghost shape",Shapes,3u,Flare.Layers.GhostSides==6?1u:Flare.Layers.GhostSides==8?2u:0u));
        auto& Preview=OpenGroup(Sheet,"Preview pose");
        Push(Preview,MakeSlider("Preview X",0.06f,0.94f,FlarePreviewX,2,""));
        Push(Preview,MakeSlider("Preview Y",0.08f,0.92f,FlarePreviewY,2,""));
}

void Frontier::ProjectZero::CelestialSequence::BuildAtmosphereSkySheet(EditorSheet& Sheet) const noexcept
{
    Sheet.Appearance=EditorSheetAppearance::AtmosphereSky;
    char Text[48];
        EditorPropertyGroup& Scatter = OpenGroup(Sheet, "Scattering");
        Push(Scatter, MakeSlider("Rayleigh", 0.0f, 4.0f, Medium.RayleighStrength, 2, "x"));
        Push(Scatter, MakeSlider("Mie", 0.0f, 6.0f, Medium.MieStrength, 2, "x"));
        Push(Scatter, MakeSlider("Mie Anisotropy", 0.0f, 0.99f, Medium.MieAnisotropy, 3, ""));
        Push(Scatter, MakeSlider("Ozone", 0.0f, 4.0f, Medium.OzoneStrength, 2, "x"));

        EditorPropertyGroup& Planet = OpenGroup(Sheet, "Planet");
        Push(Planet, MakeSlider("Rayleigh Scale H", 2000.0f, 15000.0f, Medium.RayleighScaleHeight, 0, "m"));
        Push(Planet, MakeSlider("Mie Scale H", 300.0f, 4000.0f, Medium.MieScaleHeight, 0, "m"));
        Push(Planet, MakeSlider("Atmosphere", 10000.0f, 120000.0f, Medium.AtmosphereHeight, 0, "m"));

        EditorPropertyGroup& Dusk = OpenGroup(Sheet, "Twilight");
        Push(Dusk, MakeSlider("Horizon Glow", 0.0f, 3.0f, Twilight.GlowIntensity, 2, "x"));
        Push(Dusk, MakeSlider("White Line", 0.0f, 3.0f, Twilight.LineIntensity, 2, "x"));
        Push(Dusk, MakeSwitch("Line at civil only", Twilight.LineAtCivilOnly));

        EditorPropertyGroup& Live = OpenGroup(Sheet, "Live");
        std::snprintf(Text, sizeof(Text), "%.2f AM", static_cast<double>(CelestialSolver::AirMass(Solved.Sun.Elevation)));
        Push(Live, MakeReadout("Air Mass", Text));
        std::snprintf(Text, sizeof(Text), "%u x %u", Budget.AtmosphereSamples, Budget.AtmosphereLightSamples);
        Push(Live, MakeReadout("Tier Samples", Text));
        EditorPropertyGroup& Look = OpenGroup(Sheet, "Appearance");
        Push(Look, MakeColour("Sky Tint", SkyTint));
        Push(Look, MakeSlider("Sky Brightness", 0.0f, 3.0f, SkyBrightness, 2, "x"));
        Push(Look, MakeColour("Ground Albedo", GroundAlbedo));
        // The baked dome (roadmap #26 stage A). The switch is the boolean; the readouts say what the frame is
        //    actually doing — "fetch" only while a resident bake still matches the live staging, "march"
        //    otherwise (scrubbed sun, changed medium, no sheet). The split is measured in
        //    Exhibits/Gallery/Sky/: mean error 0.014-0.123 %, fetch 67-135x the march.
        EditorPropertyGroup& Dome = OpenGroup(Sheet, "Baked Dome");
        Push(Dome, MakeSwitch("Fetch Baked Dome", SkyDomeBaked));
        Push(Dome, MakeReadout("Dome Path", QuerySkyDomeLive() ? "fetch (256^2 RGBA16F)" : "march (analytic)"));
        Push(Dome, MakeReadout("Sheet", SkyDomeSlot == kNoSkyDomeSlot ? "none resident"
                                        : (SkyDomeSeated ? "resident, staged" : "slot only")));

    auto& Image=Sheet.SkyImage;
    if(SkyPreviewHalves.size()==size_t(kSkyDomeSide)*kSkyDomeSide*8u){
        Image.Pixels=SkyPreviewHalves.data();Image.Width=kSkyDomeSide;Image.Height=kSkyDomeSide*2;
        for(int C=0;C<3;++C)Image.BakedSunDirection[C]=SkyDomeRecord.SunDirection[C];
        Image.Revision=SkyPreviewRevision;Image.Stale=!SkyDomeStagingMatches(SkyDomeRecord,PackSkyRecord());
    }
    Image.Resident=SkyDomeSlot!=kNoSkyDomeSlot;
    Image.Pending=SkyPreviewBakeRequested;
}

void Frontier::ProjectZero::CelestialSequence::BuildMoonSlot(EditorSheet& Sheet,uint32_t S) const noexcept {
static const char* const Presets[]={"Luna","Ember","Glacier","Sulfur","Shroud","Shard"};char Title[24],Label[28];
            const MoonSlotState& M = MoonSlots[S];
            std::snprintf(Title, sizeof(Title), "Moon %u", S + 1u);
            EditorPropertyGroup& Slot = OpenGroup(Sheet, Title);
            MoonPropLabel(S, "Visible", Label, sizeof(Label));
            Push(Slot, MakeSwitch(Label, M.Visible));
            MoonPropLabel(S, "Preset", Label, sizeof(Label));
            Push(Slot, MakeSelect(Label, Presets, 6u, M.Preset < 6u ? M.Preset : 0u));
            MoonPropLabel(S, "Follow Sky", Label, sizeof(Label));
            Push(Slot, MakeSwitch(Label, M.FollowSky));
            MoonPropLabel(S, "Azimuth", Label, sizeof(Label));
            Push(Slot, MakeSlider(Label, 0.0f, 360.0f, M.Azimuth, 0, "deg"));
            MoonPropLabel(S, "Elevation", Label, sizeof(Label));
            Push(Slot, MakeSlider(Label, -90.0f, 90.0f, M.Elevation, 1, "deg"));
            MoonPropLabel(S, "Size", Label, sizeof(Label));
            Push(Slot, MakeSlider(Label, 0.1f, std::max(180.f,M.Size), M.Size, 2, "deg"));
            MoonPropLabel(S, "Bright", Label, sizeof(Label));
            Push(Slot, MakeSlider(Label, 0.0f, 6.0f, M.Bright, 2, "x"));
            MoonPropLabel(S, "Glow", Label, sizeof(Label));
            Push(Slot, MakeSlider(Label, 0.0f, 3.0f, M.Glow, 2, "x"));
            MoonPropLabel(S, "Phase", Label, sizeof(Label));
            Push(Slot, MakeSlider(Label, 0.0f, 1.0f, M.Phase, 2, ""));
}

void Frontier::ProjectZero::CelestialSequence::BuildMoonSheet(EditorSheet& Sheet) const noexcept {
 Sheet.Appearance=EditorSheetAppearance::Moon;Sheet.MoonAtlasRevision=MoonAtlasRevision_;
 for(uint32_t I=0;I<kMoonAtlasCount;++I){auto& B=Sheet.MoonBodies[I];const auto& V=MoonViews_[I];const auto& P=kMoonAtlas[I];if(V.TexelBytes==4&&V.Width>1&&V.Height>1){B.Pixels=V.Texels;B.Width=V.Width;B.Height=V.Height;}for(int C=0;C<3;++C)B.Tint[C]=P.Tint[C];B.Tilt=P.TiltDegrees;B.Haze=P.Haze;B.Gamma=P.Gamma;}
 Sheet.MoonAzimuth=Solved.Moon.Azimuth;Sheet.MoonElevation=Solved.Moon.Elevation;Sheet.MoonPhase=Solved.MoonPhase;
 char Text[72];
        EditorPropertyGroup& Live = OpenGroup(Sheet, "Solved");
        std::snprintf(Text, sizeof(Text), "%+.2f deg", static_cast<double>(Solved.Moon.Elevation));
        Push(Live, MakeReadout("Elevation", Text));
        std::snprintf(Text, sizeof(Text), "%.1f deg", static_cast<double>(Solved.Moon.Azimuth));
        Push(Live, MakeReadout("Azimuth", Text));
        std::snprintf(Text, sizeof(Text), "%.0f%% lit", static_cast<double>(Solved.MoonIllumination) * 100.0);
        Push(Live, MakeReadout("Illumination", Text));

 for(uint32_t I=0;I<kMoonDrawCount;++I)BuildMoonSlot(Sheet,I);
 auto& Orientation=OpenGroup(Sheet,"Orientation");char Label[28];for(uint32_t I=0;I<kMoonDrawCount;++I){MoonPropLabel(I,"Roll",Label,sizeof(Label));Push(Orientation,MakeSlider(Label,0,360,MoonSlots[I].Roll,1,"deg"));MoonPropLabel(I,"Pitch",Label,sizeof(Label));Push(Orientation,MakeSlider(Label,-180,180,MoonSlots[I].Pitch,1,"deg"));}
}
