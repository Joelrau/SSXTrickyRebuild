@echo off
rem Build, cook and package the Windows game (needs the game data from build_data.bat ... import first).
rem Usage:  package.bat [Development | Classic]
rem   default      Shipping build   -> Packaged\Windows\SSXTricky.exe
rem   Development  logging build    -> Packaged-Dev\Windows\SSXTricky.exe (log in Packaged-Dev\Windows\SSXTrickyRebuild\Saved\Logs)
rem   Classic      no Remastered look or its PC additions -> Packaged-Classic\Windows\SSXTrickyClassic.exe
rem The package holds data converted from your own disc: keep it to yourself, never share or upload it.
rem Unreal Engine 5.8 is found by tools\find_ue.bat.

setlocal
cd /d "%~dp0"
call "%~dp0tools\find_ue.bat" || exit /b 1

set "CONFIG=Shipping"
set "TARGET=SSXTricky"
set "CFG=%~dp0Unreal\SSXTrickyRebuild\Config"
set "UPROJECT=%~dp0Unreal\SSXTrickyRebuild\SSXTrickyRebuild.uproject"
set "KEEP=%~dp0Unreal\SSXTrickyRebuild\Intermediate\ClassicConfig"
set "RECEIPT=%~dp0Unreal\SSXTrickyRebuild\Binaries\Win64\SSXTrickyRebuildEditor.target"
set "OUT=%~dp0Packaged"
rem Shipping stages no .pdb: UAT copies debug files unless -nodebuginfo (IncludeDebugFiles in DefaultGame.ini only
rem applies to the editor's Package Project menu).
set "DEBUGINFO=-nodebuginfo"
if /i "%~1"=="Development" (
    set "CONFIG=Development"
    set "OUT=%~dp0Packaged-Dev"
    set "DEBUGINFO="
)
if /i "%~1"=="Classic" (
    set "TARGET=SSXTrickyClassic"
    set "OUT=%~dp0Packaged-Classic"
)

rem A Classic package that was cut short left its settings in the config files.
call :RestoreConfig || exit /b 1
rem The cooker mounts the plugins listed in the editor's receipt: with no FSR there, the game crashes at launch on its missing shaders.
if /i not "%~1"=="Classic" (
    findstr /c:"FSR" "%RECEIPT%" >nul 2>nul || call :RebuildEditor
    findstr /c:"FSR" "%RECEIPT%" >nul 2>nul || (
        echo The editor build has no FSR plugin: close the editor and run package.bat again.
        exit /b 1
    )
)
if /i "%~1"=="Classic" (
    if not exist "%KEEP%" mkdir "%KEEP%"
    copy /y "%CFG%\DefaultEngine.ini" "%KEEP%\DefaultEngine.ini" >nul
    copy /y "%CFG%\DefaultGame.ini" "%KEEP%\DefaultGame.ini" >nul
    copy /y "%UPROJECT%" "%KEEP%\SSXTrickyRebuild.uproject" >nul
    type "%~dp0tools\classic_only\DefaultEngine.append.ini" >> "%CFG%\DefaultEngine.ini"
    type "%~dp0tools\classic_only\DefaultGame.append.ini" >> "%CFG%\DefaultGame.ini"
    rem A target cannot disable a plugin with an installed engine: the FSR plugin goes off in the project file.
    powershell -NoProfile -Command "$t = [IO.File]::ReadAllText($env:UPROJECT) -replace '(\"Name\": \"FSR\",\s*\"Enabled\": )true', '$1false'; [IO.File]::WriteAllText($env:UPROJECT, $t)"
)

rem UBT keeps the expanded RuntimeDependencies wildcards in this cache, which would skip new data files.
del /q "%~dp0Unreal\SSXTrickyRebuild\Intermediate\Build\Win64\x64\%TARGET%\%CONFIG%\Makefile.bin" 2>nul

call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
    -project="%UPROJECT%" ^
    -platform=Win64 -clientconfig=%CONFIG% -target=%TARGET% ^
    -build -cook -stage -pak -stagingdirectory="%OUT%" %DEBUGINFO% ^
    -unattended -utf8output
set "RESULT=%errorlevel%"
call :RestoreConfig
if errorlevel 1 echo The editor was not rebuilt with its normal plugins: the next package.bat will try again.
if not "%RESULT%"=="0" (
    echo Packaging failed.
    exit /b 1
)
echo.
echo Done: "%OUT%\Windows\%TARGET%.exe"
exit /b 0

:RestoreConfig
if exist "%KEEP%\DefaultEngine.ini" move /y "%KEEP%\DefaultEngine.ini" "%CFG%\DefaultEngine.ini" >nul
if exist "%KEEP%\DefaultGame.ini" move /y "%KEEP%\DefaultGame.ini" "%CFG%\DefaultGame.ini" >nul
if not exist "%KEEP%\SSXTrickyRebuild.uproject" exit /b 0
move /y "%KEEP%\SSXTrickyRebuild.uproject" "%UPROJECT%" >nul

:RebuildEditor
rem The Classic package built the editor without FSR. The project file needs a new date: UBT misses a restored one.
powershell -NoProfile -Command "(Get-Item $env:UPROJECT).LastWriteTime = Get-Date"
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" SSXTrickyRebuildEditor Win64 Development -Project="%UPROJECT%" -WaitMutex
exit /b %errorlevel%
