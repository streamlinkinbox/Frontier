//============================================================================================================================================
//                                                     DIAGNOSTICINSPECTOR.H
//============================================================================================================================================
// 🧩 Small popup overlay for the R2 renderer diagnostics — NOT a settings page. F3 opens it / cycles the debug view,
//    Shift+F3 cycles backwards, F4 toggles HiZ occlusion culling, F5 toggles the R6 row-3 Walker-alias light pick
//    (off = uniform, the R0 identity proof), Escape closes it. It hangs from the notch line at the top-right (the FPS
//    readout owns the top-left) and shows the active view, the cull funnel, the GPU pass timings, the live ReSTIR
//    flags, and the scene complexity + texture/LOD census (the R4b carry-over popup content).
//
//    Layout (Notch card language, ControlKit palette): 13 px title "Debug View · <name>", 11 px rows
//        clusters   2 097 → frustum 1 240 → cone 1 240 → visible 312     (phase 1 298 + phase 2 14)
//        triangles  38 912 drawn  |  indirect: 1 draw call per phase
//        cull 0.12 ms · raster 0.41 ms · HiZ 0.08 ms · resolve 0.19 ms · kernel 9.8 ms
//        restir temporal on · spatial on · alias pick on · 4 cand + 2 extra
//        scene 26 materials → 26 slabs · 69 textures · 41.2 MB · <= 11 mips
//        key hints  F3 next · ⇧F3 previous · F4 HiZ on/off · F5 alias pick · Esc close
//    Values come from VisibilityTelemetry (two frames old, no stall).

#pragma once

#include "PixelSpace.h"
#include "../DeviceExchange/InputExchange.h"
#include "../DeviceExchange/VisibilityExchange.h"
#include <cstdint>

namespace Frontier {

// Forward declarations (passed by const reference; full headers only in the .cpp).
struct ReSTIRIntegratorConfiguration;
struct MaterialIndexMetrics;
struct TextureIndexMetrics;

class DiagnosticInspector
{
public:
    DiagnosticInspector() noexcept = default;

    // Seeds from the persisted configuration; the popup starts closed even if a debug view is persisted.
    void Seed(DebugViewCategory View, bool OcclusionCulling, bool AliasPick) noexcept { View_ = View; Occlusion_ = OcclusionCulling; AliasPick_ = AliasPick; }

    // Edge-detects F3 / Shift+F3 / F4 / F5 / Escape. Returns true when something changed (caller persists + restarts accumulation).
    bool AdvanceInteraction(const InputExchange& Input) noexcept;

    // Mirrors the integrator's live flag (the scheduler's Alias-pick checkbox writes it directly). Called every
    //    frame before AdvanceInteraction so the F5 key and the checkbox converge on one flag.
    void AssignAliasPick(bool On) noexcept { AliasPick_ = On; }

    void ConstructInspectorLayout(PixelSpace& Surface, float TopInset, float DisplayWidth, const VisibilityTelemetry& Telemetry,
                                  uint32_t ClusterTotal, bool DrawIndirectCount, const ReSTIRIntegratorConfiguration& ReSTIR,
                                  const MaterialIndexMetrics& MaterialStats, const TextureIndexMetrics& TextureStats,
                                  uint32_t MaxTextureLevels) const noexcept;

    [[nodiscard]] DebugViewCategory QueryView()      const noexcept { return View_; }
    [[nodiscard]] bool              QueryOcclusion() const noexcept { return Occlusion_; }
    [[nodiscard]] bool              QueryAliasPick() const noexcept { return AliasPick_; }
    [[nodiscard]] bool              IsOpen()         const noexcept { return Open_; }

private:
    DebugViewCategory View_       = DebugViewCategory::Off;
    bool              Occlusion_  = true;
    bool              AliasPick_  = true;    // R6 row 3: Walker-alias light pick (F5; false = uniform R0 identity)
    bool              Open_       = false;
    bool              F3Held_     = false;
    bool              F4Held_     = false;
    bool              F5Held_     = false;
    bool              EscapeHeld_ = false;
};

} // namespace Frontier
