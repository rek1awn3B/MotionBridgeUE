from __future__ import annotations

import argparse
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector


# 这批 BVH 的位移数字以厘米为单位，而 Blender/FBX 流程按米解释场景单位。
# 导入时先把厘米换成米，之后 UE 再把 1 米正确换成 100 厘米。
BVH_CENTIMETERS_TO_METERS = 0.01


def clear_scene() -> None:
    """删除默认场景对象，保证导出结果只包含本次 BVH 骨架。"""

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def import_bvh(
    source_path: Path,
    global_scale: float = BVH_CENTIMETERS_TO_METERS,
) -> bpy.types.Object:
    """将 Y-up BVH 导入为 Blender 的 Z-up Armature。"""

    result = bpy.ops.import_anim.bvh(
        filepath=str(source_path),
        target="ARMATURE",
        global_scale=global_scale,
        frame_start=1,
        use_fps_scale=False,
        update_scene_fps=True,
        update_scene_duration=True,
        use_cyclic=False,
        rotate_mode="NATIVE",
        axis_forward="-Z",
        axis_up="Y",
    )
    if "FINISHED" not in result:
        raise RuntimeError(f"BVH import failed: {result}")

    armatures = [
        scene_object
        for scene_object in bpy.context.scene.objects
        if scene_object.type == "ARMATURE"
    ]
    if len(armatures) != 1:
        raise RuntimeError(
            f"expected exactly one armature, got {len(armatures)}"
        )

    armature = armatures[0]
    armature.name = "MotionBridge_BVH_Source"
    armature.data.name = "MotionBridge_BVH_Source_Skeleton"
    armature.show_in_front = True
    return armature


def create_debug_joint_mesh(armature: bpy.types.Object) -> bpy.types.Object:
    """为每个关节创建小八面体，使 UE 能建立可预览的源 Skeletal Mesh。"""

    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, int, int]] = []
    bone_vertex_indices: dict[str, list[int]] = {}
    # 关节标记半径为 0.8 厘米；此处场景单位已经是米。
    radius = 0.8 * BVH_CENTIMETERS_TO_METERS

    for bone in armature.data.bones:
        center = bone.head_local
        first_vertex = len(vertices)
        vertices.extend(
            [
                tuple(center + Vector((radius, 0.0, 0.0))),
                tuple(center + Vector((-radius, 0.0, 0.0))),
                tuple(center + Vector((0.0, radius, 0.0))),
                tuple(center + Vector((0.0, -radius, 0.0))),
                tuple(center + Vector((0.0, 0.0, radius))),
                tuple(center + Vector((0.0, 0.0, -radius))),
            ]
        )
        bone_vertex_indices[bone.name] = list(
            range(first_vertex, first_vertex + 6)
        )

        positive_x = first_vertex
        negative_x = first_vertex + 1
        positive_y = first_vertex + 2
        negative_y = first_vertex + 3
        positive_z = first_vertex + 4
        negative_z = first_vertex + 5
        faces.extend(
            [
                (positive_z, positive_x, positive_y),
                (positive_z, positive_y, negative_x),
                (positive_z, negative_x, negative_y),
                (positive_z, negative_y, positive_x),
                (negative_z, positive_y, positive_x),
                (negative_z, negative_x, positive_y),
                (negative_z, negative_y, negative_x),
                (negative_z, positive_x, negative_y),
            ]
        )

    mesh_data = bpy.data.meshes.new("MotionBridge_JointMarkers_Mesh")
    mesh_data.from_pydata(vertices, [], faces)
    mesh_data.update()

    mesh_object = bpy.data.objects.new(
        "MotionBridge_JointMarkers",
        mesh_data,
    )
    bpy.context.collection.objects.link(mesh_object)

    for bone_name, vertex_indices in bone_vertex_indices.items():
        vertex_group = mesh_object.vertex_groups.new(name=bone_name)
        vertex_group.add(vertex_indices, 1.0, "REPLACE")

    armature_modifier = mesh_object.modifiers.new(
        name="MotionBridge_Armature",
        type="ARMATURE",
    )
    armature_modifier.object = armature
    return mesh_object


def export_fbx(
    armature: bpy.types.Object,
    debug_mesh: bpy.types.Object,
    output_path: Path,
) -> None:
    """将选中的 BVH Armature 及其动画导出为 UE 可导入的 FBX。"""

    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    debug_mesh.select_set(True)
    bpy.context.view_layer.objects.active = armature

    result = bpy.ops.export_scene.fbx(
        filepath=str(output_path),
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        global_scale=1.0,
        apply_unit_scale=False,
        apply_scale_options="FBX_SCALE_NONE",
        axis_forward="-Z",
        axis_up="Y",
        add_leaf_bones=False,
        use_armature_deform_only=False,
        bake_anim=True,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False,
        bake_anim_force_startend_keying=True,
        bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
    )
    if "FINISHED" not in result:
        raise RuntimeError(f"FBX export failed: {result}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_bvh", type=Path)
    parser.add_argument("output_fbx", type=Path)
    parser.add_argument("output_blend", type=Path)
    parser.add_argument(
        "--global-scale",
        type=float,
        default=BVH_CENTIMETERS_TO_METERS,
        help="BVH numeric units to Blender metres (inch-based data uses 0.0254)",
    )
    if "--" not in sys.argv:
        raise RuntimeError("pass script arguments after Blender's -- separator")
    arguments = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])

    source_path = arguments.source_bvh.resolve()
    output_fbx_path = arguments.output_fbx.resolve()
    output_blend_path = arguments.output_blend.resolve()
    output_fbx_path.parent.mkdir(parents=True, exist_ok=True)
    output_blend_path.parent.mkdir(parents=True, exist_ok=True)

    clear_scene()
    armature = import_bvh(source_path, arguments.global_scale)
    debug_mesh = create_debug_joint_mesh(armature)

    scene = bpy.context.scene
    # BVH 的 Frame Time 通常只保留有限小数，例如 0.0333333 会被 Blender
    # 解释成 30.00003 FPS。导出前归一到最近的整数 FPS，避免 UE 因时间范围
    # 略短于整帧而丢掉末尾一个采样点。
    exact_fps = round(scene.render.fps / scene.render.fps_base)
    scene.render.fps = exact_fps
    scene.render.fps_base = 1.0
    action = armature.animation_data.action
    if action is None:
        raise RuntimeError("imported armature has no animation action")

    scene.frame_start = math.floor(action.frame_range[0])
    scene.frame_end = math.ceil(action.frame_range[1])
    scene.frame_set(scene.frame_start)
    bpy.ops.wm.save_as_mainfile(filepath=str(output_blend_path))
    export_fbx(armature, debug_mesh, output_fbx_path)

    print(f"ARMATURE_NAME={armature.name}")
    print(f"BONE_COUNT={len(armature.data.bones)}")
    print(f"DEBUG_MESH_VERTICES={len(debug_mesh.data.vertices)}")
    print(f"DEBUG_MESH_FACES={len(debug_mesh.data.polygons)}")
    print(f"FRAME_RANGE={scene.frame_start}-{scene.frame_end}")
    print(f"FPS={scene.render.fps}/{scene.render.fps_base}")
    print(f"DIMENSIONS={tuple(round(value, 4) for value in armature.dimensions)}")
    print(f"BLEND={output_blend_path}")
    print(f"FBX={output_fbx_path}")


if __name__ == "__main__":
    main()
