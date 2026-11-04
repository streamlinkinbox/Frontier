@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build\LoginSequence.ps1"
set "LoginExit=%ERRORLEVEL%"
echo.
echo Login process exit code: %LoginExit%
pause
exit /b %LoginExit%
