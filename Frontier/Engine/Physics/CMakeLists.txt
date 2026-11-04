cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FrontierTyreContinuum LANGUAGES CXX)
endif()

# 📝 Standalone visual projection and Hex8 deformation; no vehicle dynamics or Jolt dependency.
add_library(FrontierTyreContinuum STATIC TyreContinuum.cpp TyreProjectionDevice.cpp TyreSolidDevice.cpp)
target_compile_features(FrontierTyreContinuum PUBLIC cxx_std_17)
target_include_directories(FrontierTyreContinuum PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(MSVC)
    target_compile_options(FrontierTyreContinuum PRIVATE /utf-8 /W4)
else()
    target_compile_options(FrontierTyreContinuum PRIVATE -Wall -Wextra -Werror)
endif()

# 📝 Vehicle physics: the demo-faithful vehicle solver (480 Hz internal substeps), the unified induction charger
#    (NA / turbo / Roots / TwinScrew / Centrifugal), the Project Drive port, the backend-neutral physics adapter
#    (NullPhysicsAdapter built here; JoltAdapter under FRONTIER_WITH_JOLT) and the dedicated physics thread.
find_package(Threads QUIET)
add_library(FrontierVehiclePhysics STATIC
    VehicleSolver.cpp
    TyreSlipSolver.cpp
    TyreThermalSolver.cpp
    TyreCarcassLattice.cpp
    InductionCharger.cpp
    PhysicsThread.cpp
    Adapter/NullPhysicsAdapter.cpp
    ProjectDrive/Aerodynamics.cpp
    ProjectDrive/Drivetrain.cpp
    ProjectDrive/PacejkaMagicFormula.cpp
    ProjectDrive/TyreSlipDynamics.cpp
    ProjectDrive/TyreVisualBridge.cpp
    ProjectDrive/VehicleGeometry.cpp
    ProjectDrive/VehicleSolver.cpp)
target_compile_features(FrontierVehiclePhysics PUBLIC cxx_std_17)
target_include_directories(FrontierVehiclePhysics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(FrontierVehiclePhysics PUBLIC FrontierTyreContinuum)
if(Threads_FOUND)
    target_link_libraries(FrontierVehiclePhysics PUBLIC Threads::Threads)
endif()
if(MSVC)
    target_compile_options(FrontierVehiclePhysics PRIVATE /utf-8 /W4)
else()
    target_compile_options(FrontierVehiclePhysics PRIVATE -Wall -Wextra -Werror)
endif()

# 📝 Optional Jolt Physics 5.2.0 backend: FRONTIER_JOLT_SOURCE_DIR points at the unpacked JoltPhysics-5.2.0 tree
#    (include root); the caller provides the Jolt target/library. Without it, drives use NullPhysicsAdapter.
option(FRONTIER_WITH_JOLT "Build the Jolt 5.2.0 adapter backend" OFF)
set(FRONTIER_JOLT_SOURCE_DIR "" CACHE PATH "JoltPhysics-5.2.0 source tree (include root)")
if(FRONTIER_WITH_JOLT)
    if(NOT FRONTIER_JOLT_SOURCE_DIR)
        message(FATAL_ERROR "FRONTIER_WITH_JOLT requires FRONTIER_JOLT_SOURCE_DIR (the JoltPhysics-5.2.0 tree)")
    endif()
    add_library(FrontierJoltAdapter STATIC Adapter/JoltAdapter.cpp)
    target_compile_features(FrontierJoltAdapter PUBLIC cxx_std_17)
    target_include_directories(FrontierJoltAdapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    target_include_directories(FrontierJoltAdapter PRIVATE "${FRONTIER_JOLT_SOURCE_DIR}")
    target_link_libraries(FrontierJoltAdapter PUBLIC FrontierVehiclePhysics)
    if(MSVC)
        target_compile_options(FrontierJoltAdapter PRIVATE /utf-8 /W4)
    else()
        target_compile_options(FrontierJoltAdapter PRIVATE -Wall -Wextra -Werror)
    endif()
endif()

# 📝 Optional caller-owned Vulkan command recorder; the CPU tyre library has no Vulkan dependency.
if(FRONTIER_TYRE_VULKAN_HEADERS)
    add_library(FrontierTyreProjectionVulkan STATIC TyreProjectionVulkan.cpp)
    target_compile_features(FrontierTyreProjectionVulkan PUBLIC cxx_std_17)
    target_include_directories(FrontierTyreProjectionVulkan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
        "${FRONTIER_TYRE_VULKAN_HEADERS}")
    if(MSVC)
        target_compile_options(FrontierTyreProjectionVulkan PRIVATE /utf-8 /W4)
    else()
        target_compile_options(FrontierTyreProjectionVulkan PRIVATE -Wall -Wextra -Werror)
    endif()
endif()
