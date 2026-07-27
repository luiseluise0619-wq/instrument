@echo off
REM ---------------------------------------------------------------------
REM  Slyce installer - Windows
REM
REM  Removes ONLY Slyce's own bundles, by exact name, and nothing else in
REM  your VST3 folder. The list is printed before anything is touched.
REM
REM  Copying the folder by hand MERGES it into whatever is already there,
REM  so files from an older build survive inside the new bundle. This
REM  deletes first, then copies, so what ends up installed is exactly what
REM  shipped. It also clears "VocalChop Studio.vst3" - the plugin was
REM  renamed to Slyce in 3.0, and the old bundle otherwise stays behind and
REM  shows up as a second plugin in the DAW.
REM ---------------------------------------------------------------------
setlocal EnableDelayedExpansion

REM Writing to Program Files needs admin; re-launch elevated if we aren't.
net session >nul 2>&1
if %errorLevel% neq 0 (
  echo Requesting administrator access...
  powershell -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
  exit /b
)

if "%CommonProgramFiles%"=="" (
  echo ERROR: Windows did not report a Common Files folder. Stopping rather
  echo than guessing where to install.
  pause
  exit /b 1
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

set "FOUND="
for %%N in ("Slyce.vst3" "VocalChop Studio.vst3" "VocalChopStudio.vst3") do (
  if exist "%VST3DIR%\%%~N" (
    if not defined FOUND echo These previous Slyce files will be replaced:
    set "FOUND=1"
    echo    %VST3DIR%\%%~N
  )
)
if defined FOUND (
  echo Nothing else in your VST3 folder is touched.
  echo.
  for %%N in ("Slyce.vst3" "VocalChop Studio.vst3" "VocalChopStudio.vst3") do (
    if exist "%VST3DIR%\%%~N" rmdir /s /q "%VST3DIR%\%%~N"
  )
) else (
  echo No previous version found. Installing fresh.
)

echo Installing Slyce...
xcopy /e /i /y /q "%SRC%" "%VST3DIR%\Slyce.vst3" >nul
if %errorLevel% neq 0 (
  echo ERROR: copy failed. Close your DAW and run this again.
  pause
  exit /b 1
)

echo.
echo Done. Slyce is installed to:
echo    %VST3DIR%\Slyce.vst3
echo.
echo In FL Studio: Options ^> Manage plugins ^> Find more plugins.
echo Slyce is an INSTRUMENT - add it to the Channel Rack, not the mixer.
echo.
pause
