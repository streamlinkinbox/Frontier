cmake_minimum_required(VERSION 3.20)
project(FacetCausalGpu LANGUAGES CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(Root "../../.." ABSOLUTE)
find_path(VulkanHeaders vulkan/vulkan.h HINTS "${Root}/ExternalPackages/Vulkan-Headers-1.4.321/include" REQUIRED)
set(FRONTIER_RENDERING_VULKAN_HEADERS "${VulkanHeaders}")
enable_testing()
add_subdirectory("../FacetCausal" "${CMAKE_CURRENT_BINARY_DIR}/native")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ../Code/LightComputeValidation.cpp ../Code/FacetAreaOcclusionGpuSubmission.py)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${Root}/Exhibits/LightCompute/Code/FacetAreaOcclusionGpuSubmission.py"
    "${CMAKE_CURRENT_BINARY_DIR}/FacetCausalGpuSubmission.h" COMMAND_ERROR_IS_FATAL ANY)
add_executable(FacetCausalGpuValidation ../Code/FacetCausalGpuValidation.cpp)
target_compile_features(FacetCausalGpuValidation PRIVATE cxx_std_20)
target_include_directories(FacetCausalGpuValidation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${Root}" ../Code
    "${CMAKE_CURRENT_BINARY_DIR}/native/norm/mixed/packet/native/baseline")
target_link_libraries(FacetCausalGpuValidation PRIVATE FrontierFacetAreaTransport FrontierFacetAreaReservoir
    FrontierPathReplayVulkan FrontierPathReplayDomain FrontierRadiantFacetDistribution FrontierMediumRadiancePayload
    ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(FacetCausalGpuValidation PRIVATE /utf-8 /W4 /fp:strict)
else()
    target_compile_options(FacetCausalGpuValidation PRIVATE -Wall -Wextra -Werror -Wno-unused-function
        -fno-fast-math -ffp-contract=off)
endif()
add_test(NAME FacetCausalGpuValidation COMMAND FacetCausalGpuValidation --unit)
