#============================================================================================================================================
# Frontier/CMakeLists.txt — Linux/Cross-Platform CMake (for IDE integration and Linux builds only)
#   Windows builds use Projects/Project-Zero/Build/ToolchainSequence.ps1 (MSVC direct toolchain).
#   Dependencies: python3 Tools/Bootstrap.py. Engine/editor sources are already consolidated.
#============================================================================================================================================

cmake_minimum_required(VERSION 3.20)
project(FrontierEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(FRONTIER_FLUID_ONLY "Build Flux/Ripple and scene adapter tests without external dependencies" OFF)
if(FRONTIER_FLUID_ONLY)
    enable_testing()
    add_subdirectory(Projects/Project-Fluid)
    return()
endif()
option(FRONTIER_PROOF_ONLY "Build native editor/renderer verification without a GPU SDK" OFF)
if(FRONTIER_PROOF_ONLY)
    include(Tools/Build/NativeProof.cmake)
    return()
endif()

# ─── Editor authoring tools ────────────────────────────────────────────────────────────────────────────────────────────────────────────────
# Standalone C++ tools live outside the runtime Project-Zero source batch. SolidArc is the first modelling/CAD tool;
# future siblings sit beside it under Editor/AuthoringTools/{TexturePainting,Baking,Terrain}.
add_subdirectory(Editor/AuthoringTools/Modelling/SolidArc EXCLUDE_FROM_ALL)
# ─── Reject Windows CMake builds — use ToolchainSequence.ps1 instead ────────────────────────────────────────────────────────────────────────
if(WIN32 AND MSVC)
    message(FATAL_ERROR
        "[Frontier] Windows builds must use the direct MSVC toolchain:\n"
        "  pwsh -File Projects\\Project-Zero\\Build\\ToolchainSequence.ps1\n"
        "CMake is supported on Linux / macOS only.")
endif()

# ─── ExternalPackages roots ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────
set(EXT "${CMAKE_SOURCE_DIR}/ExternalPackages")

# Dependencies are locked archives, not submodules.
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Tools/Bootstrap.py" --check
    RESULT_VARIABLE FRONTIER_DEPENDENCIES_READY)
if(NOT FRONTIER_DEPENDENCIES_READY EQUAL 0)
    message(FATAL_ERROR "Run: python3 Tools/Bootstrap.py")
endif()

# ─── ImGui patches — Frontier's divergence, applied before anything is compiled ────────────────────────────────────────────────────────────────
# Configure time, not build time: the patched headers declare the style members Frontier's sources reference, so a
#    build that compiled first and patched later would fail on the first translation unit. The script is
#    idempotent, so re-running cmake on an already-patched tree is a no-op.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/Tools/Build/ApplyImGuiPatches.py")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Tools/Build/ApplyImGuiPatches.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE IMGUI_PATCH_RESULT
        OUTPUT_VARIABLE IMGUI_PATCH_OUTPUT
        ERROR_VARIABLE  IMGUI_PATCH_OUTPUT)
    if(NOT IMGUI_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "[Frontier] ApplyImGuiPatches.py failed; refusing to configure against a half-patched ImGui:\n${IMGUI_PATCH_OUTPUT}")
    endif()
    message(STATUS "[Frontier] ImGui patches applied")
else()
    message(WARNING "[Frontier] Python3 or Tools/Build/ApplyImGuiPatches.py absent - building against pristine ImGui")
endif()

# ─── GLFW — built from source submodule (Linux static) ──────────────────────────────────────────────────────────────────────────────────────
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
add_subdirectory("${EXT}/glfw" "${CMAKE_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)

# ThorVG software artwork: pinned, shared RGBA for CPU and device presentation.
include(Tools/Build/ThorVGSoftware.cmake)

# ─── ImGui — compiled as object lib (docking branch) ────────────────────────────────────────────────────────────────────────────────────────
set(IMGUI_SRC "${EXT}/imgui")
set(IMGUI_SOURCES
    "${IMGUI_SRC}/imgui.cpp"
    "${IMGUI_SRC}/imgui_draw.cpp"
    "${IMGUI_SRC}/imgui_tables.cpp"
    "${IMGUI_SRC}/imgui_widgets.cpp"
    "${IMGUI_SRC}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_SRC}/backends/imgui_impl_vulkan.cpp"
)

# ─── Vulkan ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)

# ─── Shader lowering — GLSL-in-.slang → SPIR-V (one entry per stage; mirrors $ShaderTable in ToolchainSequence.ps1) ─────────────────
find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/bin" "/usr/bin" "/usr/local/bin")
find_program(SLANGC_EXECUTABLE slangc HINTS "$ENV{VULKAN_SDK}/bin" "/usr/bin" "/usr/local/bin")

set(SHADER_ROOT "${CMAKE_SOURCE_DIR}/Engine/Shaders")
set(SHADER_INCLUDES "${SHADER_ROOT}/PresentationDither.slang" "${SHADER_ROOT}/WeatherMedia.slang" "${SHADER_ROOT}/CloudShadow.slang" "${SHADER_ROOT}/SkyRecords.slang" "${SHADER_ROOT}/MoonRecords.slang" "${SHADER_ROOT}/PostRecords.slang" "${CMAKE_SOURCE_DIR}/Engine/DisplayPresentation/LensFlareKernel.h" "${SHADER_ROOT}/SceneRecords.slang" "${SHADER_ROOT}/RayGeneration.slang" "${SHADER_ROOT}/TraversalCWBVH.slang"
                    "${SHADER_ROOT}/InterfaceRecords.slang" "${SHADER_ROOT}/InterfaceSignedDistance.slang"
                    "${SHADER_ROOT}/ShadowRecords.slang" "${SHADER_ROOT}/ShadowSample.slang"
                    "${SHADER_ROOT}/OutlineRecords.slang" "${SHADER_ROOT}/GizmoRecords.slang")
set(SHADER_TABLE
    "FluidExtract.slang|compute|FluidExtract.spv"
    "../../Projects/Project-Fluid/Shaders/ParticleClear.slang|compute|FluidParticleClear.spv"
    "../../Projects/Project-Fluid/Shaders/ParticleSplat.slang|compute|FluidParticleSplat.spv"
    "../../Projects/Project-Fluid/Shaders/SurfaceResolve.slang|compute|FluidSurfaceResolve.spv"
    "ReSTIRViewport.slang|compute|ReSTIRViewport.spv"
    "ClusterCull.slang|compute|ClusterCull.spv"
    "HiZReduce.slang|compute|HiZReduce.spv"
    "AtrousDenoise.slang|compute|AtrousDenoise.spv"
    "LuminanceReduce.slang|compute|LuminanceReduce.spv"
    "SurfaceResolve.slang|compute|SurfaceResolve.spv"
    "ShadowResolve.slang|compute|ShadowResolve.spv"
    "ShadowRaster.vert.slang|vertex|ShadowRaster.vert.spv"
    "ShadowRaster.frag.slang|fragment|ShadowRaster.frag.spv"
    "VisibilityRaster.vert.slang|vertex|VisibilityRaster.vert.spv"
    "VisibilityRaster.frag.slang|fragment|VisibilityRaster.frag.spv"
    "InterfaceRaster.vert.slang|vertex|InterfaceRaster.vert.spv"
    "InterfaceRaster.frag.slang|fragment|InterfaceRaster.frag.spv"
    "BlasRefit.slang|compute|BlasRefit.spv"
    "BlasBuild.slang|compute|BlasBuild.spv"
    "SelectionOutline.slang|compute|SelectionOutline.spv"
    "GizmoRaster.vert.slang|vertex|GizmoRaster.vert.spv"
    "GizmoRaster.frag.slang|fragment|GizmoRaster.frag.spv")

set(SPV_OUTPUTS)
if(GLSLC_EXECUTABLE OR SLANGC_EXECUTABLE)
    foreach(ENTRY IN LISTS SHADER_TABLE)
        string(REPLACE "|" ";" FIELDS "${ENTRY}")
        list(GET FIELDS 0 SRC_NAME)
        list(GET FIELDS 1 STAGE)
        list(GET FIELDS 2 OUT_NAME)
        set(SRC "${SHADER_ROOT}/${SRC_NAME}")
        set(OUT "${SHADER_ROOT}/${OUT_NAME}")
        if(GLSLC_EXECUTABLE)
            # glslc needs a recognised extension: stage the source under a .glsl name in the build tree
            set(STAGED "${CMAKE_BINARY_DIR}/Shaders/${OUT_NAME}.glsl")
            add_custom_command(
                OUTPUT  "${OUT}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/Shaders"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${STAGED}"
                COMMAND "${GLSLC_EXECUTABLE}" "-DFRONTIER_SHADER_TOOLCHAIN=1" "-I${CMAKE_SOURCE_DIR}/Engine" "-I${SHADER_ROOT}"
                        --target-env=vulkan1.2 "-fshader-stage=${STAGE}" -o "${OUT}" "${STAGED}"
                DEPENDS "${SRC}" ${SHADER_INCLUDES}
                COMMENT "[Project-Zero] Lowering ${SRC_NAME} -> ${OUT_NAME} (${STAGE})"
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT  "${OUT}"
                COMMAND "${SLANGC_EXECUTABLE}" "${SRC}" "-DFRONTIER_SHADER_TOOLCHAIN=1" "-I${CMAKE_SOURCE_DIR}/Engine" "-I${SHADER_ROOT}"
                        -target spirv -profile glsl_450 -stage ${STAGE} -entry main -o "${OUT}"
                DEPENDS "${SRC}" ${SHADER_INCLUDES}
                COMMENT "[Project-Zero] Lowering ${SRC_NAME} -> ${OUT_NAME} (${STAGE})"
                VERBATIM)
        endif()
        list(APPEND SPV_OUTPUTS "${OUT}")
    endforeach()
    add_custom_target(ReSTIRViewportSpirv DEPENDS ${SPV_OUTPUTS})
else()
    message(WARNING "[Project-Zero] neither glslc nor slangc found - Engine/Shaders/*.spv must be pre-compiled.")
    add_custom_target(ReSTIRViewportSpirv)
endif()
set(FRONTIER_ENGINE_SOURCES
    Engine/DeviceExchange/OrientationClassifier.cpp
    Engine/DeviceExchange/InputExchange.cpp
    Engine/DeviceExchange/DiagnosticMetrics.cpp
    Engine/DeviceExchange/TelemetryProbe.cpp   # dev/debug-only in-RAM probe; empty TU unless FRONTIER_DEVELOPMENT/FRONTIER_DEBUG
    Engine/GeometricRaster/GeometryStructure.cpp
    Engine/GeometricRaster/CameraProjection.cpp
    Engine/GeometricRaster/SceneStructure.cpp
    Engine/GeometricRaster/TraversalIndex.cpp
    Engine/GeometricRaster/InstanceAcceleration.cpp
    Engine/GeometricRaster/BlasBuildMirror.cpp
    Engine/GeometricRaster/BlasDevicePayload.cpp
    Engine/GeometricRaster/BlasBuildPipeline.cpp   # D9b: the Vulkan half (pipelines, dispatch plan, readback verification)
    Engine/ContentInterchange/MaterialIndex.cpp
    Engine/ContentInterchange/MaterialCodec.cpp
    Engine/ContentInterchange/TextureIndex.cpp
    Engine/ContentInterchange/SceneCodec.cpp
    Engine/ContentInterchange/ShaderBallStructure.cpp
    Engine/ContentInterchange/ShowcaseStructure.cpp
    Engine/ContentInterchange/AssetResolution.cpp
    Engine/ContentInterchange/MaterialSwatchStructure.cpp
    Engine/ContentInterchange/FbxCodec.cpp
    Engine/ContentInterchange/ObjCodec.cpp
    Engine/ContentInterchange/ContentCodec.cpp
    Engine/ContentInterchange/UfbxTranslation.cpp
    Engine/ContentInterchange/SpaceCodec.cpp      # P1/P3: the Space container family's reader, writer and the two tools
    Engine/ContentInterchange/SpaceExport.cpp     # P2/P4/P6: resident records → containers
    Engine/DisplayPresentation/ThemeStructure.cpp
    Engine/DisplayPresentation/IconArt.cpp
    Engine/DisplayPresentation/IconPresentation.cpp
    Engine/DisplayPresentation/VectorCodec.cpp
    Engine/DisplayPresentation/FontCodec.cpp
    Engine/DisplayPresentation/ConfigurationRegistry.cpp
    Engine/DisplayPresentation/ControlCentreHost.cpp
    Engine/DisplayPresentation/PixelSpace.cpp
    Engine/DisplayPresentation/MotionIntegrator.cpp
    Engine/DisplayPresentation/GlyphSpace.cpp
    Engine/DisplayPresentation/NotificationQueue.cpp
    Engine/DisplayPresentation/TelemetryMetrics.cpp
    Engine/DisplayPresentation/ControlKit.cpp
    Engine/DisplayPresentation/TextEntryState.cpp
    Engine/Editor/EditorHost.cpp
    Engine/Editor/OutlinerPanel.cpp
    Engine/Editor/ViewportPanel.cpp
    Engine/Editor/InspectorPanel.cpp
    Engine/Editor/SunInspectorPanel.cpp
    Engine/Editor/LensFlareInspectorPanel.cpp
    Engine/Editor/AtmosphereSkyInspectorPanel.cpp
    Engine/Editor/MoonInspectorPanel.cpp
    Engine/Editor/StarsInspectorPanel.cpp
    Engine/Editor/CloudsInspectorPanel.cpp
    Engine/Editor/FogInspectorPanel.cpp
    Engine/Editor/WeatherInspectorPanel.cpp
    Engine/Editor/CameraInspectorPanel.cpp

    Engine/Editor/SunInspectorPanel.cpp
    Engine/Editor/LensFlareInspectorPanel.cpp
    Engine/Editor/AtmosphereSkyInspectorPanel.cpp
    Engine/Editor/MoonInspectorPanel.cpp
    Engine/Editor/StarsInspectorPanel.cpp
    Engine/Editor/CloudsInspectorPanel.cpp
    Engine/Editor/CameraInspectorPanel.cpp
    Engine/Editor/WeatherInspectorPanel.cpp
    Engine/Editor/FogInspectorPanel.cpp
    Engine/Editor/GizmoFigures.cpp
    Engine/DisplayPresentation/ExposureIntegrator.cpp
    Engine/DisplayPresentation/DialogueHost.cpp
    Engine/DisplayPresentation/AppearanceInspector.cpp
    Engine/DisplayPresentation/ConfigurationInspector.cpp
    Engine/DisplayPresentation/MaterialInspector.cpp
    Engine/DisplayPresentation/TypefaceRegistry.cpp
    Engine/DisplayPresentation/FidelityClassifier.cpp
    Engine/SpatialInterface/InterfaceStructure.cpp
    Engine/SpatialInterface/InterfaceSequence.cpp
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp
    Engine/SpatialInterface/PaletteConfiguration.cpp
    Engine/SpatialInterface/InterfacePointerProjection.cpp
    Engine/SpatialInterface/InterfaceTextProjection.cpp
    Engine/SpatialInterface/InterfaceScreenSequence.cpp
    Engine/SpatialInterface/InterfaceVectorCodec.cpp
    Engine/SpatialInterface/InterfaceLightProjection.cpp
)

# ─── Engine common include dirs ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
set(FRONTIER_ENGINE_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine
    ${EXT}/tomlpp/include
    ${EXT}/jolt
)

#============================================================================================================================================
# Project-Zero — Windowed ReSTIR GI Renderer (Vulkan + GLFW + ImGui + ThorVG + Slang)
#============================================================================================================================================

set(PROJECT_ZERO_SOURCES
    Projects/Project-Fluid/Source/SurfaceReconstruction.cpp
    Projects/Project-Fluid/Source/GpuSurfaceData.cpp
    Projects/Project-Fluid/Source/GpuSurfaceExtractor.cpp
    Projects/Project-Fluid/Source/FluidGpuTest.cpp
    Projects/Project-Fluid/Source/VulkanFluidMain.cpp
    Projects/Project-Zero/Source/WaterBodySequence.cpp
    Projects/Project-Fluid/Source/PbfFluid.cpp
    Projects/Project-Fluid/Source/PondWave.cpp
    ${FRONTIER_ENGINE_SOURCES}
    Engine/DeviceExchange/SwapchainExchange.cpp
    Engine/DeviceExchange/RayTracingCapabilitySet.cpp
    Engine/DeviceExchange/VisibilityExchange.cpp
    Engine/DeviceExchange/InterfaceExchange.cpp
    Engine/DeviceExchange/GizmoExchange.cpp
    Projects/Project-Zero/Source/InterfaceTrialSequence.cpp
    Projects/Project-Zero/Source/InstanceMotionSequence.cpp
    Projects/Project-Zero/Source/PerformanceTelemetrySequence.cpp
    Projects/Project-Zero/Source/PhysicsInstanceSequence.cpp
    Projects/Project-Zero/Source/InterfaceAudioSequence.cpp
    Projects/Project-Dyno/Source/CrankClickIntegrator.cpp
    Projects/Project-Dyno/Source/DynoSequence.cpp
    Engine/PlatformInterchange/AudioExchange.cpp
    Engine/PlatformInterchange/MiniaudioTranslation.cpp
    Engine/PlatformInterchange/WaveCodec.cpp
    Engine/PhysicalDynamics/RigidBodySolver.cpp
    Projects/Project-Zero/Source/CelestialSequence.cpp
    Projects/Project-Zero/Source/CommandLine.cpp   # P4: the launch line both hosts parse (-Project/-Level/-Build/-Config/+Location)
    Projects/Project-Zero/Source/ShowroomStructure.cpp
    Engine/DisplayPresentation/DiagnosticInspector.cpp
    Engine/DisplayPresentation/ReSTIRIntegrator.cpp
    Engine/DisplayPresentation/ShadingTableCodec.cpp
    Engine/ContentInterchange/ShaderballPreview.cpp
    Engine/DisplayPresentation/RenderScheduler.cpp
    Projects/Project-Zero/Source/RayTracingSolver.cpp
    Projects/Project-Zero/Source/FlyThroughSolver.cpp
    Projects/Project-Zero/Source/GameExecution.cpp
    ${IMGUI_SOURCES}
)

# Fail fast with NAMES if the source list ever rots again, mirroring the guard in
#    Projects/Project-Zero/Build/ToolchainSequence.ps1. Without it a stale entry surfaces as a wall of cascading
#    compiler errors (was: 73 C1083s on Windows, 2026-09-04) instead of one legible message. Only repo-relative
#    paths are checked; ${...} entries are generator expressions or submodule paths resolved elsewhere.
set(FRONTIER_MISSING_SOURCES "")
foreach(FrontierSource IN LISTS PROJECT_ZERO_SOURCES)
    if(NOT FrontierSource MATCHES "^\\$" AND NOT IS_ABSOLUTE "${FrontierSource}")
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${FrontierSource}")
            list(APPEND FRONTIER_MISSING_SOURCES "${FrontierSource}")
        endif()
    endif()
endforeach()
if(FRONTIER_MISSING_SOURCES)
    string(REPLACE ";" "\n    " FRONTIER_MISSING_REPORT "${FRONTIER_MISSING_SOURCES}")
    message(FATAL_ERROR "missing source files in the translation batch:\n    ${FRONTIER_MISSING_REPORT}")
endif()

add_executable(Project-Zero ${PROJECT_ZERO_SOURCES})
# Ship SVG sources + vector compatibility artwork; never offline baked icon pixels.
file(GLOB FRONTIER_ICON_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/EngineContent/Icons/*.svg")
add_custom_command(TARGET Project-Zero POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Project-Zero>/EngineContent/Icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FRONTIER_ICON_SOURCES} "$<TARGET_FILE_DIR:Project-Zero>/EngineContent/Icons"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/EngineContent/Icons/ThorVG" "$<TARGET_FILE_DIR:Project-Zero>/EngineContent/Icons/ThorVG"
    COMMENT "Staging ThorVG SVG icons" VERBATIM)

target_compile_definitions(Project-Zero PRIVATE PROJECT_FLUID_EMBEDDED)
add_dependencies(Project-Zero ReSTIRViewportSpirv)
set_source_files_properties(Engine/GeometricRaster/TraversalIndex.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-msse4.2")   # tinybvh SIMD build
set_source_files_properties(Engine/GeometricRaster/InstanceAcceleration.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-msse4.2")   # D6/D9: tiny_bvh.h's SIMD types, and the same flags the traversal batch is compiled with
set_source_files_properties(Engine/ContentInterchange/ShaderballPreview.cpp PROPERTIES COMPILE_DEFINITIONS SHADERBALL_PREVIEW_LIB
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/Engine/DisplayPresentation;${CMAKE_CURRENT_SOURCE_DIR}/Engine/Shaders;${CMAKE_CURRENT_SOURCE_DIR}/Engine/ContentInterchange")   # M7b preview TU (FRONTIER_CPU_PORT is vestigial — no ifdefs — and intentionally not passed; was Exhibits/Workbench/Materials/ShaderballExhibit.cpp — the app must not reach into the workbench)

target_include_directories(Project-Zero PRIVATE
    ${FRONTIER_ENGINE_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Zero/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Dyno/Source
    ${EXT}/miniaudio
    ${Vulkan_INCLUDE_DIRS}
    ${IMGUI_SRC}
    ${IMGUI_SRC}/backends
    ${EXT}/glfw/include
    ${THORVG_SRC}/inc
    ${EXT}/cgltf
    ${EXT}/tinybvh
    ${EXT}/stb
    ${EXT}/ufbx
    ${EXT}/fast_obj
)

target_link_libraries(Project-Zero PRIVATE Jolt)   # D4: rigid bodies drive instance transforms
set_source_files_properties(Engine/PlatformInterchange/MiniaudioTranslation.cpp PROPERTIES COMPILE_OPTIONS "-w")   # third-party TU
target_compile_definitions(Project-Zero PRIVATE
    FRONTIER_DEVELOPMENT
    FRONTIER_ENABLE_GLFW
)

target_link_libraries(Project-Zero PRIVATE
    ${Vulkan_LIBRARIES}
    glfw
    thorvg_static
    pthread dl
)
if(WIN32)
    target_link_libraries(Project-Zero PRIVATE psapi)   # TelemetryMetrics "Show RAM Usage" (GetProcessMemoryInfo)
endif()

#============================================================================================================================================
# Project-Zero-CpuReference — Headless CPU reference (sandbox stand-in; the product renderer is the Vulkan + Slang
#   stack built by Projects/Project-Zero/Build/ToolchainSequence.ps1). Explicit file list: never glob Source/ —
#   GameExecution.cpp (the Vulkan entry point) lives there.
#============================================================================================================================================

set(PROJECT_ZERO_CPU_SOURCES
    Projects/Project-Zero/Source/CpuReferenceMain.cpp
    Projects/Project-Zero/Source/RendererHost.h
    Projects/Project-Zero/Source/RendererHost.cpp
    Projects/Project-Zero/Source/RayTracingSolver.h
    Projects/Project-Zero/Source/RayTracingSolver.cpp
    Projects/Project-Zero/Source/SkyFogIntegrator.h
    Projects/Project-Zero/Source/SkyFogIntegrator.cpp
    Projects/Project-Zero/Source/TracingIndex.h
    Engine/GeometricRaster/CameraProjection.h
    Engine/GeometricRaster/CameraProjection.cpp
    Engine/DeviceExchange/OrientationClassifier.h
    Engine/DeviceExchange/OrientationClassifier.cpp
    Engine/DeviceExchange/DiagnosticMetrics.h
    Engine/DeviceExchange/DiagnosticMetrics.cpp
    Engine/DeviceExchange/TriangleSpan.h
)

add_executable(Project-Zero-CpuReference ${PROJECT_ZERO_CPU_SOURCES})
target_include_directories(Project-Zero-CpuReference PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Zero)

if(NOT MSVC)
    target_link_libraries(Project-Zero-CpuReference PRIVATE pthread)
endif()

#============================================================================================================================================
# Project-Dyno — Windowless dyno cell (miniaudio transport + powertrain acoustics; no Vulkan / GLFW / ImGui)
#   Self-contained on purpose: it lists exactly the TUs it needs so it builds even when the engine-wide lists above rot.
#============================================================================================================================================

set(PROJECT_DYNO_SOURCES
    Engine/DeviceExchange/DiagnosticMetrics.cpp
    Engine/PlatformInterchange/MiniaudioTranslation.cpp
    Engine/PlatformInterchange/WaveCodec.cpp
    Engine/PlatformInterchange/AudioExchange.cpp
    Projects/Project-Dyno/Source/DynoSequence.cpp
    Projects/Project-Dyno/Source/CrankClickIntegrator.cpp
    Projects/Project-Dyno/Source/GameExecution.cpp
)
add_executable(Project-Dyno ${PROJECT_DYNO_SOURCES})
set_source_files_properties(Engine/PlatformInterchange/MiniaudioTranslation.cpp PROPERTIES COMPILE_OPTIONS "-w")   # third-party TU
target_include_directories(Project-Dyno PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine
    ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Dyno/Source
    ${EXT}/miniaudio
    ${EXT}/tomlpp/include
    ${EXT}/stb
)
target_compile_definitions(Project-Dyno PRIVATE FRONTIER_DEVELOPMENT)
target_compile_options(Project-Dyno PRIVATE -Wall -Wextra)
target_link_libraries(Project-Dyno PRIVATE pthread dl m)

#============================================================================================================================================
# FrontierEngine demo target (Linux only)
#============================================================================================================================================

add_executable(FrontierEngine
    ${FRONTIER_ENGINE_SOURCES}
)
target_include_directories(FrontierEngine PRIVATE ${FRONTIER_ENGINE_INCLUDES})
target_link_libraries(FrontierEngine PRIVATE pthread)

#============================================================================================================================================
# Project-F20 — Racing Game   (🚧 removed 2026-09-05: Projects/Project-F20/ does not exist in this tree)
#============================================================================================================================================
# All five of its sources were absent, so `cmake` failed at configure time before reaching any real target. The
#    target is dropped rather than commented out; restore it from history if the project comes back.

#============================================================================================================================================
# Jolt Physics — compiled from the submodule as a static library, rigid-body subset (no GPU compute back ends / hair shaders)
#   Flags mirror Tools/Build/BuildJolt.sh. Jolt derives JPH_USE_* / JPH_DEBUG from the compiler flags and RegisterTypes() aborts on a
#   library/client mismatch, so FRONTIER_PHYSICS_FLAGS is applied to every target that includes <Jolt/Jolt.h> as well.
#============================================================================================================================================

set(FRONTIER_PHYSICS_FLAGS -mavx -mpopcnt -mfpmath=sse)

file(GLOB_RECURSE JOLT_SOURCES CONFIGURE_DEPENDS "${EXT}/jolt/Jolt/*.cpp")
list(FILTER JOLT_SOURCES EXCLUDE REGEX "/Compute/(CPU|DX12|VK|MTL)/")
list(FILTER JOLT_SOURCES EXCLUDE REGEX "/Shaders/(HairWrapper|TestComputeWrapper)\\.cpp$")

add_library(Jolt STATIC ${JOLT_SOURCES})
target_include_directories(Jolt PUBLIC "${EXT}/jolt")
target_compile_options(Jolt PUBLIC ${FRONTIER_PHYSICS_FLAGS} -pthread)
target_compile_options(Jolt PRIVATE -Wall -Wno-psabi -Wno-stringop-overflow)
target_compile_definitions(Jolt PUBLIC "$<$<NOT:$<CONFIG:Debug>>:NDEBUG>")   # Release → JPH_DEBUG off; Debug → asserts on (both sides)
target_link_libraries(Jolt PUBLIC pthread)

#============================================================================================================================================
# Project-Physics — Jolt rigid-body testbed (console; plane + falling boxes / spheres / capsules ticked from the main loop)
#============================================================================================================================================
# Deliberately does NOT link FrontierEngine: the testbed is a console proof of the RigidBodySolver seam and needs neither Vulkan nor
#    GLFW, so it stays buildable when the graphics submodules are uninitialised.

add_executable(Project-Physics
    Engine/DeviceExchange/OrientationClassifier.cpp
    Engine/DeviceExchange/DiagnosticMetrics.cpp
    Engine/PhysicalDynamics/RigidBodySolver.cpp
    Projects/Project-Physics/Source/DropSceneStructure.cpp
    Projects/Project-Physics/Source/GameExecution.cpp
)
target_include_directories(Project-Physics PRIVATE
    ${FRONTIER_ENGINE_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Physics/Source
)
target_compile_definitions(Project-Physics PRIVATE FRONTIER_DEVELOPMENT "$<$<CONFIG:Debug>:FRONTIER_DEBUG=1>")
target_compile_options(Project-Physics PRIVATE -Wall -Wextra -Wno-psabi)
target_link_libraries(Project-Physics PRIVATE Jolt pthread)

# Standalone fluid research host alongside the native application.
add_subdirectory(Projects/Project-Fluid)
if(FRONTIER_FLUID_OPENMP)
    find_package(OpenMP REQUIRED COMPONENTS CXX)
    # Project-Zero compiles the fluid sources directly, rather than linking Core.
    target_link_libraries(Project-Zero PRIVATE OpenMP::OpenMP_CXX)
endif()
