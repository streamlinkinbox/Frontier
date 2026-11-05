/*==========================================================================================================================================
                                                    XRAYSCANOVERLAYSEQUENCE.CPP
==========================================================================================================================================*/

#include "XRayScanOverlaySequence.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/SceneStructure.h"

namespace Frontier::XRayScan
{

namespace
{

// 📝 GarageView.js AdditiveBlend and OverBlend: colour by the layer's alpha, the target's alpha untouched.
ColorTarget ConstructTarget(TextureFormat Format, BlendFactor Destination)
{
    ColorTarget Target;
    Target.Format       = Format;
    Target.BlendEnabled = true;
    Target.Color        = {BlendOperation::Add, BlendFactor::SourceAlpha, Destination};
    Target.Alpha        = {BlendOperation::Add, BlendFactor::Zero, BlendFactor::One};
    return Target;
}

} // namespace

XRayScanOverlaySequence::XRayScanOverlaySequence(DeviceExchange& Exchange, GlobalIllumination::ShaderInterchange& Shaders) : Exchange_(Exchange), Shaders_(Shaders)
{
}

bool XRayScanOverlaySequence::Construct(TextureFormat TargetFormat, const ScanRasterResources& Raster, const std::shared_ptr<DeviceRegion>& VertexRegion,
                                        const std::shared_ptr<DeviceRegion>& InstanceRegion, const ScanEdgeRecords& Edges, std::span<const float> Sparkles)
{
    using D = DescriptorCode;
    Raster_         = Raster;
    VertexRegion_   = VertexRegion;
    InstanceRegion_ = InstanceRegion;
    // 📝 The surface raster's six slots, then the scan's: its profile, the wireframe segments, the particles, the view fields and the depth.
    const std::vector<DescriptorSlot> Slots = {{D::Uniform}, {D::StorageRead}, {D::Texture2D}, {D::Sampler}, {D::Uniform}, {D::StorageRead},
                                               {D::Uniform}, {D::StorageRead}, {D::StorageRead}, {D::Uniform}, {D::TextureDepth2D}};
    Layout_        = Exchange_.ConstructDescriptorLayout(Slots, "Scan overlay");
    ProfileRegion_ = Exchange_.ConstructRegion(ScanProfileBytes, "Scan profile");
    ViewRegion_    = Exchange_.ConstructRegion(ScanViewBytes, "Scan view");
    // 📝 A storage binding needs at least one element: no segment or no particle still binds one zero element, as the prototype does.
    const std::vector<float> NoRecord(8, 0.0f);
    EdgeCount_     = Edges.Count;
    SparkleCount_  = static_cast<uint32_t>(Sparkles.size() / 4);
    EdgeRegion_    = Edges.Count > 0 ? Exchange_.ConstructRegionFrom(Edges.Records.data(), Edges.Records.size() * sizeof(float), "Scan edges")
                                     : Exchange_.ConstructRegionFrom(NoRecord.data(), NoRecord.size() * sizeof(float), "Scan edges");
    SparkleRegion_ = SparkleCount_ > 0 ? Exchange_.ConstructRegionFrom(Sparkles.data(), Sparkles.size() * sizeof(float), "Scan particles")
                                       : Exchange_.ConstructRegionFrom(NoRecord.data(), NoRecord.size() * sizeof(float), "Scan particles");

    auto Declare = [&](const std::string& Vertex, const std::string& Fragment, BlendFactor Destination, bool Facets) {
        RasterProgramSpecification Specification;
        Specification.VertexEntry   = Vertex;
        Specification.FragmentEntry = Fragment;
        Specification.Topology      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        Specification.CullMode      = VK_CULL_MODE_NONE;
        Specification.Targets       = {ConstructTarget(TargetFormat, Destination)};
        Specification.Layouts       = {Layout_};
        if (Facets)
        {
            // 📝 The surface raster's vertex layout: position, family, normal and occupant offset per vertex, the occupant index per instance.
            Specification.VertexLayouts = {{GlobalIllumination::VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {1, 12, VertexFormat::Uint32},
                                                                                      {2, 16, VertexFormat::Float32x3}, {3, 28, VertexFormat::Uint32}}},
                                           {4, true, {{4, 0, VertexFormat::Uint32}}}};
        }
        std::shared_ptr<DeviceProgram> Program = Shaders_.ConstructRasterProgram("XRayScanOverlay", Specification);
        if (!Program) Diagnostic_ += Shaders_.LastDiagnostic() + "\n";
        return Program;
    };
    // 📝 The program unit is named for each entry's own file: XRayScanOverlay.<Entry>.spv; ShaderInterchange resolves the vertex entry's file
    //    and the fragment entry's from the specification's entries.
    Ghost_   = Declare("GhostVertex", "GhostFragment", BlendFactor::One, true);
    Edge_    = Declare("EdgeVertex", "EdgeFragment", BlendFactor::OneMinusSourceAlpha, false);
    Curtain_ = Declare("CurtainVertex", "CurtainFragment", BlendFactor::One, false);
    Sparkle_ = Declare("SparkleVertex", "SparkleFragment", BlendFactor::OneMinusSourceAlpha, false);
    return Ghost_ && Edge_ && Curtain_ && Sparkle_;
}

void XRayScanOverlaySequence::BindDepth(const std::shared_ptr<DeviceTextureView>& DepthView)
{
    Group_ = Exchange_.ConstructDescriptorGroup(Layout_, {Raster_.Refresh, Raster_.Occupants, Raster_.TyreMarks, Raster_.TyreMarkSampler, Raster_.Effect,
                                                          Raster_.Vehicle, ProfileRegion_, EdgeRegion_, SparkleRegion_, ViewRegion_, DepthView},
                                                "Scan overlay");
}

void XRayScanOverlaySequence::Update(const XRayScanSequence& Scan, const ScanStage& Stage, std::span<const uint32_t> ScannedOccupants, double CameraNear,
                                     double HudLineTop)
{
    const std::array<float, ScanProfileBytes / 4> Profile = EncodeScanProfile(Scan, Stage, SparkleCount_, HudLineTop);
    const XRayScanView View = EncodeScanView(Scan, Stage, ScannedOccupants, CameraNear);
    Exchange_.WriteRegion(ProfileRegion_, 0, Profile.data(), ScanProfileBytes);
    Exchange_.WriteRegion(ViewRegion_, 0, &View, ScanViewBytes);
}

void XRayScanOverlaySequence::Record(CommandSequence& Sequence, const std::shared_ptr<DeviceTextureView>& ColourView, const XRayScanSequence& Scan,
                                     std::span<const ScanShellRun> Shell)
{
    Counts_ = {};
    const std::array<ColorAttachment, 1> Colours = {{{ColourView, LoadOperation::Load, {0, 0, 0, 0}}}};
    Sequence.BeginRaster(Colours, nullptr, "Scan overlay");
    // 📝 Ghost, wireframe and curtain while anything of the scan shows; particles while the toggle is on (GarageView.RecordOverlay).
    if (Scan.QueryVisible())
    {
        if (Scan.QueryAmount() > 0.0)
        {
            Sequence.BindProgram(Ghost_);
            Sequence.BindGroup(0, Group_);
            Sequence.BindVertexRegion(0, VertexRegion_);
            Sequence.BindVertexRegion(1, InstanceRegion_);
            for (const ScanShellRun& Run : Shell)
            {
                Sequence.Draw(Run.VertexCount, Run.InstanceCount, Run.FirstVertex, Run.FirstInstance);
                ++Counts_.ShellDraws;
            }
            if (EdgeCount_ > 0)
            {
                Sequence.BindProgram(Edge_);
                Sequence.BindGroup(0, Group_);
                Sequence.Draw(6, EdgeCount_);
                ++Counts_.EdgeDraws;
            }
        }
        if (Scan.QueryRamp() > 0.0)
        {
            Sequence.BindProgram(Curtain_);
            Sequence.BindGroup(0, Group_);
            Sequence.Draw(6, 2);
            ++Counts_.CurtainDraws;
        }
    }
    if (Scan.QueryEnabled())
    {
        Sequence.BindProgram(Sparkle_);
        Sequence.BindGroup(0, Group_);
        Sequence.Draw(6, SparkleCount_);
        ++Counts_.SparkleDraws;
    }
    Sequence.EndRaster();
}

} // namespace Frontier::XRayScan
