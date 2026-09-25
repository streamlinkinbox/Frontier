cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ProjectFluid LANGUAGES CXX)
    enable_testing()
endif()
get_filename_component(FRONTIER_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_library(ProjectFluidCore STATIC
    Source/PbfFluid.cpp Source/PbfFluid.h
    Source/SurfaceReconstruction.cpp Source/SurfaceReconstruction.h
    Source/AnisotropicSurfaceMesh.cpp Source/AnisotropicSurfaceMesh.h
    Source/PondWave.cpp Source/PondWave.h
    Source/PondBody.cpp Source/PondBody.h
    Source/MarchingCubesTables.h)
target_include_directories(ProjectFluidCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_features(ProjectFluidCore PUBLIC cxx_std_20)

add_executable(Project-Fluid-CPU Source/CpuProofMain.cpp)
target_link_libraries(Project-Fluid-CPU PRIVATE ProjectFluidCore)
add_executable(Project-Fluid-Surface-Test Source/SurfaceMeshTest.cpp)
target_link_libraries(Project-Fluid-Surface-Test PRIVATE ProjectFluidCore)
add_executable(Project-Fluid-Pond-CPU Source/PondProofMain.cpp)
target_link_libraries(Project-Fluid-Pond-CPU PRIVATE ProjectFluidCore)
add_executable(Project-Fluid-Pond-Test Source/PondWaveTest.cpp)
target_link_libraries(Project-Fluid-Pond-Test PRIVATE ProjectFluidCore)

option(FRONTIER_BUILD_PROJECT_FLUID_VULKAN "Build the interactive Vulkan Flux window" OFF)
if(FRONTIER_BUILD_PROJECT_FLUID_VULKAN)
    find_package(Vulkan QUIET)
    find_package(glfw3 QUIET)
    if(NOT TARGET glfw AND NOT TARGET glfw3::glfw AND EXISTS "${FRONTIER_SOURCE_ROOT}/ExternalPackages/glfw/CMakeLists.txt")
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${FRONTIER_SOURCE_ROOT}/ExternalPackages/glfw" "${CMAKE_CURRENT_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)
    endif()
    find_program(PROJECT_FLUID_GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    find_program(PROJECT_FLUID_SLANGC NAMES slangc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

    if(Vulkan_FOUND AND (PROJECT_FLUID_GLSLC OR PROJECT_FLUID_SLANGC) AND (TARGET glfw OR TARGET glfw3::glfw))
        set(SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/Shaders)
        file(MAKE_DIRECTORY ${SHADER_DIR})
        set(CLEAR_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ParticleClear.slang)
        set(SPLAT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ParticleSplat.slang)
        set(RESOLVE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/SurfaceResolve.slang)
        set(CLEAR_SPV ${SHADER_DIR}/ParticleClear.spv)
        set(SPLAT_SPV ${SHADER_DIR}/ParticleSplat.spv)
        set(RESOLVE_SPV ${SHADER_DIR}/SurfaceResolve.spv)

        if(PROJECT_FLUID_GLSLC)
            # Frontier stores GLSL compute sources as .slang; stage a recognized
            # extension exactly as the engine-wide shader table does.
            set(CLEAR_STAGED ${SHADER_DIR}/ParticleClear.comp.glsl)
            set(SPLAT_STAGED ${SHADER_DIR}/ParticleSplat.comp.glsl)
            set(RESOLVE_STAGED ${SHADER_DIR}/SurfaceResolve.comp.glsl)
            add_custom_command(OUTPUT ${CLEAR_SPV}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CLEAR_SOURCE} ${CLEAR_STAGED}
                COMMAND ${PROJECT_FLUID_GLSLC} --target-env=vulkan1.2 -fshader-stage=compute -O ${CLEAR_STAGED} -o ${CLEAR_SPV}
                DEPENDS ${CLEAR_SOURCE} VERBATIM)
            add_custom_command(OUTPUT ${SPLAT_SPV}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPLAT_SOURCE} ${SPLAT_STAGED}
                COMMAND ${PROJECT_FLUID_GLSLC} --target-env=vulkan1.2 -fshader-stage=compute -O ${SPLAT_STAGED} -o ${SPLAT_SPV}
                DEPENDS ${SPLAT_SOURCE} VERBATIM)
            add_custom_command(OUTPUT ${RESOLVE_SPV}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RESOLVE_SOURCE} ${RESOLVE_STAGED}
                COMMAND ${PROJECT_FLUID_GLSLC} --target-env=vulkan1.2 -fshader-stage=compute -O ${RESOLVE_STAGED} -o ${RESOLVE_SPV}
                DEPENDS ${RESOLVE_SOURCE} VERBATIM)
        else()
            add_custom_command(OUTPUT ${CLEAR_SPV}
                COMMAND ${PROJECT_FLUID_SLANGC} ${CLEAR_SOURCE} -target spirv -profile glsl_450 -stage compute -entry main -o ${CLEAR_SPV}
                DEPENDS ${CLEAR_SOURCE} VERBATIM)
            add_custom_command(OUTPUT ${SPLAT_SPV}
                COMMAND ${PROJECT_FLUID_SLANGC} ${SPLAT_SOURCE} -target spirv -profile glsl_450 -stage compute -entry main -o ${SPLAT_SPV}
                DEPENDS ${SPLAT_SOURCE} VERBATIM)
            add_custom_command(OUTPUT ${RESOLVE_SPV}
                COMMAND ${PROJECT_FLUID_SLANGC} ${RESOLVE_SOURCE} -target spirv -profile glsl_450 -stage compute -entry main -o ${RESOLVE_SPV}
                DEPENDS ${RESOLVE_SOURCE} VERBATIM)
        endif()
        add_custom_target(ProjectFluidShaders DEPENDS ${CLEAR_SPV} ${SPLAT_SPV} ${RESOLVE_SPV})

        add_executable(Project-Fluid Source/VulkanFluidMain.cpp)
        add_dependencies(Project-Fluid ProjectFluidShaders)
        target_link_libraries(Project-Fluid PRIVATE ProjectFluidCore Vulkan::Vulkan)
        if(TARGET glfw)
            target_link_libraries(Project-Fluid PRIVATE glfw)
        else()
            target_link_libraries(Project-Fluid PRIVATE glfw3::glfw)
        endif()
        target_compile_definitions(Project-Fluid PRIVATE PROJECT_FLUID_CLEAR_SPV="${CLEAR_SPV}" PROJECT_FLUID_SPLAT_SPV="${SPLAT_SPV}" PROJECT_FLUID_RESOLVE_SPV="${RESOLVE_SPV}")
    else()
        message(FATAL_ERROR "Project-Fluid Vulkan explicitly requested but unavailable: Vulkan SDK, glslc/slangc and GLFW3 are required. CPU proof remains available.")
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME ProjectFluidCpuPbf COMMAND Project-Fluid-CPU)
    add_test(NAME ProjectFluidWatertightSurface COMMAND Project-Fluid-Surface-Test)
    add_test(NAME ProjectFluidPondWave COMMAND Project-Fluid-Pond-Test)
endif()

add_executable(Project-Fluid-Bridge-Test
    Source/SceneBridgeTest.cpp
    ${FRONTIER_SOURCE_ROOT}/Projects/Project-Zero/Source/WaterBodySequence.cpp
    ${FRONTIER_SOURCE_ROOT}/Engine/GeometricRaster/GeometryStructure.cpp
    ${FRONTIER_SOURCE_ROOT}/Engine/GeometricRaster/SceneStructure.cpp
    ${FRONTIER_SOURCE_ROOT}/Engine/ContentInterchange/MaterialIndex.cpp
    ${FRONTIER_SOURCE_ROOT}/Engine/DeviceExchange/OrientationClassifier.cpp)
target_link_libraries(Project-Fluid-Bridge-Test PRIVATE ProjectFluidCore)
add_executable(Project-Fluid-Regression-Test Source/RegressionTest.cpp)
target_link_libraries(Project-Fluid-Regression-Test PRIVATE ProjectFluidCore)
if(BUILD_TESTING)
    add_test(NAME ProjectFluidSceneBridge COMMAND Project-Fluid-Bridge-Test)
    add_test(NAME ProjectFluidRegression COMMAND Project-Fluid-Regression-Test)
endif()
if(MSVC)
    target_compile_options(ProjectFluidCore PRIVATE /utf-8)
endif()
# Header-only Vulkan type counterparts for CPU scene records; no GPU calls are linked here.
target_include_directories(Project-Fluid-Bridge-Test PRIVATE ${FRONTIER_SOURCE_ROOT}/Exhibits/Workbench/Editor/Counterparts)
add_executable(Project-Fluid-Benchmark Source/Benchmark.cpp)
target_link_libraries(Project-Fluid-Benchmark PRIVATE ProjectFluidCore)
