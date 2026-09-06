//============================================================================================================================================
//                                                   SHOWROOMEXPORTPROOF.CPP
//============================================================================================================================================
// 🧩 Proves the showroom survives the exact path the renderer uses: ShowroomStructure → SceneCodec::Encode → a real
//    glTF on disk → SceneCodec::Decode → SceneStructure. This is what GameExecution performs on first run with
//    `--scene showroom`; here it runs without a GPU.
//
//    The luminaire count is the check that matters most. If the emissive quads did not survive encode/decode the
//    room would import perfectly and then render pitch black, which is exactly the kind of failure a geometry-only
//    proof misses.
//
//    Build: bash Scratchpad/ExportShowroomLevel.sh

#include "Projects/Project-Zero/Source/ShowroomStructure.h"
#include "Engine/ContentInterchange/SceneCodec.h"
#include "Engine/ContentInterchange/TextureIndex.h"

#include <cstdio>
#include <string>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-56s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

} // namespace

int main()
{
    const std::string Path = "Projects/Project-Zero/Content/Scenes/Showroom.gltf";

    //──────────────────────────────────────────────────────────────────────────
    // Export
    //──────────────────────────────────────────────────────────────────────────
    Frontier::ProjectZero::ShowroomStructure Showroom;
    Showroom.Construct();

    std::string Error;
    if (!Showroom.Export(Path, &Error))
    {
        std::printf("EXPORT FAILED: %s\n", Error.c_str());
        return 1;
    }
    std::printf("exported %s (%zu triangles, %zu materials)\n\n",
                Path.c_str(), Showroom.QueryTriangles().size(), Showroom.QueryMaterials().size());

    //──────────────────────────────────────────────────────────────────────────
    // Import — the same call GameExecution makes
    //──────────────────────────────────────────────────────────────────────────
    Frontier::SceneStructure           Level;
    Frontier::TextureIndex             Textures;
    Frontier::SceneDecodeConfiguration Configuration;

    if (!Frontier::SceneCodec::Decode(Path, Level, &Textures, Configuration, &Error))
    {
        std::printf("IMPORT FAILED: %s\n", Error.c_str());
        return 1;
    }

    // GameExecution names the level from the filename stem, so QueryName() is "Showroom" there and the camera
    //    branch keys off it. Decode itself does not set a scene name.
    Level.AssignName("Showroom");

    const Frontier::Vector3 Lo = Level.QueryBoundsMinimum();
    const Frontier::Vector3 Hi = Level.QueryBoundsMaximum();
    std::printf("imported bounds X[%.2f %.2f] Y[%.2f %.2f] Z[%.2f %.2f], %zu luminaire triangles\n\n",
                Lo.x, Hi.x, Lo.y, Hi.y, Lo.z, Hi.z, Level.QueryLuminaires().size());

    CheckTrue("level name drives the camera branch", Level.QueryName() == "Showroom");
    CheckTrue("bounds survive the round trip",
              Lo.x > -2.01f && Hi.x < 2.01f && Lo.y > -2.01f && Hi.y < 3.01f && Lo.z > -0.01f && Hi.z < 3.01f);
    CheckTrue("triangles survive the round trip", Level.QueryTriangleCount() == Showroom.QueryTriangles().size());
    CheckTrue("emissive quads became luminaires",  Level.QueryLuminaires().size() == 4u);
    CheckTrue("the room would not render black",   !Level.QueryLuminaires().empty());

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n" : "\n>>> ALL PASS (0 failures)\n", Failures);
    return Failures ? 1 : 0;
}
