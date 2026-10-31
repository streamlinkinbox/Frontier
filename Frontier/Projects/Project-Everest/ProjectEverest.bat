@echo off
REM ============================================================================
REM  ProjectEverest.bat - open Project Everest in Frontier.
REM ============================================================================
setlocal

set "PROJECT=%~dp0ProjectEverest.frontier"

if defined FRONTIER_EXE (
    set "EXE=%FRONTIER_EXE%"
) else if exist "%~dp0..\..\Binaries\Frontier.exe" (
    set "EXE=%~dp0..\..\Binaries\Frontier.exe"
) else (
    set "EXE=Frontier.exe"
)

"%EXE%" --project "%PROJECT%" %*
exit /b %ERRORLEVEL%
