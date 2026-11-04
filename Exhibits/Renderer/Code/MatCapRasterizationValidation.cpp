/*==========================================================================================================================================
                                                   MATCAPRASTERIZATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validate real Vulkan texture lookup, transformed normals, mode/texture changes, shared residency and analytical calibration captures.

#include "VulkanReadback.h"
#include "MatCapFixtures.h"

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     INDEPENDENT BILINEAR SAMPLING REFERENCE
//------------------------------------------------------------------------------------------------------------------------

std::array<double,3> NormalizeReference(std::array<double,3> Direction)
{
    const double Length = std::sqrt(Direction[0]*Direction[0]+Direction[1]*Direction[1]+Direction[2]*Direction[2]);
    for (auto &Coordinate : Direction) Coordinate /= Length;
    return Direction;
}

void ConfirmLookup(
    const std::vector<unsigned char> &Pixels,
    const Frontier::MatCapImage      &Image,
    std::array<double,3>              Normal,
    std::array<double,3>              Tint = {1,1,1})
{
    Normal = NormalizeReference(Normal);
    const double Horizontal = std::clamp((Normal[0]*.5+.5)*Image.Width-.5,0.0,double(Image.Width-1));
    const double Vertical   = std::clamp((.5-Normal[1]*.5)*Image.Height-.5,0.0,double(Image.Height-1));
    const auto Left = uint32_t(std::floor(Horizontal)), Top = uint32_t(std::floor(Vertical));
    const auto Right = std::min(Left+1,Image.Width-1), Bottom = std::min(Top+1,Image.Height-1);
    const double HorizontalFraction = Horizontal-Left, VerticalFraction = Vertical-Top;
    const size_t Centre = (64*128+64)*4;
    for (size_t Channel = 0; Channel < 3; ++Channel)
    {
        const auto Pixel = [&](uint32_t Column,uint32_t Row)
        {
            return Image.Rgba[(Row*Image.Width+Column)*4+Channel];
        };
        const double Upper = Pixel(Left,Top)*(1-HorizontalFraction)+Pixel(Right,Top)*HorizontalFraction;
        const double Lower = Pixel(Left,Bottom)*(1-HorizontalFraction)+Pixel(Right,Bottom)*HorizontalFraction;
        const double Expected = (Upper*(1-VerticalFraction)+Lower*VerticalFraction)*Tint[Channel];
        AssertRasterCondition(std::abs(double(Pixels[Centre+Channel])-Expected) <= 2,"MatCap lookup disagrees with independent CPU bilinear reference");
    }
    AssertRasterCondition(Pixels[Centre+3] == 255,"MatCap source alpha leaked into opaque shading");
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     EXECUTED DEVICE VALIDATION
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(ArgumentCount == 4,"Expected explicit driver, shader directory and capture directory");
        const std::filesystem::path Shaders(Arguments[2]), Evidence(Arguments[3]);
        std::filesystem::create_directories(Evidence);
        const auto SurfaceVertex = DecodeShaderArchive(Shaders / "CadSurfaceVertex.spv");
        const auto SurfaceFragment = DecodeShaderArchive(Shaders / "CadSurfaceFragment.spv");
        const auto EdgeVertex = DecodeShaderArchive(Shaders / "CadEdgeVertex.spv");
        const auto EdgeFragment = DecodeShaderArchive(Shaders / "CadEdgeFragment.spv");
        const auto MatCapVertex = DecodeShaderArchive(Shaders / "MatCapSurfaceVertex.spv");
        const auto MatCapFragment = DecodeShaderArchive(Shaders / "MatCapSurfaceFragment.spv");
        const SurfaceRasterShaders Archives{{SurfaceVertex,"main"},{SurfaceFragment,"main"},
            {EdgeVertex,"main"},{EdgeFragment,"main"},{MatCapVertex,"main"},{MatCapFragment,"main"}};
        const uint32_t MinimumApi = std::max({SurfaceVertex[1],SurfaceFragment[1],EdgeVertex[1],EdgeFragment[1],
                                             MatCapVertex[1],MatCapFragment[1]}) > 0x10300 ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1;
        RasterValidationDevice Device(Arguments[1],MinimumApi);
        SurfaceRasterPrograms Programs;
        Programs.Initialize(Device.QueryAccess(),Archives);
        const auto Gradient = ConstructMatCapGradient();
        SurfaceMatCap Lookup;
        Lookup.Initialize(Programs,Gradient);
        SurfaceGeometryResidence PlaneGeometry;
        PlaneGeometry.Initialize(Device.QueryAccess(),ConstructMatCapPlane());
        SurfaceRasterization Plane;
        Plane.Initialize(Programs,PlaneGeometry,128,128);
        SurfaceProjection Identity;
        Identity.ClipFromSurface = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const std::array<double,16> IdentityView{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        auto Projection = DeriveMatCapProjection(Identity,IdentityView);
        ConfirmVulkanResult(Device.ResetCommandPool(Device.Device,Device.Pool,0));
        VkCommandBufferBeginInfo Aborted{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        ConfirmVulkanResult(Device.BeginCommandBuffer(Device.Commands,&Aborted));
        Plane.ConstructCommands(Device.Commands,Projection,Lookup);
        ConfirmVulkanResult(Device.EndCommandBuffer(Device.Commands));
        AssertRasterCondition(Lookup.QueryUploadCount() == 0 && Plane.QueryGeometryUploadCount() == 0,
                              "Cancelled MatCap command construction confirmed an unsubmitted transfer");
        const auto Centre = Device.ExtractCapture(Plane,Projection,Lookup);
        ConfirmLookup(Centre,Gradient,{0,0,1});
        AssertRasterCondition(Lookup.QueryUploadCount() == 1,"Cancelled texture transfer was not retried exactly once");
        const double Diagonal = std::sqrt(.5);
        auto Rotation = IdentityView;
        Rotation[0] = Rotation[10] = Diagonal;
        Rotation[8] = Diagonal;
        Rotation[2] = -Diagonal;
        Projection = DeriveMatCapProjection(Identity,Rotation);
        const auto Rotated = Device.ExtractCapture(Plane,Projection,Lookup);
        ConfirmLookup(Rotated,Gradient,{Diagonal,0,Diagonal});
        AssertRasterCondition(Rotated != Centre,"Camera-space normal rotation left lookup pixels unchanged");
        auto Upward = IdentityView;
        Upward[5] = Upward[10] = Diagonal;
        Upward[9] = Diagonal;
        Upward[6] = -Diagonal;
        Projection = DeriveMatCapProjection(Identity,Upward);
        ConfirmLookup(Device.ExtractCapture(Plane,Projection,Lookup),Gradient,{0,Diagonal,Diagonal});
        auto BoundaryRotation = IdentityView;
        BoundaryRotation[0] = BoundaryRotation[10] = 0;
        BoundaryRotation[8] = 1;
        BoundaryRotation[2] = -1;
        ConfirmLookup(Device.ExtractCapture(Plane,DeriveMatCapProjection(Identity,BoundaryRotation),Lookup),Gradient,{1,0,0});
        BoundaryRotation = IdentityView;
        BoundaryRotation[5] = BoundaryRotation[10] = 0;
        BoundaryRotation[9] = 1;
        BoundaryRotation[6] = -1;
        ConfirmLookup(Device.ExtractCapture(Plane,DeriveMatCapProjection(Identity,BoundaryRotation),Lookup),Gradient,{0,1,0});
        Projection.SurfaceTint = {.5f,.25f,1,1};
        ConfirmLookup(Device.ExtractCapture(Plane,Projection,Lookup),Gradient,{0,Diagonal,Diagonal},{.5,.25,1});
        SurfaceGeometryResidence DiagonalGeometry;
        DiagonalGeometry.Initialize(Device.QueryAccess(),ConstructMatCapPlane({1,1,1}));
        SurfaceRasterization DiagonalTarget;
        DiagonalTarget.Initialize(Programs,DiagonalGeometry,128,128);
        auto ScaledView = IdentityView;
        ScaledView[0] = 4;
        ScaledView[10] = .5;
        ConfirmLookup(Device.ExtractCapture(DiagonalTarget,DeriveMatCapProjection(Identity,ScaledView),Lookup),Gradient,{.25,1,2});
        auto ShearedView = IdentityView;
        ShearedView[4] = .5;
        ConfirmLookup(Device.ExtractCapture(DiagonalTarget,DeriveMatCapProjection(Identity,ShearedView),Lookup),Gradient,{1,.5,1});
        auto ReflectedView = IdentityView;
        ReflectedView[0] = -1;
        ConfirmLookup(Device.ExtractCapture(DiagonalTarget,DeriveMatCapProjection(Identity,ReflectedView),Lookup),Gradient,{-1,1,1});
        auto Occluder = ConstructMatCapPlane();
        auto Rear = ConstructMatCapPlane({0,1,1});
        for (auto &Corner : Rear.Corners) Corner.Position[2] = .8f;
        const auto FirstRear = uint32_t(Occluder.Corners.size());
        Occluder.Corners.insert(Occluder.Corners.end(),Rear.Corners.begin(),Rear.Corners.end());
        for (const auto Index : Rear.TriangleCorners) Occluder.TriangleCorners.push_back(FirstRear+Index);
        SurfaceGeometryResidence OcclusionGeometry;
        OcclusionGeometry.Initialize(Device.QueryAccess(),Occluder);
        SurfaceRasterization OcclusionTarget;
        OcclusionTarget.Initialize(Programs,OcclusionGeometry,128,128);
        ConfirmLookup(Device.ExtractCapture(OcclusionTarget,DeriveMatCapProjection(Identity,IdentityView),Lookup),Gradient,{0,0,1});
        SurfaceGeometryResidence BracketGeometry;
        BracketGeometry.Initialize(Device.QueryAccess(),ConstructBracketFixture());
        SurfaceRasterization Bracket;
        Bracket.Initialize(Programs,BracketGeometry,960,720);
        const auto Camera = ProjectBracketFixture(960,720);
        const auto NormalView = DeriveBracketNormalView(960,720);
        const auto CadBefore = Device.ExtractCapture(Bracket,Camera);
        const auto CopperPixels = ConstructCopperMatCap();
        SurfaceMatCap Copper;
        Copper.Initialize(Programs,CopperPixels);
        const auto BracketProjection = DeriveMatCapProjection(Camera,NormalView);
        const auto BracketPixels = Device.ExtractCapture(Bracket,BracketProjection,Copper);
        AssertRasterCondition(BracketPixels != CadBefore,"MatCap mode did not change CAD pixels");
        const auto CadAfter = Device.ExtractCapture(Bracket,Camera);
        AssertRasterCondition(CadBefore == CadAfter,"MatCap mode damaged CAD rendering");
        AssertRasterCondition(Bracket.QueryGeometryUploadCount() == 1,"Mode switching re-uploaded shared geometry");
        EncodeRasterCapture(Evidence / "MatCapBracket.ppm",BracketPixels,960,720);
        SurfaceRasterization Thumbnail;
        Thumbnail.Initialize(Programs,BracketGeometry,320,240);
        const auto SmallCamera = ProjectBracketFixture(320,240);
        const auto Small = Device.ExtractCapture(Thumbnail,DeriveMatCapProjection(SmallCamera,NormalView),Copper);
        EncodeRasterCapture(Evidence / "MatCapThumbnail.ppm",Small,320,240);
        AssertRasterCondition(Thumbnail.QueryGeometryUploadCount() == 1 && Copper.QueryUploadCount() == 1,
                              "Thumbnail duplicated geometry or MatCap texture residency");
        MatCapImage Alternate{1,1,{32,96,224,0}};
        SurfaceMatCap AlternateLookup;
        const auto ExpectedAlternate = Alternate;
        AlternateLookup.Initialize(Programs,Alternate);
        Alternate.Rgba.assign(4,0);
        const auto Replacement = Device.ExtractCapture(Plane,DeriveMatCapProjection(Identity,IdentityView),AlternateLookup);
        ConfirmLookup(Replacement,ExpectedAlternate,{0,0,1});
        ConfirmLookup(Device.ExtractCapture(Plane,DeriveMatCapProjection(Identity,IdentityView),Lookup),Gradient,{0,0,1});
        AssertRasterCondition(Plane.QueryGeometryUploadCount() == 1 && Lookup.QueryUploadCount() == 1,
                              "Texture replacement re-uploaded geometry or the previous lookup");
        SurfaceGeometryResidence CalibrationGeometry;
        CalibrationGeometry.Initialize(Device.QueryAccess(),ConstructNormalCalibrationSphere());
        SurfaceRasterization Calibration;
        Calibration.Initialize(Programs,CalibrationGeometry,720,720);
        SurfaceProjection SphereCamera;
        SphereCamera.ClipFromSurface = {.95f,0,0,0,0,-.95f,0,0,0,0,-1.f/6,0,0,0,.5f,1};
        const auto SphereProjection = DeriveMatCapProjection(SphereCamera,IdentityView);
        const auto CalibrationPixels = Device.ExtractCapture(Calibration,SphereProjection,Copper);
        AssertRasterCondition(QueryBrightPixelCount(CalibrationPixels) > 30000,"MatCap normal calibration produced no meaningful surface");
        EncodeRasterCapture(Evidence / "MatCapCalibration.ppm",CalibrationPixels,720,720);
        Device.BeforeSubmission = [&]
        {
            Copper.Finalize();
            Programs.Finalize();
            CalibrationGeometry.Finalize();
        };
        const auto Retained = Device.ExtractCapture(Calibration,SphereProjection,Copper);
        Device.BeforeSubmission = {};
        AssertRasterCondition(Retained == CalibrationPixels,"Ending source scopes before submission invalidated retained MatCap resources");
        AssertRasterCondition(RasterValidationDevice::PipelineCreationCount == 3,"MatCap targets duplicated shared pipelines");
        AssertRasterCondition(RasterValidationDevice::SamplerCreationCount == 1,"MatCap textures duplicated the fixed shared sampler");
        std::ofstream Report(Evidence / "MatCapRasterChecks.json");
        Report << "{\n  \"result\": \"passed\",\n  \"device\": \"" << Device.Properties.deviceName << "\",\n"
               << "  \"device_type\": " << int(Device.Properties.deviceType) << ",\n"
               << "  \"graphics_pipeline_creation_count\": " << RasterValidationDevice::PipelineCreationCount << ",\n"
               << "  \"sampler_creation_count\": " << RasterValidationDevice::SamplerCreationCount << ",\n"
               << "  \"geometry_uploads_across_mode_changes\": " << Bracket.QueryGeometryUploadCount() << ",\n"
               << "  \"gradient_texture_uploads\": " << Lookup.QueryUploadCount() << ",\n"
               << "  \"checks\": [\"GPU/CPU bilinear lookup agreement\", \"View-space rotation and top-row convention\", "
               << "\"Display-referred RGB and ignored source alpha\", \"Tint\", \"Non-uniform inverse transpose\", "
               << "\"Shear and reflected transforms\", \"Opaque depth occlusion\", \"CAD mode round trip\", "
               << "\"Shared thumbnail residency\", \"Texture replacement and source pixel copy\", \"Clamp-to-edge boundary lookup\", \"Cancelled first-use retry\", "
               << "\"Smooth normal calibration\", \"Source finalization before queue submission\"],\n"
               << "  \"validation_layers_enabled\": false,\n  \"windows_execution_verified\": false\n}\n";
        std::cout << "PASS: MatCap Vulkan lookup, normal transforms, mode/texture changes, residency, depth and lifetimes\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
