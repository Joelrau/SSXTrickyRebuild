# SSX Tricky Rebuild (Unreal Engine 5.8)

An unofficial, non-commercial fan rebuild of the PS2 snowboarding game SSX Tricky in Unreal Engine 5.8. The game's
behaviour (riding, tricks, AI racers, cameras, menus, HUD, audio) is reimplemented in C++ to match the original as
closely as possible, and checked by automated tests against recordings of the original game.

> [!WARNING]
> **This project was made entirely with AI.**
> All of it was written by AI models: the reverse engineering of the original game (the analysis,
> the notes and the Python reference models), the Unreal C++ port, the data extraction and conversion tools, the
> editor scripts, the tests and this README. The behaviour is checked against recordings of the original game, but
> the code has not been reviewed the way hand-written software would be. **Don't treat it as an example of good
> programming** or copy its patterns into code you care about.

**No game data is in this repository.** You need your own, legally owned copy of SSX Tricky (NTSC-U, game id
SLUS-20326) as a disc image. The tools read everything from that image and write it to git-ignored folders on your
machine. Never commit or share those folders, or the packaged game made from them.


## Legal

- This is an **unofficial fan project**. It is not affiliated with, endorsed by or sponsored by Electronic Arts Inc.
- SSX, SSX Tricky and the related names, logos, characters, artwork, music and other content are trademarks and/or
  copyrights of Electronic Arts Inc. and their respective owners.
- **No game assets are included**: no textures, models, audio, video, text or executable files from
  the game. The tools convert them on your own machine from your own, legally owned copy of the game.
- The MIT license in `LICENSE` covers only the code, tools and scripts in this repository, not the game or its data (see `NOTICE`).

## What you need

- **Windows 10 or 11** (64-bit), about 40 GB of free disk space.
- **Unreal Engine 5.8**. The scripts find it themselves (`tools\find_ue.bat`), in this order:
  1. the `UE_ROOT` environment variable (for example `set "UE_ROOT=D:\Epic\UE_5.8"`);
  2. `local_settings.bat` in the repo folder, if you made one: copy `local_settings.example.bat` to
     `local_settings.bat` and put your engine folder in it (the file is git-ignored);
  3. the Epic Games Launcher's registry entry for 5.8 (`HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8`);
  4. the launcher's default folder `C:\Program Files\Epic Games\UE_5.8`.
  The folder must contain `Engine\Binaries\Win64\UnrealEditor-Cmd.exe`; otherwise the scripts stop and say
  where they looked. `build_data.bat` without `import` does not need the engine.
  The command lines below write the engine folder as `%UE_ROOT%`.
- **Visual Studio 2022 (17.14 or newer) or Visual Studio 2026** with the **"Game development with C++"** workload
  and a Windows 10/11 SDK (10.0.22621 is the one UE 5.8 prefers).
- **Python 3.10 or newer** on your `PATH`, with these packages:
  ```
  pip install pycdlib pillow numpy capstone zstandard
  ```
- **A short folder path** for the repository (for example `C:\SSXTrickyRebuild`): Unreal's build writes
  intermediate files with long names and fails in deeply nested folders (Windows' 260-character path limit).
- **The disc image** of SSX Tricky (NTSC-U, SLUS-20326), for example ripped from your own disc.

## Quick start: build.bat

One command does everything: it converts the game data from your disc, builds the editor, imports the tracks, riders
and audio into Unreal, and packages a playable game.

```
build.bat "C:\path\to\your image.iso"
```

- Leave out the path if the image is in the repo folder and named `SLUS-20326.iso`, or if `local_settings.bat`
  sets `SSX_ISO` (see step 1).
- Add `dev` for a Development build with logging (`Packaged-Dev\`) instead of the normal Shipping build.
- Add `refs` to also write the test reference data (see step 5).

It stops at the first error and says which step failed. At the end it prints the time taken and where the game is:
`Packaged\Windows\SSXTricky.exe` (or `Packaged-Dev\Windows\SSXTricky.exe` with `dev`). The full run takes a long
time (the import and the cook are the slow parts); plan for a couple of hours on the first run.

The sections below are the same work split into separate steps, for when you only need part of it.

## Step by step

### 1. Put the disc image in place

Do one of these (image files, `*.iso`, are git-ignored):

- copy your disc image into the repo folder and rename it to `SLUS-20326.iso`;
- keep it anywhere and set its path once in `local_settings.bat` (copy `local_settings.example.bat`):
  `set "SSX_ISO=D:\path\to\your image.iso"`;
- or pass its path to the scripts each time.

### 2. Convert the game data: `build_data.bat`

```
build_data.bat                                  uses SSX_ISO from local_settings.bat, else .\SLUS-20326.iso
build_data.bat "C:\path\to\your image.iso"      any other path
build_data.bat "C:\path\to\your image.iso" refs      also writes the test reference data (step 5)
build_data.bat "C:\path\to\your image.iso" import    also builds the editor and imports everything into Unreal
```

What it does, in order:

1. Extracts the disc (`extracted\`), unpacks the BIG archives (`unpacked\`) and converts textures, tracks, riders,
   audio, speech, movies and fonts (`converted\`).
2. Exports the data the game reads at run time (`Unreal\SSXTrickyRebuild\SimData\` and parts of `converted\`).
3. With `import`: builds the editor and runs the Unreal import scripts (riders, audio, one map per track, the
   front-end map). This is needed before you can play in the editor or package the game.

Steps 1 and 2 take roughly half an hour; `import` adds about as much again. Run it again whenever the tools change.

### 3. Compile the C++ code

First create the Visual Studio project files, either way:

- Right-click `Unreal\SSXTrickyRebuild\SSXTrickyRebuild.uproject` and choose **Generate Visual Studio project files**, or
- run:
  ```
  "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="%CD%\Unreal\SSXTrickyRebuild\SSXTrickyRebuild.uproject" -game
  ```

Then build the editor target **SSXTrickyRebuildEditor**, either way:

- **Visual Studio:** open the solution in `Unreal\SSXTrickyRebuild\`, pick **Development Editor** and **Win64** in
  the toolbar, then build the `SSXTrickyRebuild` project (Build > Build Solution also works).
- **Command line:**
  ```
  "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" SSXTrickyRebuildEditor Win64 Development -Project="%CD%\Unreal\SSXTrickyRebuild\SSXTrickyRebuild.uproject" -WaitMutex
  ```

(`build_data.bat ... import` and `build.bat` already do this build for you.)

### 4. Open the editor

Double-click `Unreal\SSXTrickyRebuild\SSXTrickyRebuild.uproject` (or start it from Visual Studio with F5). The editor
opens on the front-end map (`/Game/Maps/FrontEnd`); press **Play** to start the game from the main menu. A gamepad
works best (Xbox pads, PS4 DualShock 4 and PS5 DualSense over USB or Bluetooth).

### 5. Run the tests

The tests are Unreal automation tests named `SSX.*`. They compare the C++ port with Python reference models and
recordings of the original game. Run them from a command prompt (close the editor first):

```
run_tests.bat                run all SSX.* tests
run_tests.bat SSX.Replay    only the tests whose name starts with SSX.Replay
```

It prints how many tests passed, were skipped and failed (with the skipped and failed test names) and exits with 1
if a test failed. The log is `Unreal\SSXTrickyRebuild\Saved\Logs\SSXTests.log`. A test is **skipped** when the data it
compares against is missing (reference data from `build_data.bat ... refs`, converted game data, PCSX2 recordings from
`SSX_TRACES`, or your replay): it is reported but not counted as passed. Set `SSX_ORIGINAL_REPLAY` (environment or
`local_settings.bat`) to your REP0001 memory-card replay file, `BASLUS-20326-REP0001`, so the replay tests run.

or in the editor: **Tools > Test Automation**, filter on `SSX`, tick the tests and press **Start Tests**.

Most tests need the reference data from `build_data.bat ... refs`. Some reference data comes from recordings of the
original game made with the PCSX2 emulator; point the `SSX_TRACES` environment variable at a folder of such
recordings to include them (`set SSX_TRACES=C:\path\to\traces` before `build_data.bat ... refs`). Without recordings
the scripts that need them print `skipped (no traces)` and the matching tests are reported as skipped instead of
failing; the game data itself never needs recordings.

### 6. Package the game: `package.bat`

```
package.bat                 Shipping build     -> Packaged\Windows\SSXTricky.exe
package.bat Development     logging build      -> Packaged-Dev\Windows\SSXTricky.exe
```

Run `build_data.bat ... import` first (the cook needs the imported maps). Double-click `SSXTricky.exe` to play.
**The package contains data converted from your own disc: keep it to yourself, never share or upload it.**

## Remastered look

An optional modern presentation on top of the faithful game: lit materials, a sun and sky light, Lumen global
illumination and reflections. Only the picture changes. Gameplay, timing, audio, HUD, menus and replays are the same as
in the Classic look, which stays the default and renders exactly as before.

- **Switch:** Options → VIDEO → LOOK (Classic / Remastered), or the console variable `ssx.Render.Look 0|1`. It
  switches live and is saved in `GameUserSettings.ini` (never on the memory card).
- **Extra data step:** `build_data.bat ... remaster` (with `import`) runs `tools\make_remaster_maps.py` (2x/4x upscaled
  colour, normal and detail-mask maps made from your disc's textures into `converted\remaster`) and imports them into
  `Content/RemasterData`. Both are game data and git-ignored. To redo only this step: `python tools\make_remaster_maps.py`,
  then the editor script `Scripts\import_remaster.py`. Without the maps the Remastered look still works, with flat detail.
- **Materials:** `Scripts\make_remaster_materials.py` creates `Content/Remastered` (no game data; committed).
- **Requirements:** a GPU with Shader Model 6 for Lumen (software ray tracing; no RT hardware needed). The project builds
  mesh distance fields for this, which makes the first editor load and the first cook slower.

## Project layout

| Folder / file | What it is |
|---|---|
| `build.bat`, `build_data.bat`, `package.bat` | The build scripts described above |
| `tools\` | Python tools: disc extraction, format converters, data exporters, test-reference writers |
| `tools\lib\` | Shared Python code used by the tools: executable table readers, `traces.py` (optional recordings) |
| `tools\lib\models\` | Python reference models of the game's behaviour (the tests compare the C++ port with them) |
| `tools\lib\scripts\` | Python decoders and validators the models and exporters use (animation banks, camera files, ...) |
| `Unreal\SSXTrickyRebuild\Source\SSXTrickyRebuild\` | The game module (C++) |
| `Unreal\SSXTrickyRebuild\Source\SSXTrickyRebuildTests\` | The test module (editor only; never in the packaged game) |
| `Unreal\SSXTrickyRebuild\Scripts\` | Unreal editor Python scripts (imports, map and material builders) |
| `Unreal\SSXTrickyRebuild\Config\` | Project settings |
| `extracted\`, `unpacked\`, `converted\` | Generated from your disc (git-ignored) |
| `Unreal\SSXTrickyRebuild\SimData\` | Generated run-time data (git-ignored) |
| `Packaged\`, `Packaged-Dev\` | The packaged game (git-ignored, about 8.5 GB Shipping, 9 GB Development) |

Why two kinds of Python scripts: everything in `tools\` is plain Python, run with your normal Python
(`python tools\<script>.py`); it extracts, converts and exports data and never needs Unreal. The scripts in
`Unreal\SSXTrickyRebuild\Scripts\` are Unreal editor scripts: they `import unreal`, so they only run inside the editor
(`UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<script>.py`, which `build_data.bat ... import` does for
you). That's why they live in separate folders.

## Troubleshooting

- **The build fails with a Live Coding message** ("Unable to build while Live Coding is active"): close the Unreal
  editor and build again.
- **Missing data** (log lines like `... not found (run tools/export_....py)`, empty menus, black tracks): run
  `build_data.bat` again, with `import` if maps or assets are missing.
- **`SSXTricky.exe` in `Unreal\SSXTrickyRebuild\Binaries\Win64` does not start or shows nothing:** that exe has no
  cooked content. Use the packaged one, `Packaged\Windows\SSXTricky.exe`, made by `package.bat`.
- **"ISO not found"**: check the path, and put it in quotes if it has spaces.
- **Unreal Engine 5.8 not found**: the message lists where the scripts looked. Set `UE_ROOT` for the window, or
  copy `local_settings.example.bat` to `local_settings.bat` and set `UE_ROOT` there (see "What you need").
- **Python was not found on PATH**: install Python 3.10+ with "Add python.exe to PATH" ticked, open a new
  command prompt and run the `pip install` line from "What you need".
- **Python errors about a missing module**: run the `pip install` line from "What you need".

## Developer notes

**How it runs.** The rider runs on `FRiderSim` (a fixed 60 Hz step) over `FPatchWorld`, which owns all rider
collision (terrain patches, rails, prop instances); the track meshes have no Unreal collision. `ASSXRaceManager`
runs races with AI racers; the front end, HUD and overlays follow the original's state machines. A gamepad goes
through a virtual PS2 pad and the game's own button mapping (`BTNMAP0/1`, layout 1 by default); DualSense and
DualShock 4 are read natively (`Input/SSXDualSense`). Keyboard bindings exist for testing.

**Where the data comes from.** Everything is read from your disc image. A few tables (surface physics, HUD layouts,
sprite sheets, animation lookup tables, per-track presentation, rider relationships) are not stored as data on the
disc: the game's executable builds them from constants in its code when it starts. `tools\lib\ee_exec.py` runs those
start-up routines from your own copy of the executable and `tools\lib\exe_tables.py` reads the finished tables.
Recordings of the original game (PCSX2 RAM snapshots and per-frame traces) are only used for test reference
data, read from the folder in `SSX_TRACES`; no game data needs them. Every tool imports `tools\libpath.py`, which
puts `tools\lib` on the Python path. The build needs only this repository and your disc image.

**Track glTF.** Each `converted\tracks\<track>\<track>.gltf` holds `<track>_terrain` (patches with lightmap UVs),
`<track>_showoff_terrain` (the ShowOff patches, drawn only in game modes 3/5), `<track>_props` (one node per
placed instance, with baked vertex lighting) and the start markers; `<track>_lightmap.png` is the lightmap atlas.
Game space is Z-up centimetres, glTF is Y-up metres.

**Colour.** The scene colour is in PS2 colour space (GS value / 255): world materials write it without an sRGB decode
and `PP_SSXAuthoredColour` shows it unchanged, so sprite blends match the GS alpha blends. The world is unlit (baked
lightmaps, probe-lit riders): no Lumen, auto exposure or bloom.

**What to rerun after a change.**

- Track geometry or world look (`pbd2gltf`, materials, lighting): `tools\convert_tracks.py` ->
  `tools\export_world_lighting.py` -> `Scripts\make_world_materials.py` -> `Scripts\build_track_map.py` for each of
  the 11 tracks -> `Scripts\build_frontend_map.py` (`build_data.bat ... import` does all of it).
- Run-time data (patches, rails, props, races, terrain LOD, fog, prop animation): the matching
  `tools\export_*.py`, then its `tools\make_*_reference.py`. No map rebuild.
- Material scripts only (`Scripts\make_world_materials.py`, `Scripts\make_particle_materials.py`): run the two
  scripts; the materials are rebuilt in place. No map rebuild.
- Audio: `tools\convert_audio.py` -> `Scripts\import_audio.py`. Riders: `tools\convert_characters.py` ->
  `Scripts\import_characters.py` (the face morph targets `ssx_face_00..22` need the re-import).

**Packaging details.** The packaged target is `SSXTricky` (Shipping by default, `Config\DefaultGame.ini`). Shipping
leaves out the tests, the debug command-line switches (`-SSXAutoTest`, `-SSXAutoShot`, ...), the diagnostic console
variables and the log output; Development keeps them. The cook includes the FrontEnd map, the 11 track maps and the
folders loaded by name at run time. The data the game reads from disk is staged as loose files (only the files the
game module opens, listed in `SSXTrickyRebuild.Build.cs`): the `SimData\` track/rider/camera tables (not the test
references) to `<package>\SSXTrickyRebuild\SimData`, and the needed parts of `converted\` (audio, front end, textures,
HUD, rider chrome/specular maps; not the glTF import inputs), `extracted\DATA\` and `unpacked\TUTORIAL` to
`<package>\SSXTrickyRebuild\GameData\` (`-SSXDataRoot=<dir>` overrides the data folder). Audio (5.4 GB) and the
front-end movies (1.8 GB) are most of the ~8.5 GB package. Shipping is staged without the .pdb (`package.bat` passes
`-nodebuginfo`; `IncludeDebugFiles` in `DefaultGame.ini` only affects the editor's Package Project menu). The exe icon comes from
the game's loading-screen logo (`tools\make_icon.py`, run by `build_data.bat`).

**Tests.** The test module `Source\SSXTrickyRebuildTests` (same folder layout as the game module) holds the `SSX.*`
automation tests and the replay harness (`-SSXReplay=`). Only the editor target builds it. The replay tests can use
a replay saved by the original game: set `SSX_ORIGINAL_REPLAY` to a `BASLUS-20326-REP0001` file copied from a
memory card; without it those checks are skipped.
