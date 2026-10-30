cmake_minimum_required(VERSION 3.20)
project(FacetImageGpu LANGUAGES CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(Root "../../.." ABSOLUTE)
find_path(VulkanHeaders vulkan/vulkan.h HINTS "${Root}/ExternalPackages/Vulkan-Headers-1.4.321/include" REQUIRED)
set(FRONTIER_RENDERING_VULKAN_HEADERS "${VulkanHeaders}")
enable_testing()
add_subdirectory("../FacetPacketGpu" "${CMAKE_CURRENT_BINARY_DIR}/packet")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ../Code/FacetImageGpuSources.py ../Code/FacetPacketGpuValidation.cpp ../Code/FacetPacketGpu.slang)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${Root}/Exhibits/LightCompute/Code/FacetImageGpuSources.py"
    "${CMAKE_CURRENT_BINARY_DIR}" COMMAND_ERROR_IS_FATAL ANY)
add_executable(FacetImageGpuValidation ../Code/FacetImageGpuValidation.cpp)
target_compile_features(FacetImageGpuValidation PRIVATE cxx_std_20)
target_include_directories(FacetImageGpuValidation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${Root}" ../Code
    "${CMAKE_CURRENT_BINARY_DIR}/packet/fixed/native/norm/mixed/packet/native/baseline")
target_link_libraries(FacetImageGpuValidation PRIVATE FrontierFacetAreaTransport FrontierFacetAreaReservoir
    FrontierPathReplayVulkan FrontierPathReplayDomain FrontierRadiantFacetDistribution FrontierMediumRadiancePayload
    ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(FacetImageGpuValidation PRIVATE /utf-8 /W4 /fp:strict)
else()
    target_compile_options(FacetImageGpuValidation PRIVATE -Wall -Wextra -Werror -Wno-unused-function
        -fno-fast-math -ffp-contract=off)
endif()
add_test(NAME FacetImageGpuValidation COMMAND FacetImageGpuValidation --unit)
