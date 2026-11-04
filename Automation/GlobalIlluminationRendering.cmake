#============================================================================================================================================
#                                                   GLOBALILLUMINATIONRENDERING.CMAKE
#============================================================================================================================================
# 🧩 1:1 C++/Vulkan/Slang port of the VehicleHMI demo's ReSTIR GI renderer (scene distance field + surface cards + BVH path
#    tracer + ReSTIR GI + SVGF + display), its Slang units, and the parity validation against captures of the original demo.

add_library(FrontierGlobalIllumination STATIC
    Frontier/Engine/DeviceExchange/DeviceExchange.cpp
    Frontier/Engine/Rendering/GlobalIllumination/TransformProjection.cpp
    Frontier/Engine/Rendering/GlobalIllumination/SceneStructure.cpp
    Frontier/Engine/Rendering/GlobalIllumination/BoundingStructure.cpp
    Frontier/Engine/Rendering/GlobalIllumination/ShaderInterchange.cpp
    Frontier/Engine/Rendering/GlobalIllumination/OccupantDistanceSpace.cpp
    Frontier/Engine/Rendering/GlobalIllumination/IlluminationConstants.cpp
    Frontier/Engine/Rendering/GlobalIllumination/SceneDistanceSpace.cpp
    Frontier/Engine/Rendering/GlobalIllumination/SurfaceCardDepot.cpp
    Frontier/Engine/Rendering/GlobalIllumination/RayTracingSpace.cpp
    Frontier/Engine/Rendering/GlobalIllumination/AtmosphereIntegrator.cpp
    Frontier/Engine/Rendering/GlobalIllumination/ShadowProjection.cpp
    Frontier/Engine/Rendering/GlobalIllumination/SurfaceProjection.cpp
    Frontier/Engine/Rendering/GlobalIllumination/ResamplingSequence.cpp
    Frontier/Engine/Rendering/GlobalIllumination/DisplayProjection.cpp
    Frontier/Engine/Rendering/GlobalIllumination/RefreshEncoding.cpp
    Frontier/Engine/Rendering/GlobalIllumination/EnvironmentEncoding.cpp
    Frontier/Engine/Rendering/GlobalIllumination/LuminaireRegister.cpp
    Frontier/Engine/Rendering/GlobalIllumination/IlluminationHost.cpp
    Frontier/Engine/Rendering/GlobalIllumination/OccupantRelocation.cpp
    Frontier/Engine/Rendering/GlobalIllumination/PickProjection.cpp
    Frontier/Engine/Rendering/VirtualizedGeometry/NestingProjection.cpp)

target_include_directories(FrontierGlobalIllumination PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/Frontier/Engine"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
target_compile_definitions(FrontierGlobalIllumination PUBLIC VK_NO_PROTOTYPES)
target_compile_features(FrontierGlobalIllumination PUBLIC cxx_std_20)
target_link_libraries(FrontierGlobalIllumination PUBLIC ${CMAKE_DL_LIBS} Threads::Threads FrontierVirtualizedGeometry)

# 📝 Only the pinned compiler (Automation/PrepareVulkanCompute.py) rebuilds these units by default, so every build presents the
#    SPIR-V the parity validation measured. Without it the editor uses the stored archives in
#    EngineContent/ShaderArchives/GlobalIllumination (Automation/StoreShaderArchives.py); pass
#    -DFRONTIER_GLOBAL_ILLUMINATION_SLANGC=<slangc> to compile them with another Slang compiler.
find_program(FRONTIER_GLOBAL_ILLUMINATION_SLANGC
    NAMES slangc
    PATHS "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin"
    NO_DEFAULT_PATH)

set(FRONTIER_GLOBAL_ILLUMINATION_SHADER_SRC "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/GlobalIllumination")
set(FRONTIER_GLOBAL_ILLUMINATION_SHADER_OUT "${CMAKE_BINARY_DIR}/ShaderArchives/GlobalIllumination")
file(GLOB FRONTIER_GLOBAL_ILLUMINATION_SLANG_SOURCES CONFIGURE_DEPENDS "${FRONTIER_GLOBAL_ILLUMINATION_SHADER_SRC}/*.slang")

# Unit:Entry:Stage — one SPIR-V file per entry point, <Unit>.<Entry>.spv.
set(FRONTIER_GLOBAL_ILLUMINATION_ENTRIES
    "DistanceTransfer:TransferDistance:compute"
    "FragmentDistance:ComputeFragmentDistance:compute"
    "DistanceUnion:UniteDistance:compute"
    "CardCapture:CaptureVertex:vertex"
    "CardCapture:CaptureFragment:fragment"
    "CardDirect:IntegrateCardDirect:compute"
    "CardRadiosity:IntegrateCardRadiosity:compute"
    "CardFinal:IntegrateCardFinal:compute"
    "AtmosphereTransmittance:IntegrateTransmittance:compute"
    "AtmosphereMultiple:IntegrateMultiple:compute"
    "AtmosphereScattering:IntegrateSkyView:compute"
    "ShadowRaster:ShadowVertex:vertex"
    "ShadowRaster:MaskedShadowVertex:vertex"
    "ShadowRaster:MaskedShadowFragment:fragment"
    "GeometryClusterCull:CullGeometryClusters:compute"
    "SurfaceRaster:SurfaceVertex:vertex"
    "SurfaceRaster:SurfaceFragment:fragment"
    "DirectRadiance:IntegrateDirectRadiance:compute"
    "PenumbraResolve:ResolvePenumbra:compute"
    "ReservoirTemporal:ResampleTemporal:compute"
    "ReservoirTemporal:ProjectResamplingGuide:compute"
    "ReservoirSpatial:ResampleSpatial:compute"
    "ReflectionTrace:TraverseReflection:compute"
    "ReflectionTrace:TraverseMirrorReflection:compute"
    "VarianceTemporal:IntegrateTemporal:compute"
    "VarianceWavelet:ConvolveWavelet:compute"
    "VarianceUpsample:ProjectScreenSignal:compute"
    "InspectionMarch:MarchInspection:compute"
    "DisplayRecombination:ResolveRecombination:compute"
    "DisplayAntialias:IntegrateAntialias:compute"
    "DisplayExposure:IntegrateExposure:compute"
    "DisplayPresent:PresentVertex:vertex"
    "DisplayPresent:PresentFragment:fragment"
    "EnvironmentVisibility:ProjectEnvironmentVisibility:compute"
    "EnvironmentVisibility:RestoreEnvironmentRadiance:compute"
    "EnvironmentLighting:ProjectEnvironmentSky:compute"
    "EnvironmentLighting:ProjectCloudShadow:compute"
    # 📝 Beyond the demo: the editor's GPU picking (PickProjection).
    "PickRaster:PickVertex:vertex"
    "PickRaster:PickFragment:fragment")

set(FRONTIER_GLOBAL_ILLUMINATION_SPV_OUTPUTS)
if(FRONTIER_GLOBAL_ILLUMINATION_SLANGC)
    foreach(ENTRY_SPEC IN LISTS FRONTIER_GLOBAL_ILLUMINATION_ENTRIES)
        string(REPLACE ":" ";" ENTRY_PARTS "${ENTRY_SPEC}")
        list(GET ENTRY_PARTS 0 ENTRY_UNIT)
        list(GET ENTRY_PARTS 1 ENTRY_NAME)
        list(GET ENTRY_PARTS 2 ENTRY_STAGE)
        set(ENTRY_SPV "${FRONTIER_GLOBAL_ILLUMINATION_SHADER_OUT}/${ENTRY_UNIT}.${ENTRY_NAME}.spv")
        list(APPEND FRONTIER_GLOBAL_ILLUMINATION_SPV_OUTPUTS "${ENTRY_SPV}")
        add_custom_command(
            OUTPUT "${ENTRY_SPV}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_GLOBAL_ILLUMINATION_SHADER_OUT}"
            COMMAND "${FRONTIER_GLOBAL_ILLUMINATION_SLANGC}" "${FRONTIER_GLOBAL_ILLUMINATION_SHADER_SRC}/${ENTRY_UNIT}.slang"
                -I "${FRONTIER_GLOBAL_ILLUMINATION_SHADER_SRC}"
                -entry "${ENTRY_NAME}"
                -stage "${ENTRY_STAGE}"
                -target spirv
                -profile spirv_1_3
                -emit-spirv-directly
                -matrix-layout-row-major
                -fvk-use-entrypoint-name
                -O0
                -o "${ENTRY_SPV}"
            DEPENDS ${FRONTIER_GLOBAL_ILLUMINATION_SLANG_SOURCES}
            VERBATIM)
    endforeach()
    add_custom_target(FrontierGlobalIlluminationShaders ALL DEPENDS ${FRONTIER_GLOBAL_ILLUMINATION_SPV_OUTPUTS})
endif()

# 📝 The live scene viewport: the illumination refresh with the PlanetaryAtmosphere environment overlaid on it on one device
#    and queue (the editor's Scene Viewport, Exhibits/SceneViewport).
add_library(FrontierSceneViewport STATIC
    Frontier/Engine/Geometry/ShapeArchive.cpp
    Frontier/Engine/Rendering/SceneViewport/EnvironmentOverlay.cpp
    Frontier/Engine/Rendering/SceneViewport/SceneViewport.cpp)
target_include_directories(FrontierSceneViewport PUBLIC "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
target_link_libraries(FrontierSceneViewport PUBLIC FrontierGlobalIllumination FrontierEnvironmentRendering FrontierAnalyticIllumination)
if(TARGET FrontierGlobalIlluminationShaders)
    add_dependencies(FrontierSceneViewport FrontierGlobalIlluminationShaders)
endif()
if(TARGET FrontierEnvironmentShaders)
    add_dependencies(FrontierSceneViewport FrontierEnvironmentShaders)
endif()
if(TARGET FrontierVirtualizedGeometryShaders)
    add_dependencies(FrontierSceneViewport FrontierVirtualizedGeometryShaders)
endif()
if(TARGET FrontierAnalyticIlluminationShaders)
    add_dependencies(FrontierSceneViewport FrontierAnalyticIlluminationShaders)
endif()

# 📝 The editor loads EngineContent/ShaderArchives/{GlobalIllumination,Environment} beside Frontier.exe. The stored archives arrive
#    with the EngineContent copy; a set this build compiled replaces its stored copy afterwards.
if(TARGET Frontier)
    foreach(SCENE_SHADERS GlobalIllumination Environment VirtualizedGeometry AnalyticIllumination)
        if(TARGET Frontier${SCENE_SHADERS}Shaders)
            add_dependencies(Frontier Frontier${SCENE_SHADERS}Shaders)
            add_custom_command(TARGET Frontier POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_BINARY_DIR}/ShaderArchives/${SCENE_SHADERS}" "$<TARGET_FILE_DIR:Frontier>/EngineContent/ShaderArchives/${SCENE_SHADERS}")
        endif()
    endforeach()
endif()

if(BUILD_TESTING)
    # 📝 Offscreen scene viewport frames (own device, same code path as the editor) — Exhibits/SceneViewport/README.md.
    add_executable(SceneViewportValidation
        Exhibits/SceneViewport/Code/SceneViewportValidation.cpp
        Engine/Render/StbImageWriteImplementation.cpp)
    target_include_directories(SceneViewportValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(SceneViewportValidation PRIVATE FrontierSceneViewport)

    # 📝 The fifteen inspection views of one settled daylight view: the Render settings inspector's debug-view thumbnails.
    add_executable(InspectionViewCapture
        Exhibits/SceneViewport/Code/InspectionViewCapture.cpp
        Engine/Render/StbImageWriteImplementation.cpp)
    target_include_directories(InspectionViewCapture PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(InspectionViewCapture PRIVATE FrontierSceneViewport)

    # 📝 How much the stars twinkle on screen under the Stars record's scintillation controls (Exhibits/SceneViewport/README.md).
    add_executable(StarScintillationCapture
        Exhibits/SceneViewport/Code/StarScintillationCapture.cpp
        Engine/Render/StbImageWriteImplementation.cpp)
    target_include_directories(StarScintillationCapture PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(StarScintillationCapture PRIVATE FrontierSceneViewport)

    # 📝 Ray-traced reflections and sun shadows under the demo's options and the refinements, measured (Exhibits/RayTracedLighting).
    add_executable(RayTracedLightingCapture
        Exhibits/RayTracedLighting/Code/RayTracedLightingCapture.cpp
        Engine/Render/StbImageWriteImplementation.cpp)
    target_include_directories(RayTracedLightingCapture PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(RayTracedLightingCapture PRIVATE FrontierSceneViewport)

    # 📝 The viewport's Unreal-style camera against the renderer's own view basis (no device) — Exhibits/SceneViewport/README.md.
    add_executable(SceneViewportFlightValidation
        Exhibits/SceneViewport/Code/SceneViewportFlightValidation.cpp)
    target_link_libraries(SceneViewportFlightValidation PRIVATE EverestProjectRuntime FrontierGlobalIllumination)
    add_test(NAME SceneViewportFlightValidation COMMAND SceneViewportFlightValidation)

    # 📝 Parity validation against a reference capture of the original demo (Exhibits/RestirGiParity/README.md).
    add_executable(RestirGiParityValidation
        Exhibits/RestirGiParity/Code/RestirGiParityValidation.cpp
        Exhibits/RestirGiParity/Code/ReferenceCapture.cpp
        Engine/Render/StbImageWriteImplementation.cpp)
    target_include_directories(RestirGiParityValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(RestirGiParityValidation PRIVATE FrontierGlobalIllumination)
    if(TARGET FrontierGlobalIlluminationShaders)
        add_dependencies(RestirGiParityValidation FrontierGlobalIlluminationShaders)
    endif()

    # 📝 Every stored set must match the current Slang sources. A set this build compiled with the compiler that stored it must also
    #    match its stored copy byte for byte: GlobalIllumination/Environment with the pinned slangc, Surface (the Project Browser's
    #    start-up archives, CAD, materials, vector coverage, bake field) with the slang-loader 0.0.7 Slang WASM. Other compilers'
    #    output is not compared.
    set(FRONTIER_PINNED_SLANG "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/")
    set(FRONTIER_COMPARED_SHADER_SETS)
    foreach(SCENE_SHADERS GlobalIllumination Environment VirtualizedGeometry AnalyticIllumination)
        if(SCENE_SHADERS STREQUAL "Environment")
            set(SCENE_SLANGC "${FRONTIER_ENV_SLANGC}")
        else()
            set(SCENE_SLANGC "${FRONTIER_GLOBAL_ILLUMINATION_SLANGC}")
        endif()
        string(FIND "${SCENE_SLANGC}" "${FRONTIER_PINNED_SLANG}" SCENE_PINNED)
        if(TARGET Frontier${SCENE_SHADERS}Shaders AND SCENE_PINNED EQUAL 0)
            list(APPEND FRONTIER_COMPARED_SHADER_SETS ${SCENE_SHADERS})
        endif()
    endforeach()
    if(TARGET FrontierSurfaceShaders AND NOT FRONTIER_SLANGC AND FRONTIER_SLANG_WASM)
        get_filename_component(SURFACE_WASM_FOLDER "${FRONTIER_SLANG_WASM}" DIRECTORY)
        if(EXISTS "${SURFACE_WASM_FOLDER}/slang-wasm.wasm")
            file(SHA256 "${SURFACE_WASM_FOLDER}/slang-wasm.wasm" SURFACE_WASM_DIGEST)
            if(SURFACE_WASM_DIGEST STREQUAL "834d30ccf9fc5c01baa33cca361734fbd65a0cdd3985af95bb454ecfff66e5f1")
                list(APPEND FRONTIER_COMPARED_SHADER_SETS Surface)
            endif()
        endif()
    endif()
    add_test(NAME StoredShaderArchiveValidation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Automation/StoreShaderArchives.py"
        --verify --sets GlobalIllumination Environment VirtualizedGeometry AnalyticIllumination Surface --build "${CMAKE_BINARY_DIR}" --compare ${FRONTIER_COMPARED_SHADER_SETS})
endif()
