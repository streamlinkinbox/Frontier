cmake_minimum_required(VERSION 3.20)
project(FacetCdfShader LANGUAGES CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(Root "../../.." ABSOLUTE)
find_path(VulkanHeaders vulkan/vulkan.h HINTS "${Root}/ExternalPackages/Vulkan-Headers-1.4.321/include" REQUIRED)
set(FRONTIER_RENDERING_VULKAN_HEADERS "${VulkanHeaders}")
enable_testing()
add_subdirectory("../FacetCdfSubmission" "${CMAKE_CURRENT_BINARY_DIR}/submission")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ../Code/FacetCdfShaderSources.py ../Code/FacetPacketGpuValidation.cpp ../Code/FacetPacketGpu.slang
    ../Code/FacetImageGpuSources.py ../Code/AreaReconnectionValidation.h
    ../Code/FacetProfileGpuSources.py ../Code/FacetProfileGpuValidation.cpp
    ../Code/FacetCdfGpuSources.py ../Code/FacetCdfGpuValidation.cpp
    ../Code/FacetCdfSubmissionSources.py ../Code/FacetCdfSubmissionValidation.cpp)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${Root}/Exhibits/LightCompute/Code/FacetCdfShaderSources.py"
    "${CMAKE_CURRENT_BINARY_DIR}" COMMAND_ERROR_IS_FATAL ANY)
add_executable(FacetCdfShaderValidation ../Code/FacetCdfShaderValidation.cpp)
target_compile_features(FacetCdfShaderValidation PRIVATE cxx_std_20)
target_include_directories(FacetCdfShaderValidation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${Root}" ../Code
    "${CMAKE_CURRENT_BINARY_DIR}/submission/cdf/profile/image/packet/fixed/native/norm/mixed/packet/native/baseline")
target_link_libraries(FacetCdfShaderValidation PRIVATE FrontierFacetAreaTransport FrontierFacetAreaReservoir
    FrontierPathReplayVulkan FrontierPathReplayDomain FrontierRadiantFacetDistribution FrontierMediumRadiancePayload
    ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(FacetCdfShaderValidation PRIVATE /utf-8 /W4 /fp:strict)
else()
    target_compile_options(FacetCdfShaderValidation PRIVATE -Wall -Wextra -Werror -Wno-unused-function
        -fno-fast-math -ffp-contract=off)
endif()
add_test(NAME FacetCdfShaderValidation COMMAND FacetCdfShaderValidation --unit)
