#============================================================================================================================================
#                                                    PROJECTDRIVE.CMAKE
#============================================================================================================================================
# 📦 Defines ProjectDrive.dll and its headless content author; Frontier.exe owns the window, device, renderer, editor, input, camera, and sky.

file(READ "${CMAKE_CURRENT_LIST_DIR}/DriveSimulationSources.json" DriveSimulationManifest)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/DriveSimulationSources.json")
string(JSON DriveSimulationCount LENGTH "${DriveSimulationManifest}" sources)
math(EXPR DriveSimulationLast "${DriveSimulationCount} - 1")
foreach(Slot RANGE 0 ${DriveSimulationLast})
    string(JSON Source GET "${DriveSimulationManifest}" sources ${Slot})
    list(APPEND PROJECT_DRIVE_CODE_IMAGE "${Source}")
endforeach()

foreach(ProjectDriveSource IN LISTS PROJECT_DRIVE_CODE_IMAGE)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ProjectDriveSource}")
        message(FATAL_ERROR "ProjectDrive code image source is missing: ${ProjectDriveSource}")
    endif()
endforeach()

add_library(ProjectDrive SHARED ${PROJECT_DRIVE_CODE_IMAGE})
target_include_directories(ProjectDrive PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/ProjectInterchange
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/ExternalPackages/vulkan-headers/include
)
set_target_properties(ProjectDrive PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Drive/Build"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Projects/Project-Drive/Build"
)

include(${CMAKE_CURRENT_LIST_DIR}/DriveContent.cmake)
