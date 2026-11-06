# Frontier engine — session, game mechanics, authentic Frontier UI/Editor, Vulkan 1.1 GPU 3D Pyro volumetrics,
# toroidal world-grid driving-course smoke, 3D SDF Proving Grounds, and Project Everest.
#
#   cmake -S Frontier -B build -DCMAKE_BUILD_TYPE=Release -DFRONTIER_ENABLE_GLFW=ON
#   cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(FrontierSession CXX)
include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus /bigobj)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

set(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

option(FRONTIER_ENABLE_GLFW "Build the interactive GLFW + Vulkan 1.1 Everest window" OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

# --- Third-party header/source dependencies (Khronos Vulkan-Headers, toml++, patched Dear ImGui, ThorVG, stb) ---
FetchContent_Declare(
    vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.296.tar.gz
    SOURCE_SUBDIR _header_only_
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/1e8829b793b66ad17011732a146b8077d379b011.tar.gz
    SOURCE_SUBDIR _header_only_
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/3bae66c735670619baf51391eba7f3d90a25d125.tar.gz
    SOURCE_SUBDIR _manual_sources_
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    thorvg
    URL https://github.com/thorvg/thorvg/archive/6715f99ac106b6d4587f384a78c59fe957cfd2a3.tar.gz
    SOURCE_SUBDIR _manual_sources_
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    SOURCE_SUBDIR _header_only_
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(vulkan_headers tomlplusplus imgui thorvg stb)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${FRONTIER_ROOT}/Automation/ApplyImGuiPatches.py" --imgui-root "${imgui_SOURCE_DIR}"
    RESULT_VARIABLE IMGUI_PATCH_RESULT
)
if(NOT IMGUI_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to apply Frontier ImGui patch stack A..H onto ${imgui_SOURCE_DIR}")
endif()

set(THORVG_SRC "${thorvg_SOURCE_DIR}")
include("${FRONTIER_ROOT}/Automation/ThorVGSoftware.cmake")

add_library(FrontierImGui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_include_directories(FrontierImGui PUBLIC "${imgui_SOURCE_DIR}")
target_compile_definitions(FrontierImGui PUBLIC "ImDrawIdx=unsigned int")
target_compile_features(FrontierImGui PUBLIC cxx_std_20)

add_library(FrontierTextFiles STATIC
    "${FRONTIER_ROOT}/Engine/Interface/TextFileReplacement.cpp")
target_include_directories(FrontierTextFiles PUBLIC "${FRONTIER_ROOT}")

add_library(FrontierThumbnailAtlasCache STATIC
    "${FRONTIER_ROOT}/Engine/Interface/ThumbnailAtlasCache.cpp")
target_include_directories(FrontierThumbnailAtlasCache PUBLIC "${FRONTIER_ROOT}")

add_library(FrontierInterface STATIC
    "${FRONTIER_ROOT}/Engine/Interface/IconArt.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/IconPresentation.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/PixelSpace.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/GlyphSpace.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/ControlKit.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/TextEntryState.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/ThemeStructure.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/VectorCodec.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/ContentDirectory.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/FontCodec.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/TypefaceRegistry.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/AppearanceInspector.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/AppearanceCodec.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/MotionIntegrator.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/PanelExpansionTab.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/ConfigurationInspector.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/ConfigurationStructure.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/InputPreferencesCodec.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/MonitorPlacement.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/PanelDockspaceMonitor.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/PanelDockspace.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/PanelDockspaceLayout.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/NotificationSequence.cpp"
    "${FRONTIER_ROOT}/Engine/Interface/ConfirmationDialogue.cpp"
    "${FRONTIER_ROOT}/Engine/ContentInterchange/ProjectSpaceCodec.cpp"
    "${FRONTIER_ROOT}/Engine/ContentInterchange/ProjectGenerator.cpp")
target_include_directories(FrontierInterface PUBLIC
    "${FRONTIER_ROOT}"
    "${tomlplusplus_SOURCE_DIR}/include")
target_link_libraries(FrontierInterface PUBLIC
    FrontierImGui
    thorvg_static
    FrontierTextFiles
    FrontierThumbnailAtlasCache)

add_library(FrontierInterfaceExtension STATIC
    "${FRONTIER_ROOT}/Frontier/Extensions/Interface/GizmoSpace.cpp"
    "${FRONTIER_ROOT}/Frontier/Extensions/Interface/GizmoRasterization.cpp")
target_include_directories(FrontierInterfaceExtension PUBLIC "${FRONTIER_ROOT}" "${FRONTIER_ROOT}/Frontier")
target_link_libraries(FrontierInterfaceExtension PUBLIC FrontierImGui)

add_library(FrontierEditorUI STATIC
    "${FRONTIER_ROOT}/UI/Editor/ProjectBrowserPanel.cpp"
    "${FRONTIER_ROOT}/UI/Editor/FoundationPanel.cpp"
    "${FRONTIER_ROOT}/UI/Editor/EditorWorkspace.cpp"
    "${FRONTIER_ROOT}/UI/Editor/ControlCentrePanel.cpp"
    "${FRONTIER_ROOT}/UI/Editor/ContentBrowserPanel.cpp")
target_link_libraries(FrontierEditorUI PUBLIC FrontierInterface)
target_compile_definitions(FrontierEditorUI PUBLIC FRONTIER_EDITOR=1)

add_library(FrontierVehicleDynamics STATIC
    Engine/PhysicalDynamics/Vehicle/Aerodynamics.cpp
    Engine/PhysicalDynamics/Vehicle/Drivetrain.cpp
    Engine/PhysicalDynamics/Vehicle/PacejkaMagicFormula.cpp
    Engine/PhysicalDynamics/Vehicle/TyreSlipDynamics.cpp
    Engine/PhysicalDynamics/Vehicle/VehicleGeometry.cpp
    Engine/PhysicalDynamics/Vehicle/VehicleSolver.cpp
    Engine/PhysicalDynamics/Vehicle/TyreCarcassLattice.cpp
    Engine/PhysicalDynamics/Vehicle/TyreCarcassSolid.cpp
    Engine/PhysicalDynamics/Vehicle/VehicleSpecification.cpp)
target_include_directories(FrontierVehicleDynamics PUBLIC
    "${FRONTIER_ROOT}"
    "${FRONTIER_ROOT}/Frontier")

add_library(FrontierSceneEditorUI STATIC
    "${FRONTIER_ROOT}/Engine/Render/StbImageImplementation.cpp"
    "${FRONTIER_ROOT}/Engine/ContentInterchange/VolumeDistanceField.cpp"
    "${FRONTIER_ROOT}/Engine/ContentInterchange/BakeMesh.cpp"
    Engine/Geometry/ShapeArchive.cpp
    Engine/Geometry/PrimitiveGeometryGenerator.cpp
    Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.cpp
    Engine/Geometry/GeometryProcessing/FacetTierBuild.cpp
    Engine/Rendering/ClusterNesting.cpp
    Engine/Rendering/GlobalIllumination/EnvironmentEncoding.cpp
    Engine/PhysicalDynamics/AtmosphericFlow.cpp
    Engine/PhysicalDynamics/ForceField.cpp
    Engine/PhysicalDynamics/ForceSourceCatalogue.cpp
    Engine/PhysicalDynamics/FluidDynamics/GasParameterCatalogue.cpp
    Engine/PhysicalDynamics/FluidDynamics/GasInstrumentProjection.cpp
    Engine/PhysicalDynamics/ParticleDynamics/ParticleEffectLibrary.cpp
    Engine/PhysicalDynamics/ParticleDynamics/ParticleFieldTaxonomy.cpp
    Engine/PhysicalDynamics/ParticleDynamics/ParticleUniformPacking.cpp
    Engine/PhysicalDynamics/ParticleDynamics/ParticleSpace.cpp
    Engine/PhysicalDynamics/ParticleDynamics/ParticleSpaceCodec.cpp
    Engine/PhysicalDynamics/ParticleDynamics/ParticleLightning.cpp
    Engine/Scene/RecordStore.cpp
    Engine/Scene/RecordEntry.cpp
    Engine/Scene/SceneDirectory.cpp
    Engine/Scene/WorldSpace.cpp
    Engine/Scene/WorldRegister.cpp
    Engine/Scene/VehicleRecordCodec.cpp
    Engine/Scene/VehicleReadings.cpp
    Engine/Scene/GeometryLayout.cpp
    Engine/Diagnostics/LogCategories.cpp
    Engine/Diagnostics/LogRingStore.cpp
    Engine/Diagnostics/LogStream.cpp
    Engine/Interface/ProjectContent.cpp
    Engine/Interface/ProjectDescriptor.cpp
    Engine/Interface/StackRailControls.cpp
    Engine/Interface/LogStreamPanel.cpp
    Engine/Interface/LogStreamPanelPresentation.cpp
    Engine/Interface/SceneDirectoryPanel.cpp
    Engine/Interface/SceneDirectoryPanelPresentation.cpp
    Engine/Interface/InspectorRuntime.cpp
    Engine/Interface/InspectorControls.cpp
    Engine/Interface/DiagramControls.cpp
    Engine/Interface/FractureEditor.cpp
    Engine/Interface/FluidPlumeEditor.cpp
    Engine/Interface/FluidPlumeEditor/FluidPlumeEditorWorkspace.cpp
    Engine/Interface/FluidPlumeEditor/FluidPlumeLibrary.cpp
    Engine/Interface/FluidPlumeEditor/FluidPlumePages.cpp
    Engine/Interface/ParticleEditor.cpp
    Engine/Interface/ParticleEditor/ParticleFieldOverlay.cpp
    Engine/Interface/ParticleEditor/ParticleEditorPages.cpp
    Engine/Interface/ParticleEditor/ParticleEditorWorkspace.cpp
    Engine/Interface/Controls/ControlTheme.cpp
    Engine/Interface/Controls/ControlColumn.cpp
    Engine/Interface/Controls/GlyphRun.cpp
    Engine/Interface/Controls/GlyphSymbol.cpp
    Engine/Interface/Controls/DiagramPen.cpp
    Engine/Interface/Controls/DiagramCanvas.cpp
    Engine/Interface/Controls/PageHeading.cpp
    Engine/Interface/Controls/RasterPlate.cpp
    Engine/Interface/Controls/ActionRow.cpp
    Engine/Interface/Controls/AxisFields.cpp
    Engine/Interface/Controls/WorldVectorOverlay.cpp
    Engine/Interface/Controls/SectionHeading.cpp
    Engine/Interface/Controls/DisclosureRow.cpp
    Engine/Interface/Controls/SwitchRow.cpp
    Engine/Interface/Controls/TabStrip.cpp
    Engine/Interface/Controls/ReadoutBlock.cpp
    Engine/Interface/Controls/TagPill.cpp
    Engine/Interface/Controls/MetricTiles.cpp
    Engine/Interface/Controls/SwitchTiles.cpp
    Engine/Interface/Controls/SliderRow.cpp
    Engine/Interface/Controls/SelectRow.cpp
    Engine/Interface/Controls/PresetStrip.cpp
    Engine/Interface/Controls/ChoiceRow.cpp
    Engine/Interface/Controls/ChoiceList.cpp
    Engine/Interface/Controls/ToggleRow.cpp
    Engine/Interface/Controls/NextButton.cpp
    Engine/Interface/Controls/DateRow.cpp
    Engine/Interface/Controls/TintRow.cpp
    Engine/Interface/Controls/CaptionText.cpp
    Engine/Interface/Controls/FooterRow.cpp
    Engine/Interface/Controls/RecordHeading.cpp
    Engine/Interface/Controls/CardTitleRow.cpp
    Engine/Interface/Controls/NotesField.cpp
    Engine/Interface/Controls/TransformTable.cpp
    Engine/Interface/Controls/QuickTiles.cpp
    Engine/Interface/Controls/ModifierPanel.cpp
    Engine/Interface/Controls/ChannelRows.cpp
    Engine/Interface/Controls/PlacementGizmo.cpp
    Engine/Interface/ConstructPanel.cpp
    Engine/Interface/Inspectors/RenderInspectionModes.cpp
    Engine/Interface/Inspectors/SpatialInspector.cpp
    Engine/Interface/Inspectors/PhysicsInspector.cpp
    Engine/Interface/Inspectors/ThermalInspector.cpp
    Engine/Interface/Inspectors/ChemicalInspector.cpp
    Engine/Interface/Inspectors/SurfaceOpticsInspector.cpp
    Engine/Interface/Inspectors/RenderSettingsInspector.cpp
    Engine/Interface/Inspectors/RenderSettings/RenderSettingsReadings.cpp
    Engine/Interface/Controls/SectionRows.cpp
    Engine/Interface/Inspectors/RenderSettings/SamplingSection.cpp
    Engine/Interface/Inspectors/RenderSettings/IlluminationSection.cpp
    Engine/Interface/Inspectors/RenderSettings/ReflectionSection.cpp
    Engine/Interface/Inspectors/RenderSettings/ShadowSection.cpp
    Engine/Interface/Inspectors/RenderSettings/DenoiseSection.cpp
    Engine/Interface/Inspectors/RenderSettings/ExposureSection.cpp
    Engine/Interface/Inspectors/RenderSettings/GeometrySection.cpp
    Engine/Interface/Inspectors/RenderSettings/ViewSection.cpp
    Engine/Interface/Inspectors/RenderSettings/TimingSection.cpp
    Engine/Interface/Inspectors/SolarInspector.cpp
    Engine/Interface/Inspectors/OpticalReference.cpp
    Engine/Interface/Inspectors/OpticalFlareInspector.cpp
    Engine/Interface/Inspectors/AtmosphereInspector.cpp
    Engine/Interface/Inspectors/AtmosphereBakeImages.cpp
    Engine/Interface/Inspectors/CloudDensityReference.cpp
    Engine/Interface/Inspectors/CloudInspector.cpp
    Engine/Interface/Inspectors/StarCatalogueReference.cpp
    Engine/Interface/Inspectors/StarInspector.cpp
    Engine/Interface/Inspectors/LunarSurfaceReference.cpp
    Engine/Interface/Inspectors/MoonInspector.cpp
    Engine/Interface/Inspectors/PlanetInspector.cpp
    Engine/Interface/Inspectors/PlanetSurfacePreview.cpp
    Engine/Interface/Inspectors/FogInspector.cpp
    Engine/Interface/Inspectors/LightningInspector.cpp
    Engine/Interface/Inspectors/WindInspector.cpp
    Engine/Interface/Inspectors/ForceFieldInspector.cpp
    Engine/Interface/Inspectors/ForceSourceInspector.cpp
    Engine/Interface/ForceFieldVisualization/ForceFieldVisualization.cpp
    Engine/Interface/Inspectors/ForceField/ForceFieldPageReading.cpp
    Engine/Interface/Inspectors/ForceField/ForceFieldPieces.cpp
    Engine/Interface/Inspectors/ForceField/FieldViewSection.cpp
    Engine/Interface/Inspectors/ForceField/FieldRangeSection.cpp
    Engine/Interface/Inspectors/ForceField/FieldForcesSection.cpp
    Engine/Interface/Inspectors/ForceField/FieldReceiversSection.cpp
    Engine/Interface/Inspectors/ForceField/FieldTimelineSection.cpp
    Engine/Interface/Inspectors/ForceField/FieldVectorsSection.cpp
    Engine/Interface/Inspectors/ForceField/FieldBudgetSection.cpp
    Engine/Interface/Inspectors/ForceField/WindFlowSection.cpp
    Engine/Interface/Inspectors/ForceField/WindGustSection.cpp
    Engine/Interface/Inspectors/ForceField/WindAltitudeSection.cpp
    Engine/Interface/Inspectors/ForceField/WindEddySection.cpp
    Engine/Interface/Inspectors/ForceField/WindMesoscaleSection.cpp
    Engine/Interface/Inspectors/FluidPlumeInspector.cpp
    Engine/Interface/Inspectors/FractureInspector.cpp
    Engine/Interface/Inspectors/ParticleInspector.cpp
    Engine/Interface/Inspectors/GeometryInspector.cpp
    Engine/Interface/Inspectors/VehicleInspector.cpp
    Engine/Interface/Inspectors/TyreInspector.cpp
    Engine/Interface/Inspectors/Geometry/TransformSection.cpp
    Engine/Interface/Inspectors/Geometry/QuickControlSection.cpp
    Engine/Interface/Inspectors/Geometry/ModifierStackSection.cpp
    Engine/Interface/Inspectors/Geometry/MaterialSection.cpp
    Engine/Interface/Inspectors/Geometry/FractureSection.cpp
    Engine/Interface/SceneInspectorPanel.cpp
    Engine/Interface/ViewportChromeControls.cpp)
target_include_directories(FrontierSceneEditorUI PUBLIC
    "${FRONTIER_ROOT}"
    "${FRONTIER_ROOT}/Frontier")
target_include_directories(FrontierSceneEditorUI PRIVATE "${stb_SOURCE_DIR}")
target_compile_definitions(FrontierSceneEditorUI PUBLIC
    FRONTIER_WITH_INTERFACE_SDK=1
    FRONTIER_EDITOR_PROFILE=1
    FRONTIER_EDITOR=1)
target_link_libraries(FrontierSceneEditorUI PUBLIC
    FrontierEditorUI
    FrontierInterfaceExtension
    FrontierVehicleDynamics
    Threads::Threads)

add_library(FrontierEverestWorkspace STATIC
    "${FRONTIER_ROOT}/Projects/Everest/Source/EverestApplication.cpp"
    "${FRONTIER_ROOT}/Projects/Everest/Source/EverestSimulationRecords.cpp"
    "${FRONTIER_ROOT}/Projects/Everest/Source/EverestWorkspace.cpp"
    "${FRONTIER_ROOT}/Projects/Everest/Source/EverestFieldViewport.cpp"
    "${FRONTIER_ROOT}/Projects/Everest/Source/EverestViewportNavigation.cpp")
target_include_directories(FrontierEverestWorkspace PUBLIC
    "${FRONTIER_ROOT}"
    "${FRONTIER_ROOT}/Frontier")
target_link_libraries(FrontierEverestWorkspace PUBLIC FrontierSceneEditorUI)

add_library(FrontierSession STATIC
    Engine/Input/InputEncoder.cpp
    Engine/Input/InputDriver.cpp
    Engine/Input/SyntheticDriver.cpp
    Engine/Scene/DeployableUnit.cpp
    Engine/Scene/FacetArchive.cpp
    Engine/Session/SpawnPolicy.cpp
    Engine/Session/SessionConfiguration.cpp
    Engine/Session/SessionPolicy.cpp
    Engine/Session/SessionSpace.cpp
    Engine/Session/TransferConfiguration.cpp
    Engine/Session/LevelTransition.cpp
    Engine/Session/LobbySession.cpp
    ../Projects/Everest/Source/Session/SessionPolicy.cpp)
target_include_directories(FrontierSession PUBLIC "${FRONTIER_ROOT}" "${FRONTIER_ROOT}/Frontier")
target_compile_features(FrontierSession PUBLIC cxx_std_20)

add_library(EverestNavigation STATIC ../Projects/Everest/Source/Navigation/NavigationApp.cpp)
target_link_libraries(EverestNavigation PUBLIC FrontierSession)

add_library(FrontierCamera STATIC Game/CameraController.cpp)
target_link_libraries(FrontierCamera PUBLIC FrontierSession)

add_library(FrontierGame STATIC Game/SoftwarePresentation.cpp)
target_link_libraries(FrontierGame PUBLIC FrontierSession FrontierCamera)

add_library(FrontierVolumetrics STATIC
    Engine/Rendering/VulkanContext.cpp
    Engine/Rendering/WorldSdfScene.cpp
    Engine/VolumetricDynamics/PyroVolumeSolver.cpp
    Engine/VolumetricDynamics/TyreSmokeIntake.cpp
    Engine/VolumetricDynamics/FluidPlumeBridge.cpp)
target_include_directories(FrontierVolumetrics PUBLIC
    "${FRONTIER_ROOT}"
    "${FRONTIER_ROOT}/Frontier"
    "${vulkan_headers_SOURCE_DIR}/include"
    "${tomlplusplus_SOURCE_DIR}/include")
target_link_libraries(FrontierVolumetrics PUBLIC
    FrontierEverestWorkspace
    FrontierSceneEditorUI
    FrontierEditorUI
    FrontierInterface
    FrontierGame
    FrontierSession
    FrontierCamera
    EverestNavigation
    FrontierImGui
    ${CMAKE_DL_LIBS})
target_compile_features(FrontierVolumetrics PUBLIC cxx_std_20)

add_executable(SessionValidation ../Validation/SessionValidation.cpp)
target_link_libraries(SessionValidation PRIVATE FrontierSession EverestNavigation FrontierCamera)

add_executable(VolumetricAndEditorValidation ../Validation/VolumetricAndEditorValidation.cpp)
target_link_libraries(VolumetricAndEditorValidation PRIVATE FrontierVolumetrics)

add_executable(LobbySession Game/LobbySessionMain.cpp)
target_link_libraries(LobbySession PRIVATE FrontierGame FrontierSession)

add_executable(Everest ../Projects/Everest/Source/Game/EverestMain.cpp)
target_link_libraries(Everest PRIVATE FrontierGame FrontierSession EverestNavigation)

if(FRONTIER_ENABLE_GLFW)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    if(UNIX AND NOT APPLE)
        find_package(X11 QUIET)
        if(NOT X11_FOUND)
            set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
        endif()
    endif()
    FetchContent_Declare(
        glfw
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(glfw)

    target_compile_definitions(FrontierSession PUBLIC FRONTIER_WITH_GLFW GLFW_INCLUDE_NONE)
    target_link_libraries(FrontierSession PUBLIC glfw)
    add_executable(EverestWindow
        ../Projects/Everest/Source/Game/EverestGlfwMain.cpp
        "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")
    target_compile_definitions(EverestWindow PRIVATE GLFW_INCLUDE_NONE)
    target_link_libraries(EverestWindow PRIVATE
        FrontierVolumetrics
        FrontierGame
        FrontierSession
        EverestNavigation
        glfw)
endif()

if(BUILD_TESTING)
    add_test(NAME SessionValidation
             COMMAND SessionValidation
             WORKING_DIRECTORY "${FRONTIER_ROOT}")
    add_test(NAME VolumetricAndEditorValidation
             COMMAND VolumetricAndEditorValidation
             WORKING_DIRECTORY "${FRONTIER_ROOT}")
endif()
