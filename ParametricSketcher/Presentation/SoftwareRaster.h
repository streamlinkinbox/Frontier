//============================================================================================================================================
// 📦 ParametricSketcher/Presentation/SoftwareRaster.h — CPU implementation of RasterExchange: executes the Slang shader bodies per fragment
//============================================================================================================================================
// A small but faithful rasteriser: clip-space triangles with near-plane clipping, perspective-correct interpolation,
//    top-left fill rule, float depth (0..1), 4-tap supersampled coverage for the analytic shaders, straight-alpha
//    blending, a uint32 pick target and a fragment tally. Line and point draws go through the same triangle path
//    because their .slang vertex stages already expand them to quads — so what you see here IS what the GPU draws.
#pragma once

#include "RasterExchange.h"
#include <memory>

namespace Frontier
{

class SoftwareRaster final : public RasterExchange
{
public:
    SoftwareRaster(uint32_t Width, uint32_t Height) noexcept;
    ~SoftwareRaster() override;

    void     Resize(uint32_t Width, uint32_t Height) noexcept override;
    uint32_t Width() const noexcept override;
    uint32_t Height() const noexcept override;

    void BeginTarget(const float ClearColour[4]) noexcept override;
    void BindView(const ViewRecord& View) noexcept override;
    void DrawGrid() noexcept override;
    void DrawSurface(const SurfaceStream& Stream, const DrawRecord& Draw) noexcept override;
    void DrawSegments(const SegmentStream& Stream, const DrawRecord& Draw) noexcept override;
    void DrawPoints(const PointStream& Stream, const DrawRecord& Draw) noexcept override;
    void BeginOverlay() noexcept override;
    void EndTarget() noexcept override;

    RasterImage Readback() const noexcept override;
    uint32_t    Pick(uint32_t X, uint32_t Y) const noexcept override;
    float       Depth(uint32_t X, uint32_t Y) const noexcept override;
    Tally       QueryTally() const noexcept override;

private:
    struct Detail;
    std::unique_ptr<Detail> Self;
};

} // namespace Frontier
