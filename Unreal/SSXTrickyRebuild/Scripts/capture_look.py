"""Editor script (development): offscreen captures of a track map in the Classic and Remastered looks.

    UnrealEditor-Cmd <uproject> -run=pythonscript -script="<abs>/capture_look.py <Map> <out dir> [x y z pitch yaw]"
        -unattended -AllowCommandletRendering -RenderOffscreen

Never starts the game: the map is loaded as an editor world, the Remastered look is applied with
USSXRemasterLibrary.SetRemasteredInWorld and a SceneCapture2D renders <out>/<Map>_classic.png and _remastered.png
(final colour, post-process volumes included). Without a camera pose the shot looks at the terrain from above its
highest corner. Editor worlds have no track presentation (MPC values, LOD terrain), so Classic here is an approximation.
"""
import os
import sys

import unreal

W, H = 1600, 900
FRAMES = 40   # Lumen gathers over frames


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        raise RuntimeError("usage: capture_look.py <Map> <out dir> [x y z pitch yaw]")
    name, out = args[0], args[1]
    os.makedirs(out, exist_ok=True)
    unreal.EditorLoadingAndSavingUtils.load_map(f"/Game/Maps/{name}")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SSXRemasterLibrary.finish_loading_for_capture()

    if len(args) >= 7:
        loc = unreal.Vector(*(float(v) for v in args[2:5]))
        rot = unreal.Rotator(roll=0.0, pitch=float(args[5]), yaw=float(args[6]))
    else:
        lo, hi = None, None
        for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
            for c in a.get_components_by_class(unreal.StaticMeshComponent):
                mesh = c.get_editor_property("static_mesh")
                if mesh and mesh.get_name().endswith("_terrain"):
                    o, e = a.get_actor_bounds(False)
                    lo, hi = o - e, o + e
        if lo is None:
            raise RuntimeError("no *_terrain mesh in the map")
        centre = (lo + hi) * 0.5
        loc = unreal.Vector(hi.x, hi.y, hi.z + 3000.0)
        rot = unreal.MathLibrary.find_look_at_rotation(loc, centre)
    unreal.log(f"[ssx] capture camera {loc} {rot}")

    rt = unreal.RenderingLibrary.create_render_target2d(world, W, H, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.SceneCapture2D, loc, rot)
    comp = cap.get_component_by_class(unreal.SceneCaptureComponent2D)
    comp.set_editor_property("texture_target", rt)
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property("capture_every_frame", False)
    comp.set_editor_property("always_persist_rendering_state", True)
    comp.set_editor_property("fov_angle", 75.0)

    for look, remastered in (("classic", False), ("remastered", True)):
        n = unreal.SSXRemasterLibrary.set_remastered_in_world(world, remastered)
        unreal.SystemLibrary.execute_console_command(world, f"ssx.Render.Look {1 if remastered else 0}")
        unreal.SSXRemasterLibrary.finish_loading_for_capture()
        for _ in range(FRAMES):
            comp.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, rt, out, f"{name}_{look}.png")
        unreal.log(f"[ssx] captured {name} {look} ({n} material slots switched)")
    unreal.SSXRemasterLibrary.set_remastered_in_world(world, False)
    unreal.SystemLibrary.execute_console_command(world, "ssx.Render.Look 0")


main()
