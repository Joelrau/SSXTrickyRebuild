"""Editor script: turn Nanite on for the imported track meshes (/Game/Tracks/<Map>), for the Remastered look.

    UnrealEditor-Cmd <uproject> -run=pythonscript -script="<abs>/enable_nanite.py [Map ...]" -unattended

The fallback mesh keeps every triangle (FallbackPercentTriangles 1.0): the Classic look renders with r.Nanite 0, i.e.
the fallback, so its geometry stays the original's. Only the Remastered look sets r.Nanite 1 (World/SSXRenderLook.cpp).
The track meshes are local imports (git-ignored); rerun after re-importing a track.
"""
import sys
import time

import unreal

assets = unreal.EditorAssetLibrary


def main():
    maps = sys.argv[1:] or ["Alaska", "Aloha", "Elysium", "Gari", "Megaple", "Merquer", "Mesa", "Pipe", "Snow", "Trick", "Untrack"]
    for name in maps:
        t0 = time.time()
        paths = [p for p in assets.list_assets(f"/Game/Tracks/{name}", recursive=True, include_folder=False)]
        done = 0
        for path in paths:
            data = unreal.EditorAssetLibrary.find_asset_data(path)
            if str(data.asset_class_path.asset_name) != "StaticMesh":
                continue
            mesh = assets.load_asset(path)
            s = mesh.get_editor_property("nanite_settings")
            if (s.get_editor_property("enabled") and s.get_editor_property("fallback_percent_triangles") >= 1.0
                    and s.get_editor_property("fallback_target") == unreal.NaniteFallbackTarget.PERCENT_TRIANGLES):
                continue
            s.set_editor_property("enabled", True)
            s.set_editor_property("fallback_target", unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
            s.set_editor_property("fallback_percent_triangles", 1.0)
            mesh.set_editor_property("nanite_settings", s)
            assets.save_asset(path, only_if_is_dirty=False)
            done += 1
        unreal.log(f"[ssx] nanite {name}: {done} meshes enabled in {time.time() - t0:.0f} s")


main()
