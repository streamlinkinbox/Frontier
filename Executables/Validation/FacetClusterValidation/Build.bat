@echo off
REM ============================================================================
REM  FacetClusterValidation\Build.bat - build + run FacetClusterValidation.exe,
REM  the exit gate for the K1 facet weld, scored cluster partition and partition
REM  invariants.
REM
REM  CPU ONLY. No device, no surface, no shaders: the build it judges is pure
REM  C++20 over local-space facet soup, so this gate links neither vulkan-1.lib
REM  nor Platform.lib and does NOT run ShaderPlan.
REM
REM  Parity expectations come from the HTML reference builder
REM  (Experimental/VehicleHmiPrototype/Source/GeometryHierarchy.js) run on the
REM  same corpora; see Exhibits/FacetCluster/README.md for the replay steps.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=FacetClusterValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJ%" mkdir "%OBJ%"

where cl >nul 2>nul
if errorlevel 1 (
    echo [%NAME%] cl.exe not on PATH. Run this from a Developer PowerShell for VS, or through the pinned toolchain.
    exit /b 2
)

cl /nologo /std:c++20 /EHsc /W4 /permissive- /I"%ROOT%" /Fo:"%OBJ%\\" /Fe:"%OUTPUT%" ^
    "%APPDIR%\%NAME%.cpp" "%ROOT%\Engine\Geometry\FacetClusterBuild.cpp"
if errorlevel 1 (
    echo [%NAME%] build failed
    exit /b 2
)

"%OUTPUT%" %*
exit /b %ERRORLEVEL%
