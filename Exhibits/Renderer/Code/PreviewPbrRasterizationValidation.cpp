/*==========================================================================================================================================
                                                PREVIEWPBRRASTERIZATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Check bounded ordered scattering against scalar reference pixels, then capture specimens or serve real readbacks to HTML.

#include "PreviewPbrFixtures.h"
#include <sstream>

int main(int ArgumentCount,char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(ArgumentCount == 4 || ArgumentCount == 5,"Expected driver, shader directory, capture directory, optional --serve");
        const std::filesystem::path Destination(Arguments[3]);
        std::filesystem::create_directories(Destination);
        PreviewPbrFixtures Fixtures(Arguments[1],Arguments[2]);
        if (ArgumentCount == 5)
        {
            AssertRasterCondition(std::string(Arguments[4]) == "--serve","Unknown readback operation");
            std::cout << "READY\n" << std::flush;
            std::string Request;
            while (std::getline(std::cin,Request))
            {
                uint32_t Geometry = 0, Mode = 0;
                float Roughness = 0, Metallic = 0, Coating = 0, Exposure = 0;
                std::istringstream Input(Request);
                std::string Trailing;
                if (Request.size() > 160 || !(Input >> Geometry >> Mode >> Roughness >> Metallic >> Coating >> Exposure) ||
                    (Input >> Trailing) || Geometry > 1 || Mode > 2 || Coating < 0 || Coating > 1)
                {
                    std::cout << "REJECTED\n" << std::flush;
                    continue;
                }
                const auto Scattering = PreviewPbrFixtures::ConstructSpecimen(Roughness,Metallic,Coating,Exposure);
                if (!ValidatePreviewScattering(Scattering))
                {
                    std::cout << "REJECTED\n" << std::flush;
                    continue;
                }
                const auto Pixels = Fixtures.Capture(Geometry,Mode,Scattering);
                EncodeRasterCapture(Destination/"Current.ppm",Pixels,Geometry == 0 ? 960 : 720,720);
                std::cout << "CAPTURE " << Fixtures.Bracket.QueryGeometryUploadCount() << ' '
                          << Fixtures.Sphere.QueryGeometryUploadCount() << ' ' << RasterValidationDevice::PipelineCreationCount
                          << '\n' << std::flush;
            }
            return 0;
        }
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition,const char *Description)
        {
            AssertRasterCondition(Condition,Description);
            ++Checks;
        };
        PreviewScattering Scattering;
        Confirm(ValidatePreviewScattering(Scattering),"Default scattering rejected");
        for (uint32_t Category = 0; Category < 10; ++Category)
        {
            auto Invalid = Scattering;
            if (Category == 0) Invalid.Configuration[0] = 0;
            if (Category == 1) Invalid.Configuration[0] = 9;
            if (Category == 2) Invalid.Strata[0].AlbedoRoughness[3] = 0;
            if (Category == 3) Invalid.Strata[0].Optics[0] = 1.1f;
            if (Category == 4) Invalid.Strata[0].Optics[1] = .9f;
            if (Category == 5) Invalid.Strata[0].Optics[2] = .5f;
            if (Category == 6) Invalid.Strata[0].Classification[0] = 1;
            if (Category == 7) Invalid.Strata[0].AbsorptionThickness[0] = 1;
            if (Category == 8) Invalid.ViewFromSurface[0] = std::numeric_limits<float>::infinity();
            if (Category == 9) Invalid.EnvironmentUp = {0,0,0,0};
            Confirm(!ValidatePreviewScattering(Invalid),"Invalid scattering accepted");
        }
        SurfaceProjection Camera;
        Camera.ClipFromSurface = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const std::array<double,16> View{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const auto Projection = DeriveSurfaceNormalProjection(Camera,View);
        const auto ConfirmReference = [&](PreviewScattering Specification,const SurfaceTextureResidence &Texture)
        {
            Specification.EnvironmentHorizon[3] = 0;
            Specification.LightDirectionIntensity = {0,0,1,2};
            Specification.LightColourExposure = {1,1,1,1};
            SurfaceScatteringResidence Parameters;
            Parameters.Initialize(Fixtures.Programs,Specification);
            const auto Pixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,Texture,Parameters);
            auto Reference = Specification;
            if (Specification.Configuration[1])
                for (size_t Channel = 0; Channel < 3; ++Channel)
                    Reference.Strata[0].AlbedoRoughness[Channel] *= float(std::pow((128.0/255+.055)/1.055,2.4));
            const auto Expected = EvaluateScatteringSequence(Reference,1,1,1,1);
            const auto Centre = (64*128+64)*4;
            for (size_t Channel = 0; Channel < 3; ++Channel)
                Confirm(std::abs(Pixels[Centre+Channel]-255*EncodePreviewDisplay(Expected[Channel]*2,1)) <= 2,
                        "PBR GPU pixel differs from scalar GGX/coating reference");
            Confirm(Pixels[Centre+3] == 255,"PBR alpha is not opaque");
            return Pixels;
        };
        const auto Opaque = ConfirmReference(Scattering,Fixtures.White);
        Scattering.Strata[0].Optics[0] = 1;
        Confirm(Opaque != ConfirmReference(Scattering,Fixtures.White),"Metallic response unchanged");
        for (float Coverage : {0.f,.5f,1.f})
        {
            auto Coated = PreviewPbrFixtures::ConstructSpecimen(.35f,.4f,1,1);
            Coated.Strata[1].Optics[2] = Coverage;
            ConfirmReference(Coated,Fixtures.White);
        }
        auto Ordered = PreviewPbrFixtures::ConstructSpecimen(.4f,0,1,1);
        Ordered.Configuration[0] = 8;
        for (size_t Index = 2; Index < 8; ++Index)
        {
            Ordered.Strata[Index] = Ordered.Strata[1];
            Ordered.Strata[Index].Optics[2] = .25f;
        }
        ConfirmReference(Ordered,Fixtures.White);
        Ordered.Strata[1].Classification[0] = 0;
        Ordered.Strata[1].AbsorptionThickness = {0,0,0,0};
        ConfirmReference(Ordered,Fixtures.White);
        SurfaceTextureResidence Grey;
        Grey.Initialize(Fixtures.Programs,{1,1,{128,128,128,0}});
        Scattering.Configuration[1] = 1;
        ConfirmReference(Scattering,Grey);
        auto LifetimeSpecification = PreviewScattering{};
        LifetimeSpecification.EnvironmentHorizon[3] = 0;
        LifetimeSpecification.LightDirectionIntensity = {0,0,1,2};
        LifetimeSpecification.LightColourExposure = {1,1,1,1};
        const auto LifetimeReference = ConfirmReference(LifetimeSpecification,Fixtures.White);
        SurfaceTextureResidence RetainedTexture;
        RetainedTexture.Initialize(Fixtures.Programs,{1,1,{255,255,255,0}});
        SurfaceScatteringResidence RetainedParameters;
        RetainedParameters.Initialize(Fixtures.Programs,LifetimeSpecification);
        ConfirmVulkanResult(Fixtures.Device.ResetCommandPool(Fixtures.Device.Device,Fixtures.Device.Pool,0));
        VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        ConfirmVulkanResult(Fixtures.Device.BeginCommandBuffer(Fixtures.Device.Commands,&Begin));
        Fixtures.Plane.ConstructCommands(Fixtures.Device.Commands,Projection,RetainedTexture,RetainedParameters);
        ConfirmVulkanResult(Fixtures.Device.EndCommandBuffer(Fixtures.Device.Commands));
        Confirm(RetainedTexture.QueryUploadCount() == 0,"Cancelled PBR recording confirmed an unsubmitted upload");
        Fixtures.Device.BeforeSubmission = [&]()
        {
            RetainedTexture.Finalize();
            RetainedParameters.Finalize();
        };
        Confirm(Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,RetainedTexture,RetainedParameters) == LifetimeReference,
                "PBR recording did not retain finalized texture and uniform allocations before submission");
        Fixtures.Device.BeforeSubmission = {};
        bool DormantRejected = false;
        try
        {
            Fixtures.Plane.ConstructCommands(Fixtures.Device.Commands,Projection,Fixtures.White,SurfaceScatteringResidence{});
        }
        catch (const std::runtime_error &)
        {
            DormantRejected = true;
        }
        Confirm(DormantRejected,"Dormant scattering unexpectedly accepted");
        const auto Specimen = PreviewPbrFixtures::ConstructSpecimen(.32f,.8f,.75f,1);
        const auto PbrBracket = Fixtures.Capture(0,2,Specimen);
        const auto Cad = Fixtures.Capture(0,0,Specimen);
        Fixtures.Capture(0,1,Specimen);
        Confirm(Fixtures.Capture(0,2,Specimen) == PbrBracket,"CAD/MatCap/PBR round trip altered PBR pixels");
        Confirm(Fixtures.Capture(0,0,Specimen) == Cad,"PBR altered CAD pixels");
        auto Dark = Specimen;
        Dark.LightColourExposure[3] = 0;
        Confirm(Fixtures.Capture(0,2,Dark) != PbrBracket,"Exposure left image unchanged");
        auto NoEnvironment = Specimen;
        NoEnvironment.EnvironmentHorizon[3] = 0;
        Confirm(Fixtures.Capture(0,2,NoEnvironment) != PbrBracket,"Analytical environment left image unchanged");
        Confirm(QueryBrightPixelCount(PbrBracket) > 1000,"PBR specimen absent");
        Confirm(Fixtures.Bracket.QueryGeometryUploadCount() == 1,"Material changes reuploaded geometry");
        Confirm(RasterValidationDevice::PipelineCreationCount == 4,"PBR changes constructed per-material pipelines");
        EncodeRasterCapture(Destination/"PreviewPbrBracket.ppm",PbrBracket,960,720);
        EncodeRasterCapture(Destination/"PreviewPbrCalibration.ppm",Fixtures.Capture(1,2,Specimen),720,720);
        EncodeRasterCapture(Destination/"PreviewCadCalibration.ppm",Fixtures.Capture(1,0,Specimen),720,720);
        EncodeRasterCapture(Destination/"PreviewMatCapCalibration.ppm",Fixtures.Capture(1,1,Specimen),720,720);
        std::ofstream Report(Destination/"PreviewPbrChecks.json");
        Report << "{\"result\":\"passed\",\"checks\":" << Checks
               << ",\"shared_pipelines\":4,\"geometry_uploads\":1,\"software_vulkan\":true,\"windows_execution\":false}\n";
        std::cout << "PASS: " << Checks << " ordered scattering and Vulkan readback checks\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
