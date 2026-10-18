"""Editor script: create /Game/Rider/M_RiderColor, a plain lit material with a 'Color' parameter."""
import unreal

path, name = "/Game/Rider", "M_RiderColor"
assets = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
if assets.does_asset_exist(f"{path}/{name}"):
    assets.delete_asset(f"{path}/{name}")

material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())
color = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, 0)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
rough = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 250)
rough.set_editor_property("r", 0.55)
mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
mel.recompile_material(material)
assets.save_asset(f"{path}/{name}", only_if_is_dirty=False)
unreal.log(f"[ssx] created {path}/{name}")
