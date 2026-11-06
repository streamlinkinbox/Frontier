/*==========================================================================================================================================
                                                         PYROSOLVERSESSION.H
==========================================================================================================================================*/
// 🧩 The Fluid editor's solver on its own Vulkan device: the Pyro volume solver (Experimental/Fluid's engine-webgl2.js on Vulkan 1.1) brought up once,
//    stepped, burst and brushed as one request says, and its picture read back top-down as RGBA8. Nothing of the solver's or the context's headers
//    leaks out of this file, so a host that presents the picture on another device (FluidVolumeExtension) never sees them.

#pragma once

#include "Frontier/Engine/Interface/FluidEditor/FluidViewportPresentation.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Frontier
{

class PyroSolverSession
{
  public:
    PyroSolverSession();
    ~PyroSolverSession();

    PyroSolverSession(const PyroSolverSession&)            = delete;
    PyroSolverSession& operator=(const PyroSolverSession&) = delete;

    // 📝 One refresh of the request (FluidPanel.js Frame): a restart empties the lattice; a new grid rebuilds it; the brush and the bursts are handed over;
    //    the single steps, then the interface time (a disabled volume takes neither); the picture is drawn from the camera at the grade's render scale
    //    and read back. False, with Failure, when there is no device or the programs would not build.
    bool Refresh(const FluidViewportRequest& Request, std::vector<uint8_t>& Rgba, std::array<uint32_t, 2>& Picture, FluidVolumeReadout& Readout, std::string& Failure);

    // 📝 The device that solves ("Vulkan · <adapter>"); empty before the first refresh.
    [[nodiscard]] const std::string& QueryAdapter() const noexcept;

  private:
    struct Implementation;
    std::unique_ptr<Implementation> Parts;
};

} // namespace Frontier
