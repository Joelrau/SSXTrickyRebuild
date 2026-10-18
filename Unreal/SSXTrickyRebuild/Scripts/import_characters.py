"""Editor script: import the converted riders (tools/convert_characters.py) as Skeletal Meshes."""
# FUN_00201138
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ssx_content  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SRC = os.path.normpath(os.path.join(HERE, "..", "..", "..", "converted", "characters"))
boards_only = "--boards-only" in sys.argv
args = [a for a in sys.argv[1:] if a != "--boards-only"]
src_root = args[0] if args and os.path.isdir(args[0]) else DEFAULT_SRC
only = set(args[-1].split(",")) if args and not os.path.isdir(args[-1]) else None
BOARD_CLASSES = ("fr", "bx", "al", "fr_goofy", "bx_goofy", "al_goofy")

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
manager = unreal.InterchangeManager.get_interchange_manager_scripted()


def import_file(path, dest):
    source = unreal.InterchangeManager.create_source_data(path)
    params = unreal.ImportAssetParameters()
    params.is_automated = True
    params.replace_existing = True
    if not manager.import_asset(dest, source, params):
        raise RuntimeError(f"Interchange import failed for {path}")
    manager.wait_until_all_tasks_done(False)


def rename(asset, new_name):
    old = asset.get_path_name().split(".")[0]
    folder = old.rsplit("/", 1)[0]
    if old.rsplit("/", 1)[1] == new_name:
        return
    tools.rename_assets([unreal.AssetRenameData(asset, folder, new_name)])


def disable_nanite(mesh):
    ns = mesh.get_editor_property("nanite_settings")
    if ns.get_editor_property("enabled"):
        ns.set_editor_property("enabled", False)
        mesh.set_editor_property("nanite_settings", ns)


def import_boards(name, src, dest):
    for cls in BOARD_CLASSES:
        board_dest = f"{dest}/Board_{cls}"
        import_file(os.path.join(src, f"{name}_board_{cls}.gltf"), board_dest)
        for p in assets.list_assets(board_dest, recursive=True):
            a = assets.load_asset(p)
            if isinstance(a, unreal.StaticMesh):
                rename(a, f"SM_{name}_board_{cls}")
                disable_nanite(a)


def import_character(name):
    src = os.path.join(src_root, name)
    dest = f"/Game/Characters/{name}"
    if boards_only:
        ssx_content.clear([f"{dest}/Board_{cls}" for cls in BOARD_CLASSES])
        import_boards(name, src, dest)
        assets.save_directory(dest, only_if_is_dirty=False, recursive=True)
        unreal.log(f"[ssx] imported {dest}/Board_*")
        return
    ssx_content.clear([dest])

    import_file(os.path.join(src, f"{name}.gltf"), dest)
    skeletal = [assets.load_asset(p) for p in assets.list_assets(dest, recursive=True)]
    meshes = [a for a in skeletal if isinstance(a, unreal.SkeletalMesh)]
    if len(meshes) != 1:
        raise RuntimeError(f"{name}: expected 1 skeletal mesh, got {len(meshes)}")
    mesh = meshes[0]
    rename(mesh, f"SK_{name}")
    skeleton = mesh.get_editor_property("skeleton")
    if skeleton:
        rename(skeleton, f"SKEL_{name}")
    physics = mesh.get_editor_property("physics_asset")
    if physics:
        rename(physics, f"PHYS_{name}")

    import_boards(name, src, dest)

    assets.save_directory(dest, only_if_is_dirty=False, recursive=True)
    unreal.log(f"[ssx] imported {dest}/SK_{name}")


names = sorted(d for d in os.listdir(src_root) if os.path.isfile(os.path.join(src_root, d, f"{d}.gltf")))
if only:
    names = [n for n in names if n in only]
failed = []
for n in names:
    try:
        import_character(n)
    except Exception as e:
        failed.append(n)
        unreal.log_error(f"[ssx] {n}: {e}")
unreal.log(f"[ssx] characters imported: {len(names) - len(failed)}/{len(names)} failed={failed}")
