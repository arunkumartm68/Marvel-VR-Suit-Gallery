"""
Bake the floor contact shadow (ground ambient occlusion) under the prepared Iron Spider for the MR contact shadow.

Run headless on the prepared .blend:
  blender -b IronSpider_Quest.blend --python bake_ground_ao.py -- <out.png> <size_m> [resolution]

The image covers a size_m x size_m square centred on the pivot (between the boots), u along +X, v along +Y in
Blender space. In Unreal this matches AMRSuit's contact-shadow plane with ContactShadowSize = size_m * 100 and
ContactShadowYaw = 0 (the plane turns with the suit's RotationOffset). White = open floor, dark = occluded.
"""
import bpy
import sys

args = sys.argv[sys.argv.index("--") + 1:]
out_png, size_m = args[0], float(args[1])
resolution = int(args[2]) if len(args) > 2 else 1024

scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 128
if not scene.world:
    scene.world = bpy.data.worlds.new("World")
scene.world.light_settings.distance = 0.9  # AO reach: soft halo under the body, dark blobs under boots and claw tips

bpy.ops.mesh.primitive_plane_add(size=size_m, location=(0.0, 0.0, 0.0005))
plane = bpy.context.active_object
image = bpy.data.images.new("GroundAO", resolution, resolution, alpha=False)
mat = bpy.data.materials.new("GroundAOBake")
mat.use_nodes = True
tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
tex.image = image
mat.node_tree.nodes.active = tex
plane.data.materials.append(mat)

bpy.ops.object.select_all(action="DESELECT")
plane.select_set(True)
bpy.context.view_layer.objects.active = plane
bpy.ops.object.bake(type="AO", margin=0, use_clear=True)
image.filepath_raw = out_png
image.file_format = "PNG"
image.save()
print("GROUND_AO_DONE", out_png)
