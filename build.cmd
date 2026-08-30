@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"

echo.
if not "%BUILD_EXIT_CODE%"=="0" (
    echo La compilacion o las pruebas fallaron con codigo %BUILD_EXIT_CODE%.
) else (
    echo Compilacion y pruebas completadas correctamente.
)

echo.
pause
exit /b %BUILD_EXIT_CODE%
