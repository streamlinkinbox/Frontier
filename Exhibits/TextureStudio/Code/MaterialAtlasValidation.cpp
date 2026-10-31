/*==========================================================================================================================================
                                                        MATERIALATLASVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Execute shared material shading, GPU atlas copies, independent dirty tiles and gutter replication on an explicit Vulkan device.

#include "AtlasReadback.h"
#include "Engine/Render/MaterialPreviewGeometry.h"
#include "Engine/Render/MaterialThumbnailAtlas.h"
#include "Engine/Render/MaterialLibraryRendering.h"
#include <sstream>

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(Count == 4, "Expected driver, shader archives and evidence directory.");
        AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
        const std::filesystem::path Shaders(Arguments[2]), Destination(Arguments[3]);
        std::filesystem::create_directories(Destination);
        const std::array<const char *, 10> Names{"CadSurfaceVertex", "CadSurfaceFragment", "CadEdgeVertex", "CadEdgeFragment",
            "MatCapSurfaceVertex", "MatCapSurfaceFragment", "PreviewPbrSurfaceVertex", "PreviewPbrSurfaceFragment", "MaterialSurfaceVertex", "MaterialSurfaceFragment"};
        std::array<std::vector<uint32_t>, 10> Words;
        for (size_t Index = 0; Index < Names.size(); ++Index) Words[Index] = DecodeShaderArchive(Shaders / (std::string(Names[Index]) + ".spv"));
        SurfaceRasterPrograms Programs;
        Programs.Initialize(Device.QueryAccess(), {{Words[0],"main"},{Words[1],"main"},{Words[2],"main"},{Words[3],"main"},
            {Words[4],"main"},{Words[5],"main"},{Words[6],"main"},{Words[7],"main"},{Words[8],"main"},{Words[9],"main"}});
        auto Sphere = ConstructMaterialPreviewSphere();
        Sphere.MaterialCoordinatesEnabled = true;
        SurfaceGeometryResidence Geometry;
        Geometry.Initialize(Device.QueryAccess(), Sphere);
        SurfaceRasterization Target;
        Target.Initialize(Programs, Geometry, 128, 128);
        MaterialThumbnailAtlas Atlas;
        Atlas.Initialize(Device.QueryAccess(), 128, 2);
        AssertRasterCondition(Atlas.QueryCapacity() == 4 && Atlas.QueryExtent() == 260, "Atlas bounds incorrect.");
        MaterialDefinition Definition;
        auto Compiled = CompileSurfaceMaterial(Definition, Destination);
        SurfaceMaterialResidence Textures;
        Textures.Initialize(Programs, Compiled.Textures);
        auto Scattering = Compiled.Scattering;
        Scattering.Illumination.ViewFromSurface[14] = -3;
        SurfaceScatteringResidence Copper, Green;
        Scattering.Illumination.Strata[0].AlbedoRoughness = {.8f, .15f, .05f, .25f};
        Copper.Initialize(Programs, Scattering);
        Scattering.Illumination.Strata[0].AlbedoRoughness = {.03f, .7f, .1f, .65f};
        Green.Initialize(Programs, Scattering);
        SurfaceNormalProjection Projection;
        Projection.ClipFromSurface = {1,0,0,0,0,-1,0,0,0,0,-.4f,0,0,0,.5f,1};
        const auto Capture = [&](uint32_t Slot, const SurfaceScatteringResidence &Parameters)
        {
            return Device.Capture(Atlas.QueryImage(), {260,260}, [&](VkCommandBuffer Commands)
            {
                Target.ConstructCommands(Commands, Projection, Textures, Parameters);
                Atlas.ConstructCopy(Commands, Target, Slot);
            }, [&]() { Target.ConfirmSubmission(); Atlas.ConfirmSubmission(); });
        };
        auto First = Capture(0, Copper);
        AssertRasterCondition(QueryBrightPixelCount(First) > 100, "Material sphere contains no shaded pixels.");
        auto Second = Capture(1, Green);
        const auto Pixel = [](const auto &Pixels, int X, int Y) { return &Pixels[(size_t(Y) * 260 + X) * 4]; };
        for (int Y = 0; Y < 130; ++Y)
            for (int X = 0; X < 130; ++X)
                AssertRasterCondition(std::memcmp(Pixel(First,X,Y), Pixel(Second,X,Y), 4) == 0, "Dirty tile changed its neighbour.");
        auto Third = Capture(0, Green);
        for (int Y = 0; Y < 130; ++Y)
            for (int X = 130; X < 260; ++X)
                AssertRasterCondition(std::memcmp(Pixel(Second,X,Y), Pixel(Third,X,Y), 4) == 0, "Slot replacement changed a clean tile.");
        AssertRasterCondition(First != Third, "Material revision did not change real atlas pixels.");
        for (int Coordinate = 1; Coordinate <= 128; ++Coordinate)
        {
            AssertRasterCondition(std::memcmp(Pixel(Third,0,Coordinate), Pixel(Third,1,Coordinate), 4) == 0, "Left gutter differs.");
            AssertRasterCondition(std::memcmp(Pixel(Third,129,Coordinate), Pixel(Third,128,Coordinate), 4) == 0, "Right gutter differs.");
            AssertRasterCondition(std::memcmp(Pixel(Third,Coordinate,0), Pixel(Third,Coordinate,1), 4) == 0, "Top gutter differs.");
            AssertRasterCondition(std::memcmp(Pixel(Third,Coordinate,129), Pixel(Third,Coordinate,128), 4) == 0, "Bottom gutter differs.");
        }
        AssertRasterCondition(std::memcmp(Pixel(Third,0,0),Pixel(Third,1,1),4) == 0, "Corner gutter differs.");
        const auto Uv = Atlas.QueryUv(3);
        AssertRasterCondition(Uv.MinimumU > .5f && Uv.MaximumU < 1 && Uv.MinimumV > .5f && Uv.MaximumV < 1, "UVs escape gutters.");
        bool Rejected = false;
        try { (void)Atlas.QueryUv(4); } catch (const std::exception &) { Rejected = true; }
        AssertRasterCondition(Rejected, "Out-of-bounds slot accepted.");
        AssertRasterCondition(Target.QueryGeometryUploadCount() == 1, "Atlas rebakes uploaded geometry again.");
        AssertRasterCondition(RasterValidationDevice::PipelineCreationCount == 5, "Per-material pipelines created.");
        EncodeRasterCapture(Destination / "MaterialAtlas.ppm", Second, 260, 260);
        MaterialLibraryRendering Rendering;
        Rendering.Initialize(Device.QueryAccess(), Shaders);
        Rendering.ConfigureEnvironments("EngineContent/EnvironmentArchives");
        Rendering.RequestLibrary(std::filesystem::canonical("Exhibits/Renderer/Materials"), {"BrushedCopper.material", "CoatedCeramic.material"});
        Rendering.ConfigureSelection("BrushedCopper.material", Compiled);
        uint64_t Serial = 0;
        const auto CaptureLibrary = [&]()
        {
            ++Serial;
            const bool FirstPreview = !Rendering.QueryPreviewReady();
            const auto Extent = FirstPreview ? 512u : Rendering.QueryAtlasExtent();
            return Device.Capture(FirstPreview ? Rendering.QueryPreviewImage() : Rendering.QueryAtlasImage(), {Extent,Extent}, [&](VkCommandBuffer Commands)
            {
                Rendering.ConstructCommands(Commands, Serial - 1, Serial);
            }, [&]() { Rendering.ConfirmSubmission(); });
        };
        (void)CaptureLibrary();
        (void)CaptureLibrary();
        AssertRasterCondition(Rendering.QueryPreviewReady() && Rendering.QueryThumbnail("BrushedCopper.material").Ready,
            "Material library did not expose submitted preview/thumbnail images.");
        const auto Resident = CaptureLibrary();
        const auto ResidentAgain = CaptureLibrary();
        AssertRasterCondition(Resident == ResidentAgain && Rendering.QueryBakeCount() == 2, "Clean resident tiles were rebaked.");
        const auto StablePreviewView = Rendering.QueryPreviewView();
        const auto StableAtlasView = Rendering.QueryAtlasView();
        const auto CaptureInspection = [&]()
        {
            ++Serial;
            return Device.Capture(Rendering.QueryPreviewImage(), {512,512}, [&](VkCommandBuffer Commands)
            {
                Rendering.ConstructCommands(Commands, Serial - 1, Serial);
            }, [&]() { Rendering.ConfirmSubmission(); });
        };
        auto Inspection = Rendering.QueryPreviewConfiguration();
        auto PreviousInspection = CaptureInspection();
        EncodeRasterCapture(Destination / "InspectionShaderBall.ppm", PreviousInspection, 512, 512);
        for (uint32_t Shape = 1; Shape <= 3; ++Shape)
        {
            Inspection.Shape = MaterialPreviewShape(Shape);
            Rendering.ConfigurePreview(Inspection);
            const auto Pixels = CaptureInspection();
            AssertRasterCondition(Pixels != PreviousInspection, "Preview shape did not change GPU pixels.");
            EncodeRasterCapture(Destination / ("InspectionShape" + std::to_string(Shape) + ".ppm"), Pixels, 512, 512);
            PreviousInspection = Pixels;
        }
        for (uint32_t Environment = 1; Environment <= 3; ++Environment)
        {
            Inspection.Environment = Environment;
            Rendering.ConfigurePreview(Inspection);
            const auto Pixels = CaptureInspection();
            AssertRasterCondition(Pixels != PreviousInspection, "Environment did not change GPU pixels.");
            PreviousInspection = Pixels;
        }
        Inspection.Zoom = .5f;
        Rendering.ConfigurePreview(Inspection);
        const auto Zoomed = CaptureInspection();
        AssertRasterCondition(Zoomed != PreviousInspection, "Zoom did not change GPU projection.");
        Inspection.Exposure = .25f;
        Rendering.ConfigurePreview(Inspection);
        AssertRasterCondition(CaptureInspection() != Zoomed, "Exposure did not change GPU pixels.");
        const auto BeforePan = CaptureInspection();
        Inspection.PanX = .3f;
        Inspection.PanY = -.2f;
        Rendering.ConfigurePreview(Inspection);
        const auto Panned = CaptureInspection();
        AssertRasterCondition(Panned != BeforePan, "Pan did not change GPU image pixels.");
        Inspection.Yaw = .65f;
        Inspection.Pitch = -.25f;
        Rendering.ConfigurePreview(Inspection);
        AssertRasterCondition(CaptureInspection() != Panned, "Orbit did not change GPU image pixels.");
        AssertRasterCondition(CaptureLibrary() == Resident && Rendering.QueryBakeCount() == 2,
            "Preview configuration changed thumbnail atlas pixels or rebaked clean tiles.");
        AssertRasterCondition(Rendering.QueryPreviewView() == StablePreviewView && Rendering.QueryAtlasView() == StableAtlasView,
            "Preview changes replaced stable GPU image views.");
        Rendering.ConfigurePreview({});
        (void)CaptureInspection();
        auto LightingNeutral = Compiled;
        auto LightingAuthored = Compiled;
        LightingNeutral.Scattering.Illumination.LightDirectionIntensity = {-.4f,.7f,.6f,.35f};
        LightingNeutral.Scattering.Illumination.LightColourExposure = {1,1,1,.75f};
        LightingAuthored.Scattering.Illumination.LightDirectionIntensity = {.9f,-.1f,.4f,18.f};
        LightingAuthored.Scattering.Illumination.LightColourExposure = {.2f,.4f,1.f,4.f};
        Rendering.ConfigureSelection("PreviewRig/Neutral", LightingNeutral);
        const auto NeutralPreview = CaptureInspection();
        Rendering.ConfigureSelection("PreviewRig/Authored", LightingAuthored);
        const auto AuthoredLightPreview = CaptureInspection();
        AssertRasterCondition(NeutralPreview == AuthoredLightPreview,
            "Studio softbox inherited arbitrary light direction, intensity or tint from authored material sources.");
        Rendering.ConfigureSelection("BrushedCopper.material", Compiled);
        (void)CaptureInspection();
        const auto CapturePatternPreview = [&](float Red, float Green, float NormalX = .5f, float NormalZ = 1.f)
        {
            std::array<std::vector<float>,6> AuthoringSamples{{
                {Red,Green,.05f,1}, {NormalX,.5f,NormalZ,1}, {.5f,.5f,.5f,0},
                {0,0,0,0}, {0,0,0,0}, {1,1,1,0}}};
            std::array<std::span<const float>,6> AuthoringViews;
            for (size_t Index = 0; Index < AuthoringSamples.size(); ++Index) AuthoringViews[Index] = AuthoringSamples[Index];
            auto PatternMaterial = Compiled;
            PatternMaterial.AuthoringMaps = ConstructAuthoringMaterialMaps(1,1,AuthoringViews);
            Rendering.ConfigureSelection("Pattern/live-preview",PatternMaterial);
            AssertRasterCondition(!Rendering.QueryPreviewReady(), "Changed Pattern maps exposed a stale preview as current.");
            return CaptureInspection();
        };
        const auto PatternRed = CapturePatternPreview(.95f,.03f);
        const auto PatternGreen = CapturePatternPreview(.02f,.9f);
        AssertRasterCondition(PatternRed != PatternGreen,
            "Changed Float32 Pattern colour maps on the same preview selection did not update PBR pixels.");
        const auto PatternFlatNormal = CapturePatternPreview(.45f,.32f,.5f,1.f);
        const auto PatternTiltedNormal = CapturePatternPreview(.45f,.32f,.9f,.65f);
        AssertRasterCondition(PatternFlatNormal != PatternTiltedNormal,
            "Changed Float32 Pattern normal maps on the same preview selection did not update PBR pixels.");
        Rendering.ConfigureSelection("BrushedCopper.material", Compiled);
        Rendering.InvalidateThumbnails();
        (void)CaptureLibrary();
        (void)CaptureLibrary();
        AssertRasterCondition(Rendering.QueryBakeCount() == 2, "Unchanged applied/saved material triggered a bake.");
        auto Replacement = Compiled;
        Replacement.Scattering.Illumination.Strata[0].AlbedoRoughness = {.8f,.03f,.1f,.8f};
        Rendering.ConfigureSelection("BrushedCopper.material", Replacement);
        (void)CaptureLibrary();
        const auto Replaced = CaptureLibrary();
        AssertRasterCondition(Replaced != Resident && Rendering.QueryBakeCount() == 3, "Applied material revision did not rebake its tile.");
        const auto Generation = Rendering.QueryThumbnail("BrushedCopper.material").Generation;
        std::vector<std::filesystem::path> Replacements;
        for (uint32_t Index = 0; Index < 64; ++Index) Replacements.push_back("Eviction" + std::to_string(Index) + ".material");
        Rendering.RequestLibrary(std::filesystem::canonical("Exhibits/Renderer/Materials"), Replacements);
        AssertRasterCondition(!Rendering.AuthenticateThumbnail("BrushedCopper.material", Generation), "Evicted token authenticated.");
        Rendering.RequestLibrary(std::filesystem::canonical("Exhibits/Renderer/Materials"), {"BrushedCopper.material"});
        (void)CaptureLibrary();
        AssertRasterCondition(Rendering.QueryThumbnail("BrushedCopper.material").Generation > Generation,
            "Reused tile retained a stale slot generation.");
        Rendering.RequestLibrary(Destination, {"Missing.material"});
        ++Serial;
        (void)Device.Capture(Rendering.QueryAtlasImage(), {Rendering.QueryAtlasExtent(),Rendering.QueryAtlasExtent()},
            [&](VkCommandBuffer Commands) { Rendering.ConstructCommands(Commands, Serial - 1, Serial); },
            [&]() { Rendering.ConfirmSubmission(); });
        AssertRasterCondition(!Rendering.QueryThumbnail("BrushedCopper.material").Ready && !Rendering.QueryThumbnail("Missing.material").Ready,
            "Library replacement leaked a previous material's tile.");
        AssertRasterCondition(!Rendering.QueryThumbnailDiagnostic("Missing.material").empty(), "Failed tile has no diagnostic.");
        AssertRasterCondition(!Rendering.AuthenticateThumbnail("BrushedCopper.material", Generation), "Stale thumbnail token authenticated.");
        AssertRasterCondition(Generation != 0 && RasterValidationDevice::PipelineCreationCount == 10,
            "Shared library pipelines were rebuilt per thumbnail.");
        std::ofstream Report(Destination / "MaterialAtlasChecks.json");
        Report << "{\"result\":\"passed\",\"gpu_execution\":true,\"hardware_execution\":false,\"windows_execution\":false,"
            "\"atlas_images\":1,\"shared_pipelines\":5,\"geometry_uploads\":1,"
            "\"checks\":[\"shared authored material evaluation\",\"GPU-to-GPU atlas copies\",\"dirty tile isolation\","
            "\"slot replacement\",\"replicated gutters\",\"bounded UVs and slot refusal\",\"library submission readiness\",\"clean residency and no-op invalidation\",\"applied material invalidation\",\"root replacement and diagnostics\",\"cache eviction and stale token refusal\",\"four GPU inspection shapes\",\"four GPU lighting environments\",\"GPU zoom and exposure\",\"preview-only dirtying\",\"stable preview and atlas image views\",\"same-selection Float32 Pattern-map invalidation\",\"stale preview readiness refusal\"]}\n";
        std::cout << "PASS: material GPU atlas, dirty tile isolation, gutters and shared residency\n";
        return 0;
    }
    catch (const std::exception &Failure) { std::cerr << Failure.what() << '\n'; return 1; }
}
