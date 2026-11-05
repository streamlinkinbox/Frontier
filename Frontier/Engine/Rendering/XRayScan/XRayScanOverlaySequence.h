/*==========================================================================================================================================
                                                     XRAYSCANOVERLAYSEQUENCE.H
==========================================================================================================================================*/
// 🧩 The scan's display-referred layers on the device exchange: the ghost shell, the wireframe, the curtain and the particles drawn over a
//    tone-mapped picture, in the order the reference draws them. The host half of ScanOverlay.wgsl / GarageView.js (RecordOverlay); the shaders
//    are the XRayScan archive set (Frontier/Engine/Shaders/XRayScan).

#pragma once

#include "XRayScanEdges.h"
#include "XRayScanProfile.h"
#include "XRayScanSequence.h"

#include "Frontier/Engine/DeviceExchange/DeviceExchange.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/ShaderInterchange.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Frontier::XRayScan
{

// 📝 One draw of the ghost shell: a facet set's vertices for InstanceCount consecutive occupants starting at FirstInstance (the scene's draw runs,
//    restricted to the scanned occupants; the facet set's vertex range supplies the first two).
struct ScanShellRun
{
    uint32_t FirstVertex   = 0;   // [-]
    uint32_t VertexCount   = 0;   // [-]
    uint32_t FirstInstance = 0;   // [-]
    uint32_t InstanceCount = 0;   // [-]
};

// 📝 The six resources of the surface raster's descriptor group, in its slot order: the refresh constants, the occupant profiles, the tyre marks
//    and their sampler, the effect profile and the vehicle profile. The overlay reads the same group, so a scanned car deforms as the solid one.
struct ScanRasterResources
{
    DescriptorResource Refresh{};
    DescriptorResource Occupants{};
    DescriptorResource TyreMarks{};
    DescriptorResource TyreMarkSampler{};
    DescriptorResource Effect{};
    DescriptorResource Vehicle{};
};

// 📝 Statistics of the last recording (the draws it issued), for captures and tests.
struct ScanOverlayCounts
{
    uint32_t ShellDraws = 0;
    uint32_t EdgeDraws  = 0;
    uint32_t CurtainDraws = 0;
    uint32_t SparkleDraws = 0;
};

class XRayScanOverlaySequence
{
public:
    XRayScanOverlaySequence(DeviceExchange& Exchange, GlobalIllumination::ShaderInterchange& Shaders);

    // 📝 The layout, the eight programs and the scan's own regions. VertexRegion holds the facet vertices (VertexStride bytes each) and
    //    InstanceRegion the occupant index of every instance (the surface raster's instance-rate stream). Returns false when a program fails.
    bool Construct(TextureFormat TargetFormat, const ScanRasterResources& Raster, const std::shared_ptr<DeviceRegion>& VertexRegion,
                   const std::shared_ptr<DeviceRegion>& InstanceRegion, const ScanEdgeRecords& Edges, std::span<const float> Sparkles);

    // 📝 The picture's raster depth the layers are occluded by: reversed, device depth = near / view depth. Bound again whenever it is rebuilt.
    void BindDepth(const std::shared_ptr<DeviceTextureView>& DepthView);

    // 📝 EncodeScanRecord and the view fields: queued for the next submission.
    void Update(const XRayScanSequence& Scan, const ScanStage& Stage, std::span<const uint32_t> ScannedOccupants, double CameraNear, double HudLineTop = 0.0);

    // 📝 RecordOverlay without the stage veils: a raster scope over ColourView (loaded, not cleared) drawing what the clock shows.
    void Record(CommandSequence& Sequence, const std::shared_ptr<DeviceTextureView>& ColourView, const XRayScanSequence& Scan, std::span<const ScanShellRun> Shell);

    [[nodiscard]] const ScanOverlayCounts& QueryCounts() const noexcept { return Counts_; }
    [[nodiscard]] const std::string&       QueryDiagnostic() const noexcept { return Diagnostic_; }

private:
    DeviceExchange&                         Exchange_;
    GlobalIllumination::ShaderInterchange&  Shaders_;
    std::shared_ptr<DescriptorLayout>       Layout_{};
    std::shared_ptr<DescriptorGroup>        Group_{};
    ScanRasterResources                     Raster_{};
    std::shared_ptr<DeviceRegion>           ProfileRegion_{}, ViewRegion_{}, EdgeRegion_{}, SparkleRegion_{}, VertexRegion_{}, InstanceRegion_{};
    std::shared_ptr<DeviceProgram>          Ghost_{}, Edge_{}, Curtain_{}, Sparkle_{};
    uint32_t                                EdgeCount_    = 0;
    uint32_t                                SparkleCount_ = 0;
    ScanOverlayCounts                       Counts_{};
    std::string                             Diagnostic_{};
};

} // namespace Frontier::XRayScan
