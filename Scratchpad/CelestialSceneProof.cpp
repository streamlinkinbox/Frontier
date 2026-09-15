//============================================================================================================================================
// 📦 Scratchpad/CelestialSceneProof.cpp — the sky is in the scene, in the outliner, and it ticks
//============================================================================================================================================
// The Celestial port built each system as a settings struct plus a pure evaluator, and each was proved on its
//    own. This proves the thing those proofs could not: that they are WIRED — that Project Zero holds one
//    coherent world, that the outliner shows it, that editing a sheet changes it, and that the tick advances it.
//
// The failure this guards against is the one integration always has: every part passing its own test while the
//    assembly does nothing. A settings struct nobody reads and a roster nobody fills both look healthy in
//    isolation.

#include "../Projects/Project-Zero/Source/CelestialSequence.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

using namespace Frontier;
using namespace Frontier::ProjectZero;

namespace {
int Failures = 0;
void Expect(bool Condition, const char* What)
{
    if (!Condition) ++Failures;
    std::printf("  %-68s %s\n", What, Condition ? "PASS" : "FAIL");
}
} // namespace

int main()
{
    std::printf("\nCelestialSequence — the port, assembled into Project Zero\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    CelestialSequence Sky;
    Sky.Prepare();

    // ── ① a prepared world is a believable one ─────────────────────────────────────────────────────────────────
    // Opening the editor should show weather, not a switch to find. A default of "everything off" passes every
    // structural test and gives a blank sky, which is the integration equivalent of a stub.
    std::printf("1. Prepare leaves a world worth looking at\n");
    {
        const CelestialFrame& F = Sky.Frame();
        std::printf("     sun %+.2f deg, cloud %s at %.0f m coverage %.2f, wind %.1f m/s\n",
                    static_cast<double>(F.Sun.Elevation), Sky.Cloud.Enabled ? "on" : "off",
                    static_cast<double>(Sky.Cloud.Base), static_cast<double>(Sky.Cloud.Coverage),
                    static_cast<double>(Sky.Wind.Speed));
        Expect(F.Sun.Elevation > 0.0f, "the sun is up at the default time");
        Expect(Sky.Cloud.Enabled && Sky.Cloud.Coverage > 0.1f, "there is cloud in the sky");
        Expect(Sky.Wind.Speed > 0.0f, "and the air is moving");
        Expect(!Sky.Precip.Enabled, "but it is not raining unless asked");
    }

    // ── ② the outliner sees every entity ───────────────────────────────────────────────────────────────────────
    std::printf("\n2. every entity reaches the outliner\n");
    {
        EditorInstance Roster[kMaxEditorInstances]{};
        const uint32_t SceneRows = 3u;             // pretend the scene wrote three rows first
        const uint32_t Written = Sky.AppendRoster(Roster, SceneRows, kMaxEditorInstances);
        std::printf("     %u rows appended after %u scene rows\n", Written, SceneRows);
        Expect(Written == kCelestialEntityCount + 1u, "a folder plus one row per entity");

        Expect(Roster[SceneRows].Category == EditorInstanceCategory::Folder &&
               std::strcmp(Roster[SceneRows].Label, "Celestial") == 0, "the folder is first and named");
        Expect(Roster[SceneRows].KidCount == kCelestialEntityCount, "and counts its children");

        bool Named = true, Deepened = true;
        for (uint32_t E = 0; E < kCelestialEntityCount; ++E)
        {
            const EditorInstance& Row = Roster[SceneRows + 1u + E];
            if (std::strcmp(Row.Label, CelestialEntityName(static_cast<CelestialEntity>(E))) != 0) Named = false;
            if (Row.Depth != 1u) Deepened = false;
        }
        Expect(Named, "every row carries its entity's name");
        Expect(Deepened, "and sits one level under the folder");

        // The sun is a light, so the outliner's lighting filter finds it where people look for it.
        Expect(Roster[SceneRows + 1u + static_cast<uint32_t>(CelestialEntity::Sun)].Category
               == EditorInstanceCategory::Light, "the sun is filed as a light");

        // The roster must respect a cap rather than run off the end of a fixed array.
        EditorInstance Small[kMaxEditorInstances]{};
        const uint32_t Capped = Sky.AppendRoster(Small, kMaxEditorInstances - 4u, kMaxEditorInstances);
        std::printf("     with only 4 slots left: %u rows written\n", Capped);
        Expect(Capped <= 4u, "the append honours the roster cap");
    }

    // ── ③ picks route to the right entity ──────────────────────────────────────────────────────────────────────
    std::printf("\n3. a pick lands on the entity it names\n");
    {
        const uint32_t First = 3u;
        CelestialEntity Picked{};
        Expect(!Sky.Owns(First, First, Picked), "the folder row itself is not an entity");
        bool AllRoute = true;
        for (uint32_t E = 0; E < kCelestialEntityCount; ++E)
            if (!Sky.Owns(First + 1u + E, First, Picked) || static_cast<uint32_t>(Picked) != E) AllRoute = false;
        Expect(AllRoute, "every celestial row routes to its own entity");
        Expect(!Sky.Owns(First + 1u + kCelestialEntityCount, First, Picked), "and the block ends where it should");
        Expect(!Sky.Owns(0u, First, Picked), "a scene row above the block is not claimed");
    }

    // ── ④ every entity has a sheet ─────────────────────────────────────────────────────────────────────────────
    std::printf("\n4. every entity builds an inspector sheet\n");
    {
        bool AllFilled = true;
        uint32_t Widest = 0u;
        for (uint32_t E = 0; E < kCelestialEntityCount; ++E)
        {
            EditorSheet Sheet{};
            Sky.BuildSheet(static_cast<CelestialEntity>(E), Sheet);
            if (Sheet.GroupCount == 0u) { AllFilled = false; std::printf("     %s has NO sheet\n",
                                          CelestialEntityName(static_cast<CelestialEntity>(E))); }
            if (Sheet.GroupCount > Widest) Widest = Sheet.GroupCount;
            // A sheet that overflows its fixed arrays would corrupt the panel rather than truncate.
            if (Sheet.GroupCount > kMaxEditorSheetGroups) AllFilled = false;
            for (uint32_t G = 0; G < Sheet.GroupCount; ++G)
                if (Sheet.Groups[G].PropertyCount > kMaxEditorGroupProps) AllFilled = false;
        }
        std::printf("     widest sheet: %u of %u groups allowed\n", Widest, kMaxEditorSheetGroups);
        Expect(AllFilled, "every entity fills a sheet, and none overflows");
    }

    // ── ⑤ editing a sheet changes the world ────────────────────────────────────────────────────────────────────
    // The seam that matters. A sheet the panel can edit but that never writes back is the commonest way an
    // integration looks finished and does nothing.
    std::printf("\n5. a sheet edit reaches the simulation\n");
    {
        EditorSheet Sheet{};
        Sky.BuildSheet(CelestialEntity::CloudLayer, Sheet);
        const float Before = Sky.Cloud.Coverage;

        // Find the coverage slider and move it, exactly as the panel would.
        bool Moved = false;
        for (uint32_t G = 0; G < Sheet.GroupCount && !Moved; ++G)
            for (uint32_t P = 0; P < Sheet.Groups[G].PropertyCount; ++P)
                if (std::strcmp(Sheet.Groups[G].Properties[P].Label, "Coverage") == 0)
                { Sheet.Groups[G].Properties[P].Figure = 0.87f; Moved = true; break; }
        Expect(Moved, "the cloud sheet offers a coverage slider");

        Sky.ApplySheet(CelestialEntity::CloudLayer, Sheet);
        std::printf("     coverage %.2f -> %.2f\n", static_cast<double>(Before), static_cast<double>(Sky.Cloud.Coverage));
        Expect(std::fabs(Sky.Cloud.Coverage - 0.87f) < 1e-4f, "and moving it changes the cloud layer");

        // Write-back must be by LABEL, not by position: inserting a row above must not shift the target.
        EditorSheet Wind{};
        Sky.BuildSheet(CelestialEntity::Wind, Wind);
        for (uint32_t G = 0; G < Wind.GroupCount; ++G)
            for (uint32_t P = 0; P < Wind.Groups[G].PropertyCount; ++P)
                if (std::strcmp(Wind.Groups[G].Properties[P].Label, "Speed") == 0)
                    Wind.Groups[G].Properties[P].Figure = 19.5f;
        Sky.ApplySheet(CelestialEntity::Wind, Wind);
        Expect(std::fabs(Sky.Wind.Speed - 19.5f) < 1e-4f, "wind speed writes back too");

        // And a partial sheet must not wipe the fields it does not mention.
        EditorSheet Empty{};
        const float Keep = Sky.Wind.Bearing;
        Sky.ApplySheet(CelestialEntity::Wind, Empty);
        Expect(std::fabs(Sky.Wind.Bearing - Keep) < 1e-4f,
               "an empty sheet leaves the state alone rather than zeroing it");
    }

    // ── ⑥ the tick advances the world ──────────────────────────────────────────────────────────────────────────
    std::printf("\n6. the tick moves the clock, the wind and the weather\n");
    {
        CelestialSequence Live;
        Live.Prepare();
        Live.Clock.Animate = true;
        Live.Clock.SpeedTimes = 100.0f;
        Live.Precip.Enabled = true;
        Live.Precip.Category = PrecipitationCategory::Rain;
        Live.Precip.RateMillimetresPerHour = 30.0f;

        const float StartHours = Live.Observation.LocalHours;
        const float StartElevation = Live.Frame().Sun.Elevation;
        const float StartPhase = Live.Wind.GustPhase;
        const float Camera[3] = { 0.0f, 0.0f, 2.0f };

        for (int I = 0; I < 600; ++I) Live.Tick(1.0f / 60.0f, Camera, 0.0f);

        std::printf("     10 s at x100: %.3f h -> %.3f h, sun %+.2f -> %+.2f deg, %u drops\n",
                    static_cast<double>(StartHours), static_cast<double>(Live.Observation.LocalHours),
                    static_cast<double>(StartElevation), static_cast<double>(Live.Frame().Sun.Elevation),
                    Live.Weather().Telemetry().Alive);
        Expect(std::fabs(Live.Observation.LocalHours - StartHours) > 1e-3f, "the clock advanced");
        Expect(std::fabs(Live.Frame().Sun.Elevation - StartElevation) > 1e-3f, "and the sun moved with it");
        Expect(Live.Wind.GustPhase > StartPhase, "the wind's gust phase advanced");
        Expect(Live.Weather().Telemetry().Alive > 0u, "and rain is falling");

        // A hidden entity must actually stop: the outliner's eye is not decoration.
        Live.Shown[static_cast<uint32_t>(CelestialEntity::Precipitation)] = false;
        for (int I = 0; I < 120; ++I) Live.Tick(1.0f / 60.0f, Camera, 0.0f);
        std::printf("     with the row hidden: %u drops\n", Live.Weather().Telemetry().Alive);
        Expect(Live.Weather().Telemetry().Alive == 0u, "hiding the row stops the rain");
    }

    // ── ⑦ the raster receives it ───────────────────────────────────────────────────────────────────────────────
    std::printf("\n7. the settings reach the renderer\n");
    {
        CelestialSequence Bound;
        Bound.Prepare();
        VisibilityRaster Raster;

        CelestialBudget Budget{};
        Budget.AtmosphereSamples = 24u;
        Budget.AtmosphereLightSamples = 9u;
        Bound.ApplyTo(Raster, Budget);

        const VisibilityRaster::CelestialSettings& Applied = Raster.QueryCelestial();
        std::printf("     enabled %s, %u x %u samples, stars %s\n", Applied.Enabled ? "yes" : "no",
                    Applied.SampleCount, Applied.LightSampleCount,
                    Applied.Stars != nullptr ? "bound" : "none");
        Expect(Applied.Enabled, "the raster is told to draw the sky");
        Expect(Applied.SampleCount == 24u && Applied.LightSampleCount == 9u,
               "the tier's sample budget is carried through, not a default");
        Expect(std::fabs(Applied.Light.Direction[2] - Bound.Frame().Sun.Direction[2]) < 1e-5f,
               "the solved sun direction is what the raster gets");
        Expect(std::fabs(Applied.Latitude - Bound.Observation.Latitude) < 1e-5f,
               "and the observer's latitude, so the stars turn about the right pole");

        // Hiding the sun must darken rather than delete: the geometry stays, the radiance goes.
        Bound.Shown[static_cast<uint32_t>(CelestialEntity::Sun)] = false;
        Bound.ApplyTo(Raster, Budget);
        Expect(Raster.QueryCelestial().Light.Intensity == 0.0f, "hiding the sun puts the lights out");
    }

    // ── ⑧ the volumes can be grabbed ───────────────────────────────────────────────────────────────────────────
    std::printf("\n8. bodiless volumes offer a marker\n");
    {
        CelestialSequence Grab;
        Grab.Prepare();
        Grab.LocalCloud.Enabled = true;
        Grab.LocalFog.Enabled = true;

        VolumeMarker Markers[8]{};
        const uint32_t Count = Grab.CollectMarkers(Markers, 8u);
        std::printf("     %u markers collected\n", Count);
        Expect(Count == 2u, "both local volumes offer one");

        const float Target[3] = { 12.0f, 34.0f, 56.0f };
        Grab.MoveMarker(static_cast<uint32_t>(CelestialEntity::LocalCloud), Target);
        Expect(std::fabs(Grab.LocalCloud.Centre[0] - 12.0f) < 1e-5f &&
               std::fabs(Grab.LocalCloud.Centre[2] - 56.0f) < 1e-5f,
               "dragging the marker moves the volume it stands for");
        Expect(std::fabs(Grab.LocalFog.Centre[0] - 12.0f) > 1e-5f, "and only that volume");

        Grab.LocalCloud.Enabled = false;
        Expect(Grab.CollectMarkers(Markers, 8u) == 1u, "a disabled volume offers none");
    }

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the sky is in the scene" : "  THE SKY IS NOT IN THE SCENE");
    return Failures == 0 ? 0 : 1;
}
