@echo off
rem Runs the SSX automation tests headless and prints a summary: passed, skipped (missing data), failed.
rem Usage:  run_tests.bat [filter]        filter defaults to SSX (e.g. run_tests.bat SSX.Replay)
rem Close the Unreal editor first. Log: Unreal\SSXTrickyRebuild\Saved\Logs\SSXTests.log
rem SSX_ORIGINAL_REPLAY (your REP0001 memory-card replay file) is read from the environment or local_settings.bat;
rem tests that need data you have not produced are reported as skipped, not passed.
rem Exit code is 1 if a test failed. Unreal Engine 5.8 is found by tools\find_ue.bat.

setlocal
cd /d "%~dp0"
set "REPO=%~dp0"
set "FILTER=%~1"
if not defined FILTER set "FILTER=SSX"

call "%REPO%tools\find_ue.bat" || exit /b 1
set "KEEP_UE_ROOT=%UE_ROOT%"
if not defined SSX_ORIGINAL_REPLAY if exist "%REPO%local_settings.bat" call "%REPO%local_settings.bat"
set "UE_ROOT=%KEEP_UE_ROOT%"
if defined SSX_ORIGINAL_REPLAY (echo SSX_ORIGINAL_REPLAY: %SSX_ORIGINAL_REPLAY%) else echo SSX_ORIGINAL_REPLAY not set: the replay tests are skipped.

set "UPROJECT=%REPO%Unreal\SSXTrickyRebuild\SSXTrickyRebuild.uproject"
set "LOG=%REPO%Unreal\SSXTrickyRebuild\Saved\Logs\SSXTests.log"
if exist "%LOG%" del "%LOG%"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%UPROJECT%" -nullrhi -DisablePlugins=FSR -unattended -nosplash -ExecCmds="Automation RunTests %FILTER%; Quit" -log=SSXTests.log
if not exist "%LOG%" (
    echo run_tests.bat: no log written: "%LOG%"
    exit /b 1
)
echo.
python "%REPO%tools\test_summary.py" "%LOG%"
exit /b %ERRORLEVEL%
