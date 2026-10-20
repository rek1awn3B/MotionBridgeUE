from __future__ import annotations

import math

import unreal


DIRECTORY = "/Game/MotionBridge/RootMotionSource"
MESH_PATH = f"{DIRECTORY}/SK_MotionBridgeRootMotionSource"
ANIMATION_PATH = f"{DIRECTORY}/A_MotionBridgeRootMotionSource_Anim"
EXPECTED_SAMPLES = 69
MIN_HEIGHT = 150.0
MAX_HEIGHT = 220.0
MIN_ROOT_HORIZONTAL = 90.0
MAX_ROOT_HORIZONTAL = 100.0


def axis_range(
    positions: list[tuple[float, float, float]],
    axis_index: int,
) -> tuple[float, float]:
    values = [position[axis_index] for position in positions]
    return min(values), max(values)


def main() -> None:
    mesh = unreal.load_asset(MESH_PATH)
    animation = unreal.load_asset(ANIMATION_PATH)
    if mesh is None or animation is None:
        raise RuntimeError("root-motion source mesh or animation is missing")

    bounds = mesh.get_bounds()
    mesh_height = bounds.box_extent.z * 2.0
    if not MIN_HEIGHT <= mesh_height <= MAX_HEIGHT:
        raise RuntimeError(
            "source mesh has the wrong physical scale: "
            f"height={mesh_height} cm"
        )
    unreal.log(f"MOTIONBRIDGE_SOURCE_MESH_HEIGHT={mesh_height:.6f}")

    options = unreal.AnimPoseEvaluationOptions()
    options.evaluation_type = unreal.AnimDataEvalType.RAW
    options.optional_skeletal_mesh = mesh
    # 与 UE 的批量重定向器保持同一套求值选项，避免验证脚本和实际
    # bake 路径对根运动采用不同解释。
    options.extract_root_motion = False
    options.incorporate_root_motion_into_pose = True
    frame_count = animation.data_model_interface.get_number_of_frames()
    if frame_count + 1 != EXPECTED_SAMPLES:
        raise RuntimeError(
            f"expected {EXPECTED_SAMPLES} animation samples, got "
            f"{frame_count + 1}"
        )
    root_positions: list[tuple[float, float, float]] = []
    hips_positions: list[tuple[float, float, float]] = []

    for frame_index in range(frame_count + 1):
        pose = animation.get_anim_pose_at_frame(frame_index, options)
        if not pose.is_valid():
            raise RuntimeError(f"invalid pose at frame {frame_index}")
        bone_names = {str(name) for name in pose.get_bone_names()}
        if "MotionRoot" not in bone_names:
            raise RuntimeError("MotionRoot track is missing from imported pose")

        hips_candidates = sorted(
            name for name in bone_names if name.endswith("Hips")
        )
        if len(hips_candidates) != 1:
            raise RuntimeError(
                f"expected one Hips bone, got {hips_candidates}"
            )
        hips_name = hips_candidates[0]

        for bone_name, output in (
            ("MotionRoot", root_positions),
            (hips_name, hips_positions),
        ):
            transform = pose.get_bone_pose(
                bone_name,
                unreal.AnimPoseSpaces.WORLD,
            )
            translation = transform.translation
            values = (translation.x, translation.y, translation.z)
            if not all(math.isfinite(value) for value in values):
                raise RuntimeError(
                    f"non-finite position: frame={frame_index}, "
                    f"bone={bone_name}"
                )
            output.append(values)

    root_ranges = [
        axis_range(root_positions, axis_index)
        for axis_index in range(3)
    ]
    root_extents = [maximum - minimum for minimum, maximum in root_ranges]
    horizontal_extent = math.hypot(root_extents[0], root_extents[1])
    if not MIN_ROOT_HORIZONTAL <= horizontal_extent <= MAX_ROOT_HORIZONTAL:
        raise RuntimeError(
            "MotionRoot horizontal trajectory has the wrong distance or unit: "
            f"extent={horizontal_extent}"
        )
    if root_extents[2] > 1.0e-3:
        raise RuntimeError(
            "MotionRoot should remain on the ground plane: "
            f"Z extent={root_extents[2]}"
        )

    for bone_name, positions in (
        ("ROOT", root_positions),
        ("HIPS", hips_positions),
    ):
        for axis_index, axis_name in enumerate(("X", "Y", "Z")):
            minimum, maximum = axis_range(positions, axis_index)
            unreal.log(
                f"MOTIONBRIDGE_{bone_name}_{axis_name}_RANGE="
                f"{minimum:.6f},{maximum:.6f}"
            )
    unreal.log(f"MOTIONBRIDGE_ROOT_MOTION_FRAMES={frame_count + 1}")
    unreal.log("MOTIONBRIDGE_ROOT_MOTION_VALIDATION=PASS")


main()
