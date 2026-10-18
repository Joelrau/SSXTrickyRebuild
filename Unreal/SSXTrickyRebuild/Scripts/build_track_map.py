"""Editor script: build a playable map for one track from its converted glTF."""
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ssx_content  # noqa: E402
import ssx_world_build as wb  # noqa: E402

track, gltf_path = sys.argv[1], sys.argv[2]
if not os.path.isfile(gltf_path):
    raise RuntimeError(f"[ssx] track glTF not found: {gltf_path}")
content_path = f"/Game/Tracks/{track}"
map_path = f"/Game/Maps/{track}"

assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

ssx_content.clear([content_path], [map_path])
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/Maps", "/Game/Tracks", "/Game/World"], True)
if assets.does_asset_exist(map_path):
    if not levels.load_level(map_path):
        raise RuntimeError(f"cannot load {map_path}")
    actors.destroy_actors([a for a in actors.get_all_level_actors()
                           if not (isinstance(a, unreal.WorldSettings) or a.get_class().get_name() == "Brush")])
    unreal.log(f"[ssx] cleared {map_path}; kept {[a.get_class().get_name() for a in actors.get_all_level_actors()]}")
elif not levels.new_level(map_path):
    raise RuntimeError(f"cannot create {map_path}")

wb.import_scene(gltf_path, content_path)

mesh_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
mesh_count = 0
for asset_path in assets.list_assets(content_path, recursive=True):
    asset = assets.load_asset(asset_path)
    if isinstance(asset, unreal.StaticMesh):
        mesh_subsystem.remove_collisions(asset)
        for section in range(asset.get_num_sections(0)):
            mesh_subsystem.enable_section_collision(asset, False, 0, section)
        body = asset.get_editor_property("body_setup")
        if body:
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        ns = asset.get_editor_property("nanite_settings")
        if ns.get_editor_property("enabled"):
            ns.set_editor_property("enabled", False)
            asset.set_editor_property("nanite_settings", ns)
        asset.modify()
        mesh_count += 1
for a in actors.get_all_level_actors():
    for comp in a.get_components_by_class(unreal.StaticMeshComponent):
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_collision_profile_name("NoCollision")
unreal.log(f"[ssx] {mesh_count} static meshes without collision")

tagged = wb.tag_prop_actors(actors.get_all_level_actors())
unreal.log(f"[ssx] {tagged} prop actors tagged with their instance / object index")
showoff = [a for a in actors.get_all_level_actors() if a.get_actor_label().endswith("_showoff_terrain")]
for a in showoff:
    a.tags = list(a.tags) + [unreal.Name("SSXShowOffTerrain")]
unreal.log(f"[ssx] {len(showoff)} ShowOff terrain actor(s) tagged")

all_actors = actors.get_all_level_actors()
def find_actor(suffix):
    for a in all_actors:
        if a.get_actor_label().endswith(suffix):
            return a
    return None

start_marker = find_actor("_PlayerStart")
fwd_marker = find_actor("_PlayerStartForward")
if start_marker and fwd_marker:
    start = start_marker.get_actor_location()
    direction = fwd_marker.get_actor_location() - start
    yaw = unreal.MathLibrary.make_rot_from_x(direction).yaw
    actors.spawn_actor_from_class(unreal.PlayerStart, start, unreal.Rotator(0.0, 0.0, yaw))
    unreal.log(f"[ssx] PlayerStart at {start}, yaw {yaw:.1f}")
    actors.destroy_actors([start_marker, fwd_marker])
else:
    unreal.log_warning("[ssx] start markers not found; place a PlayerStart manually")

lowest = min((a.get_actor_bounds(False)[0].z - a.get_actor_bounds(False)[1].z
              for a in all_actors if isinstance(a, unreal.StaticMeshActor)), default=-100000.0)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property("kill_z", lowest - 20000.0)
unreal.log(f"[ssx] kill_z = {lowest - 20000.0:.0f}")

stem = os.path.splitext(os.path.basename(gltf_path))[0]
lm_png = os.path.join(os.path.dirname(gltf_path), f"{stem}_lightmap.png")
lightmap = wb.import_lightmap(lm_png, content_path, f"T_{stem}_Lightmap") if os.path.isfile(lm_png) else None
if lightmap is None:
    unreal.log_warning(f"[ssx] no lightmap atlas {lm_png} (re-run tools/convert_tracks.py)")
wb.apply_world_materials(content_path, lightmap)
wb.add_sky(os.path.join(os.path.dirname(os.path.dirname(gltf_path)), f"{stem}_sky", f"{stem}_sky.gltf"), f"{content_path}/Sky")
wb.add_presentation_volume()

levels.save_current_level()
assets.save_directory(content_path, only_if_is_dirty=False, recursive=True)
unreal.log(f"[ssx] built {map_path} with {len(all_actors)} actors")
