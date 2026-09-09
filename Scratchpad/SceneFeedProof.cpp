// Feeds the FRESH CornellBox.gltf through EditorFeedSequence and pins the roster rows, the sheet figures and the
//    animated run — the shapes GameExecution's editor tick walks. Run via Scratchpad/CheckSceneFeed.sh.
#include "../Projects/Project-Zero/Source/EditorFeedSequence.h"
#include "../Projects/Project-Zero/Source/ShowroomStructure.h"
#include "../Engine/ContentInterchange/SceneCodec.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int Failures = 0;

void CheckTrue(const char* Label, bool Passed)
{
    std::printf("  %-58s %s\n", Label, Passed ? "PASS" : "FAIL");
    if (!Passed) ++Failures;
}

bool Near(float A, float B, float Tol) { return std::fabs(A - B) <= Tol; }

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::ProjectZero;

    SceneStructure Level;
    std::string Error;
    const std::string Path = "Projects/Project-Zero/Content/Scenes/CornellBox.gltf";
    if (!SceneCodec::Decode(Path, Level, nullptr, SceneDecodeConfiguration{}, &Error))
    {
        std::printf("[Feed] decode failed: %s\n>>> FAILURES (decode)\n", Error.c_str());
        return 1;
    }

    EditorFeedSequence Feed;
    EditorInstance Rows[kMaxEditorInstances] = {};
    const uint32_t RowCount = Feed.FillRoster(Rows, Level);

    std::printf("[Feed] roster over %zu placements\n", Level.QueryPlacements().size());
    CheckTrue("four folders, eleven objects, fly camera: 16 rows", RowCount == 16u);
    const auto RowIs = [&](uint32_t R, const char* Label, EditorInstanceCategory Cat, uint32_t Depth)
    {
        return R < RowCount && std::strcmp(Rows[R].Label, Label) == 0 && Rows[R].Category == Cat
            && Rows[R].Depth == Depth;
    };
    CheckTrue("Room folds the five static walls", RowIs(0u, "Room", EditorInstanceCategory::Folder, 0u)
              && Rows[0].KidCount == 5u && RowIs(1u, "Floor", EditorInstanceCategory::Geometry, 1u)
              && RowIs(2u, "Ceiling", EditorInstanceCategory::Geometry, 1u)
              && RowIs(3u, "Back Wall", EditorInstanceCategory::Geometry, 1u)
              && RowIs(4u, "Left Wall", EditorInstanceCategory::Geometry, 1u)
              && RowIs(5u, "Right Wall", EditorInstanceCategory::Geometry, 1u));
    CheckTrue("Objects folds the five flagged dynamics",
              RowIs(6u, "Objects", EditorInstanceCategory::Folder, 0u) && Rows[6].KidCount == 5u
                  && RowIs(7u, "Tall Box", EditorInstanceCategory::Geometry, 1u)
                  && RowIs(8u, "Short Box", EditorInstanceCategory::Geometry, 1u)
                  && RowIs(9u, "Sphere", EditorInstanceCategory::Geometry, 1u)
                  && RowIs(10u, "Cone", EditorInstanceCategory::Geometry, 1u)
                  && RowIs(11u, "Torus", EditorInstanceCategory::Geometry, 1u) && Rows[7].Dynamic
                  && Rows[8].Dynamic && Rows[9].Dynamic && Rows[10].Dynamic && Rows[11].Dynamic);
    CheckTrue("Lighting folds the luminaire", RowIs(12u, "Lighting", EditorInstanceCategory::Folder, 0u)
              && Rows[12].KidCount == 1u
              && RowIs(13u, "Ceiling Luminaire", EditorInstanceCategory::Light, 1u));
    CheckTrue("Cameras folds the fly camera", RowIs(14u, "Cameras", EditorInstanceCategory::Folder, 0u)
              && Rows[14].KidCount == 1u && RowIs(15u, "Main Camera", EditorInstanceCategory::Camera, 1u));
    bool AllVisible = true;
    for (uint32_t R = 0u; R < RowCount; ++R) AllVisible = AllVisible && Rows[R].Visible;
    CheckTrue("every row is visible", AllVisible);
    CheckTrue("geometry rows wear their material albedo",
              Near(Rows[1].Tint[0], 0.75f, 0.01f) && Near(Rows[1].Tint[1], 0.75f, 0.01f)
                  && Near(Rows[1].Tint[2], 0.75f, 0.01f) && Near(Rows[4].Tint[0], 0.85f, 0.01f)
                  && Near(Rows[4].Tint[1], 0.12f, 0.01f) && Near(Rows[4].Tint[2], 0.12f, 0.01f));

    FlyThroughSolver Camera;
    std::vector<InstanceRecord> Live = Level.QueryInstances();

    std::printf("[Feed] sheets\n");
    EditorSheet Sheet = {};
    EditorProperty* Mirror = Feed.BuildSheet(0u, Rows, RowCount, &Sheet, Camera, Level, Live);
    CheckTrue("the Room folder counts its contents",
              Sheet.GroupCount == 1u && std::strcmp(Sheet.Groups[0].Title, "Group") == 0
                  && Sheet.Groups[0].PropertyCount == 2u
                  && std::strcmp(Sheet.Groups[0].Properties[0].Text, "5 direct \xc2\xb7 5 total") == 0);
    CheckTrue("the folder opens the tint mirror", Mirror != nullptr && Mirror->Swatches
              && Near(Mirror->ColourTint[0], Rows[0].Tint[0], 1e-6f));

    (void)Feed.BuildSheet(7u, Rows, RowCount, &Sheet, Camera, Level, Live);
    const float* At = Sheet.Groups[0].Properties[0].Axes;
    CheckTrue("the Tall Box sits on its baked centroid",
              Sheet.GroupCount == 2u && std::strcmp(Sheet.Groups[0].Title, "Transform") == 0
                  && Near(At[0], -0.90f, 0.02f) && Near(At[1], 2.70f, 0.02f) && Near(At[2], 0.90f, 0.02f));
    CheckTrue("baked rotation reads as identity", std::strcmp(Sheet.Groups[0].Properties[1].Text, "+0\xc2\xb0 about Z") == 0);
    const auto& TallRecords = Level.QueryMaterials().QueryRecords();
    const MaterialRecord& TallMat = TallRecords[Level.QueryInstances()[5u].MaterialIndex];
    CheckTrue("the Surface card mirrors the material record",
              std::strcmp(Sheet.Groups[1].Title, "Surface") == 0
                  && Near(Sheet.Groups[1].Properties[0].ColourTint[0], TallMat.AlbedoR, 1e-6f)
                  && Near(Sheet.Groups[1].Properties[1].Figure, TallMat.EmissiveR, 1e-6f)
                  && Near(Sheet.Groups[1].Properties[2].Figure, TallMat.Roughness, 1e-6f));
    CheckTrue("geometry opens no mirror",
              Feed.BuildSheet(7u, Rows, RowCount, &Sheet, Camera, Level, Live) == nullptr);

    Live[5u].World[12] += 1.0f;
    (void)Feed.BuildSheet(7u, Rows, RowCount, &Sheet, Camera, Level, Live);
    CheckTrue("a driven body shows where it IS",
              Near(Sheet.Groups[0].Properties[0].Axes[0], 0.10f, 0.02f));
    Live[5u].World[12] -= 1.0f;

    (void)Feed.BuildSheet(13u, Rows, RowCount, &Sheet, Camera, Level, Live);
    const MaterialRecord& LampMat = TallRecords[Level.QueryInstances()[10u].MaterialIndex];
    CheckTrue("the luminaire keeps its intensity",
              Sheet.GroupCount == 2u && Near(Sheet.Groups[0].Properties[0].Figure, LampMat.EmissiveR, 1e-4f));
    CheckTrue("the luminaire aims at nadir",
              std::strcmp(Sheet.Groups[1].Properties[0].Text, "-Z (nadir)") == 0);

    (void)Feed.BuildSheet(15u, Rows, RowCount, &Sheet, Camera, Level, Live);
    const Vector3 Eye = Camera.Convert<Vector3>();
    CheckTrue("the camera card follows the fly camera",
              Sheet.GroupCount == 3u && Near(Sheet.Groups[0].Properties[0].Axes[0], Eye.x, 1e-6f)
                  && Near(Sheet.Groups[1].Properties[0].Figure,
                          Camera.QueryFieldOfViewRadians() * 57.29578f, 1e-4f));




    Sheet.GroupCount = 7u;
    CheckTrue("a wild pick builds nothing",
              Feed.BuildSheet(99u, Rows, RowCount, &Sheet, Camera, Level, Live) == nullptr
                  && Sheet.GroupCount == 0u);

    std::printf("[Feed] animated span\n");
    uint32_t First = 0u, Count = 0u;
    CheckTrue("the five loose objects are the run",
              Feed.QueryAnimatedSpan(&First, &Count, Level) && First == 5u && Count == 5u);
    CheckTrue("null out-params refuse", !Feed.QueryAnimatedSpan(nullptr, &Count, Level));

    SceneStructure Empty;
    CheckTrue("an empty level still folds its folders", Feed.FillRoster(Rows, Empty) == 5u);
    CheckTrue("an empty level animates nothing", !Feed.QueryAnimatedSpan(&First, &Count, Empty));

    // The drop showroom: twelve dynamic bodies between the static scenery and the trailing luminaires. The
    //    physics bridge takes its body run from this query, so the proof pins it on the real level.
    std::printf("[Feed] drop span\n");
    ShowroomStructure DropRoom;
    DropRoom.Construct(12u);
    SceneEncodeConfiguration DropConfig;
    DropConfig.Name  = "ShowroomDrop";
    DropConfig.Spans = &DropRoom.QuerySpans();
    SceneStructure DropLevel;
    bool DropOk = SceneCodec::Encode("/tmp/DropSpanProof.gltf", DropRoom.QueryTriangles(), DropRoom.QueryMaterials(),
                                     &Error, DropConfig)
        && SceneCodec::Decode("/tmp/DropSpanProof.gltf", DropLevel, nullptr, SceneDecodeConfiguration{}, &Error);
    CheckTrue("the drop level round-trips", DropOk);
    CheckTrue("fourteen walls, twelve bodies, two lamps",
              DropLevel.QueryPlacements().size() == 26u && DropRoom.QuerySpans().size() == 26u);
    bool DropSpan = Feed.QueryAnimatedSpan(&First, &Count, DropLevel);
    CheckTrue("the bodies are one run of twelve", DropSpan && First == 12u && Count == 12u);
    CheckTrue("the run names read Body 01..12",
              DropOk && DropLevel.QueryPlacements()[12u].Name == "Body 01"
                  && DropLevel.QueryPlacements()[23u].Name == "Body 12");

    if (Failures == 0) std::printf("\n>>> ALL PASS (0 failures)\n");
    else               std::printf("\n>>> FAILURES (%d failures)\n", Failures);
    return Failures == 0 ? 0 : 1;
}
