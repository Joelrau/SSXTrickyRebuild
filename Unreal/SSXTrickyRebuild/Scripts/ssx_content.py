"""Clearing imported content before a re-import."""
import os
import stat

import unreal

CONTENT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content"))


def _disk_path(package_path):
    if not package_path.startswith("/Game/"):
        raise RuntimeError(f"not a /Game path: {package_path}")
    return os.path.join(CONTENT_DIR, *package_path[len("/Game/"):].split("/"))


def _check_no_link(path):
    if os.lstat(path).st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        raise RuntimeError(f"{path} is a link or junction: not deleting")


def _remove_tree(root):
    _check_no_link(root)
    folders, files = [], []
    for folder, subdirs, names in os.walk(root, followlinks=False):
        folders.append(folder)
        for name in subdirs + names:
            _check_no_link(os.path.join(folder, name))
        files += [os.path.join(folder, n) for n in names]
    for path in files:
        os.remove(path)
    for folder in reversed(folders):
        os.rmdir(folder)
    return len(files)


def clear(folders, packages=()):
    """Remove imported folders and single packages (e.g. the map) from disk and from the asset registry, before
    anything loads them. The editor delete (EditorAssetLibrary.delete_directory) loads and reference-checks every
    package and keeps their registry package data, so every re-imported asset logs 'Error opening file'.
    Anything already loaded goes through the editor delete."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    parents = sorted({p.rsplit("/", 1)[0] for p in packages})
    registry.scan_paths_synchronous(list(folders) + parents, False)
    removed = 0
    for folder in folders:
        if any(a.is_asset_loaded() for a in registry.get_assets_by_path(folder, recursive=True)):
            unreal.EditorAssetLibrary.delete_directory(folder)
        elif os.path.isdir(_disk_path(folder)):
            removed += _remove_tree(_disk_path(folder))
    for package in packages:
        found = registry.get_assets_by_package_name(package)
        if any(a.is_asset_loaded() for a in found):
            unreal.EditorAssetLibrary.delete_asset(package)
            continue
        for ext in (".umap", ".uasset"):
            path = _disk_path(package) + ext
            if os.path.isfile(path):
                _check_no_link(path)
                os.remove(path)
                removed += 1
    registry.scan_paths_synchronous(list(folders) + parents, True)
    unreal.log(f"[ssx] cleared {', '.join(list(folders) + list(packages))}: {removed} files")
