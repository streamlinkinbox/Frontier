# Frontier render infrastructure proof — submission ordering, transient aliasing, threading.
cmake_minimum_required(VERSION 3.16)
project(FrontierRenderInfrastructureProof CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)

add_executable(RenderInfrastructureValidation
    RenderInfrastructureValidation.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogRingStore.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogCategories.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogStream.cpp
    ${FRONTIER_ROOT}/Engine/Interface/LogStreamPanel.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/SubmissionOrder.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/TransientTargetAtlas.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/BarrierDerivation.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/ViewportUnits.cpp
    ${FRONTIER_ROOT}/Engine/Rendering/InspectionUnits.cpp
    ${FRONTIER_ROOT}/Projects/Project-ProvingGrounds/Source/ProvingGrounds.cpp
    ${FRONTIER_ROOT}/Engine/Threading/WorkerPool.cpp)
target_include_directories(RenderInfrastructureValidation PRIVATE ${FRONTIER_ROOT})
find_package(Threads REQUIRED)
target_link_libraries(RenderInfrastructureValidation PRIVATE Threads::Threads)

# CPU capture harness: renders ProvingGrounds frames (PPM) with the SurfaceRaster.slang maths
# mirrored, for owner-visible evidence (Docs/ProgressEvidence.html). Not a test — a camera.
add_executable(ProvingGroundsCapture
    ProvingGroundsCapture.cpp
    ${FRONTIER_ROOT}/Projects/Project-ProvingGrounds/Source/ProvingGrounds.cpp)
target_include_directories(ProvingGroundsCapture PRIVATE ${FRONTIER_ROOT})

enable_testing()
add_test(NAME RenderInfrastructureValidation COMMAND RenderInfrastructureValidation)
