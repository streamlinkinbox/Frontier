/*==========================================================================================================================================
                                                         PARTICLEEDITOR.H
==========================================================================================================================================*/
// 🧩 Standalone GPU particle dynamics authoring editor bound to ParticleInspector.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordToken.h"

namespace Frontier
{

#if FRONTIER_EDITOR

class RecordInspector;

class ParticleEditor
{
  public:
    static void ConstructInterface(RecordToken Token, RecordInspector& Inspector);
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
