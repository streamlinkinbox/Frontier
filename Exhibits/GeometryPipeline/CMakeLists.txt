cmake_minimum_required(VERSION 3.20)
project(FrontierGeometryPipelineExhibit CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)

add_executable(GeometryPipelineValidation
    GeometryPipelineValidation.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/ClusterNesting.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/VisibilityCull.cpp)

target_include_directories(GeometryPipelineValidation PRIVATE ${FRONTIER_ROOT})
