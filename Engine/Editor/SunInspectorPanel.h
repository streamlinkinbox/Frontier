#pragma once
#include "EditorInstance.h"
namespace Frontier {
class ControlPanel;
// Startup only. ImGui owns the fonts; repeated calls reuse the current context's faces.
void PrepareSunInspectorFonts() noexcept;
// Called inside the real InspectorPanel's scrolling property child. No project includes.
void RecordSunInspector(ControlPanel& Controls, EditorInstance& Row, EditorSheet& Sheet) noexcept;
}
