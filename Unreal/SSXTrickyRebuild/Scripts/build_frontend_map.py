"""Editor script: build the front-end map /Game/Maps/FrontEnd from the converted ssxfe scene."""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ssx_content  # noqa: E402
import ssx_world_build as wb  # noqa: E402

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, "..", "..", ".."))
gltf_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, "converted", "tracks", "ssxfe", "ssxfe.gltf")
content_path = "/Game/FrontEnd/Scene"
map_path = "/Game/Maps/FrontEnd"

if not os.path.isfile(gltf_path):
    raise RuntimeError(f"{gltf_path} not found: run tools/convert_tracks.py first")

assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

ssx_content.clear([content_path], [map_path])
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/Maps", "/Game/FrontEnd", "/Game/World"], True)
if assets.does_asset_exist(map_path):
    if not levels.load_level(map_path):
        raise RuntimeError(f"cannot load {map_path}")
    actors.destroy_actors([a for a in actors.get_all_level_actors()
                           if not (isinstance(a, unreal.WorldSettings) or a.get_class().get_name() == "Brush")])
elif not levels.new_level(map_path):
    raise RuntimeError(f"cannot create {map_path}")

wb.import_scene(gltf_path, content_path)
unreal.log(f"[ssx] {wb.tag_prop_actors(actors.get_all_level_actors())} FE prop actors tagged")


def medal_instances():
    """ssxfe.map INTERNAL INSTANCES: mdl_medal_NN -> instance index (UID). The venue-select stage's medal models; the
    controller shows one only when its venue has a medal (ASSXFrontEndPlayerController::TickMedals, playtest round 4)."""
    path = os.path.join(repo, "unpacked", "MODELS", "SSXFE", "data", "models", "ssxfe.map")
    out, sec = {}, False
    if not os.path.isfile(path):
        unreal.log_warning(f"[ssx] {path} missing: medal models stay always visible")
        return out
    for line in open(path, encoding="latin-1"):
        if "INTERNAL INSTANCES BEGIN" in line:
            sec = True
            continue
        if "INTERNAL INSTANCES END" in line:
            break
        f = line.split()
        if sec and not line.startswith("#") and len(f) >= 2 and f[0].startswith("mdl_medal_") and f[0][10:].isdigit():
            out[int(f[1])] = int(f[0][10:])
    return out


_medals = medal_instances()
_tagged = 0
for a in actors.get_all_level_actors():
    for t in a.tags:
        s = str(t)
        if s.startswith("SSXInst") and s[7:].isdigit() and int(s[7:]) in _medals:
            a.tags = list(a.tags) + [unreal.Name(f"SSXFeMedal{_medals[int(s[7:])]}")]
            _tagged += 1
            break
unreal.log(f"[ssx] {_tagged} medal actors tagged ({len(_medals)} mdl_medal instances in ssxfe.map)")

camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
camera.set_actor_label("FrontEndCamera")
camera.tags = [unreal.Name("FrontEndCamera")]
cam_comp = camera.get_component_by_class(unreal.CameraComponent)
cam_comp.set_editor_property("constrain_aspect_ratio", True)
cam_comp.set_editor_property("aspect_ratio", 4.0 / 3.0)
all_actors = actors.get_all_level_actors()

lm_png = os.path.join(os.path.dirname(gltf_path), "ssxfe_lightmap.png")
lightmap = wb.import_lightmap(lm_png, content_path, "T_ssxfe_Lightmap") if os.path.isfile(lm_png) else None
wb.apply_world_materials(content_path, lightmap, opaque=True)
stage = 0
for a in all_actors:
    for comp in a.get_components_by_class(unreal.StaticMeshComponent):
        comp.set_editor_property("visible", True)
        comp.set_editor_property("hidden_in_game", False)
        comp.set_editor_property("render_in_main_pass", True)
        comp.set_editor_property("render_in_depth_pass", True)
        stage += 1
unreal.log(f"[ssx] FE stage: {stage} mesh components opaque/depth-tested")
wb.add_presentation_volume()

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = world.get_world_settings()
settings.set_editor_property("default_game_mode", unreal.load_class(None, "/Script/SSXTrickyRebuild.SSXFrontEndGameMode"))

levels.save_current_level()
assets.save_directory(content_path, only_if_is_dirty=False, recursive=True)
unreal.log(f"[ssx] built {map_path} ({len(all_actors)} actors)")
