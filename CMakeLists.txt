#============================================================================================================================================
# 📦 Frontier/CMakeLists.txt — Cross-Platform CMake Configuration for Windows (MSVC) & Linux (GCC/Clang)
#============================================================================================================================================

cmake_minimum_required(VERSION 3.20)
project(FrontierEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(BuildConfiguration/WindowsConfiguration.cmake)

set(FRONTIER_ENGINE_SOURCES
    DeviceExchange/VulkanExchange.h
    DeviceExchange/VulkanExchange.cpp
    DeviceExchange/ByteSpace.h
    DeviceExchange/ByteSpace.cpp
    DeviceExchange/TaskScheduler.h
    DeviceExchange/TaskScheduler.cpp
    DeviceExchange/ExecutionQueue.h
    DeviceExchange/ExecutionQueue.cpp
    DeviceExchange/VendorClassifier.h
    DeviceExchange/VendorClassifier.cpp
    DeviceExchange/OrientationClassifier.h
    DeviceExchange/OrientationClassifier.cpp
    DeviceExchange/WindowExchange.h
    DeviceExchange/WindowExchange.cpp
    DeviceExchange/InputExchange.h
    DeviceExchange/InputExchange.cpp
    DeviceExchange/RenderTargetExchange.h
    DeviceExchange/RenderTargetExchange.cpp
    DeviceExchange/DiagnosticMetrics.h
    DeviceExchange/DiagnosticMetrics.cpp

    PhysicalDynamics/RigidBodyStructure.h
    PhysicalDynamics/RigidBodySolver.h
    PhysicalDynamics/RigidBodySolver.cpp
    PhysicalDynamics/DeformableStructure.h
    PhysicalDynamics/DeformableSolver.h
    PhysicalDynamics/DeformableSolver.cpp
    PhysicalDynamics/LocomotionSolver.h
    PhysicalDynamics/LocomotionSolver.cpp
    PhysicalDynamics/SpatialStructure.h
    PhysicalDynamics/WorldSpace.h
    PhysicalDynamics/WorldSpace.cpp

    VolumetricDynamics/LevelSetSpace.h
    VolumetricDynamics/LevelSetSpace.cpp
    VolumetricDynamics/FluidSolver.h
    VolumetricDynamics/FluidSolver.cpp
    VolumetricDynamics/ParticleStructure.h
    VolumetricDynamics/ParticleIntegrator.h
    VolumetricDynamics/ParticleIntegrator.cpp

    GeometricRaster/GeometryStructure.h
    GeometricRaster/GeometryStructure.cpp
    GeometricRaster/CameraProjection.h
    GeometricRaster/CameraProjection.cpp
    GeometricRaster/VisibilityProjection.h
    GeometricRaster/VisibilityProjection.cpp
    GeometricRaster/RasterSequence.h
    GeometricRaster/RasterSequence.cpp
    GeometricRaster/MaterialCodec.h
    GeometricRaster/MaterialCodec.cpp

    PhotometricIllumination/ClusteredSpace.h
    PhotometricIllumination/ClusteredSpace.cpp
    PhotometricIllumination/DirectIlluminationIntegrator.h
    PhotometricIllumination/DirectIlluminationIntegrator.cpp
    PhotometricIllumination/GlobalIlluminationIntegrator.h
    PhotometricIllumination/GlobalIlluminationIntegrator.cpp
    PhotometricIllumination/AtmosphereIntegrator.h
    PhotometricIllumination/AtmosphereIntegrator.cpp

    PlatformInterchange/AcousticStructure.h
    PlatformInterchange/AcousticStructure.cpp
    PlatformInterchange/AcousticIntegrator.h
    PlatformInterchange/AcousticIntegrator.cpp
    PlatformInterchange/VoiceExchange.h
    PlatformInterchange/VoiceExchange.cpp
    PlatformInterchange/OnlineInterchange.h
    PlatformInterchange/OnlineInterchange.cpp

    DisplayPresentation/ThemeStructure.h
    DisplayPresentation/ThemeStructure.cpp
    DisplayPresentation/VectorCodec.h
    DisplayPresentation/VectorCodec.cpp
    DisplayPresentation/FontCodec.h
    DisplayPresentation/FontCodec.cpp
    DisplayPresentation/ControlCentrePanel.h
    DisplayPresentation/ControlCentrePanel.cpp
    DisplayPresentation/WorkspacePanel.h
    DisplayPresentation/WorkspacePanel.cpp
    DisplayPresentation/CycleScheduler.h
    DisplayPresentation/CycleScheduler.cpp
    DisplayPresentation/FidelityClassifier.h
    DisplayPresentation/FidelityClassifier.cpp
    DisplayPresentation/FrontierHost.h
    DisplayPresentation/FrontierHost.cpp
)

# Engine Demo Target
add_executable(FrontierEngine ${FRONTIER_ENGINE_SOURCES} DisplayPresentation/EngineExecution.cpp)
target_include_directories(FrontierEngine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT MSVC)
    target_link_libraries(FrontierEngine PRIVATE pthread)
endif()

# Standalone Game Target (Project-F20)
set(PROJECT_F20_SOURCES
    ${FRONTIER_ENGINE_SOURCES}
    Projects/Project-F20/Source/VehicleStructure.h
    Projects/Project-F20/Source/VehicleSolver.h
    Projects/Project-F20/Source/VehicleSolver.cpp
    Projects/Project-F20/Source/TrackSequence.h
    Projects/Project-F20/Source/TrackSequence.cpp
    Projects/Project-F20/Source/GameHost.h
    Projects/Project-F20/Source/GameHost.cpp
    Projects/Project-F20/Source/GameExecution.cpp
)

add_executable(Project-F20 ${PROJECT_F20_SOURCES})
target_include_directories(Project-F20 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-F20)

if(NOT MSVC)
    target_link_libraries(Project-F20 PRIVATE pthread)
endif()

# Standalone Testing Ground Target (Project-Zero)
set(PROJECT_ZERO_SOURCES
    ${FRONTIER_ENGINE_SOURCES}
    Projects/Project-Zero/Source/SceneStructure.h
    Projects/Project-Zero/Source/RayTracingSolver.h
    Projects/Project-Zero/Source/RayTracingSolver.cpp
    Projects/Project-Zero/Source/FlyThroughSolver.h
    Projects/Project-Zero/Source/FlyThroughSolver.cpp
    Projects/Project-Zero/Source/RendererHost.h
    Projects/Project-Zero/Source/RendererHost.cpp
    Projects/Project-Zero/Source/GameExecution.cpp
)

add_executable(Project-Zero ${PROJECT_ZERO_SOURCES})
target_include_directories(Project-Zero PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Zero)

if(NOT MSVC)
    target_link_libraries(Project-Zero PRIVATE pthread)
endif()
