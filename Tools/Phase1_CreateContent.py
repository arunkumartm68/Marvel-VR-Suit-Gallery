"""
Phase 1 content for the Life-Size Suit MR viewer.

Creates (and re-creates, if run again):
  /Game/MRSuitViewer/Materials/M_MR_FloorGrid        translucent world-space grid for the floor test aid
  /Game/MRSuitViewer/Materials/M_MR_ReferenceUnlit   unlit shaded material for the 100 cm reference cube
  /Game/MRSuitViewer/Blueprints/BP_MRPawn            MR pawn (tracked head + controllers, no locomotion)
  /Game/MRSuitViewer/Blueprints/BP_MRGameMode        game mode using BP_MRPawn
  /Game/MRSuitViewer/Blueprints/BP_MRSuitViewer      MR session: passthrough, room scan, floor detection
  /Game/MRSuitViewer/Maps/L_MRSuitViewer             empty MR level (PlayerStart + BP_MRSuitViewer)

Run inside the Unreal Editor: Tools > Execute Python Script... and pick this file.
"""
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
BEL = unreal.BlueprintEditorLibrary
ROOT = "/Game/MRSuitViewer"
MAT_DIR = f"{ROOT}/Materials"
BP_DIR = f"{ROOT}/Blueprints"
MAP_PATH = f"{ROOT}/Maps/L_MRSuitViewer"

# Recommended structure. Empty folders are created for later phases so the layout is visible now.
for sub in ("Blueprints", "Maps", "Materials", "Data", "Suit", "UI", "Input"):
    EAL.make_directory(f"{ROOT}/{sub}")


# ============================================================================ materials
def new_material(name):
    path = f"{MAT_DIR}/{name}"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return AT.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def node(mat, cls, x, y, **props):
    expr = MEL.create_material_expression(mat, cls, x, y)
    for key, value in props.items():
        expr.set_editor_property(key, value)
    return expr


def link(src, dst, dst_input="", src_output=""):
    if not MEL.connect_material_expressions(src, src_output, dst, dst_input):
        raise RuntimeError(f"connect failed: {src.get_name()}.{src_output} -> {dst.get_name()}.{dst_input}")


def scalar(mat, name, value, x, y):
    return node(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def binary(mat, cls, a, b, x, y):
    expr = node(mat, cls, x, y)
    link(a, expr, "A")
    link(b, expr, "B")
    return expr


def unary(mat, cls, a, x, y):
    expr = node(mat, cls, x, y)
    link(a, expr)
    return expr


def mask(mat, src, x, y, r=False, g=False, b=False):
    expr = node(mat, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=False)
    link(src, expr)
    return expr


# M_MR_FloorGrid: unlit translucent grid in world space (25 cm cells), fading out radially from the component origin.
grid = new_material("M_MR_FloorGrid")
grid.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
grid.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

world_pos = node(grid, unreal.MaterialExpressionWorldPosition, -1800, 0)
world_xy = mask(grid, world_pos, -1600, 0, r=True, g=True)
cell = scalar(grid, "GridSize", 25.0, -1600, 150)
cell_uv = binary(grid, unreal.MaterialExpressionDivide, world_xy, cell, -1400, 0)
frac = unary(grid, unreal.MaterialExpressionFrac, cell_uv, -1250, 0)
inv = unary(grid, unreal.MaterialExpressionOneMinus, frac, -1100, 80)
edge_uv = binary(grid, unreal.MaterialExpressionMin, frac, inv, -950, 0)
edge_cm = binary(grid, unreal.MaterialExpressionMultiply, edge_uv, cell, -800, 0)
edge_x = mask(grid, edge_cm, -650, -60, r=True)
edge_y = mask(grid, edge_cm, -650, 60, g=True)
edge = binary(grid, unreal.MaterialExpressionMin, edge_x, edge_y, -500, 0)
line_width = scalar(grid, "LineWidth", 0.6, -500, 150)
line_t = binary(grid, unreal.MaterialExpressionDivide, edge, line_width, -350, 0)
line_sat = unary(grid, unreal.MaterialExpressionSaturate, line_t, -200, 0)
line = unary(grid, unreal.MaterialExpressionOneMinus, line_sat, -50, 0)
fill = scalar(grid, "FillOpacity", 0.06, -50, 120)
line_fill = binary(grid, unreal.MaterialExpressionMax, line, fill, 100, 0)

obj_pos = node(grid, unreal.MaterialExpressionObjectPositionWS, -800, 350)
obj_xy = mask(grid, obj_pos, -650, 350, r=True, g=True)
dist = binary(grid, unreal.MaterialExpressionDistance, world_xy, obj_xy, -500, 350)
radius = scalar(grid, "FadeRadius", 150.0, -500, 480)
fade_t = binary(grid, unreal.MaterialExpressionDivide, dist, radius, -350, 350)
fade_sat = unary(grid, unreal.MaterialExpressionSaturate, fade_t, -200, 350)
fade = unary(grid, unreal.MaterialExpressionOneMinus, fade_sat, -50, 350)

opacity_raw = binary(grid, unreal.MaterialExpressionMultiply, line_fill, fade, 250, 100)
opacity_scale = scalar(grid, "Opacity", 0.9, 250, 250)
opacity = binary(grid, unreal.MaterialExpressionMultiply, opacity_raw, opacity_scale, 400, 100)
color = node(grid, unreal.MaterialExpressionVectorParameter, 250, -150, parameter_name="Color", default_value=unreal.LinearColor(0.0, 0.85, 1.0, 1.0))

MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
MEL.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
MEL.recompile_material(grid)
EAL.save_loaded_asset(grid)

# M_MR_ReferenceUnlit: unlit opaque colour with fake per-face shading and dark edge lines, so the
# 100 cm reference cube reads clearly in passthrough without any scene lights.
ref = new_material("M_MR_ReferenceUnlit")
ref.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

normal = node(ref, unreal.MaterialExpressionVertexNormalWS, -1100, -150)
light_dir = node(ref, unreal.MaterialExpressionConstant3Vector, -1100, 0, constant=unreal.LinearColor(0.35, 0.45, 0.82, 1.0))
ndotl = binary(ref, unreal.MaterialExpressionDotProduct, normal, light_dir, -900, -100)
ndotl_sat = unary(ref, unreal.MaterialExpressionSaturate, ndotl, -750, -100)
shade = node(ref, unreal.MaterialExpressionLinearInterpolate, -600, -100, const_a=0.45, const_b=1.0)
link(ndotl_sat, shade, "Alpha")

uv = node(ref, unreal.MaterialExpressionTextureCoordinate, -1100, 250)
uv_inv = unary(ref, unreal.MaterialExpressionOneMinus, uv, -950, 320)
uv_edge = binary(ref, unreal.MaterialExpressionMin, uv, uv_inv, -800, 250)
uv_edge_u = mask(ref, uv_edge, -650, 200, r=True)
uv_edge_v = mask(ref, uv_edge, -650, 320, g=True)
uv_edge_min = binary(ref, unreal.MaterialExpressionMin, uv_edge_u, uv_edge_v, -500, 250)
edge_width = scalar(ref, "EdgeWidth", 0.015, -500, 380)
edge_t = binary(ref, unreal.MaterialExpressionDivide, uv_edge_min, edge_width, -350, 250)
edge_sat = unary(ref, unreal.MaterialExpressionSaturate, edge_t, -200, 250)
edge_shade = node(ref, unreal.MaterialExpressionLinearInterpolate, -50, 250, const_a=0.2, const_b=1.0)
link(edge_sat, edge_shade, "Alpha")

base = node(ref, unreal.MaterialExpressionVectorParameter, -450, -300, parameter_name="Color", default_value=unreal.LinearColor(0.95, 0.42, 0.06, 1.0))
lit = binary(ref, unreal.MaterialExpressionMultiply, base, shade, -300, -150)
final = binary(ref, unreal.MaterialExpressionMultiply, lit, edge_shade, 150, 0)
MEL.connect_material_property(final, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
MEL.recompile_material(ref)
EAL.save_loaded_asset(ref)


# ============================================================================ blueprints
def make_blueprint(name, parent_class):
    path = f"{BP_DIR}/{name}"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    bp = AT.create_asset(name, BP_DIR, unreal.Blueprint, factory)
    BEL.compile_blueprint(bp)
    return bp


def set_defaults(bp, **values):
    cdo = unreal.get_default_object(bp.generated_class())
    for key, value in values.items():
        cdo.set_editor_property(key, value)
    BEL.compile_blueprint(bp)
    EAL.save_loaded_asset(bp)


pawn_bp = make_blueprint("BP_MRPawn", unreal.MRViewerPawn.static_class())
EAL.save_loaded_asset(pawn_bp)

game_mode_bp = make_blueprint("BP_MRGameMode", unreal.GameModeBase.static_class())
set_defaults(game_mode_bp, default_pawn_class=pawn_bp.generated_class())

viewer_bp = make_blueprint("BP_MRSuitViewer", unreal.MRSuitViewer.static_class())
set_defaults(viewer_bp, floor_grid_material=grid, scale_reference_material=ref)


# ============================================================================ MR level
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if EAL.does_asset_exist(MAP_PATH):
    level_editor.load_level(MAP_PATH)
    for actor in actors.get_all_level_actors():
        if isinstance(actor, (unreal.PlayerStart, unreal.MRSuitViewer)):
            actors.destroy_actor(actor)
else:
    level_editor.new_level(MAP_PATH)

# Empty level on purpose: no sky, floor, fog or lights, so every background pixel shows passthrough.
player_start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
player_start.set_actor_label("PlayerStart_TrackingOrigin")
viewer = actors.spawn_actor_from_class(viewer_bp.generated_class(), unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
viewer.set_actor_label("MRSuitViewer")

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = world.get_world_settings()
settings.set_editor_property("default_game_mode", game_mode_bp.generated_class())
settings.set_editor_property("force_no_precomputed_lighting", True)
level_editor.save_current_level()

print("PHASE1_CONTENT_OK")
for path in (f"{MAT_DIR}/M_MR_FloorGrid", f"{MAT_DIR}/M_MR_ReferenceUnlit", f"{BP_DIR}/BP_MRPawn", f"{BP_DIR}/BP_MRGameMode", f"{BP_DIR}/BP_MRSuitViewer", MAP_PATH):
    print("  ", path, EAL.does_asset_exist(path))
print("   level actors:", [a.get_actor_label() for a in actors.get_all_level_actors()])
