/*==========================================================================================================================================
                                                      SOLARDEPTHRASTERIZATION.CPP
==========================================================================================================================================*/
// 🧩 Depth-only orthographic raster of the scene along the sun into the analytic illumination's shadow map.
#include "SolarDepthRasterization.h"
namespace Frontier::AnalyticIllumination
{
using namespace Frontier::GlobalIllumination;

SolarDepthRasterization::SolarDepthRasterization(DeviceExchange& Exchange, ShaderInterchange& Shaders) : Exchange_(Exchange), Shaders_(Shaders)
{
}

void SolarDepthRasterization::ConstructPrograms(const std::shared_ptr<DescriptorLayout>& Layout)
{
    RasterProgramSpecification Specification;
    Specification.VertexEntry    = "SolarDepthVertex";
    Specification.VertexLayouts  = {{VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {1, 12, VertexFormat::Uint32}, {2, 16, VertexFormat::Float32x3},
                                                           {3, 28, VertexFormat::Uint32}}},
                                    {4, true, {{4, 0, VertexFormat::Uint32}}}};
    Specification.DepthEnabled   = true;
    Specification.DepthWrite     = true;
    Specification.DepthCompare   = CompareFunction::Less;
    Specification.DepthBias      = 1.0f;
    Specification.DepthBiasSlope = 1.75f;
    Specification.Layouts        = {Layout};
    Program_ = Shaders_.ConstructRasterProgram("SolarDepthRasterization", Specification);
}

void SolarDepthRasterization::ConstructTargets(uint32_t Size)
{
    Extent = Size;
    TextureSpecification Specification;
    Specification.Label  = "Analytic solar depth";
    Specification.Width  = Size;
    Specification.Height = Size;
    Specification.Format = TextureFormat::Depth32Float;
    Specification.Usage  = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    DepthTexture = Exchange_.ConstructTexture(Specification);
    DepthView    = Exchange_.ConstructView(DepthTexture);
}

void SolarDepthRasterization::Record(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& Group, const SceneStructure& Scene,
                                     const std::shared_ptr<DeviceRegion>& VertexRegion, const std::shared_ptr<DeviceRegion>& Instances)
{
    const DepthAttachment Depth{DepthView, LoadOperation::Clear, 1.0f};
    Sequence.BeginRaster({}, &Depth, "Analytic solar depth");
    Sequence.BindProgram(Program_);
    Sequence.BindGroup(0, Group);
    Sequence.BindVertexRegion(0, VertexRegion);
    Sequence.BindVertexRegion(1, Instances);
    // 📝 The shadow runs: every drawn occupant but the ground and the objects switched out of shadows (ShadowExempt).
    for (const DrawRun& Run : Scene.ShadowRuns)
    {
        const FacetSet& Set = Scene.FacetSets[Run.FacetSet];
        Sequence.Draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
    }
    Sequence.EndRaster();
}
} // namespace Frontier::AnalyticIllumination
