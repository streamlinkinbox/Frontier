#=====================================================================================================================================
#                                                            CMAKELISTS.TXT
#=====================================================================================================================================
# 🧩 GeometryProcessing exhibit — validation executables and the inspection host over Engine/GeometryProcessing

# 📝 Standalone project: cmake -S Exhibits/GeometryProcessing/Code -B <build>. The library itself is not copied; it is added
#    from Engine/GeometryProcessing (exhibits reuse production code).

cmake_minimum_required(VERSION 3.20)
project(GeometryProcessingExhibit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../Engine/GeometryProcessing" GeometryProcessing)

#-------------------------------------------------------------------------------------------------------------------
#                                                    EXECUTABLES
#-------------------------------------------------------------------------------------------------------------------

add_executable(GeometryInspection GeometryInspection.cpp)
target_link_libraries(GeometryInspection PRIVATE FrontierGeometryProcessing)

add_executable(TopologyValidation TopologyValidation.cpp)
target_link_libraries(TopologyValidation PRIVATE FrontierGeometryProcessing)

add_executable(PredicateValidation PredicateValidation.cpp)
target_link_libraries(PredicateValidation PRIVATE FrontierGeometryProcessing)

add_executable(IntersectionValidation IntersectionValidation.cpp)
target_link_libraries(IntersectionValidation PRIVATE FrontierGeometryProcessing)

add_executable(ContainmentValidation ContainmentValidation.cpp)
target_link_libraries(ContainmentValidation PRIVATE FrontierGeometryProcessing)

add_executable(BooleanValidation BooleanValidation.cpp)
target_link_libraries(BooleanValidation PRIVATE FrontierGeometryProcessing)

add_executable(AlgorithmStressValidation AlgorithmStressValidation.cpp)
target_link_libraries(AlgorithmStressValidation PRIVATE FrontierGeometryProcessing)

add_executable(TessellationValidation TessellationValidation.cpp)
target_link_libraries(TessellationValidation PRIVATE FrontierGeometryProcessing)

add_executable(BlendValidation BlendValidation.cpp)
target_link_libraries(BlendValidation PRIVATE FrontierGeometryProcessing)

add_executable(FaceOperationValidation FaceOperationValidation.cpp)
target_link_libraries(FaceOperationValidation PRIVATE FrontierGeometryProcessing)
add_executable(FeatureValidation FeatureValidation.cpp)
target_link_libraries(FeatureValidation PRIVATE FrontierGeometryProcessing)

add_executable(SketchValidation SketchValidation.cpp)
target_link_libraries(SketchValidation PRIVATE FrontierGeometryProcessing)
add_executable(SketchTrimValidation SketchTrimValidation.cpp)
target_link_libraries(SketchTrimValidation PRIVATE FrontierGeometryProcessing)
add_executable(SketchEditingValidation SketchEditingValidation.cpp)
target_link_libraries(SketchEditingValidation PRIVATE FrontierGeometryProcessing)
add_executable(SketchConstrainingValidation SketchConstrainingValidation.cpp)
target_link_libraries(SketchConstrainingValidation PRIVATE FrontierGeometryProcessing)
add_executable(BodyEditingValidation BodyEditingValidation.cpp)
target_link_libraries(BodyEditingValidation PRIVATE FrontierGeometryProcessing)
add_executable(SplineValidation SplineValidation.cpp)
target_link_libraries(SplineValidation PRIVATE FrontierGeometryProcessing)

add_executable(CorpusValidation CorpusValidation.cpp)
target_link_libraries(CorpusValidation PRIVATE FrontierGeometryProcessing)

add_executable(PatternStressValidation PatternStressValidation.cpp)
target_link_libraries(PatternStressValidation PRIVATE FrontierGeometryProcessing)

add_executable(RollingBallValidation RollingBallValidation.cpp)
target_link_libraries(RollingBallValidation PRIVATE FrontierGeometryProcessing)
add_executable(SurfacingValidation SurfacingValidation.cpp)
target_link_libraries(SurfacingValidation PRIVATE FrontierGeometryProcessing)
add_executable(PanelSurfacingValidation PanelSurfacingValidation.cpp)
target_link_libraries(PanelSurfacingValidation PRIVATE FrontierGeometryProcessing)
add_executable(NurbsValidation NurbsValidation.cpp)
target_link_libraries(NurbsValidation PRIVATE FrontierGeometryProcessing)
add_executable(SetbackValidation SetbackValidation.cpp)
target_link_libraries(SetbackValidation PRIVATE FrontierGeometryProcessing)
add_executable(BodyCutValidation BodyCutValidation.cpp)
target_link_libraries(BodyCutValidation PRIVATE FrontierGeometryProcessing)
add_executable(SweepValidation SweepValidation.cpp)
target_link_libraries(SweepValidation PRIVATE FrontierGeometryProcessing)
add_executable(ReplayValidation ReplayValidation.cpp)
target_link_libraries(ReplayValidation PRIVATE FrontierGeometryProcessing)
add_executable(MeasureValidation MeasureValidation.cpp)
target_link_libraries(MeasureValidation PRIVATE FrontierGeometryProcessing)
add_executable(AssemblyValidation AssemblyValidation.cpp)
target_link_libraries(AssemblyValidation PRIVATE FrontierGeometryProcessing)
add_executable(DrawingValidation DrawingValidation.cpp)
target_link_libraries(DrawingValidation PRIVATE FrontierGeometryProcessing)
add_executable(ExchangeValidation ExchangeValidation.cpp)
target_link_libraries(ExchangeValidation PRIVATE FrontierGeometryProcessing)
target_compile_definitions(ExchangeValidation PRIVATE FRONTIER_STEP_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/StepFixtures")
add_executable(FreeformBlendValidation FreeformBlendValidation.cpp)
target_link_libraries(FreeformBlendValidation PRIVATE FrontierGeometryProcessing)

enable_testing()
add_test(NAME TopologyValidation  COMMAND TopologyValidation)
add_test(NAME PredicateValidation COMMAND PredicateValidation)
add_test(NAME IntersectionValidation COMMAND IntersectionValidation)
add_test(NAME ContainmentValidation COMMAND ContainmentValidation)
add_test(NAME BooleanValidation COMMAND BooleanValidation)
add_test(NAME AlgorithmStressValidation COMMAND AlgorithmStressValidation)
add_test(NAME TessellationValidation COMMAND TessellationValidation)
add_test(NAME BlendValidation COMMAND BlendValidation)
add_test(NAME FaceOperationValidation COMMAND FaceOperationValidation)
add_test(NAME SketchValidation COMMAND SketchValidation)
add_test(NAME SketchTrimValidation COMMAND SketchTrimValidation)
add_test(NAME SketchEditingValidation COMMAND SketchEditingValidation)
add_test(NAME SketchConstrainingValidation COMMAND SketchConstrainingValidation)
add_test(NAME BodyEditingValidation COMMAND BodyEditingValidation)
add_test(NAME SplineValidation COMMAND SplineValidation)
add_test(NAME FeatureValidation COMMAND FeatureValidation)
add_test(NAME CorpusValidation COMMAND CorpusValidation)
add_test(NAME RollingBallValidation COMMAND RollingBallValidation)
add_test(NAME SurfacingValidation COMMAND SurfacingValidation)
add_test(NAME PanelSurfacingValidation COMMAND PanelSurfacingValidation)
add_test(NAME NurbsValidation COMMAND NurbsValidation)
add_test(NAME SetbackValidation COMMAND SetbackValidation)
add_test(NAME BodyCutValidation COMMAND BodyCutValidation)
add_test(NAME SweepValidation COMMAND SweepValidation)
add_test(NAME ReplayValidation COMMAND ReplayValidation)
add_test(NAME MeasureValidation COMMAND MeasureValidation)
add_test(NAME AssemblyValidation COMMAND AssemblyValidation)
add_test(NAME DrawingValidation COMMAND DrawingValidation)
add_test(NAME ExchangeValidation COMMAND ExchangeValidation)
add_test(NAME FreeformBlendValidation COMMAND FreeformBlendValidation)
add_test(NAME PatternStressValidation COMMAND PatternStressValidation)
