cmake_minimum_required(VERSION 3.20)
project(FacetProfileGpu LANGUAGES CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(Root "../../.." ABSOLUTE)
find_path(VulkanHeaders vulkan/vulkan.h HINTS "${Root}/ExternalPackages/Vulkan-Headers-1.4.321/include" REQUIRED)
set(FRONTIER_RENDERING_VULKAN_HEADERS "${VulkanHeaders}")
enable_testing()
add_subdirectory("../FacetImageGpu" "${CMAKE_CURRENT_BINARY_DIR}/image")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ../Code/FacetProfileGpuSources.py ../Code/FacetPacketGpuValidation.cpp ../Code/FacetPacketGpu.slang
    ../Code/FacetImageGpuSources.py ../Code/AreaReconnectionValidation.h)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${Root}/Exhibits/LightCompute/Code/FacetProfileGpuSources.py"
    "${CMAKE_CURRENT_BINARY_DIR}" COMMAND_ERROR_IS_FATAL ANY)
add_executable(FacetProfileGpuValidation ../Code/FacetProfileGpuValidation.cpp)
target_compile_features(FacetProfileGpuValidation PRIVATE cxx_std_20)
target_include_directories(FacetProfileGpuValidation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${Root}" ../Code
    "${CMAKE_CURRENT_BINARY_DIR}/image/packet/fixed/native/norm/mixed/packet/native/baseline")
target_link_libraries(FacetProfileGpuValidation PRIVATE FrontierFacetAreaTransport FrontierFacetAreaReservoir
    FrontierPathReplayVulkan FrontierPathReplayDomain FrontierRadiantFacetDistribution FrontierMediumRadiancePayload
    ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(FacetProfileGpuValidation PRIVATE /utf-8 /W4 /fp:strict)
else()
    target_compile_options(FacetProfileGpuValidation PRIVATE -Wall -Wextra -Werror -Wno-unused-function
        -fno-fast-math -ffp-contract=off)
endif()
add_test(NAME FacetProfileGpuValidation COMMAND FacetProfileGpuValidation --unit)
