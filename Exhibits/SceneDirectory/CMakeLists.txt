# Frontier record registration proof — constructor registers, destructor unregisters.
# Build in each profile to prove the gates:
#   (default)                     Development: store + SceneDirectory active
#   -DFRONTIER_EDITOR_PROFILE=1   Editor: same machinery as Development
#   -DFRONTIER_SHIPPING_PROFILE=1 Shipping: directory + panel drop out of the link set below;
#                                 the store keeps resolving tokens (plan §5/§6, decision D3)
cmake_minimum_required(VERSION 3.16)
project(FrontierSceneDirectoryProof CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)

set(SCENE_PROOF_SOURCES
    SceneDirectoryValidation.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogRingStore.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogCategories.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogStream.cpp
    ${FRONTIER_ROOT}/Engine/Scene/RecordStore.cpp
    ${FRONTIER_ROOT}/Engine/Scene/RecordEntry.cpp)
if(NOT FRONTIER_SHIPPING_PROFILE)
    list(APPEND SCENE_PROOF_SOURCES
        ${FRONTIER_ROOT}/Engine/Scene/SceneDirectory.cpp
        ${FRONTIER_ROOT}/Engine/Interface/SceneDirectoryPanel.cpp
        ${FRONTIER_ROOT}/Engine/Interface/SceneDirectoryPanelPresentation.cpp
        ${FRONTIER_ROOT}/Engine/Interface/StackRailControls.cpp)
endif()

add_executable(SceneDirectoryValidation ${SCENE_PROOF_SOURCES})
target_include_directories(SceneDirectoryValidation PRIVATE ${FRONTIER_ROOT})
if(FRONTIER_EDITOR_PROFILE)
    target_compile_definitions(SceneDirectoryValidation PRIVATE FRONTIER_EDITOR_PROFILE=1)
elseif(FRONTIER_SHIPPING_PROFILE)
    target_compile_definitions(SceneDirectoryValidation PRIVATE FRONTIER_SHIPPING_PROFILE=1)
endif()
find_package(Threads REQUIRED)
target_link_libraries(SceneDirectoryValidation PRIVATE Threads::Threads)

enable_testing()
add_test(NAME SceneDirectoryValidation COMMAND SceneDirectoryValidation)
