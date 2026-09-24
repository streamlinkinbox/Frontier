#pragma once
#include "EditorInstance.h"
#include <cstdint>
struct ImTextureData;
namespace Frontier {
class ControlPanel;
void RecordLensFlareInspector(ControlPanel&,EditorInstance&,EditorSheet&) noexcept;
// Authoring-resource inspection/export. Context-owned; no project or GPU ownership crosses this API.
struct LensFlarePreviewInfo { const ImTextureData* Texture=nullptr; uint64_t Revision=0, BakedRevision=0; int Width=0,Height=0; };
LensFlarePreviewInfo QueryLensFlarePreview() noexcept;
bool ExportLensFlarePreview(const char* Path) noexcept;
}
