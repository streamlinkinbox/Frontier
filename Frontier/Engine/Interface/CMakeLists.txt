cmake_minimum_required(VERSION 3.20)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 📝 Gameplay mechanics (VehicleCabinHmi, PortalMechanics) live in Frontier/Engine/Gameplay/GameMechanics.
if(NOT TARGET FrontierGameMechanics)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Gameplay/GameMechanics" "${CMAKE_CURRENT_BINARY_DIR}/FrontierGameMechanics")
endif()
