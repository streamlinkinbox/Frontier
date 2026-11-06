/*==========================================================================================================================================
                                                     PLUMEVIEWPORTPRESENTATION.H
==========================================================================================================================================*/
// 🧩 What the Smoke & fire editor asks of the host that owns the GPU, and what it gets back: the gas it edits, the test emitter and the camera
//    go in; the presented image and the solver's readout come out. The plume's lattices live on the host's device; the editor never touches them.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace Frontier
{

namespace FluidDynamics
{
struct GasParameters;
}

// 📝 What the running plume tells the editor (the host fills it from its SmokeSequence each refresh). Without one the editor reads "no solver": it
//    never shows counters nothing is counting.
struct FluidPlumeReadout
{
    uint32_t Grade       = 2;       // [-] the grade the plume is built at: the Render settings' quality tier (0 low, 1 medium, 2 high)
    bool     Resident    = false;   // [-] the lattices hold state
    uint32_t Sources     = 0;       // [-] emitters this refresh
    uint32_t Ticks       = 0;       // [-] transport steps taken
    uint32_t Relocations = 0;       // [-] moves of the toroidal window
};

// 📝 One refresh of the viewport. The editor owns the gas, the emitter and the camera; the host owns the solver, so it is the host that advances the
//    plume by Seconds, empties it when Clear is set and marches it from the camera into an image of Extent.
struct PlumeViewportRequest
{
    const FluidDynamics::GasParameters* Gas         = nullptr;
    bool                                Enabled     = true;    // [-] the record's switch
    uint32_t                            TestEmitter = 0;       // [-] 0 off, 1 smoke plume, 2 fire
    double                              Seconds     = 0.0;     // [s] interface time since the last refresh; 0 while the editor is paused
    bool                                Clear       = false;   // [-] empty the lattices before this refresh
    std::array<uint32_t, 2>             Extent{};              // [px] the stage
    double                              Yaw         = 0.0;     // [rad] the orbit about the emitter
    double                              Pitch       = 0.0;     // [rad]
    double                              Distance    = 0.0;     // [m]
};

struct PlumeViewportImage
{
    uint64_t                Texture = 0;   // ImTextureID; 0 = no image
    std::array<uint32_t, 2> Extent{};
    std::string             Adapter;
    std::string             Failure;
    double                  RefreshMilliseconds = 0.0;   // [ms] the host's time for the refresh: its transport step and the march
    FluidPlumeReadout       Readout{};
    bool                    Solving = false;             // [-] a solver stands behind Readout
};

using PlumeViewportPresenter = std::function<PlumeViewportImage(const PlumeViewportRequest&)>;

} // namespace Frontier
