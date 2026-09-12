//============================================================================================================================================
//                                                 CELESTIALSEQUENCE.CPP
//============================================================================================================================================

#include "CelestialSequence.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::ProjectZero {

namespace {

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
    G = EditorPropertyGroup{};
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
        E.Tilt = A.TiltDegrees * kDeg;
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
    if (DeltaSeconds < 0.0f) DeltaSeconds = 0.0f;

    // ① The clock. Wrapped rather than clamped, so a fast day cycle rolls into the next morning.
    if (Clock.Animate)
    {
        ElapsedHours += DeltaSeconds * Clock.SpeedTimes / 3600.0f;
        Observation.LocalHours += ElapsedHours;
        ElapsedHours = 0.0f;
        while (Observation.LocalHours >= 24.0f) Observation.LocalHours -= 24.0f;
        while (Observation.LocalHours < 0.0f)   Observation.LocalHours += 24.0f;
    }

    // ② The ephemeris, from whatever the clock now says.
    Solved = CelestialSolver::Solve(Observation);
    for (int C = 0; C < 3; ++C) Light.Direction[C] = Solved.Sun.Direction[C];

    // ③ The wind's gust phase. Everything downstream advects by this, so it moves before they do.
    Wind.GustPhase += DeltaSeconds * 0.35f;
    if (Wind.GustPhase > 6.28318531f * 1024.0f) Wind.GustPhase -= 6.28318531f * 1024.0f;

    // ④ Precipitation last: its emitter reads the cloud layer, which the wind has just moved.
    const bool Falling = Precip.Enabled && Shown[static_cast<uint32_t>(CelestialEntity::Precipitation)];
    PrecipitationSettings Active = Precip;
    Active.Enabled = Falling;
    Rain.Step(Active, Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] ? Cloud : CloudLayerSettings{},
              Wind, Camera, DeltaSeconds, Observation.LocalHours * 3600.0f, GroundHeight);
}

//------------------------------------------------------------------------------------------------------------------------

void CelestialSequence::ApplyTo(VisibilityRaster& Raster, const CelestialBudget& Budget) const noexcept
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
    Settings.SampleCount      = Budget.AtmosphereSamples;
    Settings.LightSampleCount = Budget.AtmosphereLightSamples;

    const bool WantStars = Shown[static_cast<uint32_t>(CelestialEntity::Stars)] && !Catalogue.Empty();
    Settings.Stars             = WantStars ? &Catalogue : nullptr;
    Settings.StarBrightness    = StarBrightness;
    Settings.StarSize          = StarSize;
    Settings.LocalSiderealTime = Solved.LocalSiderealTime;
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
    Settings.Wind = Wind;
    Settings.CloudBudget = Budget.Volumetrics;
    Settings.CloudTime = Observation.LocalHours * 3600.0f;

    Raster.AssignCelestial(Settings);
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

    SkyConstantRecord Record = PackSkyConstants(Medium, Effective, Twilight, Solved.Sun.Elevation,
                                                  /*CameraHeightMetres=*/2.0f, Budget.AtmosphereSamples,
                                                  Budget.AtmosphereLightSamples, Enabled, SunDirect);

    // The GPU path used to stop at the atmosphere record, so the CPU raster showed broken cumulus while ReSTIR
    // showed a clear blue miss. Pack the same gated weather state that ApplyTo lends the raster; this is the only
    // bridge for clouds, local volumes and their sun-shadow settings to binding 21.
    const bool WantClouds = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)];
    const bool WantLocalCloud = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::LocalCloud)];
    const bool WantLocalFog = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::LocalFog)];
    PackSkyVolumes(Record, Enabled,
                   WantClouds ? Cloud : CloudLayerSettings{},
                   WantLocalCloud ? LocalCloud : LocalVolumeSettings{},
                   WantLocalFog ? LocalFog : LocalVolumeSettings{},
                   Wind, Budget.Volumetrics, Observation.LocalHours * 3600.0f);
    return Record;
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
    const float StarBrightness = WantStars ? CelestialSequence::StarBrightness : 0.0f;
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
    float RainVisibility = 0.0f;
    if (Enabled && Precip.Enabled)
    {
        if (Precip.Category == Frontier::PrecipitationCategory::Rain)
            RainVisibility = Precip.RateMillimetresPerHour / 10.0f > 1.0f ? 1.0f
                           : Precip.RateMillimetresPerHour / 10.0f;
        else if (Precip.Category == Frontier::PrecipitationCategory::Drizzle)
            RainVisibility = Precip.RateMillimetresPerHour / 20.0f > 0.5f ? 0.5f
                           : Precip.RateMillimetresPerHour / 20.0f;
        if (RainVisibility < 0.0f) RainVisibility = 0.0f;
    }
    const bool WantBow = Enabled && Shown[static_cast<uint32_t>(CelestialEntity::Rainbow)] && Rainbow.Enabled;

    // The horizon fade for the flare's visibility, −2..0° of sun elevation. The model's SmoothStep is private
    //    to its class, so the three lines are restated here rather than reached for.
    const float ElevT = (Solved.Sun.Elevation + 2.0f) / 2.0f;
    const float ElevC = ElevT < 0.0f ? 0.0f : (ElevT > 1.0f ? 1.0f : ElevT);
    const float ElevationFade = ElevC * ElevC * (3.0f - 2.0f * ElevC);

    return PackPostConstants(Solved.LocalSiderealTime, Observation.Latitude, StarSize, StarBrightness, PixelSpread,
                             static_cast<uint32_t>(Flare.Category), Flare.GhostCount, Flare.Intensity,
                             Flare.HaloRadius, Flare.Chromatic, Flare.StreakGain, Flare.ApertureBlades,
                             // Below-horizon suns flare nothing (no direct light enters the lens); faded over
                             // −2..0° so the ghosts leave with the sunset rather than popping.
                             SunVisibility * ElevationFade,
                             SunU, SunV, WantFlare,
                             Rainbow.Intensity, Rainbow.Width, Rainbow.SecondaryGain, Rainbow.AlexanderBand,
                             Rainbow.MinimumPathMetres, RainVisibility, WantBow);
}

//------------------------------------------------------------------------------------------------------------------------

uint32_t CelestialSequence::AppendRoster(EditorInstance* Instances, uint32_t Written, uint32_t Capacity) const noexcept
{
    if (Instances == nullptr || Written >= Capacity) return 0u;

    uint32_t Count = 0u;
    // The folder, then its entities one level deeper — the same preorder the scene rows use.
    EditorInstance& Folder = Instances[Written];
    Folder = EditorInstance{};
    std::snprintf(Folder.Label, sizeof(Folder.Label), "Celestial");
    Folder.Depth    = 0u;
    Folder.Category = EditorInstanceCategory::Folder;
    Folder.KidCount = 0u;
    Folder.Visible  = Enabled;
    CopyTint(Folder.Tint, kTintFolder);
    ++Count;

    for (uint32_t I = 0; I < kCelestialEntityCount; ++I)
    {
        if (Written + Count >= Capacity) break;
        const CelestialEntity Entity = static_cast<CelestialEntity>(I);
        EditorInstance& Row = Instances[Written + Count];
        Row = EditorInstance{};
        std::snprintf(Row.Label, sizeof(Row.Label), "%s", CelestialEntityName(Entity));
        Row.Depth   = 1u;
        Row.Visible = Shown[I];
        // A Light row for the sun (it is one), a Camera row for nothing, Geometry for the rest: the outliner's
        //    filter chips are built on these, so the sun lands under the lighting filter where people look.
        Row.Category = (Entity == CelestialEntity::Sun) ? EditorInstanceCategory::Light
                                                        : EditorInstanceCategory::Geometry;
        // Dynamic marks what the tick moves, which is exactly what the DYN badge is for. The moons qualify
        //    through slot one's linked Luna: the slot's own values stand still, but the resolved direction
        //    tracks the solved frame every tick, the same way the sun's does.
        Row.Dynamic = Entity == CelestialEntity::Sun || Entity == CelestialEntity::CloudLayer
                   || Entity == CelestialEntity::Wind || Entity == CelestialEntity::Precipitation
                   || Entity == CelestialEntity::Stars || Entity == CelestialEntity::Moons;
        switch (Entity)
        {
        case CelestialEntity::Atmosphere:     CopyTint(Row.Tint, kTintAtmosphere); break;
        case CelestialEntity::Sun:            CopyTint(Row.Tint, kTintSun);        break;
        case CelestialEntity::Sky:            CopyTint(Row.Tint, kTintSky);        break;
        case CelestialEntity::Stars:
        case CelestialEntity::Moons:          CopyTint(Row.Tint, kTintStars);      break;
        case CelestialEntity::HeightFog:
        case CelestialEntity::AtmosphericFog:
        case CelestialEntity::LocalFog:       CopyTint(Row.Tint, kTintFog);        break;
        case CelestialEntity::CloudLayer:
        case CelestialEntity::LocalCloud:     CopyTint(Row.Tint, kTintCloud);      break;
        case CelestialEntity::Wind:           CopyTint(Row.Tint, kTintWind);       break;
        case CelestialEntity::Precipitation:  CopyTint(Row.Tint, kTintPrecip);     break;
        default:                              CopyTint(Row.Tint, kTintOptics);     break;
        }
        ++Count;
        ++Folder.KidCount;
    }
    return Count;
}

bool CelestialSequence::Owns(uint32_t RosterIndex, uint32_t FirstRow, CelestialEntity& Entity) const noexcept
{
    // FirstRow is the folder; the entities follow it.
    if (RosterIndex <= FirstRow) return false;
    const uint32_t Offset = RosterIndex - FirstRow - 1u;
    if (Offset >= kCelestialEntityCount) return false;
    Entity = static_cast<CelestialEntity>(Offset);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------

void CelestialSequence::BuildSheet(CelestialEntity Entity, EditorSheet& Sheet) const noexcept
{
    Sheet = EditorSheet{};
    char Text[48];

    switch (Entity)
    {
    case CelestialEntity::Atmosphere:
    {
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
        break;
    }
    case CelestialEntity::Sun:
    {
        EditorPropertyGroup& When = OpenGroup(Sheet, "Time");
        Push(When, MakeSlider("Local Hours", 0.0f, 24.0f, Observation.LocalHours, 2, "h"));
        Push(When, MakeSwitch("Animate", Clock.Animate));
        {
            static const char* const Speeds[] = { "x1", "x8", "x30", "x100" };
            const float Rates[4] = { 1.0f, 8.0f, 30.0f, 100.0f };
            uint32_t Picked = 1u;
            for (uint32_t I = 0; I < 4u; ++I) if (std::fabs(Clock.SpeedTimes - Rates[I]) < 0.01f) Picked = I;
            Push(When, MakeSelect("Speed", Speeds, 4u, Picked));
        }

        EditorPropertyGroup& Where = OpenGroup(Sheet, "Place");
        Push(Where, MakeSlider("Latitude", -90.0f, 90.0f, Observation.Latitude, 2, "deg"));
        Push(Where, MakeSlider("Longitude", -180.0f, 180.0f, Observation.Longitude, 2, "deg"));
        Push(Where, MakeSlider("Day of Month", 1.0f, 31.0f, static_cast<float>(Observation.Day), 0, ""));
        Push(Where, MakeSlider("Month", 1.0f, 12.0f, static_cast<float>(Observation.Month), 0, ""));

        EditorPropertyGroup& Beam = OpenGroup(Sheet, "Light");
        Push(Beam, MakeSlider("Intensity", 0.0f, 60.0f, Light.Intensity, 1, "x"));
        Push(Beam, MakeSlider("Direct", 0.0f, 5.0f, SunDirect, 2, "x"));

        // Read-outs rather than sliders: these are SOLVED, and offering to edit them would imply the solver
        //    could be overridden, which it cannot.
        EditorPropertyGroup& Live = OpenGroup(Sheet, "Solved");
        std::snprintf(Text, sizeof(Text), "%+.2f deg", static_cast<double>(Solved.Sun.Elevation));
        Push(Live, MakeReadout("Elevation", Text));
        std::snprintf(Text, sizeof(Text), "%.1f deg", static_cast<double>(Solved.Sun.Azimuth));
        Push(Live, MakeReadout("Azimuth", Text));
        std::snprintf(Text, sizeof(Text), "%+.2f deg", static_cast<double>(Solved.Declination));
        Push(Live, MakeReadout("Declination", Text));
        std::snprintf(Text, sizeof(Text), "%+.1f min", static_cast<double>(Solved.EquationOfTime));
        Push(Live, MakeReadout("Equation of Time", Text));
        break;
    }
    case CelestialEntity::Sky:
    {
        EditorPropertyGroup& Look = OpenGroup(Sheet, "Appearance");
        Push(Look, MakeColour("Sky Tint", SkyTint));
        Push(Look, MakeSlider("Sky Brightness", 0.0f, 3.0f, SkyBrightness, 2, "x"));
        EditorPropertyGroup& Below = OpenGroup(Sheet, "Ground");
        Push(Below, MakeColour("Ground Albedo", GroundAlbedo));
        break;
    }
    case CelestialEntity::Stars:
    {
        EditorPropertyGroup& Field = OpenGroup(Sheet, "Field");
        Push(Field, MakeSlider("Brightness", 0.0f, 4.0f, StarBrightness, 2, "x"));
        Push(Field, MakeSlider("Point Size", 0.4f, 3.0f, StarSize, 2, "x"));
        EditorPropertyGroup& Live = OpenGroup(Sheet, "Catalogue");
        std::snprintf(Text, sizeof(Text), "%u stars", Catalogue.QuerySourceCount());
        Push(Live, MakeReadout("Loaded", Text));
        std::snprintf(Text, sizeof(Text), "%.1f deg", static_cast<double>(Solved.LocalSiderealTime));
        Push(Live, MakeReadout("Sidereal Time", Text));
        break;
    }
    case CelestialEntity::Moons:
    {
        EditorPropertyGroup& Live = OpenGroup(Sheet, "Solved");
        std::snprintf(Text, sizeof(Text), "%+.2f deg", static_cast<double>(Solved.Moon.Elevation));
        Push(Live, MakeReadout("Elevation", Text));
        std::snprintf(Text, sizeof(Text), "%.1f deg", static_cast<double>(Solved.Moon.Azimuth));
        Push(Live, MakeReadout("Azimuth", Text));
        std::snprintf(Text, sizeof(Text), "%.0f%% lit", static_cast<double>(Solved.MoonIllumination) * 100.0);
        Push(Live, MakeReadout("Illumination", Text));
        // One group per roster slot after the solved readouts: 5 groups against a 6-group sheet, 9 properties
        //    against 10 per group. Labels carry the M1..M4 prefix (MoonPropLabel): Find reads back BY LABEL, so
        //    unprefixed groups would all land on slot one's fields.
        static const char* const Presets[] = { "Luna", "Ember", "Glacier", "Sulfur", "Shroud", "Shard" };
        char Title[24];
        char Label[28];
        for (uint32_t S = 0u; S < kMoonDrawCount; ++S)
        {
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
            Push(Slot, MakeSlider(Label, 0.1f, 40.0f, M.Size, 2, "deg"));
            MoonPropLabel(S, "Bright", Label, sizeof(Label));
            Push(Slot, MakeSlider(Label, 0.0f, 6.0f, M.Bright, 2, "x"));
            MoonPropLabel(S, "Glow", Label, sizeof(Label));
            Push(Slot, MakeSlider(Label, 0.0f, 3.0f, M.Glow, 2, "x"));
            MoonPropLabel(S, "Phase", Label, sizeof(Label));
            Push(Slot, MakeSlider(Label, 0.0f, 1.0f, M.Phase, 2, ""));
        }
        break;
    }
    case CelestialEntity::HeightFog:
    {
        EditorPropertyGroup& Medium2 = OpenGroup(Sheet, "Medium");
        Push(Medium2, MakeSwitch("Enabled", Fog.HeightEnabled));
        Push(Medium2, MakeSlider("Density", 0.0f, 0.2f, Fog.HeightDensity, 4, ""));
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
    case CelestialEntity::CloudLayer:
    {
        EditorPropertyGroup& Shape = OpenGroup(Sheet, "Shape");
        Push(Shape, MakeSwitch("Enabled", Cloud.Enabled));
        {
            static const char* const Types[] = { "Stratus", "Stratocum", "Cumulus", "Cumulonim", "Altostrat", "Cirrus" };
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
        break;
    }
    case CelestialEntity::LocalCloud:
    case CelestialEntity::LocalFog:
    {
        const LocalVolumeSettings& V = (Entity == CelestialEntity::LocalCloud) ? LocalCloud : LocalFog;
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
    case CelestialEntity::Wind:
    {
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
        break;
    }
    case CelestialEntity::Precipitation:
    {
        EditorPropertyGroup& Kind = OpenGroup(Sheet, "Type");
        Push(Kind, MakeSwitch("Enabled", Precip.Enabled));
        {
            static const char* const Types[] = { "Rain", "Drizzle", "Hail", "Snow", "Sleet" };
            Push(Kind, MakeSelect("Precipitation", Types, 5u, static_cast<uint32_t>(Precip.Category)));
        }
        Push(Kind, MakeSlider("Intensity", 0.0f, 100.0f, Precip.RateMillimetresPerHour, 1, "mm/h"));
        Push(Kind, MakeSlider("Density", 0.1f, 6.0f, Precip.Density, 2, "x"));
        Push(Kind, MakeSlider("Particle Size", 0.3f, 3.0f, Precip.SizeScale, 2, "x"));

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
        break;
    }
    case CelestialEntity::Rainbow:
    {
        EditorPropertyGroup& Bow = OpenGroup(Sheet, "Bow");
        Push(Bow, MakeSwitch("Enabled", Rainbow.Enabled));
        Push(Bow, MakeSlider("Intensity", 0.0f, 3.0f, Rainbow.Intensity, 2, "x"));
        Push(Bow, MakeSlider("Width", 0.1f, 4.0f, Rainbow.Width, 2, "x"));
        Push(Bow, MakeSlider("Secondary", 0.0f, 1.0f, Rainbow.SecondaryGain, 2, ""));
        Push(Bow, MakeSwitch("Alexander's Band", Rainbow.AlexanderBand));
        break;
    }
    case CelestialEntity::LensFlare:
    {
        EditorPropertyGroup& Lens = OpenGroup(Sheet, "Lens");
        Push(Lens, MakeSwitch("Enabled", Flare.Enabled));
        {
            static const char* const Types[] = { "Cinematic", "Anamorphic", "Starburst", "Halo" };
            Push(Lens, MakeSelect("Type", Types, 4u, static_cast<uint32_t>(Flare.Category)));
        }
        Push(Lens, MakeSlider("Intensity", 0.0f, 3.0f, Flare.Intensity, 2, "x"));
        Push(Lens, MakeSlider("Ghosts", 0.0f, 8.0f, static_cast<float>(Flare.GhostCount), 0, ""));
        Push(Lens, MakeSlider("Halo Radius", 0.1f, 1.2f, Flare.HaloRadius, 2, ""));
        Push(Lens, MakeSlider("Chromatic", 0.0f, 1.0f, Flare.Chromatic, 2, ""));
        Push(Lens, MakeSlider("Aperture Blades", 3.0f, 12.0f, static_cast<float>(Flare.ApertureBlades), 0, ""));
        break;
    }
    default:
        break;
    }
}

//------------------------------------------------------------------------------------------------------------------------

void CelestialSequence::ApplySheet(CelestialEntity Entity, const EditorSheet& Sheet) noexcept
{
    switch (Entity)
    {
    case CelestialEntity::Atmosphere:
        Medium.RayleighStrength    = ReadSlider(Sheet, "Rayleigh", Medium.RayleighStrength);
        Medium.MieStrength         = ReadSlider(Sheet, "Mie", Medium.MieStrength);
        Medium.MieAnisotropy       = ReadSlider(Sheet, "Mie Anisotropy", Medium.MieAnisotropy);
        Medium.OzoneStrength       = ReadSlider(Sheet, "Ozone", Medium.OzoneStrength);
        Medium.RayleighScaleHeight = ReadSlider(Sheet, "Rayleigh Scale H", Medium.RayleighScaleHeight);
        Medium.MieScaleHeight      = ReadSlider(Sheet, "Mie Scale H", Medium.MieScaleHeight);
        Medium.AtmosphereHeight    = ReadSlider(Sheet, "Atmosphere", Medium.AtmosphereHeight);
        Twilight.GlowIntensity     = ReadSlider(Sheet, "Horizon Glow", Twilight.GlowIntensity);
        Twilight.LineIntensity     = ReadSlider(Sheet, "White Line", Twilight.LineIntensity);
        Twilight.LineAtCivilOnly   = ReadSwitch(Sheet, "Line at civil only", Twilight.LineAtCivilOnly);
        break;
    case CelestialEntity::Sun:
    {
        Observation.LocalHours = ReadSlider(Sheet, "Local Hours", Observation.LocalHours);
        Clock.Animate          = ReadSwitch(Sheet, "Animate", Clock.Animate);
        const float Rates[4] = { 1.0f, 8.0f, 30.0f, 100.0f };
        Clock.SpeedTimes       = Rates[ReadSelect(Sheet, "Speed", 1u) & 3u];
        Observation.Latitude   = ReadSlider(Sheet, "Latitude", Observation.Latitude);
        Observation.Longitude  = ReadSlider(Sheet, "Longitude", Observation.Longitude);
        Observation.Day        = static_cast<int32_t>(ReadSlider(Sheet, "Day of Month", static_cast<float>(Observation.Day)));
        Observation.Month      = static_cast<int32_t>(ReadSlider(Sheet, "Month", static_cast<float>(Observation.Month)));
        Light.Intensity        = ReadSlider(Sheet, "Intensity", Light.Intensity);
        SunDirect              = ReadSlider(Sheet, "Direct", SunDirect);
        break;
    }
    case CelestialEntity::Sky:
    {
        const EditorProperty* Tint = Find(Sheet, "Sky Tint");
        if (Tint != nullptr) for (int C = 0; C < 3; ++C) SkyTint[C] = Tint->ColourTint[C];
        SkyBrightness = ReadSlider(Sheet, "Sky Brightness", SkyBrightness);
        const EditorProperty* Ground = Find(Sheet, "Ground Albedo");
        if (Ground != nullptr) for (int C = 0; C < 3; ++C) GroundAlbedo[C] = Ground->ColourTint[C];
        break;
    }
    case CelestialEntity::Stars:
        StarBrightness = ReadSlider(Sheet, "Brightness", StarBrightness);
        StarSize       = ReadSlider(Sheet, "Point Size", StarSize);
        break;
    case CelestialEntity::Moons:
    {
        char Label[28];
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
            M.Azimuth = ReadSlider(Sheet, Label, M.Azimuth);
            MoonPropLabel(S, "Elevation", Label, sizeof(Label));
            M.Elevation = ReadSlider(Sheet, Label, M.Elevation);
            MoonPropLabel(S, "Size", Label, sizeof(Label));
            if (!Reskinned) M.Size = ReadSlider(Sheet, Label, M.Size);
            MoonPropLabel(S, "Bright", Label, sizeof(Label));
            M.Bright = ReadSlider(Sheet, Label, M.Bright);
            MoonPropLabel(S, "Glow", Label, sizeof(Label));
            M.Glow = ReadSlider(Sheet, Label, M.Glow);
            MoonPropLabel(S, "Phase", Label, sizeof(Label));
            M.Phase = ReadSlider(Sheet, Label, M.Phase);
        }
        break;
    }
    case CelestialEntity::HeightFog:
    {
        Fog.HeightEnabled = ReadSwitch(Sheet, "Enabled", Fog.HeightEnabled);
        Fog.HeightDensity = ReadSlider(Sheet, "Density", Fog.HeightDensity);
        Fog.FalloffHeight = ReadSlider(Sheet, "Falloff Height", Fog.FalloffHeight);
        Fog.SunScatter    = ReadSlider(Sheet, "Sun Scatter", Fog.SunScatter);
        const EditorProperty* Colour = Find(Sheet, "Colour");
        if (Colour != nullptr) for (int C = 0; C < 3; ++C) Fog.HeightColour[C] = Colour->ColourTint[C];
        break;
    }
    case CelestialEntity::AtmosphericFog:
        Fog.AerialEnabled = ReadSwitch(Sheet, "Enabled", Fog.AerialEnabled);
        Fog.AerialDensity = ReadSlider(Sheet, "Density", Fog.AerialDensity);
        Fog.AerialStart   = ReadSlider(Sheet, "Start", Fog.AerialStart);
        Fog.AerialMie     = ReadSlider(Sheet, "Mie Blend", Fog.AerialMie);
        break;
    case CelestialEntity::CloudLayer:
        Cloud.Enabled       = ReadSwitch(Sheet, "Enabled", Cloud.Enabled);
        Cloud.Type          = static_cast<CloudTypeCategory>(ReadSelect(Sheet, "Type", static_cast<uint32_t>(Cloud.Type)));
        Cloud.Coverage      = ReadSlider(Sheet, "Coverage", Cloud.Coverage);
        Cloud.Density       = ReadSlider(Sheet, "Density", Cloud.Density);
        Cloud.Scale         = ReadSlider(Sheet, "Feature Scale", Cloud.Scale);
        Cloud.Base          = ReadSlider(Sheet, "Base", Cloud.Base);
        Cloud.Thickness     = ReadSlider(Sheet, "Thickness", Cloud.Thickness);
        Cloud.CeilingMetres = ReadSlider(Sheet, "Ceiling", Cloud.CeilingMetres);
        Cloud.FollowWind    = ReadSwitch(Sheet, "Follow Wind", Cloud.FollowWind);
        break;
    case CelestialEntity::LocalCloud:
    case CelestialEntity::LocalFog:
    {
        LocalVolumeSettings& V = (Entity == CelestialEntity::LocalCloud) ? LocalCloud : LocalFog;
        V.Enabled    = ReadSwitch(Sheet, "Enabled", V.Enabled);
        ReadAxes(Sheet, "Centre", V.Centre);
        ReadAxes(Sheet, "Half Size", V.HalfSize);
        V.Density    = ReadSlider(Sheet, "Density", V.Density);
        V.Coverage   = ReadSlider(Sheet, "Coverage", V.Coverage);
        V.Scale      = ReadSlider(Sheet, "Feature Scale", V.Scale);
        V.Anisotropy = ReadSlider(Sheet, "Anisotropy", V.Anisotropy);
        V.FollowWind = ReadSwitch(Sheet, "Follow Wind", V.FollowWind);
        break;
    }
    case CelestialEntity::Wind:
        Wind.Speed      = ReadSlider(Sheet, "Speed", Wind.Speed);
        Wind.Bearing    = ReadSlider(Sheet, "Bearing", Wind.Bearing);
        Wind.Shear      = ReadSlider(Sheet, "Shear", Wind.Shear);
        Wind.Veer       = ReadSlider(Sheet, "Veer", Wind.Veer);
        Wind.Gust       = ReadSlider(Sheet, "Gust", Wind.Gust);
        Wind.Turbulence = ReadSlider(Sheet, "Turbulence", Wind.Turbulence);
        Wind.Steadiness = ReadSlider(Sheet, "Steadiness", Wind.Steadiness);
        break;
    case CelestialEntity::Precipitation:
        Precip.Enabled                = ReadSwitch(Sheet, "Enabled", Precip.Enabled);
        Precip.Category               = static_cast<PrecipitationCategory>(
                                            ReadSelect(Sheet, "Precipitation", static_cast<uint32_t>(Precip.Category)));
        Precip.RateMillimetresPerHour = ReadSlider(Sheet, "Intensity", Precip.RateMillimetresPerHour);
        Precip.Density                = ReadSlider(Sheet, "Density", Precip.Density);
        Precip.SizeScale              = ReadSlider(Sheet, "Particle Size", Precip.SizeScale);
        Precip.WindDrift              = ReadSlider(Sheet, "Wind Drift", Precip.WindDrift);
        Precip.FollowWind             = ReadSwitch(Sheet, "Follow Wind", Precip.FollowWind);
        Precip.SpawnFromClouds        = ReadSwitch(Sheet, "Spawn from Clouds", Precip.SpawnFromClouds);
        Precip.GroundCollision        = ReadSwitch(Sheet, "Ground Collision", Precip.GroundCollision);
        Precip.Accumulation           = ReadSlider(Sheet, "Accumulation", Precip.Accumulation);
        break;
    case CelestialEntity::Rainbow:
        Rainbow.Enabled       = ReadSwitch(Sheet, "Enabled", Rainbow.Enabled);
        Rainbow.Intensity     = ReadSlider(Sheet, "Intensity", Rainbow.Intensity);
        Rainbow.Width         = ReadSlider(Sheet, "Width", Rainbow.Width);
        Rainbow.SecondaryGain = ReadSlider(Sheet, "Secondary", Rainbow.SecondaryGain);
        Rainbow.AlexanderBand = ReadSwitch(Sheet, "Alexander's Band", Rainbow.AlexanderBand);
        break;
    case CelestialEntity::LensFlare:
        Flare.Enabled        = ReadSwitch(Sheet, "Enabled", Flare.Enabled);
        Flare.Category       = static_cast<AtmosphericOptics::LensFlareCategory>(
                                   ReadSelect(Sheet, "Type", static_cast<uint32_t>(Flare.Category)));
        Flare.Intensity      = ReadSlider(Sheet, "Intensity", Flare.Intensity);
        Flare.GhostCount     = static_cast<uint32_t>(ReadSlider(Sheet, "Ghosts", static_cast<float>(Flare.GhostCount)));
        Flare.HaloRadius     = ReadSlider(Sheet, "Halo Radius", Flare.HaloRadius);
        Flare.Chromatic      = ReadSlider(Sheet, "Chromatic", Flare.Chromatic);
        Flare.ApertureBlades = static_cast<uint32_t>(ReadSlider(Sheet, "Aperture Blades",
                                                                static_cast<float>(Flare.ApertureBlades)));
        break;
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
