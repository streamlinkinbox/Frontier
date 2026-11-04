/*==========================================================================================================================================
                                                           PICKPROJECTION.CPP
==========================================================================================================================================*/
// 🧩 Beyond the demo: GPU picking for the editor. The occupants drawn at and around one screen pixel, found by rasterising the
//    scene's own facet sets and occupant profiles from a camera's unjittered view-projection into a small target centred on that
//    pixel. It runs only when asked (a click), never inside a refresh.

#include "PickProjection.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>

namespace Frontier::GlobalIllumination
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   OCCUPANT PICK
//------------------------------------------------------------------------------------------------------------------------

uint32_t OccupantPick::QueryOccupant(int32_t OffsetX, int32_t OffsetY) const noexcept
{
    const int32_t Span   = static_cast<int32_t>(QuerySpan());
    const int32_t Column = static_cast<int32_t>(Reach) + OffsetX;
    const int32_t Row    = static_cast<int32_t>(Reach) + OffsetY;
    if (Column < 0 || Row < 0 || Column >= Span || Row >= Span) return NoOccupant;
    const size_t Index = static_cast<size_t>(Row) * static_cast<size_t>(Span) + static_cast<size_t>(Column);
    return Index < Occupants.size() ? Occupants[Index] : NoOccupant;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PICK PROJECTION
//------------------------------------------------------------------------------------------------------------------------

PickProjection::PickProjection(DeviceExchange& Exchange, ShaderInterchange& Shaders) : Exchange_(Exchange), Shaders_(Shaders)
{
}

void PickProjection::Construct(const std::shared_ptr<DeviceRegion>& OccupantRegion)
{
    using D = DescriptorCode;
    const std::vector<DescriptorSlot> Slots = {{D::Uniform}, {D::StorageRead}};
    Layout_        = Exchange_.ConstructDescriptorLayout(Slots, "Occupant pick");
    ProfileRegion_ = Exchange_.ConstructRegion(80, "Occupant pick profile");
    Group_         = Exchange_.ConstructDescriptorGroup(Layout_, {ProfileRegion_, OccupantRegion}, "Occupant pick");
    // 📝 The surface raster's vertex layout (position, occupant offset; the occupant from the instance list) with its depth test:
    //    reverse depth, nearest wins.
    RasterProgramSpecification Raster;
    Raster.VertexEntry   = "PickVertex";
    Raster.FragmentEntry = "PickFragment";
    Raster.VertexLayouts = {{VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {3, 28, VertexFormat::Uint32}}}, {4, true, {{4, 0, VertexFormat::Uint32}}}};
    Raster.DepthEnabled  = true;
    Raster.DepthWrite    = true;
    Raster.DepthCompare  = CompareFunction::Greater;
    Raster.Targets       = {{TextureFormat::R32Uint}};
    Raster.Layouts       = {Layout_};
    Program_ = Shaders_.ConstructRasterProgram("PickRaster", Raster);
    const uint32_t Span = 2u * Reach + 1u;
    auto Target = [&](const std::string& Label, TextureFormat Format, uint32_t Usage) {
        TextureSpecification Specification;
        Specification.Label  = Label;
        Specification.Width  = Span;
        Specification.Height = Span;
        Specification.Format = Format;
        Specification.Usage  = Usage;
        return Exchange_.ConstructTexture(Specification);
    };
    OccupantTexture_ = Target("Occupant pick", TextureFormat::R32Uint, TextureUsage::RenderAttachment | TextureUsage::CopySource);
    DepthTexture_    = Target("Occupant pick depth", TextureFormat::Depth32Float, TextureUsage::RenderAttachment);
    OccupantView_    = Exchange_.ConstructView(OccupantTexture_);
    DepthView_       = Exchange_.ConstructView(DepthTexture_);
}

bool PickProjection::Pick(const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion, const Transform& ViewProjection,
                          std::array<uint32_t, 2> ScreenExtent, std::array<uint32_t, 2> Pixel, OccupantPick& Outcome, std::string* OutDiagnostic)
{
    const auto Start = std::chrono::steady_clock::now();
    Outcome = {};
    Outcome.Reach = Reach;
    if (!Program_ || !VertexRegion || ScreenExtent[0] == 0 || ScreenExtent[1] == 0)
    {
        if (OutDiagnostic) *OutDiagnostic = "the pick program or the scene is not up";
        return false;
    }
    Pixel = {std::min(Pixel[0], ScreenExtent[0] - 1u), std::min(Pixel[1], ScreenExtent[1] - 1u)};
    Outcome.Pixel = Pixel;
    const uint32_t Span = 2u * Reach + 1u;

    // 📝 The occupant identities each draw run's instances index (one per occupant, as the surface raster's instance list).
    const uint32_t Count = static_cast<uint32_t>(Scene.Occupants.size());
    if (!InstanceRegion_ || InstanceCount_ != Count)
    {
        std::vector<uint32_t> Identities(std::max(1u, Count));
        std::iota(Identities.begin(), Identities.end(), 0u);
        InstanceRegion_ = Exchange_.ConstructRegionFrom(Identities.data(), Identities.size() * 4, "Occupant pick identities");
        InstanceCount_  = Count;
    }

    // 📝 PickProfile: the view-projection as the refresh stores it (column-major floats), then the picked pixel's centre in clip
    //    space (y up) and the extent ratio that stretches the neighbourhood over the target.
    std::array<float, 20> Profile{};
    for (size_t Element = 0; Element < 16; ++Element) Profile[Element] = static_cast<float>(ViewProjection[Element]);
    Profile[16] = static_cast<float>((Pixel[0] + 0.5) / ScreenExtent[0] * 2.0 - 1.0);
    Profile[17] = static_cast<float>(1.0 - (Pixel[1] + 0.5) / ScreenExtent[1] * 2.0);
    Profile[18] = static_cast<float>(static_cast<double>(ScreenExtent[0]) / Span);
    Profile[19] = static_cast<float>(static_cast<double>(ScreenExtent[1]) / Span);
    Exchange_.WriteRegion(ProfileRegion_, 0, Profile.data(), sizeof(Profile));

    CommandSequence Sequence = Exchange_.BeginSequence();
    const std::array<ColorAttachment, 1> Colors = {{{OccupantView_, LoadOperation::Clear, {0.0f, 0.0f, 0.0f, 0.0f}}}};
    const DepthAttachment Depth{DepthView_, LoadOperation::Clear, 0.0f};
    Sequence.BeginRaster(Colors, &Depth, "Occupant pick");
    Sequence.BindProgram(Program_);
    Sequence.BindGroup(0, Group_);
    Sequence.BindVertexRegion(0, VertexRegion);
    Sequence.BindVertexRegion(1, InstanceRegion_);
    for (const DrawRun& Run : Scene.DrawRuns)
    {
        const FacetSet& Set = Scene.FacetSets[Run.FacetSet];
        Sequence.Draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
    }
    Sequence.EndRaster();
    Exchange_.LabelSubmissions("occupant pick");
    std::string Diagnostic;
    const bool  Submitted = Exchange_.Submit(Sequence, &Diagnostic);
    Exchange_.LabelSubmissions("");
    std::vector<uint8_t> Bytes;
    if (!Submitted || !Exchange_.ReadTexture(OccupantTexture_, 0, 0, Bytes) || Bytes.size() < static_cast<size_t>(Span) * Span * 4)
    {
        if (OutDiagnostic) *OutDiagnostic = "occupant pick failed" + (Diagnostic.empty() ? std::string() : ": " + Diagnostic);
        return false;
    }
    Outcome.Occupants.resize(static_cast<size_t>(Span) * Span);
    for (size_t Texel = 0; Texel < Outcome.Occupants.size(); ++Texel)
    {
        uint32_t Value = 0;
        std::memcpy(&Value, Bytes.data() + Texel * 4, 4);
        Outcome.Occupants[Texel] = Value == 0 ? OccupantPick::NoOccupant : Value - 1u;
    }
    Outcome.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    return true;
}

} // namespace Frontier::GlobalIllumination
