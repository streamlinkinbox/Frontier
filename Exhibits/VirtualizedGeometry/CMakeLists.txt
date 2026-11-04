cmake_minimum_required(VERSION 3.20)
project(FrontierVirtualizedGeometryExhibit CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)

# The branch's C++ tier build, measured on the VehicleHMI demo's corpus surfaces (see README.md).
add_executable(TierComparison
    Code/TierComparison.cpp
    ${FRONTIER_ROOT}/Engine/Geometry/GeometryProcessing/FacetTierBuild.cpp
    ${FRONTIER_ROOT}/Engine/Geometry/GeometryProcessing/QuadricTierBuild.cpp
    ${FRONTIER_ROOT}/Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.cpp)

target_include_directories(TierComparison PRIVATE ${FRONTIER_ROOT})
