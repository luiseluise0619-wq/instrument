@echo off
REM ---------------------------------------------------------------------
REM  Slyce installer - Windows
REM
REM  Copying the folder by hand MERGES it into whatever is already there,
REM  so files from an older build survive inside the new bundle. This
REM  removes the old one first, then copies, so what ends up installed is
REM  exactly what shipped.
REM
REM  It also removes "VocalChop Studio.vst3" - the plugin was renamed to
REM  Slyce in 3.0, and anyone who installed an earlier build still has the
REM  old bundle sitting next to the new one, showing up twice in the DAW.
REM ---------------------------------------------------------------------
setlocal

REM Writing to Program Files needs admin; re-launch elevated if we aren't.
net session >nul 2>&1
if %errorLevel% neq 0 (
  echo Requesting administrator access...
  powershell -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
  exit /b
)

set "VST3DIR=%CommonProgramFiles%\VST3"
set "SRC=%~dp0Slyce.vst3"

if not exist "%SRC%" (
  echo ERROR: Slyce.vst3 not found next to this script.
  echo Unzip the whole download first, then run this from inside that folder.
  pause
  exit /b 1
)

if not exist "%VST3DIR%" mkdir "%VST3DIR%"

echo Removing any previous version...
if exist "%VST3DIR%\Slyce.vst3"            rmdir /s /q "%VST3DIR%\Slyce.vst3"
if exist "%VST3DIR%\VocalChop Studio.vst3" rmdir /s /q "%VST3DIR%\VocalChop Studio.vst3"
if exist "%VST3DIR%\VocalChopStudio.vst3"  rmdir /s /q "%VST3DIR%\VocalChopStudio.vst3"

echo Installing Slyce...
xcopy /e /i /y /q "%SRC%" "%VST3DIR%\Slyce.vst3" >nul
if %errorLevel% neq 0 (
  echo ERROR: copy failed. Close your DAW and run this again.
  pause
  exit /b 1
)

echo.
echo Done. Slyce is installed to:
echo   %VST3DIR%\Slyce.vst3
echo.
echo In FL Studio: Options ^> Manage plugins ^> Find more plugins.
echo Slyce is an INSTRUMENT - add it to the Channel Rack, not the mixer.
echo.
pause
