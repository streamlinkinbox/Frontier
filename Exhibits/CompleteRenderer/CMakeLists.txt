cmake_minimum_required(VERSION 3.20)
project(FrontierCompleteRendererExhibit CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)
set(RENDERING ${FRONTIER_ROOT}/Engine/Rendering)
set(DIAGNOSTICS
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogRingStore.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogCategories.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogStream.cpp)
set(REFERENCE_SOURCES
    ${RENDERING}/ClusterNesting.cpp
    ${RENDERING}/VisibilityCull.cpp
    ${RENDERING}/DiscrepancySequence.cpp
    ${RENDERING}/ReservoirSampling.cpp
    ${RENDERING}/VarianceFiltering.cpp
    ${RENDERING}/DistanceField.cpp
    ${RENDERING}/SurfaceCards.cpp
    ${RENDERING}/AtmosphereModel.cpp
    ${RENDERING}/CloudField.cpp
    ${RENDERING}/FogMedium.cpp
    ${RENDERING}/ReferenceRenderer.cpp)
add_executable(CompleteRendererCapture CompleteRendererCapture.cpp ${DIAGNOSTICS} ${REFERENCE_SOURCES})
add_executable(CompleteRendererValidation CompleteRendererValidation.cpp ${DIAGNOSTICS} ${REFERENCE_SOURCES}
    ${RENDERING}/IlluminationUnits.cpp
    ${RENDERING}/EnvironmentUnits.cpp
    ${RENDERING}/CompleteFrameUnits.cpp
    ${RENDERING}/InspectionUnits.cpp
    ${RENDERING}/SubmissionOrder.cpp
    ${RENDERING}/TransientTargetAtlas.cpp
    ${RENDERING}/BarrierDerivation.cpp)
target_include_directories(CompleteRendererCapture PRIVATE ${FRONTIER_ROOT})
target_include_directories(CompleteRendererValidation PRIVATE ${FRONTIER_ROOT})
find_package(Threads REQUIRED)
target_link_libraries(CompleteRendererCapture PRIVATE Threads::Threads)
target_link_libraries(CompleteRendererValidation PRIVATE Threads::Threads)
enable_testing()
add_test(NAME CompleteRendererValidation COMMAND CompleteRendererValidation)
