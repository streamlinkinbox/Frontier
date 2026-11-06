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

    [[nodiscard]] PyroVolumeSolver& AccessWorldSolver() noexcept { return WorldSolver; }
    [[nodiscard]] PyroVolumeSolver& AccessEditorSolver() noexcept { return EditorSolver; }
    [[nodiscard]] TyreSmokeIntake& AccessTyreIntake() noexcept { return TyreIntake; }
    [[nodiscard]] DeployableUnit& AccessVehicleUnit() noexcept { return SimVehicle; }
    [[nodiscard]] GasSettings& AccessWorldSettings() noexcept { return WorldSettings; }
    [[nodiscard]] uint64_t QueryWorldGridRelocations() const noexcept { return WorldGridRelocations; }

  private:
    WorldSdfScene          SdfWorld{};
    FacetArchiveData       BodyFacet{};
    FacetArchiveData       TyreFacet{};
    FacetArchiveData       RimFacet{};

    PyroVolumeSolver       WorldSolver{};
    GasSettings            WorldSettings{};
    PyroVolumeSolver       EditorSolver{};
    GasSettings            EditorSettings{};

    DeployableUnit         SimVehicle{ UnitCategory::Vehicle };
    TyreSmokeIntake        TyreIntake{};

    bool                   ProvingGroundsMode   = true;
    bool                   Initialized          = false;
    uint64_t               WorldTicks           = 0u;
    uint64_t               EditorTicks          = 0u;
    uint64_t               WorldGridRelocations = 0u;
    std::array<int32_t, 2> LastWorldGridOrg     = { 0, 0 };
    uint32_t               LastWorldActiveSrcs  = 0u;
};

} // namespace Frontier
