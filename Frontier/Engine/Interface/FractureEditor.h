/*==========================================================================================================================================
                                                         FRACTUREEDITOR.H
==========================================================================================================================================*/
// 🧩 Standalone Voronoi convex decomposition authoring editor bound to FractureInspector.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordToken.h"

namespace Frontier
{

#if FRONTIER_EDITOR

class RecordInspector;

class FractureEditor
{
  public:
    static void ConstructInterface(RecordToken Token, RecordInspector& Inspector);
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
