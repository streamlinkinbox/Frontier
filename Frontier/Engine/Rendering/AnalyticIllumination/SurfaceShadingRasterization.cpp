/*==========================================================================================================================================
                                                    SURFACESHADINGRASTERIZATION.CPP
==========================================================================================================================================*/
// 🧩 Sky and surface programs of the analytic illumination and their one raster sequence into the presented image.
#include "SurfaceShadingRasterization.h"
namespace Frontier::AnalyticIllumination
{
using namespace Frontier::GlobalIllumination;

SurfaceShadingRasterization::SurfaceShadingRasterization(DeviceExchange& Exchange, ShaderInterchange& Shaders) : Exchange_(Exchange), Shaders_(Shaders)
{
}

void SurfaceShadingRasterization::ConstructPrograms(const std::shared_ptr<DescriptorLayout>& Layout)
{
    // 📝 The presented image is rgba8 viewed as sRGB, as the illumination's own present raster writes it.
    RasterProgramSpecification Sky;
    Sky.VertexEntry   = "SkyVertex";
    Sky.FragmentEntry = "SkyFragment";
    Sky.DepthEnabled  = true;
    Sky.DepthWrite    = false;
    Sky.DepthCompare  = CompareFunction::Always;
    Sky.Targets       = {{TextureFormat::Rgba8UnormSrgb}};
    Sky.Layouts       = {Layout};
    SkyProgram_ = Shaders_.ConstructRasterProgram("SurfaceShadingRasterization", Sky);
    RasterProgramSpecification Surface;
    Surface.Label         = "SurfaceShadingRasterization:ShadingVertex";
    Surface.VertexEntry   = "ShadingVertex";
    Surface.FragmentEntry = "ShadingFragment";
    Surface.VertexLayouts = {{VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {1, 12, VertexFormat::Uint32}, {2, 16, VertexFormat::Float32x3},
                                                    {3, 28, VertexFormat::Uint32}}},
                             {4, true, {{4, 0, VertexFormat::Uint32}}}};
    Surface.DepthEnabled  = true;
    Surface.DepthWrite    = true;
    Surface.DepthCompare  = CompareFunction::Greater;
    Surface.Targets       = {{TextureFormat::Rgba8UnormSrgb}};
    Surface.Layouts       = {Layout};
    SurfaceProgram_ = Shaders_.ConstructRasterProgram("SurfaceShadingRasterization", Surface);
}

void SurfaceShadingRasterization::ConstructTargets(uint32_t Width, uint32_t Height)
{
    Extent = {Width, Height};
    TextureSpecification Specification;
    Specification.Label  = "Analytic surface depth";
    Specification.Width  = Width;
    Specification.Height = Height;
    Specification.Format = TextureFormat::Depth32Float;
    Specification.Usage  = TextureUsage::RenderAttachment;
    DepthTexture = Exchange_.ConstructTexture(Specification);
    DepthView    = Exchange_.ConstructView(DepthTexture);
}

void SurfaceShadingRasterization::Record(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& Group, const SceneStructure& Scene,
                                         const std::shared_ptr<DeviceRegion>& VertexRegion, const std::shared_ptr<DeviceRegion>& Instances,
                                         const std::shared_ptr<DeviceTextureView>& Target)
{
    const std::array<ColorAttachment, 1> Colors = {{{Target, LoadOperation::Clear, {0, 0, 0, 1}}}};
    const DepthAttachment Depth{DepthView, LoadOperation::Clear, 0.0f};
    Sequence.BeginRaster(Colors, &Depth, "Analytic surface shading");
    Sequence.BindProgram(SkyProgram_);
    Sequence.BindGroup(0, Group);
    Sequence.Draw(3);
    Sequence.BindProgram(SurfaceProgram_);
    Sequence.BindGroup(0, Group);
    Sequence.BindVertexRegion(0, VertexRegion);
    Sequence.BindVertexRegion(1, Instances);
    for (const DrawRun& Run : Scene.DrawRuns)
    {
        const FacetSet& Set = Scene.FacetSets[Run.FacetSet];
        Sequence.Draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
    }
    Sequence.EndRaster();
}
} // namespace Frontier::AnalyticIllumination
