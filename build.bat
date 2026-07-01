@echo off
setlocal enabledelayedexpansion
title VocalChop Studio - Build

echo ================================================
echo   VocalChop Studio  -  Windows build helper
echo ================================================
echo.

rem --- Check required tools -------------------------------------------------
where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake is not installed ^(or not on PATH^).
    echo         Install it from: https://cmake.org/download/
    echo         During install, choose "Add CMake to the system PATH".
    echo.
    pause
    exit /b 1
)

where git >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Git is not installed ^(or not on PATH^).
    echo         Install it from: https://git-scm.com/download/win
    echo.
    pause
    exit /b 1
)

echo [1/2] Configuring...
echo       ^(The FIRST run downloads JUCE + Signalsmith - this can take a
echo        few minutes and needs internet. Please be patient.^)
echo.
cmake -B build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo.
    echo [ERROR] Configure step failed.
    echo         Copy ALL the red/error text above and send it to Claude.
    echo.
    pause
    exit /b 1
)

echo.
echo [2/2] Building... ^(this can take several minutes^)
echo.
cmake --build build --config Release
if errorlevel 1 (
    echo.
    echo [ERROR] Build step failed.
    echo         Copy ALL the red/error text above and send it to Claude.
    echo.
    pause
    exit /b 1
)

echo.
echo ================================================
echo   DONE!  Your plugin is here:
echo   build\VocalChopStudio_artefacts\Release\VST3\
echo   VocalChop Studio.vst3
echo ================================================
echo.
echo Next: copy that .vst3 folder into
echo   C:\Program Files\Common Files\VST3\
echo then rescan plugins in FL Studio.
echo.
pause
