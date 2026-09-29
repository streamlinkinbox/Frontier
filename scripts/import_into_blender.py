"""Run locally with Blender 4.x/5.x to create an editable .blend from the asset.

blender --background --python scripts/import_into_blender.py

The GLB already contains the meshes, bone hierarchy, weights, images and three clips.
This utility is optional; it is not needed to use the browser viewer or GLB.
"""
from pathlib import Path
import bpy

ROOT = Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(ROOT / 'public/models/mantis.glb'))
bpy.context.scene.unit_settings.system = 'METRIC'
bpy.context.scene.unit_settings.scale_length = 1.0
bpy.context.scene.unit_settings.length_unit = 'MILLIMETERS'
bpy.context.scene.render.fps = 60
bpy.ops.file.pack_all()
out = ROOT / 'public/models/mantis.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(out))
print(f'Saved editable Blender project: {out}')
