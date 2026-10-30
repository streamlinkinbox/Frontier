/*==========================================================================================================================================
                                                    COMPLETE FRAME DECLARATION
                                     ------------------------------------------------------------------
                 One whole frame: surface raster -> indirect prelude -> ReSTIR chain -> SVGF ->
                      environment -> history write-back -> display present, in a single
                           declaration the ordering/atlas/barrier machinery resolves.
==========================================================================================================================================*/

// 🧩 Complete frame: merges ViewportUnits' surface/present bookends with IlluminationUnits and
//    EnvironmentUnits, closing every loop the partial declarations left as imports:
//      * SurfaceRaster writes SurfaceAttributes + SurfaceDepth in-frame (no longer imports);
//      * LightingHistoryUpdate writes the filtered result back for next frame's VarianceTemporal;
//      * DisplayPresent composites FilteredLighting with EnvironmentRadiance by depth.
//    The one remaining import is ReservoirsPrevious — correct by construction: it is last frame's
//    ReservoirsSpatial under the host ping-pong swap, so nothing in-frame may write it.
//    This is the declaration the SDK phase records; ReferenceRenderer executes its mathematics on CPU.

#pragma once

#include "EnvironmentUnits.h"
#include "IlluminationUnits.h"
#include "RenderTarget.h"
#include "SubmissionOrder.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

struct CompleteFrameDeclaration
{
    std::vector<SubmissionUnitDeclaration> Units;
    std::vector<RenderTargetDeclaration>   Targets;
};

struct CompleteFrameOptions
{
    bool IncludeInspection = false;   // appends the editor-only inspection unit when enabled
};

[[nodiscard]] CompleteFrameDeclaration DeclareCompleteFrame(uint32_t Width, uint32_t Height,
                                                            CompleteFrameOptions Options = {});

} // namespace Frontier
