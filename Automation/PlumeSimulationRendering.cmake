#============================================================================================================================================
#                                                       PLUMESIMULATIONRENDERING.CMAKE
#============================================================================================================================================
# 🧩 The smoke and fire plume: the CPU modules (gas catalogue and presets, tyre telemetry projected into sources, the schedule, the step record
#    and the tiling detail volume) under Frontier/Engine/PhysicalDynamics/FluidDynamics and Frontier/Engine/Rendering/PlumeSimulation, ported from
#    the HMI prototype's SmokeSequence.js, PlumeSourceProjection.js and PlumeSpace.js.

add_library(FrontierPlumeSimulation STATIC
    Frontier/Engine/PhysicalDynamics/FluidDynamics/GasParameterCatalogue.cpp
    Frontier/Engine/PhysicalDynamics/FluidDynamics/PlumeSourceProjection.cpp
    Frontier/Engine/PhysicalDynamics/FluidDynamics/PlumeTelemetryProjection.cpp
    Frontier/Engine/Rendering/PlumeSimulation/PlumeSchedule.cpp
    Frontier/Engine/Rendering/PlumeSimulation/SmokeSequence.cpp
    Frontier/Engine/Rendering/PlumeSimulation/PlumeSimulationSequence.cpp)
target_include_directories(FrontierPlumeSimulation PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(FrontierPlumeSimulation PUBLIC FrontierGlobalIllumination)
target_compile_features(FrontierPlumeSimulation PUBLIC cxx_std_20)
# 📝 Parity with the demo's JavaScript doubles needs plain IEEE evaluation: no fused multiply-adds.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FrontierPlumeSimulation PRIVATE -ffp-contract=off)
elseif(MSVC)
    target_compile_options(FrontierPlumeSimulation PRIVATE /fp:precise)
endif()

# 📝 The units include the illumination port's intrinsics, host constants, scene layouts, distance field march, refresh profile and sky projection
#    (they bind its groups), so they compile with the same pinned compiler; without it the plume uses EngineContent/ShaderArchives/PlumeSimulation.
if(NOT FRONTIER_GLOBAL_ILLUMINATION_SLANGC)
    find_program(FRONTIER_GLOBAL_ILLUMINATION_SLANGC
        NAMES slangc
        PATHS "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin"
        NO_DEFAULT_PATH)
endif()
set(FRONTIER_PLUME_SHADER_SRC "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/PlumeSimulation")
set(FRONTIER_PLUME_SHADER_OUT "${CMAKE_BINARY_DIR}/ShaderArchives/PlumeSimulation")
file(GLOB FRONTIER_PLUME_SLANG_SOURCES CONFIGURE_DEPENDS "${FRONTIER_PLUME_SHADER_SRC}/*.slang"
     "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/GlobalIllumination/*.slang")

# Unit:Entry:Stage — one SPIR-V file per entry point, <Unit>.<Entry>.spv.
set(FRONTIER_PLUME_ENTRIES
    "PlumeAdvection:AdvectVelocity:compute"
    "PlumeVorticity:ResolveCurl:compute"
    "PlumeVorticity:ConfineVorticity:compute"
    "PlumePressure:ResolveDivergence:compute"
    "PlumePressure:RelaxPressure:compute"
    "PlumePressure:ProjectVelocity:compute"
    "PlumeAdvection:AdvectThermo:compute"
    "PlumeAdvection:ResolveSunTransmittance:compute"
    "PlumeMarch:MarchPlume:compute")

set(FRONTIER_PLUME_SPV_OUTPUTS)
if(FRONTIER_GLOBAL_ILLUMINATION_SLANGC)
    foreach(ENTRY_SPEC IN LISTS FRONTIER_PLUME_ENTRIES)
        string(REPLACE ":" ";" ENTRY_PARTS "${ENTRY_SPEC}")
        list(GET ENTRY_PARTS 0 ENTRY_UNIT)
        list(GET ENTRY_PARTS 1 ENTRY_NAME)
        list(GET ENTRY_PARTS 2 ENTRY_STAGE)
        set(ENTRY_SPV "${FRONTIER_PLUME_SHADER_OUT}/${ENTRY_UNIT}.${ENTRY_NAME}.spv")
        list(APPEND FRONTIER_PLUME_SPV_OUTPUTS "${ENTRY_SPV}")
        add_custom_command(
            OUTPUT "${ENTRY_SPV}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_PLUME_SHADER_OUT}"
            COMMAND "${FRONTIER_GLOBAL_ILLUMINATION_SLANGC}" "${FRONTIER_PLUME_SHADER_SRC}/${ENTRY_UNIT}.slang"
                -I "${FRONTIER_PLUME_SHADER_SRC}"
                -I "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders/GlobalIllumination"
                -entry "${ENTRY_NAME}"
                -stage "${ENTRY_STAGE}"
                -target spirv
                -profile spirv_1_3
                -emit-spirv-directly
                -matrix-layout-row-major
                -fvk-use-entrypoint-name
                -O0
                -o "${ENTRY_SPV}"
            DEPENDS ${FRONTIER_PLUME_SLANG_SOURCES}
            VERBATIM)
    endforeach()
    add_custom_target(FrontierPlumeSimulationShaders ALL DEPENDS ${FRONTIER_PLUME_SPV_OUTPUTS})
endif()
if(TARGET FrontierPlumeSimulationShaders)
    add_dependencies(FrontierPlumeSimulation FrontierPlumeSimulationShaders)
endif()

if(BUILD_TESTING)
    # 📝 The C++ plume's CPU modules against the HTML prototype's own (Exhibits/PlumeSimulation/Results/PlumeReference.toml): the catalogue and
    #    presets, the settings map, the detail volume, the xorshift, the onset, the projection and the whole sequence over a burnout.
    add_executable(PlumeSimulationValidation Exhibits/PlumeSimulation/Code/PlumeSimulationValidation.cpp)
    target_include_directories(PlumeSimulationValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
    target_link_libraries(PlumeSimulationValidation PRIVATE FrontierPlumeSimulation)
    # 📝 The validation repeats the exporter's input script; its arithmetic must not be fused either.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(PlumeSimulationValidation PRIVATE -ffp-contract=off)
    elseif(MSVC)
        target_compile_options(PlumeSimulationValidation PRIVATE /fp:precise)
    endif()
    add_test(NAME PlumeSimulationValidation COMMAND PlumeSimulationValidation "${CMAKE_SOURCE_DIR}")
    set_tests_properties(PlumeSimulationValidation PROPERTIES TIMEOUT 120)

    # 📝 The plume on its own Vulkan device: a capture of the burnout and, against the reference folder PlumeParityGpu.mjs writes from the demo's WGSL on
    #    Dawn, a parity run (Exhibits/PlumeSimulation/Code/RunPlumeParity.sh). The scene distance field, shadow and sky it binds are stand-ins (see the file).
    add_executable(PlumeSimulationCapture
        Exhibits/PlumeSimulation/Code/PlumeSimulationCapture.cpp
        Engine/Render/StbImageWriteImplementation.cpp)
    target_include_directories(PlumeSimulationCapture PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb" "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
    target_link_libraries(PlumeSimulationCapture PRIVATE FrontierPlumeSimulation)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(PlumeSimulationCapture PRIVATE -ffp-contract=off)
    elseif(MSVC)
        target_compile_options(PlumeSimulationCapture PRIVATE /fp:precise)
    endif()
endif()
