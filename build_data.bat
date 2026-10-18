@echo off
rem Extract and convert all game data from the user's SSX Tricky disc image.
rem Usage:  build_data.bat ["path\to\SLUS-20326.iso"] [refs] [import]
rem   refs    also writes the test reference data (tools\make_*_reference.py, make_probe_cases.py, ground_reference.py).
rem   import  also builds the editor and runs the Unreal import / map scripts.
rem Needs Python 3.10+ (pip install pycdlib pillow numpy). Test references that compare against recordings of the
rem original game read them from SSX_TRACES (optional; without it those scripts print "skipped (no traces)").
rem Unreal Engine 5.8 (only for "import") is found by tools\find_ue.bat.

setlocal
cd /d "%~dp0"
set "REPO=%~dp0"

set "ISO="
:args
if "%~1"=="" goto :argsdone
if /i "%~1"=="import" (
    set "DOIMPORT=1"
) else if /i "%~1"=="refs" (
    set "DOREFS=1"
) else (
    set "ISO=%~1"
)
shift
goto :args
:argsdone
if not defined ISO if exist "%REPO%local_settings.bat" call "%REPO%local_settings.bat"
if not defined ISO if defined SSX_ISO set "ISO=%SSX_ISO%"
if not defined ISO set "ISO=.\SLUS-20326.iso"

set "PROJ=%REPO%Unreal\SSXTrickyRebuild\SSXTrickyRebuild.uproject"

if not exist "%ISO%" (
    echo ISO not found: "%ISO%"
    exit /b 1
)
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python was not found on PATH. Install Python 3.10+ ^(python.org, tick "Add python.exe to PATH"^),
    echo then: pip install pycdlib pillow numpy capstone zstandard
    exit /b 1
)
if defined DOIMPORT (
    call "%REPO%tools\find_ue.bat" || exit /b 1
)

echo === 1. Extract and convert
call :py tools\extract_iso.py "%ISO%" extracted || goto :fail
for %%A in (MODELS CHAR TEXTURES TUTORIAL) do (
    call :py tools\eabig.py extracted\DATA\%%A unpacked\%%A || goto :fail
)
call :py tools\sshtex.py unpacked converted\textures || goto :fail
call :py tools\sshtex.py extracted\DATA\TEXTURES converted\textures\TEXTURES || goto :fail
call :py tools\convert_tracks.py || goto :fail
call :py tools\convert_characters.py || goto :fail
call :py tools\convert_audio.py || goto :fail
call :py tools\convert_speech.py || goto :fail
call :py tools\mpc.py extracted\DATA\VIDEO converted\frontend\video || goto :fail
call :py tools\sfn.py extracted\DATA\FONTS converted\frontend\fonts || goto :fail

echo === 2. Run-time data (SimData) and front-end data
for %%S in (export_sim_data export_rails export_instance_collision export_race_data export_rider_profiles ^
            export_anim_table export_anim_clips export_cameras export_world_lighting export_terrain_lod ^
            export_effects export_fog_volumes export_prop_anim export_prop_sounds export_heats_data ^
            export_intro_dialogue export_hud_data export_trick_data export_fe_tables export_fe_sprites ^
            export_fe_cameras export_fe_riders export_progression_data export_loading_screen ^
            export_hud_anim_ref make_icon) do (
    call :py tools\%%S.py || goto :fail
)

if not defined DOREFS goto :import
echo === Test reference data
for %%F in (tools\make_*_reference.py tools\make_probe_cases.py tools\ground_reference.py) do (
    call :py %%F || goto :fail
)

:import
if not defined DOIMPORT goto :done

echo === 3. Unreal build and imports
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" SSXTrickyRebuildEditor Win64 Development -Project="%PROJ%" -WaitMutex || goto :fail
call :ue import_characters.py || goto :fail
call :ue import_audio.py || goto :fail
for %%T in (Alaska:alaska Aloha:aloha Elysium:elysium Gari:gari Megaple:megaple Merquer:merquer Mesa:mesa ^
            Pipe:pipe Snow:snow Trick:trick Untrack:untrack) do (
    for /f "tokens=1,2 delims=:" %%a in ("%%T") do (
        call :ue build_track_map.py %%a %REPO%converted\tracks\%%b\%%b.gltf || goto :fail
    )
)
call :ue build_frontend_map.py || goto :fail

:done
echo === Done.
exit /b 0

:fail
echo === FAILED (see the output above).
exit /b 1

:py
echo --- python %*
python %*
exit /b %errorlevel%

:ue
rem Forward slashes: Unreal's -script argument parsing treats backslashes as escapes (\t in \tracks).
set "UESCRIPT=%REPO%Unreal\SSXTrickyRebuild\Scripts\%*"
set "UESCRIPT=%UESCRIPT:\=/%"
echo --- editor script %*
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%UESCRIPT%" -unattended -NullRHI
exit /b %errorlevel%
