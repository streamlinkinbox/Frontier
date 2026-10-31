# Frontier Log Stream proof — portable engine, panel logic and headless host.
# Needs toml++ headers: point FRONTIER_TOMLPLUSPLUS_DIR at a tomlplusplus checkout
# (its include/ directory carries toml++/toml.h), or install tomlplusplus system-wide.
cmake_minimum_required(VERSION 3.16)
project(FrontierLogStreamProof CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)
get_filename_component(REPOSITORY_ROOT "${FRONTIER_ROOT}/.." ABSOLUTE)

find_path(TOMLPLUSPLUS_INCLUDE_DIR toml++/toml.h
    HINTS "${FRONTIER_TOMLPLUSPLUS_DIR}/include")
if(NOT TOMLPLUSPLUS_INCLUDE_DIR)
    message(FATAL_ERROR "toml++ headers not found; set FRONTIER_TOMLPLUSPLUS_DIR to a tomlplusplus checkout")
endif()

add_library(FrontierEnginePortable STATIC
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogRingStore.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogCategories.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogStream.cpp
    ${FRONTIER_ROOT}/Engine/Interface/ProjectContent.cpp
    ${FRONTIER_ROOT}/Engine/Interface/ProjectDescriptor.cpp
    ${FRONTIER_ROOT}/Engine/Interface/LogStreamPanel.cpp
    ${FRONTIER_ROOT}/Engine/Interface/LogStreamPanelPresentation.cpp
    ${FRONTIER_ROOT}/Applications/Editor/CommandLine.cpp
    ${FRONTIER_ROOT}/Applications/Editor/ProjectBrowserHost.cpp)
target_include_directories(FrontierEnginePortable PUBLIC ${FRONTIER_ROOT} ${TOMLPLUSPLUS_INCLUDE_DIR})

add_executable(LogStreamValidation
    LogStreamValidation.cpp
    ${FRONTIER_ROOT}/Projects/Project-Zero/Source/ProjectZeroInterchange.cpp)
target_include_directories(LogStreamValidation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Support)
target_link_libraries(LogStreamValidation PRIVATE FrontierEnginePortable)
find_package(Threads REQUIRED)
target_link_libraries(LogStreamValidation PRIVATE Threads::Threads)

add_executable(Frontier ${FRONTIER_ROOT}/Applications/Editor/WorkspaceApplication.cpp)
target_link_libraries(Frontier PRIVATE FrontierEnginePortable)

enable_testing()
add_test(NAME LogStreamValidation COMMAND LogStreamValidation ${REPOSITORY_ROOT})
add_test(NAME ProjectZeroHeadless COMMAND Frontier --project=${FRONTIER_ROOT}/Projects/Project-Zero/ProjectZero.frontier --headless)
