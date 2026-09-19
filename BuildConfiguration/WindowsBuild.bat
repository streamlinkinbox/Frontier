@echo off
rem ==========================================================================================================================================
rem 📦 Frontier/BuildConfiguration/WindowsBuild.bat — Cross-Platform Windows MSVC Build Script
rem ==========================================================================================================================================

echo [Frontier Build] Configuring CMake Build for Windows (MSVC, C++20, Release)...

if not exist ..\build (
    mkdir ..\build
)

cd ..\build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

echo [Frontier Build] Build successful: build\Release\FrontierEngine.exe
cd ..\BuildConfiguration
