#!/usr/bin/env bash
#=============================================================================================================================================
# 📦 Frontier/BuildConfiguration/LinuxBuild.sh — High-Performance Linux GCC/Clang Build Script
#=============================================================================================================================================

set -e

echo "[Frontier Build] Compiling Frontier Engine for Linux (C++20, Release)..."

mkdir -p ../bin

g++ -std=c++20 -O3 -Wall -Wextra -Werror -pedantic -DFRONTIER_DEVELOPMENT -pthread -I.. \
    ../DeviceExchange/*.cpp \
    ../PhysicalDynamics/*.cpp \
    ../VolumetricDynamics/*.cpp \
    ../GeometricRaster/*.cpp \
    ../PhotometricIllumination/*.cpp \
    ../PlatformInterchange/*.cpp \
    ../DisplayPresentation/*.cpp \
    -o ../bin/FrontierEngine

echo "[Frontier Build] Build successful: bin/FrontierEngine"
