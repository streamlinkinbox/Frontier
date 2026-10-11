#============================================================================================================================================
# Projects/Project-Gas/CMakeLists.txt — the standalone gas host
#   Built on its own with:  cmake -S . -B Build/Gas -DFRONTIER_GAS_ONLY=ON && cmake --build Build/Gas
#   Deliberately depends on no SDK: four engine headers, toml++, and a dependency-free PNG writer.
#============================================================================================================================================

cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ProjectGas LANGUAGES CXX)
    enable_testing()
endif()
get_filename_component(FRONTIER_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# toml++ is a locked archive rather than a submodule, and the bootstrap already carries it in the proof
# profile. Configuring without it is a clear message now rather than a wall of template errors later.
if(NOT EXISTS "${FRONTIER_SOURCE_ROOT}/ExternalPackages/tomlpp/include/toml++/toml.hpp")
    message(FATAL_ERROR "[Project-Gas] toml++ absent. Run: python3 Tools/Bootstrap.py --package tomlpp")
endif()

add_executable(Project-Gas Source/GasHostMain.cpp)
target_compile_features(Project-Gas PRIVATE cxx_std_20)
target_include_directories(Project-Gas PRIVATE
    "${FRONTIER_SOURCE_ROOT}/Engine/VolumetricDynamics"
    "${FRONTIER_SOURCE_ROOT}/Engine/DisplayPresentation"
    "${FRONTIER_SOURCE_ROOT}/Engine/ContentInterchange"
    "${FRONTIER_SOURCE_ROOT}/ExternalPackages/tomlpp/include")
if(MSVC)
    target_compile_options(Project-Gas PRIVATE /utf-8)
endif()

# The committed corpus is the test: every sample must open, and every sample must be written back byte for
# byte. `cross` returns non-zero on the first byte that differs, so ctest needs no comparison of its own.
include(CTest)
if(BUILD_TESTING)
    file(GLOB GAS_SAMPLE_SCENES "${FRONTIER_SOURCE_ROOT}/Experimental/Fluid/Samples/*.gasscene.toml")
    foreach(GAS_SAMPLE ${GAS_SAMPLE_SCENES})
        get_filename_component(GAS_SAMPLE_NAME "${GAS_SAMPLE}" NAME_WE)
        add_test(NAME ProjectGasCross-${GAS_SAMPLE_NAME} COMMAND Project-Gas cross "${GAS_SAMPLE}")
    endforeach()
endif()
