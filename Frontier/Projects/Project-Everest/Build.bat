@echo off
REM ============================================================================
REM  Build.bat - compile the Project Everest code image (ProjectEverest.dll).
REM ============================================================================
setlocal EnableDelayedExpansion

set "ROOT=%~dp0"
set "OUT=%ROOT%Build\Output"
if not exist "%OUT%" mkdir "%OUT%"

if not defined FRONTIER_SDK (
    echo FRONTIER_SDK is not set; point it at the host SDK root containing ProjectInterchange.h.
    exit /b 1
)
if not exist "%FRONTIER_SDK%\ProjectInterchange.h" (
    echo ProjectInterchange.h not found under %FRONTIER_SDK%.
    exit /b 1
)

set "PFX86=%ProgramFiles(x86)%"
set "VSWHERE=%PFX86%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
)
set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo MSVC x64 toolchain not found via vswhere.
    exit /b 1
)
call "%VCVARS%" >nul || exit /b 1

pushd "%OUT%"
cl /nologo /std:c++20 /EHsc /MD /utf-8 /O2 /LD /DPROJECTEVEREST_BUILD /I"%FRONTIER_SDK%" ^
   "%ROOT%Source\ProjectEverestInterchange.cpp" ^
   /link /OUT:"ProjectEverest.dll" || (popd & exit /b 1)
popd

set "BINARIES=%ROOT%..\..\Binaries"
set "ENGINE_CONTENT=%ROOT%..\..\..\EngineContent"
for /f "usebackq tokens=*" %%E in (`powershell -NoProfile -Command "$t = Get-Content -Raw '%ROOT%ContentManifest.toml'; [regex]::Matches($t, '\"([^\"]+)\"') | ForEach-Object { $_.Groups[1].Value }"`) do (
    echo Staging %%E
    robocopy "%ENGINE_CONTENT%\%%~pE" "%BINARIES%\EngineContent\%%~pE" "%%~nxE" /E /NFL /NDL /NJH /NJS >nul
    if !ERRORLEVEL! GEQ 8 (echo Staging failed for %%E & exit /b 1)
)

echo ProjectEverest.dll built: %OUT%\ProjectEverest.dll
exit /b 0
