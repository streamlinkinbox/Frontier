cmake_minimum_required(VERSION 3.20)
project(FrontierEnvironmentPipelineExhibit CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)

add_executable(EnvironmentPipelineValidation
    EnvironmentPipelineValidation.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogRingStore.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogCategories.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogStream.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/AtmosphereModel.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/CelestialModel.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/CloudField.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/FogMedium.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/WeatherEffects.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/EnvironmentUnits.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/SubmissionOrder.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/TransientTargetAtlas.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/BarrierDerivation.cpp)

target_include_directories(EnvironmentPipelineValidation PRIVATE ${FRONTIER_ROOT})
find_package(Threads REQUIRED)
target_link_libraries(EnvironmentPipelineValidation PRIVATE Threads::Threads)

enable_testing()
add_test(NAME EnvironmentPipelineValidation COMMAND EnvironmentPipelineValidation)
