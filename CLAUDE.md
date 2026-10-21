# Rules for AI agents working on this repo

SSX Tricky Rebuild: an unofficial PC rebuild of SSX Tricky (PS2, NTSC-U, game id SLUS-20326) in Unreal Engine 5.8.
The game's behaviour is reimplemented in fresh C++; all game data comes from the user's own disc at build time.

## 1. Match the original game

- Everything must behave exactly like the original: physics, AI, animation, effects, audio, HUD, menus, saves.
  No invented menus, logic, tuning or "improvements".
- The original game is the authority: its code (analysed externally), RAM snapshots, live PCSX2 recordings and
  graphics dumps. A user's description of the original is a hint about where to look, not a spec. If the
  evidence disagrees with the user, say so and show the evidence; if the user's own PCSX2 screenshots or logs
  disagree with an analysis, the analysis is wrong — recheck it.
- Verify with the user's real runtime when possible (their logs from PIE / the packaged game). Unit tests have
  missed real-device bugs before (e.g. UE's FSceneViewport flips Gamepad_RightY).
- Deliberate differences from the original only when the user asks. Put them behind a console variable,
  mark them in a one-line comment and the cvar help text (examples: `ssx.FE.AllRidersSavedGear`, Esc quits on
  the Title menu, render-only pose interpolation, menus clipped to 640x480).
- Decoded original behaviour that makes the replay sync worse stays off behind a cvar (e.g.
  `ssx.Rider.ControlInputs`, `ssx.Race.LineHintReset`, `ssx.Rider.PairListOrder`) until a recording confirms it.

## 2. Never commit game data or decompiled code

- Game-derived data is git-ignored and must stay that way: `extracted/`, `unpacked/`, `converted/`,
  `Unreal/SSXTrickyRebuild/SimData/`, `GameData/`, the imported `Content/{Tracks,Maps,Characters,FrontEnd,Audio}`,
  `Build/Windows/Application.ico`, `Packaged*/`, `traces/`, `*.iso`. Check `git status` before every commit.
- Never paste decompiled code (or long decompiler listings) into this repo. Write behaviour fresh. Short
  original-address tags in comments (`// 0x1364D0`) are fine and wanted.
- Every data file the game or tests need must be produced by a tool in `tools/` from the disc image (and, for
  test references only, optional recordings via `SSX_TRACES`). No hand-copied data files.

## 3. Repo layout and commands

- `build.bat [iso] [refs] [dev]` does everything; `build_data.bat [iso] [refs] [import]` converts the disc;
  `package.bat [Development]` packages to `Packaged\Windows\SSXTricky.exe`.
  Always pass the ISO path explicitly or rely on `SSX_ISO` in the git-ignored `local_settings.bat`.
- Unreal Engine is found by `tools\find_ue.bat`: `UE_ROOT`, `local_settings.bat`, the Epic launcher registry
  entry, `C:\Program Files\Epic Games\UE_5.8`.
- `tools/`: plain Python (no Unreal): extract, convert, export, test references; shared code in `tools/lib`
  (every tool starts with `import libpath`). `Unreal/SSXTrickyRebuild/Scripts/`: editor Python (`import unreal`),
  run through `UnrealEditor-Cmd ... -run=pythonscript -script="<absolute path>"`.
- C++: game module `Source/SSXTrickyRebuild` (folders Sim, Race, Camera, Input, Characters, Progression,
  Particles, Effects, World, Audio, UI, Harness). Tests live only in the editor-only module
  `Source/SSXTrickyRebuildTests` (same folder layout, test names `SSX.<Area>.<Name>`). The game target is
  `SSXTricky`, the editor target `SSXTrickyRebuildEditor`.
- Debug-only cvars, console commands, command-line switches and diagnostic logs go inside
  `#if !UE_BUILD_SHIPPING`. The Shipping package must contain no tests or debug switches.
- Build: `"%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" SSXTrickyRebuildEditor Win64 Development
  -Project="<abs>\Unreal\SSXTrickyRebuild\SSXTrickyRebuild.uproject" -WaitMutex`.
- Tests: `UnrealEditor-Cmd <uproject> -nullrhi -DisablePlugins=FSR -unattended -nosplash -ExecCmds="Automation RunTests SSX; Quit"
  -log=<unique>.log`, with `SSX_ORIGINAL_REPLAY` pointing at the user's REP0001 memory-card replay for the replay
  tests. The full suite must stay green. Run targeted groups while iterating and the full suite once at the end;
  never run two full suites at the same time (they share `Saved/Automation`).
- Material scripts (`make_world_materials.py` etc.) regenerate every material; revert regenerated assets you
  didn't mean to change, and never commit one material without the collection it references.

## 4. Safety

- If UBT says Live Coding / the editor is active, stop and ask the user to close the editor. Never kill the
  user's editor or any process you didn't start; record the PIDs you launch and kill only those.
- Never launch the game without the user's explicit permission: not `SSXTricky.exe` (`Packaged*/`), not
  `UnrealEditor -game`, not even off-screen or with `-nosound`. Headless editor commandlets
  (`-run=pythonscript`, automation tests with `-nullrhi`) are allowed. From Git Bash, prefix Unreal commands with
  `MSYS_NO_PATHCONV=1` and add `-unattended`.
- Never create symlinks, junctions or git worktrees, and never `rm -rf` / `git worktree remove --force` /
  `git clean` anything that may contain links (a junction once deleted all game data). Never touch
  `Saved/MemoryCard` (the user's saves and replays) or `Saved/Config`.
- Never rewrite published history, push, or squash without the user's explicit go-ahead.

## 5. Code style

- Match the surrounding code. Comments only where needed to understand it: short address tags, one-line
  conventions (axes, signs, units, frames, byte layouts, enum meanings, ordering), deliberate quirks and
  deviations, real `// TODO: ...` lines. No history ("now", "used to", commit hashes), no notes references,
  no narration of the original's code, no commented-out code.
- Commits: explicit paths (`git add <files>`), never `*.slnx`, `.vsconfig` or `local_settings.bat`; end the
  message with the co-author line your tool requires.
