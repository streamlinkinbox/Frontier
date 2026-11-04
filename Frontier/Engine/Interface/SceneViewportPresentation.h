/*==========================================================================================================================================
                                                     SCENEVIEWPORTPRESENTATION.H
==========================================================================================================================================*/
// 🧩 The contract between a workspace's Scene Viewport and the host that renders it: the request (stage size, camera, pick), the
//    renderer's readout and the presented image. The Rendering extension answers it; the editor workspace asks.

#pragma once

#include "Engine/Scene/CollisionSurface.h"
#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/Scene/EnvironmentDiagnostics.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace Frontier
{

// 📝 What the Scene Viewport asks the host application to render and present each interface tick; the host owns the device
//    and the renderer, the workspace owns the camera (orbit, pan, dolly, fly) and the level records the host reads.
struct SceneViewportRequest
{
    float                 PanelWidth          = 0.0f;                 // [px] interface pixels
    float                 PanelHeight         = 0.0f;                 // [px]
    float                 PixelDensity        = 1.0f;                 // [-] framebuffer pixels per interface pixel
    std::array<double, 3> CameraPosition      = {0.0, 0.0, 0.0};      // [m] z-up world of the scene
    std::array<double, 3> CameraTarget        = {1.0, 0.0, 0.0};      // [m]
    double                VerticalFieldOfView = 1.0471975511965976;   // [rad]
    double                DeltaSeconds        = 0.0;                  // [s]
    std::array<double,3> CameraUp = {0,0,1};
    // 📝 GPU picking: a click on the stage asks for the record drawn under it in the image the click was made on; the readout's
    //    Pick answers the serial one interface tick later.
    uint64_t              PickSerial          = 0;                    // [-] advances with every click; 0 = nothing asked
    std::array<double, 2> PickPoint           = {0.0, 0.0};           // [-] fraction of the stage's width and height, from the top-left
};

// 📝 The answer to the stage's last click (GPU picking, PickProjection): the Geometry record drawn at the clicked pixel, or the
//    nearest one drawn within the pick's reach around it.
struct SceneViewportPick
{
    uint64_t                Serial       = 0;              // [-] the request's PickSerial this answers; 0 = none yet
    std::optional<uint64_t> Record{};                      // [-] record token value; empty: the sky or nothing a record owns
    uint32_t                Occupant     = 0xFFFFFFFFu;    // [-] scene occupant at the clicked pixel (0xFFFFFFFF: none)
    double                  Milliseconds = 0.0;            // [ms] the pick's round trip on the device
    std::string             Failure{};                     // [-] why nothing could be picked
};

// 📝 What the host's renderer reports for the viewport bar, readout and status strip (the demo's PresentMeasurements).
struct SceneViewportReadout
{
    std::shared_ptr<const EnvironmentRenderReadout> Environment;
    bool                    Presenting          = false;     // [-] an image is live
    std::string             Adapter{};                       // [-] GPU name
    std::string             Quality{};                       // [-] low / medium / high
    std::string             RendererTag{};                   // [-] "RTX ReSTIR PT (3B GI · 2B Refl · M=2)" or "GTX SDF GI"
    std::string             AccelerationLine{};              // [-] "BVH hierarchy   TLAS …" (RTX) or empty
    std::array<uint32_t, 2> ScreenExtent        = {0, 0};    // [px]
    std::array<uint32_t, 2> TraceExtent         = {0, 0};    // [px]
    double                  RefreshMilliseconds = 0.0;       // [ms] smoothed refresh duration
    double                  LatestMilliseconds  = 0.0;       // [ms] the last refresh alone
    bool                    Held                = false;     // [-] live refresh is off: the image is the last one, not refreshed
    std::string             Failure{};                       // [-] why there is no image, if it failed
    bool                    RayTracing          = false;     // [-] RTX ReSTIR path tracer, else GTX distance-field GI
    uint32_t                TlasInstances       = 0;         // [-] active software TLAS instances
    uint32_t                ClipResolution      = 0;         // [voxels] distance clipmap edge
    uint32_t                SurfaceCards        = 0;         // [-] surface cards
    uint32_t                SceneTriangles      = 0;
    uint32_t                GeometryClusters    = 0;         // [-] geometry cluster candidates
    // 📝 What each Geometry record resolved to against the loaded scene archive (record token value → resolution) and a
    //    counter that advances whenever any of them changed; the workspace copies them into the records for their inspector.
    std::shared_ptr<const std::unordered_map<uint64_t, GeometryConfiguration::Resolution>> Geometry{};
    uint64_t                GeometryGeneration  = 0;
    // 📝 The collision facets of every resolved Geometry record (world-space triangles), rebuilt with the resolutions; the
    //    workspace hands them to the Physics extension so colliders match the drawn surfaces, and draws them as a wireframe.
    std::shared_ptr<const CollisionSurfaceArchive> CollisionSurfaces{};
    SceneViewportPick       Pick{};                          // [-] the answer to the last click on the stage
};

struct SceneViewportImage
{
    uint64_t             Texture = 0;   // [-] ImTextureID of the presented image; 0 while unavailable
    std::string          Status{};      // [-] renderer, extent and refresh time, or the bring-up / failure line
    SceneViewportReadout Readout{};     // [-]
};

} // namespace Frontier
