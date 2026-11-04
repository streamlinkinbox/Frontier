/*==========================================================================================================================================
                                                         SHADOWPROJECTION.CPP
==========================================================================================================================================*/
// 🧩 Cascaded sun shadow depth: bounding-sphere slices of the view frustum, texel-snapped against shimmer (ShadowProjection.js).

#include "ShadowProjection.h"

#include <cmath>
#include <cstring>

namespace Frontier::GlobalIllumination
{

namespace
{
constexpr double   CascadeSplits[4] = {0.0, 12.0, 40.0, 140.0};   // [m] view distances bounding each cascade
constexpr uint32_t UniformStride    = 256;                        // [B] dynamic offset alignment
double RoundHalfUp(double Scalar) { return std::floor(Scalar + 0.5); }
} // namespace

ShadowProjection::ShadowProjection(DeviceExchange& Exchange, ShaderInterchange& Shaders, uint32_t ShadowExtent)
    : Extent(ShadowExtent), Exchange_(Exchange), Shaders_(Shaders)
{
}

void ShadowProjection::ConstructResources(const std::shared_ptr<DeviceRegion>& OccupantRegion)
{
    TextureSpecification Specification;
    Specification.Label         = "Shadow cascades";
    Specification.Width         = Extent;
    Specification.Height        = Extent;
    Specification.DepthOrLayers = ShadowCascadeCount;
    Specification.Format        = TextureFormat::Depth32Float;
    Specification.Usage         = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    DepthTexture = Exchange_.ConstructTexture(Specification);
    for (uint32_t Layer = 0; Layer < ShadowCascadeCount; ++Layer)
    {
        TextureViewSpecification View;
        View.Dimension    = ViewDimension::Planar;
        View.DimensionSet = true;
        View.BaseLayer    = Layer;
        View.LayerCount   = 1;
        LayerViews.push_back(Exchange_.ConstructView(DepthTexture, View));
    }
    TextureViewSpecification Layered;
    Layered.Dimension    = ViewDimension::PlanarLayered;
    Layered.DimensionSet = true;
    LayeredView = Exchange_.ConstructView(DepthTexture, Layered);
    SamplerSpecification Comparison;
    Comparison.Compare   = CompareFunction::LessEqual;
    Comparison.MagFilter = Comparison.MinFilter = FilterMode::Linear;
    ComparisonSampler = Exchange_.ConstructSampler(Comparison);
    CascadeRegion  = Exchange_.ConstructRegion(UniformStride * ShadowCascadeCount, "Cascade profiles");
    ReceiverRegion = Exchange_.ConstructRegion(256, "Cascade receiver");
    const std::vector<DescriptorSlot> Slots = {{DescriptorCode::UniformDynamic}, {DescriptorCode::StorageRead}};
    Layout_ = Exchange_.ConstructDescriptorLayout(Slots, "Shadow raster");
    Group_  = Exchange_.ConstructDescriptorGroup(Layout_, {DescriptorResource(CascadeRegion, 0, 64), OccupantRegion}, "Shadow raster");
    RasterProgramSpecification Raster;
    Raster.VertexEntry    = "ShadowVertex";
    Raster.VertexLayouts  = {{VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {1, 28, VertexFormat::Uint32}}}};
    Raster.DepthEnabled   = true;
    Raster.DepthWrite     = true;
    Raster.DepthCompare   = CompareFunction::Less;
    Raster.DepthBias      = 2.0f;
    Raster.DepthBiasSlope = 2.0f;
    Raster.DepthBiasClamp = 0.01f;
    Raster.Layouts        = {Layout_};
    Program_ = Shaders_.ConstructRasterProgram("ShadowRaster", Raster);
}

void ShadowProjection::ConstructMaskedProgram(const std::shared_ptr<DeviceRegion>& OccupantRegion, const std::shared_ptr<DeviceRegion>& EffectRegion,
                                              const std::shared_ptr<DeviceRegion>& VehicleRegion)
{
    const std::vector<DescriptorSlot> Slots = {{DescriptorCode::UniformDynamic}, {DescriptorCode::StorageRead}, {DescriptorCode::Uniform},
                                               {DescriptorCode::StorageRead}};
    MaskedLayout_ = Exchange_.ConstructDescriptorLayout(Slots, "Vehicle shadow raster");
    MaskedGroup_  = Exchange_.ConstructDescriptorGroup(MaskedLayout_, {DescriptorResource(CascadeRegion, 0, 64), OccupantRegion, EffectRegion, VehicleRegion},
                                                       "Vehicle shadow raster");
    RasterProgramSpecification Raster;
    Raster.VertexEntry    = "MaskedShadowVertex";
    Raster.FragmentEntry  = "MaskedShadowFragment";
    Raster.VertexLayouts  = {{VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {1, 28, VertexFormat::Uint32}, {2, 12, VertexFormat::Uint32}}}};
    Raster.DepthEnabled   = true;
    Raster.DepthWrite     = true;
    Raster.DepthCompare   = CompareFunction::Less;
    Raster.DepthBias      = 2.0f;
    Raster.DepthBiasSlope = 2.0f;
    Raster.DepthBiasClamp = 0.01f;
    Raster.Layouts        = {MaskedLayout_};
    MaskedProgram_ = Shaders_.ConstructRasterProgram("ShadowRaster", Raster);
}

// 📝 Each cascade is the bounding sphere of its frustum slice (rotation-invariant footprint), its centre snapped to whole
//    shadow texels in light space so translation does not crawl the edges.
void ShadowProjection::ResolveCascades(const CameraPose& Camera, const Vector3& LightDirection)
{
    const Vector3 Up      = std::fabs(LightDirection[2]) > 0.98 ? Vector3{1.0, 0.0, 0.0} : Vector3{0.0, 0.0, 1.0};
    const Vector3 Forward = Camera.Forward;
    const Vector3 Right   = NormalizeVector(CrossVector(Forward, {0.0, 0.0, 1.0}));
    const Vector3 Upward  = CrossVector(Right, Forward);
    const double  TanY    = std::tan(0.5 * Camera.VerticalFieldOfView);
    const double  TanX    = TanY * Camera.Aspect;
    std::vector<float> Receiver(64, 0.0f);
    std::vector<float> Records(ShadowCascadeCount * UniformStride / 4, 0.0f);
    for (uint32_t Cascade = 0; Cascade < ShadowCascadeCount; ++Cascade)
    {
        const double Near = std::max(CascadeSplits[Cascade], Camera.Near);
        const double Far  = CascadeSplits[Cascade + 1];
        Vector3 Centre{0.0, 0.0, 0.0};
        std::vector<Vector3> Corners;
        for (double Distance : {Near, Far})
        {
            for (double SignX : {-1.0, 1.0})
            {
                for (double SignY : {-1.0, 1.0})
                {
                    const Vector3 Corner = MultiplyAddVector(MultiplyAddVector(MultiplyAddVector(Camera.Position, Forward, Distance), Right,
                                                                               SignX * TanX * Distance), Upward, SignY * TanY * Distance);
                    Corners.push_back(Corner);
                    Centre = AddVector(Centre, Corner);
                }
            }
        }
        Centre = ScaleVector(Centre, 1.0 / 8.0);
        double Radius = 0.0;
        for (const Vector3& Corner : Corners) Radius = std::max(Radius, LengthVector(SubtractVector(Corner, Centre)));
        Radius = std::ceil(Radius * 16.0) / 16.0;
        const double    TexelWorld = (2.0 * Radius) / Extent;
        const double    Reach      = Radius + 220.0;
        const Transform View       = ConstructViewTransform(MultiplyAddVector(Centre, LightDirection, Reach), Centre, Up);
        const Vector3   Snapped    = TransformPoint(View, Centre);
        const double    OffsetX    = Snapped[0] - RoundHalfUp(Snapped[0] / TexelWorld) * TexelWorld;
        const double    OffsetY    = Snapped[1] - RoundHalfUp(Snapped[1] / TexelWorld) * TexelWorld;
        const Transform Projection = ConstructOrthographicTransform(-Radius + OffsetX, Radius + OffsetX, -Radius + OffsetY, Radius + OffsetY, 1.0,
                                                                   Reach + Radius + 60.0);
        const Transform LightViewProjection = MultiplyTransform(Projection, View);
        for (int Element = 0; Element < 16; ++Element)
        {
            Receiver[Cascade * 16 + Element]                       = static_cast<float>(LightViewProjection[Element]);
            Records[Cascade * UniformStride / 4 + Element]         = static_cast<float>(LightViewProjection[Element]);
        }
        Receiver[48 + Cascade] = static_cast<float>(Far);
        Receiver[52 + Cascade] = static_cast<float>(TexelWorld);
    }
    Receiver[51] = static_cast<float>(ShadowCascadeCount);
    LastCascadeBytes.assign(reinterpret_cast<const uint8_t*>(Records.data()), reinterpret_cast<const uint8_t*>(Records.data()) + Records.size() * 4);
    LastReceiverBytes.assign(reinterpret_cast<const uint8_t*>(Receiver.data()), reinterpret_cast<const uint8_t*>(Receiver.data()) + Receiver.size() * 4);
    Exchange_.WriteRegion(CascadeRegion, 0, LastCascadeBytes.data(), LastCascadeBytes.size());
    Exchange_.WriteRegion(ReceiverRegion, 0, LastReceiverBytes.data(), LastReceiverBytes.size());
}

void ShadowProjection::RecordCascades(CommandSequence& Sequence, const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion)
{
    // 📝 The car's runs always take the vehicle program: its tyres deform and its glazing may let the sun in.
    const bool Masked = MaskedProgram_ != nullptr;
    for (uint32_t Cascade = 0; Cascade < ShadowCascadeCount; ++Cascade)
    {
        const DepthAttachment Depth{LayerViews[Cascade], LoadOperation::Clear, 1.0f};
        Sequence.BeginRaster({}, &Depth, "Shadow cascade " + std::to_string(Cascade));
        Sequence.BindVertexRegion(0, VertexRegion);
        int Bound = -1;
        for (const DrawRun& Run : Scene.ShadowRuns)
        {
            const int Vehicle = (Masked && Run.First < 5) ? 1 : 0;
            if (Bound != Vehicle)
            {
                const uint32_t Offset = Cascade * UniformStride;
                Sequence.BindProgram(Vehicle ? MaskedProgram_ : Program_);
                Sequence.BindGroup(0, Vehicle ? MaskedGroup_ : Group_, std::span<const uint32_t>(&Offset, 1));
                Bound = Vehicle;
            }
            const FacetSet& Set = Scene.FacetSets[Run.FacetSet];
            Sequence.Draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
        }
        Sequence.EndRaster();
    }
}

} // namespace Frontier::GlobalIllumination
