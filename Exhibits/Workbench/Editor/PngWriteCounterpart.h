// Moved to Engine/ContentInterchange/PngWriteCounterpart.h — the app's material-preview TU
//    (Engine/ContentInterchange/ShaderballPreview.cpp) writes its PNG with it, so it belongs to the engine, not the
//    workbench. This forwarder keeps the workbench harnesses' `-I Exhibits/Workbench/Editor` builds working
//    unchanged; new code should include the engine path.
#pragma once
#include "../../../Engine/ContentInterchange/PngWriteCounterpart.h"
