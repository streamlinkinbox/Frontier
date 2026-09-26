//============================================================================================================================================
//                                                     SHADERBALLPREVIEW.H
//============================================================================================================================================
// 🖼️ M7b: single-ball CPU preview of one authored material. The entry point (implemented in
//    Exhibits/Workbench/Materials/ShaderballExhibit.cpp under SHADERBALL_PREVIEW_LIB) reuses the exhibit's own
//    stage rig, camera, integrator, and PNG writer — the preview IS the exhibit with ball 0 mapped from the
//    descriptor instead of a film record — so preview pixels are comparable with the Gallery sheets by construction
//    (same tables, same rig, same ACES + γ2.2 encode).
//
//    Fidelity limits (documented, not bugs):
//      · constants only — bound textures are ignored (the CPU tracer has no texture pipeline);
//      · cutout is not previewed (the ShadingRecord carries no alpha; the ball renders opaque);
//      · single slab — Slabs[0]; multi-slab stacks preview their top authoring slab;
//      · fixed rig — the standard 3-softbox stage, exposure 1 (the tungsten backlight is SSS-sheet-only).

#pragma once

#include "MaterialDescriptor.h"
#include "MaterialIndex.h"
#include <cstdint>

namespace Frontier {

struct ShaderballPreviewRequest
{
    const MaterialDescriptor* Material  = nullptr;   // Slabs must be non-empty; Slabs[0] is previewed
    MaterialReflectance       Selection = MaterialReflectance::Standard;
    int                       Size      = 192;       // [px] square film
    int                       Spp       = 8;         // [spp]
    const char*               MeshPath  = "Exhibits/Gallery/Materials/shaderball.obj";   // repo-root-relative
    const char*               OutPath   = nullptr;   // required (.png)
};

struct ShaderballPreviewResult
{
    double Mean  = 0.0;   // film mean before tonemap (same statistic the exhibit prints per panel)
    long   Bad   = 0;     // non-finite / out-of-range film samples
    int    Tris  = 0;
    int    Nodes = 0;
};

// Renders the preview PNG. Deterministic: same descriptor ⇒ byte-identical PNG (fixed panel-tag seed 16).
// Prints [preview] progress lines like the exhibit's [exhibit] lines. The entry resets the exhibit globals, so it
// may run many times per process (proof loop, Apply-with-preview).
[[nodiscard]] bool RenderShaderballPreview(const ShaderballPreviewRequest& Req, ShaderballPreviewResult& Out) noexcept;

} // namespace Frontier
