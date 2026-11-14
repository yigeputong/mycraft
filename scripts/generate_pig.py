"""
方块猪程序化生成器 (Procedural Pig Generator)

适配版本：Blender 4.4+ (包含新版 Slotted Actions API)
用法：
    1. 打开 Blender，清空默认场景（按 A 全选，按 X 删除）。
    2. 切换到 Scripting 标签页，新建脚本并粘贴此代码。
    3. 点击右上角“运行脚本 (Run Script)”。
    4. 选中猪的所有部件，导出 glTF 2.0 (.glb)，勾选：
       - 格式: glTF Binary (.glb)
       - 包含: 已选中的物体
       - 变换: +Y Up
       - 数据: 动画 (Animations)
    5. 将导出的 pig.glb 放入 assets/models/ 目录中。

原理：
    1. 硬编码了 texOffs 和 addBox 参数来生成几何体与 UV。
    2. 使用 3D 游标强设物体原点（Pivot）在大腿根部。
    3. 自动 K 帧，生成 40 帧的交替步行循环动画，并强制使用线性插值。
"""

import bpy
import bmesh
import math

IMG_W, IMG_H = 64.0, 64.0

def build_box(name, tex_u, tex_v, w, h, d, loc_x, loc_y, loc_z):
    if name in bpy.data.objects:
        bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)
        
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    
    bm = bmesh.new()
    bw = w / 16.0
    bh = d / 16.0  # Blender Y
    bd = h / 16.0  # Blender Z
    
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(bw, bh, bd), verts=bm.verts)
    
    uv_layer = bm.loops.layers.uv.verify()
    
    def get_uv(x, y):
        return (x / IMG_W, 1.0 - (y / IMG_H))

    # 硬编码 UV 映射（基于 64x32 猪纹理）
    if name == "Head":
        uv_f = [get_uv(8,16), get_uv(16,16), get_uv(16,8), get_uv(8,8)]
        uv_b = [get_uv(24,16), get_uv(32,16), get_uv(32,8), get_uv(24,8)]
        uv_r = [get_uv(0,16), get_uv(8,16), get_uv(8,8), get_uv(0,8)]
        uv_l = [get_uv(16,16), get_uv(24,16), get_uv(24,8), get_uv(16,8)]
        uv_t = [get_uv(8,8), get_uv(16,8), get_uv(16,0), get_uv(8,0)]
        uv_btm = [get_uv(16,8), get_uv(24,8), get_uv(24,0), get_uv(16,0)]
    elif name == "Body":
        uv_f = [get_uv(36,32), get_uv(46,32), get_uv(46,16), get_uv(36,16)]
        uv_b = [get_uv(52,32), get_uv(62,32), get_uv(62,16), get_uv(52,16)]
        uv_r = [get_uv(28,32), get_uv(36,32), get_uv(36,16), get_uv(28,16)]
        uv_l = [get_uv(44,32), get_uv(52,32), get_uv(52,16), get_uv(44,16)]
        uv_t = [get_uv(36,16), get_uv(46,16), get_uv(46,8), get_uv(36,8)]
        uv_btm = [get_uv(46,16), get_uv(56,16), get_uv(56,8), get_uv(46,8)]
    elif name == "Snout":
        uv_f = [get_uv(17,20), get_uv(21,20), get_uv(21,17), get_uv(17,17)]
        uv_b = [get_uv(22,20), get_uv(26,20), get_uv(26,17), get_uv(22,17)]
        uv_r = [get_uv(16,20), get_uv(17,20), get_uv(17,17), get_uv(16,17)]
        uv_l = [get_uv(21,20), get_uv(22,20), get_uv(22,17), get_uv(21,17)]
        uv_t = [get_uv(17,17), get_uv(21,17), get_uv(21,16), get_uv(17,16)]
        uv_btm = [get_uv(21,17), get_uv(25,17), get_uv(25,16), get_uv(21,16)]
    else: # Legs
        uv_f = [get_uv(4,26), get_uv(8,26), get_uv(8,20), get_uv(4,20)]
        uv_b = [get_uv(12,26), get_uv(16,26), get_uv(16,20), get_uv(12,20)]
        uv_r = [get_uv(0,26), get_uv(4,26), get_uv(4,20), get_uv(0,20)]
        uv_l = [get_uv(8,26), get_uv(12,26), get_uv(12,20), get_uv(8,20)]
        uv_t = [get_uv(4,20), get_uv(8,20), get_uv(8,16), get_uv(4,16)]
        uv_btm = [get_uv(8,20), get_uv(12,20), get_uv(12,16), get_uv(8,16)]

    for face in bm.faces:
        n = face.normal
        if n.z > 0.5:     uvs = uv_t
        elif n.z < -0.5:  uvs = uv_btm
        elif n.y < -0.5:  uvs = uv_f
        elif n.y > 0.5:   uvs = uv_b
        elif n.x > 0.5:   uvs = uv_r
        elif n.x < -0.5:  uvs = uv_l
        else: continue
        
        for loop in face.loops:
            co = loop.vert.co
            px = co.x / bw + 0.5
            py = co.y / bh + 0.5
            pz = co.z / bd + 0.5
            
            if n.z > 0.5:    u, v = px, py
            elif n.z < -0.5: u, v = px, py
            elif n.y < -0.5: u, v = px, pz
            elif n.y > 0.5:  u, v = px, pz
            elif n.x > 0.5:  u, v = py, pz
            else:            u, v = py, pz

            uv_min_u = uvs[0][0]; uv_max_u = uvs[1][0]
            uv_max_v = uvs[2][1]; uv_min_v = uvs[0][1]
            
            if n.z < -0.5 or n.y > 0.5 or n.x > 0.5:
                u = 1.0 - u

            loop[uv_layer].uv.x = uv_min_u + u * (uv_max_u - uv_min_u)
            loop[uv_layer].uv.y = uv_min_v + v * (uv_max_v - uv_min_v)

    bm.to_mesh(mesh)
    bm.free()
    
    obj.location = (loc_x, loc_y, loc_z)
    return obj

# ================= 生成模型 =================
body = build_box("Body", 28, 8, 10, 8, 16, loc_x=0, loc_y=0, loc_z=0.625)
head = build_box("Head", 0, 0, 8, 8, 8, loc_x=0, loc_y=-0.625, loc_z=0.75)
snout = build_box("Snout", 16, 16, 4, 3, 1, loc_x=0, loc_y=-0.90625, loc_z=0.65625)

leg_rf = build_box("Leg_RF", 0, 16, 4, 6, 4, loc_x=-0.1875, loc_y=-0.3125, loc_z=0.1875)
leg_lf = build_box("Leg_LF", 0, 16, 4, 6, 4, loc_x= 0.1875, loc_y=-0.3125, loc_z=0.1875)
leg_rb = build_box("Leg_RB", 0, 16, 4, 6, 4, loc_x=-0.1875, loc_y= 0.4375, loc_z=0.1875)
leg_lb = build_box("Leg_LB", 0, 16, 4, 6, 4, loc_x= 0.1875, loc_y= 0.4375, loc_z=0.1875)

# ================= 强设原点（Pivot）在大腿根部 =================
def set_origin_to_world(obj, x, y, z):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.context.scene.cursor.location = (x, y, z)
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR', center='MEDIAN')
    obj.select_set(False)

set_origin_to_world(head, 0, -0.625, 0.5)
set_origin_to_world(snout, 0, -0.90625, 0.65625)
set_origin_to_world(leg_rf, -0.1875, -0.3125, 0.375)
set_origin_to_world(leg_lf,  0.1875, -0.3125, 0.375)
set_origin_to_world(leg_rb, -0.1875,  0.4375, 0.375)
set_origin_to_world(leg_lb,  0.1875,  0.4375, 0.375)

# ================= 强绑父子关系 =================
def set_parent_keep_transform(child, parent):
    bpy.ops.object.select_all(action='DESELECT')
    child.select_set(True)
    parent.select_set(True)
    bpy.context.view_layer.objects.active = parent
    bpy.ops.object.parent_set(type='OBJECT', keep_transform=True)

set_parent_keep_transform(head, body)
set_parent_keep_transform(snout, head)
set_parent_keep_transform(leg_rf, body)
set_parent_keep_transform(leg_lf, body)
set_parent_keep_transform(leg_rb, body)
set_parent_keep_transform(leg_lb, body)

# ================= 自动 K 帧动画 =================
def insert_keyframe(obj, frame, rot_x_deg):
    obj.rotation_mode = 'XYZ'
    obj.rotation_euler[0] = math.radians(rot_x_deg)
    obj.keyframe_insert(data_path="rotation_euler", index=0, frame=frame)

legs = [leg_rf, leg_lf, leg_rb, leg_lb]
for leg in legs: leg.animation_data_clear()

bpy.context.scene.frame_start = 1
bpy.context.scene.frame_end = 40

for leg in legs: insert_keyframe(leg, 1, 0)

insert_keyframe(leg_lf, 10, 30);  insert_keyframe(leg_rb, 10, 30)
insert_keyframe(leg_rf, 10, -30); insert_keyframe(leg_lb, 10, -30)

for leg in legs: insert_keyframe(leg, 20, 0)

insert_keyframe(leg_lf, 30, -30); insert_keyframe(leg_rb, 30, -30)
insert_keyframe(leg_rf, 30, 30);  insert_keyframe(leg_lb, 30, 30)

for leg in legs: insert_keyframe(leg, 40, 0)

# 兼容 Blender 4.4+ 的线性插值
def set_linear_interpolation(obj):
    if not obj.animation_data or not obj.animation_data.action: return
    action = obj.animation_data.action
    if hasattr(action, "fcurves"):
        for fcurve in action.fcurves:
            for kp in fcurve.keyframe_points: kp.interpolation = 'LINEAR'
    else:
        for layer in action.layers:
            for strip in layer.strips:
                if hasattr(strip, "channelbags"): 
                    for channelbag in strip.channelbags:
                        for fcurve in channelbag.fcurves:
                            for kp in fcurve.keyframe_points: 
                                kp.interpolation = 'LINEAR'

for leg in legs: set_linear_interpolation(leg)

print("🐷 猪模型与动画生成完毕！请在视图中按空格预览，然后导出 glTF 2.0 (.glb)。")