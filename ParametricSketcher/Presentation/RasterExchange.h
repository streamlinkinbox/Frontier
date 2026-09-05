//============================================================================================================================================
// 📦 ParametricSketcher/Presentation/RasterExchange.h — The device seam: Vulkan-shaped drawing verbs implemented by software and GPU rasters
//============================================================================================================================================
// Everything above this line (ScenePresentation, gizmo, overlays, console `render`) speaks only this vocabulary.
//    SoftwareRaster implements it on the CPU and writes PNG proofs; VulkanRaster implements it with real pipelines.
//    The verbs are deliberately the ones a Vulkan command buffer records: begin a target, bind a view, issue draws
//    from typed vertex streams with a per-draw record, switch to the overlay (depth-test-off) segment, end, read back.
//
// Vertex streams are float32 and tightly packed exactly as the .slang vertex stages consume them, so the GPU path
//    uploads them verbatim.
#pragma once

#include "Kernel/VectorSpecification.h"
#include <cstdint>
#include <vector>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  RECORDS (mirror ShadingRecords.slang)
//------------------------------------------------------------------------------------------------------------------------

struct ViewRecord
{
    float ViewClip[16];                                                                 // [-] column-major
    float ClipView[16];                                                                 // [-]
    float EyePosition[4];                                                               // [m] xyz, w = 1 perspective / 0 ortho
    float Viewport[4];                                                                  // [px] xy size, zw reciprocal
    float GridStyle[4];                                                                 // x minor cell [m], y major every N, z fade radius [m], w half-width [px]
    float Illumination[4];                                                              // xyz key light dir, w ambient
    float PixelAngle = 0.0f;                                                            // [rad] per pixel (perspective)
    float PixelWorld = 0.0f;                                                            // [m] per pixel (orthographic)
};

struct DrawRecord
{
    float    ModelWorld[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };                   // [-]
    float    Tint[4]        = { 0.7f, 0.72f, 0.76f, 1.0f };                             // [-] rgb, a
    float    Highlight      = 0.0f;                                                     // [-] 0 none, 1 hover, 2 selected
    uint32_t PickIdentity   = 0;                                                        // [-] 0 = not pickable
    float    LineWidth      = 1.5f;                                                     // [px]
    float    PointSize      = 7.0f;                                                     // [px]
    bool     Dashed         = false;                                                    // [-] construction geometry
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  VERTEX STREAMS
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceStream                                                                    // SurfaceRaster.slang
{
    std::vector<float>    Positions;                                                    // [m] xyz
    std::vector<float>    Normals;                                                      // [-] xyz
    std::vector<float>    Parameters;                                                   // [-] uv
    std::vector<uint32_t> Triangles;                                                    // [-] CCW seen from +normal
    [[nodiscard]] uint32_t VertexCount() const noexcept { return static_cast<uint32_t>(Positions.size() / 3); }
};

struct SegmentStream                                                                    // LineRaster.slang — pairs of xyz
{
    std::vector<float> Endpoints;                                                       // [m] A.xyz B.xyz per segment
    [[nodiscard]] uint32_t SegmentCount() const noexcept { return static_cast<uint32_t>(Endpoints.size() / 6); }
    void Append(Vec3 A, Vec3 B) noexcept
    {
        const float Values[6] = { float(A.X), float(A.Y), float(A.Z), float(B.X), float(B.Y), float(B.Z) };
        for (float F : Values) Endpoints.push_back(F);
    }
    void AppendPolyline(const std::vector<Vec3>& Points, bool Closed = false) noexcept
    {
        for (size_t I = 0; I + 1 < Points.size(); ++I) Append(Points[I], Points[I + 1]);
        if (Closed && Points.size() > 2) Append(Points.back(), Points.front());
    }
};

enum class PointGlyph : uint8_t { Disc = 0, Square = 1, Diamond = 2, Cross = 3, Ring = 4 };

struct PointStream                                                                      // PointRaster.slang — xyz + glyph
{
    std::vector<float> Points;                                                          // [m] xyz, glyph per point
    [[nodiscard]] uint32_t PointCount() const noexcept { return static_cast<uint32_t>(Points.size() / 4); }
    void Append(Vec3 P, PointGlyph Glyph) noexcept
    {
        const float Values[4] = { float(P.X), float(P.Y), float(P.Z), float(static_cast<int>(Glyph)) };
        for (float F : Values) Points.push_back(F);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  RASTER EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

struct RasterImage
{
    uint32_t             Width = 0;                                                     // [px]
    uint32_t             Height = 0;                                                    // [px]
    std::vector<uint8_t> Pixels;                                                        // [-] RGBA8, row-major, top row first
};

class RasterExchange
{
public:
    virtual ~RasterExchange() = default;

    virtual void     Resize(uint32_t Width, uint32_t Height) noexcept = 0;
    virtual uint32_t Width() const noexcept = 0;
    virtual uint32_t Height() const noexcept = 0;

    // One target per frame: clear, bind view, record draws in the main (depth-tested) segment, then overlay, then end.
    virtual void BeginTarget(const float ClearColour[4]) noexcept = 0;
    virtual void BindView(const ViewRecord& View) noexcept = 0;
    virtual void DrawGrid() noexcept = 0;
    virtual void DrawSurface(const SurfaceStream& Stream, const DrawRecord& Draw) noexcept = 0;
    virtual void DrawSegments(const SegmentStream& Stream, const DrawRecord& Draw) noexcept = 0;
    virtual void DrawPoints(const PointStream& Stream, const DrawRecord& Draw) noexcept = 0;
    virtual void BeginOverlay() noexcept = 0;                                           // depth test off, depth write off
    virtual void EndTarget() noexcept = 0;

    // Readback. Pick returns the PickIdentity under a pixel (0 = nothing).
    virtual RasterImage Readback() const noexcept = 0;
    virtual uint32_t    Pick(uint32_t X, uint32_t Y) const noexcept = 0;
    virtual float       Depth(uint32_t X, uint32_t Y) const noexcept = 0;               // [0..1] clip depth, 1 = far

    // Diagnostics for the console.
    struct Tally
    {
        uint32_t Triangles = 0, Segments = 0, Points = 0, Fragments = 0, DepthRejected = 0, BackFacing = 0;
    };
    virtual Tally QueryTally() const noexcept = 0;
};

// PNG writer shared by every backend's proof output (stored-deflate, no dependencies). Returns false on I/O failure.
[[nodiscard]] bool WritePng(const std::string& Path, const RasterImage& Image) noexcept;

} // namespace Frontier
