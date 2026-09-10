#=============================================================================================================================================
# Editor/EditorTools/ParametricSketcher/CMakeLists.txt — SolidArc: standalone NURBS modelling tool — kernel, interaction, presentation (software + Vulkan), console host
#   Builds with no engine dependency and no external packages. Linux: cmake -B build -G Ninja && cmake --build build && ctest --test-dir build
#=============================================================================================================================================

cmake_minimum_required(VERSION 3.20)
project(SolidArc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wno-unused-function)
endif()

# ─── Kernel: pure geometry, zero dependencies ───────────────────────────────────────────────────────────────────────────────────────────────
add_library(SolidArcKernel STATIC
    Kernel/CurveSpecification.cpp
    Kernel/SurfaceSpecification.cpp
    Kernel/TopologySpecification.cpp
    Kernel/SkinSolver.cpp
    Kernel/IntersectionSolver.cpp
    Kernel/FairPatchSolver.cpp
    Kernel/ProfileSolver.cpp
    Kernel/MirrorSolver.cpp
)
target_include_directories(SolidArcKernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ─── Presentation + Interaction: RasterExchange rule, CPU SoftwareRaster (Slang shaders mirrored in C++), camera ─────────────────────────
add_library(SolidArcPresentation STATIC
    Presentation/SoftwareRaster.cpp
    Presentation/ScenePresentation.cpp
    Interaction/CameraProjection.cpp
    Interaction/SnapResolution.cpp
    Interaction/InputEvent.cpp
    Interaction/HotkeyChart.cpp
    Interaction/ToolSession.cpp
    Interaction/TransformGizmo.cpp
)
target_link_libraries(SolidArcPresentation PUBLIC SolidArcKernel)
target_include_directories(SolidArcPresentation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Presentation)
target_compile_definitions(SolidArcPresentation PUBLIC SOLIDARC_PROOF_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/Proofs")

# ─── Document + Console: figure store, command codec, console host, SolidArc executable ───────────────────────────────────────────────────────
add_library(SolidArcConsoleKit STATIC
    Document/SceneDocument.cpp
    Document/FigureRecipe.cpp
    Document/UndoSequence.cpp
    Console/CommandCodec.cpp
    Console/ConsoleHost.cpp
    Console/ConsoleInteraction.cpp
    Console/ConsoleSelection.cpp
)
target_link_libraries(SolidArcConsoleKit PUBLIC SolidArcPresentation)

add_executable(SolidArc Console/SolidArcConsole.cpp)
target_link_libraries(SolidArc PRIVATE SolidArcConsoleKit)

# ─── Verification: console proofs, one executable per phase, registered with ctest ──────────────────────────────────────────────────────────
enable_testing()

add_executable(KernelVerification Verification/KernelVerification.cpp)
target_link_libraries(KernelVerification PRIVATE SolidArcKernel)
add_test(NAME KernelVerification COMMAND KernelVerification)

add_executable(RasterVerification Verification/RasterVerification.cpp)
target_link_libraries(RasterVerification PRIVATE SolidArcPresentation)
add_test(NAME RasterVerification COMMAND RasterVerification)

add_test(NAME Phase2_Primitives COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase2_Primitives.arc)

add_executable(InteractionVerification Verification/InteractionVerification.cpp)
target_link_libraries(InteractionVerification PRIVATE SolidArcConsoleKit)
add_test(NAME InteractionVerification COMMAND InteractionVerification)
add_executable(SelectionVerification Verification/SelectionVerification.cpp)
target_link_libraries(SelectionVerification PRIVATE SolidArcConsoleKit)
add_test(NAME SelectionVerification COMMAND SelectionVerification)
add_executable(TopologyVerification Verification/TopologyVerification.cpp)
target_link_libraries(TopologyVerification PRIVATE SolidArcConsoleKit)
add_test(NAME TopologyVerification COMMAND TopologyVerification)
add_executable(ProfileVerification Verification/ProfileVerification.cpp)
target_link_libraries(ProfileVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ProfileVerification COMMAND ProfileVerification)
add_test(NAME Phase3_Sketching COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase3_Sketching.arc)
add_test(NAME Phase4_Selection COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase4_Selection.arc)
add_test(NAME Phase3b_GizmoShading COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase3b_GizmoShading.arc)

add_executable(SkinVerification Verification/SkinVerification.cpp)
target_link_libraries(SkinVerification PRIVATE SolidArcConsoleKit)
add_test(NAME SkinVerification COMMAND SkinVerification)

add_executable(IntersectionVerification Verification/IntersectionVerification.cpp)
target_link_libraries(IntersectionVerification PRIVATE SolidArcConsoleKit)
add_test(NAME IntersectionVerification COMMAND IntersectionVerification)
add_executable(FairPatchVerification Verification/FairPatchVerification.cpp)
target_link_libraries(FairPatchVerification PRIVATE SolidArcConsoleKit)
add_test(NAME FairPatchVerification COMMAND FairPatchVerification)

# Phase 11a: Solidify (single-surface shell), Loft with guides, and the body operations added next.
add_executable(BodyOpsVerification Verification/BodyOpsVerification.cpp)
target_link_libraries(BodyOpsVerification PRIVATE SolidArcConsoleKit)
add_test(NAME BodyOpsVerification COMMAND BodyOpsVerification)
add_executable(ArrayAndBridgeVerification Verification/ArrayAndBridgeVerification.cpp)
target_link_libraries(ArrayAndBridgeVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ArrayAndBridgeVerification COMMAND ArrayAndBridgeVerification)
add_executable(DimensionVerification Verification/DimensionVerification.cpp)
target_link_libraries(DimensionVerification PRIVATE SolidArcConsoleKit)
add_test(NAME DimensionVerification COMMAND DimensionVerification)

# Phase 17: sub-entity dimensions (face / edge), leader lines, `dim sub` (auto-emit all sub-entity dims).
add_executable(SubEntityDimensionVerification Verification/SubEntityDimensionVerification.cpp)
target_link_libraries(SubEntityDimensionVerification PRIVATE SolidArcConsoleKit)
add_test(NAME SubEntityDimensionVerification COMMAND SubEntityDimensionVerification)

# Phase 18: 2D constraint solver — 9 constraint types, Newton + analytic Jacobian, dof analysis, dim-edit re-solve hook.
add_executable(ConstraintVerification Verification/ConstraintVerification.cpp)
target_link_libraries(ConstraintVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConstraintVerification COMMAND ConstraintVerification)

# Phase 19: mirror / radial / empty operations — 3D reflection (plane / axis / multi-axis) + radial rotation around a 3D line.
add_executable(MirrorVerification Verification/MirrorVerification.cpp)
target_link_libraries(MirrorVerification PRIVATE SolidArcConsoleKit)
add_test(NAME MirrorVerification COMMAND MirrorVerification)

# Phase 10: chain-run every per-phase script and check the contact sheet.
add_executable(SuiteVerification Verification/SuiteVerification.cpp)
target_link_libraries(SuiteVerification PRIVATE SolidArcConsoleKit)
target_compile_definitions(SuiteVerification PRIVATE SUITEVERIFICATION_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME SuiteVerification COMMAND SuiteVerification)

# Per-phase script smoke tests (the suite is the regression net; these are fast individual reruns so a broken phase
#    fails locally before the whole suite does).
add_test(NAME Phase6_Topology COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase6_Topology.arc)
add_test(NAME Phase7_Profiles COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase7_Profiles.arc)
add_test(NAME Phase7b_Areas COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase7b_Areas.arc)
add_test(NAME Phase8_Skins COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase8_Skins.arc)
add_test(NAME Phase9_Booleans COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase9_Booleans.arc)
add_test(NAME Phase9b_FairPatch COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase9b_FairPatch.arc)
add_test(NAME Phase10_Suite COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase10_Suite.arc)
add_test(NAME Phase10_ContactSheet COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase10_ContactSheet.arc)
