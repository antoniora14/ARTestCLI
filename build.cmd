@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"

echo.
if not "%BUILD_EXIT_CODE%"=="0" (
    echo The build or test workflow failed with exit code %BUILD_EXIT_CODE%.
) else (
    echo Build and test workflow completed successfully.
)

echo.
pause
exit /b %BUILD_EXIT_CODE%
