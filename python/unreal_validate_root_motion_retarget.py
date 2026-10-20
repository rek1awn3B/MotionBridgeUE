from __future__ import annotations

import math

import unreal


IS_100STYLE = "-MotionBridge100Style" in unreal.SystemLibrary.get_command_line()
IS_SFU_WALK = "-MotionBridgeSFUWalk" in unreal.SystemLibrary.get_command_line()
ANIMATION_PATH = (
    "/Game/MotionBridge/SFUWalkRetargeted/A_MotionBridgeSFUWalk_Manny"
    if IS_SFU_WALK
    else "/Game/MotionBridge/100StyleRetargeted/A_MotionBridge100Style_Manny"
    if IS_100STYLE
    else "/Game/MotionBridge/RootMotionRetargeted/"
    "A_MotionBridgeRootMotion_Manny"
)
MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
EXPECTED_SAMPLES = 120 if IS_SFU_WALK else 165 if IS_100STYLE else 69
# UE 的 Pelvis Motion 阶段会按 Manny 与源骨架的高度比例缩放步幅，
# 所以 SFU 源数据的 131 cm 会在目标骨架上变为约 157 cm。
MIN_ROOT_HORIZONTAL = 150.0 if IS_SFU_WALK else 200.0 if IS_100STYLE else 85.0
MAX_ROOT_HORIZONTAL = 165.0 if IS_SFU_WALK else 300.0 if IS_100STYLE else 105.0


def axis_extent(
    positions: list[tuple[float, float, float]],
    axis: int,
) -> float:
    values = [position[axis] for position in positions]
    return max(values) - min(values)


def main() -> None:
    animation = unreal.load_asset(ANIMATION_PATH)
    mesh = unreal.load_asset(MESH_PATH)
    if animation is None or mesh is None:
        raise RuntimeError("retargeted animation or Manny mesh is missing")
    if not animation.get_editor_property("enable_root_motion"):
        raise RuntimeError("Enable Root Motion is disabled")

    options = unreal.AnimPoseEvaluationOptions()
    options.evaluation_type = unreal.AnimDataEvalType.RAW
    options.optional_skeletal_mesh = mesh
    # 该资产已开启 Enable Root Motion。验证骨骼轨道本身时必须明确
    # 禁止把 root 抽到组件上，并把 root 合回姿势；否则查询到的是
    # “角色组件尚未移动”的视图，会误判 pelvis 被固定。
    options.extract_root_motion = False
    options.incorporate_root_motion_into_pose = True
    frame_count = animation.data_model_interface.get_number_of_frames()
    track_names = [
        str(name)
        for name in animation.data_model_interface.get_bone_track_names()
    ]
    unreal.log(
        "MOTIONBRIDGE_MANNY_RAW_TRACKS="
        f"count:{len(track_names)}|first:{track_names[:5]}|"
        f"root_index:{track_names.index('root') if 'root' in track_names else -1}"
    )
    if frame_count + 1 != EXPECTED_SAMPLES:
        raise RuntimeError(
            f"expected {EXPECTED_SAMPLES} animation samples, "
            f"got {frame_count + 1}"
        )

    tracked = {"root": [], "pelvis": [], "foot_l": [], "foot_r": []}
    pelvis_local: list[tuple[float, float, float]] = []
    for frame_index in range(frame_count + 1):
        pose = animation.get_anim_pose_at_frame(frame_index, options)
        if not pose.is_valid():
            raise RuntimeError(f"invalid pose at frame {frame_index}")
        names = {str(name) for name in pose.get_bone_names()}
        for required_name in tracked:
            if required_name not in names:
                raise RuntimeError(f"missing track: {required_name}")
            transform = pose.get_bone_pose(
                required_name,
                unreal.AnimPoseSpaces.WORLD,
            )
            translation = transform.translation
            values = (translation.x, translation.y, translation.z)
            if not all(math.isfinite(value) for value in values):
                raise RuntimeError(
                    f"non-finite pose: frame={frame_index}, "
                    f"bone={required_name}"
                )
            tracked[required_name].append(values)
        local_pelvis_transform = pose.get_bone_pose(
            "pelvis",
            unreal.AnimPoseSpaces.LOCAL,
        )
        local_translation = local_pelvis_transform.translation
        pelvis_local.append(
            (
                local_translation.x,
                local_translation.y,
                local_translation.z,
            )
        )

    root_extents = [axis_extent(tracked["root"], axis) for axis in range(3)]
    root_horizontal = math.hypot(root_extents[0], root_extents[1])
    root_start = tracked["root"][0]
    root_end = tracked["root"][-1]
    root_net = tuple(
        root_end[axis] - root_start[axis] for axis in range(3)
    )
    root_net_horizontal = math.hypot(root_net[0], root_net[1])
    pelvis_vertical = axis_extent(tracked["pelvis"], 2)
    pelvis_local_extents = [
        axis_extent(pelvis_local, axis) for axis in range(3)
    ]
    unreal.log(
        "MOTIONBRIDGE_MANNY_ROOT_EXTENT="
        + ",".join(f"{value:.6f}" for value in root_extents)
    )
    unreal.log(
        "MOTIONBRIDGE_MANNY_ROOT_ENDPOINTS="
        + ",".join(f"{value:.6f}" for value in root_start)
        + "|"
        + ",".join(f"{value:.6f}" for value in root_end)
        + f"|net_horizontal={root_net_horizontal:.6f}"
    )
    unreal.log(
        "MOTIONBRIDGE_MANNY_PELVIS_WORLD_EXTENT="
        + ",".join(
            f"{axis_extent(tracked['pelvis'], axis):.6f}"
            for axis in range(3)
        )
    )
    unreal.log(
        "MOTIONBRIDGE_MANNY_PELVIS_LOCAL_EXTENT="
        + ",".join(f"{value:.6f}" for value in pelvis_local_extents)
    )
    for bone_name in ("pelvis", "foot_l", "foot_r"):
        z_values = [position[2] for position in tracked[bone_name]]
        unreal.log(
            f"MOTIONBRIDGE_MANNY_{bone_name.upper()}_Z_RANGE="
            f"{min(z_values):.6f},{max(z_values):.6f}"
        )
    if not MIN_ROOT_HORIZONTAL <= root_horizontal <= MAX_ROOT_HORIZONTAL:
        raise RuntimeError(
            "Manny root did not retain the expected trajectory: "
            f"horizontal extent={root_horizontal}"
        )
    if (IS_100STYLE or IS_SFU_WALK) and root_net_horizontal < MIN_ROOT_HORIZONTAL:
        raise RuntimeError(
            "Manny root did not preserve forward displacement: "
            f"net horizontal={root_net_horizontal}"
        )
    if root_extents[2] > 0.1:
        raise RuntimeError(
            f"Manny root should stay on the ground: Z extent={root_extents[2]}"
        )
    if pelvis_vertical < 2.0:
        raise RuntimeError(
            "Manny pelvis lost its local vertical motion: "
            f"Z extent={pelvis_vertical}"
        )

    unreal.log(
        f"MOTIONBRIDGE_MANNY_ROOT_HORIZONTAL={root_horizontal:.6f}"
    )
    unreal.log(
        f"MOTIONBRIDGE_MANNY_PELVIS_VERTICAL={pelvis_vertical:.6f}"
    )
    unreal.log(f"MOTIONBRIDGE_MANNY_ROOT_MOTION_FRAMES={frame_count + 1}")
    unreal.log("MOTIONBRIDGE_MANNY_ROOT_MOTION_VALIDATION=PASS")


main()
