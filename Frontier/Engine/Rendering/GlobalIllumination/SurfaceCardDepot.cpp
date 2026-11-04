/*==========================================================================================================================================
                                                         SURFACECARDDEPOT.CPP
==========================================================================================================================================*/
// 🧩 Oriented surface cards per occupant in one texel space: captured once, relit by a scheduled tile budget (SurfaceCardDepot.js).

#include "SurfaceCardDepot.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <unordered_set>

namespace Frontier::GlobalIllumination
{

namespace
{
constexpr double   MinimumCardTexels = 4.0;     // [texel] shortest card edge
constexpr double   MaximumCardTexels = 256.0;   // [texel] longest card edge
constexpr uint32_t CardStride        = 48;      // [B] CardProfile

// 📝 JS Math.round: halfway cases round towards +∞.
double RoundHalfUp(double Scalar)
{
    return std::floor(Scalar + 0.5);
}
} // namespace

SurfaceCardDepot::SurfaceCardDepot(DeviceExchange& Exchange, ShaderInterchange& Shaders, const SurfaceCardConfiguration& Configuration)
    : Configuration(Configuration), Exchange_(Exchange), Shaders_(Shaders)
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CARD ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Six inward-looking cards on the faces of each occupant's local card bounds (one +Z card for a ground tile), sized by
//    texel density, rounded to whole 4×4 radiosity sites, shelf-packed; on overflow every density shrinks by 0.8.
void SurfaceCardDepot::AllocateCards(SceneStructure& Scene)
{
    double DensityScale = Configuration.DensityScale;
    for (int Attempt = 0; Attempt < 12; ++Attempt)
    {
        Cards.clear();
        for (uint32_t Index = 0; Index < Scene.Occupants.size(); ++Index)
        {
            OccupantEntry& Occupant = Scene.Occupants[Index];
            Occupant.FirstCard = static_cast<uint32_t>(Cards.size());
            Occupant.CardCount = 0;
            if (!(Occupant.Participation & OccupantParticipation::Cards)) continue;
            Vector3    Minimum = Occupant.CardMinimum;
            Vector3    Maximum = Occupant.CardMaximum;
            const bool Ground  = !(Occupant.Participation & OccupantParticipation::Distance);
            if (!Ground)
            {
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    const double Grow = std::max(0.01, 0.01 * (Maximum[Axis] - Minimum[Axis]));
                    Minimum[Axis] -= Grow;
                    Maximum[Axis] += Grow;
                }
            }
            const std::vector<uint32_t> Axes = Ground ? std::vector<uint32_t>{4} : std::vector<uint32_t>{0, 1, 2, 3, 4, 5};
            for (uint32_t Axis : Axes)
            {
                const uint32_t DepthAxis = Axis >> 1;
                const uint32_t AcrossU   = (DepthAxis + 1) % 3;
                const uint32_t AcrossV   = (DepthAxis + 2) % 3;
                const double   Density   = Occupant.CardDensity * DensityScale;
                auto Size = [Density](double Length) {
                    const double Clamped = std::min(MaximumCardTexels, std::max(MinimumCardTexels, RoundHalfUp(Length * Density)));
                    return static_cast<uint32_t>(std::ceil(Clamped / 4.0) * 4.0);
                };
                SurfaceCard Card;
                Card.LocalMinimum = Minimum;
                Card.LocalMaximum = Maximum;
                Card.Axis         = Axis;
                Card.Occupant     = Index;
                Card.TexelExtent  = {Size(Maximum[AcrossU] - Minimum[AcrossU]), Size(Maximum[AcrossV] - Minimum[AcrossV])};
                Cards.push_back(Card);
                Occupant.CardCount++;
            }
        }
        if (PackCards())
        {
            DensityScaleUsed = DensityScale;
            break;
        }
        DensityScale *= 0.8;
        if (Attempt == 11) throw std::runtime_error("Surface cards do not fit the card texel space");
    }
    GroundFirstCard = Scene.Occupants[static_cast<size_t>(Scene.GroundTilesFirst)].FirstCard;
    GroundStripCard = Scene.Occupants[static_cast<size_t>(Scene.GroundStrips[0])].FirstCard;

    OccupiedTexels = 0;
    TileTotal      = 0;
    OccupantTiles.assign(Scene.Occupants.size(), {});
    for (uint32_t CardIndex = 0; CardIndex < Cards.size(); ++CardIndex)
    {
        const SurfaceCard& Card = Cards[CardIndex];
        OccupiedTexels += static_cast<uint64_t>(Card.TexelExtent[0]) * Card.TexelExtent[1];
        for (uint32_t TileY = 0; TileY < (Card.TexelExtent[1] + 7) / 8; ++TileY)
        {
            for (uint32_t TileX = 0; TileX < (Card.TexelExtent[0] + 7) / 8; ++TileX)
            {
                OccupantTiles[Card.Occupant].push_back({CardIndex, (TileY << 16) | TileX});
                TileTotal++;
            }
        }
    }
    RebuildTileLists(Scene);
}

// 📝 Enabled occupants only: moving ones every refresh, static ones (and Dynamic with Moving false) round-robin.
void SurfaceCardDepot::RebuildTileLists(const SceneStructure& Scene)
{
    DynamicTiles.clear();
    StaticTiles.clear();
    for (uint32_t Index = 0; Index < Scene.Occupants.size(); ++Index)
    {
        const OccupantEntry& Occupant = Scene.Occupants[Index];
        if (Occupant.Inactive) continue;
        const bool Moving = (Occupant.Participation & OccupantParticipation::Dynamic) && Occupant.Moving.value_or(true);
        auto& Target = Moving ? DynamicTiles : StaticTiles;
        Target.insert(Target.end(), OccupantTiles[Index].begin(), OccupantTiles[Index].end());
    }
    if (Cursor >= StaticTiles.size()) Cursor = 0;
}

void SurfaceCardDepot::WarmOccupants(const SceneStructure& Scene, const std::vector<uint32_t>& Indices, uint32_t Refreshes)
{
    std::vector<CardTile> Tiles;
    for (uint32_t Index : Indices)
    {
        const OccupantEntry& Occupant = Scene.Occupants[Index];
        if ((Occupant.Participation & OccupantParticipation::Dynamic) && Occupant.Moving.value_or(true)) continue;
        Tiles.insert(Tiles.end(), OccupantTiles[Index].begin(), OccupantTiles[Index].end());
    }
    PriorityTiles     = Tiles;
    PriorityRefreshes = Tiles.empty() ? 0u : Refreshes;
}

// 📝 Shelf packing in descending card height; Array.prototype.sort is stable, so is this.
bool SurfaceCardDepot::PackCards()
{
    const uint32_t Extent = Configuration.Extent;
    std::vector<uint32_t> Order(Cards.size());
    std::iota(Order.begin(), Order.end(), 0u);
    std::stable_sort(Order.begin(), Order.end(), [this](uint32_t Left, uint32_t Right) {
        return Cards[Right].TexelExtent[1] < Cards[Left].TexelExtent[1];
    });
    uint32_t ShelfX = 0, ShelfY = 0, ShelfHeight = 0;
    for (uint32_t Index : Order)
    {
        SurfaceCard&   Card   = Cards[Index];
        const uint32_t Width  = Card.TexelExtent[0];
        const uint32_t Height = Card.TexelExtent[1];
        if (ShelfX + Width > Extent)
        {
            ShelfY += ShelfHeight;
            ShelfX      = 0;
            ShelfHeight = 0;
        }
        if (ShelfY + Height > Extent) return false;
        Card.TexelOrigin    = {ShelfX, ShelfY};
        Card.TexelAllotment = Card.TexelExtent;
        ShelfX += Width;
        ShelfHeight = std::max(ShelfHeight, Height);
    }
    RuntimeShelfX_      = 0;
    RuntimeShelfY_      = ShelfY + ShelfHeight;
    RuntimeShelfHeight_ = 0;
    return true;
}

void SurfaceCardDepot::EncodeCard(const SurfaceCard& Card, uint8_t* Target) const
{
    float    Scalars[12] = {};
    uint32_t Words[12]   = {};
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Scalars[Axis]     = static_cast<float>(Card.LocalMinimum[Axis]);
        Scalars[4 + Axis] = static_cast<float>(Card.LocalMaximum[Axis]);
    }
    std::memcpy(Words, Scalars, sizeof(Scalars));
    Words[3]  = Card.Axis;
    Words[7]  = Card.Occupant;
    Words[8]  = Card.TexelOrigin[0];
    Words[9]  = Card.TexelOrigin[1];
    Words[10] = Card.TexelExtent[0];
    Words[11] = Card.TexelExtent[1];
    std::memcpy(Target, Words, CardStride);
}

std::vector<uint8_t> SurfaceCardDepot::EncodeCardProfiles() const
{
    std::vector<uint8_t> Bytes(std::max<size_t>(1, Cards.size()) * CardStride, 0u);
    for (size_t Index = 0; Index < Cards.size(); ++Index) EncodeCard(Cards[Index], Bytes.data() + Index * CardStride);
    return Bytes;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   REASSIGNMENT
//------------------------------------------------------------------------------------------------------------------------

bool SurfaceCardDepot::AllotRuntimeRectangle(uint32_t Width, uint32_t Height, std::array<uint32_t, 2>& Origin)
{
    const uint32_t Extent = Configuration.Extent;
    if (Width > Extent) return false;
    if (RuntimeShelfX_ + Width > Extent)
    {
        RuntimeShelfY_ += RuntimeShelfHeight_;
        RuntimeShelfX_      = 0;
        RuntimeShelfHeight_ = 0;
    }
    if (RuntimeShelfY_ + Height > Extent) return false;
    Origin = {RuntimeShelfX_, RuntimeShelfY_};
    RuntimeShelfX_ += Width;
    RuntimeShelfHeight_ = std::max(RuntimeShelfHeight_, Height);
    return true;
}

std::vector<uint32_t> SurfaceCardDepot::ReassignOccupantCards(SceneStructure& Scene, uint32_t Index)
{
    std::vector<uint32_t> Changed;
    OccupantEntry&        Occupant = Scene.Occupants[Index];
    // 📝 The occupant's cards are the run that names it from FirstCard on (six for a reserve occupant, kept whatever it shows).
    uint32_t Count = 0;
    while (Occupant.FirstCard + Count < Cards.size() && Cards[Occupant.FirstCard + Count].Occupant == Index) ++Count;
    TileTotal -= OccupantTiles[Index].size();
    OccupantTiles[Index].clear();
    if (!(Occupant.Participation & OccupantParticipation::Cards) || Count == 0)
    {
        Occupant.CardCount = 0;
        return Changed;
    }
    Occupant.CardCount = Count;
    Vector3 Minimum = Occupant.CardMinimum;
    Vector3 Maximum = Occupant.CardMaximum;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const double Grow = std::max(0.01, 0.01 * (Maximum[Axis] - Minimum[Axis]));
        Minimum[Axis] -= Grow;
        Maximum[Axis] += Grow;
    }
    const double Density = Occupant.CardDensity * DensityScaleUsed;
    auto Size = [Density](double Length) {
        const double Clamped = std::min(MaximumCardTexels, std::max(MinimumCardTexels, RoundHalfUp(Length * Density)));
        return static_cast<uint32_t>(std::ceil(Clamped / 4.0) * 4.0);
    };
    uint64_t Tiles = TileTotal;
    for (uint32_t Local = 0; Local < Count; ++Local)
    {
        const uint32_t CardIndex = Occupant.FirstCard + Local;
        SurfaceCard&   Card      = Cards[CardIndex];
        const uint32_t DepthAxis = Card.Axis >> 1;
        const uint32_t AcrossU   = (DepthAxis + 1) % 3;
        const uint32_t AcrossV   = (DepthAxis + 2) % 3;
        std::array<uint32_t, 2> Wanted = {Size(Maximum[AcrossU] - Minimum[AcrossU]), Size(Maximum[AcrossV] - Minimum[AcrossV])};
        const uint64_t WantedTiles = static_cast<uint64_t>((Wanted[0] + 7) / 8) * ((Wanted[1] + 7) / 8);
        if (Wanted[0] > Card.TexelAllotment[0] || Wanted[1] > Card.TexelAllotment[1])
        {
            std::array<uint32_t, 2> Origin{};
            if (Tiles + WantedTiles <= TileCapacity_ && AllotRuntimeRectangle(Wanted[0], Wanted[1], Origin))
            {
                Card.TexelOrigin    = Origin;
                Card.TexelAllotment = Wanted;
            }
            else
            {
                Wanted = {std::min(Wanted[0], Card.TexelAllotment[0]), std::min(Wanted[1], Card.TexelAllotment[1])};
            }
        }
        Card.LocalMinimum = Minimum;
        Card.LocalMaximum = Maximum;
        Card.TexelExtent  = Wanted;
        for (uint32_t TileY = 0; TileY < (Card.TexelExtent[1] + 7) / 8; ++TileY)
            for (uint32_t TileX = 0; TileX < (Card.TexelExtent[0] + 7) / 8; ++TileX)
            {
                OccupantTiles[Index].push_back({CardIndex, (TileY << 16) | TileX});
                ++Tiles;
            }
        uint8_t Bytes[CardStride] = {};
        EncodeCard(Card, Bytes);
        if (CardRegion) Exchange_.WriteRegion(CardRegion, static_cast<VkDeviceSize>(CardIndex) * CardStride, Bytes, CardStride);
        Changed.push_back(CardIndex);
    }
    TileTotal = Tiles;
    return Changed;
}

void SurfaceCardDepot::RecaptureCards(const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion,
                                      const std::vector<uint32_t>& CardIndices)
{
    if (CardIndices.empty() || !CaptureProgram) return;
    // ① clear every rectangle (its whole allotment) to the capture's clear values, and its direct, final and radiosity terms
    for (const uint32_t CardIndex : CardIndices)
    {
        const SurfaceCard& Card   = Cards[CardIndex];
        const uint32_t     Width  = Card.TexelAllotment[0];
        const uint32_t     Height = Card.TexelAllotment[1];
        const size_t       Texels = static_cast<size_t>(Width) * Height;
        const std::vector<uint8_t>  Clear8(Texels * 4, 0u);
        std::vector<uint8_t>        Normal(Texels * 4, 0u);
        for (size_t Texel = 0; Texel < Texels; ++Texel)
        {
            Normal[Texel * 4]     = 128u;
            Normal[Texel * 4 + 1] = 128u;
            Normal[Texel * 4 + 2] = 255u;
        }
        const std::vector<uint16_t> Clear16(Texels * 4, 0u);
        const std::vector<uint16_t> Depth(Texels, 0x4400u);   // [-] 4.0 in binary16, the capture's far depth
        const uint32_t X = Card.TexelOrigin[0], Y = Card.TexelOrigin[1];
        Exchange_.WriteTexture(AlbedoTexture, 0, Clear8.data(), Clear8.size(), Width, Height, 1, X, Y);
        Exchange_.WriteTexture(NormalTexture, 0, Normal.data(), Normal.size(), Width, Height, 1, X, Y);
        Exchange_.WriteTexture(IncandescenceTexture, 0, Clear16.data(), Clear16.size() * 2, Width, Height, 1, X, Y);
        Exchange_.WriteTexture(DepthTexture, 0, Depth.data(), Depth.size() * 2, Width, Height, 1, X, Y);
        Exchange_.WriteTexture(DirectTexture, 0, Clear16.data(), Clear16.size() * 2, Width, Height, 1, X, Y);
        Exchange_.WriteTexture(FinalTexture, 0, Clear16.data(), Clear16.size() * 2, Width, Height, 1, X, Y);
        const std::vector<float> Sites(static_cast<size_t>(Width / 4) * 4, 0.0f);
        for (uint32_t SiteY = Y / 4; SiteY < (Y + Height) / 4; ++SiteY)
            Exchange_.WriteRegion(SiteRegion, (static_cast<VkDeviceSize>(SiteY) * SiteStride + X / 4) * 16, Sites.data(), Sites.size() * 4);
    }
    // ② draw only these cards, keeping every other texel
    if (!CaptureDepth_)
    {
        TextureSpecification DepthSpecification;
        DepthSpecification.Label  = "Card recapture depth";
        DepthSpecification.Width  = Configuration.Extent;
        DepthSpecification.Height = Configuration.Extent;
        DepthSpecification.Format = TextureFormat::Depth32Float;
        DepthSpecification.Usage  = TextureUsage::RenderAttachment;
        CaptureDepth_     = Exchange_.ConstructTexture(DepthSpecification);
        CaptureDepthView_ = Exchange_.ConstructView(CaptureDepth_);
    }
    CommandSequence Sequence = Exchange_.BeginSequence();
    const std::array<ColorAttachment, 4> Colors = {{{AlbedoView, LoadOperation::Load, {0, 0, 0, 0}},
                                                    {NormalView, LoadOperation::Load, {0.5f, 0.5f, 1.0f, 0.0f}},
                                                    {IncandescenceView, LoadOperation::Load, {0, 0, 0, 0}},
                                                    {DepthView, LoadOperation::Load, {4.0f, 0, 0, 0}}}};
    const DepthAttachment Depth{CaptureDepthView_, LoadOperation::Clear, 1.0f};
    Sequence.BeginRaster(Colors, &Depth, "Card recapture");
    Sequence.BindProgram(CaptureProgram);
    Sequence.BindGroup(0, CaptureGroup);
    Sequence.BindVertexRegion(0, VertexRegion);
    for (const uint32_t Index : CardIndices)
    {
        const SurfaceCard& Card = Cards[Index];
        const FacetSet&    Set  = Scene.FacetSets[Scene.Occupants[Card.Occupant].FacetSet];
        Sequence.SetViewport(static_cast<float>(Card.TexelOrigin[0]), static_cast<float>(Card.TexelOrigin[1]),
                             static_cast<float>(Card.TexelExtent[0]), static_cast<float>(Card.TexelExtent[1]), 0.0f, 1.0f);
        Sequence.SetScissor(Card.TexelOrigin[0], Card.TexelOrigin[1], Card.TexelExtent[0], Card.TexelExtent[1]);
        Sequence.Draw(Set.VertexCount, 1, Set.FirstVertex, Index);
    }
    Sequence.EndRaster();
    Exchange_.Submit(Sequence);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RESOURCES
//------------------------------------------------------------------------------------------------------------------------

void SurfaceCardDepot::ConstructResources(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& OccupantRegion,
                                          const std::shared_ptr<DeviceRegion>& MaterialRegion, const std::shared_ptr<DescriptorLayout>& DistanceLayout,
                                          const std::shared_ptr<DeviceRegion>& LuminaireRegion)
{
    const uint32_t Extent = Configuration.Extent;
    auto Construct = [&](const std::string& Label, TextureFormat Format, uint32_t Usage) {
        TextureSpecification Specification;
        Specification.Label  = Label;
        Specification.Width  = Extent;
        Specification.Height = Extent;
        Specification.Format = Format;
        Specification.Usage  = Usage;
        return Exchange_.ConstructTexture(Specification);
    };
    // 📝 Copy targets too: a recapture clears its rectangles with queue writes (RecaptureCards).
    const uint32_t Attachment = TextureUsage::RenderAttachment | TextureUsage::Sampled | TextureUsage::CopyTarget;
    AlbedoTexture        = Construct("Card albedo", TextureFormat::Rgba8UnormSrgb, Attachment);
    NormalTexture        = Construct("Card normal", TextureFormat::Rgba8Unorm, Attachment);
    IncandescenceTexture = Construct("Card incandescence", TextureFormat::Rgba16Float, Attachment);
    DepthTexture         = Construct("Card depth", TextureFormat::R16Float, Attachment);
    const uint32_t Stored = TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::CopyTarget;
    DirectTexture        = Construct("Card direct irradiance", TextureFormat::Rgba16Float, Stored);
    FinalTexture         = Construct("Card final radiance", TextureFormat::Rgba16Float, Stored);
    AlbedoView        = Exchange_.ConstructView(AlbedoTexture);
    NormalView        = Exchange_.ConstructView(NormalTexture);
    IncandescenceView = Exchange_.ConstructView(IncandescenceTexture);
    DepthView         = Exchange_.ConstructView(DepthTexture);
    DirectView        = Exchange_.ConstructView(DirectTexture);
    FinalView         = Exchange_.ConstructView(FinalTexture);
    SiteStride = Extent / 4;
    const std::vector<uint8_t> Profiles = EncodeCardProfiles();
    CardRegion = Exchange_.ConstructRegionFrom(Profiles.data(), Profiles.size(), "Card profiles");
    SiteRegion = Exchange_.ConstructRegion(static_cast<VkDeviceSize>(SiteStride) * SiteStride * 16, "Radiosity sites");
    // 📝 Every tile once, plus the warm-up list, which may repeat tiles the round-robin also picks this refresh.
    const uint64_t TileCapacity = std::max<uint64_t>(1, 2 * TileTotal);
    TileCapacity_ = TileCapacity;
    TileRegion = Exchange_.ConstructRegion(TileCapacity * 8, "Card tiles");
    WorkRegion = Exchange_.ConstructRegion(16, "Card work");

    using D = DescriptorCode;
    const std::vector<DescriptorSlot> DirectSlots = {{D::Uniform}, {D::StorageRead}, {D::StorageRead}, {D::StorageRead}, {D::Uniform},
                                                     {D::Texture2D}, {D::Texture2D}, {D::StorageTexture2D, TextureFormat::Rgba16Float}, {D::StorageRead}};
    DirectLayout = Exchange_.ConstructDescriptorLayout(DirectSlots, "Card direct");
    const std::vector<DescriptorSlot> RadiositySlots = {{D::Uniform}, {D::StorageRead}, {D::Uniform}, {D::Texture2D}, {D::Storage}};
    RadiosityLayout = Exchange_.ConstructDescriptorLayout(RadiositySlots, "Card radiosity");
    const std::vector<DescriptorSlot> FinalSlots = {{D::Uniform}, {D::StorageRead}, {D::StorageRead}, {D::Uniform}, {D::Texture2D}, {D::Texture2D},
                                                    {D::Texture2D}, {D::Texture2D}, {D::StorageRead}, {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    FinalLayout = Exchange_.ConstructDescriptorLayout(FinalSlots, "Card final");
    const std::vector<DescriptorSlot> SamplingSlots = {{D::StorageRead}, {D::StorageRead}, {D::Texture2D}, {D::Texture2D}, {D::Texture2D},
                                                       {D::Sampler}, {D::Texture2DLayered}, {D::Texture2DUnfiltered}};
    SamplingLayout = Exchange_.ConstructDescriptorLayout(SamplingSlots, "Card sampling");

    DirectGroup = Exchange_.ConstructDescriptorGroup(DirectLayout, {RefreshRegion, CardRegion, OccupantRegion, TileRegion, WorkRegion, NormalView,
                                                                    DepthView, DirectView, LuminaireRegion}, "Card direct");
    RadiosityGroup = Exchange_.ConstructDescriptorGroup(RadiosityLayout, {RefreshRegion, TileRegion, WorkRegion, NormalView, SiteRegion}, "Card radiosity");
    FinalGroup = Exchange_.ConstructDescriptorGroup(FinalLayout, {RefreshRegion, CardRegion, TileRegion, WorkRegion, AlbedoView, IncandescenceView,
                                                                  DirectView, DepthView, SiteRegion, FinalView}, "Card final");

    DirectProgram    = Shaders_.ConstructComputeProgram("CardDirect", "IntegrateCardDirect", {DirectLayout, DistanceLayout});
    RadiosityProgram = Shaders_.ConstructComputeProgram("CardRadiosity", "IntegrateCardRadiosity", {RadiosityLayout, DistanceLayout, SamplingLayout});
    FinalProgram     = Shaders_.ConstructComputeProgram("CardFinal", "IntegrateCardFinal", {FinalLayout});

    const std::vector<DescriptorSlot> CaptureSlots = {{D::StorageRead}, {D::StorageRead}, {D::StorageRead}};
    CaptureLayout = Exchange_.ConstructDescriptorLayout(CaptureSlots, "Card capture");
    CaptureGroup  = Exchange_.ConstructDescriptorGroup(CaptureLayout, {CardRegion, OccupantRegion, MaterialRegion}, "Card capture");
    RasterProgramSpecification Raster;
    Raster.VertexEntry   = "CaptureVertex";
    Raster.FragmentEntry = "CaptureFragment";
    Raster.VertexLayouts = {{VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {1, 12, VertexFormat::Uint32}, {2, 16, VertexFormat::Float32x3}}}};
    Raster.CullMode      = VK_CULL_MODE_NONE;
    Raster.DepthEnabled  = true;
    Raster.DepthFormat   = TextureFormat::Depth32Float;
    Raster.DepthWrite    = true;
    Raster.DepthCompare  = CompareFunction::Less;
    Raster.Targets       = {{TextureFormat::Rgba8UnormSrgb}, {TextureFormat::Rgba8Unorm}, {TextureFormat::Rgba16Float}, {TextureFormat::R16Float}};
    Raster.Layouts       = {CaptureLayout};
    CaptureProgram = Shaders_.ConstructRasterProgram("CardCapture", Raster);
}

std::shared_ptr<DescriptorGroup> SurfaceCardDepot::ConstructSamplingGroup(const std::shared_ptr<DeviceRegion>& OccupantRegion,
                                                                          const std::shared_ptr<DeviceTextureView>& SkyView,
                                                                          const std::shared_ptr<DeviceSampler>& SkySampler,
                                                                          const std::shared_ptr<DeviceTextureView>& MoonAtlas,
                                                                          const std::shared_ptr<DeviceTextureView>& Catalogue)
{
    return Exchange_.ConstructDescriptorGroup(SamplingLayout, {OccupantRegion, CardRegion, DepthView, FinalView, SkyView, SkySampler, MoonAtlas, Catalogue},
                                              "Card sampling");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CAPTURE
//------------------------------------------------------------------------------------------------------------------------

void SurfaceCardDepot::CaptureCards(const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion)
{
    const uint32_t       Extent = Configuration.Extent;
    TextureSpecification DepthSpecification;
    DepthSpecification.Label  = "Card capture depth";
    DepthSpecification.Width  = Extent;
    DepthSpecification.Height = Extent;
    DepthSpecification.Format = TextureFormat::Depth32Float;
    DepthSpecification.Usage  = TextureUsage::RenderAttachment;
    auto DepthAttachmentTexture = Exchange_.ConstructTexture(DepthSpecification);
    auto DepthAttachmentView    = Exchange_.ConstructView(DepthAttachmentTexture);
    CommandSequence Sequence = Exchange_.BeginSequence();
    const std::array<ColorAttachment, 4> Colors = {{{AlbedoView, LoadOperation::Clear, {0, 0, 0, 0}},
                                                    {NormalView, LoadOperation::Clear, {0.5f, 0.5f, 1.0f, 0.0f}},
                                                    {IncandescenceView, LoadOperation::Clear, {0, 0, 0, 0}},
                                                    {DepthView, LoadOperation::Clear, {4.0f, 0, 0, 0}}}};
    const DepthAttachment Depth{DepthAttachmentView, LoadOperation::Clear, 1.0f};
    Sequence.BeginRaster(Colors, &Depth, "Card capture");
    Sequence.BindProgram(CaptureProgram);
    Sequence.BindGroup(0, CaptureGroup);
    Sequence.BindVertexRegion(0, VertexRegion);
    for (uint32_t Index = 0; Index < Cards.size(); ++Index)
    {
        const SurfaceCard& Card = Cards[Index];
        const FacetSet&    Set  = Scene.FacetSets[Scene.Occupants[Card.Occupant].FacetSet];
        Sequence.SetViewport(static_cast<float>(Card.TexelOrigin[0]), static_cast<float>(Card.TexelOrigin[1]),
                             static_cast<float>(Card.TexelExtent[0]), static_cast<float>(Card.TexelExtent[1]), 0.0f, 1.0f);
        Sequence.SetScissor(Card.TexelOrigin[0], Card.TexelOrigin[1], Card.TexelExtent[0], Card.TexelExtent[1]);
        Sequence.Draw(Set.VertexCount, 1, Set.FirstVertex, Index);
    }
    Sequence.EndRaster();
    Exchange_.Submit(Sequence);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   LIGHTING SCHEDULE
//------------------------------------------------------------------------------------------------------------------------

void SurfaceCardDepot::InvalidateLighting()
{
    const std::vector<float> Zeros(static_cast<size_t>(SiteStride) * SiteStride * 4, 0.0f);
    Exchange_.WriteRegion(SiteRegion, 0, Zeros.data(), Zeros.size() * 4);
    WarmRefreshes = std::max(WarmRefreshes, 3u);
}

void SurfaceCardDepot::ScheduleTiles(uint32_t TileBudget)
{
    std::vector<CardTile> Scheduled = DynamicTiles;
    if (WarmRefreshes > 0)
    {
        Scheduled.insert(Scheduled.end(), StaticTiles.begin(), StaticTiles.end());
        WarmRefreshes--;
    }
    else
    {
        // 📝 A tile must not be scheduled twice in one refresh: the radiosity dispatch folds into its sites in place.
        const std::vector<CardTile> Priority = PriorityRefreshes > 0 ? PriorityTiles : std::vector<CardTile>{};
        std::unordered_set<uint64_t> Claimed;
        for (const CardTile& Tile : Priority) Claimed.insert(static_cast<uint64_t>(Tile[0]) * 4194304ull + Tile[1]);
        Scheduled.insert(Scheduled.end(), Priority.begin(), Priority.end());
        if (PriorityRefreshes > 0) PriorityRefreshes--;
        const uint32_t Budget = std::min<uint32_t>(static_cast<uint32_t>(StaticTiles.size()), TileBudget);
        for (uint32_t Index = 0; Index < Budget; ++Index)
        {
            const CardTile Tile = StaticTiles[Cursor];
            Cursor = (Cursor + 1) % static_cast<uint32_t>(StaticTiles.size());
            if (!Claimed.count(static_cast<uint64_t>(Tile[0]) * 4194304ull + Tile[1])) Scheduled.push_back(Tile);
        }
    }
    ScheduledTiles = static_cast<uint32_t>(Scheduled.size());
    if (!Scheduled.empty()) Exchange_.WriteRegion(TileRegion, 0, Scheduled.data(), Scheduled.size() * sizeof(CardTile));
    uint8_t Work[16] = {};
    const uint32_t Counts[2]  = {ScheduledTiles, SiteStride};
    const float    Weights[2] = {0.15f, 0.35f};
    std::memcpy(Work, Counts, 8);
    std::memcpy(Work + 8, Weights, 8);
    Exchange_.WriteRegion(WorkRegion, 0, Work, sizeof(Work));
}

void SurfaceCardDepot::RecordLighting(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                                      const std::shared_ptr<DescriptorGroup>& SamplingGroup, bool RadiosityEnabled)
{
    if (ScheduledTiles == 0) return;
    const uint32_t GroupsX = std::min(ScheduledTiles, 65535u);
    const uint32_t GroupsY = (ScheduledTiles + 65534u) / 65535u;
    Sequence.BindProgram(DirectProgram);
    Sequence.BindGroup(0, DirectGroup);
    Sequence.BindGroup(1, DistanceGroup);
    Sequence.Dispatch(GroupsX, GroupsY);
    if (RadiosityEnabled)
    {
        Sequence.BindProgram(RadiosityProgram);
        Sequence.BindGroup(0, RadiosityGroup);
        Sequence.BindGroup(1, DistanceGroup);
        Sequence.BindGroup(2, SamplingGroup);
        Sequence.Dispatch(GroupsX, GroupsY);
    }
    Sequence.BindProgram(FinalProgram);
    Sequence.BindGroup(0, FinalGroup);
    Sequence.Dispatch(GroupsX, GroupsY);
}

} // namespace Frontier::GlobalIllumination
