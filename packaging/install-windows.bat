@echo off
REM ---------------------------------------------------------------------
REM  Slyce installer - Windows
REM
REM  Safety rules, in the order they bite:
REM    1. Never delete on name alone. A bundle counts as ours only if its
REM       moduleinfo.json names our vendor, so a same-named plugin from
REM       anyone else is left strictly alone.
REM    2. Install BEFORE removing. The new bundle is staged under a temp
REM       name and verified; only then is the old one retired. A failed
REM       copy leaves the previous version exactly where it was.
REM    3. Retire, don't destroy. Old bundles are MOVED to a backup folder.
REM    4. Refuse to fight a running DAW, which holds the files open.
REM ---------------------------------------------------------------------
setlocal EnableDelayedExpansion
set "VENDOR=VocalChop Labs"

net session >nul 2>&1
if %errorLevel% neq 0 (
  echo Requesting administrator access...
  powershell -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
  exit /b
)

if "%CommonProgramFiles%"=="" (
  echo ERROR: Windows did not report a Common Files folder. Stopping rather
  echo than guessing where to install.
  pause & exit /b 1
)

set "VST3DIR=%CommonProgramFiles%\VST3"
set "SRC=%~dp0Slyce.vst3"
set "BACKUP=%USERPROFILE%\Slyce-previous-version"
set "LOG=%USERPROFILE%\slyce-install.log"
set "STAGE=%VST3DIR%\Slyce-installing.tmp"

if not exist "%SRC%" (
  echo ERROR: Slyce.vst3 not found next to this script.
  echo Unzip the whole download first, then run this from inside that folder.
  pause & exit /b 1
)

echo Slyce installer > "%LOG%"

REM --- 4. is a DAW running? -------------------------------------------
for %%D in (FL64.exe Ableton.exe Live.exe Cubase.exe reaper.exe ^
            Studio One.exe Bitwig Studio.exe ProTools.exe Nuendo.exe) do (
  tasklist /fi "imagename eq %%D" 2>nul | find /i "%%D" >nul && (
    echo.
    echo WARNING: %%D looks like it is running.
    echo Plug-in files are locked while a DAW is open, so installing now can
    echo half-finish. Close it first.
    echo.
    set /p ANS="Type y to continue anyway, anything else to stop: "
    if /i not "!ANS!"=="y" ( echo Stopped. Nothing was changed. & pause & exit /b 1 )
  )
)

if not exist "%VST3DIR%" mkdir "%VST3DIR%"

REM --- 1. stage the new bundle, then verify it -------------------------
if exist "%STAGE%" rmdir /s /q "%STAGE%"
echo Installing Slyce...
xcopy /e /i /y /q "%SRC%" "%STAGE%" >nul
if %errorLevel% neq 0 (
  echo ERROR: copy failed. Your existing version has NOT been touched.
  if exist "%STAGE%" rmdir /s /q "%STAGE%"
  pause & exit /b 1
)
call :IsOurs "%STAGE%"
if "!OURS!"=="0" (
  echo ERROR: the copied files did not verify. Nothing has been changed.
  rmdir /s /q "%STAGE%"
  pause & exit /b 1
)

REM --- 2b. refuse a name conflict BEFORE touching anything -------------
if exist "%VST3DIR%\Slyce.vst3" (
  call :IsOurs "%VST3DIR%\Slyce.vst3"
  if "!OURS!"=="0" (
    echo.
    echo STOPPED: %VST3DIR%\Slyce.vst3 exists and is not our plugin.
    echo Refusing to overwrite somebody else's file. Nothing has been changed.
    echo Move or rename that file, then run this again.
    rmdir /s /q "%STAGE%"
    pause & exit /b 1
  )
)

REM --- 3. retire our own old bundles by MOVING them --------------------
set "RETIRED=0"
for %%N in ("Slyce.vst3" "VocalChop Studio.vst3" "VocalChopStudio.vst3") do (
  if exist "%VST3DIR%\%%~N" (
    call :IsOurs "%VST3DIR%\%%~N"
    if "!OURS!"=="1" (
      if not exist "%BACKUP%" mkdir "%BACKUP%"
      if exist "%BACKUP%\%%~N" rmdir /s /q "%BACKUP%\%%~N"
      move /y "%VST3DIR%\%%~N" "%BACKUP%\%%~N" >nul 2>&1 && (
        echo   retired  %VST3DIR%\%%~N
        echo   retired  %VST3DIR%\%%~N >> "%LOG%"
        set "RETIRED=1"
      )
    ) else (
      echo   SKIPPED  %VST3DIR%\%%~N - not ours, left alone
      echo   SKIPPED  %VST3DIR%\%%~N >> "%LOG%"
    )
  )
)

REM --- 4. swap the staged copy into place ------------------------------
if exist "%VST3DIR%\Slyce.vst3" rmdir /s /q "%VST3DIR%\Slyce.vst3"
move /y "%STAGE%" "%VST3DIR%\Slyce.vst3" >nul

echo.
echo Done. Slyce is installed to:
echo    %VST3DIR%\Slyce.vst3
if "!RETIRED!"=="1" echo Previous version kept at: %BACKUP%
echo Full log: %LOG%
echo.
echo In FL Studio: Options ^> Manage plugins ^> Find more plugins.
echo Slyce is an INSTRUMENT - add it to the Channel Rack, not the mixer.
echo.
pause
exit /b 0

REM --- helper: does this bundle name our vendor? -----------------------
:IsOurs
set "OURS=0"
for /r "%~1" %%F in (moduleinfo.json) do (
  find /i "%VENDOR%" "%%F" >nul 2>&1 && set "OURS=1"
)
if "!OURS!"=="0" (
  REM Builds predating moduleinfo.json: check the binary itself.
  for /r "%~1" %%F in (*.vst3 *.dll) do (
    find /i "%VENDOR%" "%%F" >nul 2>&1 && set "OURS=1"
  )
)
exit /b
