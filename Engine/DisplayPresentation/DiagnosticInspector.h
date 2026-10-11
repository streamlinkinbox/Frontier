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
#include <vector>

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
    void SeedPatchErrorPixels(float Pixels) noexcept { PatchErrorPixels_ = NormalisePatchError(Pixels); }

    // Edge-detects F3 / Shift+F3 / F4 / F5 / Escape. Returns true when something changed (caller persists + restarts accumulation).
    void RecordDurations(float Seconds, const VisibilityTelemetry& Telemetry) noexcept;
    bool QueryPointerCaptured() const noexcept { return Open_ && PointerCaptured_; }
    bool* AccessOpen() noexcept { return &Open_; }
    bool AdvanceInteraction(const InputExchange& Input) noexcept;

    // Mirrors the integrator's live flag (the scheduler's Alias-pick checkbox writes it directly). Called every
    //    frame before AdvanceInteraction so the F5 key and the checkbox converge on one flag.
    void AssignAliasPick(bool On) noexcept { AliasPick_ = On; }

    void ConstructInspectorLayout(PixelSpace& Surface, float TopInset, float DisplayWidth, const VisibilityTelemetry& Telemetry,
                                  uint32_t ClusterTotal, bool DrawIndirectCount, const ReSTIRIntegratorConfiguration& ReSTIR,
                                  const MaterialIndexMetrics& MaterialStats, const TextureIndexMetrics& TextureStats,
                                  uint32_t MaxTextureLevels, const char* MaterialSummary = nullptr) noexcept;

    [[nodiscard]] DebugViewCategory QueryView()      const noexcept { return View_; }
    [[nodiscard]] bool              QueryOcclusion() const noexcept { return Occlusion_; }
    [[nodiscard]] bool              QueryAliasPick() const noexcept { return AliasPick_; }
    // How much projected geometric error the PATCH PREVIEW may show, in pixels. F6 cycles 1 → 2 → 4 → 8 → 1.
    //    One pixel is the strict default and the value every earlier build used implicitly; the larger steps exist
    //    because one alternative at one pixel only changes detail when the object is already far away, and a
    //    reviewer needs to watch the transition happen at a dolly distance to believe it is wired at all.
    [[nodiscard]] float             QueryPatchErrorPixels() const noexcept { return PatchErrorPixels_; }
    [[nodiscard]] bool              IsOpen()         const noexcept { return Open_; }

private:
    struct DurationSample { float Fps = 0, Milliseconds = 0, Gpu = -1; };
    std::vector<DurationSample> Durations_;
    uint32_t DurationCursor_ = 0, DurationCount_ = 0, IntervalCount_ = 0;
    float IntervalSeconds_ = 0;
    int Metric_ = 0, Presentation_ = 0, Period_ = 1;
    bool Paused_ = false;
    bool PointerCaptured_ = false;
    DebugViewCategory View_       = DebugViewCategory::Off;
    bool              Occlusion_  = true;
    bool              AliasPick_  = true;    // R6 row 3: Walker-alias light pick (F5; false = uniform R0 identity)
    bool              Open_       = false;
    bool              F3Held_     = false;
    bool              F4Held_     = false;
    bool              F5Held_     = false;
    bool              F6Held_     = false;
    float             PatchErrorPixels_ = 1.0f;   // [px] patch-preview screen-error tolerance (F6)

    static float NormalisePatchError(float Pixels) noexcept
    {
        // Only the four cycle steps are representable, so a corrupt configuration value cannot dial the preview
        //    to an arbitrary tolerance; anything else falls back to the strict default.
        return Pixels == 2.0f || Pixels == 4.0f || Pixels == 8.0f ? Pixels : 1.0f;
    }
    bool              EscapeHeld_ = false;
};

} // namespace Frontier
