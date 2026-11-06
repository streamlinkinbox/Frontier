/*==========================================================================================================================================
                                                        PYROSOLVERSESSION.CPP
==========================================================================================================================================*/
// 🧩 One refresh of the Fluid editor's solver; see PyroSolverSession.h.

#include "Frontier/Extensions/Fluid/PyroSolverSession.h"

#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Frontier
{

struct PyroSolverSession::Implementation
{
    VulkanContext    Context;
    PyroVolumeSolver Solver;
    bool             Ready = false;
    std::string      Failure;
    std::string      Adapter;

    ~Implementation()
    {
        if (Ready) Solver.Shutdown(Context);
    }
};

PyroSolverSession::PyroSolverSession() : Parts(std::make_unique<Implementation>()) {}

PyroSolverSession::~PyroSolverSession() = default;

const std::string& PyroSolverSession::QueryAdapter() const noexcept
{
    return Parts->Adapter;
}

bool PyroSolverSession::Refresh(const FluidViewportRequest& Request, std::vector<uint8_t>& Rgba, std::array<uint32_t, 2>& Picture, FluidVolumeReadout& Readout, std::string& Failure)
{
    Implementation& Own = *Parts;
    if (Request.Settings == nullptr || Request.Extent[0] < 32 || Request.Extent[1] < 32)
    {
        Failure = "There is no picture to draw.";
        return false;
    }
    if (!Own.Failure.empty())
    {
        Failure = Own.Failure;
        return false;
    }
    // 📝 The settings are the record's; the solver may adjust the copy it steps, and the record is left as the person set it.
    GasSettings Working = *Request.Settings;
    std::string Diagnostic;
    if (!Own.Ready)
    {
        if (!Own.Context.InitializeHeadless(Diagnostic) || !Own.Solver.Initialize(Own.Context, Working, Diagnostic))
        {
            Own.Failure = "The Fluid solver could not start: " + Diagnostic;
            Failure     = Own.Failure;
            return false;
        }
        Own.Ready   = true;
        Own.Adapter = Own.Context.QueryDeviceSummary();
    }
    PyroVolumeSolver& Solver = Own.Solver;
    VulkanContext&    Context = Own.Context;
    // 📝 A new grid rebuilds the lattice and restarts the simulation (the prototype's "Grid rebuilt at N³. Simulation restarted.").
    Solver.EnsureLatticeResolution(Context, Working.LatticeResolution);
    if (Request.Clear) Solver.ClearLattice(Context);
    Solver.AssignBrush(Request.Brush.PositionUvw, Request.Brush.Velocity, Request.Brush.Held);
    for (const FluidBurstRequest& Burst : Request.Bursts)
        Solver.TriggerBurstSequence(Context, Working, Burst.Pattern.c_str(), Burst.AtPoint ? &Burst.PointUvw : nullptr);
    if (Request.Enabled)
    {
        for (uint32_t Step = 0; Step < Request.Steps; ++Step) Solver.AdvanceSimulation(Context, Working, 1.0f / 60.0f);
        if (Request.Seconds > 0.0) Solver.AdvanceSimulation(Context, Working, static_cast<float>(Request.Seconds));
    }

    // 📝 The canvas is the viewport at the grade's render scale (renderScale), the camera's aspect the viewport's.
    const float Scale  = std::clamp(Working.RenderScale, 0.25f, 2.0f);
    const int   Width  = std::max(64, static_cast<int>(std::lround(static_cast<double>(Request.Extent[0]) * Scale)));
    const int   Height = std::max(64, static_cast<int>(std::lround(static_cast<double>(Request.Extent[1]) * Scale)));
    PyroOrbitCamera Camera = Request.Camera;
    Camera.Aspect          = static_cast<float>(Request.Extent[0]) / static_cast<float>(Request.Extent[1]);
    if (!Solver.RenderStandaloneViewport(Context, Working, Camera.ComputeBasis(), Width, Height, false))
    {
        Failure = "The Fluid picture could not be drawn.";
        return false;
    }
    int ReadWidth = 0, ReadHeight = 0;
    if (!Solver.ReadViewportRgba(Context, Rgba, ReadWidth, ReadHeight) || ReadWidth <= 0 || ReadHeight <= 0)
    {
        Failure = "The Fluid picture could not be read back.";
        return false;
    }
    Picture = { static_cast<uint32_t>(ReadWidth), static_cast<uint32_t>(ReadHeight) };

    const PyroBoundsBox Bounds = Solver.ComputeBoundsBox(Working);
    Readout.Resolution        = static_cast<uint32_t>(std::max(0, Solver.QueryLatticeResolution()));
    Readout.Steps             = Solver.QueryStepCount();
    Readout.SimulationSeconds = static_cast<double>(Solver.QuerySimulationSeconds());
    Readout.BoundsWidth       = Bounds.EffectiveWidth;
    Readout.BoundsHeight      = Bounds.EffectiveHeight;
    Readout.PendingBursts     = static_cast<uint32_t>(Solver.QueryPendingBlasts().size());
    return true;
}

} // namespace Frontier
