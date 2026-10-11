// Moved to Engine/Shaders/SlangCpuShim.h — the shim is what lets engine shader text compile as C++, and the app's
//    material-preview TU (Engine/ContentInterchange/ShaderballPreview.cpp) needs it, so it belongs to the engine,
//    not the workbench. This forwarder keeps the workbench harnesses' `-I Exhibits/Workbench/Materials` builds
//    working unchanged; new code should include the engine path.
#pragma once
#include "../../../Engine/Shaders/SlangCpuShim.h"
