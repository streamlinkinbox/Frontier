#=====================================================================================================================================
#                                                            SOLIDARC.CMAKE
#=====================================================================================================================================
# 🧩 SolidArc targets: geometry library, shared orbit camera, UI library, application, capture, validations and walkthroughs

# 📝 Included by the Frontier root CMakeLists.txt (include(Experimental/SolidArc/Native/SolidArc.cmake)) after FrontierImGui
#    and glfw exist, or by the standalone Experimental/SolidArc/Native/CMakeLists.txt that defines the same two targets from
#    the same pins. Paths are resolved from FRONTIER_ROOT (the repository root), never from the including directory.

#-------------------------------------------------------------------------------------------------------------------
#                                                    PATHS
#-------------------------------------------------------------------------------------------------------------------

if(NOT DEFINED FRONTIER_ROOT)
    set(FRONTIER_ROOT "${CMAKE_SOURCE_DIR}")
endif()
set(SOLIDARC_UI       "${FRONTIER_ROOT}/UI/SolidArc")                                   # panels, document, viewport, shaders
set(SOLIDARC_NATIVE   "${FRONTIER_ROOT}/Experimental/SolidArc/Native")                  # application, options, tooling
set(SOLIDARC_EXHIBIT  "${FRONTIER_ROOT}/Exhibits/SolidArc/Code")                        # capture, validation, walkthrough scripts
set(SOLIDARC_PACKAGES "${FRONTIER_ROOT}/ExternalPackages")                              # pinned third-party sources
set(SOLIDARC_SLANG_COMPILER "" CACHE FILEPATH "slang-wasm.js of slang-loader 0.0.7: regenerate the shader archive when the .slang files change")
option(SOLIDARC_WINDOWED_HOST "Build the GLFW windowed host" ON)

# 📝 Upstream ships DM Sans under FontArchives/DMSans; the standalone tree kept it under FontArchives/DM Sans.
set(SOLIDARC_FONT_FILE "${FRONTIER_ROOT}/EngineContent/FontArchives/DMSans/DMSans-Regular.ttf")
if(NOT EXISTS "${SOLIDARC_FONT_FILE}")
    set(SOLIDARC_FONT_FILE "${FRONTIER_ROOT}/EngineContent/FontArchives/DM Sans/DMSans-Regular.ttf")
endif()

foreach(Witness imgui/backends/imgui_impl_vulkan.cpp tomlpp/include/toml++/toml.hpp vulkan-headers/include/vulkan/vulkan.h stb/stb_image_write.h volk/volk.h)
    if(NOT EXISTS "${SOLIDARC_PACKAGES}/${Witness}")
        message(FATAL_ERROR "Missing ${SOLIDARC_PACKAGES}/${Witness} — run: python Automation/PrepareUI.py")
    endif()
endforeach()
foreach(Required FrontierImGui FrontierInterface)
    if(NOT TARGET ${Required})
        message(FATAL_ERROR "SolidArc.cmake needs the ${Required} target before it is included")
    endif()
endforeach()

#-------------------------------------------------------------------------------------------------------------------
#                                                    THIRD PARTY
#-------------------------------------------------------------------------------------------------------------------

add_subdirectory("${FRONTIER_ROOT}/Engine/GeometryProcessing" "${CMAKE_BINARY_DIR}/GeometryProcessing")

add_library(FrontierOrbitCamera STATIC "${FRONTIER_ROOT}/Engine/Render/OrbitCamera.cpp")
target_include_directories(FrontierOrbitCamera PUBLIC "${FRONTIER_ROOT}")
target_compile_features(FrontierOrbitCamera PUBLIC cxx_std_20)

# 📝 volk: the Vulkan meta-loader. No link-time dependency on vulkan-1 / libvulkan; the loader (or a driver such as
#    SwiftShader, for headless captures) is resolved at run time. Upstream has no volk pin: UIDependencies.json gains one.
add_library(SolidArcVolk STATIC "${SOLIDARC_PACKAGES}/volk/volk.c")
target_include_directories(SolidArcVolk SYSTEM PUBLIC "${SOLIDARC_PACKAGES}/volk" "${SOLIDARC_PACKAGES}/vulkan-headers/include")
target_compile_definitions(SolidArcVolk PUBLIC VK_NO_PROTOTYPES)
if(WIN32)
    target_compile_definitions(SolidArcVolk PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_link_libraries(SolidArcVolk PUBLIC ${CMAKE_DL_LIBS})
endif()

# 📝 ImGui itself is the shared FrontierImGui (patched, 32-bit indices); only the Vulkan backend is compiled here, on volk.
add_library(SolidArcImGuiVulkan STATIC "${SOLIDARC_PACKAGES}/imgui/backends/imgui_impl_vulkan.cpp")
target_include_directories(SolidArcImGuiVulkan SYSTEM PUBLIC "${SOLIDARC_PACKAGES}/imgui/backends")
target_compile_definitions(SolidArcImGuiVulkan PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(SolidArcImGuiVulkan PUBLIC FrontierImGui SolidArcVolk)

add_library(SolidArcTomlPlusPlus INTERFACE)
target_include_directories(SolidArcTomlPlusPlus SYSTEM INTERFACE "${SOLIDARC_PACKAGES}/tomlpp/include")

add_library(SolidArcStb INTERFACE)
target_include_directories(SolidArcStb SYSTEM INTERFACE "${SOLIDARC_PACKAGES}/stb")

#-------------------------------------------------------------------------------------------------------------------
#                                                    SHADER ARCHIVE
#-------------------------------------------------------------------------------------------------------------------

# 📝 The SPIR-V archive is committed (UI/SolidArc/Viewport/Generated/SolidArcShaderArchive.h) so a build needs no shader
#    compiler. With SOLIDARC_SLANG_COMPILER set, editing a .slang file regenerates it through Node.js + slang-loader.
set(SOLIDARC_SHADERS
    "${SOLIDARC_UI}/Shaders/SolidArcProjection.slang"
    "${SOLIDARC_UI}/Shaders/SolidArcSurface.slang"
    "${SOLIDARC_UI}/Shaders/SolidArcEdge.slang"
    "${SOLIDARC_UI}/Shaders/SolidArcGrid.slang"
    "${SOLIDARC_UI}/Shaders/SolidArcImagePlane.slang"
    "${SOLIDARC_UI}/Shaders/SolidArcGizmo.slang"
    "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/CadStroke.slang"                                # Shared with Frontier CadEdge
    "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/MatCapLookup.slang"                             # Shared with Frontier MatCapSurface
)
set(SOLIDARC_SHADER_ARCHIVE "${SOLIDARC_UI}/Viewport/Generated/SolidArcShaderArchive.h")
if(SOLIDARC_SLANG_COMPILER)
    find_program(SOLIDARC_NODE node REQUIRED)
    add_custom_command(
        OUTPUT  "${SOLIDARC_SHADER_ARCHIVE}"
        COMMAND "${SOLIDARC_NODE}" "${SOLIDARC_NATIVE}/Tooling/CompileSolidArcShaders.mjs" --compiler "${SOLIDARC_SLANG_COMPILER}"
        DEPENDS ${SOLIDARC_SHADERS} "${SOLIDARC_NATIVE}/Tooling/CompileSolidArcShaders.mjs"
        COMMENT "Compiling SolidArc Slang shaders to SPIR-V"
        VERBATIM)
endif()
add_custom_target(SolidArcShaderArchive DEPENDS "${SOLIDARC_SHADER_ARCHIVE}")

#-------------------------------------------------------------------------------------------------------------------
#                                                    LIBRARY
#-------------------------------------------------------------------------------------------------------------------

# 📝 Document layer without Vulkan / ImGui: the project browser host links it (with the browser link) next to its own backend.
add_library(FrontierSolidArcDocument STATIC
    ${SOLIDARC_UI}/Document/SolidArcDocument.cpp
    ${SOLIDARC_UI}/Document/SolidArcDocumentCodec.cpp
    ${SOLIDARC_UI}/Document/VehicleCurveImport.cpp
    ${SOLIDARC_UI}/Document/SolidArcImagePlane.cpp
    ${SOLIDARC_UI}/Document/SolidArcMatcap.cpp
    ${SOLIDARC_UI}/Document/SolidArcMeasurement.cpp
    ${SOLIDARC_UI}/Document/SolidArcDrawingSheet.cpp
    ${SOLIDARC_UI}/Document/SolidArcExchange.cpp
    ${SOLIDARC_UI}/Document/SolidArcKnife.cpp
    ${SOLIDARC_UI}/Document/SolidArcScene.cpp
    ${SOLIDARC_UI}/Document/SolidArcSpecimen.cpp
)
target_include_directories(FrontierSolidArcDocument PUBLIC "${FRONTIER_ROOT}")
target_link_libraries(FrontierSolidArcDocument PUBLIC FrontierGeometryProcessing FrontierOrbitCamera SolidArcTomlPlusPlus SolidArcStb)

add_library(FrontierSolidArcUI STATIC
    ${SOLIDARC_UI}/Viewport/SolidArcCamera.cpp
    ${SOLIDARC_UI}/Viewport/SolidArcDevice.cpp
    ${SOLIDARC_UI}/Viewport/SolidArcRasterization.cpp
    ${SOLIDARC_UI}/Viewport/SolidArcGizmo.cpp
    ${SOLIDARC_UI}/Viewport/SolidArcPortableImage.cpp
    ${SOLIDARC_UI}/Viewport/SolidArcSnapping.cpp
    ${SOLIDARC_UI}/Viewport/SolidArcSurfaceTracing.cpp
    ${SOLIDARC_UI}/Interface/SolidArcInterface.cpp
    ${SOLIDARC_UI}/Interface/SolidArcNavigation.cpp
    ${SOLIDARC_UI}/Interface/SolidArcOutliner.cpp
    ${SOLIDARC_UI}/Interface/SolidArcStyleSheet.cpp
    ${SOLIDARC_UI}/Interface/SolidArcFrame.cpp
    ${SOLIDARC_UI}/Interface/SolidArcOptions.cpp
    ${SOLIDARC_UI}/Interface/SolidArcScript.cpp
    ${SOLIDARC_UI}/Interface/SolidArcFileDialog.cpp
)
add_dependencies(FrontierSolidArcUI SolidArcShaderArchive)
if(WIN32)
    target_link_libraries(FrontierSolidArcUI PUBLIC comdlg32)                                # GetOpenFileNameW (Image plane › Browse)
endif()
target_include_directories(FrontierSolidArcUI PUBLIC "${FRONTIER_ROOT}")
target_link_libraries(FrontierSolidArcUI PUBLIC FrontierSolidArcDocument FrontierInterface FrontierAuthoringControls SolidArcVolk SolidArcImGuiVulkan SolidArcTomlPlusPlus SolidArcStb)
target_compile_definitions(FrontierSolidArcUI PRIVATE SOLIDARC_FONT_PATH="${SOLIDARC_FONT_FILE}" SOLIDARC_CONTENT_ROOT="${FRONTIER_ROOT}")
if(NOT MSVC)
    target_compile_options(FrontierSolidArcUI PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    target_compile_options(FrontierSolidArcDocument PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# 📝 S5 project browser link: .solidarc previews (Engine/Render/BrowserPreviewGeometry.h layout) and the workspace launch.
#    Upstream only (FrontierEditorUI owns ProjectBrowserPanel); the Windows Frontier host links it below.
if(TARGET FrontierEditorUI)
    add_library(FrontierSolidArcBrowser STATIC
        ${SOLIDARC_UI}/Browser/SolidArcBrowserPreview.cpp
        ${SOLIDARC_UI}/Browser/SolidArcBrowserLink.cpp
    )
    target_link_libraries(FrontierSolidArcBrowser PUBLIC FrontierSolidArcDocument FrontierEditorUI)
    if(NOT MSVC)
        target_compile_options(FrontierSolidArcBrowser PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    endif()
endif()

#-------------------------------------------------------------------------------------------------------------------
#                                                    EXECUTABLES
#-------------------------------------------------------------------------------------------------------------------

add_executable(SolidArcCapture "${SOLIDARC_EXHIBIT}/SolidArcCapture.cpp")
target_link_libraries(SolidArcCapture PRIVATE FrontierSolidArcUI)

if(SOLIDARC_WINDOWED_HOST AND TARGET glfw)
    # 📝 Target SolidArc, executable SolidArc (the same pattern as TextureStudio → TextureWorkspace upstream).
    add_executable(SolidArc "${SOLIDARC_NATIVE}/SolidArcApplication.cpp" "${SOLIDARC_PACKAGES}/imgui/backends/imgui_impl_glfw.cpp")
    set_target_properties(SolidArc PROPERTIES OUTPUT_NAME SolidArcWorkspace)
    target_compile_definitions(SolidArc PRIVATE GLFW_INCLUDE_NONE)
    target_link_libraries(SolidArc PRIVATE FrontierSolidArcUI glfw)
    if(WIN32)
        set_target_properties(SolidArc PROPERTIES WIN32_EXECUTABLE ON)
        target_link_options(SolidArc PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/ENTRY:mainCRTStartup>)
        # 📝 Version details (Package/SolidArcWorkspace.rc); no icon and no custom manifest. MSVC only, like the
        #    Frontier and TextureWorkspace version resources: the Zig cross-toolchain has no resource compiler.
        if(MSVC)
            enable_language(RC)
            target_sources(SolidArc PRIVATE "${SOLIDARC_NATIVE}/Package/SolidArcWorkspace.rc")
        endif()
        # 📝 MinGW-style toolchains (the Zig cross-build) keep a console window unless the subsystem is named explicitly.
        if(NOT MSVC)
            target_link_options(SolidArc PRIVATE "-Wl,--subsystem,windows")
        endif()
    endif()
endif()

# 📝 S6 package (cmake --install <build> --component SolidArc): the workspace with its package options, DM Sans, the icon
#    artwork, the curve sample and the third-party notices. Experimental/SolidArc/Native/Tooling/CrossBuildWindows.sh zips it.
if(TARGET SolidArc)
    install(TARGETS SolidArc RUNTIME DESTINATION . COMPONENT SolidArc)
    install(FILES "${SOLIDARC_NATIVE}/Package/SolidArcWorkspace.toml" "${SOLIDARC_NATIVE}/Package/README.md"
            DESTINATION . COMPONENT SolidArc)
    install(DIRECTORY "${FRONTIER_ROOT}/EngineContent/FontArchives/DMSans/" DESTINATION EngineContent/FontArchives/DMSans COMPONENT SolidArc)
    install(DIRECTORY "${FRONTIER_ROOT}/EngineContent/Icons/" DESTINATION EngineContent/Icons COMPONENT SolidArc
            PATTERN "SolidArc" EXCLUDE)
    install(DIRECTORY "${FRONTIER_ROOT}/EngineContent/Icons/SolidArc/" DESTINATION EngineContent/Icons/SolidArc COMPONENT SolidArc)
    install(FILES "${FRONTIER_ROOT}/Experimental/VehicleSurfacing/Samples/SyntheticBonnet.toml" DESTINATION Samples COMPONENT SolidArc)
    install(FILES "${SOLIDARC_PACKAGES}/imgui/LICENSE.txt"   DESTINATION Notices RENAME ImGui-LICENSE.txt      COMPONENT SolidArc)
    install(FILES "${SOLIDARC_PACKAGES}/glfw/LICENSE.md"     DESTINATION Notices RENAME GLFW-LICENSE.md        COMPONENT SolidArc)
    install(FILES "${SOLIDARC_PACKAGES}/tomlpp/LICENSE"      DESTINATION Notices RENAME TomlPlusPlus-LICENSE.txt COMPONENT SolidArc)
    install(FILES "${SOLIDARC_PACKAGES}/stb/LICENSE"         DESTINATION Notices RENAME Stb-LICENSE.txt        COMPONENT SolidArc)
    install(FILES "${SOLIDARC_PACKAGES}/volk/LICENSE.md"     DESTINATION Notices RENAME Volk-LICENSE.md        COMPONENT SolidArc)
    install(FILES "${SOLIDARC_PACKAGES}/thorvg/LICENSE"      DESTINATION Notices RENAME ThorVG-LICENSE.txt     COMPONENT SolidArc)
    install(DIRECTORY "${SOLIDARC_PACKAGES}/vulkan-headers/LICENSES" DESTINATION Notices/Vulkan-Headers COMPONENT SolidArc)
endif()

# 📝 The Windows Frontier browser host decodes .solidarc previews and starts SolidArcWorkspace beside itself.
if(TARGET FrontierSolidArcBrowser AND TARGET Frontier)
    target_link_libraries(Frontier PRIVATE FrontierSolidArcBrowser)
    target_compile_definitions(Frontier PRIVATE FRONTIER_SOLIDARC_ENABLED=1)
    if(TARGET SolidArc)
        add_dependencies(Frontier SolidArc)
    endif()
endif()

# 📝 S5 evidence: the upstream project browser + reference preview renderer, offscreen, with the SolidArc link. The hand-off
#    starts SolidArcCapture (same --open contract as SolidArcWorkspace) and waits for its first capture.
if(TARGET FrontierSolidArcBrowser AND TARGET FrontierSurfaceRasterization)
    add_executable(SolidArcBrowserValidation "${SOLIDARC_EXHIBIT}/SolidArcBrowserValidation.cpp")
    target_link_libraries(SolidArcBrowserValidation PRIVATE FrontierSolidArcUI FrontierSolidArcBrowser FrontierSurfaceRasterization)
    # 📝 A build without a Slang compiler has no Surface set of its own: the test reads the stored set, as the editor then does.
    set(SOLIDARC_BROWSER_SHADERS "${FRONTIER_ROOT}/EngineContent/ShaderArchives/Surface")
    if(TARGET FrontierSurfaceShaders)
        add_dependencies(SolidArcBrowserValidation FrontierSurfaceShaders)
        set(SOLIDARC_BROWSER_SHADERS "${FRONTIER_SURFACE_SHADER_DIRECTORY}")
    endif()
    add_dependencies(SolidArcBrowserValidation SolidArcCapture)
    add_test(NAME SolidArcBrowserValidation
             COMMAND SolidArcBrowserValidation --exhibits "${CMAKE_BINARY_DIR}/SolidArc/BrowserValidation"
                     --shaders "${SOLIDARC_BROWSER_SHADERS}" --workspace "$<TARGET_FILE:SolidArcCapture>")
    set_tests_properties(SolidArcBrowserValidation PROPERTIES WORKING_DIRECTORY "${FRONTIER_ROOT}" SKIP_RETURN_CODE 77 TIMEOUT 900)
endif()

add_executable(SolidArcDocumentValidation "${SOLIDARC_EXHIBIT}/SolidArcDocumentValidation.cpp")
target_link_libraries(SolidArcDocumentValidation PRIVATE FrontierSolidArcUI)
target_compile_definitions(SolidArcDocumentValidation PRIVATE SOLIDARC_VEHICLE_SAMPLE="${FRONTIER_ROOT}/Experimental/VehicleSurfacing/Samples/SyntheticBonnet.toml"
                                                                     SOLIDARC_STEP_FIXTURES="${FRONTIER_ROOT}/Exhibits/GeometryProcessing/Code/StepFixtures"
                                                                     SOLIDARC_ICON_FOLDER="${FRONTIER_ROOT}/EngineContent/Icons/SolidArc")

add_executable(SolidArcViewportValidation "${SOLIDARC_EXHIBIT}/SolidArcViewportValidation.cpp")
target_link_libraries(SolidArcViewportValidation PRIVATE FrontierSolidArcUI)

enable_testing()
add_test(NAME SolidArcDocumentValidation COMMAND SolidArcDocumentValidation)
add_test(NAME SolidArcViewportValidation COMMAND SolidArcViewportValidation)
set_tests_properties(SolidArcViewportValidation PROPERTIES SKIP_RETURN_CODE 77)

# 📝 Interface walkthroughs: real ImGui input on the offscreen Vulkan frame (set SOLIDARC_VULKAN_DRIVER for SwiftShader).
#    Run from the repository root because the scripts name _ClaudeScratch/SolidArcWorkspace/… files; the recovery
#    walkthrough consumes the autosave the first one leaves behind.
set(SOLIDARC_WALKTHROUGH_CAPTURES "${CMAKE_BINARY_DIR}/SolidArc/WalkthroughCaptures")
add_test(NAME SolidArcWorkspaceWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/Recovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Bracket.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/WorkspaceWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcRecoveryWalkthrough
         COMMAND SolidArcCapture
                 --options "${SOLIDARC_EXHIBIT}/Scripts/RecoveryOptions.toml"
                 --script "${SOLIDARC_EXHIBIT}/Scripts/RecoveryWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcShortcutWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ShortcutRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Shortcut.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ShortcutWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcVariableBlendWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/VariableRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Variable.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/VariableBlendWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcSketchDrawingWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/SketchRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Sketch.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/SketchDrawingWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcConstructDrawingWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ConstructRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Construct.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ConstructDrawingWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcGizmoWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/GizmoRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Gizmo.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/GizmoWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcSketchPointsWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/SketchPointsRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/SketchPoints.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/SketchPointsWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcShellsWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ShellsRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Shells.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ShellsWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcAltSlideWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/AltSlideRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/AltSlide.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/AltSlideWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcFillWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/FillRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Fill.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/FillWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcFilletDragWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/FilletDragRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/FilletDrag.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/FilletDragWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcOrientWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/OrientRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Orient.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/OrientWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcExtrudeManifoldWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ManifoldRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Manifold.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ExtrudeManifoldWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcExtrudeAxisWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ExtrudeAxisRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/ExtrudeAxis.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ExtrudeAxisWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcEdgePathWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/EdgePathRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/EdgePath.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/EdgePathWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcWrapWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/WrapRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Wrap.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/WrapWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcWrapPrecisionWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/WrapPrecisionRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/WrapPrecision.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/WrapPrecisionWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcBevelReachWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/BevelReachRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/BevelReach.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/BevelReachWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcBevelMergeWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/BevelMergeRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/BevelMerge.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/BevelMergeWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcCurveRequestWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/CurveRequestRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/CurveRequest.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/CurveRequestWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcReshapeWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ReshapeRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Reshape.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ReshapeWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcDeleteWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/DeleteRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Delete.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/DeleteWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcImprintWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ImprintRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Imprint.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ImprintWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcPointCutWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/PointCutRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/PointCut.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/PointCutWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcCurveFlowWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/CurveFlowRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/CurveFlow.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/CurveFlowWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcKnifeWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/KnifeRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Knife.solidarc
                 --inserts _ClaudeScratch/SolidArcWorkspace/KnifeInserts
                 --script "${SOLIDARC_EXHIBIT}/Scripts/KnifeWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcBevelHandlesWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/BevelHandlesRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/BevelHandles.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/BevelHandlesWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcResponsivenessWalkthrough
         COMMAND SolidArcCapture --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ResponsivenessRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Responsiveness.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ResponsivenessWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
                 --trace _ClaudeScratch/SolidArcWorkspace/ResponsivenessTrace.log
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcEdgeStrokeWalkthrough
         COMMAND SolidArcCapture --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/EdgeStrokeRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/EdgeStroke.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/EdgeStrokeWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcSketchCornersWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/SketchCornersRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/SketchCorners.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/SketchCornersWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcTrimJoinWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/TrimJoinRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/TrimJoin.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/TrimJoinWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcInsetWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/InsetRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Inset.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/InsetWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcVehicleSurfacingWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/VehicleRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Vehicle.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/VehicleSurfacingWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcPanelAssemblyWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/PanelRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Panels.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/PanelAssemblyWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcSurfaceEvaluationWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/EvaluationRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Evaluation.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/SurfaceEvaluationWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
# 📝 Blueprint study: the BMW three-view is third-party and never committed; the walkthrough runs only where a copy sits at
#    _ClaudeScratch/Blueprint/BmwM3E90.jpg (see Experimental/VehicleSurfacing/Samples/BmwM3E90Calibration.toml).
if(EXISTS "${FRONTIER_ROOT}/_ClaudeScratch/Blueprint/BmwM3E90.jpg")
    add_test(NAME SolidArcBlueprintModellingWalkthrough
             COMMAND SolidArcCapture --specimen --reset-files
                     --recovery _ClaudeScratch/SolidArcWorkspace/BlueprintRecovery.solidarc
                     --document _ClaudeScratch/SolidArcWorkspace/Blueprint.solidarc
                     --script "${SOLIDARC_EXHIBIT}/Scripts/BlueprintModellingWalkthrough.txt"
                     --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
             WORKING_DIRECTORY "${FRONTIER_ROOT}")
    set_tests_properties(SolidArcBlueprintModellingWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
endif()
add_test(NAME SolidArcMatcapWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/MatcapRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Matcap.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/MatcapWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcMatcapWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME SolidArcMultiBodyWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/MultiBodyRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/MultiBody.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/MultiBodyWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcMultiBodyWalkthrough PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME SolidArcTrimSplitWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/TrimSplitRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/TrimSplit.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/TrimSplitWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcTrimSplitWalkthrough PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME SolidArcEditingWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/EditingRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Editing.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/EditingWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcEditingWalkthrough PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME SolidArcConstraintsWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ConstraintsRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Constraints.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ConstraintsWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcConstraintsWalkthrough PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME SolidArcLinkedConstraintsWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/LinkedConstraintsRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/LinkedConstraints.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/LinkedConstraintsWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcLinkedConstraintsWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
# SolidArc revolve / sweep walkthrough — Alt+R revolve (proposed y-axis, X / Y keys, a clicked profile line as
# the axis, typed degrees) and Shift+P / Alt+E evolution (proposed path, a clicked curve switches it), staged previews.
add_test(NAME SolidArcRevolveSweepWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/RevolveSweepRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/RevolveSweep.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/RevolveSweepWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
add_test(NAME SolidArcIncrementalReplayWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ReplayRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Replay.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/IncrementalReplayWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcIncrementalReplayWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME SolidArcXRayWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/XRayRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/XRay.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/XRayWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcXRayWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME SolidArcMeasureSectionWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/MeasureRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Measure.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/MeasureSectionWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcMeasureSectionWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME SolidArcDrawingSheetWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/SheetRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Sheet.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/DrawingSheetWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcDrawingSheetWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME SolidArcExchangeWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/ExchangeRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Exchange.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/ExchangeWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcExchangeWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME SolidArcSnappingWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/SnapRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Snap.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/SnappingWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcSnappingWalkthrough PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME SolidArcNurbsBlendWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/NurbsBlendRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/NurbsBlend.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/NurbsBlendWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
# Each NURBS blend takes 40–60 s (rolling-ball assembly plus audit), five of them plus the cut: allow 40 minutes.
set_tests_properties(SolidArcNurbsBlendWalkthrough PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 2400)

add_test(NAME SolidArcAssemblyWalkthrough
         COMMAND SolidArcCapture --specimen --reset-files
                 --recovery _ClaudeScratch/SolidArcWorkspace/AssemblyRecovery.solidarc
                 --document _ClaudeScratch/SolidArcWorkspace/Assembly.solidarc
                 --script "${SOLIDARC_EXHIBIT}/Scripts/AssemblyWalkthrough.txt"
                 --exhibits "${SOLIDARC_WALKTHROUGH_CAPTURES}"
         WORKING_DIRECTORY "${FRONTIER_ROOT}")
set_tests_properties(SolidArcAssemblyWalkthrough PROPERTIES SKIP_RETURN_CODE 77)

set_tests_properties(SolidArcPanelAssemblyWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcSurfaceEvaluationWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcSketchDrawingWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcConstructDrawingWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcInsetWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcGizmoWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcReshapeWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcWrapPrecisionWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcCurveRequestWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcBevelReachWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcBevelMergeWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcDeleteWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcSketchPointsWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcShellsWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcTrimJoinWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcKnifeWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcPointCutWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcCurveFlowWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcBevelHandlesWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcSketchCornersWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcAltSlideWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcVehicleSurfacingWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcShortcutWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcVariableBlendWalkthrough PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(SolidArcWorkspaceWalkthrough PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_SETUP SolidArcAutosave)
set_tests_properties(SolidArcRecoveryWalkthrough PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED SolidArcAutosave)

include("${SOLIDARC_EXHIBIT}/Integration/Validation.cmake")

add_executable(SolidArcCurveWorkflowValidation "${SOLIDARC_EXHIBIT}/SolidArcCurveWorkflowValidation.cpp")
target_link_libraries(SolidArcCurveWorkflowValidation PRIVATE FrontierSolidArcUI)
add_test(NAME SolidArcCurveWorkflowValidation COMMAND SolidArcCurveWorkflowValidation)
