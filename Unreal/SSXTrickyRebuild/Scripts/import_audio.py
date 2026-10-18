"""Editor script: import the converted audio (tools/convert_audio.py) as SoundWave assets."""
import json
import os
import re
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SRC = os.path.normpath(os.path.join(HERE, "..", "..", "..", "converted", "audio"))
BATCH = 64

force = "--force" in sys.argv[1:]
only = ""
src_root = DEFAULT_SRC
for arg in sys.argv[1:]:
    if arg.startswith("--only="):
        only = arg[len("--only="):]
    elif arg.startswith("--src="):
        src_root = arg[len("--src="):]

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def sanitize(name):
    """Same rule as SSXAudio::SanitizeAssetName."""
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def asset_location(file):
    """(package folder, asset name) for a manifest file path, as SSXAudio::AssetPathForFile."""
    folder = os.path.dirname(file).replace("\\", "/")
    name = sanitize(os.path.splitext(os.path.basename(file))[0])
    return f"/Game/Audio/{folder}", name


def set_looping(path, loop):
    wave = assets.load_asset(path)
    if not isinstance(wave, unreal.SoundWave):
        return False
    if bool(wave.get_editor_property("looping")) != loop:
        wave.set_editor_property("looping", loop)
        return True
    return False


with open(os.path.join(src_root, "manifest.json"), "r", encoding="utf-8") as f:
    manifest = json.load(f)

entries = [e for e in manifest["entries"] if e.get("import") and e["file"].startswith(only)]
todo, kept, missing = [], [], []
for e in entries:
    src = os.path.join(src_root, e["file"])
    if not os.path.isfile(src):
        missing.append(e["file"])
        continue
    folder, name = asset_location(e["file"])
    if not force and assets.does_asset_exist(f"{folder}/{name}"):
        kept.append(e)
    else:
        todo.append(e)

unreal.log(f"[ssx-audio] {len(entries)} entries: {len(todo)} to import, {len(kept)} already imported, {len(missing)} missing files")

imported = 0
failed = []
for start in range(0, len(todo), BATCH):
    batch = todo[start:start + BATCH]
    tasks = []
    for e in batch:
        folder, name = asset_location(e["file"])
        t = unreal.AssetImportTask()
        t.filename = os.path.join(src_root, e["file"])
        t.destination_path = folder
        t.destination_name = name
        t.automated = True
        t.replace_existing = True
        t.save = False
        tasks.append(t)
    tools.import_asset_tasks(tasks)
    dirty = []
    for e, t in zip(batch, tasks):
        folder, name = asset_location(e["file"])
        path = f"{folder}/{name}"
        if not assets.does_asset_exist(path):
            failed.append(e["file"])
            continue
        set_looping(path, "loop_start" in e)
        dirty.append(path)
        imported += 1
    for path in dirty:
        assets.save_asset(path, only_if_is_dirty=False)
    unreal.log(f"[ssx-audio] {min(start + BATCH, len(todo))}/{len(todo)} imported")

relooped = 0
for e in kept:
    folder, name = asset_location(e["file"])
    path = f"{folder}/{name}"
    if set_looping(path, "loop_start" in e):
        assets.save_asset(path, only_if_is_dirty=False)
        relooped += 1

unreal.log(f"[ssx-audio] done: {imported} imported, {len(kept)} kept ({relooped} looping flags fixed), "
           f"{len(failed)} failed, {len(missing)} missing")
for f in failed[:50]:
    unreal.log_warning(f"[ssx-audio] import failed: {f}")
for f in missing[:50]:
    unreal.log_warning(f"[ssx-audio] missing source: {f}")
