#============================================================================================================================================
#                                                        XRAYSCANRENDERING.CMAKE
#============================================================================================================================================
# 🧩 The X-ray scan: the CPU modules (clock, feature edges, sparkles, profile records), the overlay on the device exchange
#    (Frontier/Engine/Rendering/XRayScan) and its Slang units ported from ScanOverlay.wgsl and ViewWorld.wgsl (archive set XRayScan), compiled
#    one SPIR-V file per entry point.

add_library(FrontierXRayScan STATIC
    Frontier/Engine/Rendering/XRayScan/XRayScanSequence.cpp
    Frontier/Engine/Rendering/XRayScan/XRayScanEdges.cpp
    Frontier/Engine/Rendering/XRayScan/XRayScanSparkles.cpp
    Frontier/Engine/Rendering/XRayScan/XRayScanProfile.cpp
    Frontier/Engine/Rendering/XRayScan/XRayScanOverlaySequence.cpp)
target_include_directories(FrontierXRayScan PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(FrontierXRayScan PUBLIC FrontierGlobalIllumination)
target_compile_features(FrontierXRayScan PUBLIC cxx_std_20)
# 📝 Parity with the demo's JavaScript doubles needs plain IEEE evaluation: no fused multiply-adds.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FrontierXRayScan PRIVATE -ffp-contract=off)
elseif(MSVC)
    target_compile_options(FrontierXRayScan PRIVATE /fp:precise)
endif()

# 📝 The units include the illumination port's intrinsics, layouts and surface raster (they bind its group), so they compile with the same
#    pinned compiler; without it the overlay uses EngineContent/ShaderArchives/XRayScan.
find_program(FRONTIER_GLOBAL_ILLUMINATION_SLANGC
    NAMES slangc
    PATHS "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin"
    NO_DEFAULT_PATH)
set(FRONTIER_XRAY_SCAN_SHADER_SRC "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/XRayScan")
set(FRONTIER_XRAY_SCAN_SHADER_OUT "${CMAKE_BINARY_DIR}/ShaderArchives/XRayScan")
file(GLOB FRONTIER_XRAY_SCAN_SLANG_SOURCES CONFIGURE_DEPENDS "${FRONTIER_XRAY_SCAN_SHADER_SRC}/*.slang"
     "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/GlobalIllumination/*.slang")

# Unit:Entry:Stage — one SPIR-V file per entry point, <Unit>.<Entry>.spv.
set(FRONTIER_XRAY_SCAN_ENTRIES
    "XRayScanOverlay:GhostVertex:vertex"
    "XRayScanOverlay:GhostFragment:fragment"
    "XRayScanOverlay:EdgeVertex:vertex"
    "XRayScanOverlay:EdgeFragment:fragment"
    "XRayScanOverlay:CurtainVertex:vertex"
    "XRayScanOverlay:CurtainFragment:fragment"
    "XRayScanOverlay:SparkleVertex:vertex"
    "XRayScanOverlay:SparkleFragment:fragment")

set(FRONTIER_XRAY_SCAN_SPV_OUTPUTS)
if(FRONTIER_GLOBAL_ILLUMINATION_SLANGC)
    foreach(ENTRY_SPEC IN LISTS FRONTIER_XRAY_SCAN_ENTRIES)
        string(REPLACE ":" ";" ENTRY_PARTS "${ENTRY_SPEC}")
        list(GET ENTRY_PARTS 0 ENTRY_UNIT)
        list(GET ENTRY_PARTS 1 ENTRY_NAME)
        list(GET ENTRY_PARTS 2 ENTRY_STAGE)
        set(ENTRY_SPV "${FRONTIER_XRAY_SCAN_SHADER_OUT}/${ENTRY_UNIT}.${ENTRY_NAME}.spv")
        list(APPEND FRONTIER_XRAY_SCAN_SPV_OUTPUTS "${ENTRY_SPV}")
        add_custom_command(
            OUTPUT "${ENTRY_SPV}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_XRAY_SCAN_SHADER_OUT}"
            COMMAND "${FRONTIER_GLOBAL_ILLUMINATION_SLANGC}" "${FRONTIER_XRAY_SCAN_SHADER_SRC}/${ENTRY_UNIT}.slang"
                -I "${FRONTIER_XRAY_SCAN_SHADER_SRC}"
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
            DEPENDS ${FRONTIER_XRAY_SCAN_SLANG_SOURCES}
            VERBATIM)
    endforeach()
    add_custom_target(FrontierXRayScanShaders ALL DEPENDS ${FRONTIER_XRAY_SCAN_SPV_OUTPUTS})
endif()
if(TARGET FrontierXRayScanShaders)
    add_dependencies(FrontierXRayScan FrontierXRayScanShaders)
endif()

if(BUILD_TESTING)
    # 📝 The C++ scan against the HTML prototype's own modules (Exhibits/XRayScan/Results/ScanReference.toml): the clock, the profile records, the
    #    sparkles, the feature edges of 18 shapes and of the hero car.
    add_executable(XRayScanValidation Exhibits/XRayScan/Code/XRayScanValidation.cpp)
    target_include_directories(XRayScanValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
    target_link_libraries(XRayScanValidation PRIVATE FrontierXRayScan)
    add_test(NAME XRayScanValidation COMMAND XRayScanValidation "${CMAKE_SOURCE_DIR}")
    set_tests_properties(XRayScanValidation PROPERTIES TIMEOUT 120)

    # 📝 The overlay on its own Vulkan device over the hero car (Exhibits/XRayScan): the scan's sweep, one picture per moment. Its solid picture is
    #    a stand-in (XRayScanCaptureSolid.slang, compiled beside the capture and not part of the XRayScan archive set).
    add_executable(XRayScanCapture
        Exhibits/XRayScan/Code/XRayScanCapture.cpp
        Engine/Render/StbImageWriteImplementation.cpp)
    target_include_directories(XRayScanCapture PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(XRayScanCapture PRIVATE FrontierXRayScan)
    if(FRONTIER_GLOBAL_ILLUMINATION_SLANGC)
        set(FRONTIER_XRAY_SCAN_CAPTURE_OUT "${CMAKE_BINARY_DIR}/ShaderArchives/XRayScanCapture")
        set(FRONTIER_XRAY_SCAN_CAPTURE_SPV_OUTPUTS)
        foreach(ENTRY_SPEC "SolidVertex:vertex" "SolidFragment:fragment")
            string(REPLACE ":" ";" ENTRY_PARTS "${ENTRY_SPEC}")
            list(GET ENTRY_PARTS 0 ENTRY_NAME)
            list(GET ENTRY_PARTS 1 ENTRY_STAGE)
            set(ENTRY_SPV "${FRONTIER_XRAY_SCAN_CAPTURE_OUT}/XRayScanCaptureSolid.${ENTRY_NAME}.spv")
            list(APPEND FRONTIER_XRAY_SCAN_CAPTURE_SPV_OUTPUTS "${ENTRY_SPV}")
            add_custom_command(
                OUTPUT "${ENTRY_SPV}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_XRAY_SCAN_CAPTURE_OUT}"
                COMMAND "${FRONTIER_GLOBAL_ILLUMINATION_SLANGC}" "${CMAKE_SOURCE_DIR}/Exhibits/XRayScan/Code/XRayScanCaptureSolid.slang"
                    -I "${CMAKE_SOURCE_DIR}/Exhibits/XRayScan/Code"
                    -I "${FRONTIER_XRAY_SCAN_SHADER_SRC}"
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
                DEPENDS ${FRONTIER_XRAY_SCAN_SLANG_SOURCES} "${CMAKE_SOURCE_DIR}/Exhibits/XRayScan/Code/XRayScanCaptureSolid.slang"
                VERBATIM)
        endforeach()
        add_custom_target(XRayScanCaptureShaders ALL DEPENDS ${FRONTIER_XRAY_SCAN_CAPTURE_SPV_OUTPUTS})
        add_dependencies(XRayScanCapture XRayScanCaptureShaders)
    endif()
endif()
