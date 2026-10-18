@echo off
rem Finds Unreal Engine 5.8 for build_data.bat, package.bat and build.bat and sets UE_ROOT in the caller.
rem Usage (from a repo script):  call "%~dp0tools\find_ue.bat" || exit /b 1
rem Lookup order:
rem   1. the UE_ROOT environment variable, if set
rem   2. local_settings.bat in the repo root, if present (git-ignored; e.g.  set "UE_ROOT=D:\UE_5.8")
rem   3. the Epic Games Launcher registry entry HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8 (InstalledDirectory)
rem   4. the launcher default C:\Program Files\Epic Games\UE_5.8
rem Then checks Engine\Build\BatchFiles\Build.bat, RunUAT.bat and Engine\Binaries\Win64\UnrealEditor-Cmd.exe exist.
set "UE_FROM="
if defined UE_ROOT if not defined SSX_LOCAL_SETTINGS set "UE_FROM=the UE_ROOT environment variable"
if not defined UE_ROOT if exist "%~dp0..\local_settings.bat" call "%~dp0..\local_settings.bat"
if not defined UE_FROM if defined UE_ROOT set "UE_FROM=local_settings.bat"
if not defined UE_ROOT (
    for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8" /v InstalledDirectory 2^>nul ^| findstr /i "InstalledDirectory"') do set "UE_ROOT=%%b"
)
if not defined UE_FROM if defined UE_ROOT set "UE_FROM=the Epic Games Launcher registry entry"
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
if not defined UE_FROM set "UE_FROM=the launcher default folder"
if "%UE_ROOT:~-1%"=="\" set "UE_ROOT=%UE_ROOT:~0,-1%"

set "UE_MISSING="
for %%F in ("Engine\Build\BatchFiles\Build.bat" "Engine\Build\BatchFiles\RunUAT.bat" "Engine\Binaries\Win64\UnrealEditor-Cmd.exe") do (
    if not exist "%UE_ROOT%\%%~F" set "UE_MISSING=%%~F"
)
if not defined UE_MISSING (
    echo Unreal Engine 5.8: "%UE_ROOT%" ^(from %UE_FROM%^)
    exit /b 0
)
echo.
echo ERROR: Unreal Engine 5.8 not found.
echo   Tried "%UE_ROOT%" ^(from %UE_FROM%^): "%UE_MISSING%" is missing.
echo   Looked, in this order: the UE_ROOT environment variable, local_settings.bat in the repo root,
echo   the Epic Games Launcher registry entry HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8, and
echo   C:\Program Files\Epic Games\UE_5.8.
echo   Fix: install Unreal Engine 5.8, or point UE_ROOT at it, either for this window:
echo       set "UE_ROOT=D:\path\to\UE_5.8"
echo   or permanently in local_settings.bat next to build.bat (see local_settings.example.bat).
exit /b 1
