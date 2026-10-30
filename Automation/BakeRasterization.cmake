#============================================================================================================================================
#                                                           BAKERASTERIZATION.CMAKE
#============================================================================================================================================
# 🧩 Build the Vulkan compute bake host, its BakeFieldCompute shader archive, and the CPU reference validation.

target_sources(FrontierSurfaceRasterization PRIVATE
    Engine/Render/BakeRasterization.cpp)
set(BAKE_FIELD_ARCHIVE "${FRONTIER_SURFACE_SHADER_DIRECTORY}/BakeFieldCompute.spv")
if(FRONTIER_SLANGC)
    add_custom_command(OUTPUT "${BAKE_FIELD_ARCHIVE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
        COMMAND "${FRONTIER_SLANGC}" "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/BakeFieldEvaluation.slang"
            -entry BakeFieldCompute -stage compute -target spirv -profile spirv_1_3
            -o "${BAKE_FIELD_ARCHIVE}"
        DEPENDS Engine/Render/Shader/BakeFieldEvaluation.slang VERBATIM)
elseif(FRONTIER_SLANG_WASM)
    add_custom_command(OUTPUT "${BAKE_FIELD_ARCHIVE}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}/BakeFieldCompilation.json"
        COMMAND "${FRONTIER_NODE}" "${CMAKE_SOURCE_DIR}/Automation/CompileBakeField.mjs"
            "${FRONTIER_SLANG_WASM}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS Automation/CompileBakeField.mjs Engine/Render/Shader/BakeFieldEvaluation.slang
            "${FRONTIER_SLANG_WASM}" VERBATIM)
endif()
if(FRONTIER_SLANGC OR FRONTIER_SLANG_WASM)
    add_custom_target(FrontierBakeFieldShaders ALL DEPENDS "${BAKE_FIELD_ARCHIVE}")
    if(TARGET TextureStudio)
        add_dependencies(TextureStudio FrontierBakeFieldShaders)
        install(FILES "${BAKE_FIELD_ARCHIVE}" DESTINATION EngineContent/ShaderArchives/Surface)
    endif()
endif()
if(BUILD_TESTING)
    # 📝 CPU-only by design: proves the flattened field the GPU shader consumes reproduces RunMeshBake
    # byte-for-byte, so it runs everywhere CTest runs without a Vulkan driver.
    add_executable(BakeFieldValidation Exhibits/TextureStudio/Code/BakeFieldValidation.cpp)
    target_include_directories(BakeFieldValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(BakeFieldValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME BakeFieldValidation COMMAND BakeFieldValidation "${CMAKE_BINARY_DIR}/BakeFieldEvidence")
    set_tests_properties(BakeFieldValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
