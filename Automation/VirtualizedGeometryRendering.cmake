#============================================================================================================================================
#                                                   VIRTUALIZEDGEOMETRYRENDERING.CMAKE
#============================================================================================================================================
# 🧩 1:1 C++ port of the VehicleHMI demo's virtualized geometry (cluster nesting build, runtime cut, culling and residency) and its
#    bit-exact parity validation against the demo's own builder (Exhibits/VirtualizedGeometry).

add_library(FrontierVirtualizedGeometry STATIC
    Frontier/Engine/Rendering/VirtualizedGeometry/NestingConstruction.cpp
    Frontier/Engine/Rendering/VirtualizedGeometry/NestingSelection.cpp
    Frontier/Engine/Rendering/VirtualizedGeometry/VisibilityResolve.cpp
    Frontier/Engine/Rendering/VirtualizedGeometry/NestingArchive.cpp)
target_include_directories(FrontierVirtualizedGeometry PUBLIC "${CMAKE_SOURCE_DIR}/Frontier/Engine")
target_compile_features(FrontierVirtualizedGeometry PUBLIC cxx_std_20)
# 📝 Parity with the demo's JavaScript doubles needs plain IEEE evaluation: no fused multiply-adds.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FrontierVirtualizedGeometry PRIVATE -ffp-contract=off)
elseif(MSVC)
    target_compile_options(FrontierVirtualizedGeometry PRIVATE /fp:precise)
endif()

# 📝 The set's Slang units include the illumination port's refresh profile and intrinsics (Shaders/GlobalIllumination), so they
#    compile with the same pinned compiler; without it the editor uses EngineContent/ShaderArchives/VirtualizedGeometry.
find_program(FRONTIER_GLOBAL_ILLUMINATION_SLANGC
    NAMES slangc
    PATHS "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin"
    NO_DEFAULT_PATH)
set(FRONTIER_VIRTUALIZED_GEOMETRY_SHADER_SRC "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/VirtualizedGeometry")
set(FRONTIER_VIRTUALIZED_GEOMETRY_SHADER_OUT "${CMAKE_BINARY_DIR}/ShaderArchives/VirtualizedGeometry")
file(GLOB FRONTIER_VIRTUALIZED_GEOMETRY_SLANG_SOURCES CONFIGURE_DEPENDS "${FRONTIER_VIRTUALIZED_GEOMETRY_SHADER_SRC}/*.slang"
     "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/GlobalIllumination/*.slang")

# Unit:Entry:Stage — one SPIR-V file per entry point, <Unit>.<Entry>.spv.
set(FRONTIER_VIRTUALIZED_GEOMETRY_ENTRIES
    "VirtualizedGeometryCull:SelectAndCullPhase1:compute"
    "VirtualizedGeometryCull:CullPhase2Disoccluded:compute"
    "VirtualizedGeometryCull:SelectAndCullPhase1Compacted:compute"
    "VirtualizedGeometryCull:CullPhase2DisoccludedCompacted:compute"
    "HiZPyramid:SeedHiZFromDepth:compute"
    "HiZDownsample:DownsampleHiZMip:compute"
    "VisibilityRaster:VisibilityVertex:vertex"
    "VisibilityRaster:VisibilityFragment:fragment"
    "VisibilityRaster:VisibilityGlassFragment:fragment"
    "VisibilityRaster:VisibilityGlassBackFragment:fragment"
    "MaterialResolve:ResolveOpaqueVisibility:compute"
    "MaterialResolve:ResolveRefractionGather:compute"
    "MaterialResolve:DebugVisibilityIds:compute"
    "MaterialResolve:DebugGlassLayers:compute"
    "MaterialResolve:DebugCopyResolved:compute"
    "MaterialResolve:DebugHiZView:compute"
    "MaterialResolve:DebugLodLevels:compute"
    "ClusterDebugFallback:DebugUnavailable:compute"
    "ClusterDebugFallback:DebugFailed:compute"
    "ClusterDebugFallback:DebugNoGeometry:compute")

set(FRONTIER_VIRTUALIZED_GEOMETRY_SPV_OUTPUTS)
if(FRONTIER_GLOBAL_ILLUMINATION_SLANGC)
    foreach(ENTRY_SPEC IN LISTS FRONTIER_VIRTUALIZED_GEOMETRY_ENTRIES)
        string(REPLACE ":" ";" ENTRY_PARTS "${ENTRY_SPEC}")
        list(GET ENTRY_PARTS 0 ENTRY_UNIT)
        list(GET ENTRY_PARTS 1 ENTRY_NAME)
        list(GET ENTRY_PARTS 2 ENTRY_STAGE)
        set(ENTRY_SPV "${FRONTIER_VIRTUALIZED_GEOMETRY_SHADER_OUT}/${ENTRY_UNIT}.${ENTRY_NAME}.spv")
        list(APPEND FRONTIER_VIRTUALIZED_GEOMETRY_SPV_OUTPUTS "${ENTRY_SPV}")
        add_custom_command(
            OUTPUT "${ENTRY_SPV}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_VIRTUALIZED_GEOMETRY_SHADER_OUT}"
            COMMAND "${FRONTIER_GLOBAL_ILLUMINATION_SLANGC}" "${FRONTIER_VIRTUALIZED_GEOMETRY_SHADER_SRC}/${ENTRY_UNIT}.slang"
                -I "${FRONTIER_VIRTUALIZED_GEOMETRY_SHADER_SRC}"
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
            DEPENDS ${FRONTIER_VIRTUALIZED_GEOMETRY_SLANG_SOURCES}
            VERBATIM)
    endforeach()
    add_custom_target(FrontierVirtualizedGeometryShaders ALL DEPENDS ${FRONTIER_VIRTUALIZED_GEOMETRY_SPV_OUTPUTS})
endif()

if(BUILD_TESTING)
    # 📝 Every corpus surface rebuilt from the demo's own soup must hash to the demo builder's digests
    #    (Exhibits/VirtualizedGeometry/README.md).
    add_executable(NestingParityValidation
        Exhibits/VirtualizedGeometry/Code/NestingParityValidation.cpp
        Exhibits/VirtualizedGeometry/Code/SurfaceSoupArchive.cpp
        Engine/Render/StbImageImplementation.cpp)
    target_include_directories(NestingParityValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_compile_definitions(NestingParityValidation PRIVATE
        FRONTIER_VIRTUALIZED_GEOMETRY_EXHIBIT="${CMAKE_SOURCE_DIR}/Exhibits/VirtualizedGeometry")
    target_link_libraries(NestingParityValidation PRIVATE FrontierVirtualizedGeometry)
    add_test(NAME NestingParityValidation COMMAND NestingParityValidation)

    # 📝 The demo's 22 virtualized geometry tests, assertion for assertion (Exhibits/VirtualizedGeometry/README.md).
    add_executable(VirtualizedGeometryValidation
        Exhibits/VirtualizedGeometry/Code/VirtualizedGeometryValidation.cpp
        Exhibits/VirtualizedGeometry/Code/SurfaceSoupArchive.cpp
        Engine/Render/StbImageImplementation.cpp)
    target_include_directories(VirtualizedGeometryValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_compile_definitions(VirtualizedGeometryValidation PRIVATE
        FRONTIER_VIRTUALIZED_GEOMETRY_EXHIBIT="${CMAKE_SOURCE_DIR}/Exhibits/VirtualizedGeometry")
    target_link_libraries(VirtualizedGeometryValidation PRIVATE FrontierVirtualizedGeometry)
    add_test(NAME VirtualizedGeometryValidation COMMAND VirtualizedGeometryValidation)

    # 📝 I2, the import step: the proving ground's cluster nestings, built once and stored beside its facet space; the validation
    #    rebuilds them and requires the stored archive to be current (EngineContent/EnvironmentArchives/ProvingGround).
    add_executable(NestingArchiveExport Exhibits/VirtualizedGeometry/Code/NestingArchiveExport.cpp)
    target_link_libraries(NestingArchiveExport PRIVATE FrontierGlobalIllumination)
    add_test(NAME NestingArchiveValidation COMMAND NestingArchiveExport --verify
        "${CMAKE_SOURCE_DIR}/EngineContent/EnvironmentArchives/ProvingGround/ProvingGround.facetspace")

    # 📝 Before and after of every builder refinement (I2-I5) on the corpus (Results/NestingRefinement.txt).
    add_executable(NestingRefinementMeasure
        Exhibits/VirtualizedGeometry/Code/NestingRefinementMeasure.cpp
        Exhibits/VirtualizedGeometry/Code/SurfaceSoupArchive.cpp
        Engine/Render/StbImageImplementation.cpp)
    target_include_directories(NestingRefinementMeasure PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_compile_definitions(NestingRefinementMeasure PRIVATE
        FRONTIER_VIRTUALIZED_GEOMETRY_EXHIBIT="${CMAKE_SOURCE_DIR}/Exhibits/VirtualizedGeometry")
    target_link_libraries(NestingRefinementMeasure PRIVATE FrontierVirtualizedGeometry)

    # 📝 The GPU takeover in the editor's scene viewport, offscreen (needs a Vulkan device; Exhibits/VirtualizedGeometry/README.md).
    add_executable(VirtualizedGeometryCapture
        Exhibits/VirtualizedGeometry/Code/VirtualizedGeometryCapture.cpp
        Engine/Render/StbImageWriteImplementation.cpp)
    target_include_directories(VirtualizedGeometryCapture PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(VirtualizedGeometryCapture PRIVATE FrontierSceneViewport)
endif()
