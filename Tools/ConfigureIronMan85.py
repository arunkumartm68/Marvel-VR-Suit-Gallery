import unreal

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
BEL = unreal.BlueprintEditorLibrary

config_path = "/Game/MRSuitViewer/Data/DA_SuitConfiguration"
config = EAL.load_asset(config_path)

if config:
    print(f"Loaded {config_path}")
    print(f"Current mesh: {config.get_editor_property('suit_mesh')}")
    print(f"Current rotation_offset: {config.get_editor_property('rotation_offset')}")
    print(f"Current suit_scale: {config.get_editor_property('suit_scale')}")
    
    # Configure 1:1 physical real-world scale
    mesh_path = "/Game/MRSuitViewer/Suit/SM_IronManMk85_Quest"
    if EAL.does_asset_exist(mesh_path):
        suit_mesh = EAL.load_asset(mesh_path)
        config.set_editor_property("suit_mesh", suit_mesh)
    
    # Authored mesh stands upright when Pitch=0.0, Roll=0.0, Yaw=-90.0 (front facing +X)
    config.set_editor_property("rotation_offset", unreal.Rotator(pitch=0.0, yaw=-90.0, roll=0.0))
    config.set_editor_property("target_height", 190.0)
    config.set_editor_property("suit_scale", 1.0)
    config.set_editor_property("initial_scale", 1.0)
    config.set_editor_property("minimum_scale", 0.2)
    config.set_editor_property("maximum_scale", 3.0)
    config.set_editor_property("floor_offset", 0.0)
    
    EAL.save_loaded_asset(config)
    print("DA_SuitConfiguration saved successfully with physical 1:1 scale and limits!")

# Create / verify BP_IronMan85
bp_dir = "/Game/MRSuitViewer/Blueprints"
iron_man_bp_path = f"{bp_dir}/BP_IronMan85"

if not EAL.does_asset_exist(iron_man_bp_path):
    print("Creating BP_IronMan85...")
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.MRSuit.static_class())
    bp = AT.create_asset("BP_IronMan85", bp_dir, unreal.Blueprint, factory)
    
    # Set CDO defaults
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("configuration", config)
    BEL.compile_blueprint(bp)
    EAL.save_loaded_asset(bp)
    print("BP_IronMan85 created and saved!")
else:
    print("Updating BP_IronMan85 defaults...")
    bp = EAL.load_asset(iron_man_bp_path)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("configuration", config)
    BEL.compile_blueprint(bp)
    EAL.save_loaded_asset(bp)
    print("BP_IronMan85 updated and saved!")

# Also update BP_MRSuit to point to config
mr_suit_bp_path = f"{bp_dir}/BP_MRSuit"
if EAL.does_asset_exist(mr_suit_bp_path):
    bp_suit = EAL.load_asset(mr_suit_bp_path)
    cdo = unreal.get_default_object(bp_suit.generated_class())
    cdo.set_editor_property("configuration", config)
    BEL.compile_blueprint(bp_suit)
    EAL.save_loaded_asset(bp_suit)
    print("BP_MRSuit updated with DA_SuitConfiguration!")

# Also recompile BP_MRPawn so it inherits the newly added components
pawn_bp_path = f"{bp_dir}/BP_MRPawn"
if EAL.does_asset_exist(pawn_bp_path):
    bp_pawn = EAL.load_asset(pawn_bp_path)
    BEL.compile_blueprint(bp_pawn)
    EAL.save_loaded_asset(bp_pawn)
    print("BP_MRPawn compiled and saved!")

# Update BP_MRSuitViewer to use BP_IronMan85 (or BP_MRSuit) and config
viewer_bp_path = f"{bp_dir}/BP_MRSuitViewer"
if EAL.does_asset_exist(viewer_bp_path):
    bp_viewer = EAL.load_asset(viewer_bp_path)
    cdo_viewer = unreal.get_default_object(bp_viewer.generated_class())
    ironman_class = bp.generated_class() if EAL.does_asset_exist(iron_man_bp_path) else bp_suit.generated_class()
    cdo_viewer.set_editor_property("suit_class", ironman_class)
    cdo_viewer.set_editor_property("suit_configuration", config)
    for prop, val in [("auto_place_suit", False), ("show_status_panel", False), ("show_floor_grid", False), ("show_scale_reference", False)]:
        try:
            cdo_viewer.set_editor_property(prop, val)
        except Exception as e:
            print(f"Note on {prop}: {e}")
    BEL.compile_blueprint(bp_viewer)
    EAL.save_loaded_asset(bp_viewer)
    print("BP_MRSuitViewer updated with BP_IronMan85 and DA_SuitConfiguration!")

print("All asset updates completed successfully.")
