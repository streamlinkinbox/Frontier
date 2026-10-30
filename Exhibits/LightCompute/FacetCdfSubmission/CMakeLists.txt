cmake_minimum_required(VERSION 3.20)
project(FacetCdfSubmission LANGUAGES CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(Root "../../.." ABSOLUTE)
find_path(VulkanHeaders vulkan/vulkan.h HINTS "${Root}/ExternalPackages/Vulkan-Headers-1.4.321/include" REQUIRED)
set(FRONTIER_RENDERING_VULKAN_HEADERS "${VulkanHeaders}")
enable_testing()
add_subdirectory("../FacetCdfGpu" "${CMAKE_CURRENT_BINARY_DIR}/cdf")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ../Code/FacetCdfSubmissionSources.py ../Code/FacetPacketGpuValidation.cpp ../Code/FacetPacketGpu.slang
    ../Code/FacetImageGpuSources.py ../Code/AreaReconnectionValidation.h
    ../Code/FacetProfileGpuSources.py ../Code/FacetProfileGpuValidation.cpp
    ../Code/FacetCdfGpuSources.py ../Code/FacetCdfGpuValidation.cpp)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${Root}/Exhibits/LightCompute/Code/FacetCdfSubmissionSources.py"
    "${CMAKE_CURRENT_BINARY_DIR}" COMMAND_ERROR_IS_FATAL ANY)
add_executable(FacetCdfSubmissionValidation ../Code/FacetCdfSubmissionValidation.cpp)
target_compile_features(FacetCdfSubmissionValidation PRIVATE cxx_std_20)
target_include_directories(FacetCdfSubmissionValidation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${Root}" ../Code
    "${CMAKE_CURRENT_BINARY_DIR}/cdf/profile/image/packet/fixed/native/norm/mixed/packet/native/baseline")
target_link_libraries(FacetCdfSubmissionValidation PRIVATE FrontierFacetAreaTransport FrontierFacetAreaReservoir
    FrontierPathReplayVulkan FrontierPathReplayDomain FrontierRadiantFacetDistribution FrontierMediumRadiancePayload
    ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(FacetCdfSubmissionValidation PRIVATE /utf-8 /W4 /fp:strict)
else()
    target_compile_options(FacetCdfSubmissionValidation PRIVATE -Wall -Wextra -Werror -Wno-unused-function
        -fno-fast-math -ffp-contract=off)
endif()
add_test(NAME FacetCdfSubmissionValidation COMMAND FacetCdfSubmissionValidation --unit)
