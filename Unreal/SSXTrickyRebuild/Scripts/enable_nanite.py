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


def flag_classic_materials():
    """The Classic base materials stay on the Nanite meshes wherever the Remastered look keeps them: without the usage
    flag those slots draw the default (checker) material. The flag only adds a shader permutation; Classic (r.Nanite 0)
    draws as before."""
    for path in assets.list_assets("/Game/World", recursive=False, include_folder=False):
        name = path.split("/")[-1].split(".")[0]
        if not name.startswith("M_SSX") or name.startswith("M_SSXRider") or name == "M_SSXClearColour":
            continue
        m = assets.load_asset(path)
        if isinstance(m, unreal.Material) and not m.get_editor_property("used_with_nanite"):
            m.set_editor_property("used_with_nanite", True)
            unreal.MaterialEditingLibrary.recompile_material(m)
            assets.save_asset(path, only_if_is_dirty=False)
            unreal.log(f"[ssx] nanite usage flag on {name}")


NANITE_MAX_SECTIONS = 64


def build_terrain_shadow(path, mesh):
    """A terrain past Nanite's section limit builds no Nanite data; its shadow-only copy (/Game/TerrainShadow/<mesh>_Shadow,
    opaque sections merged, git-ignored) casts the Remastered sun shadow instead (USSXRemasterWorld::MakeTerrainShadowTwin)."""
    shadow_pkg = f"/Game/TerrainShadow/{path.split('/')[-1].split('.')[0]}_Shadow"
    if mesh.get_num_sections(0) <= NANITE_MAX_SECTIONS:
        if assets.does_asset_exist(shadow_pkg):
            assets.delete_asset(shadow_pkg)
        return
    if assets.does_asset_exist(shadow_pkg):
        assets.delete_asset(shadow_pkg)
    out = unreal.SSXRemasterLibrary.build_terrain_shadow_mesh(mesh, shadow_pkg)
    if out:
        assets.save_asset(shadow_pkg, only_if_is_dirty=False)
        unreal.log(f"[ssx] terrain shadow {shadow_pkg}: {mesh.get_num_sections(0)} sections -> {out.get_num_sections(0)}")
    else:
        unreal.log_warning(f"[ssx] terrain shadow {shadow_pkg}: still past {NANITE_MAX_SECTIONS} sections")


def flag_translucent_components(name):
    """A component with a translucent slot never draws as Nanite (Nanite draws opaque / masked only): Disallow Nanite in
    the map, so the engine logs no invalid-material warning for it. Its mesh keeps the Nanite data for the Remastered
    shadow twin (USSXRemasterWorld::ApplyTranslucentProps)."""
    path = f"/Game/Maps/{name}"
    if not assets.does_asset_exist(path):
        return
    world = unreal.EditorLoadingAndSavingUtils.load_map(path)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    solid = (unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED)
    changed = flagged = 0
    for actor in actors:
        for c in actor.get_components_by_class(unreal.StaticMeshComponent):
            mats = [c.get_material(i) for i in range(c.get_num_materials())]
            want = any(m is not None and m.get_blend_mode() not in solid for m in mats)
            flagged += want
            if bool(c.get_editor_property("disallow_nanite")) != want:
                c.set_editor_property("disallow_nanite", want)
                changed += 1
    if changed:
        unreal.EditorLoadingAndSavingUtils.save_map(world, path)
    unreal.log(f"[ssx] {name}: {flagged} components with a translucent slot drawn non-Nanite ({changed} changed)")


def main():
    flag_classic_materials()
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
            if path.split(".")[0].endswith("_terrain"):
                build_terrain_shadow(path, mesh)
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
        flag_translucent_components(name)


main()
