from __future__ import annotations

import statistics

import unreal


SOURCE_ANIMATION = "/Game/MotionBridge/100StyleSource/A_MotionBridge100StyleSource_Anim"
SOURCE_MESH = "/Game/MotionBridge/100StyleSource/SK_MotionBridge100StyleSource"
TARGET_ANIMATION = "/Game/MotionBridge/100StyleRetargeted/A_MotionBridge100Style_Manny"
TARGET_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"


def world_x(pose: unreal.AnimPose, bone: str) -> float:
    transform = pose.get_bone_pose(bone, unreal.AnimPoseSpaces.WORLD)
    return transform.translation.x


def collect(animation_path: str, mesh_path: str, bones: dict[str, str]) -> dict[str, list[float]]:
    animation = unreal.load_asset(animation_path)
    mesh = unreal.load_asset(mesh_path)
    if animation is None or mesh is None:
        raise RuntimeError(f"missing animation or mesh: {animation_path}")

    options = unreal.AnimPoseEvaluationOptions()
    options.evaluation_type = unreal.AnimDataEvalType.RAW
    options.optional_skeletal_mesh = mesh
    options.extract_root_motion = False
    options.incorporate_root_motion_into_pose = True

    frame_count = animation.data_model_interface.get_number_of_frames() + 1
    values = {label: [] for label in bones}
    for frame in range(frame_count):
        pose = animation.get_anim_pose_at_frame(frame, options)
        for label, bone in bones.items():
            values[label].append(world_x(pose, bone))
    return values


def summarize(prefix: str, values: dict[str, list[float]]) -> None:
    pelvis = values["pelvis"]
    for side in ("left", "right"):
        sign = 1.0 if side == "left" else -1.0
        hip = values[f"{side}_hip"]
        knee = values[f"{side}_knee"]
        foot = values[f"{side}_foot"]
        toe = values[f"{side}_toe"]
        for joint_name, joint in (("hip", hip), ("knee", knee), ("foot", foot), ("toe", toe)):
            lateral = [sign * (x - root_x) for x, root_x in zip(joint, pelvis)]
            unreal.log(
                f"MOTIONBRIDGE_LEG_LATERAL={prefix}|{side}|{joint_name}|"
                f"min={min(lateral):.4f}|mean={statistics.fmean(lateral):.4f}|"
                f"max={max(lateral):.4f}"
            )
        foot_from_hip = [
            sign * (foot_x - hip_x) for foot_x, hip_x in zip(foot, hip)
        ]
        unreal.log(
            f"MOTIONBRIDGE_FOOT_FROM_HIP={prefix}|{side}|"
            f"min={min(foot_from_hip):.4f}|"
            f"mean={statistics.fmean(foot_from_hip):.4f}|"
            f"max={max(foot_from_hip):.4f}"
        )

    foot_separation = [
        abs(left - right)
        for left, right in zip(values["left_foot"], values["right_foot"])
    ]
    unreal.log(
        f"MOTIONBRIDGE_FOOT_SEPARATION={prefix}|"
        f"min={min(foot_separation):.4f}|"
        f"mean={statistics.fmean(foot_separation):.4f}|"
        f"max={max(foot_separation):.4f}"
    )


source = collect(
    SOURCE_ANIMATION,
    SOURCE_MESH,
    {
        "pelvis": "Hips",
        "left_hip": "LeftHip",
        "left_knee": "LeftKnee",
        "left_foot": "LeftAnkle",
        "left_toe": "LeftToe",
        "right_hip": "RightHip",
        "right_knee": "RightKnee",
        "right_foot": "RightAnkle",
        "right_toe": "RightToe",
    },
)
target = collect(
    TARGET_ANIMATION,
    TARGET_MESH,
    {
        "pelvis": "pelvis",
        "left_hip": "thigh_l",
        "left_knee": "calf_l",
        "left_foot": "foot_l",
        "left_toe": "ball_l",
        "right_hip": "thigh_r",
        "right_knee": "calf_r",
        "right_foot": "foot_r",
        "right_toe": "ball_r",
    },
)

summarize("SOURCE", source)
summarize("TARGET", target)
