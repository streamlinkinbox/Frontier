#=============================================================================================================================================
# ParametricSketcher/CMakeLists.txt — SolidArc: standalone NURBS modelling tool — kernel, interaction, presentation (software + Vulkan), console host
#   Builds with no engine dependency and no external packages. Linux: cmake -B build -G Ninja && cmake --build build && ctest --test-dir build
#=============================================================================================================================================

cmake_minimum_required(VERSION 3.20)
project(SolidArc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wno-unused-function)
endif()

# ─── Kernel: pure geometry, zero dependencies ───────────────────────────────────────────────────────────────────────────────────────────────
add_library(SolidArcKernel STATIC
    Kernel/CurveSpecification.cpp
    Kernel/SurfaceSpecification.cpp
)
target_include_directories(SolidArcKernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ─── Presentation + Interaction: RasterExchange contract, CPU SoftwareRaster (Slang shaders mirrored in C++), camera ─────────────────────────
add_library(SolidArcPresentation STATIC
    Presentation/SoftwareRaster.cpp
    Presentation/ScenePresentation.cpp
    Interaction/CameraProjection.cpp
)
target_link_libraries(SolidArcPresentation PUBLIC SolidArcKernel)
target_include_directories(SolidArcPresentation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Presentation)
target_compile_definitions(SolidArcPresentation PUBLIC SOLIDARC_PROOF_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/Proofs")

# ─── Document + Console: item store, command codec, console host, SolidArc executable ───────────────────────────────────────────────────────
add_library(SolidArcConsoleKit STATIC
    Document/SceneDocument.cpp
    Console/CommandCodec.cpp
    Console/ConsoleHost.cpp
)
target_link_libraries(SolidArcConsoleKit PUBLIC SolidArcPresentation)

add_executable(SolidArc Console/SolidArcConsole.cpp)
target_link_libraries(SolidArc PRIVATE SolidArcConsoleKit)

# ─── Verification: console proofs, one executable per phase, registered with ctest ──────────────────────────────────────────────────────────
enable_testing()

add_executable(KernelVerification Verification/KernelVerification.cpp)
target_link_libraries(KernelVerification PRIVATE SolidArcKernel)
add_test(NAME KernelVerification COMMAND KernelVerification)

add_executable(RasterVerification Verification/RasterVerification.cpp)
target_link_libraries(RasterVerification PRIVATE SolidArcPresentation)
add_test(NAME RasterVerification COMMAND RasterVerification)

add_test(NAME Phase2_Primitives COMMAND SolidArc ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/Phase2_Primitives.arc)
