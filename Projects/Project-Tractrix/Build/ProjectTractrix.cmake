#============================================================================================================================================
#                                                  PROJECTTRACTRIX.CMAKE
#============================================================================================================================================
# 📦 Defines ProjectTractrix.dll as a standalone project code image opened by Frontier.exe.

set(PROJECT_TRACTRIX_CODE_IMAGE
    Projects/Project-Tractrix/Source/ProjectTractrixInterchange.cpp
)

foreach(ProjectTractrixSource IN LISTS PROJECT_TRACTRIX_CODE_IMAGE)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ProjectTractrixSource}")
        message(FATAL_ERROR "ProjectTractrix code image source is missing: ${ProjectTractrixSource}")
    endif()
endforeach()

add_library(ProjectTractrix SHARED ${PROJECT_TRACTRIX_CODE_IMAGE})
target_include_directories(ProjectTractrix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/ProjectInterchange
)
set_target_properties(ProjectTractrix PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Tractrix/Build"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Tractrix/Build"
)
