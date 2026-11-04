/*==========================================================================================================================================
                                                        FLUIDPLUMEEDITOR.H
==========================================================================================================================================*/
// 🧩 Standalone Eulerian fluid plume & blackbody combustion authoring editor bound to FluidPlumeInspector.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordToken.h"

namespace Frontier
{

#if FRONTIER_EDITOR

class RecordInspector;

class FluidPlumeEditor
{
  public:
    static void ConstructInterface(RecordToken Token, RecordInspector& Inspector);
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
