cmake_minimum_required(VERSION 3.20)
project(FrontierIndirectIlluminationExhibit CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)

add_executable(IndirectIlluminationValidation
    IndirectIlluminationValidation.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogRingStore.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogCategories.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogStream.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/DiscrepancySequence.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/DistanceField.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/SurfaceCards.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/IlluminationUnits.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/SubmissionOrder.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/TransientTargetAtlas.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/BarrierDerivation.cpp)

target_include_directories(IndirectIlluminationValidation PRIVATE ${FRONTIER_ROOT})
find_package(Threads REQUIRED)
target_link_libraries(IndirectIlluminationValidation PRIVATE Threads::Threads)

enable_testing()
add_test(NAME IndirectIlluminationValidation COMMAND IndirectIlluminationValidation)
