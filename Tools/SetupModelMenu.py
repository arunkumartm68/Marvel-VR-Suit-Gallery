"""
Sets up the hand-menu model list for the MR viewer. Safe to run more than once.

  * M_MR_MenuPlate    - unlit material with a "Color" parameter for the menu panels
  * DA_ModelCatalog   - the models offered in the hand menu (entry 0: Iron Man Mark 85)
  * BP_MRSuitViewer   - ModelCatalog = DA_ModelCatalog, empty room at start
  * BP_MRPawn         - hand menu uses M_MR_MenuPlate

Run headless (editor closed):
  UnrealEditor-Cmd.exe Marvel.uproject -run=pythonscript -script="Tools/SetupModelMenu.py"
"""
import unreal

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

MENU_MATERIAL = "/Game/MRSuitViewer/Materials/M_MR_MenuPlate"
CATALOG = "/Game/MRSuitViewer/Data/DA_ModelCatalog"
IRON_MAN_CLASS = "/Game/MRSuitViewer/Blueprints/BP_IronMan85.BP_IronMan85_C"
IRON_MAN_CONFIG = "/Game/MRSuitViewer/Data/DA_SuitConfiguration.DA_SuitConfiguration"
VIEWER_BP = "/Game/MRSuitViewer/Blueprints/BP_MRSuitViewer"
PAWN_BP = "/Game/MRSuitViewer/Blueprints/BP_MRPawn"
LEVEL = "/Game/MRSuitViewer/Maps/L_MRSuitViewer"


def log(msg):
    unreal.log_warning("[SetupModelMenu] " + msg)  # warnings stand out in the commandlet output


# 1. Menu material
if eal.does_asset_exist(MENU_MATERIAL):
    menu_material = eal.load_asset(MENU_MATERIAL)
    log("menu material exists")
else:
    menu_material = tools.create_asset("M_MR_MenuPlate", "/Game/MRSuitViewer/Materials", unreal.Material, unreal.MaterialFactoryNew())
    menu_material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mel = unreal.MaterialEditingLibrary
    color = mel.create_material_expression(menu_material, unreal.MaterialExpressionVectorParameter, -300, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.02, 0.045, 0.07, 1.0))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(menu_material)
    log("menu material created")
eal.save_asset(MENU_MATERIAL, only_if_is_dirty=False)

# 2. Model catalog
if eal.does_asset_exist(CATALOG):
    catalog = eal.load_asset(CATALOG)
    log("catalog exists")
else:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.MRModelCatalog)
    catalog = tools.create_asset("DA_ModelCatalog", "/Game/MRSuitViewer/Data", unreal.MRModelCatalog, factory)
    log("catalog created")

iron_man = unreal.MRModelEntry()
iron_man.set_editor_property("display_name", unreal.Text("IRON MAN MARK 85"))
iron_man.set_editor_property("description", unreal.Text("Life size  -  1.90 m"))
iron_man.set_editor_property("actor_class", unreal.load_class(None, IRON_MAN_CLASS))
iron_man.set_editor_property("configuration", unreal.load_object(None, IRON_MAN_CONFIG))

models = list(catalog.get_editor_property("models"))
if not models:
    models = [iron_man]
else:
    models[0] = iron_man  # keep any further models the user added
catalog.set_editor_property("models", models)
eal.save_asset(CATALOG, only_if_is_dirty=False)
for index, entry in enumerate(catalog.get_editor_property("models")):
    log("catalog[%d] = %s | %s | %s" % (index, entry.get_editor_property("display_name"),
                                         entry.get_editor_property("actor_class"), entry.get_editor_property("configuration")))

# 3. Viewer: use the catalog, start with an empty room
viewer_class = unreal.load_class(None, VIEWER_BP + ".BP_MRSuitViewer_C")
viewer_cdo = unreal.get_default_object(viewer_class)
viewer_cdo.set_editor_property("model_catalog", catalog)
viewer_cdo.set_editor_property("auto_place_suit", False)
eal.save_asset(VIEWER_BP, only_if_is_dirty=False)
log("BP_MRSuitViewer: model_catalog=%s auto_place_suit=%s" % (viewer_cdo.get_editor_property("model_catalog"), viewer_cdo.get_editor_property("auto_place_suit")))

# The level's viewer instance may override the defaults; set it there too.
try:
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    for actor in actors:
        if actor.get_class() == viewer_class:
            actor.set_editor_property("model_catalog", catalog)
            actor.set_editor_property("auto_place_suit", False)
            log("level viewer instance %s updated" % actor.get_name())
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(save_map_packages=True, save_content_packages=True)
except Exception as error:
    log("could not update the level instance: %s" % error)

# 4. Pawn: hand menu material
pawn_class = unreal.load_class(None, PAWN_BP + ".BP_MRPawn_C")
pawn_cdo = unreal.get_default_object(pawn_class)
menu = pawn_cdo.get_editor_property("wrist_menu")
menu.set_editor_property("plate_material", menu_material)
eal.save_asset(PAWN_BP, only_if_is_dirty=False)
log("BP_MRPawn wrist menu plate_material=%s" % menu.get_editor_property("plate_material"))

log("done")
