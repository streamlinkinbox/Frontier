/*==========================================================================================================================================
                                                        PLUMESTANDINS.CPP
==========================================================================================================================================*/

#include "PlumeStandIns.h"

#include <vector>

namespace Frontier::PlumeSimulation
{

namespace
{

std::shared_ptr<DeviceTextureView> ConstructViewOf(DeviceExchange& Exchange, const std::shared_ptr<DeviceTexture>& Texture, ViewDimension Dimension)
{
    TextureViewSpecification Specification;
    Specification.Dimension    = Dimension;
    Specification.DimensionSet = true;
    return Exchange.ConstructView(Texture, Specification);
}

} // namespace

bool ConstructStandInDepth(DeviceExchange& Exchange, std::array<uint32_t, 2> Screen, PlumeStandIns& StandIns, std::string& Diagnostic)
{
    TextureSpecification Depth;
    Depth.Label           = "Surface depth";
    Depth.Width           = Screen[0];
    Depth.Height          = Screen[1];
    Depth.Format          = TextureFormat::Depth32Float;
    Depth.Usage           = TextureUsage::Sampled | TextureUsage::RenderAttachment;
    StandIns.DepthTexture = Exchange.ConstructTexture(Depth);
    StandIns.DepthView    = Exchange.ConstructView(StandIns.DepthTexture);
    // 📝 Depth images are not zero-filled by the exchange: an empty raster scope clears it to the reversed far plane: nothing stored, nothing occludes.
    {
        CommandSequence       Sequence = Exchange.BeginSequence();
        const DepthAttachment Cleared{ StandIns.DepthView, LoadOperation::Clear, 0.0f };
        Sequence.BeginRaster({}, &Cleared, "Surface depth");
        Sequence.EndRaster();
        if (!Exchange.Submit(Sequence, &Diagnostic)) return false;
    }
    StandIns.March.SurfaceDepth = StandIns.DepthView;
    return true;
}

bool ConstructPlumeStandIns(DeviceExchange& Exchange, std::array<uint32_t, 2> Screen, PlumeStandIns& StandIns, std::string& Diagnostic)
{
    using D = DescriptorCode;
    // ① the scene distance field's group: zero cascades, the ground plane at z = 0
    auto                                              DistanceRecords = Exchange.ConstructRegion(160, "Distance record");
    std::array<std::shared_ptr<DeviceTextureView>, 4> ClipViews{};
    for (uint32_t Level = 0; Level < 4; ++Level)
    {
        TextureSpecification Clip;
        Clip.Label       = "Clip " + std::to_string(Level);
        Clip.Dimension   = TextureDimension::Volumetric;
        Clip.Format      = TextureFormat::Rgba16Float;
        Clip.Usage       = TextureUsage::Sampled;
        ClipViews[Level] = ConstructViewOf(Exchange, Exchange.ConstructTexture(Clip), ViewDimension::Volumetric);
    }
    SamplerSpecification Cascade;
    Cascade.MagFilter    = Cascade.MinFilter = FilterMode::Linear;
    Cascade.AddressU     = Cascade.AddressV = Cascade.AddressW = AddressMode::Repeat;
    auto CascadeSampler  = Exchange.ConstructSampler(Cascade);
    auto EpochRegion     = Exchange.ConstructRegion(16, "Brick stamps");
    StandIns.DistanceLayout = Exchange.ConstructDescriptorLayout(
        std::vector<DescriptorSlot>{ { D::Uniform }, { D::Texture3D }, { D::Texture3D }, { D::Texture3D }, { D::Texture3D }, { D::Sampler }, { D::StorageRead } }, "Distance traversal");
    StandIns.DistanceGroup = Exchange.ConstructDescriptorGroup(StandIns.DistanceLayout, { DistanceRecords, ClipViews[0], ClipViews[1], ClipViews[2], ClipViews[3], CascadeSampler, EpochRegion });
    StandIns.Occupants     = Exchange.ConstructRegion(336 * 4, "Occupants");

    // ② what the march reads beside the plume: the refresh constants, a surface with no depth, a shadow with no cascade, a one-texel sky
    StandIns.Refresh = Exchange.ConstructRegion(1072, "Refresh constants");
    if (!ConstructStandInDepth(Exchange, Screen, StandIns, Diagnostic)) return false;
    TextureSpecification Shadow;
    Shadow.Label  = "Shadow depth";
    Shadow.Format = TextureFormat::Depth32Float;
    Shadow.Usage  = TextureUsage::Sampled;
    auto                 ShadowTexture = Exchange.ConstructTexture(Shadow);
    SamplerSpecification Comparison;
    Comparison.MagFilter = Comparison.MinFilter = FilterMode::Linear;
    Comparison.Compare                          = CompareFunction::Less;
    TextureSpecification Sky;
    Sky.Label                         = "Sky view";
    Sky.Format                        = TextureFormat::Rgba8Unorm;
    Sky.Usage                         = TextureUsage::Sampled | TextureUsage::CopyTarget;
    auto                         SkyTexture = Exchange.ConstructTexture(Sky);
    const std::array<uint8_t, 4> SkyTexel   = { 115, 158, 230, 255 };
    Exchange.WriteTexture(SkyTexture, 0, SkyTexel.data(), SkyTexel.size(), 1, 1);
    SamplerSpecification SkySampling;
    SkySampling.MagFilter                = SkySampling.MinFilter = FilterMode::Linear;
    StandIns.March.Refresh               = StandIns.Refresh;
    StandIns.March.SurfaceDepth          = StandIns.DepthView;
    StandIns.March.ShadowLayered         = ConstructViewOf(Exchange, ShadowTexture, ViewDimension::PlanarLayered);
    StandIns.March.ShadowComparison      = Exchange.ConstructSampler(Comparison);
    StandIns.March.ShadowReceiver        = Exchange.ConstructRegion(224, "Cascade receiver");
    StandIns.March.SkyView               = Exchange.ConstructView(SkyTexture);
    StandIns.March.SkySampler            = Exchange.ConstructSampler(SkySampling);
    return true;
}

} // namespace Frontier::PlumeSimulation
