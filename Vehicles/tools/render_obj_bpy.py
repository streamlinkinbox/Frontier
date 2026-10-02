import bpy, sys, math
obj_path, out = sys.argv[-2], sys.argv[-1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=obj_path, forward_axis='Y', up_axis='Z')
ob = bpy.context.selected_objects[0]
for p in ob.data.polygons: p.use_smooth = False
wire = ob.copy(); wire.data = ob.data.copy(); sc0 = bpy.context.scene; sc0.collection.objects.link(wire)
wm = wire.modifiers.new('w', 'WIREFRAME'); wm.thickness = 0.25; wm.use_replace = True
wmat = bpy.data.materials.new('wire'); wmat.diffuse_color = (0.95, 0.55, 0.05, 1); wire.data.materials.clear(); wire.data.materials.append(wmat)
mat = bpy.data.materials.new('rubber'); mat.diffuse_color = (0.25,0.25,0.26,1); ob.data.materials.append(mat)
sun = bpy.data.lights.new('s','SUN'); sun.energy = 4; so = bpy.data.objects.new('sun', sun); bpy.context.scene.collection.objects.link(so); so.rotation_euler = (0.6, 0.3, 0.4)
w = bpy.data.worlds.new('w'); bpy.context.scene.world = w; w.use_nodes = True; w.node_tree.nodes['Background'].inputs[0].default_value = (0.6,0.6,0.65,1); w.node_tree.nodes['Background'].inputs[1].default_value = 1.0
sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = 16; sc.cycles.device='CPU'; sc.cycles.use_denoising=False
sc.display.shading.light = 'STUDIO'; sc.display.shading.color_type = 'MATERIAL'
sc.display.shading.show_cavity = True
sc.render.resolution_x, sc.render.resolution_y = 1920, 1200
cam = bpy.data.cameras.new('c'); co = bpy.data.objects.new('cam', cam); sc.collection.objects.link(co); sc.camera = co
def shot(loc, target, name, ortho=None, wire=False):
    co.location = loc
    d = [t-l for t,l in zip(target, loc)]
    import mathutils
    co.rotation_euler = mathutils.Vector(d).to_track_quat('-Z','Y').to_euler()
    if ortho: cam.type='ORTHO'; cam.ortho_scale=ortho
    else: cam.type='PERSP'; cam.lens=50
    sc.display.shading.show_object_outline = False
    ob.show_wire = wire; sc.display.shading.show_xray = False
    # workbench wireframe overlay via viewport-independent: use freestyle-free approach -> wire material trick not available; use solid + outline
    sc.render.filepath = f'{out}/{name}.png'; bpy.ops.render.render(write_still=True)
# top of tread is +Z (y axis = wheel axis); the OBJ is in mm
shot((0, 0, 700), (0, 0, 330), 'Quads_01_TopClose', ortho=120)
wire.hide_render = True
shot((300, -350, 620), (0, 0, 320), 'Quads_02_Quarter')
shot((900, 0, 0), (0, 0, 0), 'Quads_03_Side', ortho=760)
shot((0, 0, 1200), (0, 0, 0), 'Quads_04_Face', ortho=760)
