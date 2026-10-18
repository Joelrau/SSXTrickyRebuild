"""Editor script: import the Remastered-look maps (tools/make_remaster_maps.py) into /Game/RemasterData.

converted/remaster/<rel>_C.png (colour, sRGB), _N.png (normal map) and _M.png (linear detail masks) become
/Game/RemasterData/<rel dir>/T_<stem>_C / _N / _M. Game-derived data: /Game/RemasterData is never committed.
The Remastered look finds them by path; a texture without maps falls back to shader-derived detail.
"""
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ssx_content  # noqa: E402

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
SRC = os.path.join(REPO, "converted", "remaster")
DEST = "/Game/RemasterData"
BATCH = 200


def settings(tex, kind):
    if kind == "N":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    elif kind == "M":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", True)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)


def main():
    manifest_path = os.path.join(SRC, "manifest.json")
    if not os.path.isfile(manifest_path):
        raise RuntimeError(f"{manifest_path} missing: run tools/make_remaster_maps.py first")
    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)
    ssx_content.clear([DEST])
    entries = []
    for t in manifest["textures"]:
        for rel in t["maps"]:
            folder = DEST + "/" + os.path.dirname(rel)
            stem = os.path.splitext(os.path.basename(rel))[0]
            entries.append((os.path.join(SRC, *rel.split("/")), folder, "T_" + stem, stem[-1]))
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    done = 0
    for i in range(0, len(entries), BATCH):
        batch = entries[i:i + BATCH]
        tasks = []
        for src, folder, name, _ in batch:
            t = unreal.AssetImportTask()
            t.filename = src
            t.destination_path = folder
            t.destination_name = name
            t.automated = True
            t.replace_existing = True
            t.save = False
            tasks.append(t)
        tools.import_asset_tasks(tasks)
        for (src, folder, name, kind) in batch:
            path = f"{folder}/{name}"
            tex = unreal.EditorAssetLibrary.load_asset(path)
            if not isinstance(tex, unreal.Texture2D):
                raise RuntimeError(f"import failed: {src}")
            settings(tex, kind)
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        done += len(batch)
        unreal.log(f"[ssx] remaster maps: {done}/{len(entries)}")
    unreal.log(f"[ssx] imported {len(entries)} remaster maps into {DEST}")


main()
