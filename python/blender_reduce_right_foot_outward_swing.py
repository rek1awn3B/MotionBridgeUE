from __future__ import annotations

import argparse
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))

from blender_bvh_to_fbx import (
    clear_scene,
    create_debug_joint_mesh,
    export_fbx,
    import_bvh,
)


def world_head(armature: bpy.types.Object, bone_name: str) -> Vector:
    """返回姿势骨骼头部的世界坐标。"""

    return armature.matrix_world @ armature.pose.bones[bone_name].head


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_bvh", type=Path)
    parser.add_argument("output_fbx", type=Path)
    parser.add_argument("output_blend", type=Path)
    parser.add_argument("--lateral-limit-cm", type=float, default=11.8)
    parser.add_argument("--side", choices=("left", "right"), default="left")
    if "--" not in sys.argv:
        raise RuntimeError("pass script arguments after Blender's -- separator")
    arguments = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])

    source_path = arguments.source_bvh.resolve()
    output_fbx_path = arguments.output_fbx.resolve()
    output_blend_path = arguments.output_blend.resolve()
    output_fbx_path.parent.mkdir(parents=True, exist_ok=True)
    output_blend_path.parent.mkdir(parents=True, exist_ok=True)

    clear_scene()
    armature = import_bvh(source_path)
    debug_mesh = create_debug_joint_mesh(armature)
    scene = bpy.context.scene
    exact_fps = round(scene.render.fps / scene.render.fps_base)
    scene.render.fps = exact_fps
    scene.render.fps_base = 1.0

    action = armature.animation_data.action
    if action is None:
        raise RuntimeError("imported armature has no animation action")
    frame_start = math.floor(action.frame_range[0])
    frame_end = math.ceil(action.frame_range[1])
    scene.frame_start = frame_start
    scene.frame_end = frame_end

    selected_side = arguments.side.capitalize()
    selected_ankle_name = f"{selected_side}Ankle"
    selected_knee_name = f"{selected_side}Knee"
    selected_sign = -1.0 if selected_side == "Left" else 1.0

    required_bones = {
        "Hips",
        "LeftHip",
        "LeftAnkle",
        "LeftToe",
        "RightHip",
        selected_knee_name,
        "RightAnkle",
        "RightToe",
    }
    missing_bones = required_bones - set(armature.pose.bones.keys())
    if missing_bones:
        raise RuntimeError(f"missing required bones: {sorted(missing_bones)}")

    lateral_limit = arguments.lateral_limit_cm * 0.01
    targets: dict[int, Vector] = {}
    selected_samples: dict[int, tuple[Vector, Vector, float]] = {}
    before_lateral: list[float] = []
    left_lateral: list[float] = []
    hip_half_widths: list[float] = []
    left_foot_yaw_degrees: list[float] = []
    right_foot_yaw_degrees: list[float] = []
    left_ankle_heights: list[float] = []
    right_ankle_heights: list[float] = []
    correction_lengths: list[float] = []
    selected_ankle_heights: list[float] = []

    # 先在没有约束的原动作上缓存目标。RightHip - LeftHip 给出人物的
    # 局部右方向；只沿这个方向收脚，不改变脚踝的前后位置和高度。
    for frame in range(frame_start, frame_end + 1):
        scene.frame_set(frame)
        hips = world_head(armature, "Hips")
        left_hip = world_head(armature, "LeftHip")
        right_hip = world_head(armature, "RightHip")
        left_ankle = world_head(armature, "LeftAnkle")
        left_toe = world_head(armature, "LeftToe")
        right_ankle = world_head(armature, "RightAnkle")
        right_toe = world_head(armature, "RightToe")

        right_direction = right_hip - left_hip
        right_direction.z = 0.0
        if right_direction.length <= 1e-8:
            raise RuntimeError(f"invalid body right direction at frame {frame}")
        right_direction.normalize()
        body_forward = right_direction.cross(Vector((0.0, 0.0, 1.0)))
        body_forward.normalize()

        left_foot = left_toe - left_ankle
        right_foot = right_toe - right_ankle
        left_foot.z = 0.0
        right_foot.z = 0.0
        left_foot_yaw_degrees.append(
            math.degrees(
                math.atan2(left_foot.dot(right_direction), left_foot.dot(body_forward))
            )
        )
        right_foot_yaw_degrees.append(
            math.degrees(
                math.atan2(right_foot.dot(right_direction), right_foot.dot(body_forward))
            )
        )
        left_ankle_heights.append(left_ankle.z)
        right_ankle_heights.append(right_ankle.z)

        ankle = left_ankle if selected_side == "Left" else right_ankle
        # right_direction 指向角色右侧。把左右两侧统一转换成“向身体外侧为正”，
        # 才不会再把画面右侧误认成骨骼命名中的 Right。
        outward_lateral = (
            (ankle - hips).dot(right_direction) * selected_sign
        )
        left_lateral.append((left_ankle - hips).dot(right_direction))
        hip_half_widths.append((right_hip - left_hip).length * 0.5)
        selected_samples[frame] = (
            ankle.copy(),
            right_direction.copy(),
            outward_lateral,
        )
        selected_ankle_heights.append(ankle.z)
        before_lateral.append(outward_lateral)

    # 着地脚必须保持原世界位置，否则骨盆横摆时会被错误地拖着横向滑动。
    # 只在脚踝明显离地后逐渐启用横向收窄，并用 smoothstep 避免在
    # 离地/落地边界产生速度突变。
    contact_height = min(selected_ankle_heights) + 0.02
    full_swing_height = min(selected_ankle_heights) + 0.06
    for frame in range(frame_start, frame_end + 1):
        ankle, right_direction, outward_lateral = selected_samples[frame]
        raw_weight = (
            (selected_ankle_heights[frame - frame_start] - contact_height)
            / (full_swing_height - contact_height)
        )
        weight = max(0.0, min(1.0, raw_weight))
        weight = weight * weight * (3.0 - 2.0 * weight)
        correction = max(0.0, outward_lateral - lateral_limit) * weight
        targets[frame] = (
            ankle - right_direction * (selected_sign * correction)
        )
        correction_lengths.append(correction)

    target = bpy.data.objects.new(
        f"MotionBridge_{selected_ankle_name}Target", None
    )
    bpy.context.collection.objects.link(target)
    for frame, location in targets.items():
        target.location = location
        target.keyframe_insert(data_path="location", frame=frame)

    # 约束放在小腿上：小腿尾端就是脚踝头部。chain_count=2 只重新求解
    # 选中侧的髋和膝，脚踝原有旋转、脚掌朝向及其他骨骼动画保持不变。
    selected_knee = armature.pose.bones[selected_knee_name]
    ik_constraint = selected_knee.constraints.new(type="IK")
    ik_constraint.name = f"MotionBridge_{selected_side}LegSwingCorrection"
    ik_constraint.target = target
    ik_constraint.chain_count = 2
    ik_constraint.use_stretch = False

    bpy.context.view_layer.objects.active = armature
    armature.select_set(True)
    bpy.ops.object.mode_set(mode="POSE")
    # Blender 5 移除了旧 Bone.select 写接口；整套姿势烘焙仍只会把
    # 选中膝盖上的 IK 约束结果带入，其余骨骼保持当前可见姿势。
    bpy.ops.nla.bake(
        frame_start=frame_start,
        frame_end=frame_end,
        step=1,
        only_selected=False,
        visual_keying=True,
        clear_constraints=True,
        clear_parents=False,
        use_current_action=True,
        clean_curves=False,
        bake_types={"POSE"},
    )
    bpy.ops.object.mode_set(mode="OBJECT")

    after_lateral: list[float] = []
    for frame in range(frame_start, frame_end + 1):
        scene.frame_set(frame)
        hips = world_head(armature, "Hips")
        left_hip = world_head(armature, "LeftHip")
        right_hip = world_head(armature, "RightHip")
        ankle = world_head(armature, selected_ankle_name)
        right_direction = right_hip - left_hip
        right_direction.z = 0.0
        right_direction.normalize()
        after_lateral.append(
            (ankle - hips).dot(right_direction) * selected_sign
        )

    scene.frame_set(frame_start)
    bpy.ops.wm.save_as_mainfile(filepath=str(output_blend_path))
    export_fbx(armature, debug_mesh, output_fbx_path)

    print(f"FRAME_RANGE={frame_start}-{frame_end}")
    print(f"LATERAL_LIMIT_CM={arguments.lateral_limit_cm:.3f}")
    print(f"SELECTED_SIDE={selected_side}")
    print(
        "SELECTED_OUTWARD_BEFORE_CM="
        f"{min(before_lateral) * 100.0:.3f},"
        f"{max(before_lateral) * 100.0:.3f}"
    )
    print(
        "LEFT_LATERAL_SOURCE_CM="
        f"{min(left_lateral) * 100.0:.3f},"
        f"{max(left_lateral) * 100.0:.3f}"
    )
    print(
        "HIP_HALF_WIDTH_CM="
        f"{min(hip_half_widths) * 100.0:.3f},"
        f"{max(hip_half_widths) * 100.0:.3f}"
    )
    left_contact_yaws = [
        yaw
        for yaw, height in zip(left_foot_yaw_degrees, left_ankle_heights)
        if height <= min(left_ankle_heights) + 0.02
    ]
    right_contact_yaws = [
        yaw
        for yaw, height in zip(right_foot_yaw_degrees, right_ankle_heights)
        if height <= min(right_ankle_heights) + 0.02
    ]
    print(
        "LEFT_FOOT_YAW_CONTACT_DEG="
        f"{min(left_contact_yaws):.3f},{max(left_contact_yaws):.3f}"
    )
    print(
        "RIGHT_FOOT_YAW_CONTACT_DEG="
        f"{min(right_contact_yaws):.3f},{max(right_contact_yaws):.3f}"
    )
    print(
        "SELECTED_OUTWARD_AFTER_CM="
        f"{min(after_lateral) * 100.0:.3f},"
        f"{max(after_lateral) * 100.0:.3f}"
    )
    print(f"MAX_CORRECTION_CM={max(correction_lengths) * 100.0:.3f}")
    selected_contact_corrections = [
        correction
        for correction, height in zip(correction_lengths, selected_ankle_heights)
        if height <= min(selected_ankle_heights) + 0.02
    ]
    print(
        "MAX_CONTACT_CORRECTION_CM="
        f"{max(selected_contact_corrections) * 100.0:.3f}"
    )
    print(f"BLEND={output_blend_path}")
    print(f"FBX={output_fbx_path}")


if __name__ == "__main__":
    main()
