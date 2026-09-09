//============================================================================================================================================
//                                                       RENDERERPANEL.H
//============================================================================================================================================
// 🧩 Immediate-mode ImGui control centre overlay — presents ReSTIR parameters, camera telemetry and scene diagnostics.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)
#endif

#include "ReSTIRIntegrator.h"
#include "../Editor/EditorHost.h"
#include "../../Projects/Project-Zero/Source/FlyThroughSolver.h"
#include "../../Projects/Project-Zero/Source/RayTracingSolver.h"
#include <cstdint>
#include <functional>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDERER PANEL
//------------------------------------------------------------------------------------------------------------------------

class RenderScheduler
{
public:
    RenderScheduler() noexcept = default;
    ~RenderScheduler() noexcept = default;

    RenderScheduler(const RenderScheduler&)            = delete;
    RenderScheduler& operator=(const RenderScheduler&) = delete;

    // Call once after ImGui context exists — applies colour scheme and style
    void ApplyTheme() noexcept;

    // Call every frame between ImGui::NewFrame() and ImGui::Render()
    // Mutates integrator parameters directly via its public setters
    // OverlayHook runs between ImGui::NewFrame and ImGui::Render so engine overlays (Control Centre) can
    //    record onto the foreground draw list of the same frame. Pass nullptr / empty for none.
    using OverlayHook = std::function<void()>;

    void Present(ReSTIRIntegrator&                       Integrator,
                 const ProjectZero::FlyThroughSolver&    Camera,
                 const ProjectZero::RayTracingSolver&    Scene,
                 uint32_t                                ViewportWidth,
                 uint32_t                                ViewportHeight,
                 const OverlayHook&                      Overlay = {}) noexcept;

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

    // True while an ImGui window has captured the pointer / keyboard (one tick stale — the state is read
    //    from the previous tick, the same lag every overlay gate accepts).
    [[nodiscard]] bool QueryEditorCapturesPointer() const noexcept;
    [[nodiscard]] bool QueryEditorCapturesKeyboard() const noexcept;

private:
    bool QuitRequested = false;     // [-]  quit button pressed

#ifdef FRONTIER_DEVELOPMENT
    EditorHost Editor_;
#endif

    void SectionCamera  (const ProjectZero::FlyThroughSolver& Camera) noexcept;
    void SectionReSTIR  (ReSTIRIntegrator& Integrator, uint32_t ViewportWidth, uint32_t ViewportHeight) noexcept;
    void SectionScene   (const ProjectZero::RayTracingSolver& Scene) noexcept;
};

template<>
inline bool RenderScheduler::Convert<bool>() const noexcept
{
    return QuitRequested;
}

} // namespace Frontier
