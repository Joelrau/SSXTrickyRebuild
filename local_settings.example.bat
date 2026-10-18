@echo off
rem Copy this file to local_settings.bat (git-ignored) and set the paths for your PC.
rem build_data.bat, package.bat and build.bat read it when the UE_ROOT environment variable is not set.
rem The folder must contain Engine\Binaries\Win64\UnrealEditor-Cmd.exe.
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
rem Disc image used when build.bat / build_data.bat get no ISO path.
set "SSX_ISO=D:\path\to\SLUS-20326.iso"
set "SSX_LOCAL_SETTINGS=1"
rem Optional: folder with PCSX2 recordings for the test reference data (refs step).
rem set "SSX_TRACES=D:\path\to\traces"
rem Optional: your REP0001 replay from the PS2 memory card (BASLUS-20326-REP0001 file) for the replay tests in run_tests.bat.
rem set "SSX_ORIGINAL_REPLAY=D:\path\to\BASLUS-20326-REP0001"
