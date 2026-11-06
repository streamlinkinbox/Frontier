#pragma once
/*==========================================================================================================================================
                                                          FLUIDPLUMEBRIDGE.H
==========================================================================================================================================*/
// 🧩 Bridges Frontier's FluidPlumeEditor / EverestWorkspace (FluidDynamics::GasParameters, PlumeViewportRequest, SceneViewportRequest)
//    onto the Vulkan 1.1 PyroVolumeSolver + WorldSdfScene + Pacejka TyreSmokeIntake runtime, including continuous toroidal
//    world-grid tyre smoke across the Proving Grounds driving course.

#include "Engine/Interface/FluidPlumeEditor/PlumeViewportPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/SceneViewportPresentation.h"
#include "Engine/PhysicalDynamics/FluidDynamics/GasParameterCatalogue.h"
#include "Engine/Rendering/VulkanContext.h"
#include "Engine/Rendering/WorldSdfScene.h"
#include "Engine/Scene/DeployableUnit.h"
#include "Engine/Scene/WorldSpace.h"
#include "Engine/VolumetricDynamics/PyroVolumeSolver.h"
#include "Engine/VolumetricDynamics/TyreSmokeIntake.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{

void SyncGasParametersToPyroSettings(const FluidDynamics::GasParameters& Source, GasSettings& Target,
                                     uint32_t QualityGrade = 1u) noexcept;

class EverestPlumeSceneRuntime
{
  public:
    EverestPlumeSceneRuntime() = default;
    ~EverestPlumeSceneRuntime() = default;

    EverestPlumeSceneRuntime(const EverestPlumeSceneRuntime&)            = delete;
    EverestPlumeSceneRuntime& operator=(const EverestPlumeSceneRuntime&) = delete;

    bool Initialize(VulkanContext& Ctx, const std::filesystem::path& ArchiveRoot, std::string& OutDiagnostic);
    void Shutdown(VulkanContext& Ctx) noexcept;

    void ConfigureProvingGroundsMode(bool Enabled) noexcept { ProvingGroundsMode = Enabled; }
    [[nodiscard]] bool IsProvingGroundsMode() const noexcept { return ProvingGroundsMode; }

    void ResetWorldSimulation(VulkanContext& Ctx, WorldSpace& World);
    void StepWorldSimulation(VulkanContext& Ctx, WorldSpace& World, double DtSeconds,
                             float DriverThrottle, float DriverBrake, float DriverSteer,
                             bool DriverHandbrake, bool ForceReset);

    SceneViewportImage PresentSceneViewport(VulkanContext& Ctx, WorldSpace& World,
                                            const SceneViewportRequest& Request);

    PlumeViewportImage PresentPlumeViewport(VulkanContext& Ctx,
                                            const PlumeViewportRequest& Request);

    // [unit] - the Fluid editor's viewport (Experimental/Fluid's page): one refresh of the record's settings on a solver of its own, drawn from the editor's
    //          camera; the picture is a texture of this device, so the window samples it with no copy
    FluidViewportImage PresentFluidViewport(VulkanContext& Ctx, const FluidViewportRequest& Request);

    [[nodiscard]] PyroVolumeSolver& AccessWorldSolver() noexcept { return WorldSolver; }
    [[nodiscard]] PyroVolumeSolver& AccessEditorSolver() noexcept { return EditorSolver; }
    // [unit] - the solver of the Fluid editor's viewport; it exists once the editor has been shown (PresentFluidViewport)
    [[nodiscard]] PyroVolumeSolver& AccessFluidSolver() noexcept { return FluidSolver; }
    [[nodiscard]] bool QueryFluidSolverReady() const noexcept { return FluidSolverReady; }
    [[nodiscard]] TyreSmokeIntake& AccessTyreIntake() noexcept { return TyreIntake; }
    [[nodiscard]] DeployableUnit& AccessVehicleUnit() noexcept { return SimVehicle; }
    [[nodiscard]] GasSettings& AccessWorldSettings() noexcept { return WorldSettings; }
    // [-] - times the world solver's window has been put on a new place since the last reset (the first anchoring is not one)
    [[nodiscard]] uint64_t QueryWorldGridRelocations() const noexcept { return WorldSolver.QueryWorldGridRelocations(); }
    // [-] - the window the solid atlas on the device was built for: its origin x and z in cells, and the lattice's edge; the atlas is window-local, so
    //       it is right only while this equals the solver's own window
    [[nodiscard]] std::array<int32_t, 3> QuerySolidAtlasWindow() const noexcept { return SolidAtlasWindow; }
    // [-] - the scene viewport's colour as the last PresentSceneViewport made it, read back to the host (RGBA8, top row first); false until one exists
    [[nodiscard]] bool ReadSceneColor(VulkanContext& Ctx, std::vector<uint8_t>& OutRgba, int& OutWidth, int& OutHeight)
    {
        return SdfWorld.ReadWorldRgba(Ctx, OutRgba, OutWidth, OutHeight);
    }

  private:
    WorldSdfScene          SdfWorld{};
    FacetArchiveData       BodyFacet{};
    FacetArchiveData       TyreFacet{};
    FacetArchiveData       RimFacet{};

    PyroVolumeSolver       WorldSolver{};
    GasSettings            WorldSettings{};
    PyroVolumeSolver       EditorSolver{};
    GasSettings            EditorSettings{};

    PyroVolumeSolver       FluidSolver{};
    bool                   FluidSolverReady     = false;
    std::string            FluidSolverFailure{};

    DeployableUnit         SimVehicle{ UnitCategory::Vehicle };
    TyreSmokeIntake        TyreIntake{};

    bool                   ProvingGroundsMode   = true;
    bool                   Initialized          = false;
    uint64_t               WorldTicks           = 0u;
    uint64_t               EditorTicks          = 0u;
    std::array<int32_t, 3> SolidAtlasWindow     = { INT32_MIN, INT32_MIN, 0 };
    uint32_t               LastWorldActiveSrcs  = 0u;
    std::vector<uint16_t>  AtlasHalfTexels{};    // [-] - the optimized path's atlas, kept between ticks so that its storage is reused
};

} // namespace Frontier
