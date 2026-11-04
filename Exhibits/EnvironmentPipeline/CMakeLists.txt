cmake_minimum_required(VERSION 3.20)
project(FrontierEnvironmentPipelineExhibit CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(EnvironmentPipelineValidation
    EnvironmentPipelineValidation.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogRingStore.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogCategories.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogStream.cpp
    ${FRONTIER_ROOT}/Engine/DeviceExchange/VulkanComputeExchange.cpp
    ${FRONTIER_ROOT}/Engine/PlanetaryAtmosphere/AtmosphereScattering.cpp
    ${FRONTIER_ROOT}/Engine/PlanetaryAtmosphere/AstronomicalEphemeris.cpp
    ${FRONTIER_ROOT}/Engine/PlanetaryAtmosphere/CloudSpace.cpp
    ${FRONTIER_ROOT}/Engine/PlanetaryAtmosphere/FogSpace.cpp
    ${FRONTIER_ROOT}/Engine/PlanetaryAtmosphere/OpticalLensFlare.cpp
    ${FRONTIER_ROOT}/Engine/PlanetaryAtmosphere/WeatherEffects.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/EnvironmentUnits.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/SubmissionOrder.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/TransientTargetAtlas.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/BarrierDerivation.cpp)

target_include_directories(EnvironmentPipelineValidation PRIVATE
    ${FRONTIER_ROOT}
    ${REPO_ROOT}/ExternalPackages/stb
    ${REPO_ROOT}/ExternalPackages/vulkan-headers/include)
target_compile_definitions(EnvironmentPipelineValidation PRIVATE VK_NO_PROTOTYPES)
find_package(Threads REQUIRED)
target_link_libraries(EnvironmentPipelineValidation PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

enable_testing()
add_test(NAME EnvironmentPipelineValidation COMMAND EnvironmentPipelineValidation)
