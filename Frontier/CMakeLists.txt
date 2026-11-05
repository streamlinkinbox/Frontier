# Frontier engine — session & game mechanics (standalone slice).
#
# This library has no engine, renderer or GLFW dependency: it builds and validates anywhere a C++17
# compiler does. Define FRONTIER_WITH_GLFW and link glfw to enable InputEncoder::SampleGlfwWindow /
# InputDriver::DriveFromWindow against a live window.
#
#   cmake -S Frontier -B /tmp/frontier-build && cmake --build /tmp/frontier-build
#   /tmp/frontier-build/SessionValidation

cmake_minimum_required(VERSION 3.16)
project(FrontierSession CXX)

add_library(FrontierSession STATIC
    Engine/Input/InputEncoder.cpp
    Engine/Input/InputDriver.cpp
    Engine/Input/SyntheticDriver.cpp
    Engine/Scene/DeployableUnit.cpp
    Engine/Scene/FacetArchive.cpp
    Engine/Session/SpawnPolicy.cpp
    Engine/Session/SessionConfiguration.cpp
    Engine/Session/SessionPolicy.cpp
    Engine/Session/SessionSpace.cpp
    Engine/Session/TransferConfiguration.cpp
    Engine/Session/LevelTransition.cpp
    ../Projects/Everest/Source/Session/SessionPolicy.cpp)
target_include_directories(FrontierSession PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(FrontierSession PUBLIC cxx_std_17)

add_executable(SessionValidation ../Validation/SessionValidation.cpp)
target_link_libraries(SessionValidation PRIVATE FrontierSession)

add_library(FrontierGame STATIC Game/LobbyGame.cpp Game/SoftwarePresentation.cpp)
target_link_libraries(FrontierGame PUBLIC FrontierSession)

add_executable(LobbyGame Game/LobbyGameMain.cpp)
target_link_libraries(LobbyGame PRIVATE FrontierGame)
