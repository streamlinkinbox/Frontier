# Build the consolidated sources directly. No downloaded Frontier tree or patch stack.
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Tools/Bootstrap.py" --profile proof --check
    RESULT_VARIABLE READY)
if(NOT READY EQUAL 0)
    message(FATAL_ERROR "Run: python3 Tools/Bootstrap.py --profile proof")
endif()
include(${CMAKE_SOURCE_DIR}/Tools/Build/ThorVGSoftware.cmake)
add_executable(FrontierNativeProof
    Engine/Editor/EditorHost.cpp
    Engine/Editor/ControlPanel.cpp
    Engine/Editor/OutlinerPanel.cpp
    Engine/Editor/ViewportPanel.cpp
    Engine/Editor/InspectorPanel.cpp
    Engine/Editor/ShadeTick.cpp
    Engine/DisplayPresentation/ControlCentreHost.cpp
    Engine/DisplayPresentation/PixelSpace.cpp
    Engine/DisplayPresentation/MotionIntegrator.cpp
    Engine/DisplayPresentation/ThemeStructure.cpp
    Engine/DisplayPresentation/ControlKit.cpp
    Engine/DisplayPresentation/AppearanceInspector.cpp
    Engine/DisplayPresentation/ConfigurationInspector.cpp
    Engine/DisplayPresentation/DialogueHost.cpp
    Engine/DisplayPresentation/FidelityClassifier.cpp
    Engine/DisplayPresentation/VectorCodec.cpp
    Engine/DisplayPresentation/NotificationQueue.cpp
    Engine/DisplayPresentation/TelemetryMetrics.cpp
    Engine/DisplayPresentation/TypefaceRegistry.cpp
    Engine/DisplayPresentation/GlyphSpace.cpp
    Engine/DisplayPresentation/FontCodec.cpp
    Engine/DisplayPresentation/MaterialInspector.cpp
    Engine/ContentInterchange/AssetResolution.cpp
    Engine/ContentInterchange/MaterialIndex.cpp
    Engine/DeviceExchange/InputExchange.cpp
    Engine/GeometricRaster/CameraProjection.cpp
    Engine/DeviceExchange/OrientationClassifier.cpp
    ExternalPackages/imgui/imgui.cpp
    ExternalPackages/imgui/imgui_draw.cpp
    ExternalPackages/imgui/imgui_tables.cpp
    ExternalPackages/imgui/imgui_widgets.cpp
    Projects/Project-Zero/Source/CelestialSequence.cpp
    Engine/Editor/SunInspectorPanel.cpp
    Engine/Editor/LensFlareInspectorPanel.cpp
    Engine/Editor/AtmosphereSkyInspectorPanel.cpp
    Engine/Editor/MoonInspectorPanel.cpp
    Engine/Editor/StarsInspectorPanel.cpp
    Engine/Editor/CloudsInspectorPanel.cpp
    Engine/Editor/FogInspectorPanel.cpp
    Engine/Editor/WeatherInspectorPanel.cpp
    Engine/Editor/CameraInspectorPanel.cpp
    Engine/DisplayPresentation/CelestialSolver.cpp
    Engine/DisplayPresentation/IconArt.cpp
    Engine/DisplayPresentation/IconPresentation.cpp
    Engine/GeometricRaster/StarCatalogueIndex.cpp
    Engine/GeometricRaster/VisibilityRaster.cpp
    Engine/GeometricRaster/GeometryStructure.cpp
    Engine/GeometricRaster/SceneStructure.cpp
    Projects/Project-Zero/Source/EditorFeedSequence.cpp
    Projects/Project-Zero/Source/FlyThroughSolver.cpp
    Exhibits/Workbench/Billboards/NativeSceneProof.cpp
)
target_include_directories(FrontierNativeProof PRIVATE
    Engine Engine/Editor Engine/DisplayPresentation Engine/ContentInterchange
    Engine/DeviceExchange Engine/GeometricRaster Engine/PhysicalDynamics
    Engine/PlatformInterchange Engine/Shaders Engine/SpatialInterface
    Projects/Project-Zero/Source Projects/Project-Dyno/Source
    ExternalPackages/imgui ExternalPackages/tomlpp/include ExternalPackages/thorvg/inc
    ExternalPackages/stb ExternalPackages/vulkan-headers/include
    Exhibits/Workbench/Editor Exhibits/Workbench/Editor/Counterparts Exhibits/Workbench/IconArt)
target_compile_definitions(FrontierNativeProof PRIVATE FRONTIER_DEVELOPMENT TVG_STATIC)
target_link_libraries(FrontierNativeProof PRIVATE thorvg_static)
if(MSVC)
    target_compile_options(FrontierNativeProof PRIVATE /utf-8 /Gy /O2)
    target_link_options(FrontierNativeProof PRIVATE /OPT:REF)
    target_link_libraries(FrontierNativeProof PRIVATE user32)
else()
    target_compile_options(FrontierNativeProof PRIVATE -O2 -ffunction-sections -fdata-sections)
    target_link_options(FrontierNativeProof PRIVATE -Wl,--gc-sections)
endif()
enable_testing()
add_test(NAME BootstrapSafety COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Tools/Tests/TestBootstrap.py")
add_test(NAME NativeBillboards COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Tools/Build/RunNativeProof.py"
    $<TARGET_FILE:FrontierNativeProof> "${CMAKE_BINARY_DIR}/evidence")
set_tests_properties(NativeBillboards PROPERTIES TIMEOUT 900)

add_test(NAME NativeWindBindings COMMAND FrontierNativeProof --wind-bindings)
set_tests_properties(NativeWindBindings PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
