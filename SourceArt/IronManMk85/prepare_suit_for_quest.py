"""
Prepare the Iron Man Mark 85 Sketchfab FBX for a life-size Quest 3S MR viewer.

Run headless:
  blender -b --factory-startup --python prepare_suit_for_quest.py -- <source.fbx> <out.fbx> <out.blend> <height_m>

Steps (geometry is otherwise left untouched):
  1. remove helper geometry: the fully transparent 15 cm chest polygon and ~89k loose vertices
  2. bake all object transforms (source is Y-up centimetres, 3.59 m tall)
  3. pivot = centre between the feet at floor level, then uniform scale to the requested real height
  4. reduce the three subdivided armour shells for standalone Quest (arc reactor + stones untouched)
  5. readable material names, single joined mesh, FBX export for Unreal (+ .blend for later edits)
"""
import bpy
import json
import math
import sys

import bmesh
import numpy as np
from mathutils import Matrix

src, out_fbx, out_blend, target_height = sys.argv[sys.argv.index("--") + 1:][:4]
target_height = float(target_height)

# Per-shell triangle ratios (source: 1.24M / 750k / 227k triangles).
DECIMATE = {
    "Subdivision_Surface_894": 0.10,  # dark under-suit, mostly hidden between plates
    "Subdivision_Surface_379": 0.15,  # red outer plates
    "Subdivision_Surface_893": 0.30,  # gold plates (faceplate, arms, thighs)
}
MATERIAL_NAMES = {
    "Mat": "Undersuit",
    "Mat.1": "RedArmor",
    "Mat.2": "GoldArmor",
    "Материал.6": "Glow",          # arc reactor + eyes
    "Материал": "StoneOrange",     # nano-gauntlet stones
    "Материал.1": "StonePurple",
    "Материал.2": "StoneMagenta",
    "Материал.3": "StoneBlue",
    "Материал.4": "StoneYellow",
    "Материал.5": "StoneGreen",
}

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
scene = bpy.context.scene

# 1. Helper geometry --------------------------------------------------------------------------
for obj in list(scene.objects):
    if obj.type != "MESH" or obj.name.startswith("Полигон"):  # alpha-0 polygon, invisible in the original
        bpy.data.objects.remove(obj, do_unlink=True)
meshes = [o for o in scene.objects if o.type == "MESH"]

bpy.ops.object.select_all(action="SELECT")
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.make_single_user(object=True, obdata=True)

# 2. Bake transforms ----------------------------------------------------------------------------
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

loose_removed = 0
for obj in meshes:
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    loose = [v for v in bm.verts if not v.link_faces]
    loose_removed += len(loose)
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bmesh.ops.delete(bm, geom=[e for e in bm.edges if not e.link_faces], context="EDGES")
    bm.to_mesh(obj.data)
    bm.free()


def coords(obj):
    flat = np.empty(len(obj.data.vertices) * 3, dtype=np.float64)
    obj.data.vertices.foreach_get("co", flat)
    return flat.reshape(-1, 3)


# 3. Pivot between the feet on the floor, scale to real height ----------------------------------
all_co = np.concatenate([coords(o) for o in meshes])
min_z, max_z = all_co[:, 2].min(), all_co[:, 2].max()
source_height = max_z - min_z
feet = all_co[all_co[:, 2] < min_z + 0.03 * source_height]
pivot_x = (feet[:, 0].min() + feet[:, 0].max()) / 2
pivot_y = (feet[:, 1].min() + feet[:, 1].max()) / 2
scale = target_height / source_height
to_origin = Matrix.Scale(scale, 4) @ Matrix.Translation((-pivot_x, -pivot_y, -min_z))
for obj in meshes:
    obj.data.transform(to_origin)
    obj.data.update()

# 4. Reduce the subdivided shells ---------------------------------------------------------------
triangles = {}
for obj in meshes:
    ratio = DECIMATE.get(obj.name)
    if ratio:
        mod = obj.modifiers.new("Decimate", "DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = ratio
        mod.use_collapse_triangulate = True
        evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        reduced = bpy.data.meshes.new_from_object(evaluated)
        obj.modifiers.clear()
        old = obj.data
        obj.data = reduced
        bpy.data.meshes.remove(old)
        reduced.shade_smooth()
        reduced.set_sharp_from_angle(angle=math.radians(40.0))
    obj.data.calc_loop_triangles()
    triangles[obj.name] = len(obj.data.loop_triangles)

# 5. Materials, join, export --------------------------------------------------------------------
for mat in bpy.data.materials:
    if mat.name in MATERIAL_NAMES:
        mat.name = MATERIAL_NAMES[mat.name]

bpy.ops.object.select_all(action="SELECT")
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.join()
suit = bpy.context.view_layer.objects.active
suit.name = suit.data.name = "SM_IronManMk85"

final = coords(suit)
feet = final[final[:, 2] < 0.03 * target_height]
left, right = feet[feet[:, 0] >= 0], feet[feet[:, 0] < 0]
report = {
    "source_height_m": round(float(source_height), 4),
    "scale": round(float(scale), 6),
    "loose_vertices_removed": loose_removed,
    "triangles_per_part": triangles,
    "triangles_total": int(sum(triangles.values())),
    "bbox_min_m": [round(float(v), 4) for v in final.min(axis=0)],
    "bbox_max_m": [round(float(v), 4) for v in final.max(axis=0)],
    "feet_centres_m": [[round(float(v), 4) for v in ((f[:, 0].min() + f[:, 0].max()) / 2, (f[:, 1].min() + f[:, 1].max()) / 2)] for f in (left, right) if len(f)],
    "feet_size_m": [[round(float(v), 4) for v in (f[:, 0].max() - f[:, 0].min(), f[:, 1].max() - f[:, 1].min())] for f in (left, right) if len(f)],
    "materials": [s.material.name for s in suit.material_slots if s.material],
}

bpy.ops.export_scene.fbx(
    filepath=out_fbx,
    use_selection=False,
    object_types={"MESH"},
    apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_UNITS",
    axis_forward="-Z",
    axis_up="Y",
    mesh_smooth_type="FACE",
    use_mesh_modifiers=True,
    add_leaf_bones=False,
    bake_anim=False,
    path_mode="STRIP",
)
bpy.ops.wm.save_as_mainfile(filepath=out_blend)
print("SUIT_REPORT=" + json.dumps(report, ensure_ascii=False))
