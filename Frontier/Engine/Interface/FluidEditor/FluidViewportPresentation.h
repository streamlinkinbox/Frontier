/*==========================================================================================================================================
                                                       FLUIDVIEWPORTPRESENTATION.H
==========================================================================================================================================*/
// 🧩 What the Fluid editor's viewport asks of a host and what it gets back. The host owns the Vulkan device and the solver; the editor owns the volume's
//    settings, the camera and the verbs, and describes one refresh in a request: the settings, the time to run, the steps and bursts the person raised,
//    the brush, the camera and the size of the picture. A host that has no solver leaves the presenter empty, and the window says so.

#pragma once

#include "Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h"
#include "Frontier/Engine/VolumetricDynamics/PyroCamera.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Frontier
{

// 📝 What the running solver says about itself.
struct FluidVolumeReadout
{
    uint32_t Resolution        = 0;     // [cells] an edge of the lattice
    uint32_t Steps             = 0;     // [-] transport steps taken since the last clear
    double   SimulationSeconds = 0.0;   // [s] simulated time since the last clear
    float    BoundsWidth       = 0.0f;  // [m] the domain's width now, with the surge a burst gives it
    float    BoundsHeight      = 0.0f;  // [m]
    uint32_t PendingBursts     = 0;     // [-] bursts waiting for their time
};

// 📝 A burst the person asked for: one of the prototype's patterns (burst, shrapnel, salvo, collide, mushroom) at its own places, or, with AtPoint, one
//    burst at a point of the volume (the Place burst tool's click). The editor turns the pointer into that point itself, as the prototype's panel does.
struct FluidBurstRequest
{
    std::string          Pattern  = "burst";
    bool                 AtPoint  = false;
    std::array<float, 3> PointUvw = { 0.5f, 0.18f, 0.5f };   // [-] a point of the volume, each axis 0 to 1
};

// 📝 The source brush: a flame laid into the volume where the pointer is, moving with it (Engine.setBrush).
struct FluidBrushRequest
{
    bool                 Held        = false;
    std::array<float, 3> PositionUvw = { 0.5f, 0.25f, 0.5f };   // [-] a point of the volume, each axis 0 to 1
    std::array<float, 3> Velocity    = { 0.0f, 3.0f, 0.0f };    // [-] the brush's velocity in the solver's units
};

struct FluidViewportRequest
{
    const GasSettings*             Settings = nullptr;   // [-] the record's, borrowed for the refresh
    bool                           Enabled  = true;      // [-] a disabled volume stands still: it is drawn but not stepped
    double                         Seconds  = 0.0;       // [s] interface time to simulate; zero while paused
    uint32_t                       Steps    = 0;         // [-] single steps of 1/60 s the person asked for (taken while paused too)
    bool                           Clear    = false;     // [-] empty the lattice and restart the clock
    std::vector<FluidBurstRequest> Bursts;
    FluidBrushRequest              Brush;
    PyroOrbitCamera                Camera;
    std::array<uint32_t, 2>        Extent{};             // [px] the size of the picture the viewport shows
};

struct FluidViewportImage
{
    uint64_t                Texture = 0;               // [-] the interface renderer's handle of the picture; zero when there is none
    std::array<uint32_t, 2> Extent{};                  // [px]
    std::string             Adapter;                   // [-] the device that solved
    std::string             Failure;                   // [-] why there is no picture
    bool                    Solving = false;           // [-] a solver stands behind the picture
    double                  RefreshMilliseconds = 0.0; // [ms] the last refresh, as the host measured it
    FluidVolumeReadout      Readout{};
};

using FluidViewportPresenter = std::function<FluidViewportImage(const FluidViewportRequest&)>;

} // namespace Frontier
