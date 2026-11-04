/*==========================================================================================================================================
                                                 MATERIALRASTERIZATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Execute authored-material compilation, scalar-reference texture checks and resident Vulkan material previews.

#include "PreviewPbrFixtures.h"
#include "Engine/Render/AnisotropicReflection.h"
#include "Engine/Render/VectorSurfaceMaterialBridge.h"
#include "Engine/Render/VectorSurfaceCompilation.h"
#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include <sstream>
#include <optional>

namespace
{
std::string ExtractText(const std::filesystem::path &Path)
{
    std::ifstream Input(Path,std::ios::binary | std::ios::ate);
    const auto Size = Input.tellg();
    AssertRasterCondition(Input && Size >= 0 && Size <= 1048576,"Material file missing or oversized");
    std::string Text(static_cast<size_t>(Size),'\0');
    Input.seekg(0);
    Input.read(Text.data(),Size);
    AssertRasterCondition(bool(Input),"Material source read failed");
    return Text;
}

template<size_t Count>
bool ComparePyramids(const std::array<Frontier::MaterialTexturePyramid,Count> &Left,
                     const std::array<Frontier::MaterialTexturePyramid,Count> &Right)
{
    for (size_t Index = 0; Index < Count; ++Index)
    {
        if (Left[Index].Layers != Right[Index].Layers || Left[Index].Rgba != Right[Index].Rgba ||
            Left[Index].FloatRgba != Right[Index].FloatRgba || Left[Index].Levels.size() != Right[Index].Levels.size()) return false;
        for (size_t Level = 0; Level < Left[Index].Levels.size(); ++Level)
            if (Left[Index].Levels[Level].Width != Right[Index].Levels[Level].Width ||
                Left[Index].Levels[Level].Height != Right[Index].Levels[Level].Height) return false;
    }
    return true;
}

bool CompareTextures(const Frontier::CompiledSurfaceMaterial &Left,const Frontier::CompiledSurfaceMaterial &Right)
{
    return ComparePyramids(Left.Textures,Right.Textures) && ComparePyramids(Left.AuthoringMaps,Right.AuthoringMaps);
}
} // namespace

int main(int ArgumentCount,char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(ArgumentCount == 5 || ArgumentCount == 6,"Expected driver, shaders, destination, ContentDirectory, optional --serve");
        const std::filesystem::path Destination(Arguments[3]), ContentDirectory(Arguments[4]);
        std::filesystem::create_directories(Destination);
        PreviewPbrFixtures Fixtures(Arguments[1],Arguments[2],true);
        if (ArgumentCount == 6)
        {
            AssertRasterCondition(std::string(Arguments[5]) == "--serve","Unsupported material executable operation");
            std::unique_ptr<SurfaceMaterialResidence> CachedTextures;
            std::optional<CompiledSurfaceMaterial> PreviousMaterial;
            std::cout << "READY\n" << std::flush;
            std::string Line;
            while (std::getline(std::cin,Line))
            {
                try
                {
                    uint32_t Geometry = 0, Mode = 0;
                    float Roughness = 0, Metallic = 0, Coating = 0, Exposure = 0;
                    std::istringstream Input(Line);
                    std::string Trailing;
                    if (Line.size() > 160 || !(Input >> Geometry >> Mode >> Roughness >> Metallic >> Coating >> Exposure) ||
                        (Input >> Trailing) || Geometry > 1 || Mode > 3 || !std::isfinite(Coating) || Coating < 0 || Coating > 1)
                        throw std::runtime_error("Invalid material preview request");
                    const auto Specification = PreviewPbrFixtures::ConstructSpecimen(Roughness,Metallic,Coating,Exposure);
                    if (!ValidatePreviewScattering(Specification)) throw std::runtime_error("Preview coefficients outside supported range");
                    std::vector<unsigned char> Pixels;
                    if (Mode == 3)
                    {
                        const auto Authored = DecodeSurfaceMaterial(ExtractText(Destination/"Current.material"));
                        auto Compiled = CompileSurfaceMaterial(Authored,ContentDirectory);
                        if (!CachedTextures || !PreviousMaterial || !CompareTextures(*PreviousMaterial,Compiled))
                        {
                            auto Replacement = std::make_unique<SurfaceMaterialResidence>();
                            Replacement->Initialize(Fixtures.Programs,Compiled.Textures,Compiled.AuthoringMaps);
                            CachedTextures = std::move(Replacement);
                            PreviousMaterial = Compiled;
                        }
                        Compiled.Scattering.Illumination.LightColourExposure[3] = Exposure;
                        Pixels = Fixtures.CaptureMaterial(Geometry,Compiled.Scattering,*CachedTextures);
                    }
                    else Pixels = Fixtures.Capture(Geometry,Mode,Specification);
                    EncodeRasterCapture(Destination/"Current.ppm",Pixels,Geometry == 0 ? 960 : 720,720);
                    std::cout << "CAPTURE " << Fixtures.Bracket.QueryGeometryUploadCount() << ' '
                              << Fixtures.Sphere.QueryGeometryUploadCount() << ' ' << RasterValidationDevice::PipelineCreationCount
                              << ' ' << (CachedTextures ? CachedTextures->QueryUploadCount() : 0) << '\n' << std::flush;
                }
                catch (const std::exception &Failure)
                {
                    std::string Diagnostic = Failure.what();
                    for (auto &Character : Diagnostic) if (Character == '\n' || Character == '\r') Character = ' ';
                    std::cout << "ERROR " << Diagnostic.substr(0,1024) << '\n' << std::flush;
                }
            }
            return 0;
        }
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition,const char *Description)
        {
            AssertRasterCondition(Condition,Description);
            ++Checks;
        };
        const auto Reject = [&](const std::string &Text)
        {
            bool Refused = false;
            try { (void)DecodeSurfaceMaterial(Text); } catch (const std::exception &) { Refused = true; }
            Confirm(Refused,"Invalid/unsupported authored material accepted");
        };
        const std::string Minimal = "Format = 1\nName = 'Reference'\n[[Strata]]\nCategory = 'Opaque'\n";
        auto Definition = DecodeSurfaceMaterial(Minimal);
        const auto RoundTrip = DecodeSurfaceMaterial(EncodeSurfaceMaterial(Definition));
        Confirm(EncodeSurfaceMaterial(RoundTrip) == EncodeSurfaceMaterial(Definition),"Material TOML round trip changed authored meaning");
        for (const auto &Invalid : {"Format=2\nName='x'", "Format=1\nName='x'\nUnknown=1", "Format=1\nName='x'\n[[Strata]]\nCategory='Transmission'",
            "Format=1\nName='x'\n[[Strata]]\nCategory='Coating'", "Format=1\nName='x'\n[[Strata]]\nCategory='Opaque'\nRoughness=nan",
            "Format=1\nName='x'\n[[Strata]]\nCategory='Opaque'\nMetallic=2", "Format=1\nName='x'\n[[Strata]]\nCategory='Opaque'\nTextureMask=16",
            "Format=1\nName='x'\n[[Strata]]\nCategory='Opaque'\nSpecular=1.1",
            "Format=1\nName='x'\n[[Strata]]\nCategory='Opaque'\nHeightStrength=-0.1"}) Reject(Invalid);
        Reject(Minimal+"Roughness = 'bad'\n");
        Reject(Minimal+"Albedo = [1, 2]\n");
        auto Compiled = CompileSurfaceMaterial(Definition,ContentDirectory);
        for (const auto &Texture : Compiled.Textures) Confirm(ValidateMaterialPyramid(Texture),"Default texture pyramid invalid");
        for (const auto &Path : {"../Escape.png","/Outside.png","C:/Outside.png","Textures/Missing.png"})
        {
            auto Invalid = Definition;
            Invalid.TextureSources[0] = Path;
            bool Refused = false;
            try { (void)CompileSurfaceMaterial(Invalid,ContentDirectory); } catch (const std::exception &) { Refused = true; }
            Confirm(Refused,"Escaping/missing texture reference accepted");
        }
        const auto LinearMip = ConstructMaterialPyramid({2,1,{0,0,0,255,255,255,255,255}},MaterialTextureCategory::Albedo);
        Confirm(std::abs(int(LinearMip.Rgba[8])-188) <= 1,"sRGB mip was averaged in encoded space");
        const auto OddMip = ConstructMaterialPyramid({3,1,{0,0,0,255,0,0,0,255,255,255,255,255}},MaterialTextureCategory::OcclusionRoughnessMetallic);
        Confirm(OddMip.Rgba[12] == 85,"Odd-dimension mip dropped edge pixels");
        const std::array<float,16> FloatPixels{.1f,.2f,.3f,1,.3f,.4f,.5f,1,.5f,.6f,.7f,1,.7f,.8f,.9f,1};
        const auto FloatPyramid = ConstructFloatMaterialPyramid(2,2,MaterialTextureCategory::OcclusionRoughnessMetallic,FloatPixels);
        Confirm(FloatPyramid.FloatRgba.size() == 20 && FloatPyramid.Levels.size() == 2 &&
                FloatPyramid.Levels[1].Offset == 16*sizeof(float),"Float32 mip offsets or dimensions invalid");
        Confirm(std::abs(FloatPyramid.FloatRgba[16]-.4f) < 1e-6 && ValidateMaterialPyramid(FloatPyramid),
                "Float32 mip averaging lost source precision");
        auto InvalidFloatPixels = FloatPixels;
        InvalidFloatPixels[3] = std::numeric_limits<float>::quiet_NaN();
        bool RejectedFloatPixels = false;
        try { (void)ConstructFloatMaterialPyramid(2,2,MaterialTextureCategory::Albedo,InvalidFloatPixels); }
        catch (const std::exception &) { RejectedFloatPixels = true; }
        Confirm(RejectedFloatPixels,"Non-finite Float32 material samples accepted");
        auto Broken = LinearMip;
        Broken.Levels.push_back({1,1,12});
        Broken.Rgba.resize(16);
        Confirm(!ValidateMaterialPyramid(Broken),"Repeated terminal mip accepted");
        SurfaceProjection Camera;
        Camera.ClipFromSurface = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const std::array<double,16> View{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const auto Projection = DeriveSurfaceNormalProjection(Camera,View);
        Compiled.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        Compiled.Scattering.Illumination.LightDirectionIntensity = {0,0,1,2};
        Compiled.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        const auto CompareReference = [&](CompiledSurfaceMaterial Specification,double AlbedoMultiplier)
        {
            SurfaceMaterialResidence Textures;
            Textures.Initialize(Fixtures.Programs,Specification.Textures);
            SurfaceScatteringResidence Parameters;
            Parameters.Initialize(Fixtures.Programs,Specification.Scattering);
            const auto Pixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,Textures,Parameters);
            auto Reference = Specification.Scattering.Illumination;
            for (size_t Channel = 0; Channel < 3; ++Channel) Reference.Strata[0].AlbedoRoughness[Channel] *= float(AlbedoMultiplier);
            const auto Angular = EvaluateScatteringSequence(Reference,1,1,1,1);
            const auto Centre = (64*128+64)*4;
            for (size_t Channel = 0; Channel < 3; ++Channel)
                Confirm(std::abs(Pixels[Centre+Channel]-255*EncodePreviewDisplay(Angular[Channel]*2,1)) <= 2,
                        "Authored-material GPU pixel differs from independent scalar reference");
            Confirm(Pixels[Centre+3] == 255,"Material source alpha changed opaque output");
            Confirm(Textures.QueryUploadCount() == 10,"Material/authoring image upload count invalid");
            return Pixels;
        };
        const auto Plain = CompareReference(Compiled,1);
        Compiled.Scattering.TextureUse[0][0] = 1;
        Compiled.Textures[0] = ConstructMaterialPyramid({1,1,{128,128,128,0}},MaterialTextureCategory::Albedo);
        CompareReference(Compiled,std::pow((128.0/255+.055)/1.055,2.4));
        const std::array<float,4> FloatPixel{.12345679f,.12345679f,.12345679f,1};
        Compiled.Textures[0] = ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::Albedo,FloatPixel);
        CompareReference(Compiled,.12345679);
        Compiled.Textures[0] = LinearMip;
        Compiled.Scattering.UvScaleOffset = {0,0,.5f,.5f};
        CompareReference(Compiled,.5);
        Compiled.Scattering.UvScaleOffset = {512,512,0,0};
        CompareReference(Compiled,std::pow((188.0/255+.055)/1.055,2.4));
        auto AuthoredMaps = CompileSurfaceMaterial(Definition,ContentDirectory);
        AuthoredMaps.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        AuthoredMaps.Scattering.Illumination.LightDirectionIntensity = {0,0,1,2};
        AuthoredMaps.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        const auto BaseParameters = AuthoredMaps.Scattering.Illumination.Strata[0].AlbedoRoughness;
        const float BaseMetalness = AuthoredMaps.Scattering.Illumination.Strata[0].Optics[0];
        const std::array<float,4> AuthoredColour{.9f,.25f,.4f,.6f};
        const std::array<float,4> AuthoredRoughness{.76f,0,0,.75f};
        const std::array<float,4> AuthoredMetalness{.82f,0,0,.5f};
        AuthoredMaps.AuthoringMaps[0] = ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::Albedo,AuthoredColour);
        AuthoredMaps.AuthoringMaps[3] = ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,AuthoredRoughness);
        AuthoredMaps.AuthoringMaps[4] = ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,AuthoredMetalness);
        SurfaceMaterialResidence AuthoredMapTextures;
        AuthoredMapTextures.Initialize(Fixtures.Programs,AuthoredMaps.Textures,AuthoredMaps.AuthoringMaps);
        SurfaceScatteringResidence AuthoredMapParameters;
        AuthoredMapParameters.Initialize(Fixtures.Programs,AuthoredMaps.Scattering);
        const auto AuthoredPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,AuthoredMapTextures,AuthoredMapParameters);
        auto AuthoredReference = AuthoredMaps.Scattering.Illumination;
        for (size_t Channel = 0; Channel < 3; ++Channel)
            AuthoredReference.Strata[0].AlbedoRoughness[Channel] = std::lerp(BaseParameters[Channel],AuthoredColour[Channel],AuthoredColour[3]);
        AuthoredReference.Strata[0].AlbedoRoughness[3] = std::lerp(BaseParameters[3],AuthoredRoughness[0],AuthoredRoughness[3]);
        AuthoredReference.Strata[0].Optics[0] = std::lerp(BaseMetalness,AuthoredMetalness[0],AuthoredMetalness[3]);
        const auto AuthoredAngular = EvaluateScatteringSequence(AuthoredReference,1,1,1,1);
        for (size_t Channel = 0; Channel < 3; ++Channel)
            Confirm(std::abs(AuthoredPixels[(64*128+64)*4+Channel]-255*EncodePreviewDisplay(AuthoredAngular[Channel]*2,1)) <= 2,
                "Float32 authoring colour/roughness/metalness differs from independent PBR reference");
        auto GoldThread = CompileSurfaceMaterial(Definition,ContentDirectory);
        GoldThread.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        GoldThread.Scattering.Illumination.LightDirectionIntensity = {0,0,1,2};
        GoldThread.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        SurfaceMaterialResidence GoldBaselineTextures;
        GoldBaselineTextures.Initialize(Fixtures.Programs,GoldThread.Textures);
        SurfaceScatteringResidence GoldBaselineParameters;
        GoldBaselineParameters.Initialize(Fixtures.Programs,GoldThread.Scattering);
        const auto GoldBaselinePixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,GoldBaselineTextures,GoldBaselineParameters);
        const std::array<float,16> GoldColour{1,1,1,0,1,1,1,0,.9f,.66f,.18f,1,.9f,.66f,.18f,1};
        const std::array<float,16> GoldMetalness{0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1};
        const std::array<float,16> GoldNormal{.5f,.5f,1,0,.5f,.5f,1,0,.5f,.5f,1,0,.5f,.5f,1,0};
        const std::array<float,16> NeutralHeight{.5f,.5f,.5f,0,.5f,.5f,.5f,0,.5f,.5f,.5f,0,.5f,.5f,.5f,0};
        const std::array<float,16> NeutralRoughness{1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0};
        const std::array<float,16> NeutralOcclusion{1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0};
        const std::array<std::span<const float>,6> GoldMapSamples{GoldColour,GoldNormal,NeutralHeight,NeutralRoughness,GoldMetalness,NeutralOcclusion};
        GoldThread.AuthoringMaps = ConstructAuthoringMaterialMaps(4,1,GoldMapSamples);
        SurfaceMaterialResidence GoldTextures;
        GoldTextures.Initialize(Fixtures.Programs,GoldThread.Textures,GoldThread.AuthoringMaps);
        SurfaceScatteringResidence GoldParameters;
        GoldParameters.Initialize(Fixtures.Programs,GoldThread.Scattering);
        const auto GoldPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,GoldTextures,GoldParameters);
        for (const auto [Column, Metallic] : {std::pair<size_t,float>{32,BaseMetalness},std::pair<size_t,float>{96,1.f}})
        {
            auto Reference = GoldThread.Scattering.Illumination;
            if (Metallic == 1)
            {
                Reference.Strata[0].AlbedoRoughness[0] = GoldColour[8];
                Reference.Strata[0].AlbedoRoughness[1] = GoldColour[9];
                Reference.Strata[0].AlbedoRoughness[2] = GoldColour[10];
                Reference.Strata[0].Optics[0] = 1;
            }
            const auto Angular = EvaluateScatteringSequence(Reference,1,1,1,1);
            for (size_t Channel = 0; Channel < 3; ++Channel)
            {
                const double Expected = Metallic == BaseMetalness ? GoldBaselinePixels[(64*128+Column)*4+Channel]
                    : 255*EncodePreviewDisplay(Angular[Channel]*2,1);
                Confirm(std::abs(GoldPixels[(64*128+Column)*4+Channel]-Expected) <= 3,
                    "Gold-thread metalness leaked into or failed to affect its independent fabric/thread texel");
            }
        }
        std::ifstream VectorShaderInput(std::filesystem::path(Arguments[2]) / "VectorSurfaceCompute.spv",std::ios::binary | std::ios::ate);
        const auto VectorShaderBytes = VectorShaderInput.tellg();
        Confirm(VectorShaderInput && VectorShaderBytes >= 20 && VectorShaderBytes % 4 == 0,
            "Float32 vector shader archive missing from the material validation shader directory");
        std::vector<uint32_t> VectorShaderWords(size_t(VectorShaderBytes)/4);
        VectorShaderInput.seekg(0);
        Confirm(bool(VectorShaderInput.read(reinterpret_cast<char *>(VectorShaderWords.data()),VectorShaderBytes)),
            "Float32 vector shader archive could not be read");
        VectorAuthoringDocument AuthoringDocument;
        VectorCoverageInstruction AuthoringRectangle;
        AuthoringRectangle.Bounds = {256,256,220,220};
        AuthoringRectangle.Colour = {.7f,.34f,.12f,1};
        AuthoringRectangle.ColourEnd = AuthoringRectangle.Colour;
        AuthoringRectangle.Paint[3] = 1;
        AuthoringDocument.Instructions.push_back(AuthoringRectangle);
        VectorCoverageInstruction AuthoringEllipse;
        AuthoringEllipse.Topology[0] = uint32_t(VectorCoverageCategory::Ellipse);
        AuthoringEllipse.Bounds = {256,256,96,96};
        AuthoringEllipse.Colour = {.92f,.68f,.19f,1};
        AuthoringEllipse.ColourEnd = AuthoringEllipse.Colour;
        AuthoringEllipse.Paint[3] = 1;
        AuthoringDocument.Instructions.push_back(AuthoringEllipse);
        VectorAuthoringLayer AuthoringLayer;
        AuthoringLayer.Token = 1;
        AuthoringLayer.Root = 0;
        AuthoringLayer.Name = "GPU to PBR bridge";
        AuthoringLayer.SurfaceRelief = .8f;
        AuthoringLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength = .28f;
        AuthoringLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Strength = .9f;
        AuthoringLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength = .42f;
        AuthoringLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Metalness)].Strength = .68f;
        AuthoringLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Occlusion)].Strength = .35f;
        AuthoringDocument.Layers.push_back(AuthoringLayer);
        VectorAuthoringLayer AuthoringGoldThread;
        AuthoringGoldThread.Token = 2;
        AuthoringGoldThread.Root = 1;
        AuthoringGoldThread.Name = "Separate gold thread";
        AuthoringGoldThread.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength = .18f;
        AuthoringGoldThread.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Metalness)].Strength = 1.f;
        AuthoringDocument.Layers.push_back(AuthoringGoldThread);
        AuthoringDocument.TokenClock = 2;
        uint64_t VectorSerial = 1;
        constexpr uint32_t AuthoringMapResolution = 512;
        VectorSurfaceMaterialBridge MaterialBridge;
        MaterialBridge.Initialize(Fixtures.Device.QueryAccess(),RasterShader{VectorShaderWords,"main"},
            AuthoringMapResolution,AuthoringMapResolution);
        Confirm(MaterialBridge.ConfigureDocument(AuthoringDocument,0),
            "Float32 material bridge did not configure its six authoring channels");
        ConfirmVulkanResult(Fixtures.Device.ResetCommandPool(Fixtures.Device.Device,Fixtures.Device.Pool,0));
        ConfirmVulkanResult(Fixtures.Device.ResetFences(Fixtures.Device.Device,1,&Fixtures.Device.Completion));
        VkCommandBufferBeginInfo BridgeBegin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        BridgeBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ConfirmVulkanResult(Fixtures.Device.BeginCommandBuffer(Fixtures.Device.Commands,&BridgeBegin));
        Confirm(MaterialBridge.ConstructCommands(Fixtures.Device.Commands,0,VectorSerial),
            "Float32 material bridge failed to record its six image dispatch/readback operations");
        ConfirmVulkanResult(Fixtures.Device.EndCommandBuffer(Fixtures.Device.Commands));
        VkSubmitInfo BridgeSubmission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        BridgeSubmission.commandBufferCount = 1;
        BridgeSubmission.pCommandBuffers = &Fixtures.Device.Commands;
        ConfirmVulkanResult(Fixtures.Device.QueueSubmit(Fixtures.Device.Queue,1,&BridgeSubmission,Fixtures.Device.Completion));
        MaterialBridge.ConfirmSubmission();
        bool PrematureMapsRefused = false;
        try { (void)MaterialBridge.RetrieveAuthoringMaps(VectorSerial-1); }
        catch (const std::exception &) { PrematureMapsRefused = true; }
        Confirm(PrematureMapsRefused,"Float32 material bridge exposed maps before its submission retired");
        ConfirmVulkanResult(Fixtures.Device.WaitForFences(Fixtures.Device.Device,1,&Fixtures.Device.Completion,VK_TRUE,UINT64_MAX));
        auto GpuAuthoringMaps = MaterialBridge.RetrieveAuthoringMaps(VectorSerial);
        const size_t FabricMapTexel = (160*AuthoringMapResolution+160)*4;
        const size_t GoldMapTexel = (256*AuthoringMapResolution+256)*4;
        Confirm(std::abs(GpuAuthoringMaps[3].FloatRgba[FabricMapTexel]-.42f) < .003f &&
                std::abs(GpuAuthoringMaps[3].FloatRgba[GoldMapTexel]-.18f) < .003f &&
                std::abs(GpuAuthoringMaps[4].FloatRgba[FabricMapTexel]-.68f) < .003f &&
                std::abs(GpuAuthoringMaps[4].FloatRgba[GoldMapTexel]-1.f) < .003f,
            "GPU material bridge mixed gold-thread roughness/metalness into dielectric fabric.");
        Confirm(GpuAuthoringMaps[2].FloatRgba[FabricMapTexel+3] > .9f &&
                GpuAuthoringMaps[2].FloatRgba[GoldMapTexel+3] > .9f &&
                std::abs(GpuAuthoringMaps[2].FloatRgba[FabricMapTexel] - GpuAuthoringMaps[2].FloatRgba[GoldMapTexel]) > .05f,
            "GPU material bridge did not preserve independent authored height-map samples.");
        Confirm(!MaterialBridge.ConfigureDocument(AuthoringDocument,VectorSerial),
            "Equivalent authoring document invalidated the Float32 material-map cache");
        AuthoringDocument.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength = .61f;
        AuthoringDocument.Layers[0].SurfaceRelief = .35f;
        Confirm(MaterialBridge.ConfigureDocument(AuthoringDocument,VectorSerial),
            "Changed authoring channel did not reconfigure the Float32 material bridge");
        ++VectorSerial;
        ConfirmVulkanResult(Fixtures.Device.ResetCommandPool(Fixtures.Device.Device,Fixtures.Device.Pool,0));
        ConfirmVulkanResult(Fixtures.Device.ResetFences(Fixtures.Device.Device,1,&Fixtures.Device.Completion));
        VkCommandBufferBeginInfo RevisionBegin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        RevisionBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ConfirmVulkanResult(Fixtures.Device.BeginCommandBuffer(Fixtures.Device.Commands,&RevisionBegin));
        Confirm(MaterialBridge.ConstructCommands(Fixtures.Device.Commands,VectorSerial-1,VectorSerial),
            "Changed authoring document failed to record a new Float32 material generation");
        ConfirmVulkanResult(Fixtures.Device.EndCommandBuffer(Fixtures.Device.Commands));
        VkSubmitInfo RevisionSubmission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        RevisionSubmission.commandBufferCount = 1;
        RevisionSubmission.pCommandBuffers = &Fixtures.Device.Commands;
        ConfirmVulkanResult(Fixtures.Device.QueueSubmit(Fixtures.Device.Queue,1,&RevisionSubmission,Fixtures.Device.Completion));
        MaterialBridge.ConfirmSubmission();
        ConfirmVulkanResult(Fixtures.Device.WaitForFences(Fixtures.Device.Device,1,&Fixtures.Device.Completion,VK_TRUE,UINT64_MAX));
        const auto RevisedMaps = MaterialBridge.RetrieveAuthoringMaps(VectorSerial);
        Confirm(std::abs(RevisedMaps[3].FloatRgba[FabricMapTexel]-.61f) < .003f &&
                std::abs(RevisedMaps[3].FloatRgba[GoldMapTexel]-.18f) < .003f &&
                std::abs(RevisedMaps[4].FloatRgba[FabricMapTexel]-.68f) < .003f &&
                std::abs(RevisedMaps[4].FloatRgba[GoldMapTexel]-1.f) < .003f,
            "Per-layer roughness update changed dielectric fabric without preserving separate gold-thread response");
        float MaximumFabricNormalChange = 0;
        for (size_t Pixel = 0; Pixel < size_t(AuthoringMapResolution) * AuthoringMapResolution * 4; ++Pixel)
            if (Pixel % 4 != 3)
                MaximumFabricNormalChange = std::max(MaximumFabricNormalChange,
                    std::abs(RevisedMaps[1].FloatRgba[Pixel] - GpuAuthoringMaps[1].FloatRgba[Pixel]));
        const float GoldNormalChange = std::abs(RevisedMaps[1].FloatRgba[GoldMapTexel+2] - GpuAuthoringMaps[1].FloatRgba[GoldMapTexel+2]);
        Confirm(MaximumFabricNormalChange > .02f && GoldNormalChange < .002f,
            "Height-relief calibration failed to update fabric tangent normals independently of the gold thread");
        MaterialBridge.Finalize();
        auto GpuAuthoredMaterial = CompileSurfaceMaterial(Definition,ContentDirectory);
        GpuAuthoredMaterial.AuthoringMaps = std::move(GpuAuthoringMaps);
        GpuAuthoredMaterial.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        GpuAuthoredMaterial.Scattering.Illumination.LightDirectionIntensity = {0,0,1,2};
        GpuAuthoredMaterial.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        for (size_t Index = 0; Index < View.size(); ++Index)
            GpuAuthoredMaterial.Scattering.Illumination.ViewFromSurface[Index] = float(View[Index]);
        SurfaceMaterialResidence GpuAuthoredTextures;
        GpuAuthoredTextures.Initialize(Fixtures.Programs,GpuAuthoredMaterial.Textures,GpuAuthoredMaterial.AuthoringMaps);
        SurfaceScatteringResidence GpuAuthoredParameters;
        GpuAuthoredParameters.Initialize(Fixtures.Programs,GpuAuthoredMaterial.Scattering);
        const auto GpuAuthoredPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,
            GpuAuthoredTextures,GpuAuthoredParameters);
        auto HeightOnlyMaterial = CompileSurfaceMaterial(Definition,ContentDirectory);
        HeightOnlyMaterial.AuthoringMaps[2] = GpuAuthoredMaterial.AuthoringMaps[2];
        HeightOnlyMaterial.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        HeightOnlyMaterial.Scattering.Illumination.LightDirectionIntensity = {.6f,.3f,.74f,2};
        HeightOnlyMaterial.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        auto HeightFlatParameters = HeightOnlyMaterial.Scattering;
        HeightFlatParameters.StratumNormal[0][1] = 0;
        SurfaceMaterialResidence HeightOnlyTextures;
        HeightOnlyTextures.Initialize(Fixtures.Programs,HeightOnlyMaterial.Textures,HeightOnlyMaterial.AuthoringMaps);
        SurfaceScatteringResidence HeightFlatResidence;
        HeightFlatResidence.Initialize(Fixtures.Programs,HeightFlatParameters);
        const auto HeightFlatPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,
            HeightOnlyTextures,HeightFlatResidence);
        HeightFlatParameters.StratumNormal[0][1] = 4;
        SurfaceScatteringResidence HeightReliefResidence;
        HeightReliefResidence.Initialize(Fixtures.Programs,HeightFlatParameters);
        const auto HeightReliefPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,
            HeightOnlyTextures,HeightReliefResidence);
        size_t HeightChangedPixels = 0;
        for (size_t Pixel = 0; Pixel < 128*128; ++Pixel)
            for (size_t Channel = 0; Channel < 3; ++Channel)
                if (std::abs(int(HeightFlatPixels[Pixel*4+Channel])-int(HeightReliefPixels[Pixel*4+Channel])) > 1)
                {
                    ++HeightChangedPixels;
                    break;
                }
        Confirm(HeightChangedPixels > 8,
            "Changing the height-map strength did not change integrated material-preview pixels.");
        auto SpecularParameters = HeightOnlyMaterial.Scattering;
        SpecularParameters.StratumNormal[0][1] = 0;
        SpecularParameters.DirectionalOptics[0][3] = 0;
        SurfaceScatteringResidence SpecularZeroResidence;
        SpecularZeroResidence.Initialize(Fixtures.Programs,SpecularParameters);
        const auto SpecularZeroPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,
            HeightOnlyTextures,SpecularZeroResidence);
        SpecularParameters.DirectionalOptics[0][3] = 1;
        SurfaceScatteringResidence SpecularFullResidence;
        SpecularFullResidence.Initialize(Fixtures.Programs,SpecularParameters);
        const auto SpecularFullPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,
            HeightOnlyTextures,SpecularFullResidence);
        Confirm(SpecularZeroPixels != SpecularFullPixels,
            "Changing dielectric specular level did not change integrated material-preview pixels.");
        const auto BridgeCaptureDirectory = Destination/"PbrBridgeFrames";
        std::filesystem::create_directories(BridgeCaptureDirectory);
        EncodeRasterCapture(BridgeCaptureDirectory/"MaterialBaseline.ppm",Plain,128,128);
        EncodeRasterCapture(BridgeCaptureDirectory/"GpuAuthoringMaps.ppm",GpuAuthoredPixels,128,128);
        const size_t GpuTexel = GoldMapTexel;
        const auto &GpuColourMap = GpuAuthoredMaterial.AuthoringMaps[0].FloatRgba;
        const auto &GpuRoughnessMap = GpuAuthoredMaterial.AuthoringMaps[3].FloatRgba;
        const auto &GpuMetalnessMap = GpuAuthoredMaterial.AuthoringMaps[4].FloatRgba;
        auto GpuReference = GpuAuthoredMaterial.Scattering.Illumination;
        for (size_t Channel = 0; Channel < 3; ++Channel)
            GpuReference.Strata[0].AlbedoRoughness[Channel] = std::lerp(
                GpuReference.Strata[0].AlbedoRoughness[Channel],GpuColourMap[GpuTexel+Channel],GpuColourMap[GpuTexel+3]);
        GpuReference.Strata[0].AlbedoRoughness[3] = std::lerp(
            GpuReference.Strata[0].AlbedoRoughness[3],GpuRoughnessMap[GpuTexel],GpuRoughnessMap[GpuTexel+3]);
        GpuReference.Strata[0].Optics[0] = std::lerp(
            GpuReference.Strata[0].Optics[0],GpuMetalnessMap[GpuTexel],GpuMetalnessMap[GpuTexel+3]);
        const auto GpuAngular = EvaluateScatteringSequence(GpuReference,1,1,1,1);
        for (size_t Channel = 0; Channel < 3; ++Channel)
            Confirm(std::abs(GpuAuthoredPixels[(64*128+64)*4+Channel]-
                255*EncodePreviewDisplay(GpuAngular[Channel]*2,1)) <= 2,
                "GPU-generated authoring maps differ from their integrated PBR pixel reference");
        auto RevisedAuthoredMaterial = GpuAuthoredMaterial;
        RevisedAuthoredMaterial.AuthoringMaps = RevisedMaps;
        SurfaceMaterialResidence RevisedAuthoringTextures;
        RevisedAuthoringTextures.Initialize(Fixtures.Programs,RevisedAuthoredMaterial.Textures,
            RevisedAuthoredMaterial.AuthoringMaps);
        const auto RevisedAuthoringPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,
            RevisedAuthoringTextures,GpuAuthoredParameters);
        size_t ChangedMaterialPixels = 0;
        for (size_t Pixel = 0; Pixel < 128*128; ++Pixel)
        {
            const size_t Offset = Pixel*4;
            bool Changed = false;
            for (size_t Channel = 0; Channel < 3; ++Channel)
                Changed = Changed || std::abs(int(RevisedAuthoringPixels[Offset+Channel])-int(GpuAuthoredPixels[Offset+Channel])) > 1;
            if (Changed) ++ChangedMaterialPixels;
        }
        const size_t GoldCapturePixel = (64*128+64)*4;
        Confirm(ChangedMaterialPixels > 0 &&
                std::equal(RevisedAuthoringPixels.begin()+GoldCapturePixel,
                    RevisedAuthoringPixels.begin()+GoldCapturePixel+3,GpuAuthoredPixels.begin()+GoldCapturePixel,
                    [](uint8_t Revised, uint8_t Original) { return std::abs(int(Revised)-int(Original)) <= 1; }),
            "Fabric roughness strength failed to update the PBR preview or altered the independent gold thread.");
        EncodeRasterCapture(BridgeCaptureDirectory/"UpdatedLayerRoughness.ppm",RevisedAuthoringPixels,128,128);
        std::ofstream BridgeFrames(BridgeCaptureDirectory/"BridgeFrames.json");
        BridgeFrames << "{\"result\":\"passed\",\"native_cpp_vulkan_capture\":true,\"resolution\":[128,128],"
            "\"authoring_map_resolution\":[512,512],"
            "\"frames\":[\"MaterialBaseline.ppm\",\"GpuAuthoringMaps.ppm\",\"UpdatedLayerRoughness.ppm\"],"
            "\"channel_source\":\"VectorSurfaceMaterialBridge GPU dispatch/readback\"}\n";
        Confirm(bool(BridgeFrames),"Integrated authoring/PBR frame manifest could not be written");
        auto Directional = CompileSurfaceMaterial(Definition,ContentDirectory);
        Directional.Scattering.TextureUse[0][0] = 4;
        Directional.Textures[2] = ConstructMaterialPyramid({1,1,{204,179,230,255}},MaterialTextureCategory::TangentNormal);
        Directional.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        Directional.Scattering.Illumination.LightDirectionIntensity = {.6f,.3f,.74f,2};
        Directional.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        SurfaceMaterialResidence DirectionalTextures;
        DirectionalTextures.Initialize(Fixtures.Programs,Directional.Textures);
        const auto Normalize = [](std::array<double,3> Direction)
        {
            const auto Length = std::sqrt(Direction[0]*Direction[0]+Direction[1]*Direction[1]+Direction[2]*Direction[2]);
            for (auto &Axis : Direction) Axis /= Length;
            return Direction;
        };
        const auto Light = Normalize({.6f,.3f,.74f});
        const auto Half = Normalize({Light[0],Light[1],Light[2]+1});
        for (double Reflection : {1.0,-1.0})
        {
            auto ReflectedView = View;
            ReflectedView[0] = Reflection;
            Directional.Scattering.Illumination.ViewFromSurface[0] = float(Reflection);
            SurfaceScatteringResidence Parameters;
            Parameters.Initialize(Fixtures.Programs,Directional.Scattering);
            const auto Pixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,DeriveSurfaceNormalProjection(Camera,ReflectedView),DirectionalTextures,Parameters);
            const auto Normal = Normalize({Reflection*(204.0/255*2-1),179.0/255*2-1,230.0/255*2-1});
            const double NormalLight = Normal[0]*Light[0]+Normal[1]*Light[1]+Normal[2]*Light[2];
            const double NormalHalf = Normal[0]*Half[0]+Normal[1]*Half[1]+Normal[2]*Half[2];
            const auto Angular = EvaluateScatteringSequence(Directional.Scattering.Illumination,Normal[2],NormalLight,NormalHalf,Half[2]);
            for (size_t Channel = 0; Channel < 3; ++Channel)
                Confirm(std::abs(Pixels[(64*128+64)*4+Channel]-255*EncodePreviewDisplay(Angular[Channel]*NormalLight*2,1)) <= 2,
                        "Tangent-normal reflection/handedness differs from scalar reference");
        }
        auto AuthoredNormalMaterial = CompileSurfaceMaterial(Definition,ContentDirectory);
        for (size_t Index = 0; Index < View.size(); ++Index)
            AuthoredNormalMaterial.Scattering.Illumination.ViewFromSurface[Index] = float(View[Index]);
        AuthoredNormalMaterial.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        AuthoredNormalMaterial.Scattering.Illumination.LightDirectionIntensity = {.6f,.3f,.74f,2};
        AuthoredNormalMaterial.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        const std::array<float,4> AuthoredNormalPixel{204.f/255,179.f/255,230.f/255,1};
        AuthoredNormalMaterial.AuthoringMaps[1] = ConstructFloatMaterialPyramid(1,1,
            MaterialTextureCategory::TangentNormal,AuthoredNormalPixel);
        SurfaceMaterialResidence AuthoredNormalTextures;
        AuthoredNormalTextures.Initialize(Fixtures.Programs,AuthoredNormalMaterial.Textures,AuthoredNormalMaterial.AuthoringMaps);
        SurfaceScatteringResidence AuthoredNormalParameters;
        AuthoredNormalParameters.Initialize(Fixtures.Programs,AuthoredNormalMaterial.Scattering);
        const auto AuthoredNormalPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,
            AuthoredNormalTextures,AuthoredNormalParameters);
        const auto AuthoredTangentNormal = Normalize({204.0/255*2-1,179.0/255*2-1,230.0/255*2-1});
        const double AuthoredNormalLight = AuthoredTangentNormal[0]*Light[0]+
            AuthoredTangentNormal[1]*Light[1]+AuthoredTangentNormal[2]*Light[2];
        const double AuthoredNormalHalf = AuthoredTangentNormal[0]*Half[0]+
            AuthoredTangentNormal[1]*Half[1]+AuthoredTangentNormal[2]*Half[2];
        const auto AuthoredNormalAngular = EvaluateScatteringSequence(AuthoredNormalMaterial.Scattering.Illumination,
            AuthoredTangentNormal[2],AuthoredNormalLight,AuthoredNormalHalf,Half[2]);
        for (size_t Channel = 0; Channel < 3; ++Channel)
            Confirm(std::abs(AuthoredNormalPixels[(64*128+64)*4+Channel]-
                255*EncodePreviewDisplay(AuthoredNormalAngular[Channel]*AuthoredNormalLight*2,1)) <= 2,
                "Float32 authoring tangent normal differs from the independent scattering reference");
        auto AuthoredOcclusionMaterial = CompileSurfaceMaterial(Definition,ContentDirectory);
        AuthoredOcclusionMaterial.Scattering.Illumination.EnvironmentHorizon = {.2f,.2f,.2f,1};
        AuthoredOcclusionMaterial.Scattering.Illumination.EnvironmentZenith = {.2f,.2f,.2f,0};
        AuthoredOcclusionMaterial.Scattering.Illumination.LightDirectionIntensity = {0,0,1,0};
        AuthoredOcclusionMaterial.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        SurfaceMaterialResidence NeutralOcclusionTextures;
        NeutralOcclusionTextures.Initialize(Fixtures.Programs,AuthoredOcclusionMaterial.Textures,
            AuthoredOcclusionMaterial.AuthoringMaps);
        SurfaceScatteringResidence AuthoredOcclusionParameters;
        AuthoredOcclusionParameters.Initialize(Fixtures.Programs,AuthoredOcclusionMaterial.Scattering);
        const auto NeutralOcclusionPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,
            NeutralOcclusionTextures,AuthoredOcclusionParameters);
        const std::array<float,4> AuthoredOcclusionPixel{.1f,.1f,.1f,1};
        AuthoredOcclusionMaterial.AuthoringMaps[5] = ConstructFloatMaterialPyramid(1,1,
            MaterialTextureCategory::OcclusionRoughnessMetallic,AuthoredOcclusionPixel);
        SurfaceMaterialResidence AuthoredOcclusionTextures;
        AuthoredOcclusionTextures.Initialize(Fixtures.Programs,AuthoredOcclusionMaterial.Textures,
            AuthoredOcclusionMaterial.AuthoringMaps);
        const auto AuthoredOcclusionPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,
            AuthoredOcclusionTextures,AuthoredOcclusionParameters);
        Confirm(AuthoredOcclusionPixels[(64*128+64)*4] < NeutralOcclusionPixels[(64*128+64)*4],
            "Float32 authoring occlusion contribution did not reduce ambient material response");
        for (float Strength : {0.f,.35f,2.f})
            for (float Scale : {-1.f,0.f,1.f})
            {
                auto Specification = Directional.Scattering;
                Specification.Illumination.ViewFromSurface[0] = 1;
                Specification.StratumNormal[0][0] = Strength;
                Specification.StratumUv[0] = {Scale,1,0,0};
                SurfaceScatteringResidence Parameters;
                Parameters.Initialize(Fixtures.Programs,Specification);
                const auto Pixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,DirectionalTextures,Parameters);
                const auto Normal = Normalize({(204.0/255*2-1)*Strength*(Scale < 0 ? -1 : 1),
                                                (179.0/255*2-1)*Strength,230.0/255*2-1});
                const double NormalLight = Normal[0]*Light[0]+Normal[1]*Light[1]+Normal[2]*Light[2];
                const double NormalHalf = Normal[0]*Half[0]+Normal[1]*Half[1]+Normal[2]*Half[2];
                const auto Angular = EvaluateScatteringSequence(Specification.Illumination,Normal[2],NormalLight,NormalHalf,Half[2]);
                for (size_t Channel = 0; Channel < 3; ++Channel)
                    Confirm(std::abs(Pixels[(64*128+64)*4+Channel]-255*EncodePreviewDisplay(Angular[Channel]*NormalLight*2,1)) <= 2,
                            "Layer normal strength/negative or zero UV scale differs from scalar reference");
            }
        auto CoatedNormals = Directional.Scattering;
        CoatedNormals.Illumination.ViewFromSurface[0] = 1;
        CoatedNormals.Illumination.Configuration[0] = 2;
        auto &IndependentCoat = CoatedNormals.Illumination.Strata[1];
        IndependentCoat.Classification[0] = 1;
        IndependentCoat.Optics[2] = .7f;
        IndependentCoat.AbsorptionThickness = {2,4,8,.01f};
        const auto BaseNormal = Normalize({204.0/255*2-1,179.0/255*2-1,230.0/255*2-1});
        const double BaseNormalLight = BaseNormal[0]*Light[0]+BaseNormal[1]*Light[1]+BaseNormal[2]*Light[2];
        const double BaseNormalHalf = BaseNormal[0]*Half[0]+BaseNormal[1]*Half[1]+BaseNormal[2]*Half[2];
        auto Isolated = CoatedNormals.Illumination;
        Isolated.Configuration[0] = 1;
        const auto Underlying = EvaluateScatteringSequence(Isolated,BaseNormal[2],BaseNormalLight,BaseNormalHalf,Half[2]);
        Isolated.Strata[0] = IndependentCoat;
        const auto CoatReflection = EvaluateScatteringSequence(Isolated,1,Light[2],Half[2],Half[2]);
        const double Entrance = .04+.96*std::pow(1-Light[2],5);
        SurfaceScatteringResidence CoatedParameters;
        CoatedParameters.Initialize(Fixtures.Programs,CoatedNormals);
        const auto IndependentPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,DirectionalTextures,CoatedParameters);
        for (size_t Channel = 0; Channel < 3; ++Channel)
        {
            const double Absorption = std::exp(-IndependentCoat.AbsorptionThickness[Channel]*IndependentCoat.AbsorptionThickness[3]*(1+1/Light[2]));
            const double Under = Underlying[Channel]*BaseNormalLight;
            const double Reference = (.3*Under+.7*(CoatReflection[Channel]*Light[2]+Under*(1-Entrance)*.96*Absorption))*2;
            Confirm(std::abs(IndependentPixels[(64*128+64)*4+Channel]-255*EncodePreviewDisplay(Reference,1)) <= 2,
                    "Independent coat/base normals incorrectly share a cosine or tangent normal");
        }
        for (const auto &Invalid : {"Anisotropy=1\n","Anisotropy=nan\n","Rotation=101\n","NormalStrength=-1\n",
             "Fresnel='Conductor'\n","Fresnel='Dielectric'\nMetallic=0.5\n","UvScaleOffset=[1,2,3]\n",
             "[Strata.Textures]\nRadiance='Textures/Radiance.png'\n"}) Reject(Minimal+Invalid);
        const auto Extended = DecodeSurfaceMaterial(Minimal+"Anisotropy=-0.8\nRotation=0.73\nFresnel='Dielectric'\nNormalStrength=0.25\n"
            "UvScaleOffset=[2,-3,0.1,0.2]\n[Strata.Textures]\nAlbedo='Textures/Albedo.png'\n");
        Confirm(EncodeSurfaceMaterial(DecodeSurfaceMaterial(EncodeSurfaceMaterial(Extended))) == EncodeSurfaceMaterial(Extended),
                "Directional optics/per-stratum input round trip changed meaning");
        Confirm(std::abs(EvaluateExactDielectricReflectance(1,1,1.5)-.04) < 1e-12,"Normal-incidence dielectric Fresnel wrong");
        Confirm(EvaluateExactDielectricReflectance(0,1,1.5) == 1,"Grazing dielectric Fresnel wrong");
        Confirm(EvaluateExactDielectricReflectance(.1,1.5,1) == 1,"Total internal reflection missing");
        Confirm(EvaluateExactDielectricReflectance(0,1,1) == 0,"Equal-IOR interface reflects");
        Confirm(EvaluateExactDielectricReflectance(.9,1.5,1) < 1,"Subcritical dielectric interface incorrectly reflects everything");
        const auto Isotropic = DeriveAnisotropicSpread(.4,0);
        Confirm(Isotropic[0] == Isotropic[1],"Zero anisotropy changes isotropic spread");
        for (double Anisotropy : {-.9,0.,.9})
        {
            const auto Spread = DeriveAnisotropicSpread(.4,Anisotropy);
            double Integral = 0;
            constexpr size_t PolarSamples = 512, AzimuthSamples = 512;
            for (size_t Polar = 0; Polar < PolarSamples; ++Polar)
            {
                const double θ = (Polar+.5)*std::numbers::pi/(2*PolarSamples);
                for (size_t Azimuth = 0; Azimuth < AzimuthSamples; ++Azimuth)
                {
                    const double φ = (Azimuth+.5)*2*std::numbers::pi/AzimuthSamples;
                    Integral += EvaluateAnisotropicDistribution({std::sin(θ)*std::cos(φ),std::sin(θ)*std::sin(φ),std::cos(θ)},Spread)*
                                std::cos(θ)*std::sin(θ)*std::numbers::pi/(2*PolarSamples)*2*std::numbers::pi/AzimuthSamples;
                }
            }
            Confirm(std::abs(Integral-1) < .002,"Anisotropic half-vector proposal is not normalized");
        }
        auto Optical = CompileSurfaceMaterial(Definition,ContentDirectory);
        Optical.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        Optical.Scattering.Illumination.LightDirectionIntensity = {.6f,.3f,.74f,2};
        Optical.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        SurfaceMaterialResidence OpticalTextures;
        OpticalTextures.Initialize(Fixtures.Programs,Optical.Textures);
        for (float Anisotropy : {-.8f,0.f,.8f})
            for (float Rotation : {0.f,.73f,1.5707963f})
                for (float Exact : {0.f,1.f})
                {
                    Optical.Scattering.DirectionalOptics[0] = {Anisotropy,Rotation,Exact,0};
                    SurfaceScatteringResidence Parameters;
                    Parameters.Initialize(Fixtures.Programs,Optical.Scattering);
                    const auto Pixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,OpticalTextures,Parameters);
                    const auto Rotate = [&](const std::array<double,3> &Direction)
                    {
                        return std::array<double,3>{std::cos(Rotation)*Direction[0]+std::sin(Rotation)*Direction[1],
                            -std::sin(Rotation)*Direction[0]+std::cos(Rotation)*Direction[1],Direction[2]};
                    };
                    const auto Spread = DeriveAnisotropicSpread(Optical.Scattering.Illumination.Strata[0].AlbedoRoughness[3],Anisotropy);
                    const double Distribution = EvaluateAnisotropicDistribution(Rotate(Half),Spread);
                    const double Visibility = EvaluateAnisotropicVisibility({0,0,1},Rotate(Light),Spread);
                    const double Fresnel = Exact != 0 ? EvaluateExactDielectricReflectance(Half[2],1,1.5) : .04+.96*std::pow(1-Half[2],5);
                    const double Reference = ((1-Fresnel)*.5/std::numbers::pi+Distribution*Visibility*Fresnel)*Light[2]*2;
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Confirm(std::abs(Pixels[(64*128+64)*4+Channel]-255*EncodePreviewDisplay(Reference,1)) <= 2,
                                "Rotated anisotropic GGX / exact dielectric GPU pixel differs from scalar reference");
                }
        auto LayeredDefinition = DecodeSurfaceMaterial(Minimal+"TextureMask=1\n[[Strata]]\nCategory='Opaque'\nTextureMask=1\n"
            "UvScaleOffset=[1,1,0.13,0.27]\n[Strata.Textures]\nAlbedo='Textures/Radiance.png'\n[[Strata]]\nCategory='Opaque'\n");
        LayeredDefinition.TextureSources[0] = "Textures/Albedo.png";
        auto Layered = CompileSurfaceMaterial(LayeredDefinition,ContentDirectory);
        Confirm(Layered.Textures[0].Layers == 2 && Layered.Scattering.TextureUse[0][1] == 0 &&
                Layered.Scattering.TextureUse[1][1] == 1 && Layered.Scattering.TextureUse[2][1] == 0,
                "Per-stratum texture slices were not deduplicated");
        Confirm(ValidateMaterialPyramid(Layered.Textures[0]),"Layered mip-major array storage invalid");
        Confirm(Layered.Textures[0].Levels[1].Offset == 128*128*4*2,"Array mip offsets omit slice count");
        Layered.Scattering.Illumination.Configuration[0] = 2;
        Layered.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        SurfaceMaterialResidence LayeredTextures;
        LayeredTextures.Initialize(Fixtures.Programs,Layered.Textures);
        const auto CapturePlane = [&](const MaterialScattering &Scattering)
        {
            SurfaceScatteringResidence Parameters;
            Parameters.Initialize(Fixtures.Programs,Scattering);
            return Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,LayeredTextures,Parameters);
        };
        for (float UvScale : {1.f,512.f})
        {
            auto Sequence = Layered.Scattering;
            Sequence.UvScaleOffset = {UvScale,UvScale,0,0};
            const auto LayeredPixels = CapturePlane(Sequence);
            Sequence.Illumination.Configuration[0] = 1;
            Sequence.Illumination.Strata[0] = Sequence.Illumination.Strata[1];
            Sequence.TextureUse[0] = Sequence.TextureUse[1];
            Sequence.StratumUv[0] = Sequence.StratumUv[1];
            Confirm(CapturePlane(Sequence) == LayeredPixels,"Per-layer texture selection/UV/mip evaluation differs from isolated top stratum");
            Sequence.TextureUse[0][1] = 0;
            Confirm(CapturePlane(Sequence) != LayeredPixels,"Array slice selection did not affect Vulkan pixels");
        }
        auto NeutralDefinition = LayeredDefinition;
        NeutralDefinition.TextureSources[0].clear();
        const auto NeutralArrays = CompileSurfaceMaterial(NeutralDefinition,ContentDirectory);
        Confirm(NeutralArrays.Textures[0].Layers == 2 && NeutralArrays.Textures[0].Rgba[128*128*4-1] == 255 &&
                ValidateMaterialPyramid(NeutralArrays.Textures[0]),"Neutral 1x1 texture did not expand across the array mip chain");
        auto InvalidSelection = Layered.Scattering;
        InvalidSelection.TextureUse[0][1] = 7;
        bool InvalidSliceRefused = false;
        try { (void)CapturePlane(InvalidSelection); } catch (const std::exception &) { InvalidSliceRefused = true; }
        Confirm(InvalidSliceRefused,"Out-of-residence texture slice accepted at command recording");
        auto InvalidLayers = Layered.Textures[0];
        InvalidLayers.Layers = 0;
        Confirm(!ValidateMaterialPyramid(InvalidLayers),"Zero-layer material image accepted");
        LayeredDefinition.StratumTextureSources[1][0] = "../Escape.png";
        bool LayerEscapeRefused = false;
        try { (void)CompileSurfaceMaterial(LayeredDefinition,ContentDirectory); } catch (const std::exception &) { LayerEscapeRefused = true; }
        Confirm(LayerEscapeRefused,"Per-stratum source escaped ContentDirectory");
        auto LifetimeMaterial = CompileSurfaceMaterial(Definition,ContentDirectory);
        LifetimeMaterial.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        LifetimeMaterial.Scattering.Illumination.LightDirectionIntensity = {0,0,1,2};
        LifetimeMaterial.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        SurfaceMaterialResidence RetainedTextures;
        RetainedTextures.Initialize(Fixtures.Programs,LifetimeMaterial.Textures);
        SurfaceScatteringResidence RetainedParameters;
        RetainedParameters.Initialize(Fixtures.Programs,LifetimeMaterial.Scattering);
        // 📝 Mutating the compilation product cannot mutate immutable device parameters/textures.
        LifetimeMaterial.Scattering.Illumination.Strata[0].AlbedoRoughness = {0,0,0,1};
        LifetimeMaterial.Textures[0].Rgba.assign(4,0);
        ConfirmVulkanResult(Fixtures.Device.ResetCommandPool(Fixtures.Device.Device,Fixtures.Device.Pool,0));
        VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        ConfirmVulkanResult(Fixtures.Device.BeginCommandBuffer(Fixtures.Device.Commands,&Begin));
        Fixtures.Plane.ConstructCommands(Fixtures.Device.Commands,Projection,RetainedTextures,RetainedParameters);
        ConfirmVulkanResult(Fixtures.Device.EndCommandBuffer(Fixtures.Device.Commands));
        Confirm(RetainedTextures.QueryUploadCount() == 0,"Cancelled material recording confirmed unsubmitted uploads");
        Fixtures.Device.BeforeSubmission = [&]()
        {
            RetainedTextures.Finalize();
            RetainedParameters.Finalize();
        };
        Confirm(Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,RetainedTextures,RetainedParameters) == Plain,
                "Material source mutation/finalization invalidated recorded immutable allocations");
        Fixtures.Device.BeforeSubmission = {};
        auto Ordered = Definition;
        Ordered.Scattering.Illumination.Configuration[0] = 8;
        for (size_t Index = 1; Index < 8; ++Index)
        {
            auto &Stratum = Ordered.Scattering.Illumination.Strata[Index];
            Stratum.Classification[0] = 1;
            Stratum.AlbedoRoughness[3] = .2f;
            Stratum.Optics[2] = .2f;
            Stratum.AbsorptionThickness = {2,4,8,.01f};
        }
        const auto OrderedRoundTrip = DecodeSurfaceMaterial(EncodeSurfaceMaterial(Ordered));
        auto OrderedCompiled = CompileSurfaceMaterial(OrderedRoundTrip,ContentDirectory);
        OrderedCompiled.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        OrderedCompiled.Scattering.Illumination.LightDirectionIntensity = {0,0,1,2};
        OrderedCompiled.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        CompareReference(OrderedCompiled,1);
        const auto NormalMip = ConstructMaterialPyramid({2,1,{255,128,255,255,0,128,255,255}},MaterialTextureCategory::TangentNormal);
        Confirm(NormalMip.Rgba[8] >= 127 && NormalMip.Rgba[8] <= 128 && NormalMip.Rgba[10] >= 254,"Normal mip not renormalized");
        for (const auto &Name : {"BrushedCopper","CoatedCeramic","TexturedAlloy","LuminousPattern"})
        {
            auto Material = CompileSurfaceMaterial(DecodeSurfaceMaterial(ExtractText(ContentDirectory/(std::string(Name)+".material"))),ContentDirectory);
            SurfaceMaterialResidence Textures;
            Textures.Initialize(Fixtures.Programs,Material.Textures);
            const auto Bracket = Fixtures.CaptureMaterial(0,Material.Scattering,Textures);
            const auto Sphere = Fixtures.CaptureMaterial(1,Material.Scattering,Textures);
            Confirm(QueryBrightPixelCount(Sphere) > 1000,"Material specimen absent");
            Confirm(Textures.QueryUploadCount() == 10,"Target changes reuploaded material/authoring textures");
            Confirm(Fixtures.CaptureMaterial(0,Material.Scattering,Textures) == Bracket,"Material round trip changed native pixels");
            EncodeRasterCapture(Destination/(std::string("Material")+Name+"Bracket.ppm"),Bracket,960,720);
            EncodeRasterCapture(Destination/(std::string("Material")+Name+"Sphere.ppm"),Sphere,720,720);
            if (std::string(Name) == "TexturedAlloy")
            {
                auto Disabled = Material.Scattering;
                Disabled.TextureUse[0][0] &= ~4u;
                Confirm(Fixtures.CaptureMaterial(1,Disabled,Textures) != Sphere,"Tangent normal input did not affect material");
                Disabled = Material.Scattering;
                Disabled.TextureUse[0][0] &= ~2u;
                Confirm(Fixtures.CaptureMaterial(1,Disabled,Textures) != Sphere,"ORM input did not affect material");
            }
            if (std::string(Name) == "CoatedCeramic")
            {
                auto Disabled = Material.Scattering;
                Disabled.Illumination.Strata[1].Optics[2] = 0;
                Confirm(Fixtures.CaptureMaterial(1,Disabled,Textures) != Sphere,"Coating coverage did not affect material");
            }
            if (std::string(Name) == "LuminousPattern")
            {
                auto Disabled = Material.Scattering;
                Disabled.RadianceNormal = {0,0,0,1};
                Confirm(Fixtures.CaptureMaterial(1,Disabled,Textures) != Sphere,"Radiance texture did not affect material");
            }
        }
        Confirm(!Plain.empty(),"Reference image missing");
        Confirm(Fixtures.Bracket.QueryGeometryUploadCount() == 1 && Fixtures.Sphere.QueryGeometryUploadCount() == 1,"Material changes reuploaded geometry");
        Confirm(RasterValidationDevice::PipelineCreationCount == 5,"Materials created per-instance shader pipelines");
        std::ofstream Report(Destination/"MaterialChecks.json");
        Report << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"pipelines\":5,\"windows_execution\":false}\n";
        std::cout << "PASS: " << Checks << " authored material, texture pyramid and native Vulkan checks\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
