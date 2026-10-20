from __future__ import annotations

import math

import unreal


ANIMATION_PATH = "/Game/MotionBridge/Retargeted/A_MotionBridgeSource_Manny"
MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"


def all_finite(transform: unreal.Transform) -> bool:
    translation = transform.translation
    rotation = transform.rotation
    scale = transform.scale3d
    values = (
        translation.x,
        translation.y,
        translation.z,
        rotation.x,
        rotation.y,
        rotation.z,
        rotation.w,
        scale.x,
        scale.y,
        scale.z,
    )
    return all(math.isfinite(value) for value in values)


def has_valid_scale_and_rotation(transform: unreal.Transform) -> bool:
    scale = transform.scale3d
    rotation = transform.rotation
    scale_is_nonzero = all(
        abs(value) > 1.0e-8 for value in (scale.x, scale.y, scale.z)
    )
    rotation_squared_length = (
        rotation.x * rotation.x
        + rotation.y * rotation.y
        + rotation.z * rotation.z
        + rotation.w * rotation.w
    )
    return scale_is_nonzero and rotation_squared_length > 1.0e-8


def main() -> None:
    animation = unreal.load_asset(ANIMATION_PATH)
    mesh = unreal.load_asset(MESH_PATH)
    if animation is None or mesh is None:
        raise RuntimeError("animation or target mesh is missing")

    options = unreal.AnimPoseEvaluationOptions()
    options.evaluation_type = unreal.AnimDataEvalType.RAW
    options.optional_skeletal_mesh = mesh
    frame_count = animation.data_model_interface.get_number_of_frames()
    tracked_positions = {
        "root": [],
        "pelvis": [],
    }

    for frame_index in range(frame_count + 1):
        pose = animation.get_anim_pose_at_frame(frame_index, options)
        if not pose.is_valid():
            raise RuntimeError(f"invalid pose at frame {frame_index}")
        for bone_name in pose.get_bone_names():
            transform = pose.get_bone_pose(bone_name)
            if not all_finite(transform):
                raise RuntimeError(
                    f"non-finite transform: frame={frame_index}, bone={bone_name}"
                )
            if not has_valid_scale_and_rotation(transform):
                raise RuntimeError(
                    "invalid scale or rotation: "
                    f"frame={frame_index}, bone={bone_name}, "
                    f"transform={transform}"
                )
        for bone_name in tracked_positions:
            world_transform = pose.get_bone_pose(
                bone_name,
                unreal.AnimPoseSpaces.WORLD,
            )
            translation = world_transform.translation
            tracked_positions[bone_name].append(
                (translation.x, translation.y, translation.z)
            )

    for bone_name, positions in tracked_positions.items():
        for axis_index, axis_name in enumerate(("X", "Y", "Z")):
            values = [position[axis_index] for position in positions]
            unreal.log(
                f"MOTIONBRIDGE_{bone_name.upper()}_{axis_name}_RANGE="
                f"{min(values):.6f},{max(values):.6f}"
            )
    unreal.log(f"MOTIONBRIDGE_POSE_FRAMES={frame_count + 1}")
    unreal.log("MOTIONBRIDGE_RAW_POSE_VALIDATION=PASS")


main()
