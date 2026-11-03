@echo off
rem Build, cook and package the Windows game (needs the game data from build_data.bat ... import first).
rem Usage:  package.bat [Development]
rem   default      Shipping build   -> Packaged\Windows\SSXTricky.exe
rem   Development  logging build    -> Packaged-Dev\Windows\SSXTricky.exe (log in Packaged-Dev\Windows\SSXTrickyRebuild\Saved\Logs)
rem The package holds data converted from your own disc: keep it to yourself, never share or upload it.
rem Unreal Engine 5.8 is found by tools\find_ue.bat.

setlocal
cd /d "%~dp0"
call "%~dp0tools\find_ue.bat" || exit /b 1

set "CONFIG=Shipping"
set "OUT=%~dp0Packaged"
rem Shipping stages no .pdb: UAT copies debug files unless -nodebuginfo (IncludeDebugFiles in DefaultGame.ini only
rem applies to the editor's Package Project menu).
set "DEBUGINFO=-nodebuginfo"
if /i "%~1"=="Development" (
    set "CONFIG=Development"
    set "OUT=%~dp0Packaged-Dev"
    set "DEBUGINFO="
)

rem UBT keeps the expanded RuntimeDependencies wildcards in this cache, which would skip new data files.
del /q "%~dp0Unreal\SSXTrickyRebuild\Intermediate\Build\Win64\x64\SSXTricky\%CONFIG%\Makefile.bin" 2>nul

call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
    -project="%~dp0Unreal\SSXTrickyRebuild\SSXTrickyRebuild.uproject" ^
    -platform=Win64 -clientconfig=%CONFIG% -target=SSXTricky ^
    -build -cook -stage -pak -stagingdirectory="%OUT%" %DEBUGINFO% ^
    -unattended -utf8output
if errorlevel 1 (
    echo Packaging failed.
    exit /b 1
)
echo.
echo Done: "%OUT%\Windows\SSXTricky.exe"
