/*==========================================================================================================================================
                                                      SCENEVIEWPORTPICKVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 GPU picking against the shown triangles (own device): at sampled pixels of three views the occupant the pick finds — at the
//    picked pixel and at every pixel of its neighbourhood — is the nearest triangle a CPU ray cast meets through that pixel's centre;
//    the bounding-box test it replaced is measured on the same pixels; then the editor's path, a click's serial through the Everest
//    viewport to the Geometry record drawn under it.

#include "EverestSceneViewport.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/PickProjection.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/TransformProjection.h"
#include "Frontier/Engine/Scene/SceneWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using namespace Frontier::GlobalIllumination;

int Failures = 0;
int Checks   = 0;

void Report(bool Passed, const std::string& Line)
{
    ++Checks;
    Failures += Passed ? 0 : 1;
    std::printf("%s  %s\n", Passed ? "PASS" : "FAIL", Line.c_str());
}

// 📝 One drawn facet in world space: a corner, its two edges and the occupant the surface raster draws it for.
struct WorldFacet
{
    float    Corner[3];
    float    EdgeA[3];
    float    EdgeB[3];
    uint32_t Occupant;
};

// 📝 Every facet the surface raster draws (its draw runs, each run's instances, a fragmented set's per-facet occupant offsets).
std::vector<WorldFacet> CollectFacets(const SceneStructure& Scene)
{
    std::vector<WorldFacet> Facets;
    for (const DrawRun& Run : Scene.DrawRuns)
    {
        const FacetSet& Set   = Scene.FacetSets[Run.FacetSet];
        const size_t    Drawn = std::min<size_t>(Set.Facets.size(), Set.VertexCount / 3u);
        for (uint32_t Instance = 0; Instance < Run.Count; ++Instance)
            for (size_t Index = 0; Index < Drawn; ++Index)
            {
                const FacetEntry& Facet    = Set.Facets[Index];
                const uint32_t    Occupant = Run.First + Instance + Facet.OccupantOffset;
                if (Occupant >= Scene.Occupants.size()) continue;
                const Transform& Placement = Scene.Occupants[Occupant].Current;
                const Vector3    A         = TransformPoint(Placement, Facet.Corners[0]);
                const Vector3    B         = TransformPoint(Placement, Facet.Corners[1]);
                const Vector3    C         = TransformPoint(Placement, Facet.Corners[2]);
                WorldFacet       Entry{};
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Entry.Corner[Axis] = static_cast<float>(A[Axis]);
                    Entry.EdgeA[Axis]  = static_cast<float>(B[Axis] - A[Axis]);
                    Entry.EdgeB[Axis]  = static_cast<float>(C[Axis] - A[Axis]);
                }
                Entry.Occupant = Occupant;
                Facets.push_back(Entry);
            }
    }
    return Facets;
}

struct FacetHit
{
    double   Travel   = 0.0;   // [m] from the near plane
    double   Margin   = 0.0;   // [m] distance from the hit to the facet's nearest edge
    uint32_t Occupant = OccupantPick::NoOccupant;
};

// 📝 What a ray through a pixel's centre meets, and whether a rasteriser must agree: the nearest hit is decided unless another
//    occupant's facet lies within a relative 10⁻⁴ of its depth (coplanar overlap) or the hit sits within 0.05 px of an outline the
//    occupant does not continue past.
struct RayAnswer
{
    uint32_t Occupant = OccupantPick::NoOccupant;
    bool     Decided  = true;
};

RayAnswer CastRay(const std::vector<WorldFacet>& Facets, const Vector3& Origin, const Vector3& Direction, double PixelSlope)
{
    std::vector<FacetHit> Hits;
    for (const WorldFacet& Facet : Facets)
    {
        const double A[3] = {Facet.EdgeA[0], Facet.EdgeA[1], Facet.EdgeA[2]};
        const double B[3] = {Facet.EdgeB[0], Facet.EdgeB[1], Facet.EdgeB[2]};
        const double P[3] = {Direction[1] * B[2] - Direction[2] * B[1], Direction[2] * B[0] - Direction[0] * B[2], Direction[0] * B[1] - Direction[1] * B[0]};
        const double Determinant = A[0] * P[0] + A[1] * P[1] + A[2] * P[2];
        if (std::fabs(Determinant) < 1.0e-20) continue;
        const double Inverse = 1.0 / Determinant;
        const double T[3]    = {Origin[0] - Facet.Corner[0], Origin[1] - Facet.Corner[1], Origin[2] - Facet.Corner[2]};
        // 📝 A loose barycentric bound first; the outline test below works in pixels (a long, thin facet's barycentric margin
        //    says little about how far the ray passes it).
        const double U       = (T[0] * P[0] + T[1] * P[1] + T[2] * P[2]) * Inverse;
        if (U < -0.5 || U > 1.5) continue;
        const double Q[3] = {T[1] * A[2] - T[2] * A[1], T[2] * A[0] - T[0] * A[2], T[0] * A[1] - T[1] * A[0]};
        const double V    = (Direction[0] * Q[0] + Direction[1] * Q[1] + Direction[2] * Q[2]) * Inverse;
        if (V < -0.5 || U + V > 1.5) continue;
        const double Travel = (B[0] * Q[0] + B[1] * Q[1] + B[2] * Q[2]) * Inverse;
        if (Travel <= 0.0) continue;
        // 📝 Barycentric coordinate × the facet's height over that edge = the distance to the edge (negative: just outside it).
        const double Normal[3] = {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
        const double Twice     = std::sqrt(Normal[0] * Normal[0] + Normal[1] * Normal[1] + Normal[2] * Normal[2]);
        const double Lengths[3] = {std::sqrt((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2])),
                                   std::sqrt(B[0] * B[0] + B[1] * B[1] + B[2] * B[2]), std::sqrt(A[0] * A[0] + A[1] * A[1] + A[2] * A[2])};
        const double Weights[3] = {1.0 - U - V, U, V};
        double       Margin     = std::numeric_limits<double>::max();
        for (int Edge = 0; Edge < 3; ++Edge) Margin = std::min(Margin, Weights[Edge] * Twice / std::max(Lengths[Edge], 1.0e-12));
        if (Margin < -0.1 * PixelSlope * Travel) continue;   // [-] more than 0.1 px outside: not a candidate
        Hits.push_back({Travel, Margin, Facet.Occupant});
    }
    RayAnswer Answer;
    std::sort(Hits.begin(), Hits.end(), [](const FacetHit& Left, const FacetHit& Right) { return Left.Travel < Right.Travel; });
    // 📝 The first hit inside its facet; hits just outside (negative margin) only take part in the outline test below.
    const auto Inside = std::find_if(Hits.begin(), Hits.end(), [](const FacetHit& Hit) { return Hit.Margin >= 0.0; });
    if (Inside == Hits.end())
    {
        // 📝 A miss is decided unless a facet passes within 0.05 px of the ray.
        for (const FacetHit& Hit : Hits) Answer.Decided = Answer.Decided && -Hit.Margin > 0.05 * PixelSlope * Hit.Travel;
        return Answer;
    }
    Answer.Occupant = Inside->Occupant;
    const double Tolerance = std::max(1.0e-4 * Inside->Travel, 1.0e-4);
    for (const FacetHit& Hit : Hits)
    {
        const bool Near = std::fabs(Hit.Travel - Inside->Travel) <= Tolerance;
        // 📝 Another occupant at the same depth, or one in front just outside its outline, could take the pixel.
        if (Hit.Occupant != Inside->Occupant && (Near || Hit.Travel < Inside->Travel) && std::fabs(Hit.Margin) <= 0.05 * PixelSlope * Hit.Travel)
            Answer.Decided = false;
        if (Hit.Occupant != Inside->Occupant && Near && Hit.Margin >= 0.0) Answer.Decided = false;
    }
    // 📝 Close to its facet's edge the occupant must continue past it (a neighbouring facet of its own at the same depth).
    if (Inside->Margin <= 0.05 * PixelSlope * Inside->Travel)
    {
        bool Continued = false;
        for (const FacetHit& Hit : Hits)
            Continued = Continued || (&Hit != &*Inside && Hit.Occupant == Inside->Occupant && std::fabs(Hit.Travel - Inside->Travel) <= Tolerance);
        Answer.Decided = Answer.Decided && Continued;
    }
    return Answer;
}

// 📝 For a disagreement: how the ray passes an occupant's facets (the hit with the widest margin, negative when outside every facet).
std::string DescribePassage(const std::vector<WorldFacet>& Facets, const Vector3& Origin, const Vector3& Direction, double PixelSlope, uint32_t Occupant)
{
    double BestMargin = -std::numeric_limits<double>::max(), BestTravel = 0.0;
    for (const WorldFacet& Facet : Facets)
    {
        if (Facet.Occupant != Occupant) continue;
        const double A[3] = {Facet.EdgeA[0], Facet.EdgeA[1], Facet.EdgeA[2]};
        const double B[3] = {Facet.EdgeB[0], Facet.EdgeB[1], Facet.EdgeB[2]};
        const double P[3] = {Direction[1] * B[2] - Direction[2] * B[1], Direction[2] * B[0] - Direction[0] * B[2], Direction[0] * B[1] - Direction[1] * B[0]};
        const double Determinant = A[0] * P[0] + A[1] * P[1] + A[2] * P[2];
        if (std::fabs(Determinant) < 1.0e-20) continue;
        const double T[3] = {Origin[0] - Facet.Corner[0], Origin[1] - Facet.Corner[1], Origin[2] - Facet.Corner[2]};
        const double U    = (T[0] * P[0] + T[1] * P[1] + T[2] * P[2]) / Determinant;
        const double Q[3] = {T[1] * A[2] - T[2] * A[1], T[2] * A[0] - T[0] * A[2], T[0] * A[1] - T[1] * A[0]};
        const double V    = (Direction[0] * Q[0] + Direction[1] * Q[1] + Direction[2] * Q[2]) / Determinant;
        const double Travel = (B[0] * Q[0] + B[1] * Q[1] + B[2] * Q[2]) / Determinant;
        if (Travel <= 0.0) continue;
        const double Normal[3] = {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
        const double Twice     = std::sqrt(Normal[0] * Normal[0] + Normal[1] * Normal[1] + Normal[2] * Normal[2]);
        const double Lengths[3] = {std::sqrt((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2])),
                                   std::sqrt(B[0] * B[0] + B[1] * B[1] + B[2] * B[2]), std::sqrt(A[0] * A[0] + A[1] * A[1] + A[2] * A[2])};
        const double Weights[3] = {1.0 - U - V, U, V};
        double       Margin     = std::numeric_limits<double>::max();
        for (int Edge = 0; Edge < 3; ++Edge) Margin = std::min(Margin, Weights[Edge] * Twice / std::max(Lengths[Edge], 1.0e-12));
        if (Margin > BestMargin)
        {
            BestMargin = Margin;
            BestTravel = Travel;
        }
    }
    char Text[160];
    std::snprintf(Text, sizeof(Text), "#%u at %.4f m, %.3f px %s its facets", Occupant, BestTravel, std::fabs(BestMargin) / (PixelSlope * std::max(BestTravel, 1.0e-9)),
                  BestMargin >= 0.0 ? "inside" : "outside");
    return Text;
}

// 📝 The replaced method: the nearest bounding box of a record's drawn part (its facet set's local bounds in its placement) that the
//    ray enters; a box around the camera counts at its far wall. Occupants no record owns held no box.
uint32_t CastBoxes(const SceneStructure& Scene, const std::vector<char>& Owned, const Vector3& Origin, const Vector3& Direction)
{
    uint32_t Nearest = OccupantPick::NoOccupant;
    double   Best    = std::numeric_limits<double>::max();
    for (const DrawRun& Run : Scene.DrawRuns)
        for (uint32_t Occupant = Run.First; Occupant < Run.First + Run.Count; ++Occupant)
        {
            if (!Owned[Occupant]) continue;
            const OccupantEntry& Entry   = Scene.Occupants[Occupant];
            const FacetSet&      Set     = Scene.FacetSets[Entry.FacetSet];
            const Vector3        Minimum = Entry.LocalMinimum ? *Entry.LocalMinimum : Set.LocalMinimum;
            const Vector3        Maximum = Entry.LocalMaximum ? *Entry.LocalMaximum : Set.LocalMaximum;
            const Transform      Inverse = InvertTransform(Entry.Current);
            const Vector3        Local   = TransformPoint(Inverse, Origin);
            const Vector3        Toward  = TransformDirection(Inverse, Direction);
            double               Enter = 0.0, Leave = std::numeric_limits<double>::max();
            bool                 Missed = false;
            for (int Axis = 0; Axis < 3 && !Missed; ++Axis)
            {
                if (std::fabs(Toward[Axis]) < 1.0e-12)
                {
                    Missed = Local[Axis] < Minimum[Axis] || Local[Axis] > Maximum[Axis];
                    continue;
                }
                double Near = (Minimum[Axis] - Local[Axis]) / Toward[Axis];
                double Far  = (Maximum[Axis] - Local[Axis]) / Toward[Axis];
                if (Near > Far) std::swap(Near, Far);
                Enter  = std::max(Enter, Near);
                Leave  = std::min(Leave, Far);
                Missed = Enter > Leave;
            }
            if (!Missed && Enter <= 1.0e-9) Enter = Leave;
            if (Missed || Enter >= Best) continue;
            Best    = Enter;
            Nearest = Occupant;
        }
    return Nearest;
}

struct ViewSample
{
    const char*    Name;
    ViewportCamera Camera;
};

ViewportCamera LookFrom(const Vector3& Position, const Vector3& Target)
{
    ViewportCamera Camera;
    Camera.Position = Position;
    Camera.Target   = Target;
    return Camera;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    try
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        if (ArgumentCount < 3) throw std::runtime_error("usage: SceneViewportPickValidation <Projects/Everest> <ShaderRoot> [scratch]");
        const std::filesystem::path Project = std::filesystem::absolute(Arguments[1]);
        const std::filesystem::path Shaders = std::filesystem::absolute(Arguments[2]);
        const std::filesystem::path Scratch = ArgumentCount > 3 ? std::filesystem::absolute(Arguments[3]) : std::filesystem::temp_directory_path();
        std::filesystem::create_directories(Scratch);

        // ① the level and the bridge the editor uses, on a device of its own: low grade, distance-field GI, the analytic
        //   renderer presenting (picking does not depend on any of them; they keep the bring-up and refreshes short)
        Frontier::SceneWorld World;
        std::string          Diagnostic;
        if (!World.LoadLevelSpace(Project / "Content" / "Levels" / "Default.levelspace", &Diagnostic)) throw std::runtime_error(Diagnostic);
        auto* Pipeline = dynamic_cast<Frontier::RenderPipelineRecord*>(World.FindFirstByType("RenderPipeline"));
        if (Pipeline == nullptr) throw std::runtime_error("no Render settings record");
        Pipeline->AccessConfiguration().QualityTier          = 0;
        Pipeline->AccessConfiguration().HardwareRayQueryTier = false;
        Pipeline->AccessConfiguration().AnalyticIllumination = true;
        Pipeline->AccessConfiguration().AdvanceRevision();
        // 📝 A 4 m engine torus lying on open ground: its bounding box covers its 1.6 m hole.
        const Frontier::RecordToken TorusToken = World.ConstructRecord("Geometry", "Torus");
        {
            auto& Torus = dynamic_cast<Frontier::GeometryRecord*>(World.FindRecord(TorusToken))->AccessConfiguration();
            Torus.OccupantStems.clear();
            Torus.ClaimsArchive = false;
            Torus.ShapeName     = "Torus";
            const float At[3]   = {-44.0f, -21.0f, 0.6f};
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Torus.Spatial.PositionMetres[Axis] = At[Axis];
                Torus.Spatial.Scale[Axis]          = 4.0f;
            }
            Torus.AdvanceRevision();
        }
        Everest::EverestSceneViewport Bridge(Project, Shaders, nullptr);

        // 📝 The workspace's default view (SceneViewportFlight): position, heading 37.57°, elevation −5.23°.
        const double         Heading   = 37.57 * 3.141592653589793 / 180.0;
        const double         Elevation = -5.23 * 3.141592653589793 / 180.0;
        const Vector3        Eye{-61.9997, -34.0018, 9.0052};
        const Vector3        Forward{std::cos(Elevation) * std::cos(Heading), std::cos(Elevation) * std::sin(Heading), std::sin(Elevation)};
        Everest::SceneViewportRequest Request;
        Request.PanelWidth          = 320.0f;
        Request.PanelHeight         = 180.0f;
        Request.PixelDensity        = 1.0f;
        Request.CameraPosition      = Eye;
        Request.CameraTarget        = {Eye[0] + Forward[0] * 65.88, Eye[1] + Forward[1] * 65.88, Eye[2] + Forward[2] * 65.88};
        Request.DeltaSeconds        = 1.0 / 30.0;
        const auto Start = std::chrono::steady_clock::now();
        const Everest::SceneViewportRefresh First = Bridge.Render(Request, World, 0.0);
        std::printf("bring-up and first refresh: %.1f s — %s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(),
                    First.Status.c_str());
        Report(First.View != VK_NULL_HANDLE && First.Readout.Presenting, "the Everest viewport presents an image (" + std::to_string(First.Extent[0]) + " x " +
                                                                              std::to_string(First.Extent[1]) + ")");
        Frontier::GlobalIllumination::SceneViewport* Viewport = Bridge.AccessViewport();
        if (Viewport == nullptr || Viewport->QueryScene() == nullptr) throw std::runtime_error("no scene viewport: " + First.Status);
        const SceneStructure&         Scene  = *Viewport->QueryScene();
        const std::array<uint32_t, 2> Extent = First.Extent;

        // ② the CPU reference: every drawn facet in world space, and which occupants a Geometry record owns
        const std::vector<WorldFacet> Facets = CollectFacets(Scene);
        const auto&                   Objects = Bridge.QueryGeometry();
        std::vector<char>             Owned(Scene.Occupants.size(), 0);
        size_t                        OwnedCount = 0;
        for (uint32_t Occupant = 0; Occupant < Scene.Occupants.size(); ++Occupant)
        {
            Owned[Occupant] = Objects.QueryOwner(Occupant) ? 1 : 0;
            OwnedCount += static_cast<size_t>(Owned[Occupant]);
        }
        std::printf("reference: %zu drawn facets; %zu occupants owned by Geometry records\n", Facets.size(), OwnedCount);

        // ③ three views, a 6 × 4 grid of picks each; every fourth pick compares its whole neighbourhood
        Frontier::GlobalIllumination::ViewportCamera Default;
        Default.Position = Eye;
        Default.Target   = {Request.CameraTarget[0], Request.CameraTarget[1], Request.CameraTarget[2]};
        const ViewSample Views[] = {{"default view", Default},
                                    {"low, toward the tower", LookFrom({-34.0, -14.0, 2.4}, {-8.0, 6.0, 2.6})},
                                    {"high, over the course", LookFrom({-12.0, -46.0, 28.0}, {-10.0, 4.0, 0.0})}};
        std::string Table = "| View | Picks | Pixels compared | Decided | Agree | Bounding box names another record | Pick ms (mean) |\n"
                            "|---|---:|---:|---:|---:|---:|---:|\n";
        size_t TotalDecided = 0, TotalAgree = 0, TotalCompared = 0, TotalBox = 0, TotalPicks = 0, TotalCovered = 0, TotalMisnamed = 0;
        double TotalMilliseconds = 0.0;
        uint32_t                OwnedOccupant = OccupantPick::NoOccupant;
        std::array<uint32_t, 2> OwnedPixel{}, SkyPixel{};
        bool                    SkyFound = false;
        for (const ViewSample& View : Views)
        {
            const Transform ViewProjection = Frontier::GlobalIllumination::SceneViewport::ResolveViewProjection(View.Camera, Extent);
            const Transform Unprojection   = InvertTransform(ViewProjection);
            const double    PixelSlope     = 2.0 * std::tan(0.5 * View.Camera.VerticalFieldOfView) / Extent[1];
            size_t Decided = 0, Agree = 0, Compared = 0, BoxDiffers = 0, Picks = 0;
            double Milliseconds = 0.0;
            for (uint32_t Row = 0; Row < 4; ++Row)
                for (uint32_t Column = 0; Column < 6; ++Column)
                {
                    const std::array<uint32_t, 2> Pixel = {static_cast<uint32_t>((Column + 0.5) / 6.0 * Extent[0]), static_cast<uint32_t>((Row + 0.5) / 4.0 * Extent[1])};
                    OccupantPick Pick;
                    if (!Viewport->PickOccupants(View.Camera, Extent, Pixel, Pick, &Diagnostic)) throw std::runtime_error("pick failed: " + Diagnostic);
                    ++Picks;
                    Milliseconds += Pick.Milliseconds;
                    const bool Whole = (Row * 6 + Column) % 4 == 0;
                    const int  Reach = Whole ? static_cast<int>(Pick.Reach) : 0;
                    for (int OffsetY = -Reach; OffsetY <= Reach; ++OffsetY)
                        for (int OffsetX = -Reach; OffsetX <= Reach; ++OffsetX)
                        {
                            const int64_t X = static_cast<int64_t>(Pixel[0]) + OffsetX, Y = static_cast<int64_t>(Pixel[1]) + OffsetY;
                            if (X < 0 || Y < 0 || X >= Extent[0] || Y >= Extent[1]) continue;
                            const double  NdcX = (X + 0.5) / Extent[0] * 2.0 - 1.0;
                            const double  NdcY = 1.0 - (Y + 0.5) / Extent[1] * 2.0;
                            const Vector3 Near = TransformPoint(Unprojection, {NdcX, NdcY, 1.0});
                            const Vector3 Half = TransformPoint(Unprojection, {NdcX, NdcY, 0.5});
                            const Vector3 Direction = NormalizeVector(SubtractVector(Half, Near));
                            const RayAnswer Answer  = CastRay(Facets, Near, Direction, PixelSlope);
                            const uint32_t  Seen    = Pick.QueryOccupant(OffsetX, OffsetY);
                            ++Compared;
                            if (!Answer.Decided) continue;
                            ++Decided;
                            Agree += Seen == Answer.Occupant ? 1 : 0;
                            if (Seen != Answer.Occupant)
                                std::printf("      %s pixel (%lld, %lld): GPU %u (%s), triangles %u (%s)\n        GPU's: %s\n        reference's: %s\n", View.Name,
                                            static_cast<long long>(X), static_cast<long long>(Y), Seen, Seen < Scene.Occupants.size() ? Scene.Occupants[Seen].Name.c_str() : "-",
                                            Answer.Occupant, Answer.Occupant < Scene.Occupants.size() ? Scene.Occupants[Answer.Occupant].Name.c_str() : "-",
                                            DescribePassage(Facets, Near, Direction, PixelSlope, Seen).c_str(),
                                            DescribePassage(Facets, Near, Direction, PixelSlope, Answer.Occupant).c_str());
                            if (OffsetX == 0 && OffsetY == 0)
                            {
                                // 📝 Records compared, not parts: a box on another part of the same record selects the same thing.
                                const uint32_t Boxed = CastBoxes(Scene, Owned, Near, Direction);
                                const auto     Drawn = Answer.Occupant != OccupantPick::NoOccupant ? Objects.QueryOwner(Answer.Occupant) : std::nullopt;
                                BoxDiffers += (Boxed != OccupantPick::NoOccupant ? Objects.QueryOwner(Boxed) : std::nullopt) != Drawn ? 1 : 0;
                                // 📝 For the editor's path below: a pixel of the default view on a record's part, and one on the sky.
                                if (&View == &Views[0] && OwnedOccupant == OccupantPick::NoOccupant && Answer.Occupant != OccupantPick::NoOccupant &&
                                    Owned[Answer.Occupant])
                                {
                                    OwnedOccupant = Answer.Occupant;
                                    OwnedPixel    = Pixel;
                                }
                                if (&View == &Views[0] && !SkyFound && Answer.Occupant == OccupantPick::NoOccupant)
                                {
                                    SkyFound = true;
                                    SkyPixel = Pixel;
                                }
                            }
                        }
                }
            // 📝 The whole view on the CPU (every second pixel): where a bounding-box pick names another record than the one drawn.
            size_t Covered = 0, Misnamed = 0;
            for (uint32_t Y = 0; Y < Extent[1]; Y += 2)
                for (uint32_t X = 0; X < Extent[0]; X += 2)
                {
                    const double    NdcX      = (X + 0.5) / Extent[0] * 2.0 - 1.0;
                    const double    NdcY      = 1.0 - (Y + 0.5) / Extent[1] * 2.0;
                    const Vector3   Near      = TransformPoint(Unprojection, {NdcX, NdcY, 1.0});
                    const Vector3   Direction = NormalizeVector(SubtractVector(TransformPoint(Unprojection, {NdcX, NdcY, 0.5}), Near));
                    const RayAnswer Answer    = CastRay(Facets, Near, Direction, PixelSlope);
                    if (!Answer.Decided) continue;
                    const uint32_t Boxed = CastBoxes(Scene, Owned, Near, Direction);
                    const auto     Drawn = Answer.Occupant != OccupantPick::NoOccupant ? Objects.QueryOwner(Answer.Occupant) : std::nullopt;
                    ++Covered;
                    Misnamed += (Boxed != OccupantPick::NoOccupant ? Objects.QueryOwner(Boxed) : std::nullopt) != Drawn ? 1 : 0;
                }
            TotalCovered += Covered;
            TotalMisnamed += Misnamed;
            char Line[240];
            std::snprintf(Line, sizeof(Line), "| %s | %zu | %zu | %zu | %zu | %zu of %zu picks; %.1f %% of the view | %.0f |\n", View.Name, Picks, Compared, Decided, Agree,
                          BoxDiffers, Picks, 100.0 * Misnamed / std::max<size_t>(1, Covered), Milliseconds / std::max<size_t>(1, Picks));
            Table += Line;
            TotalDecided += Decided;
            TotalAgree += Agree;
            TotalCompared += Compared;
            TotalBox += BoxDiffers;
            TotalPicks += Picks;
            TotalMilliseconds += Milliseconds;
            Report(Decided > 0 && Agree == Decided, std::string(View.Name) + ": the GPU pick equals the nearest triangle at " + std::to_string(Agree) + " of " +
                                                        std::to_string(Decided) + " decided pixels (" + std::to_string(Compared - Decided) + " at an outline or a coplanar overlap)");
        }
        Report(TotalDecided >= TotalCompared * 9 / 10, std::to_string(TotalDecided) + " of " + std::to_string(TotalCompared) + " compared pixels are decided by the reference");
        std::printf("bounding boxes would have chosen another object than the one drawn at %zu of %zu picked pixels and at %zu of %zu pixels "
                    "of the three views (%.1f %%)\n", TotalBox, TotalPicks, TotalMisnamed, TotalCovered, 100.0 * TotalMisnamed / std::max<size_t>(1, TotalCovered));

        // ④ the torus's hole: a click there picks what is seen through it, where the torus's bounding box took the click
        {
            uint32_t TorusOccupant = OccupantPick::NoOccupant;
            for (uint32_t Occupant = 0; Occupant < Scene.Occupants.size(); ++Occupant)
                if (Objects.QueryOwner(Occupant) == TorusToken.Value) TorusOccupant = Occupant;
            const ViewportCamera          Hole           = LookFrom({-44.0, -27.0, 14.0}, {-44.0, -21.0, 0.6});
            const Transform               ViewProjection = Frontier::GlobalIllumination::SceneViewport::ResolveViewProjection(Hole, Extent);
            const Transform               Unprojection   = InvertTransform(ViewProjection);
            const Vector3                 Centre         = TransformPoint(ViewProjection, {-44.0, -21.0, 0.6});
            const std::array<uint32_t, 2> Pixel = {static_cast<uint32_t>((Centre[0] * 0.5 + 0.5) * Extent[0]), static_cast<uint32_t>((0.5 - Centre[1] * 0.5) * Extent[1])};
            OccupantPick Pick;
            if (!Viewport->PickOccupants(Hole, Extent, Pixel, Pick, &Diagnostic)) throw std::runtime_error("pick failed: " + Diagnostic);
            const double    NdcX      = (Pixel[0] + 0.5) / Extent[0] * 2.0 - 1.0;
            const double    NdcY      = 1.0 - (Pixel[1] + 0.5) / Extent[1] * 2.0;
            const Vector3   Near      = TransformPoint(Unprojection, {NdcX, NdcY, 1.0});
            const Vector3   Direction = NormalizeVector(SubtractVector(TransformPoint(Unprojection, {NdcX, NdcY, 0.5}), Near));
            const RayAnswer Answer    = CastRay(Facets, Near, Direction, 2.0 * std::tan(0.5 * Hole.VerticalFieldOfView) / Extent[1]);
            const uint32_t  Seen      = Pick.QueryOccupant(0, 0);
            const uint32_t  Boxed     = CastBoxes(Scene, Owned, Near, Direction);
            size_t          Ring      = 0;
            for (const uint32_t Occupant : Pick.Occupants) Ring += Occupant == TorusOccupant ? 1 : 0;
            const auto Picked = Objects.ResolvePickedRecord(Pick);
            Report(TorusOccupant != OccupantPick::NoOccupant && Answer.Decided && Seen == Answer.Occupant && Seen != TorusOccupant && Boxed == TorusOccupant &&
                       Picked != TorusToken.Value,
                   "the torus (" + (TorusOccupant != OccupantPick::NoOccupant ? "reserve occupant #" + std::to_string(TorusOccupant) : std::string("not placed")) +
                       ") through its hole (pixel " + std::to_string(Pixel[0]) + ", " + std::to_string(Pixel[1]) + "): the GPU pick finds #" + std::to_string(Seen) + " (" +
                       (Seen < Scene.Occupants.size() ? Scene.Occupants[Seen].Name : std::string("nothing")) + ") as the triangles do, the torus's box took it " +
                       "(#" + std::to_string(Boxed) + "); " + std::to_string(Ring) + " of 81 neighbourhood texels show the torus's ring");
        }

        // ⑤ the editor's path: a click's serial and stage point answered with the record drawn there, from the last image
        Report(OwnedOccupant != OccupantPick::NoOccupant && SkyFound, "the default view has a pixel on a record's part and one on the sky");
        if (OwnedOccupant != OccupantPick::NoOccupant)
        {
            Request.PickSerial = 1;
            Request.PickPoint  = {(OwnedPixel[0] + 0.5) / Extent[0], (OwnedPixel[1] + 0.5) / Extent[1]};
            const auto Clicked = Bridge.Render(Request, World, 1.0 / 30.0);
            const auto Owner   = Bridge.QueryGeometry().QueryOwner(OwnedOccupant);
            Report(Clicked.Readout.Pick.Serial == 1 && Clicked.Readout.Pick.Occupant == OwnedOccupant && Clicked.Readout.Pick.Record == Owner,
                   "a click on occupant #" + std::to_string(OwnedOccupant) + " answers serial " + std::to_string(Clicked.Readout.Pick.Serial) +
                       " with its record (" + std::to_string(Clicked.Readout.Pick.Record.value_or(0)) + ") in " +
                       std::to_string(static_cast<int>(Clicked.Readout.Pick.Milliseconds)) + " ms" +
                       (Clicked.Readout.Pick.Failure.empty() ? std::string() : " — " + Clicked.Readout.Pick.Failure));
        }
        if (SkyFound)
        {
            Request.PickSerial = 2;
            Request.PickPoint  = {(SkyPixel[0] + 0.5) / Extent[0], (SkyPixel[1] + 0.5) / Extent[1]};
            const auto Clicked = Bridge.Render(Request, World, 2.0 / 30.0);
            Report(Clicked.Readout.Pick.Serial == 2 && !Clicked.Readout.Pick.Record && Clicked.Readout.Pick.Failure.empty(),
                   "a click on the sky answers serial 2 with no record");
            const auto Again = Bridge.Render(Request, World, 3.0 / 30.0);
            Report(Again.Readout.Pick.Serial == 2, "an unchanged serial is not picked again (the answer stays)");
        }

        char Summary[320];
        std::snprintf(Summary, sizeof(Summary), "\nGPU picks: %zu, mean %.0f ms on this device; %zu of %zu decided pixels agree with the triangles; bounding boxes "
                                                "would differ at %zu of %zu picks and at %.1f %% of the views' pixels\n",
                      TotalPicks, TotalMilliseconds / std::max<size_t>(1, TotalPicks), TotalAgree, TotalDecided, TotalBox, TotalPicks,
                      100.0 * TotalMisnamed / std::max<size_t>(1, TotalCovered));
        Table += Summary;
        std::ofstream(Scratch / "SceneViewportPickValidation.md") << Table;
        std::printf("\n%s", Table.c_str());
        std::printf("SceneViewportPickValidation: %d of %d checks passed\n", Checks - Failures, Checks);
        return Failures == 0 ? 0 : 1;
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "SceneViewportPickValidation FAILED: %s\n", Failure.what());
        return 1;
    }
}
