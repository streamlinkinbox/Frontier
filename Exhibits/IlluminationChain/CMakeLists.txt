cmake_minimum_required(VERSION 3.20)
project(FrontierIlluminationChainExhibit CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)

add_executable(IlluminationChainValidation
    IlluminationChainValidation.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogRingStore.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogCategories.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogStream.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/DiscrepancySequence.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/ReservoirSampling.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/VarianceFiltering.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/IlluminationUnits.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/SubmissionOrder.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/TransientTargetAtlas.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/BarrierDerivation.cpp)

target_include_directories(IlluminationChainValidation PRIVATE ${FRONTIER_ROOT})
find_package(Threads REQUIRED)
target_link_libraries(IlluminationChainValidation PRIVATE Threads::Threads)

enable_testing()
add_test(NAME IlluminationChainValidation COMMAND IlluminationChainValidation)
