"""
Prepare the Iron Spider (MCU) Nomad Sculpt glb for the life-size Quest 3S MR viewer.

Run headless:
  blender -b --factory-startup --python prepare_ironspider_for_quest.py -- <source.glb> <out.fbx> <out.blend> <height_m> [preview_dir]

Steps (the suit itself is left untouched, only the four back claws are re-posed):
  1. bake the node transforms, drop the two shells that exactly duplicate body / web-shooter faces (they would z-fight)
  2. re-pose the four back claws from "straight back" into the movie X, reaching round to the front: each claw turns
     on its two back ball joints, then bends at its three hinges (upper pair over the shoulders with the hooked blades
     coming down in front, lower pair under the arms down to the floor in front), clear of the body
  3. pivot = centre between the boots at floor level, uniform scale so the body (boots to head) is the requested height;
     smooth shading with 40 degree hard edges (the glb has no normals, so it would import flat shaded)
  4. readable material names, single joined mesh, FBX export for Unreal (+ .blend for later edits, + preview renders)

Source facts (Blender space, Z up, the suit faces -Y): body 1.808 m tall, claws are loose mechanical pieces of
Object_353 lying in the planes x = +-0.0655 (upper pair) and x = +-0.0745 (lower pair), each a chain
back ball -> hinge 1 -> hinge 2 -> hinge 3 -> gold blade, all extending along +Y behind the back.
"""
import bpy
import json
import math
import os
import sys

import numpy as np
from mathutils import Matrix, Vector
from mathutils.kdtree import KDTree

args = sys.argv[sys.argv.index("--") + 1:]
src, out_fbx, out_blend, target_height = args[:4]
preview_dir = os.path.abspath(args[4]) if len(args) > 4 else None
target_height = float(target_height)

# Exact face duplicates of the body / web-shooter meshes with another material: they would z-fight in Unreal.
DUPLICATE_SHELLS = {"27_bodyenvgold_1_0_0", "27_webshooterenvred_1_0_0"}
MATERIAL_NAMES = {
    "24_body_0.2_0_0": "Body",
    "24_bodyred_0.2_0_0": "BodyRed",
    "24_head_0.2_0_0": "Head",
    "24_hand_0.2_0_0": "Hands",
    "24_leg_0.2_0_0": "Legs",
    "24_shoe_0.2_0_0": "Boots",
    "24_webshooter_0.2_0_0": "WebShooters",
    "24_-Spider_Legs.legs_0.2_0_0": "SpiderLegs",
    "27_-Spider_Legs.backplateenvblue_1_0_0.1": "Backplate",
    "24_webtile_0.2_0_0": "WebTile",
    "24_webs_0.2_0_0": "Webs",
    "24_lens_0.3_0_0": "Lens",
    "24_-Squint_Lens.a_0.3_0_0": "Lens",
    "21_eyeemission_1_0_0": "Eyes",
    "21_webshooterem_1_0_0": "WebShooterGlow",
    "21_bodyem_1_0_0": "ChestGlow",
    "21_shoeem_1_0_0": "BootTrim",
}
CLAW_MATERIAL = "24_-Spider_Legs.legs_0.2_0_0"
BOOTS_MATERIAL = "24_shoe_0.2_0_0"
HEAD_MATERIAL = "24_head_0.2_0_0"
BACKPLATE_MATERIAL = "27_-Spider_Legs.backplateenvblue_1_0_0.1"

# Joint centres (y, z) measured from the round joint caps, and the claw plane |x|. Each claw is a chain
# ball 1 (on the backplate) -> ball 2 -> hinge 1 -> hinge 2 -> hinge 3 -> gold blade.
CLAWS = {
    "upper": {"x": 0.0655, "balls": [(-0.1462, -0.2762), (0.0174, -0.2943)],
              "hinges": [(0.6555, -0.1856), (1.1192, 0.1606), (1.4093, 0.5046)]},
    "lower": {"x": 0.0745, "balls": [(-0.1637, -0.4385), (-0.0012, -0.4136)],
              "hinges": [(0.6409, -0.4963), (1.1183, -0.8234), (1.4220, -1.1554)]},
}
# Movie pose with the claws reaching round to the FRONT (front view: the X). Values come from claw_rig_data.py +
# claw_pose_search.py, which keep the claws about 8 cm off the body and 9 cm apart. Angles in degrees, for the claws on
# the +x side; the -x claws mirror them. root_yaw / root_elevation aim the short ball 1 -> ball 2 link (yaw 0 = straight
# back, 90 = out to the claw's own side, 180 = straight ahead). From ball 2 on, the claw is a flat hinge chain: yaw and
# roll orient that plane, e2..e5 are the in-plane angles of the links ball 2 -> hinge 1, hinge 1 -> 2, hinge 2 -> 3 and
# the blade.
POSE = {
    "upper": {"root_yaw": 48.91, "root_elevation": 4.97, "yaw": 133.54, "roll": -4.01, "e": [59.05, 21.75, -1.57, -58.54]},
    "lower": {"root_yaw": 10.03, "root_elevation": 2.99, "yaw": 121.56, "roll": -17.52, "e": [-0.74, -43.86, -63.51, None]},  # None: blade rests on the floor
}
FLOOR_CLEARANCE = 0.004  # m above the floor for the lowest blade point (before scaling)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
scene = bpy.context.scene

# 1. Bake transforms, drop helpers and duplicate shells ------------------------------------------
meshes = []
for obj in list(scene.objects):
    if obj.type != "MESH":
        continue
    obj.data.transform(obj.matrix_world)
    obj.parent = None
    obj.matrix_world = Matrix.Identity(4)
    if obj.material_slots and obj.material_slots[0].material and obj.material_slots[0].material.name in DUPLICATE_SHELLS:
        bpy.data.objects.remove(obj, do_unlink=True)
        continue
    meshes.append(obj)
for obj in list(scene.objects):
    if obj.type != "MESH":
        bpy.data.objects.remove(obj, do_unlink=True)


def by_material(name):
    return next(o for o in meshes if o.material_slots[0].material.name == name)


def coords(obj):
    flat = np.empty(len(obj.data.vertices) * 3, dtype=np.float64)
    obj.data.vertices.foreach_get("co", flat)
    return flat.reshape(-1, 3)


def set_coords(obj, co):
    obj.data.vertices.foreach_set("co", co.reshape(-1).astype(np.float32))
    obj.data.update()


boots = coords(by_material(BOOTS_MATERIAL))
floor_z = boots[:, 2].min()
head_top = coords(by_material(HEAD_MATERIAL))[:, 2].max()
source_height = head_top - floor_z

# 2. Re-pose the claws -----------------------------------------------------------------------------
claws = by_material(CLAW_MATERIAL)
me = claws.data
co = coords(claws)
edges = np.empty(len(me.edges) * 2, dtype=np.int64)
me.edges.foreach_get("vertices", edges)
edges = edges.reshape(-1, 2)

# Loose pieces (union-find over edges); every piece moves rigidly with the link it belongs to.
parent = np.arange(len(co))


def find(i):
    root = i
    while parent[root] != root:
        root = parent[root]
    while parent[i] != root:
        parent[i], i = root, parent[i]
    return root


for a, b in edges:
    ra, rb = find(a), find(b)
    if ra != rb:
        parent[ra] = rb
_, piece = np.unique([find(i) for i in range(len(co))], return_inverse=True)
n_pieces = piece.max() + 1
centre = np.zeros((n_pieces, 3))
np.add.at(centre, piece, co)
centre /= np.bincount(piece)[:, None]


def rot_x(deg):
    return np.array(Matrix.Rotation(math.radians(deg), 3, "X"))


def rot_y(deg):
    return np.array(Matrix.Rotation(math.radians(deg), 3, "Y"))


def rot_z(deg):
    return np.array(Matrix.Rotation(math.radians(deg), 3, "Z"))


def chain_distance(cl):
    """Distance of every piece centre (in the y-z plane) to a claw's joint chain; the blade continues along +y."""
    pts = [np.array(p) for p in cl["balls"] + cl["hinges"]]
    best = np.full(n_pieces, np.inf)
    for p0, p1 in zip(pts[:-1], pts[1:]):
        d = p1 - p0
        t = np.clip(((centre[:, 1:] - p0) @ d) / (d @ d), 0.0, 1.0)
        best = np.minimum(best, np.linalg.norm(centre[:, 1:] - (p0 + t[:, None] * d), axis=1))
    last = pts[-1]
    return np.where(centre[:, 1] > last[0], np.minimum(best, np.abs(centre[:, 2] - last[1])), best)


report_claws = {}
new_co = co.copy()
moved_by_claw = {}
link_verts = {}  # claw -> vertex indices of links 0..5
for kind, claw in CLAWS.items():
    pose = POSE[kind]
    other = CLAWS["lower" if kind == "upper" else "upper"]
    for side in (-1.0, 1.0):
        x = side * claw["x"]
        joints = [np.array((x, y, z)) for y, z in claw["balls"] + claw["hinges"]]
        # Pieces of this claw: on this side, nearer to this claw's chain than to the other claw's.
        members = (np.sign(centre[:, 0]) == side) & (chain_distance(claw) < chain_distance(other))

        # Link per piece: 0 = mount on the backplate (stays), 1 = ball 1 -> ball 2, 2..4 = links ending at hinges 1..3,
        # 5 = blade. Boundaries are planes through each joint, normal to the bisector of the links meeting there; the
        # ball planes sit 1.2 cm towards the back so each ball (centred on its joint) turns with the link after it.
        dirs = [joints[i + 1] - joints[i] for i in range(4)]
        dirs = [d / np.linalg.norm(d) for d in dirs] + [np.array((0.0, 1.0, 0.0))]
        link = np.zeros(n_pieces, dtype=np.int64)
        for j in range(5):
            normal = dirs[0] if j == 0 else dirs[j - 1] + dirs[j]
            normal = normal / np.linalg.norm(normal)
            link += ((centre - joints[j]) @ normal > (-0.012 if j < 2 else 0.0)).astype(np.int64)
        link = np.where(members, link, -1)

        # Blade tip (farthest blade vertex from hinge 3) to measure the blade link's own direction.
        blade_verts = np.isin(piece, np.nonzero(link == 5)[0])
        tip = co[blade_verts][np.argmax(np.linalg.norm(co[blade_verts] - joints[4], axis=1))]
        chain = joints + [tip]
        old = [math.degrees(math.atan2(chain[i + 1][2] - chain[i][2], chain[i + 1][1] - chain[i][1])) for i in range(5)]
        angles = list(pose["e"])

        def place(e):
            """Transforms (R, old_origin, new_origin) for links 1..5; the -x claws mirror the +x pose."""
            root = rot_z(-side * pose["root_yaw"]) @ rot_x(pose["root_elevation"] - old[0])
            out = [(root, chain[0], chain[0])]
            new_joint = chain[0] + root @ (chain[1] - chain[0])
            plane = rot_z(-side * pose["yaw"]) @ rot_y(side * pose["roll"])
            for i in range(1, 5):
                R = plane @ rot_x(e[i - 1] - old[i])
                out.append((R, chain[i], new_joint))
                new_joint = new_joint + R @ (chain[i + 1] - chain[i])
            return out

        if angles[3] is None:
            # Solve the blade angle so the lowest blade vertex rests on the floor.
            blade_co = co[blade_verts]
            lo_e, hi_e = -100.0, 30.0
            for _ in range(60):
                mid = (lo_e + hi_e) / 2
                R, o, n = place(angles[:3] + [mid])[4]
                lowest = ((blade_co - o) @ R.T + n)[:, 2].min()
                if lowest < floor_z + FLOOR_CLEARANCE:
                    lo_e = mid  # too low: raise the blade
                else:
                    hi_e = mid
            angles[3] = (lo_e + hi_e) / 2
        for i, (R, o, n) in enumerate(place(angles), start=1):
            verts = np.isin(piece, np.nonzero(link == i)[0])
            new_co[verts] = (co[verts] - o) @ R.T + n
        moved = np.isin(piece, np.nonzero(link >= 1)[0])
        key = "%s_%s" % (kind, "left" if side > 0 else "right")
        moved_by_claw[key] = moved
        link_verts[key] = [np.nonzero(np.isin(piece, np.nonzero(link == i)[0]))[0] for i in range(6)]
        report_claws[key] = {
            "pieces_per_link": [int((link == i).sum()) for i in range(6)],
            "blade_angle": round(angles[3], 1),
            "posed_min": [round(float(v), 3) for v in new_co[moved].min(0)],
            "posed_max": [round(float(v), 3) for v in new_co[moved].max(0)],
        }

# How close the posed claws come to the body (cm, before scaling).
body_tree = KDTree(sum(len(o.data.vertices) for o in meshes if o not in (claws, by_material(BACKPLATE_MATERIAL))))
k = 0
for o in meshes:
    if o in (claws, by_material(BACKPLATE_MATERIAL)):
        continue
    for v in coords(o):
        body_tree.insert(v, k)
        k += 1
body_tree.balance()
for key, info in report_claws.items():
    claw_pts = new_co[moved_by_claw[key]]
    claw_pts = claw_pts[:: max(1, len(claw_pts) // 4000)]
    info["closest_to_body_cm"] = round(min(body_tree.find(p)[2] for p in claw_pts) * 100, 1)
set_coords(claws, new_co)

# 3. Pivot between the boots on the floor, scale the body to the real height ------------------------
feet = boots[boots[:, 2] < floor_z + 0.03 * source_height]
pivot_x = (feet[:, 0].min() + feet[:, 0].max()) / 2
pivot_y = (feet[:, 1].min() + feet[:, 1].max()) / 2
scale = target_height / source_height
to_origin = Matrix.Scale(scale, 4) @ Matrix.Translation((-pivot_x, -pivot_y, -floor_z))
triangles = {}
for obj in meshes:
    obj.data.transform(to_origin)
    obj.data.update()
    # The glb has no normals, so it imports flat shaded (every triangle its own vertices in Unreal):
    # smooth it like Nomad shows it, keeping hard edges on the mechanical parts.
    obj.data.shade_smooth()
    obj.data.set_sharp_from_angle(angle=math.radians(40.0))
    obj.data.calc_loop_triangles()
    triangles[obj.material_slots[0].material.name] = len(obj.data.loop_triangles)

# Touch outline for grabbing in the MR app: capsules that tightly contain every vertex, so a pinch only takes hold on
# the model itself and not in the empty space around the spread legs (the box around the whole model is 2.6 m wide).


def capsule_cover(pts, max_radius, depth=0):
    """(start, end, radius) capsules containing all points, split until each is at most max_radius thick."""
    centre = pts.mean(0)
    _, _, axes = np.linalg.svd(pts - centre, full_matrices=False)
    t = (pts - centre) @ axes[0]
    perp = np.linalg.norm((pts - centre) - np.outer(t, axes[0]), axis=1)
    radius = perp.max()
    if radius > max_radius and depth < 6 and len(pts) >= 40:
        # Long parts are cut across their length, short wide ones (the torso) side by side.
        split_axis = axes[0] if t.max() - t.min() > 2.5 * radius else axes[1]
        side = (pts - centre) @ split_axis
        half = side <= np.median(side)
        if half.any() and (~half).any():
            return capsule_cover(pts[half], max_radius, depth + 1) + capsule_cover(pts[~half], max_radius, depth + 1)
    # Shortest segment that keeps every point within the radius (the round caps cover the ends).
    slack = np.sqrt(np.maximum(radius * radius - perp * perp, 0.0))
    t0, t1 = (t + slack).min(), (t - slack).max()
    if t0 > t1:
        t0 = t1 = (t.min() + t.max()) / 2
        radius = np.linalg.norm(pts - (centre + axes[0] * t0), axis=1).max()
    return [(centre + axes[0] * t0, centre + axes[0] * t1, float(radius))]


claw_co = coords(claws)
backplate = by_material(BACKPLATE_MATERIAL)
body_co = np.concatenate([coords(o) for o in meshes if o not in (claws, backplate)])
x, z = body_co[:, 0], body_co[:, 2]
neck, crotch, arm_x = 0.84 * target_height, 0.465 * target_height, 0.21  # A-pose landmarks
parts = {
    "head": (body_co[(z > neck) & (np.abs(x) <= arm_x)], 0.12),
    "torso": (body_co[(z > crotch) & (z <= neck) & (np.abs(x) <= arm_x)], 0.15),
    "arm_left": (body_co[(z > crotch) & (x > arm_x)], 0.07),
    "arm_right": (body_co[(z > crotch) & (x < -arm_x)], 0.07),
    "leg_left": (body_co[(z <= crotch) & (x >= 0)], 0.09),
    "leg_right": (body_co[(z <= crotch) & (x < 0)], 0.09),
    "backplate": (np.concatenate([coords(backplate)] + [claw_co[v[0]] for v in link_verts.values()]), 0.06),
}
for key, verts in link_verts.items():
    for i in range(1, 6):
        parts["%s_link%d" % (key, i)] = (claw_co[verts[i]], 0.045)
capsules = []
for name, (pts, max_radius) in parts.items():
    capsules += capsule_cover(pts, max_radius)

# Every vertex must be inside the outline (gap <= 0).
all_co = np.concatenate([coords(o) for o in meshes])
gaps = np.full(len(all_co), np.inf)
for start, end, radius in capsules:
    d = end - start
    tt = np.clip(((all_co - start) @ d) / max(d @ d, 1e-12), 0.0, 1.0)
    gaps = np.minimum(gaps, np.linalg.norm(all_co - (start + tt[:, None] * d), axis=1) - radius)
# Unreal mesh space: centimetres, Blender -Y (the suit's front) is Unreal +Y.
to_unreal = lambda v: [round(float(v[0]) * 100, 2), round(float(-v[1]) * 100, 2), round(float(v[2]) * 100, 2)]
grab_capsules = [{"Start": to_unreal(a), "End": to_unreal(b), "Radius": round(r * 100, 2)} for a, b, r in capsules]
with open(os.path.join(os.path.dirname(os.path.abspath(out_fbx)), "IronSpider_grab_capsules.json"), "w") as f:
    json.dump(grab_capsules, f, indent=1)

# 4. Materials, join, export ----------------------------------------------------------------------------
# Lens and squint lens share one look: merge them into one material slot.
renamed = {}
for obj in meshes:
    mat = obj.material_slots[0].material
    new_name = MATERIAL_NAMES.get(mat.name)
    if not new_name:
        continue
    if new_name in renamed and renamed[new_name] is not mat:
        obj.material_slots[0].material = renamed[new_name]
    else:
        mat.name = new_name
        renamed[new_name] = mat

bpy.ops.object.select_all(action="SELECT")
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.join()
suit = bpy.context.view_layer.objects.active
suit.name = suit.data.name = "SM_IronSpider"
# Keep only the UV map: the constant per-mesh vertex colours/PBR values become material instance parameters.
for attr in list(suit.data.color_attributes):
    suit.data.color_attributes.remove(attr)

final = coords(suit)
report = {
    "source_height_m": round(float(source_height), 4),
    "scale": round(float(scale), 6),
    "triangles_per_material": triangles,
    "triangles_total": int(sum(triangles.values())),
    "bbox_min_m": [round(float(v), 4) for v in final.min(axis=0)],
    "bbox_max_m": [round(float(v), 4) for v in final.max(axis=0)],
    "materials": [s.material.name for s in suit.material_slots if s.material],
    "claws": report_claws,
    "grab_capsules": len(grab_capsules),
    "grab_outline_worst_gap_cm": round(float(gaps.max()) * 100, 3),
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

# Preview renders (Workbench, base-colour textures) to check the pose before importing.
if preview_dir:
    os.makedirs(preview_dir, exist_ok=True)
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
    scene.render.resolution_x = 900
    scene.render.resolution_y = 900
    lo, hi = Vector(final.min(axis=0)), Vector(final.max(axis=0))
    centre_v = (lo + hi) / 2
    size = max(hi - lo)
    cam_data = bpy.data.cameras.new("PreviewCamera")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = size * 1.08
    cam = bpy.data.objects.new("PreviewCamera", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    views = {
        "front": Vector((0, -1, 0)),
        "side": Vector((1, 0, 0)),
        "back": Vector((0, 1, 0)),
        "top": Vector((0, 0, 1)),
        "three_quarter": Vector((0.8, -1.0, 0.35)).normalized(),
    }
    for name, direction in views.items():
        cam.location = centre_v + direction * size * 3
        cam.rotation_euler = (centre_v - cam.location).to_track_quat("-Z", "Y").to_euler()
        if name == "top":
            cam.rotation_euler = (0, 0, 0)
        scene.render.filepath = os.path.join(preview_dir, "%s.png" % name)
        bpy.ops.render.render(write_still=True)
    print("PREVIEWS_DONE")
