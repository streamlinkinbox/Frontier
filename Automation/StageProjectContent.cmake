#============================================================================================================================================
#                                                     STAGEPROJECTCONTENT.CMAKE
#============================================================================================================================================
# 🧩 Compile-time automatic staging of declared [engine.content_imports].entries from EngineContent/ into <Project>/Content/.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set_property(GLOBAL PROPERTY FRONTIER_STAGE_CONTENT_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/StageProjectContent.py")

function(frontier_stage_project_content TARGET_NAME PROJECTSPACE_FILE ENGINE_CONTENT_ROOT PROJECT_CONTENT_ROOT)
    get_property(_STAGE_SCRIPT GLOBAL PROPERTY FRONTIER_STAGE_CONTENT_SCRIPT)
    get_filename_component(_PROJ_STEM "${PROJECTSPACE_FILE}" NAME_WE)
    set(_STAMP_TARGET "StageContent_${_PROJ_STEM}")
    set(_STAMP_FILE "${CMAKE_BINARY_DIR}/ContentStaging/${_PROJ_STEM}.stamp")

    if(NOT TARGET "${_STAMP_TARGET}")
        file(GLOB_RECURSE _ENGINE_CONTENT_INPUTS CONFIGURE_DEPENDS "${ENGINE_CONTENT_ROOT}/*")
        add_custom_command(
            OUTPUT "${_STAMP_FILE}"
            COMMAND "${Python3_EXECUTABLE}" "${_STAGE_SCRIPT}"
                --projectspace "${PROJECTSPACE_FILE}"
                --engine-content "${ENGINE_CONTENT_ROOT}"
                --project-content "${PROJECT_CONTENT_ROOT}"
                --stamp "${_STAMP_FILE}"
            DEPENDS "${PROJECTSPACE_FILE}" "${_STAGE_SCRIPT}" ${_ENGINE_CONTENT_INPUTS}
            COMMENT "Staging declared EngineContent imports into ${PROJECT_CONTENT_ROOT}"
            VERBATIM)
        add_custom_target("${_STAMP_TARGET}" ALL DEPENDS "${_STAMP_FILE}")
    endif()

    if(TARGET "${TARGET_NAME}")
        add_dependencies("${TARGET_NAME}" "${_STAMP_TARGET}")
    endif()
endfunction()
