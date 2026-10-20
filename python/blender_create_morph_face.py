"""生成可公开展示的原创表情 Morph Target 测试头并导出 FBX。"""

from __future__ import annotations

import math
import sys
from pathlib import Path

import bpy


def material(name: str, color: tuple[float, float, float, float]):
    result = bpy.data.materials.new(name)
    result.diffuse_color = color
    result.roughness = 0.62
    return result


def add_uv_part(
    name: str,
    location: tuple[float, float, float],
    scale: tuple[float, float, float],
    part_material,
):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=32,
        ring_count=20,
        location=location,
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(part_material)
    return obj


def add_mouth(part_material):
    curve = bpy.data.curves.new("MouthCurve", "CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = 0.025
    curve.bevel_resolution = 3
    spline = curve.splines.new("BEZIER")
    spline.bezier_points.add(4)
    points = [
        (-0.34, -0.735, -0.18),
        (-0.17, -0.79, -0.23),
        (0.0, -0.81, -0.25),
        (0.17, -0.79, -0.23),
        (0.34, -0.735, -0.18),
    ]
    for point, coordinate in zip(spline.bezier_points, points):
        point.co = coordinate
        point.handle_left_type = "AUTO"
        point.handle_right_type = "AUTO"
    obj = bpy.data.objects.new("Mouth", curve)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(part_material)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.convert(target="MESH")
    return bpy.context.object


def join_parts(parts):
    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    face = bpy.context.object
    face.name = "SK_MotionBridgeFace"
    return face


def add_shape_keys(face):
    face.shape_key_add(name="Basis")
    basis = face.data.shape_keys.key_blocks["Basis"]

    smile = face.shape_key_add(name="Smile")
    jaw = face.shape_key_add(name="JawOpen")
    blink_left = face.shape_key_add(name="BlinkLeft")
    blink_right = face.shape_key_add(name="BlinkRight")

    for index, point in enumerate(basis.data):
        x, y, z = point.co

        mouth_region = y < -0.68 and -0.42 < z < 0.02 and abs(x) < 0.48
        if mouth_region:
            corner = min(max((abs(x) - 0.08) / 0.30, 0.0), 1.0)
            smile.data[index].co.z += 0.20 * corner
            smile.data[index].co.x *= 1.0 + 0.10 * corner

        lower_face = y < -0.25 and z < -0.02
        if lower_face:
            influence = min(max((-z + 0.02) / 0.75, 0.0), 1.0)
            jaw.data[index].co.z -= 0.25 * influence
            jaw.data[index].co.y -= 0.04 * influence

        eye_band = y < -0.62 and 0.08 < z < 0.43
        if eye_band and x < -0.04:
            blink_left.data[index].co.z = 0.24 + (z - 0.24) * 0.12
        if eye_band and x > 0.04:
            blink_right.data[index].co.z = 0.24 + (z - 0.24) * 0.12


def add_root_armature(face):
    armature_data = bpy.data.armatures.new("MotionBridgeFace_Skeleton")
    armature = bpy.data.objects.new("MotionBridgeFace_Armature", armature_data)
    bpy.context.collection.objects.link(armature)
    bpy.context.view_layer.objects.active = armature
    armature.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    root = armature_data.edit_bones.new("root")
    root.head = (0.0, 0.0, -1.0)
    root.tail = (0.0, 0.0, 1.0)
    bpy.ops.object.mode_set(mode="OBJECT")

    group = face.vertex_groups.new(name="root")
    group.add(range(len(face.data.vertices)), 1.0, "REPLACE")
    modifier = face.modifiers.new(name="Armature", type="ARMATURE")
    modifier.object = armature
    face.parent = armature
    return armature


def main():
    output_arg = sys.argv[sys.argv.index("--") + 1]
    output_path = Path(output_arg).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    skin = material("M_Skin", (0.22, 0.50, 0.86, 1.0))
    white = material("M_EyeWhite", (0.92, 0.95, 1.0, 1.0))
    dark = material("M_Dark", (0.015, 0.025, 0.05, 1.0))

    head = add_uv_part("Head", (0.0, 0.0, 0.0), (0.72, 0.68, 0.90), skin)
    eye_l = add_uv_part("EyeL", (-0.27, -0.61, 0.24), (0.18, 0.10, 0.14), white)
    eye_r = add_uv_part("EyeR", (0.27, -0.61, 0.24), (0.18, 0.10, 0.14), white)
    pupil_l = add_uv_part("PupilL", (-0.27, -0.705, 0.24), (0.065, 0.035, 0.075), dark)
    pupil_r = add_uv_part("PupilR", (0.27, -0.705, 0.24), (0.065, 0.035, 0.075), dark)
    mouth = add_mouth(dark)
    face = join_parts([head, eye_l, eye_r, pupil_l, pupil_r, mouth])
    add_shape_keys(face)
    armature = add_root_armature(face)

    bpy.ops.object.select_all(action="DESELECT")
    face.select_set(True)
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.fbx(
        filepath=str(output_path),
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL",
        axis_forward="-Y",
        axis_up="Z",
        add_leaf_bones=False,
        bake_anim=False,
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
    )
    print(f"MOTIONBRIDGE_MORPH_FBX=PASS|path={output_path}")


if __name__ == "__main__":
    main()
