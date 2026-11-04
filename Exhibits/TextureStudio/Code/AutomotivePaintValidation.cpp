/*==========================================================================================================================================
                                                     AUTOMOTIVEPAINTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify authored paint transactions and actual GPU sensitivity, minification, pedestal isolation and bounded pipeline/geometry reuse.

#include "AtlasReadback.h"
#include "Engine/ContentInterchange/MaterialLibrary.h"
#include "Engine/Render/MaterialPreviewGeometry.h"
#include <functional>
#include <chrono>
#include <limits>
#include <sstream>

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(Count == 5, "Expected driver, shaders, library and scratch destination.");
        const std::filesystem::path Shaders(Arguments[2]), LibraryDirectory(Arguments[3]), Destination(Arguments[4]);
        std::filesystem::create_directories(Destination);
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition, const char *Description)
        {
            AssertRasterCondition(Condition, Description);
            ++Checks;
        };
        std::ifstream SourceFile(LibraryDirectory / "racing-green.material");
        const std::string Source{std::istreambuf_iterator<char>(SourceFile), std::istreambuf_iterator<char>()};
        auto Definition = DecodeSurfaceMaterial(Source);
        Confirm(Definition.Scattering.Paint.Configuration == std::array<uint32_t,4>{1,3,0,5}, "Green recipe is not executable three-layer, five-colour paint.");
        Confirm(Definition.Scattering.Paint.Distribution == std::array<float,4>{.64f,450,1,.65f}, "Green flake parameters differ from the HTML.");
        const auto Roundtrip = DecodeSurfaceMaterial(EncodeSurfaceMaterial(Definition));
        Confirm(std::memcmp(&Definition.Scattering.Paint, &Roundtrip.Scattering.Paint, sizeof(AutomotivePaint)) == 0, "Paint codec loses parameters.");
        for (size_t Component = 0; Component < 4; ++Component)
        {
            auto Invalid = Definition.Scattering;
            Invalid.Paint.Distribution[Component] = std::numeric_limits<float>::quiet_NaN();
            Confirm(!ValidateMaterialScattering(Invalid), "Non-finite procedural coefficient accepted.");
        }
        auto Invalid = Definition.Scattering;
        Invalid.Paint.Configuration[1] = 5;
        Confirm(!ValidateMaterialScattering(Invalid), "Unbounded particle layers accepted.");
        Invalid = Definition.Scattering;
        Invalid.Paint.Configuration[3] = 13;
        Confirm(!ValidateMaterialScattering(Invalid), "Unbounded palette accepted.");
        Invalid = Definition.Scattering;
        Invalid.Paint.Finish[0] = .9f;
        Confirm(!ValidateMaterialScattering(Invalid), "Reversed roughness range accepted.");
        Invalid = Definition.Scattering;
        Invalid.Paint.AverageTint[0] = std::numeric_limits<float>::infinity();
        Confirm(!ValidateMaterialScattering(Invalid), "Non-finite derived tint accepted.");
        auto Compiled = CompileSurfaceMaterial(Definition, LibraryDirectory);
        Confirm(Compiled.Scattering.Paint.AverageTint[0] > .1f && Compiled.Scattering.Paint.AverageTint[3] == 0, "Linear-space unresolved tint was not compiled.");
        const auto Working = Destination / ("Editable-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(Working);
        std::ofstream(Working / "Green.material") << "# Retain UTF-8: café; paint edit must preserve this comment.\n" << Source;
        MaterialLibrary Library;
        Confirm(Library.ConfigureDirectory(Working) && Library.OpenMaterial("Green.material"), "Paint library failed to open.");
        auto Edit = Definition.Scattering.Paint;
        Edit.Distribution[0] = .15f;
        Confirm(Library.ConfigurePaint(Edit), "Structured paint editing failed.");
        Confirm(Library.QuerySource().starts_with("# Retain UTF-8: café;"), "Structured paint edit discarded authored source.");
        Confirm(Library.QueryApplied()->Scattering.Paint.Distribution[0] == .64f && Library.QueryUnapplied(), "Draft overwrote the last valid GPU material before Apply.");
        Confirm(Library.ApplySource() && Library.QueryApplied()->Scattering.Paint.Distribution[0] == .15f, "Apply failed to compile paint coefficients.");
        const auto Revision = Library.QueryRevision();
        Library.ReplaceSource(Library.QuerySource() + "\nUnrecognizedPaintParameter = 3\n");
        Confirm(!Library.ApplySource() && Library.QueryRevision() == Revision && Library.QueryApplied()->Scattering.Paint.Distribution[0] == .15f,
            "Malformed paint destroyed the last valid material.");
        Confirm(Library.RevertSource() && Library.QueryApplied()->Scattering.Paint.Distribution[0] == .64f, "Paint Revert failed.");
        Confirm(Library.ConfigurePaint(Edit) && Library.SaveMaterial(), "Paint Save failed.");
        Confirm(Library.DuplicateMaterial("Duplicate.material"), "Paint Duplicate failed.");
        Confirm(Library.QueryApplied()->Scattering.Paint.Distribution[0] == .15f, "Paint Duplicate stripped procedural coefficients.");

        AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
        const std::array<const char *, 10> Names{"CadSurfaceVertex", "CadSurfaceFragment", "CadEdgeVertex", "CadEdgeFragment",
            "MatCapSurfaceVertex", "MatCapSurfaceFragment", "PreviewPbrSurfaceVertex", "PreviewPbrSurfaceFragment", "MaterialSurfaceVertex", "MaterialSurfaceFragment"};
        std::array<std::vector<uint32_t>, 10> Words;
        for (size_t Index = 0; Index < Names.size(); ++Index) Words[Index] = DecodeShaderArchive(Shaders / (std::string(Names[Index]) + ".spv"));
        SurfaceRasterPrograms Programs;
        Programs.Initialize(Device.QueryAccess(), {{Words[0],"main"},{Words[1],"main"},{Words[2],"main"},{Words[3],"main"},
            {Words[4],"main"},{Words[5],"main"},{Words[6],"main"},{Words[7],"main"},{Words[8],"main"},{Words[9],"main"}});
        const auto Scene = ConstructMaterialPreviewSpecimen();
        std::string Diagnostic;
        Confirm(ValidateSurfaceGeometry(Scene, Diagnostic), "Shader ball/pedestal geometry is invalid.");
        std::array<uint32_t,4> Roles{};
        for (const auto &Corner : Scene.Corners) ++Roles[Corner.Coordinates[0] < 0 ? size_t(-Corner.Coordinates[0]) : 0];
        Confirm(Roles[0] > 60000 && Roles[1] > 0 && Roles[2] > 0 && Roles[3] > 0, "Shader ball, pedestal, foot or trim ring missing.");
        SurfaceGeometryResidence Geometry;
        Geometry.Initialize(Device.QueryAccess(), Scene);
        SurfaceRasterization Target, Thumbnail;
        // 📝 Control sensitivity uses a resolved macro view at thumbnail resolution to bound software-driver test time.
        // The separate workspace exhibit verifies the actual 450 µm preset in the production 512px preview.
        constexpr uint32_t Extent = 128;
        auto GpuDefinition = Definition;
        GpuDefinition.Scattering.Paint.Distribution[1] = 1800;
        Target.Initialize(Programs, Geometry, Extent, Extent);
        Thumbnail.Initialize(Programs, Geometry, 128, 128);
        Target.ConfigureTransparentBackground(true);
        Thumbnail.ConfigureTransparentBackground(true);
        SurfaceMaterialResidence Textures;
        Textures.Initialize(Programs, Compiled.Textures);
        SurfaceNormalProjection Projection;
        Projection.ClipFromSurface = {1,0,0,0,0,-1,0,0,0,0,-.4f,0,0,0,.5f,1};
        const auto Capture = [&](MaterialDefinition Subject, bool Small = false)
        {
            auto CompiledSubject = CompileSurfaceMaterial(Subject, LibraryDirectory);
            auto &Scattering = CompiledSubject.Scattering;
            Scattering.PreviewConfiguration = {1,1,0,0};
            Scattering.Illumination.EnvironmentHorizon = {.10f,.105f,.11f,1};
            Scattering.Illumination.EnvironmentZenith = {.32f,.33f,.34f,0};
            SurfaceScatteringResidence Parameters;
            Parameters.Initialize(Programs, Scattering);
            auto &Drawing = Small ? Thumbnail : Target;
            const uint32_t CaptureExtent = Small ? 128 : Extent;
            return Device.Capture(Drawing.QueryImage(), {CaptureExtent,CaptureExtent}, [&](VkCommandBuffer Commands)
            {
                Drawing.ConstructCommands(Commands, Projection, Textures, Parameters);
            }, [&]() { Drawing.ConfirmSubmission(); });
        };
        std::cout << "GPU: resolved 1800 µm macro sensitivity at 128px; production preset separately captured at 512px" << std::endl;
        const auto Reference = Capture(GpuDefinition);
        Confirm(QueryBrightPixelCount(Reference) > 1000, "Paint rendering produced an empty or dark frame.");
        EncodeRasterCapture(Destination / "GreenAutomotivePaint.ppm", Reference, Extent, Extent);
        const auto PipelineCount = RasterValidationDevice::PipelineCreationCount;
        const auto PixelDifferences = [](const auto &First, const auto &Second, size_t Begin, size_t End)
        {
            size_t Differences = 0;
            for (size_t Pixel = Begin; Pixel < End; ++Pixel)
                if (std::abs(int(First[Pixel*4])-Second[Pixel*4]) + std::abs(int(First[Pixel*4+1])-Second[Pixel*4+1]) +
                    std::abs(int(First[Pixel*4+2])-Second[Pixel*4+2]) > 6) ++Differences;
            return Differences;
        };
        const std::vector<std::pair<const char *, std::function<void(MaterialDefinition &)>>> Variations{
            {"Disabled", [](auto &Material) { Material.Scattering.Paint.Configuration[0] = 0; }},
            {"Coverage", [](auto &Material) { Material.Scattering.Paint.Distribution[0] = 0; }},
            {"ParticleSize", [](auto &Material) { Material.Scattering.Paint.Distribution[1] = 60; }},
            {"Repetition", [](auto &Material) { Material.Scattering.Paint.Distribution[2] = 4; }},
            {"BinderDepth", [](auto &Material) { Material.Scattering.Paint.Distribution[3] = 2; }},
            {"Layers", [](auto &Material) { Material.Scattering.Paint.Configuration[1] = 1; }},
            {"Palette", [](auto &Material) { for (auto &Tint : Material.Scattering.Paint.Palette) { Tint[0] = .9f; Tint[1] = .01f; Tint[2] = .01f; } }},
            {"SingleTint", [](auto &Material) { Material.Scattering.Paint.Configuration[2] = 2; }},
            {"Roughness", [](auto &Material) { Material.Scattering.Paint.Finish[0] = .55f; Material.Scattering.Paint.Finish[1] = .8f; }},
            {"Metallic", [](auto &Material) { Material.Scattering.Paint.Finish[2] = 0; Material.Scattering.Paint.Finish[3] = .2f; }},
            {"Tilt", [](auto &Material) { Material.Scattering.Paint.Relief[0] = .7f; }},
            {"Burial", [](auto &Material) { Material.Scattering.Paint.Relief[1] = 1; }},
            {"OrangePeel", [](auto &Material) { Material.Scattering.Paint.Relief[2] = 1; }},
            {"PeelFrequency", [](auto &Material) { Material.Scattering.Paint.Relief[3] = 180; }},
            {"Clearcoat", [](auto &Material) { Material.Scattering.Illumination.Strata[1].Optics[2] = 0; }}
        };
        std::ostringstream Sensitivity;
        for (size_t Index = 0; Index < Variations.size(); ++Index)
        {
            auto Variant = GpuDefinition;
            Variations[Index].second(Variant);
            const auto Pixels = Capture(Variant);
            const auto Changed = PixelDifferences(Reference, Pixels, 0, Extent * Extent);
            Confirm(Changed > 150, "A procedural control has no measurable GPU effect.");
            Confirm(PixelDifferences(Reference, Pixels, (455 * Extent / 512) * Extent, (500 * Extent / 512) * Extent) == 0, "Paint parameters leaked onto the pedestal.");
            EncodeRasterCapture(Destination / (std::string(Variations[Index].first) + ".ppm"), Pixels, Extent, Extent);
            std::cout << "GPU: " << Variations[Index].first << " changed " << Changed << " pixels; pedestal unchanged" << std::endl;
            Sensitivity << (Index ? "," : "") << "\"" << Variations[Index].first << "\":" << Changed;
        }
        auto Unresolved = GpuDefinition;
        Unresolved.Scattering.Paint.Distribution[1] = .05f;
        const auto Tiny = Capture(Unresolved, true);
        Unresolved.Scattering.Paint.Distribution[2] = 100;
        const auto Tinier = Capture(Unresolved, true);
        Confirm(Tiny == Tinier, "Unresolved flake sizes alias instead of using the linear-space average.");
        Confirm(Target.QueryGeometryUploadCount() == 1 && RasterValidationDevice::PipelineCreationCount == PipelineCount,
            "Procedural edits recreated geometry or pipelines.");
        Confirm(Textures.QueryUploadCount() == 4, "Procedural edits uploaded particle textures instead of executing the shader.");
        std::ofstream(Destination / "AutomotivePaintChecks.json") << "{\"result\":\"passed\",\"checks\":" << Checks <<
            ",\"macro_test_extent\":128,\"macro_test_flake_size_um\":1800,\"production_workspace_preview_extent\":512,\"gpu_control_pixel_differences\":{" << Sensitivity.str() << "},\"geometry_uploads\":1,\"material_texture_uploads\":4,"
            "\"windows_execution\":false,\"hardware_execution\":false}\n";
        std::cout << "PASS: " << Checks << " authored paint, procedural GPU, pedestal-isolation and reuse checks\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
