#pragma once
#include <imgui.h>
#include "../DisplayPresentation/IconArt.h"
namespace Frontier
{
class ControlPanel; struct EditorInstance; struct EditorSheet;

// The host supplies the engine's own rasterized glyph for a light's header icon. LightSpecification.js
//    reuses the shipped icon library rather than authoring a second light glyph set, and so does the panel.
extern ImTextureID (*LightGlyphSource)(IconSymbol, float);

void RecordLightInspector(ControlPanel&, EditorInstance&, EditorSheet&);
}
