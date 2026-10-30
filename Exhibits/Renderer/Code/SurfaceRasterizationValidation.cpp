/*==========================================================================================================================================
                                                   SURFACERASTERIZATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Execute the real Vulkan/Slang CAD rasterizer with an explicit driver, read back pixels and test surface/edge visibility.

#include "VulkanReadback.h"
#include "BracketFixture.h"

//------------------------------------------------------------------------------------------------------------------------
//                                                     EXECUTED VALIDATION
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(ArgumentCount == 4, "Expected explicit Vulkan driver, shader directory and evidence directory");
        const std::filesystem::path Shaders(Arguments[2]), Evidence(Arguments[3]);
        std::filesystem::create_directories(Evidence);
        auto Geometry = ConstructBracketFixture();
        std::string Diagnostic;
        AssertRasterCondition(ValidateSurfaceGeometry(Geometry, Diagnostic), "Authored bracket invalid");
        AssertRasterCondition(Geometry.Edges.size() == 24, "Only authored block edges belong in CAD, not triangulation diagonals");
        auto Invalid = Geometry;
        Invalid.TriangleCorners[0] = UINT32_MAX;
        AssertRasterCondition(!ValidateSurfaceGeometry(Invalid, Diagnostic), "Invalid triangle accepted");
        Invalid = Geometry;
        Invalid.Corners[0].Position[0] = std::numeric_limits<float>::infinity();
        AssertRasterCondition(!ValidateSurfaceGeometry(Invalid, Diagnostic), "Nonfinite coordinates accepted");
        Invalid = Geometry;
        Invalid.Edges[0].End = Invalid.Edges[0].Start;
        AssertRasterCondition(!ValidateSurfaceGeometry(Invalid, Diagnostic), "Zero-length edge accepted");
        const auto SurfaceVertex = DecodeShaderArchive(Shaders / "CadSurfaceVertex.spv");
        const auto SurfaceFragment = DecodeShaderArchive(Shaders / "CadSurfaceFragment.spv");
        const auto EdgeVertex = DecodeShaderArchive(Shaders / "CadEdgeVertex.spv");
        const auto EdgeFragment = DecodeShaderArchive(Shaders / "CadEdgeFragment.spv");
        const SurfaceRasterShaders Programs{{SurfaceVertex,"main"},{SurfaceFragment,"main"},
                                           {EdgeVertex,"main"},{EdgeFragment,"main"}};
        const uint32_t MinimumApi = std::max({SurfaceVertex[1],SurfaceFragment[1],EdgeVertex[1],EdgeFragment[1]}) > 0x10300
            ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1;
        RasterValidationDevice Device(Arguments[1],MinimumApi);
        SurfaceRasterPrograms SharedPrograms;
        SharedPrograms.Initialize(Device.QueryAccess(), Programs);
        SurfaceGeometryResidence SharedGeometry;
        SharedGeometry.Initialize(Device.QueryAccess(), Geometry);
        SurfaceRasterization Raster;
        Raster.Initialize(SharedPrograms, SharedGeometry, 960, 720);
        const auto Orthographic = Device.ExtractCapture(Raster, ProjectBracketFixture(960,720));
        AssertRasterCondition(QueryBrightPixelCount(Orthographic) > 500, "No visible CAD strokes in Vulkan readback");
        const auto Perspective = Device.ExtractCapture(Raster, ProjectBracketFixture(960,720,true));
        AssertRasterCondition(Perspective != Orthographic, "Perspective projection did not change pixels");
        AssertRasterCondition(Raster.QueryGeometryUploadCount() == 1, "Camera changes must not re-upload geometry");
        EncodeRasterCapture(Evidence / "CadOrthographic.ppm", Orthographic,960,720);
        EncodeRasterCapture(Evidence / "CadPerspective.ppm", Perspective,960,720);
        SurfaceRasterization Thumbnail;
        Thumbnail.Initialize(SharedPrograms, SharedGeometry, 320, 240);
        const auto Small = Device.ExtractCapture(Thumbnail, ProjectBracketFixture(320,240));
        AssertRasterCondition(Thumbnail.QueryGeometryUploadCount() == 1,"Thumbnail duplicated the viewport geometry upload");
        AssertRasterCondition(QueryBrightPixelCount(Small) > 100, "No visible thumbnail strokes");
        EncodeRasterCapture(Evidence / "CadThumbnail.ppm",Small,320,240);
        SurfaceGeometry Occlusion;
        Occlusion.Corners = {{{-.75f,-.75f,.4f},{0,0,1}},{{.75f,-.75f,.4f},{0,0,1}},
                             {{.75f,.75f,.4f},{0,0,1}},{{-.75f,.75f,.4f},{0,0,1}}};
        Occlusion.TriangleCorners = {0,1,2,0,2,3};
        Occlusion.Edges = {{{-.95f,0,.8f},{.95f,0,.8f}},{{-.5f,.2f,.2f},{.5f,.2f,.2f}}};
        SurfaceGeometryResidence OcclusionGeometry;
        OcclusionGeometry.Initialize(Device.QueryAccess(),Occlusion);
        SurfaceRasterization Visibility;
        Visibility.Initialize(SharedPrograms,OcclusionGeometry,128,128);
        SurfaceProjection Identity;
        Identity.ClipFromSurface = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        // 📝 Cancel first-use command construction: the next accepted submission must still copy geometry.
        ConfirmVulkanResult(Device.ResetCommandPool(Device.Device,Device.Pool,0));
        VkCommandBufferBeginInfo Aborted{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        ConfirmVulkanResult(Device.BeginCommandBuffer(Device.Commands,&Aborted));
        Visibility.ConstructCommands(Device.Commands,Identity);
        ConfirmVulkanResult(Device.EndCommandBuffer(Device.Commands));
        AssertRasterCondition(Visibility.QueryGeometryUploadCount() == 0,"Unsubmitted transfer counted as uploaded");
        const auto DepthPixels = Device.ExtractCapture(Visibility,Identity);
        AssertRasterCondition(Visibility.QueryGeometryUploadCount() == 1,"Cancelled transfer did not retry");
        AssertRasterCondition(DepthPixels[(64*128+64)*4] < 80, "A rear edge leaked through the opaque surface");
        AssertRasterCondition(DepthPixels[(77*128+64)*4] > 100, "A front edge was incorrectly occluded");
        AssertRasterCondition(DepthPixels[(64*128+123)*4] > 100, "Rear edge outside the occluder should remain visible");
        EncodeRasterCapture(Evidence / "CadDepthOcclusion.ppm",DepthPixels,128,128);
        SurfaceGeometry Clipping = Occlusion;
        Clipping.Edges = {{{-.2f,-.2f,-.2f},{.25f,.25f,1}},{{-.3f,.1f,-.3f},{.2f,.3f,-.1f}}};
        SurfaceGeometryResidence ClipGeometry;
        ClipGeometry.Initialize(Device.QueryAccess(),Clipping);
        SurfaceRasterization ClipTarget;
        ClipTarget.Initialize(SharedPrograms,ClipGeometry,128,128);
        auto NearProjection = Identity;
        NearProjection.ClipFromSurface[11] = 1;
        NearProjection.ClipFromSurface[14] = -.1f;
        NearProjection.ClipFromSurface[15] = 0;
        const auto ClipPixels = Device.ExtractCapture(ClipTarget,NearProjection);
        AssertRasterCondition(QueryBrightPixelCount(ClipPixels) > 5 && QueryBrightPixelCount(ClipPixels) < 512,
                              "Near/eye clipping produced missing strokes or an unbounded ribbon");
        EncodeRasterCapture(Evidence / "CadNearClip.ppm",ClipPixels,128,128);
        AssertRasterCondition(RasterValidationDevice::PipelineCreationCount == 2,"Targets duplicated shared graphics pipelines");
        SharedPrograms.Finalize();
        SharedGeometry.Finalize();
        const auto Retained = Device.ExtractCapture(Raster,ProjectBracketFixture(960,720));
        AssertRasterCondition(Retained == Orthographic,"Ending source scopes invalidated retained target resources");
        std::ofstream Report(Evidence / "CadRasterChecks.json");
        Report << "{\n  \"result\": \"passed\",\n  \"device\": \"" << Device.Properties.deviceName << "\",\n"
               << "  \"device_type\": " << int(Device.Properties.deviceType) << ",\n"
               << "  \"api_version\": " << Device.Properties.apiVersion << ",\n"
               << "  \"orthographic_bright_pixels\": " << QueryBrightPixelCount(Orthographic) << ",\n"
               << "  \"geometry_uploads_for_two_cameras\": " << Raster.QueryGeometryUploadCount() << ",\n"
               << "  \"graphics_pipeline_creation_count\": " << RasterValidationDevice::PipelineCreationCount << ",\n"
               << "  \"checks\": [\"Bounded valid geometry and explicit topology edges\", \"Invalid input refusal\", "
               << "\"Real offscreen Vulkan/Slang pixels\", \"Orthographic and perspective cameras\", "
               << "\"Resident geometry reuse\", \"320x240 thumbnail\", \"Front/rear edge depth occlusion\", \"Cancelled transfer retry\", \"Near/eye clipping\", "
               << "\"Shared pipeline pair across targets\", \"Retained resource lifetimes\"],\n"
               << "  \"validation_layers_enabled\": false,\n"
               << "  \"optional_device_features_enabled\": false,\n"
               << "  \"execution\": \"Explicit driver offscreen Vulkan; inspect device_type for software versus hardware. Not Windows UI validation.\"\n}\n";
        std::cout << "PASS: Vulkan CAD pixels, perspective, resident uploads, thumbnail, hidden edges; device "
                  << Device.Properties.deviceName << '\n';
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
