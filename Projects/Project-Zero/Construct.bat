@echo off
rem ============================================================================================================================================
rem Project-Zero/Construct.bat - One-click MSVC build (and run) for the Project-Zero Vulkan showcase .exe
rem ============================================================================================================================================
rem   Construct.bat                    builds Release and opens the live window
rem   Construct.bat -Run               same as above (explicit)
rem   Construct.bat -Rebuild -Run      full rebuild, then run
rem   Construct.bat -Configuration Debug
rem                                    builds Debug without running
rem Forwards to Build/ToolchainSequence.ps1, which compiles the Vulkan + Slang product stack (see INTEGRATION.md).
rem The headless CPU reference (no GPU needed) is built by Build/Construct.ps1 instead.
setlocal
if "%~1"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build\ToolchainSequence.ps1" -Run
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build\ToolchainSequence.ps1" %*
)
exit /b %errorlevel%
