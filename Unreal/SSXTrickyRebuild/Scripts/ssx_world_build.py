"""Shared map-building steps for the world presentation."""
import os
import re

import unreal

assets = unreal.EditorAssetLibrary
WORLD = "/Game/World"
SKY_TAG = "SSXSky"
SKY_SCALE = 50.0


def _load(name):
    a = unreal.load_asset(f"{WORLD}/{name}")
    if a is None:
        raise RuntimeError(f"{WORLD}/{name} missing: run Scripts/make_world_materials.py first")
    return a


def import_lightmap(png_path, dest_path, name):
    """The packed lightmap atlas (tools/pbd2gltf.py): raw values, no sRGB, uncompressed, no mips."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png_path)
    task.set_editor_property("destination_path", dest_path)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.load_asset(f"{dest_path}/{name}")
    if tex is None:
        raise RuntimeError(f"lightmap import failed: {png_path}")
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    return tex


def _base_texture(mi):
    for t in mi.get_editor_property("texture_parameter_values"):
        if str(t.parameter_info.name) == "BaseColorTexture" and t.parameter_value:
            return t.parameter_value
    return None


def _is_masked(mi):
    parent = mi.get_editor_property("parent")
    return parent is not None and "Mask" in parent.get_name()


def keep_imported_uvs(mesh):
    """Stop the mesh build from generating lightmap UVs: they would overwrite UV channel 1, which holds
    the patch parameter for the baked lightmap tile (TEXCOORD_1, tools/pbd2gltf.py)."""
    lib = unreal.EditorStaticMeshLibrary
    settings = lib.get_lod_build_settings(mesh, 0)
    if settings.get_editor_property("generate_lightmap_u_vs") or not settings.get_editor_property("use_full_precision_u_vs"):
        settings.set_editor_property("generate_lightmap_u_vs", False)
        settings.set_editor_property("use_full_precision_u_vs", True)
        lib.set_lod_build_settings(mesh, 0, settings)
        unreal.log(f"[ssx] {mesh.get_name()}: lightmap UV generation off, full-precision UVs, imported UV1/UV2 kept")


PROP_NAME = re.compile(r"^tex_(-?\d+)(_2s)?(?:_f([0-9a-f]+)(?:_(cut|soft))?(?:_(core|blend))?)?$")


def is_two_sided_material(src):
    """The Interchange material made from a pbd2gltf two-sided prop material (glTF name tex_NNN_2s..., doubleSided)."""
    m = PROP_NAME.match(src.get_name())
    return bool(m and m.group(2))


def prop_kind(src, masked):
    """Prop parent material from the pbd2gltf name tex_NNN[_2s]_fXXXXX[_cut|_soft][_core|_blend] (no Opaque / _TwoSided suffix).
    Every prop batch draws with GS ALPHA mode 3 + alpha test GREATER 1, so the texture alpha picks the blend:
    opaque -> M_SSXProp, cutout -> M_SSXProp_Masked, soft -> M_SSXProp_TranslucentCore / M_SSXProp_Translucent.
    The material flags (0x40000 translucent, 0x20000) only set the original's draw order."""
    m = PROP_NAME.match(src.get_name())
    if not m or m.group(3) is None:
        return "M_SSXProp_Masked" if masked else "M_SSXProp"
    alpha, part = m.group(4), m.group(5)
    if part == "core":
        return "M_SSXProp_TranslucentCore"
    if part == "blend":
        return "M_SSXProp_Translucent"
    if alpha in ("cut", "soft"):
        return "M_SSXProp_Masked"
    return "M_SSXProp"


def prop_flags(src):
    """The .pbd material flags (+0x40) of a pbd2gltf prop material name, or None."""
    m = PROP_NAME.match(src.get_name())
    return int(m.group(3), 16) if m and m.group(3) else None


def apply_world_materials(content_path, lightmap=None, sky=False, opaque=False):
    """Swap the Interchange glTF materials of every static mesh under content_path for the world materials:
    *_terrain -> M_SSXTerrain (texture x lightmap), others -> M_SSXProp (texture x baked vertex colour) or M_SSXSky.
    Textures without alpha get the BLEND_OPAQUE variants (no draw-distance clip); alpha-tested ones stay masked."""
    parents = {k: _load(k) for k in ("M_SSXTerrain", "M_SSXTerrain_Masked", "M_SSXProp", "M_SSXProp_Masked", "M_SSXSky",
                                     "M_SSXTerrainOpaque", "M_SSXPropOpaque", "M_SSXProp_TwoSided",
                                     "M_SSXProp_Masked_TwoSided", "M_SSXPropOpaque_TwoSided",
                                     "M_SSXProp_TranslucentCore", "M_SSXProp_TranslucentCore_TwoSided",
                                     "M_SSXProp_Translucent", "M_SSXProp_Translucent_TwoSided")}
    counts = {}
    factory = unreal.MaterialInstanceConstantFactoryNew()
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    made = {}
    meshes = 0
    for path in assets.list_assets(content_path, recursive=True):
        mesh = assets.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        meshes += 1
        ns = mesh.get_editor_property("nanite_settings")
        if ns.get_editor_property("enabled"):
            ns.set_editor_property("enabled", False)
            mesh.set_editor_property("nanite_settings", ns)
        terrain = mesh.get_name().endswith("_terrain")
        if terrain:
            keep_imported_uvs(mesh)
        for slot, sm in enumerate(mesh.get_editor_property("static_materials")):
            src = sm.get_editor_property("material_interface")
            if not isinstance(src, unreal.MaterialInstanceConstant):
                continue
            masked = _is_masked(src)
            if sky:
                kind = "M_SSXSky"
            elif terrain:
                kind = "M_SSXTerrain" + ("_Masked" if masked else ("Opaque" if opaque else ""))
            else:
                kind = prop_kind(src, masked)
                if kind == "M_SSXProp" and opaque:
                    kind = "M_SSXPropOpaque"
            if not sky and not terrain and is_two_sided_material(src):
                kind += "_TwoSided"
            key = (src.get_path_name(), kind)
            if key not in made:
                folder = src.get_path_name().rsplit("/", 1)[0]
                name = f"{src.get_name()}_{kind[6:]}"
                if assets.does_asset_exist(f"{folder}/{name}"):
                    assets.delete_asset(f"{folder}/{name}")
                mic = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, factory)
                mel.set_material_instance_parent(mic, parents[kind])
                tex = _base_texture(src)
                if tex:
                    if tex.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_EDITOR_ICON:
                        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
                    mel.set_material_instance_texture_parameter_value(mic, "BaseTex", tex)
                if kind.startswith("M_SSXTerrain") and lightmap is not None:
                    mel.set_material_instance_texture_parameter_value(mic, "Lightmap", lightmap)
                made[key] = mic
                counts[kind] = counts.get(kind, 0) + 1
            mesh.set_material(slot, made[key])
    unreal.log(f"[ssx] world materials: {meshes} meshes, {len(made)} material instances; per parent "
               + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    return made


def tag_prop_actors(level_actors):
    """Tag the prop object actors ("<track>_inst<iiii>_obj<k>", tools/pbd2gltf.py) SSXInst<i> and SSXObj<k>: the
    run-time ShowOff draw rule, track effects and prop animation find their actors by these tags."""
    import re
    label = re.compile(r"_inst(\d+)_obj(\d+)$")
    n = 0
    for a in level_actors:
        m = label.search(a.get_actor_label())
        if m:
            a.tags = list(a.tags) + [unreal.Name(f"SSXInst{int(m.group(1))}"), unreal.Name(f"SSXObj{int(m.group(2))}")]
            n += 1
    return n


def import_scene(gltf_path, content_path):
    """Interchange scene import into the current level; returns the actors it spawned."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    before = set(a.get_path_name() for a in actors.get_all_level_actors())
    source = unreal.InterchangeManager.create_source_data(gltf_path)
    params = unreal.ImportAssetParameters()
    params.is_automated = True
    params.replace_existing = True
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    if not manager.import_scene(content_path, source, params):
        raise RuntimeError(f"Interchange scene import failed for {gltf_path}")
    manager.wait_until_all_tasks_done(False)
    return [a for a in actors.get_all_level_actors() if a.get_path_name() not in before]


def add_sky(sky_gltf, content_path):
    """The track's sky dome: unlit, camera-centred at run time
    (USSXWorldPresentation moves the actor tagged SSXSky to the camera every frame)."""
    if not os.path.isfile(sky_gltf):
        unreal.log_warning(f"[ssx] no sky model {sky_gltf}; none is drawn (as the original when missing)")
        return None
    spawned = import_scene(sky_gltf, content_path)
    apply_world_materials(content_path, sky=True)
    roots = [a for a in spawned if a.get_attach_parent_actor() is None]
    for a in spawned:
        root = a.get_editor_property("root_component")
        if root:
            root.set_mobility(unreal.ComponentMobility.MOVABLE)
        for comp in a.get_components_by_class(unreal.StaticMeshComponent):
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            comp.set_editor_property("cast_shadow", False)
    for r in roots:
        r.tags = [unreal.Name(SKY_TAG)]
        r.set_actor_scale3d(unreal.Vector(SKY_SCALE, SKY_SCALE, SKY_SCALE))
    unreal.log(f"[ssx] sky: {len(spawned)} actors, {len(roots)} root(s) tagged {SKY_TAG}")
    return roots


def add_presentation_volume():
    """Unbound post-process volume: the scene colour is shown as authored (PP_SSXAuthoredColour
    replaces the tonemapper), exposure locked, bloom / vignette / grain / flares / motion blur off."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    vol = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
    vol.set_actor_label("SSXPresentation")
    vol.set_editor_property("unbound", True)
    s = vol.get_editor_property("settings")
    for field, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                         ("auto_exposure_bias", 0.0),
                         ("auto_exposure_apply_physical_camera_exposure", False),
                         ("bloom_intensity", 0.0), ("vignette_intensity", 0.0), ("film_grain_intensity", 0.0),
                         ("lens_flare_intensity", 0.0), ("motion_blur_amount", 0.0),
                         ("scene_fringe_intensity", 0.0), ("ambient_occlusion_intensity", 0.0)):
        s.set_editor_property(f"override_{field}", True)
        s.set_editor_property(field, value)
    wb = unreal.WeightedBlendable()
    wb.set_editor_property("weight", 1.0)
    wb.set_editor_property("object", _load("PP_SSXAuthoredColour"))
    blendables = unreal.WeightedBlendables()
    blendables.set_editor_property("array", [wb])
    s.set_editor_property("weighted_blendables", blendables)
    vol.set_editor_property("settings", s)
    return vol
