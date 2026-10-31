# Native Project Everest browser, editor workspace, and lifecycle validation.
# Build products stay under Build/ (ignored); captured evidence is copied into Exhibits/ProjectEverest/Images.
cmake_minimum_required(VERSION 3.20)
project(ProjectEverestNative LANGUAGES CXX)

include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(FRONTIER_ENGINE_ROOT "${REPOSITORY_ROOT}/Frontier")

find_path(TOMLPLUSPLUS_INCLUDE_DIR toml++/toml.h
    HINTS "${FRONTIER_TOMLPLUSPLUS_DIR}/include"
          "${REPOSITORY_ROOT}/ExternalPackages/tomlpp/include")
if(NOT TOMLPLUSPLUS_INCLUDE_DIR)
    message(FATAL_ERROR "toml++ is required; prepare the pinned UI dependencies or set FRONTIER_TOMLPLUSPLUS_DIR")
endif()

find_package(Threads REQUIRED)

# Portable host/descriptor code used by the production headless command and validation target.
add_library(FrontierEnginePortable STATIC
    "${FRONTIER_ENGINE_ROOT}/Engine/Diagnostics/LogRingStore.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Diagnostics/LogCategories.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Diagnostics/LogStream.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/ProjectContent.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/ProjectDescriptor.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/LogStreamPanel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/LogStreamPanelPresentation.cpp"
    "${FRONTIER_ENGINE_ROOT}/Applications/Editor/CommandLine.cpp"
    "${FRONTIER_ENGINE_ROOT}/Applications/Editor/ProjectBrowserHost.cpp")
target_include_directories(FrontierEnginePortable PUBLIC
    "${REPOSITORY_ROOT}" "${FRONTIER_ENGINE_ROOT}" "${TOMLPLUSPLUS_INCLUDE_DIR}")
target_link_libraries(FrontierEnginePortable PUBLIC Threads::Threads)

add_executable(Frontier
    "${FRONTIER_ENGINE_ROOT}/Applications/Editor/FrontierMain.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/SceneInspectorPanel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/SceneDirectoryPanel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/RecordStore.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/RecordEntry.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/SceneDirectory.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/EnvironmentRecords.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/AtmosphereModel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/CloudField.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/FogMedium.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/CelestialModel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/ClusterNesting.cpp")
target_link_libraries(Frontier PRIVATE FrontierEnginePortable)

# Compile the actual GLFW-backed native entry point against the production SDK contracts as well.
# It is an object-only validation target here: no product binary or GPU presentation is delivered.
add_library(FrontierNativeEntry OBJECT
    "${FRONTIER_ENGINE_ROOT}/Applications/Editor/FrontierMain.cpp")
target_include_directories(FrontierNativeEntry PRIVATE
    "${REPOSITORY_ROOT}"
    "${FRONTIER_ENGINE_ROOT}"
    "${REPOSITORY_ROOT}/ExternalPackages/imgui"
    "${REPOSITORY_ROOT}/ExternalPackages/glfw/include"
    "${TOMLPLUSPLUS_INCLUDE_DIR}")
target_compile_definitions(FrontierNativeEntry PRIVATE
    FRONTIER_WITH_INTERFACE_SDK=1
    FRONTIER_EDITOR_PROFILE=1
    GLFW_INCLUDE_NONE=1)

add_executable(NativeProjectValidation
    NativeProjectValidation.cpp
    "${FRONTIER_ENGINE_ROOT}/Applications/Editor/ProjectBrowserHost.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Diagnostics/LogCategories.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Diagnostics/LogRingStore.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Diagnostics/LogStream.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/ProjectContent.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/ProjectDescriptor.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/SceneDirectoryPanel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/EnvironmentRecords.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/RecordEntry.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/RecordStore.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/SceneDirectory.cpp")
target_include_directories(NativeProjectValidation PRIVATE
    "${REPOSITORY_ROOT}" "${FRONTIER_ENGINE_ROOT}" "${TOMLPLUSPLUS_INCLUDE_DIR}")
target_compile_definitions(NativeProjectValidation PRIVATE FRONTIER_EDITOR_PROFILE=1)
target_link_libraries(NativeProjectValidation PRIVATE Threads::Threads)

# The capture executable is the same native ImGui presentation and docking/control stack as FrontierMain.
# Only the host window/GPU presentation is replaced by PanelCapture's deterministic CPU rasterizer.
set(FRONTIER_ROOT "${REPOSITORY_ROOT}")
include("${REPOSITORY_ROOT}/Automation/ThorVGSoftware.cmake")
set(IMGUI_ROOT "${REPOSITORY_ROOT}/ExternalPackages/imgui")
set(STB_ROOT "${REPOSITORY_ROOT}/ExternalPackages/stb")
if(NOT EXISTS "${IMGUI_ROOT}/imgui.cpp" OR NOT EXISTS "${STB_ROOT}/stb_image.h")
    message(FATAL_ERROR "Prepare pinned UI dependencies first: python3 Automation/PrepareUI.py")
endif()

set(NATIVE_UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../LogStream/PanelCapture.cpp"
    "${FRONTIER_ENGINE_ROOT}/Applications/Editor/ProjectBrowserHost.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Diagnostics/LogCategories.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Diagnostics/LogRingStore.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Diagnostics/LogStream.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/ProjectContent.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/ProjectDescriptor.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/LogStreamPanel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/LogStreamPanelPresentation.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/SceneDirectoryPanel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/SceneDirectoryPanelPresentation.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/VehicleHmiGlyphs.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/SceneInspectorPanel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Interface/StackRailControls.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/EnvironmentRecords.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/RecordEntry.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/RecordStore.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Scene/SceneDirectory.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/AtmosphereModel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/CloudField.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/FogMedium.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/CelestialModel.cpp"
    "${FRONTIER_ENGINE_ROOT}/Engine/Rendering/ClusterNesting.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/AppearanceCodec.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/AppearanceInspector.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/ConfigurationInspector.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/ConfigurationStructure.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/ConfirmationDialogue.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/ContentDirectory.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/ControlKit.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/FontCodec.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/GlyphSpace.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/IconArt.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/IconPresentation.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/InputPreferencesCodec.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/MonitorPlacement.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/MotionIntegrator.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/NotchMotion.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/NotificationSequence.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/PanelDockspace.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/PanelDockspaceLayout.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/PanelDockspaceMonitor.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/PanelExpansionTab.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/PixelSpace.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/TextEntryState.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/TextFileReplacement.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/ThemeStructure.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/TypefaceRegistry.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/VectorAuthoringControls.cpp"
    "${REPOSITORY_ROOT}/Engine/Interface/VectorCodec.cpp"
    "${REPOSITORY_ROOT}/UI/Editor/ContentBrowserPanel.cpp"
    "${REPOSITORY_ROOT}/UI/Editor/ControlCentrePanel.cpp"
    "${REPOSITORY_ROOT}/UI/Editor/FoundationPanel.cpp"
    "${REPOSITORY_ROOT}/Engine/ContentInterchange/ProjectSpaceCodec.cpp"
    "${REPOSITORY_ROOT}/Engine/Render/StbImageImplementation.cpp"
    "${REPOSITORY_ROOT}/Engine/Render/StbImageWriteImplementation.cpp"
    "${IMGUI_ROOT}/imgui.cpp"
    "${IMGUI_ROOT}/imgui_draw.cpp"
    "${IMGUI_ROOT}/imgui_tables.cpp"
    "${IMGUI_ROOT}/imgui_widgets.cpp")

add_executable(NativeUIEvidence ${NATIVE_UI_SOURCES})
target_include_directories(NativeUIEvidence PRIVATE
    "${REPOSITORY_ROOT}"
    "${FRONTIER_ENGINE_ROOT}"
    "${IMGUI_ROOT}"
    "${TOMLPLUSPLUS_INCLUDE_DIR}"
    "${STB_ROOT}")
target_compile_definitions(NativeUIEvidence PRIVATE
    FRONTIER_WITH_INTERFACE_SDK=1
    FRONTIER_WITH_WORKSPACE_CAPTURE=1
    FRONTIER_EDITOR_PROFILE=1
    TVG_STATIC=1)
target_link_libraries(NativeUIEvidence PRIVATE thorvg_static Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FrontierNativeEntry PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(NativeProjectValidation PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(NativeUIEvidence PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_test(NAME NativeProjectValidation
        COMMAND NativeProjectValidation "${REPOSITORY_ROOT}")
    add_test(NAME FrontierProjectZeroHeadless
        COMMAND Frontier
            "--project=${FRONTIER_ENGINE_ROOT}/Projects/Project-Zero/ProjectZero.frontier"
            --headless)
    set_tests_properties(NativeProjectValidation FrontierProjectZeroHeadless PROPERTIES
        WORKING_DIRECTORY "${REPOSITORY_ROOT}")

    set(NATIVE_CAPTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/native-captures")
    set(NATIVE_CAPTURE_FONT "${REPOSITORY_ROOT}/EngineContent/FontArchives/Inter/Inter-Regular.otf")
    add_test(NAME NativeProjectBrowserFlow
        COMMAND NativeUIEvidence "${NATIVE_CAPTURE_FONT}" "${NATIVE_CAPTURE_ROOT}/browser"
            --browser "${REPOSITORY_ROOT}")
    add_test(NAME NativeProjectEverestWorkspace
        COMMAND NativeUIEvidence "${NATIVE_CAPTURE_FONT}" "${NATIVE_CAPTURE_ROOT}/workspace"
            --workspace "${REPOSITORY_ROOT}")
    set_tests_properties(NativeProjectBrowserFlow NativeProjectEverestWorkspace PROPERTIES
        WORKING_DIRECTORY "${REPOSITORY_ROOT}")
endif()
