# 🧩 Generate retained demo observations and run the standalone Engine continuum validation.
execute_process(COMMAND "${NODE}" "${ROOT}/Exhibits/VehiclePhysics/Code/TyreContinuumReference.mjs"
    WORKING_DIRECTORY "${ROOT}" OUTPUT_FILE "${OBSERVATIONS}" RESULT_VARIABLE REFERENCE_RESULT)
if(NOT REFERENCE_RESULT EQUAL 0)
    message(FATAL_ERROR "JavaScript continuum reference failed: ${REFERENCE_RESULT}")
endif()
execute_process(COMMAND "${PROBE}" "${OBSERVATIONS}" RESULT_VARIABLE VALIDATION_RESULT)
if(NOT VALIDATION_RESULT EQUAL 0)
    message(FATAL_ERROR "Standalone Engine continuum validation failed: ${VALIDATION_RESULT}")
endif()
