@echo off
rem Build everything in one go: game data from your disc, the Unreal imports and maps, then the packaged game.
rem Usage:  build.bat ["path\to\SLUS-20326.iso"] [refs] [dev] [noremaster]
rem   (no ISO path)  uses SSX_ISO from local_settings.bat, else .\SLUS-20326.iso
rem   refs           also writes the test reference data (see build_data.bat)
rem   dev            packages a Development build (Packaged-Dev\) instead of Shipping (Packaged\)
rem   noremaster     skips the Remastered-look data (derived texture maps, Nanite); that look then lacks them
rem Runs build_data.bat <iso> [refs] import remaster, then package.bat [Development]. Stops at the first failure.
rem Unreal Engine 5.8 is found by tools\find_ue.bat.

setlocal
cd /d "%~dp0"
set "REPO=%~dp0"

set "ISO="
set "REFS="
set "REMASTER=remaster"
set "PKGARG="
set "OUT=%REPO%Packaged"
:args
if "%~1"=="" goto :argsdone
if /i "%~1"=="refs" (
    set "REFS=refs"
) else if /i "%~1"=="noremaster" (
    set "REMASTER="
) else if /i "%~1"=="dev" (
    set "PKGARG=Development"
    set "OUT=%REPO%Packaged-Dev"
) else (
    set "ISO=%~1"
)
shift
goto :args
:argsdone
if not defined ISO if exist "%REPO%local_settings.bat" call "%REPO%local_settings.bat"
if not defined ISO if defined SSX_ISO set "ISO=%SSX_ISO%"
if not defined ISO set "ISO=.\SLUS-20326.iso"

if not exist "%ISO%" (
    echo build.bat: disc image not found: "%ISO%"
    echo Put your SLUS-20326.iso in this folder or pass its path: build.bat "D:\path\to\image.iso"
    exit /b 1
)

python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python was not found on PATH. Install Python 3.10+ ^(python.org, tick "Add python.exe to PATH"^),
    echo then: pip install pycdlib pillow numpy capstone zstandard
    exit /b 1
)
call "%REPO%tools\find_ue.bat" || exit /b 1

for /f %%t in ('powershell -NoProfile -Command "[DateTimeOffset]::Now.ToUnixTimeSeconds()"') do set "T0=%%t"

echo ===== Step 1 of 2: game data, editor build, imports and maps (build_data.bat)
call "%REPO%build_data.bat" "%ISO%" %REFS% import %REMASTER%
if errorlevel 1 (
    echo.
    echo build.bat: FAILED in step 1 ^(build_data.bat^). See the messages above.
    exit /b 1
)

echo ===== Step 2 of 2: package the game (package.bat %PKGARG%)
call "%REPO%package.bat" %PKGARG%
if errorlevel 1 (
    echo.
    echo build.bat: FAILED in step 2 ^(package.bat^). See the messages above.
    exit /b 1
)

for /f %%t in ('powershell -NoProfile -Command "[DateTimeOffset]::Now.ToUnixTimeSeconds()"') do set "T1=%%t"
set /a "SECS=T1-T0"
set /a "MINS=SECS/60"
set /a "REST=SECS%%60"
echo.
echo ===== Done in %MINS% min %REST% s.
echo The game: "%OUT%\Windows\SSXTricky.exe"
echo It contains data converted from your own disc: keep it to yourself, never share or upload it.
exit /b 0
