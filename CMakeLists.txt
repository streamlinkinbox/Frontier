#============================================================================================================================================
# Frontier/CMakeLists.txt — Linux/Cross-Platform CMake (for IDE integration and Linux builds only)
#   Windows builds use Tools/Build/ToolchainSequence.ps1 (MSVC direct toolchain).
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
# The gas host, beside FRONTIER_FLUID_ONLY and for the same reason: the gas solver is header-only and needs
# no Vulkan, so a machine working on it should not have to satisfy a renderer to compile it.
option(FRONTIER_GAS_ONLY "Build the standalone gas host without a GPU SDK" OFF)
if(FRONTIER_GAS_ONLY)
    enable_testing()
    add_subdirectory(Projects/Project-Gas)
    return()
endif()
option(FRONTIER_PROOF_ONLY "Build native editor/renderer verification without a GPU SDK" OFF)
if(FRONTIER_PROOF_ONLY)
    include(Tools/Build/NativeProof.cmake)
    return()
endif()

# ─── Editor authoring tools ────────────────────────────────────────────────────────────────────────────────────────────────────────────────
# Standalone C++ tools live outside the runtime Frontier host source batch. SolidArc is the first modelling/CAD tool;
# future siblings sit beside it under Editor/AuthoringTools/{TexturePainting,Baking,Terrain}.
add_subdirectory(Editor/AuthoringTools/Modelling/SolidArc EXCLUDE_FROM_ALL)
# ─── Reject Windows CMake builds — use ToolchainSequence.ps1 instead ────────────────────────────────────────────────────────────────────────
if(WIN32 AND MSVC)
    message(FATAL_ERROR
        "[Frontier] Windows builds must use the direct MSVC toolchain:\n"
        "  pwsh -File Tools\\Build\\ToolchainSequence.ps1\n"
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

if(WIN32)
    list(APPEND IMGUI_SOURCES "${IMGUI_SRC}/backends/imgui_impl_win32.cpp" "${IMGUI_SRC}/backends/imgui_impl_dx11.cpp")
endif()

# ─── Vulkan ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)

# ─── Shader lowering — GLSL-in-.slang → SPIR-V (one entry per stage; mirrors $ShaderTable in ToolchainSequence.ps1) ─────────────────
find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/bin" "/usr/bin" "/usr/local/bin")
find_program(SLANGC_EXECUTABLE slangc HINTS "$ENV{VULKAN_SDK}/bin" "/usr/bin" "/usr/local/bin")

set(SHADER_ROOT "${CMAKE_SOURCE_DIR}/Engine/Shaders")
set(SHADER_INCLUDES "${SHADER_ROOT}/ViewportIntegrator.slang" "${SHADER_ROOT}/RayQueryTraversal.slang" "${SHADER_ROOT}/ReSTIRViewport.slang" "${SHADER_ROOT}/DistanceFieldGatherBody.slang" "${SHADER_ROOT}/DistanceFieldCaptureBody.slang" "${SHADER_ROOT}/DistanceFieldMaterial.slang" "${SHADER_ROOT}/DistanceFieldTransport.slang" "${SHADER_ROOT}/DistanceFieldGIResolveBody.slang" "${SHADER_ROOT}/SceneMaterialResolve.slang" "${SHADER_ROOT}/DistanceFieldRecords.slang" "${SHADER_ROOT}/GlobalDistanceField.slang" "${SHADER_ROOT}/SurfaceCacheRecords.slang" "${SHADER_ROOT}/PatchSelection.slang" "${SHADER_ROOT}/PatchPolicy.shared.h" "${SHADER_ROOT}/PresentationDither.slang" "${SHADER_ROOT}/WeatherMedia.slang" "${SHADER_ROOT}/CloudShadow.slang" "${SHADER_ROOT}/SkyRecords.slang" "${SHADER_ROOT}/MoonRecords.slang" "${SHADER_ROOT}/PostRecords.slang" "${CMAKE_SOURCE_DIR}/Engine/DisplayPresentation/LensFlareKernel.h" "${SHADER_ROOT}/SceneRecords.slang" "${SHADER_ROOT}/RayGeneration.slang" "${SHADER_ROOT}/TraversalCWBVH.slang"
                    "${SHADER_ROOT}/InterfaceRecords.slang" "${SHADER_ROOT}/InterfaceSignedDistance.slang"
                    "${SHADER_ROOT}/ShadowRecords.slang" "${SHADER_ROOT}/ShadowSample.slang"
                    "${SHADER_ROOT}/OutlineRecords.slang" "${SHADER_ROOT}/GizmoRecords.slang")
set(SHADER_TABLE
    "FluidExtract.slang|compute|FluidExtract.spv"
    "GasVolumeRaymarch.slang|compute|GasVolumeRaymarch.spv"
    "../../Projects/Project-Fluid/Shaders/ParticleClear.slang|compute|FluidParticleClear.spv"
    "../../Projects/Project-Fluid/Shaders/ParticleSplat.slang|compute|FluidParticleSplat.spv"
    "../../Projects/Project-Fluid/Shaders/SurfaceResolve.slang|compute|FluidSurfaceResolve.spv"
    "ReSTIRViewport.slang|compute|ReSTIRViewport.spv"
    "RasterViewport.slang|compute|RasterViewport.spv"
    "RayQueryViewport.slang|compute|RayQueryViewport.spv"
    "SurfelIrradianceUpdate.slang|compute|SurfelIrradianceUpdate.spv"
    "SurfelCommit.slang|compute|SurfelCommit.spv"
    "DistanceFieldConstruct.slang|compute|DistanceFieldConstruct.spv"
    "DistanceFieldCapture.slang|compute|DistanceFieldCapture.spv"
    "DistanceFieldCaptureFixed.slang|compute|DistanceFieldCaptureFixed.spv"
    "DistanceFieldRadiance.slang|compute|DistanceFieldRadiance.spv"
    "DistanceFieldGather.slang|compute|DistanceFieldGather.spv"
    "DistanceFieldGatherFixed.slang|compute|DistanceFieldGatherFixed.spv"
    "DistanceFieldGIResolveFixed.slang|compute|DistanceFieldGIResolveFixed.spv"
    "DistanceFieldGIResolve.slang|compute|DistanceFieldGIResolve.spv"
    "SurfelGIResolve.slang|compute|SurfelGIResolve.spv"
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
                COMMENT "[Frontier] Lowering ${SRC_NAME} -> ${OUT_NAME} (${STAGE})"
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT  "${OUT}"
                COMMAND "${SLANGC_EXECUTABLE}" "${SRC}" "-DFRONTIER_SHADER_TOOLCHAIN=1" "-I${CMAKE_SOURCE_DIR}/Engine" "-I${SHADER_ROOT}"
                        -target spirv -profile glsl_450 -stage ${STAGE} -entry main -o "${OUT}"
                DEPENDS "${SRC}" ${SHADER_INCLUDES}
                COMMENT "[Frontier] Lowering ${SRC_NAME} -> ${OUT_NAME} (${STAGE})"
                VERBATIM)
        endif()
        list(APPEND SPV_OUTPUTS "${OUT}")
    endforeach()
    add_custom_target(ReSTIRViewportSpirv DEPENDS ${SPV_OUTPUTS})
else()
    message(WARNING "[Frontier] neither glslc nor slangc found - Engine/Shaders/*.spv must be pre-compiled.")
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
    # ShowcaseStructure::BringGridMesh calls ShaderBallGeometry::LoadResolved — without this TU the
    #    exe link fails with an unresolved external.
    Engine/ContentInterchange/ShaderBallGeometry.cpp
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
    Engine/DisplayPresentation/SurfelReference.cpp
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
    Engine/Editor/ControlPanel.cpp              # the panel widgets every inspector draws through
    Engine/Editor/ShadeTick.cpp                 # EditorHost.h declares it
    Engine/DisplayPresentation/CelestialSolver.cpp   # sun, moon and air mass for the sky inspectors
    Engine/GeometricRaster/StarCatalogueIndex.cpp    # the star catalogue CelestialSequence loads
    Engine/Editor/OutlinerPanel.cpp
    Engine/Editor/ViewportPanel.cpp
    Engine/Editor/InspectorPanel.cpp
    Engine/Editor/ConstructWorld.cpp
    Engine/Editor/SolidArcInspectorPanel.cpp
    Engine/Editor/TyreInspectorPanel.cpp
    Engine/Editor/SunInspectorPanel.cpp
    Engine/Editor/LensFlareInspectorPanel.cpp
    Engine/Editor/AtmosphereSkyInspectorPanel.cpp
    Engine/Editor/MoonInspectorPanel.cpp
    Engine/Editor/StarsInspectorPanel.cpp
    Engine/Editor/CloudsInspectorPanel.cpp
    Engine/Editor/FogInspectorPanel.cpp
    Engine/Editor/WeatherInspectorPanel.cpp
    Engine/Editor/CameraInspectorPanel.cpp
    Engine/Editor/LightInspectorPanel.cpp
    Engine/Editor/PostProcessInspectorPanel.cpp

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
#============================================================================================================================================
# Frontier.exe — one windowed engine host (Vulkan + GLFW + ImGui + ThorVG + Slang)
#============================================================================================================================================

set(FRONTIER_HOST_SOURCES
    Projects/Project-Fluid/Source/SurfaceReconstruction.cpp
    Projects/Project-Fluid/Source/GpuSurfaceData.cpp
    Projects/Project-Fluid/Source/GpuSurfaceExtractor.cpp
    Projects/Project-Fluid/Source/FluidGpuTest.cpp
    Projects/Project-Fluid/Source/VulkanFluidMain.cpp
    Projects/Project-Fluid/Source/PbfFluid.cpp
    Projects/Project-Fluid/Source/PondWave.cpp
    ${FRONTIER_ENGINE_SOURCES}
    Engine/ProjectInterchange/ProjectSpecification.cpp
    Engine/ProjectInterchange/CodeInterchange.cpp
    Engine/Host/FrontierExecution.cpp
    Engine/Host/FrontierHost.cpp
    Engine/Host/ProjectOpeningSequence.cpp
    Engine/Host/FrontierRuntime.cpp
    Engine/Host/WaterBodySequence.cpp
    Engine/Host/InterfaceTrialSequence.cpp
    Engine/Host/InstanceMotionSequence.cpp
    Engine/Host/PerformanceTelemetrySequence.cpp
    Engine/Host/PhysicsInstanceSequence.cpp
    Engine/Host/InterfaceAudioSequence.cpp
    Engine/Host/CrankClickIntegrator.cpp
    Engine/Host/DynoSequence.cpp
    Engine/Host/CelestialSequence.cpp
    Engine/Host/CommandLine.cpp
    Engine/Host/ShowroomStructure.cpp
    Engine/Host/RayTracingSolver.cpp
    Engine/Host/FlyThroughSolver.cpp
    Engine/Host/EditorFeedSequence.cpp
    Engine/DeviceExchange/SwapchainExchange.cpp
    Engine/DeviceExchange/RayQueryExchange.cpp
    Engine/GeometricRaster/DistanceFieldStructure.cpp
    Engine/DeviceExchange/DistanceFieldGIStage.cpp
    Engine/DeviceExchange/SurfelGIStage.cpp
    Engine/DeviceExchange/RayTracingCapabilitySet.cpp
    Engine/DeviceExchange/VisibilityExchange.cpp
    Engine/DeviceExchange/InterfaceExchange.cpp
    Engine/DeviceExchange/GizmoExchange.cpp
    Engine/PlatformInterchange/AudioExchange.cpp
    Engine/PlatformInterchange/MiniaudioTranslation.cpp
    Engine/PlatformInterchange/WaveCodec.cpp
    Engine/PhysicalDynamics/RigidBodySolver.cpp
    Engine/DisplayPresentation/DiagnosticInspector.cpp
    Engine/DisplayPresentation/ReSTIRIntegrator.cpp
    Engine/DisplayPresentation/ShadingTableCodec.cpp
    Engine/ContentInterchange/ShaderballPreview.cpp
    Engine/DisplayPresentation/RenderScheduler.cpp
    ${IMGUI_SOURCES}
)

# Only shared engine and host translation units belong here. Project code images declare their own compact batches below.
set(FRONTIER_MISSING_SOURCES "")
foreach(FrontierSource IN LISTS FRONTIER_HOST_SOURCES)
    if(NOT FrontierSource MATCHES "^\\$" AND NOT IS_ABSOLUTE "${FrontierSource}")
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${FrontierSource}")
            list(APPEND FRONTIER_MISSING_SOURCES "${FrontierSource}")
        endif()
    endif()
endforeach()
if(FRONTIER_MISSING_SOURCES)
    string(REPLACE ";" "\n    " FRONTIER_MISSING_REPORT "${FRONTIER_MISSING_SOURCES}")
    message(FATAL_ERROR "missing source files in the Frontier host translation batch:\n    ${FRONTIER_MISSING_REPORT}")
endif()

add_executable(Frontier ${FRONTIER_HOST_SOURCES})
set_target_properties(Frontier PROPERTIES OUTPUT_NAME "Frontier")
file(GLOB FRONTIER_ICON_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/EngineContent/Icons/*.svg")
add_custom_command(TARGET Frontier POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Frontier>/EngineContent/Icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FRONTIER_ICON_SOURCES} "$<TARGET_FILE_DIR:Frontier>/EngineContent/Icons"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/EngineContent/Icons/ThorVG" "$<TARGET_FILE_DIR:Frontier>/EngineContent/Icons/ThorVG"
    COMMENT "Staging Frontier SVG icons" VERBATIM)

add_dependencies(Frontier ReSTIRViewportSpirv)
set_source_files_properties(Engine/GeometricRaster/TraversalIndex.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-msse4.2")
set_source_files_properties(Engine/GeometricRaster/InstanceAcceleration.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-msse4.2")
set_source_files_properties(Engine/ContentInterchange/ShaderballPreview.cpp PROPERTIES COMPILE_DEFINITIONS SHADERBALL_PREVIEW_LIB
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/Engine/DisplayPresentation;${CMAKE_CURRENT_SOURCE_DIR}/Engine/Shaders;${CMAKE_CURRENT_SOURCE_DIR}/Engine/ContentInterchange")
set_source_files_properties(Engine/PlatformInterchange/MiniaudioTranslation.cpp PROPERTIES COMPILE_OPTIONS "-w")

target_include_directories(Frontier PRIVATE
    ${FRONTIER_ENGINE_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Host
    # The editor panels include their neighbours and the sky solver by bare name, the way the MSVC
    #    route already puts both folders on the include path.
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Editor
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/DisplayPresentation
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
target_link_libraries(Frontier PRIVATE
    Jolt
    ${Vulkan_LIBRARIES}
    glfw
    thorvg_static
    pthread
    dl
)
target_compile_definitions(Frontier PRIVATE
    PROJECT_FLUID_EMBEDDED
    FRONTIER_DEVELOPMENT
    FRONTIER_ENABLE_GLFW
)
if(WIN32)
    target_link_libraries(Frontier PRIVATE psapi d3d11 dxgi d3dcompiler dwmapi imm32 ole32 uuid windowscodecs)
endif()

# Project code images contain project semantics only. They do not compile the window, device, renderer, editor, or host.
add_library(ProjectZero SHARED Projects/Project-Zero/Source/ProjectZeroInterchange.cpp)
include(Projects/Project-Drive/Build/ProjectDrive.cmake)
foreach(ProjectCodeImage IN ITEMS ProjectZero ProjectDrive)
    target_include_directories(${ProjectCodeImage} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Engine/ProjectInterchange)
    set_target_properties(${ProjectCodeImage} PROPERTIES PREFIX "" SUFFIX ".dll")
endforeach()
set_target_properties(ProjectZero PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Projects/Project-Zero/Build"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Projects/Project-Zero/Build")
set_target_properties(ProjectDrive PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Projects/Project-Drive/Build"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Projects/Project-Drive/Build")

#============================================================================================================================================
# Project-Zero-CpuReference — headless CPU reference. It remains a separate non-windowed verification route while
#   Frontier.exe is the product Vulkan/Slang host. Its explicit batch intentionally excludes all window ownership.
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

# The testbed's own sources are not in every checkout of this repository, and a missing source file
#    is a configure-time error that takes the whole tree down with it, so it is declared only when
#    it is actually here.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Physics/Source/DropSceneStructure.cpp")
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
else()
    message(STATUS "[Frontier] Project-Physics sources absent - the Jolt testbed is not declared")
endif()

# Standalone fluid research host alongside the native application.
add_subdirectory(Projects/Project-Fluid)
if(FRONTIER_FLUID_OPENMP)
    find_package(OpenMP REQUIRED COMPONENTS CXX)
    # Frontier.exe compiles shared fluid preview support directly, rather than linking Core.
    target_link_libraries(Frontier PRIVATE OpenMP::OpenMP_CXX)
endif()
