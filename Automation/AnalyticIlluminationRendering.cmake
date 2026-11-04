#============================================================================================================================================
#                                                   ANALYTICILLUMINATIONRENDERING.CMAKE
#============================================================================================================================================
# 🧩 The analytic illumination: the editor's fast closed-form renderer (one shadowed sun, Lambert and Blinn-Phong surfaces, hemisphere
#    fill, gradient sky) of the scene the global illumination loaded, and its Slang units (archive set AnalyticIllumination).

add_library(FrontierAnalyticIllumination STATIC
    Frontier/Engine/Rendering/AnalyticIllumination/SolarDepthRasterization.cpp
    Frontier/Engine/Rendering/AnalyticIllumination/SurfaceShadingRasterization.cpp
    Frontier/Engine/Rendering/AnalyticIllumination/AnalyticIlluminationSequence.cpp)
target_link_libraries(FrontierAnalyticIllumination PUBLIC FrontierGlobalIllumination)

# 📝 The units include the illumination port's scene layouts and intrinsics, so they compile with the same pinned compiler; without it
#    the editor uses EngineContent/ShaderArchives/AnalyticIllumination.
find_program(FRONTIER_GLOBAL_ILLUMINATION_SLANGC
    NAMES slangc
    PATHS "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin"
    NO_DEFAULT_PATH)
set(FRONTIER_ANALYTIC_ILLUMINATION_SHADER_SRC "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/AnalyticIllumination")
set(FRONTIER_ANALYTIC_ILLUMINATION_SHADER_OUT "${CMAKE_BINARY_DIR}/ShaderArchives/AnalyticIllumination")
file(GLOB FRONTIER_ANALYTIC_ILLUMINATION_SLANG_SOURCES CONFIGURE_DEPENDS "${FRONTIER_ANALYTIC_ILLUMINATION_SHADER_SRC}/*.slang"
     "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/GlobalIllumination/*.slang")

# Unit:Entry:Stage — one SPIR-V file per entry point, <Unit>.<Entry>.spv.
set(FRONTIER_ANALYTIC_ILLUMINATION_ENTRIES
    "SolarDepthRasterization:SolarDepthVertex:vertex"
    "SurfaceShadingRasterization:SkyVertex:vertex"
    "SurfaceShadingRasterization:SkyFragment:fragment"
    "SurfaceShadingRasterization:ShadingVertex:vertex"
    "SurfaceShadingRasterization:ShadingFragment:fragment")

set(FRONTIER_ANALYTIC_ILLUMINATION_SPV_OUTPUTS)
if(FRONTIER_GLOBAL_ILLUMINATION_SLANGC)
    foreach(ENTRY_SPEC IN LISTS FRONTIER_ANALYTIC_ILLUMINATION_ENTRIES)
        string(REPLACE ":" ";" ENTRY_PARTS "${ENTRY_SPEC}")
        list(GET ENTRY_PARTS 0 ENTRY_UNIT)
        list(GET ENTRY_PARTS 1 ENTRY_NAME)
        list(GET ENTRY_PARTS 2 ENTRY_STAGE)
        set(ENTRY_SPV "${FRONTIER_ANALYTIC_ILLUMINATION_SHADER_OUT}/${ENTRY_UNIT}.${ENTRY_NAME}.spv")
        list(APPEND FRONTIER_ANALYTIC_ILLUMINATION_SPV_OUTPUTS "${ENTRY_SPV}")
        add_custom_command(
            OUTPUT "${ENTRY_SPV}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_ANALYTIC_ILLUMINATION_SHADER_OUT}"
            COMMAND "${FRONTIER_GLOBAL_ILLUMINATION_SLANGC}" "${FRONTIER_ANALYTIC_ILLUMINATION_SHADER_SRC}/${ENTRY_UNIT}.slang"
                -I "${FRONTIER_ANALYTIC_ILLUMINATION_SHADER_SRC}"
                -I "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/GlobalIllumination"
                -entry "${ENTRY_NAME}"
                -stage "${ENTRY_STAGE}"
                -target spirv
                -profile spirv_1_3
                -emit-spirv-directly
                -matrix-layout-row-major
                -fvk-use-entrypoint-name
                -O0
                -o "${ENTRY_SPV}"
            DEPENDS ${FRONTIER_ANALYTIC_ILLUMINATION_SLANG_SOURCES}
            VERBATIM)
    endforeach()
    add_custom_target(FrontierAnalyticIlluminationShaders ALL DEPENDS ${FRONTIER_ANALYTIC_ILLUMINATION_SPV_OUTPUTS})
endif()

if(BUILD_TESTING)
    # 📝 The analytic illumination in the editor's scene viewport, offscreen (needs a Vulkan device; Exhibits/AnalyticIllumination).
    add_executable(AnalyticIlluminationCapture
        Exhibits/AnalyticIllumination/Code/AnalyticIlluminationCapture.cpp
        Engine/Render/StbImageWriteImplementation.cpp)
    target_include_directories(AnalyticIlluminationCapture PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(AnalyticIlluminationCapture PRIVATE FrontierSceneViewport)
endif()
