cmake_minimum_required(VERSION 3.20)
project(EnvironmentScene LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(Root "../.." ABSOLUTE)

add_executable(EnvironmentSceneValidation
    Code/EnvironmentSceneValidation.cpp
    "${Root}/Frontier/Engine/Scene/RecordStore.cpp"
    "${Root}/Frontier/Engine/Scene/RecordEntry.cpp"
    "${Root}/Frontier/Engine/Scene/SceneDirectory.cpp"
    "${Root}/Frontier/Engine/Scene/EnvironmentRecords.cpp"
    "${Root}/Frontier/Engine/Interface/SceneDirectoryPanel.cpp"
    "${Root}/Frontier/Engine/Rendering/AtmosphereModel.cpp"
    "${Root}/Frontier/Engine/Rendering/CloudField.cpp"
    "${Root}/Frontier/Engine/Rendering/FogMedium.cpp"
    "${Root}/Frontier/Engine/Rendering/CelestialModel.cpp"
    "${Root}/Frontier/Engine/Rendering/ClusterNesting.cpp"
    "${Root}/Frontier/Engine/Diagnostics/LogRingStore.cpp"
    "${Root}/Frontier/Engine/Diagnostics/LogCategories.cpp"
    "${Root}/Frontier/Engine/Diagnostics/LogStream.cpp")
target_include_directories(EnvironmentSceneValidation PRIVATE "${Root}/Frontier" "${Root}")
target_compile_definitions(EnvironmentSceneValidation PRIVATE FRONTIER_EDITOR_PROFILE=1)
find_package(Threads REQUIRED)
target_link_libraries(EnvironmentSceneValidation PRIVATE Threads::Threads)
if(NOT MSVC)
    target_compile_options(EnvironmentSceneValidation PRIVATE -Wall -Wextra -Werror)
endif()
enable_testing()
add_test(NAME EnvironmentSceneValidation COMMAND EnvironmentSceneValidation "${CMAKE_CURRENT_BINARY_DIR}/EnvironmentScene.ppm")
