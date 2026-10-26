from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from build_bvh_source_calibration import (
    fit_two_sided_rotation_map,
    matrix_to_quaternion,
    quaternion_to_matrix,
    rotation_error_degrees,
)


# 只映射 BVH 确实提供的主要关节。Manny 的扭转骨、手指和 IK 辅助骨
# 保持参考姿势，由 UE 的蒙皮链和 Foot IK 正常消费。
TARGET_TO_SOURCE = {
    "root": "MotionBridge_BVH_Source",
    "pelvis": "Hips",
    "spine_01": "Chest",
    "spine_02": "Chest2",
    "spine_03": "Chest3",
    "spine_04": "Chest4",
    "spine_05": "Chest4",
    "neck_01": "Neck",
    "neck_02": "Neck",
    "head": "Head",
    "clavicle_l": "LeftCollar",
    "upperarm_l": "LeftShoulder",
    "lowerarm_l": "LeftElbow",
    "hand_l": "LeftWrist",
    "clavicle_r": "RightCollar",
    "upperarm_r": "RightShoulder",
    "lowerarm_r": "RightElbow",
    "hand_r": "RightWrist",
    "thigh_l": "LeftHip",
    "calf_l": "LeftKnee",
    "foot_l": "LeftAnkle",
    "ball_l": "LeftToe",
    "thigh_r": "RightHip",
    "calf_r": "RightKnee",
    "foot_r": "RightAnkle",
    "ball_r": "RightToe",
}

SOURCE_PARENT = {
    "MotionBridge_BVH_Source": None,
    "MotionRoot": "MotionBridge_BVH_Source",
    "Hips": "MotionRoot",
    "Chest": "Hips",
    "Chest2": "Chest",
    "Chest3": "Chest2",
    "Chest4": "Chest3",
    "Neck": "Chest4",
    "Head": "Neck",
    "LeftCollar": "Chest4",
    "LeftShoulder": "LeftCollar",
    "LeftElbow": "LeftShoulder",
    "LeftWrist": "LeftElbow",
    "RightCollar": "Chest4",
    "RightShoulder": "RightCollar",
    "RightElbow": "RightShoulder",
    "RightWrist": "RightElbow",
    "LeftHip": "Hips",
    "LeftKnee": "LeftHip",
    "LeftAnkle": "LeftKnee",
    "LeftToe": "LeftAnkle",
    "RightHip": "Hips",
    "RightKnee": "RightHip",
    "RightAnkle": "RightKnee",
    "RightToe": "RightAnkle",
}

TARGET_PARENT = {
    "root": None,
    "pelvis": "root",
    "spine_01": "pelvis",
    "spine_02": "spine_01",
    "spine_03": "spine_02",
    "spine_04": "spine_03",
    "spine_05": "spine_04",
    "neck_01": "spine_05",
    "neck_02": "neck_01",
    "head": "neck_02",
    "clavicle_l": "spine_05",
    "upperarm_l": "clavicle_l",
    "lowerarm_l": "upperarm_l",
    "hand_l": "lowerarm_l",
    "clavicle_r": "spine_05",
    "upperarm_r": "clavicle_r",
    "lowerarm_r": "upperarm_r",
    "hand_r": "lowerarm_r",
    "thigh_l": "pelvis",
    "calf_l": "thigh_l",
    "foot_l": "calf_l",
    "ball_l": "foot_l",
    "thigh_r": "pelvis",
    "calf_r": "thigh_r",
    "foot_r": "calf_r",
    "ball_r": "foot_r",
}


def mapped_global_rotations(
    frame: dict[str, Any],
    indices: dict[str, int],
    parents: dict[str, str | None],
) -> dict[str, np.ndarray]:
    result: dict[str, np.ndarray] = {}
    for bone, parent in parents.items():
        local = quaternion_to_matrix(frame["bone_rotations"][indices[bone]])
        result[bone] = local if parent is None else result[parent] @ local
    return result


def load_stream(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream_file:
        value = json.load(stream_file)
    if not value.get("bones") or not value.get("frames"):
        raise ValueError(f"empty stream: {path}")
    return value


def build_calibration(
    source_stream_path: Path,
    target_stream_path: Path,
) -> dict[str, Any]:
    source = load_stream(source_stream_path)
    target = load_stream(target_stream_path)
    if len(source["frames"]) != len(target["frames"]):
        raise ValueError("source and target reference frame counts differ")

    source_indices = {
        bone: index for index, bone in enumerate(source["bones"])
    }
    target_indices = {
        bone: index for index, bone in enumerate(target["bones"])
    }
    missing_source = sorted(set(TARGET_TO_SOURCE.values()) - source_indices.keys())
    missing_target = sorted(set(TARGET_TO_SOURCE.keys()) - target_indices.keys())
    if missing_source or missing_target:
        raise ValueError(
            f"missing mapped bones: source={missing_source}, target={missing_target}"
        )

    source_globals = [
        mapped_global_rotations(frame, source_indices, SOURCE_PARENT)
        for frame in source["frames"]
    ]
    target_globals = [
        mapped_global_rotations(frame, target_indices, TARGET_PARENT)
        for frame in target["frames"]
    ]

    mappings: dict[str, Any] = {}
    maximum_error = 0.0
    worst_bone = ""
    for target_bone, source_bone in TARGET_TO_SOURCE.items():
        source_rotations = [frame[source_bone] for frame in source_globals]
        target_rotations = [frame[target_bone] for frame in target_globals]
        left, right = fit_two_sided_rotation_map(
            source_rotations,
            target_rotations,
        )
        bone_error = max(
            rotation_error_degrees(left @ source_rotation @ right, target_rotation)
            for source_rotation, target_rotation in zip(
                source_rotations,
                target_rotations,
            )
        )
        if bone_error > maximum_error:
            maximum_error = bone_error
            worst_bone = target_bone
        mappings[target_bone] = {
            "source_bone": source_bone,
            "target_parent": TARGET_PARENT[target_bone],
            "left_rotation": left.tolist(),
            "right_rotation": right.tolist(),
            "validation_max_error_degrees": bone_error,
        }

    first_target_frame = target["frames"][0]
    # root motion 单独交给 Character Actor，骨架内 root 保持原点与单位旋转。
    reference_rotations = first_target_frame["bone_rotations"]
    reference_rotations[target_indices["root"]] = [0.0, 0.0, 0.0, 1.0]
    reference_translations = first_target_frame["bone_translations"]
    reference_translations[target_indices["root"]] = [0.0, 0.0, 0.0]

    return {
        "schema_version": 1,
        "target_skeleton": "UE5_Manny",
        "source_skeleton": source.get("skeleton", "100style-bvh-source"),
        "target_bones": target["bones"],
        "reference_local_translations": reference_translations,
        "reference_local_rotations": reference_rotations,
        "bone_mappings": mappings,
        "source_parents": SOURCE_PARENT,
        "calibrated_from": {
            "source": source_stream_path.name,
            "target": target_stream_path.name,
        },
        "validation_max_rotation_error_degrees": maximum_error,
        "validation_worst_bone": worst_bone,
    }


def apply_calibration(
    source_stream: dict[str, Any],
    calibration: dict[str, Any],
) -> dict[str, Any]:
    source_indices = {
        bone: index for index, bone in enumerate(source_stream["bones"])
    }
    target_bones = list(calibration["target_bones"])
    reference_translations = calibration["reference_local_translations"]
    reference_rotations = calibration["reference_local_rotations"]
    mappings = calibration["bone_mappings"]
    target_indices = {
        bone: index for index, bone in enumerate(target_bones)
    }

    frames: list[dict[str, Any]] = []
    for source_frame in source_stream["frames"]:
        rotations = [list(value) for value in reference_rotations]
        translations = [list(value) for value in reference_translations]
        source_globals = mapped_global_rotations(
            source_frame,
            source_indices,
            calibration["source_parents"],
        )
        target_globals: dict[str, np.ndarray] = {}
        for target_bone, mapping in mappings.items():
            source_rotation = source_globals[mapping["source_bone"]]
            left = np.asarray(mapping["left_rotation"], dtype=np.float64)
            right = np.asarray(mapping["right_rotation"], dtype=np.float64)
            target_global = left @ source_rotation @ right
            target_globals[target_bone] = target_global
            target_parent = mapping["target_parent"]
            target_local = (
                target_global
                if target_parent is None
                else target_globals[target_parent].T @ target_global
            )
            rotations[target_indices[target_bone]] = matrix_to_quaternion(
                target_local
            )

        frames.append(
            {
                "frame_index": source_frame["frame_index"],
                "timestamp": source_frame["timestamp"],
                "root_position": source_frame["root_position"],
                "root_rotation": source_frame["root_rotation"],
                "bone_translations": translations,
                "bone_rotations": rotations,
            }
        )

    return {
        "protocol_version": 1,
        "stream_id": f"{source_stream['stream_id']}-to-manny",
        "skeleton": calibration["target_skeleton"],
        "source_format": source_stream.get("source_format", "BVH"),
        "source_file": source_stream.get("source_file", ""),
        "retarget_mode": "calibrated-two-sided-local-rotation-map",
        "fps": source_stream["fps"],
        "bones": target_bones,
        "frames": frames,
    }


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-stream",
        type=Path,
        default=root / "artifacts" / "realtime" / "source_reference_stream.json",
    )
    parser.add_argument(
        "--target-stream",
        type=Path,
        default=root / "artifacts" / "realtime" / "manny_root_motion_stream.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "data" / "calibration" / "100style_to_manny_calibration.json",
    )
    return parser.parse_args()


def main() -> None:
    arguments = parse_args()
    calibration = build_calibration(
        arguments.source_stream,
        arguments.target_stream,
    )
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(calibration, indent=2),
        encoding="utf-8",
    )
    print(f"MANNY_CALIBRATION_BONES={len(calibration['bone_mappings'])}")
    print(
        "MANNY_CALIBRATION_MAX_ERROR_DEG="
        f"{calibration['validation_max_rotation_error_degrees']:.6f}"
    )
    print(
        "MANNY_CALIBRATION_WORST_BONE="
        f"{calibration['validation_worst_bone']}"
    )
    print(f"MANNY_CALIBRATION_OUTPUT={arguments.output}")


if __name__ == "__main__":
    main()
