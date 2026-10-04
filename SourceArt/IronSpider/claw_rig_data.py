"""Export the Iron Spider claw rig (joints, link vertices) and the body point cloud for claw_pose_search.py.

  blender -b --factory-startup --python claw_rig_data.py -- <source.glb> <out.npz>

The joints and the piece-to-link split match prepare_ironspider_for_quest.py (only the +x claws are exported; the
-x claws mirror them). The body is down-sampled to an 8 mm grid.
"""
import bpy
import math
import sys

import numpy as np
from mathutils import Matrix

src, out = sys.argv[sys.argv.index("--") + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
DUPLICATE_SHELLS = {"27_bodyenvgold_1_0_0", "27_webshooterenvred_1_0_0"}
meshes = []
for obj in list(bpy.context.scene.objects):
    if obj.type != "MESH":
        continue
    obj.data.transform(obj.matrix_world)
    obj.matrix_world = Matrix.Identity(4)
    if obj.material_slots[0].material.name in DUPLICATE_SHELLS:
        continue
    meshes.append(obj)


def coords(obj):
    flat = np.empty(len(obj.data.vertices) * 3)
    obj.data.vertices.foreach_get("co", flat)
    return flat.reshape(-1, 3)


mat_of = {o.material_slots[0].material.name: o for o in meshes}
claws = mat_of["24_-Spider_Legs.legs_0.2_0_0"]
boots = coords(mat_of["24_shoe_0.2_0_0"])
floor_z = boots[:, 2].min()
head_top = coords(mat_of["24_head_0.2_0_0"])[:, 2].max()
feet = boots[boots[:, 2] < floor_z + 0.03 * (head_top - floor_z)]
pivot = np.array(((feet[:, 0].min() + feet[:, 0].max()) / 2, (feet[:, 1].min() + feet[:, 1].max()) / 2, floor_z))

body = np.concatenate([coords(o) for o in meshes if o not in (claws, mat_of["27_-Spider_Legs.backplateenvblue_1_0_0.1"])])
# 8 mm voxel downsample of the body
keys = np.floor(body / 0.008).astype(np.int64)
_, first = np.unique(keys, axis=0, return_index=True)
body = body[first]

co = coords(claws)
me = claws.data
edges = np.empty(len(me.edges) * 2, dtype=np.int64)
me.edges.foreach_get("vertices", edges)
edges = edges.reshape(-1, 2)
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

CLAWS = {
    "upper": {"x": 0.0655, "balls": [(-0.1462, -0.2762), (0.0174, -0.2943)], "hinges": [(0.6555, -0.1856), (1.1192, 0.1606), (1.4093, 0.5046)]},
    "lower": {"x": 0.0745, "balls": [(-0.1637, -0.4385), (-0.0012, -0.4136)], "hinges": [(0.6409, -0.4963), (1.1183, -0.8234), (1.4220, -1.1554)]},
}


def chain_distance(cl):
    pts = [np.array(p) for p in cl["balls"] + cl["hinges"]]
    best = np.full(n_pieces, np.inf)
    for p0, p1 in zip(pts[:-1], pts[1:]):
        d = p1 - p0
        t = np.clip(((centre[:, 1:] - p0) @ d) / (d @ d), 0.0, 1.0)
        best = np.minimum(best, np.linalg.norm(centre[:, 1:] - (p0 + t[:, None] * d), axis=1))
    last = pts[-1]
    return np.where(centre[:, 1] > last[0], np.minimum(best, np.abs(centre[:, 2] - last[1])), best)


save = {"floor_z": floor_z, "head_top": head_top, "pivot": pivot, "body": body}
rng = np.random.default_rng(0)
for kind, claw in CLAWS.items():
    side = 1.0
    x = side * claw["x"]
    joints = [np.array((x, y, z)) for y, z in claw["balls"] + claw["hinges"]]
    members = (np.sign(centre[:, 0]) == side) & (chain_distance(claw) < chain_distance(CLAWS["lower" if kind == "upper" else "upper"]))
    dirs = [joints[i + 1] - joints[i] for i in range(4)]
    dirs = [d / np.linalg.norm(d) for d in dirs] + [np.array((0.0, 1.0, 0.0))]
    link = np.zeros(n_pieces, dtype=np.int64)
    for j in range(5):
        normal = dirs[0] if j == 0 else dirs[j - 1] + dirs[j]
        normal = normal / np.linalg.norm(normal)
        link += ((centre - joints[j]) @ normal > (-0.012 if j < 2 else 0.0)).astype(np.int64)
    link = np.where(members, link, -1)
    blade_verts = np.isin(piece, np.nonzero(link == 5)[0])
    tip = co[blade_verts][np.argmax(np.linalg.norm(co[blade_verts] - joints[4], axis=1))]
    save[kind + "_joints"] = np.array(joints + [tip])
    for i in range(6):
        v = co[np.isin(piece, np.nonzero(link == i)[0])]
        if i >= 1 and len(v) > 600:
            v = v[rng.choice(len(v), 600, replace=False)]
        save["%s_link%d" % (kind, i)] = v
    print(kind, "pieces per link", [int((link == i).sum()) for i in range(6)])
np.savez(out, **save)
print("RIG_SAVED", out, "body points", len(body), "pivot", pivot.round(3), "floor", round(floor_z, 4))
