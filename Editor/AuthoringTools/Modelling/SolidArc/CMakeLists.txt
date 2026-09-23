#=============================================================================================================================================
# Editor/AuthoringTools/Modelling/SolidArc/CMakeLists.txt — SolidArc standalone NURBS / B-rep modelling tool.
#   Builds with no engine dependency and no external packages. The direct-modelling gates cover face loft,
#   fixed-topology tweaks, planar chamfers, bounded curved-support chamfers, face transforms, and the Phase 36a face-edit batch.
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
    Kernel/ConstraintSolver.cpp
    Kernel/ConstraintGraph.cpp
    Kernel/MirrorSolver.cpp
    Kernel/BlendSolver.cpp
    Kernel/TweakSolver.cpp
    Kernel/FaceEditSolver.cpp
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
set(SOLIDARC_PROOF_FOLDER "${CMAKE_CURRENT_BINARY_DIR}/Proofs")
file(MAKE_DIRECTORY "${SOLIDARC_PROOF_FOLDER}")
target_compile_definitions(SolidArcPresentation PUBLIC SOLIDARC_PROOF_FOLDER="${SOLIDARC_PROOF_FOLDER}")

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

# Phase 23: adversarial 2D contacts, self-crossing NURBS, and free-form Boolean/offset quality.
add_executable(ProfileAdversarialVerification Verification/ProfileAdversarialVerification.cpp)
target_link_libraries(ProfileAdversarialVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ProfileAdversarialVerification COMMAND ProfileAdversarialVerification)
add_test(NAME Phase3_Sketching COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase3_Sketching.arc)
add_test(NAME Phase4_Selection COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase4_Selection.arc)
add_test(NAME Phase3b_GizmoShading COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase3b_GizmoShading.arc)

add_executable(SkinVerification Verification/SkinVerification.cpp)
target_link_libraries(SkinVerification PRIVATE SolidArcConsoleKit)
add_test(NAME SkinVerification COMMAND SkinVerification)

add_executable(IntersectionVerification Verification/IntersectionVerification.cpp)
target_link_libraries(IntersectionVerification PRIVATE SolidArcConsoleKit)
add_test(NAME IntersectionVerification COMMAND IntersectionVerification)

# Phase 24/24b/24c/24d: exact B-rep Boolean contact, identity, parameter and periodic-seam equivalence.
add_executable(BooleanContactVerification Verification/BooleanContactVerification.cpp)
target_link_libraries(BooleanContactVerification PRIVATE SolidArcConsoleKit)
add_test(NAME BooleanContactVerification COMMAND BooleanContactVerification)
add_executable(BooleanIdentityVerification Verification/BooleanIdentityVerification.cpp)
target_link_libraries(BooleanIdentityVerification PRIVATE SolidArcConsoleKit)
add_test(NAME BooleanIdentityVerification COMMAND BooleanIdentityVerification)
add_executable(BooleanEquivalenceVerification Verification/BooleanEquivalenceVerification.cpp)
target_link_libraries(BooleanEquivalenceVerification PRIVATE SolidArcConsoleKit)
add_test(NAME BooleanEquivalenceVerification COMMAND BooleanEquivalenceVerification)
add_executable(BooleanCylinderSeamVerification Verification/BooleanCylinderSeamVerification.cpp)
target_link_libraries(BooleanCylinderSeamVerification PRIVATE SolidArcConsoleKit)
add_test(NAME BooleanCylinderSeamVerification COMMAND BooleanCylinderSeamVerification)
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

# Blend operations: chamfer / fillet / face push, each checked against its closed-form volume.
add_executable(BlendVerification Verification/BlendVerification.cpp)
target_link_libraries(BlendVerification PRIVATE SolidArcConsoleKit)
add_test(NAME BlendVerification COMMAND BlendVerification)

# Phase 34c/34d: convex planar arbitrary-dihedral single edges and transactional connected edge loops.
add_executable(ChamferLoopVerification Verification/ChamferLoopVerification.cpp)
target_link_libraries(ChamferLoopVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ChamferLoopVerification COMMAND ChamferLoopVerification)

# Phase 34e: bounded planar face rotation and uniform scale tweaks on fixed topology.
add_executable(TransformTweakVerification Verification/TransformTweakVerification.cpp)
target_link_libraries(TransformTweakVerification PRIVATE SolidArcConsoleKit)
add_test(NAME TransformTweakVerification COMMAND TransformTweakVerification)

# Phase 35a: bounded exact native-cylinder curved-cap and circular-edge axial tweaks.
add_executable(CurvedTweakVerification Verification/CurvedTweakVerification.cpp)
target_link_libraries(CurvedTweakVerification PRIVATE SolidArcConsoleKit)
add_test(NAME CurvedTweakVerification COMMAND CurvedTweakVerification)

# Phase 35b: bounded concave/non-convex planar chamfer networks on simple extruded profiles.
add_executable(ConcaveChamferVerification Verification/ConcaveChamferVerification.cpp)
target_link_libraries(ConcaveChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConcaveChamferVerification COMMAND ConcaveChamferVerification)

# Phase 35c: exact native-cone circular-cap chamfer route.
add_executable(ConeChamferVerification Verification/ConeChamferVerification.cpp)
target_link_libraries(ConeChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConeChamferVerification COMMAND ConeChamferVerification)

# Phase 35d: connected same-body face lofts, bounded to the exact opposite-cap identity of a canonical prism.
add_executable(ConnectedFaceLoftVerification Verification/ConnectedFaceLoftVerification.cpp)
target_link_libraries(ConnectedFaceLoftVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConnectedFaceLoftVerification COMMAND ConnectedFaceLoftVerification)

# Phase 36a: conservative general face-edit batch — exact box offset/shell/draft/delete/replace/extend/trim and healing.
add_executable(FaceEditVerification Verification/FaceEditVerification.cpp)
target_link_libraries(FaceEditVerification PRIVATE SolidArcConsoleKit)
add_test(NAME FaceEditVerification COMMAND FaceEditVerification)

# Phase 36b: connected same-body cap identities and non-identity dissimilar-hull replacement lofts.
add_executable(GeneralConnectedFaceLoftVerification Verification/GeneralConnectedFaceLoftVerification.cpp)
target_link_libraries(GeneralConnectedFaceLoftVerification PRIVATE SolidArcConsoleKit)
add_test(NAME GeneralConnectedFaceLoftVerification COMMAND GeneralConnectedFaceLoftVerification)

# Phase 36c: bounded mixed plane/cylinder curved-root chamfer.
add_executable(GeneralCurvedChamferVerification Verification/GeneralCurvedChamferVerification.cpp)
target_link_libraries(GeneralCurvedChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME GeneralCurvedChamferVerification COMMAND GeneralCurvedChamferVerification)

# Phase 36f: bounded complete circular plane–cone boss-root chamfer; arbitrary mixed/freeform curved edges remain refused.
add_executable(PlaneConeChamferVerification Verification/PlaneConeChamferVerification.cpp)
target_link_libraries(PlaneConeChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PlaneConeChamferVerification COMMAND PlaneConeChamferVerification)

# Phase 36g: bounded complete coaxial cylinder–cone boss-root chamfer; general/non-coaxial pairs remain refused.
add_executable(CylinderConeChamferVerification Verification/CylinderConeChamferVerification.cpp)
target_link_libraries(CylinderConeChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME CylinderConeChamferVerification COMMAND CylinderConeChamferVerification)

# Phase 36h: bounded half-turn partial curved-root chamfer; general-angle sectors and arbitrary partial loops remain refused.
add_executable(PartialCurvedChamferVerification Verification/PartialCurvedChamferVerification.cpp)
target_link_libraries(PartialCurvedChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialCurvedChamferVerification COMMAND PartialCurvedChamferVerification)

# Phase 36i: bounded general-angle partial curved-root chamfer with radial endpoint-cap healing.
add_executable(SectorCurvedChamferVerification Verification/SectorCurvedChamferVerification.cpp)
target_link_libraries(SectorCurvedChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME SectorCurvedChamferVerification COMMAND SectorCurvedChamferVerification)

# Phase 36j: bounded general-angle partial plane-cone chamfer with radial endpoint-cap healing.
add_executable(PartialPlaneConeChamferVerification Verification/PartialPlaneConeChamferVerification.cpp)
target_link_libraries(PartialPlaneConeChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialPlaneConeChamferVerification COMMAND PartialPlaneConeChamferVerification)

# Phase 36k: bounded partial coaxial cone–cylinder root chamfer with radial endpoint-cap healing.
add_executable(PartialConeCylinderChamferVerification Verification/PartialConeCylinderChamferVerification.cpp)
target_link_libraries(PartialConeCylinderChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialConeCylinderChamferVerification COMMAND PartialConeCylinderChamferVerification)

# Phase 36l: bounded partial coaxial cone–cone root chamfer with radial endpoint-cap healing.
add_executable(PartialConeConeChamferVerification Verification/PartialConeConeChamferVerification.cpp)
target_link_libraries(PartialConeConeChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialConeConeChamferVerification COMMAND PartialConeConeChamferVerification)

# Phase 36m: bounded complete coaxial apex plane–cone root chamfer.
add_executable(ApexPlaneConeChamferVerification Verification/ApexPlaneConeChamferVerification.cpp)
target_link_libraries(ApexPlaneConeChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ApexPlaneConeChamferVerification COMMAND ApexPlaneConeChamferVerification)

# Phase 36n: bounded partial coaxial apex plane–cone root chamfer.
add_executable(PartialApexPlaneConeChamferVerification Verification/PartialApexPlaneConeChamferVerification.cpp)
target_link_libraries(PartialApexPlaneConeChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialApexPlaneConeChamferVerification COMMAND PartialApexPlaneConeChamferVerification)

# Phase 36o: bounded partial coaxial plane–cone root fillet.
add_executable(PartialPlaneConeFilletVerification Verification/PartialPlaneConeFilletVerification.cpp)
target_link_libraries(PartialPlaneConeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialPlaneConeFilletVerification COMMAND PartialPlaneConeFilletVerification)

# Phase 36p: bounded complete coaxial cylinder–cone root fillet.
add_executable(CylinderConeFilletVerification Verification/CylinderConeFilletVerification.cpp)
target_link_libraries(CylinderConeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME CylinderConeFilletVerification COMMAND CylinderConeFilletVerification)

# Phase 36q: bounded complete coaxial cone–cone root fillet.
add_executable(ConeConeFilletVerification Verification/ConeConeFilletVerification.cpp)
target_link_libraries(ConeConeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConeConeFilletVerification COMMAND ConeConeFilletVerification)

# Phase 36r: bounded partial coaxial cone–cone root fillet.
add_executable(PartialConeConeFilletVerification Verification/PartialConeConeFilletVerification.cpp)
target_link_libraries(PartialConeConeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialConeConeFilletVerification COMMAND PartialConeConeFilletVerification)

# Phase 36s: bounded partial coaxial cone–cylinder root fillet.
add_executable(PartialConeCylinderFilletVerification Verification/PartialConeCylinderFilletVerification.cpp)
target_link_libraries(PartialConeCylinderFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialConeCylinderFilletVerification COMMAND PartialConeCylinderFilletVerification)

# Phase 36d: bounded convex non-planar closed edge-loop chamfers.
add_executable(ArbitraryNonPlanarEdgeLoopVerification Verification/ArbitraryNonPlanarEdgeLoopVerification.cpp)
target_link_libraries(ArbitraryNonPlanarEdgeLoopVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ArbitraryNonPlanarEdgeLoopVerification COMMAND ArbitraryNonPlanarEdgeLoopVerification)

# Phase 36e: bounded Phase 33 linear variable-radius ruled-solid foundation.
add_executable(Phase33VariableRadiusVerification Verification/Phase33VariableRadiusVerification.cpp)
target_link_libraries(Phase33VariableRadiusVerification PRIVATE SolidArcConsoleKit)
add_test(NAME Phase33VariableRadiusVerification COMMAND Phase33VariableRadiusVerification)

# Stage 1: bounded rolling-ball variable-radius fillet on one straight planar corner.
add_executable(VariableRadiusCornerFilletVerification Verification/VariableRadiusCornerFilletVerification.cpp)
target_link_libraries(VariableRadiusCornerFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME VariableRadiusCornerFilletVerification COMMAND VariableRadiusCornerFilletVerification)

# Stage 2: the same exact rolling section with an independent linear support-setback law.
add_executable(VariableSetbackCornerFilletVerification Verification/VariableSetbackCornerFilletVerification.cpp)
target_link_libraries(VariableSetbackCornerFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME VariableSetbackCornerFilletVerification COMMAND VariableSetbackCornerFilletVerification)

# Stage 3a: quadratic nonlinear radius law with explicit derivative/curvature acceptance.
add_executable(NonlinearVariableRadiusCornerVerification Verification/NonlinearVariableRadiusCornerVerification.cpp)
target_link_libraries(NonlinearVariableRadiusCornerVerification PRIVATE SolidArcConsoleKit)
add_test(NAME NonlinearVariableRadiusCornerVerification COMMAND NonlinearVariableRadiusCornerVerification)

# Stage 3b: quintic G2 planar corner transition; this is a non-rolling profile family.
add_executable(G2PlanarCornerVerification Verification/G2PlanarCornerVerification.cpp)
target_link_libraries(G2PlanarCornerVerification PRIVATE SolidArcConsoleKit)
add_test(NAME G2PlanarCornerVerification COMMAND G2PlanarCornerVerification)

# Stage 3c: genuinely nonlinear support-setback law with quadratic extent/volume acceptance.
add_executable(NonlinearVariableSetbackCornerVerification Verification/NonlinearVariableSetbackCornerVerification.cpp)
target_link_libraries(NonlinearVariableSetbackCornerVerification PRIVATE SolidArcConsoleKit)
add_test(NAME NonlinearVariableSetbackCornerVerification COMMAND NonlinearVariableSetbackCornerVerification)

# Phase 37f: independent positive linear setbacks on the two perpendicular supports.
add_executable(UnequalSetbackCornerVerification Verification/UnequalSetbackCornerVerification.cpp)
target_link_libraries(UnequalSetbackCornerVerification PRIVATE SolidArcConsoleKit)
add_test(NAME UnequalSetbackCornerVerification COMMAND UnequalSetbackCornerVerification)

# Stage 3e: independent genuinely nonlinear quadratic setbacks on the two perpendicular supports.
add_executable(NonlinearUnequalSetbackCornerVerification Verification/NonlinearUnequalSetbackCornerVerification.cpp)
target_link_libraries(NonlinearUnequalSetbackCornerVerification PRIVATE SolidArcConsoleKit)
add_test(NAME NonlinearUnequalSetbackCornerVerification COMMAND NonlinearUnequalSetbackCornerVerification)

# Stage 4a: one explicit strict-interior constant-radius partial-edge blend with planar transition sectors.
add_executable(PartialEdgeFilletVerification Verification/PartialEdgeFilletVerification.cpp)
target_link_libraries(PartialEdgeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialEdgeFilletVerification COMMAND PartialEdgeFilletVerification)

# Stage 4b: one coaxial right-cone apex rounded by an exact tangent spherical cap.
add_executable(ConeApexFilletVerification Verification/ConeApexFilletVerification.cpp)
target_link_libraries(ConeApexFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConeApexFilletVerification COMMAND ConeApexFilletVerification)

# Stage 4c: one explicit strict-interior quadratic-radius partial-edge blend.
add_executable(QuadraticPartialEdgeFilletVerification Verification/QuadraticPartialEdgeFilletVerification.cpp)
target_link_libraries(QuadraticPartialEdgeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME QuadraticPartialEdgeFilletVerification COMMAND QuadraticPartialEdgeFilletVerification)

# Stage 4d: one strict partial coaxial cone-apex spherical cap with explicit meridian caps.
add_executable(PartialConeApexFilletVerification Verification/PartialConeApexFilletVerification.cpp)
target_link_libraries(PartialConeApexFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialConeApexFilletVerification COMMAND PartialConeApexFilletVerification)

# Stage 4e: one explicit constant-radius oblique planar corner fillet.
add_executable(ObliquePlanarCornerFilletVerification Verification/ObliquePlanarCornerFilletVerification.cpp)
target_link_libraries(ObliquePlanarCornerFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ObliquePlanarCornerFilletVerification COMMAND ObliquePlanarCornerFilletVerification)

# Stage 4f: one strict-interior constant-radius oblique partial-edge fillet.
add_executable(ObliquePartialEdgeFilletVerification Verification/ObliquePartialEdgeFilletVerification.cpp)
target_link_libraries(ObliquePartialEdgeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ObliquePartialEdgeFilletVerification COMMAND ObliquePartialEdgeFilletVerification)

# Stage 4g: one strict-interior genuinely nonlinear oblique partial-edge fillet.
add_executable(ObliqueQuadraticPartialEdgeFilletVerification Verification/ObliqueQuadraticPartialEdgeFilletVerification.cpp)
target_link_libraries(ObliqueQuadraticPartialEdgeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ObliqueQuadraticPartialEdgeFilletVerification COMMAND ObliqueQuadraticPartialEdgeFilletVerification)

# Stage 4h: one complete-edge genuinely nonlinear oblique quadratic fillet.
add_executable(ObliqueQuadraticEdgeFilletVerification Verification/ObliqueQuadraticEdgeFilletVerification.cpp)
target_link_libraries(ObliqueQuadraticEdgeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ObliqueQuadraticEdgeFilletVerification COMMAND ObliqueQuadraticEdgeFilletVerification)

# Stage 4i: exact rolling-ball core with curvature-matched planar support transitions.
add_executable(G2RollingBallVerification Verification/G2RollingBallVerification.cpp)
target_link_libraries(G2RollingBallVerification PRIVATE SolidArcConsoleKit)
add_test(NAME G2RollingBallVerification COMMAND G2RollingBallVerification)

# Stage 4j: exact oblique rolling-ball core with curvature-matched planar support transitions.
add_executable(ObliqueG2RollingBallVerification Verification/ObliqueG2RollingBallVerification.cpp)
target_link_libraries(ObliqueG2RollingBallVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ObliqueG2RollingBallVerification COMMAND ObliqueG2RollingBallVerification)

# Stage 4k: genuinely nonlinear variable-radius rolling-ball core with G2 planar transitions.
add_executable(VariableG2RollingBallVerification Verification/VariableG2RollingBallVerification.cpp)
target_link_libraries(VariableG2RollingBallVerification PRIVATE SolidArcConsoleKit)
add_test(NAME VariableG2RollingBallVerification COMMAND VariableG2RollingBallVerification)

# Stage 4l: genuinely nonlinear oblique variable-radius rolling-ball core with G2 transitions.
add_executable(ObliqueVariableG2RollingBallVerification Verification/ObliqueVariableG2RollingBallVerification.cpp)
target_link_libraries(ObliqueVariableG2RollingBallVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ObliqueVariableG2RollingBallVerification COMMAND ObliqueVariableG2RollingBallVerification)

# Stage 4m: bounded dispatch from one eligible rectangular B-rep edge to the verified G2 reconstruction route.
add_executable(EligibleG2EdgeDispatchVerification Verification/EligibleG2EdgeDispatchVerification.cpp)
target_link_libraries(EligibleG2EdgeDispatchVerification PRIVATE SolidArcConsoleKit)
add_test(NAME EligibleG2EdgeDispatchVerification COMMAND EligibleG2EdgeDispatchVerification)

# Stage 4n: bounded dispatch from one eligible non-orthogonal rectangular B-rep edge to the oblique G2 route.
add_executable(EligibleObliqueG2EdgeDispatchVerification Verification/EligibleObliqueG2EdgeDispatchVerification.cpp)
target_link_libraries(EligibleObliqueG2EdgeDispatchVerification PRIVATE SolidArcConsoleKit)
add_test(NAME EligibleObliqueG2EdgeDispatchVerification COMMAND EligibleObliqueG2EdgeDispatchVerification)

# Stage 4o: bounded dispatch from one eligible orthogonal rectangular B-rep edge to the variable-radius G2 route.
add_executable(EligibleVariableG2EdgeDispatchVerification Verification/EligibleVariableG2EdgeDispatchVerification.cpp)
target_link_libraries(EligibleVariableG2EdgeDispatchVerification PRIVATE SolidArcConsoleKit)
add_test(NAME EligibleVariableG2EdgeDispatchVerification COMMAND EligibleVariableG2EdgeDispatchVerification)

# Stage 4p: bounded dispatch from one eligible non-orthogonal rectangular B-rep edge to the oblique variable G2 route.
add_executable(EligibleObliqueVariableG2EdgeDispatchVerification Verification/EligibleObliqueVariableG2EdgeDispatchVerification.cpp)
target_link_libraries(EligibleObliqueVariableG2EdgeDispatchVerification PRIVATE SolidArcConsoleKit)
add_test(NAME EligibleObliqueVariableG2EdgeDispatchVerification COMMAND EligibleObliqueVariableG2EdgeDispatchVerification)

# Stage 4q: bounded dispatch from the unique apex vertex of a canonical native cone to the spherical-cap route.
add_executable(ConeApexVertexDispatchVerification Verification/ConeApexVertexDispatchVerification.cpp)
target_link_libraries(ConeApexVertexDispatchVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConeApexVertexDispatchVerification COMMAND ConeApexVertexDispatchVerification)

# Stage 4r: bounded dispatch from the unique apex vertex of a canonical half-turn partial cone.
add_executable(PartialConeApexVertexDispatchVerification Verification/PartialConeApexVertexDispatchVerification.cpp)
target_link_libraries(PartialConeApexVertexDispatchVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialConeApexVertexDispatchVerification COMMAND PartialConeApexVertexDispatchVerification)

# Stage 4s: bounded dispatch from the unique apex vertex of a canonical non-reflex partial-cone revolve.
add_executable(GeneralPartialConeApexVertexDispatchVerification Verification/GeneralPartialConeApexVertexDispatchVerification.cpp)
target_link_libraries(GeneralPartialConeApexVertexDispatchVerification PRIVATE SolidArcConsoleKit)
add_test(NAME GeneralPartialConeApexVertexDispatchVerification COMMAND GeneralPartialConeApexVertexDispatchVerification)

# Stage 4t: bounded dispatch from the unique apex vertex of a canonical reflex partial-cone revolve.
add_executable(ReflexPartialConeApexVertexDispatchVerification Verification/ReflexPartialConeApexVertexDispatchVerification.cpp)
target_link_libraries(ReflexPartialConeApexVertexDispatchVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ReflexPartialConeApexVertexDispatchVerification COMMAND ReflexPartialConeApexVertexDispatchVerification)

# Stage 4u: bounded dispatch from the unique apex vertex of a canonical native cone to an exact planar cap.
add_executable(ConeApexChamferVerification Verification/ConeApexChamferVerification.cpp)
target_link_libraries(ConeApexChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConeApexChamferVerification COMMAND ConeApexChamferVerification)

# Stage 4v: bounded dispatch from the unique apex vertex of a canonical native partial cone to an exact planar cap.
add_executable(PartialConeApexVertexChamferVerification Verification/PartialConeApexVertexChamferVerification.cpp)
target_link_libraries(PartialConeApexVertexChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialConeApexVertexChamferVerification COMMAND PartialConeApexVertexChamferVerification)

# Phase 38w: preserve one exact unequal-radius coaxial bicone apex combination with a conical chamfer bridge.
add_executable(UnequalConeApexChamferVerification Verification/UnequalConeApexChamferVerification.cpp)
target_link_libraries(UnequalConeApexChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME UnequalConeApexChamferVerification COMMAND UnequalConeApexChamferVerification)

# Phase 38x: preserve unequal support radii while allowing one independent axial set-back per bicone side.
add_executable(UnequalSetbackBiconeApexChamferVerification Verification/UnequalSetbackBiconeApexChamferVerification.cpp)
target_link_libraries(UnequalSetbackBiconeApexChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME UnequalSetbackBiconeApexChamferVerification COMMAND UnequalSetbackBiconeApexChamferVerification)

# Phase 38y: preserve unequal coaxial cone supports while replacing the shared apex with a tangent toroidal fillet.
add_executable(UnequalConeApexFilletVerification Verification/UnequalConeApexFilletVerification.cpp)
target_link_libraries(UnequalConeApexFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME UnequalConeApexFilletVerification COMMAND UnequalConeApexFilletVerification)

# Phase 38z: preserve equal coaxial cone radii while replacing their shared apex with a tangent toroidal fillet.
add_executable(EqualRadiusBiconeApexFilletVerification Verification/EqualRadiusBiconeApexFilletVerification.cpp)
target_link_libraries(EqualRadiusBiconeApexFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME EqualRadiusBiconeApexFilletVerification COMMAND EqualRadiusBiconeApexFilletVerification)

# Phase 39a: preserve one strict non-reflex partial unequal-radius bicone sector with radial endpoint caps.
add_executable(PartialUnequalBiconeApexChamferVerification Verification/PartialUnequalBiconeApexChamferVerification.cpp)
target_link_libraries(PartialUnequalBiconeApexChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialUnequalBiconeApexChamferVerification COMMAND PartialUnequalBiconeApexChamferVerification)

# Phase 39b: retain the same bounded partial source contract while replacing the apex with a toroidal fillet.
add_executable(PartialUnequalBiconeApexFilletVerification Verification/PartialUnequalBiconeApexFilletVerification.cpp)
target_link_libraries(PartialUnequalBiconeApexFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PartialUnequalBiconeApexFilletVerification COMMAND PartialUnequalBiconeApexFilletVerification)

# Phase 25–30: exact cylinder cap blends and exact native cylinder/frustum face pushes.
add_executable(CylinderChamferVerification Verification/CylinderChamferVerification.cpp)
target_link_libraries(CylinderChamferVerification PRIVATE SolidArcConsoleKit)
add_test(NAME CylinderChamferVerification COMMAND CylinderChamferVerification)
add_executable(CylinderFilletVerification Verification/CylinderFilletVerification.cpp)
target_link_libraries(CylinderFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME CylinderFilletVerification COMMAND CylinderFilletVerification)
add_executable(CylinderPushVerification Verification/CylinderPushVerification.cpp)
target_link_libraries(CylinderPushVerification PRIVATE SolidArcConsoleKit)
add_test(NAME CylinderPushVerification COMMAND CylinderPushVerification)
add_executable(CylinderSidePushVerification Verification/CylinderSidePushVerification.cpp)
target_link_libraries(CylinderSidePushVerification PRIVATE SolidArcConsoleKit)
add_test(NAME CylinderSidePushVerification COMMAND CylinderSidePushVerification)
add_executable(ConeSidePushVerification Verification/ConeSidePushVerification.cpp)
target_link_libraries(ConeSidePushVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConeSidePushVerification COMMAND ConeSidePushVerification)
add_executable(ConeCapPushVerification Verification/ConeCapPushVerification.cpp)
target_link_libraries(ConeCapPushVerification PRIVATE SolidArcConsoleKit)
add_test(NAME ConeCapPushVerification COMMAND ConeCapPushVerification)

# Phase 31: first general smooth-support rolling-ball fillet — a planar annular shoulder meeting a cylindrical boss.
add_executable(PlaneCylinderFilletVerification Verification/PlaneCylinderFilletVerification.cpp)
target_link_libraries(PlaneCylinderFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PlaneCylinderFilletVerification COMMAND PlaneCylinderFilletVerification)

# Phase 32a: G1 tangent-chain propagation across angular representation seams at that exact boss root.
add_executable(TangentChainFilletVerification Verification/TangentChainFilletVerification.cpp)
target_link_libraries(TangentChainFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME TangentChainFilletVerification COMMAND TangentChainFilletVerification)

# Phase 32b: exact finite semicircular boss-root chain with two planar-cap endpoint meridians.
add_executable(OpenChainFilletVerification Verification/OpenChainFilletVerification.cpp)
target_link_libraries(OpenChainFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME OpenChainFilletVerification COMMAND OpenChainFilletVerification)

# Phase 32c: deterministic transactional multi-edge sets with chain-member deduplication and corner refusal.
add_executable(MultiEdgeFilletVerification Verification/MultiEdgeFilletVerification.cpp)
target_link_libraries(MultiEdgeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME MultiEdgeFilletVerification COMMAND MultiEdgeFilletVerification)

# Phase 32d: exact general-angle plane-cylinder chains terminated by two radial caps and one axis edge.
add_executable(SectorEndpointFilletVerification Verification/SectorEndpointFilletVerification.cpp)
target_link_libraries(SectorEndpointFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME SectorEndpointFilletVerification COMMAND SectorEndpointFilletVerification)

# Phase 32e: equal-radius orthogonal three-edge box corner with a rational spherical octant transition.
add_executable(CornerFilletVerification Verification/CornerFilletVerification.cpp)
target_link_libraries(CornerFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME CornerFilletVerification COMMAND CornerFilletVerification)

# Phase 32f: complete equal-radius rectangular edge network with twelve cylinders and eight spherical corners.
add_executable(RoundedBoxFilletVerification Verification/RoundedBoxFilletVerification.cpp)
target_link_libraries(RoundedBoxFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME RoundedBoxFilletVerification COMMAND RoundedBoxFilletVerification)

# Phase 32g: complete four-edge parallel family rebuilt as an exact rounded prism with cross-wall feasibility.
add_executable(RoundedPrismFilletVerification Verification/RoundedPrismFilletVerification.cpp)
target_link_libraries(RoundedPrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME RoundedPrismFilletVerification COMMAND RoundedPrismFilletVerification)

# Phase 32h: preserve one centred coaxial circular through-hole while rebuilding that outer parallel family.
add_executable(PerforatedPrismFilletVerification Verification/PerforatedPrismFilletVerification.cpp)
target_link_libraries(PerforatedPrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PerforatedPrismFilletVerification COMMAND PerforatedPrismFilletVerification)

# Phase 32i: extend that single through-hole route to safe offsets using the exact rounded-wall inward offset.
add_executable(OffsetBorePrismFilletVerification Verification/OffsetBorePrismFilletVerification.cpp)
target_link_libraries(OffsetBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME OffsetBorePrismFilletVerification COMMAND OffsetBorePrismFilletVerification)

# Phase 32j: preserve exactly two separated axis-parallel bores with pairwise ligament feasibility.
add_executable(TwinBorePrismFilletVerification Verification/TwinBorePrismFilletVerification.cpp)
target_link_libraries(TwinBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME TwinBorePrismFilletVerification COMMAND TwinBorePrismFilletVerification)

# Phase 32k: scale exact geometric rim pairing to a bounded set of at most eight separated bores.
add_executable(MultiBorePrismFilletVerification Verification/MultiBorePrismFilletVerification.cpp)
target_link_libraries(MultiBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME MultiBorePrismFilletVerification COMMAND MultiBorePrismFilletVerification)

# Phase 32l: preserve one axis-parallel blind cylindrical cavity entering either rounded-prism end.
add_executable(BlindBorePrismFilletVerification Verification/BlindBorePrismFilletVerification.cpp)
target_link_libraries(BlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME BlindBorePrismFilletVerification COMMAND BlindBorePrismFilletVerification)

# Phase 32m: preserve exactly two separated axis-parallel blind cavities entering the same or opposite prism ends.
add_executable(DualBlindBorePrismFilletVerification Verification/DualBlindBorePrismFilletVerification.cpp)
target_link_libraries(DualBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME DualBlindBorePrismFilletVerification COMMAND DualBlindBorePrismFilletVerification)

# Phase 32n: scale the same exact finite-cylinder route to a bounded set of at most eight blind cavities.
add_executable(MultiBlindBorePrismFilletVerification Verification/MultiBlindBorePrismFilletVerification.cpp)
target_link_libraries(MultiBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME MultiBlindBorePrismFilletVerification COMMAND MultiBlindBorePrismFilletVerification)

# Phase 32o: preserve one coaxial two-diameter stepped blind cavity with an exact annular shoulder.
add_executable(SteppedBlindBorePrismFilletVerification Verification/SteppedBlindBorePrismFilletVerification.cpp)
target_link_libraries(SteppedBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME SteppedBlindBorePrismFilletVerification COMMAND SteppedBlindBorePrismFilletVerification)

# Phase 32p: scale one coaxial stepped cavity to a bounded chain of at most eight decreasing diameters.
add_executable(MultiStageBlindBorePrismFilletVerification Verification/MultiStageBlindBorePrismFilletVerification.cpp)
target_link_libraries(MultiStageBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME MultiStageBlindBorePrismFilletVerification COMMAND MultiStageBlindBorePrismFilletVerification)

# Phase 32q: preserve exactly two separated coaxial two-stage blind cavities with finite-band clearance.
add_executable(DualSteppedBlindBorePrismFilletVerification Verification/DualSteppedBlindBorePrismFilletVerification.cpp)
target_link_libraries(DualSteppedBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME DualSteppedBlindBorePrismFilletVerification COMMAND DualSteppedBlindBorePrismFilletVerification)

# Phase 32r: preserve one orthogonal blind cavity entering any retained planar rounded-prism side.
add_executable(SideBlindBorePrismFilletVerification Verification/SideBlindBorePrismFilletVerification.cpp)
target_link_libraries(SideBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME SideBlindBorePrismFilletVerification COMMAND SideBlindBorePrismFilletVerification)

# Phase 32s: preserve exactly two separated parallel blind cavities entering retained planar sides.
add_executable(DualSideBlindBorePrismFilletVerification Verification/DualSideBlindBorePrismFilletVerification.cpp)
target_link_libraries(DualSideBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME DualSideBlindBorePrismFilletVerification COMMAND DualSideBlindBorePrismFilletVerification)

# Phase 32t: preserve three through eight separated parallel blind cavities entering retained planar sides.
add_executable(MultiSideBlindBorePrismFilletVerification Verification/MultiSideBlindBorePrismFilletVerification.cpp)
target_link_libraries(MultiSideBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME MultiSideBlindBorePrismFilletVerification COMMAND MultiSideBlindBorePrismFilletVerification)

# Phase 32u: preserve one orthogonal two-diameter stepped blind cavity entering a retained planar side.
add_executable(SideSteppedBlindBorePrismFilletVerification Verification/SideSteppedBlindBorePrismFilletVerification.cpp)
target_link_libraries(SideSteppedBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME SideSteppedBlindBorePrismFilletVerification COMMAND SideSteppedBlindBorePrismFilletVerification)

# Phase 32v: preserve one orthogonal side-entering stepped cavity with three through eight decreasing diameters.
add_executable(MultiStageSideBlindBorePrismFilletVerification Verification/MultiStageSideBlindBorePrismFilletVerification.cpp)
target_link_libraries(MultiStageSideBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME MultiStageSideBlindBorePrismFilletVerification COMMAND MultiStageSideBlindBorePrismFilletVerification)

# Phase 32w: preserve exactly two separated two-stage cavities entering parallel retained sides.
add_executable(DualSideSteppedBlindBorePrismFilletVerification Verification/DualSideSteppedBlindBorePrismFilletVerification.cpp)
target_link_libraries(DualSideSteppedBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME DualSideSteppedBlindBorePrismFilletVerification COMMAND DualSideSteppedBlindBorePrismFilletVerification)

# Phase 32x: scale separated parallel side-entering two-stage cavities to a bounded set of three through eight.
add_executable(MultiSideSteppedBlindBorePrismFilletVerification Verification/MultiSideSteppedBlindBorePrismFilletVerification.cpp)
target_link_libraries(MultiSideSteppedBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME MultiSideSteppedBlindBorePrismFilletVerification COMMAND MultiSideSteppedBlindBorePrismFilletVerification)

# Phase 32y: generalize bounded side-stepped sets to mixed two-through-eight-stage chains with sixteen total stages.
add_executable(MixedStageSideBlindBorePrismFilletVerification Verification/MixedStageSideBlindBorePrismFilletVerification.cpp)
target_link_libraries(MixedStageSideBlindBorePrismFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME MixedStageSideBlindBorePrismFilletVerification COMMAND MixedStageSideBlindBorePrismFilletVerification)

# Phase 32z: first unequal-radius support pair — the exact plane–cone boss-root rolling ball and its prerequisites.
add_executable(PlaneConeFilletVerification Verification/PlaneConeFilletVerification.cpp)
target_link_libraries(PlaneConeFilletVerification PRIVATE SolidArcConsoleKit)
add_test(NAME PlaneConeFilletVerification COMMAND PlaneConeFilletVerification)

# Phase 34a: face loft — one solid from two through a chosen face of each (rims shared exactly, no Boolean).
add_executable(FaceLoftVerification Verification/FaceLoftVerification.cpp)
target_link_libraries(FaceLoftVerification PRIVATE SolidArcConsoleKit)
add_test(NAME FaceLoftVerification COMMAND FaceLoftVerification)

# Phase 34b: tweak — translate a face, an edge or a vertex of a solid on fixed topology (planar stays planar; --warp admits bilinear faces).
add_executable(TweakVerification Verification/TweakVerification.cpp)
target_link_libraries(TweakVerification PRIVATE SolidArcConsoleKit)
add_test(NAME TweakVerification COMMAND TweakVerification)

# Direct-modelling regression: the user-facing chamfer accepts one edge and refuses an edge loop transactionally.
add_executable(DirectModelingVerification Verification/DirectModelingVerification.cpp)
target_link_libraries(DirectModelingVerification PRIVATE SolidArcConsoleKit)
add_test(NAME DirectModelingVerification COMMAND DirectModelingVerification)

# Phase 22: versioned native .arc construction journals — atomic save/open and transactional failed opens.
add_executable(DocumentVerification Verification/DocumentVerification.cpp)
target_link_libraries(DocumentVerification PRIVATE SolidArcConsoleKit)
add_test(NAME DocumentVerification COMMAND DocumentVerification)

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
add_test(NAME Phase23_AdversarialProfiles COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase23_AdversarialProfiles.arc)
