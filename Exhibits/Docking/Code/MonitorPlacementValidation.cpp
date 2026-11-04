/*==========================================================================================================================================
                                                      MONITORPLACEMENTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise monitor removal, work-area changes, negative coordinates and saved/current density reconciliation.

#include "Engine/Interface/PanelDockspace.h"
#include <array>
#include <cmath>
#include <imgui.h>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Frontier;
namespace
{
WindowRectangle NativeBounds{5000, 100, 1200, 900};
int             Relocations = 0;

void Require(bool Condition, const char *Message)
{
    if (!Condition)
        throw std::runtime_error(Message);
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     MONITOR GEOMETRY VALIDATION
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    try
    {
        const std::array<MonitorWorkArea, 2> Dual{{{{0, 40, 1920, 1040}, 1}, {{-2560, 0, 2560, 1400}, 1.5f}}};
        auto Rectangle = ResolveMonitorPlacement({-2400, 100, 800, 600}, std::nullopt, Dual);
        Require(Rectangle.X == -2400 && Rectangle.Y == 100, "Visible negative-coordinate display must be retained");
        Rectangle = ResolveMonitorPlacement({9000, -9000, 5000, 3000}, std::nullopt, Dual);
        Require(Rectangle.X == 0 && Rectangle.Y == 40 && Rectangle.Width == 1920 && Rectangle.Height == 1040,
                "Removed monitor must recover into a current work area");
        const std::array<MonitorWorkArea, 1> Dense{{{{0, 40, 2560, 1400}, 2}}};
        Rectangle = ResolveMonitorPlacement({100, 140, 600, 400}, MonitorWorkArea{{0, 40, 1920, 1040}, 1}, Dense);
        Require(Rectangle.X == 200 && Rectangle.Y == 240 && Rectangle.Width == 1200 && Rectangle.Height == 800,
                "Saved layout must reconcile current monitor density exactly once");
        const std::array<MonitorWorkArea, 1> Small{{{{0, 0, 800, 560}, 1}}};
        Rectangle = ResolveMonitorPlacement({-2300, 100, 1000, 900}, Dual[1], Small);
        Require(Rectangle.X >= 0 && Rectangle.Y >= 0 && Rectangle.Width <= 800 && Rectangle.Height <= 560,
                "Removed scaled display must fit the surviving work area");
        Rectangle = ResolveMonitorPlacement({1500, 900, 700, 600}, std::nullopt, Small);
        Require(Rectangle.X == 100 && Rectangle.Y == 0,
                "Resolution/taskbar changes must leave all window edges reachable");
        const auto Repeated = ResolveMonitorPlacement(Rectangle, std::nullopt, Small);
        Require(Repeated.X == Rectangle.X && Repeated.Width == Rectangle.Width, "Live recovery must be idempotent");
        Rectangle = ResolveMonitorPlacement({std::numeric_limits<float>::quiet_NaN(), 0, -1, 0}, std::nullopt, Small);
        Require(std::isfinite(Rectangle.X) && Rectangle.Width > 0 && Rectangle.Height > 0,
                "Malformed geometry must be finite");
        const std::array<MonitorWorkArea, 1> Invalid{{{{0, 0, 0, 0}, 0}}};
        Rectangle = ResolveMonitorPlacement({10, 20, 300, 200}, std::nullopt, Invalid);
        Require(Rectangle.X == 10 && Rectangle.Width == 300,
                "Missing/transient monitor enumeration must not move windows");
        Require(!ValidateMonitor({{0, 0, 1000, 1000}, std::numeric_limits<float>::infinity()}),
                "Nonfinite density refused");
        ImGui::CreateContext();
        {
            PanelDockspace Panels;
            auto          &Platform = ImGui::GetPlatformIO();
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            ImGuiPlatformMonitor Monitor;
            Monitor.MainPos = Monitor.WorkPos = ImVec2(0, 0);
            Monitor.MainSize = Monitor.WorkSize = ImVec2(800, 560);
            Monitor.DpiScale                    = 1;
            Platform.Monitors.push_back(Monitor);
            const int InitialCount = Platform.Viewports.Size;
            if (InitialCount == 0)
                Platform.Viewports.push_back(ImGui::GetMainViewport());
            ImGuiViewport Secondary;
            Secondary.PlatformHandle = &Secondary;
            Platform.Viewports.push_back(&Secondary);
            Platform.Platform_GetWindowPos  = [](ImGuiViewport *) { return ImVec2(NativeBounds.X, NativeBounds.Y); };
            Platform.Platform_GetWindowSize = [](ImGuiViewport *)
            { return ImVec2(NativeBounds.Width, NativeBounds.Height); };
            Platform.Platform_SetWindowPos = [](ImGuiViewport *, ImVec2 Position)
            {
                NativeBounds.X = Position.x;
                NativeBounds.Y = Position.y;
                ++Relocations;
            };
            Platform.Platform_SetWindowSize = [](ImGuiViewport *, ImVec2 Size)
            {
                NativeBounds.Width  = Size.x;
                NativeBounds.Height = Size.y;
            };
            Panels.RecoverPlatformWindows();
            Require(NativeBounds.X == 0 && NativeBounds.Width == 800 && Secondary.PlatformRequestMove &&
                        Secondary.PlatformRequestResize,
                    "Topology recovery must invoke real platform callback slots");
            Panels.RecoverPlatformWindows();
            Require(Relocations == 1, "Unchanged monitor enumeration must not keep relocating native windows");
            Secondary.Flags |= ImGuiViewportFlags_IsMinimized;
            Platform.Monitors[0].WorkSize = ImVec2(600, 400);
            Panels.RecoverPlatformWindows();
            Require(NativeBounds.Width == 800, "Minimized native window must not be resized during topology change");
            Secondary.Flags &= ~ImGuiViewportFlags_IsMinimized;
            Panels.RecoverPlatformWindows();
            Require(NativeBounds.Width == 600 && NativeBounds.Height == 400,
                    "Deferred recovery must apply when a minimized secondary window returns");
            Platform.Viewports.resize(InitialCount);
            Platform.Platform_SetWindowPos  = nullptr;
            Platform.Platform_SetWindowSize = nullptr;
            Platform.Platform_GetWindowPos  = nullptr;
            Platform.Platform_GetWindowSize = nullptr;
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        }
        ImGui::DestroyContext();
        std::cout << "PASS: monitor work-area recovery, removed/negative monitors, density reconciliation, idempotence "
                     "and invalid input.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
