from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

import numpy as np

from bvh_parser import parse_bvh
from bvh_pose import calculate_world_pose, decode_frame_pose
from root_motion_bvh import add_virtual_root_to_bvh


WRAPPER_BONE = "MotionBridge_BVH_Source"


def quaternion_to_matrix(value: list[float]) -> np.ndarray:
    x, y, z, w = (float(component) for component in value)
    length = math.sqrt(x * x + y * y + z * z + w * w)
    if length <= 1.0e-12:
        raise ValueError("zero-length quaternion")
    x, y, z, w = x / length, y / length, z / length, w / length
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ],
        dtype=np.float64,
    )


def matrix_to_quaternion(matrix: np.ndarray) -> list[float]:
    value = np.asarray(matrix, dtype=np.float64)
    trace = float(np.trace(value))
    if trace > 0.0:
        scale = math.sqrt(trace + 1.0) * 2.0
        w = 0.25 * scale
        x = (value[2, 1] - value[1, 2]) / scale
        y = (value[0, 2] - value[2, 0]) / scale
        z = (value[1, 0] - value[0, 1]) / scale
    else:
        diagonal = np.diag(value)
        index = int(np.argmax(diagonal))
        if index == 0:
            scale = math.sqrt(1.0 + value[0, 0] - value[1, 1] - value[2, 2]) * 2.0
            w = (value[2, 1] - value[1, 2]) / scale
            x = 0.25 * scale
            y = (value[0, 1] + value[1, 0]) / scale
            z = (value[0, 2] + value[2, 0]) / scale
        elif index == 1:
            scale = math.sqrt(1.0 + value[1, 1] - value[0, 0] - value[2, 2]) * 2.0
            w = (value[0, 2] - value[2, 0]) / scale
            x = (value[0, 1] + value[1, 0]) / scale
            y = 0.25 * scale
            z = (value[1, 2] + value[2, 1]) / scale
        else:
            scale = math.sqrt(1.0 + value[2, 2] - value[0, 0] - value[1, 1]) * 2.0
            w = (value[1, 0] - value[0, 1]) / scale
            x = (value[0, 2] + value[2, 0]) / scale
            y = (value[1, 2] + value[2, 1]) / scale
            z = 0.25 * scale
    quaternion = np.array([x, y, z, w], dtype=np.float64)
    quaternion /= np.linalg.norm(quaternion)
    if quaternion[3] < 0.0:
        quaternion *= -1.0
    return [float(component) for component in quaternion]


def rotation_error_degrees(left: np.ndarray, right: np.ndarray) -> float:
    relative = left.T @ right
    cosine = np.clip((np.trace(relative) - 1.0) * 0.5, -1.0, 1.0)
    return math.degrees(math.acos(float(cosine)))


def rotation_vector(matrix: np.ndarray) -> np.ndarray:
    value = np.asarray(matrix, dtype=np.float64)
    cosine = np.clip((np.trace(value) - 1.0) * 0.5, -1.0, 1.0)
    angle = math.acos(float(cosine))
    if angle <= 1.0e-10:
        return np.zeros(3, dtype=np.float64)
    sine = math.sin(angle)
    if abs(sine) <= 1.0e-8:
        eigenvalues, eigenvectors = np.linalg.eig(value)
        axis = np.real(eigenvectors[:, np.argmin(abs(eigenvalues - 1.0))])
        axis /= np.linalg.norm(axis)
        return axis * angle
    axis = np.array(
        [
            value[2, 1] - value[1, 2],
            value[0, 2] - value[2, 0],
            value[1, 0] - value[0, 1],
        ],
        dtype=np.float64,
    ) / (2.0 * sine)
    return axis * angle


def fit_two_sided_rotation_map(
    source_rotations: list[np.ndarray],
    target_rotations: list[np.ndarray],
) -> tuple[np.ndarray, np.ndarray]:
    source_vectors: list[np.ndarray] = []
    target_vectors: list[np.ndarray] = []
    for index in range(1, len(source_rotations)):
        source_delta = (
            source_rotations[index]
            @ source_rotations[index - 1].T
        )
        target_delta = (
            target_rotations[index]
            @ target_rotations[index - 1].T
        )
        source_vector = rotation_vector(source_delta)
        target_vector = rotation_vector(target_delta)
        if np.linalg.norm(source_vector) > 1.0e-8 and np.linalg.norm(target_vector) > 1.0e-8:
            source_vectors.append(source_vector)
            target_vectors.append(target_vector)

    if not source_vectors:
        left = np.eye(3, dtype=np.float64)
    else:
        source_matrix = np.stack(source_vectors)
        target_matrix = np.stack(target_vectors)
        covariance = target_matrix.T @ source_matrix
        left_u, _, left_vt = np.linalg.svd(covariance)
        handedness = np.eye(3, dtype=np.float64)
        handedness[2, 2] = np.linalg.det(left_u @ left_vt)
        left = left_u @ handedness @ left_vt

    right = (
        source_rotations[0].T
        @ left.T
        @ target_rotations[0]
    )
    return left, right


def load_reference_stream(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream_file:
        stream = json.load(stream_file)
    if not stream.get("bones") or not stream.get("frames"):
        raise ValueError("reference stream is empty")
    return stream


def build_calibration(
    source_bvh_path: Path,
    reference_stream_path: Path,
) -> dict[str, Any]:
    source = parse_bvh(source_bvh_path.read_text(encoding="utf-8"))
    virtual_root_source = parse_bvh(add_virtual_root_to_bvh(source))
    reference = load_reference_stream(reference_stream_path)

    bones = list(reference["bones"])
    expected_bones = [WRAPPER_BONE] + [
        joint.name
        for joint in virtual_root_source.joints
        if not joint.is_end_site
    ]
    if bones != expected_bones:
        raise ValueError(
            "source mesh bone order does not match the calibrated BVH: "
            f"expected {expected_bones}, got {bones}"
        )
    if len(reference["frames"]) != virtual_root_source.motion.frame_count:
        raise ValueError("reference and BVH frame counts differ")

    parents: list[int] = [-1]
    processed_to_stream: dict[int, int] = {}
    for joint_index, joint in enumerate(virtual_root_source.joints):
        if joint.is_end_site:
            continue
        stream_index = len(parents)
        processed_to_stream[joint_index] = stream_index
        if joint.parent_index is None or joint.parent_index < 0:
            parents.append(0)
        else:
            parents.append(processed_to_stream[joint.parent_index])

    raw_local_frames: list[list[np.ndarray]] = []
    ue_local_frames: list[list[np.ndarray]] = []
    raw_translation_frames: list[list[np.ndarray]] = []
    ue_translation_frames: list[list[np.ndarray]] = []
    for frame_index in range(virtual_root_source.motion.frame_count):
        local_pose = decode_frame_pose(virtual_root_source, frame_index)
        raw_local_frames.append([np.eye(3, dtype=np.float64)] + [
            local_pose.rotations[joint_index]
            for joint_index, joint in enumerate(virtual_root_source.joints)
            if not joint.is_end_site
        ])
        raw_translation_frames.append([np.zeros(3, dtype=np.float64)] + [
            local_pose.translations[joint_index]
            for joint_index, joint in enumerate(virtual_root_source.joints)
            if not joint.is_end_site
        ])
        ue_local_frames.append([
            quaternion_to_matrix(value)
            for value in reference["frames"][frame_index]["bone_rotations"]
        ])
        ue_translation_frames.append([
            np.asarray(value, dtype=np.float64)
            for value in reference["frames"][frame_index]["bone_translations"]
        ])

    left_rotation_maps: list[np.ndarray] = []
    right_rotation_maps: list[np.ndarray] = []
    for bone_index in range(len(bones)):
        left, right = fit_two_sided_rotation_map(
            [frame[bone_index] for frame in raw_local_frames],
            [frame[bone_index] for frame in ue_local_frames],
        )
        left_rotation_maps.append(left)
        right_rotation_maps.append(right)

    translation_affine_maps: dict[str, list[list[float]]] = {}
    for bone_name in ("MotionRoot", "Hips"):
        bone_index = bones.index(bone_name)
        source_values = np.stack(
            [frame[bone_index] for frame in raw_translation_frames]
        )
        source_values = np.concatenate(
            [source_values, np.ones((source_values.shape[0], 1))],
            axis=1,
        )
        target_values = np.stack(
            [frame[bone_index] for frame in ue_translation_frames]
        )
        affine, _, _, _ = np.linalg.lstsq(
            source_values,
            target_values,
            rcond=None,
        )
        translation_affine_maps[bone_name] = affine.tolist()

    reference_translations = reference["frames"][0]["bone_translations"]
    calibration = {
        "schema_version": 1,
        "source_skeleton": "100style-bvh-source",
        "wrapper_bone": WRAPPER_BONE,
        "bones": bones,
        "parents": parents,
        "reference_local_translations": reference_translations,
        "left_rotation_maps": [
            matrix.tolist() for matrix in left_rotation_maps
        ],
        "right_rotation_maps": [
            matrix.tolist() for matrix in right_rotation_maps
        ],
        "translation_affine_maps": translation_affine_maps,
        "source_position_scale": 0.01,
        "source_translation_axes": [0, 2, 1],
        "actor_position_scale": 1.0,
        "actor_translation_axes": [0, 2, 1],
        "calibrated_from": source_bvh_path.name,
    }

    max_errors = {bone: 0.0 for bone in bones}
    for frame_index in range(virtual_root_source.motion.frame_count):
        predicted_locals = [
            left @ source @ right
            for left, source, right in zip(
                left_rotation_maps,
                raw_local_frames[frame_index],
                right_rotation_maps,
            )
        ]
        expected_locals = ue_local_frames[frame_index]
        for bone, predicted, expected in zip(
            bones,
            predicted_locals,
            expected_locals,
        ):
            max_errors[bone] = max(
                max_errors[bone],
                rotation_error_degrees(predicted, expected),
            )

    calibration["validation_max_rotation_error_degrees"] = max_errors
    return calibration


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-bvh",
        type=Path,
        default=root / "data" / "bvh" / "100style" / "Neutral_FW_frames_530_694.bvh",
    )
    parser.add_argument(
        "--reference-stream",
        type=Path,
        default=root / "artifacts" / "realtime" / "source_reference_stream.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "data" / "calibration" / "100style_source_calibration.json",
    )
    return parser.parse_args()


def main() -> None:
    arguments = parse_args()
    calibration = build_calibration(
        arguments.source_bvh,
        arguments.reference_stream,
    )
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(calibration, indent=2),
        encoding="utf-8",
    )
    errors = calibration["validation_max_rotation_error_degrees"]
    print(f"CALIBRATION_BONES={len(calibration['bones'])}")
    print(f"CALIBRATION_MAX_ERROR_DEG={max(errors.values()):.6f}")
    print(
        "CALIBRATION_WORST_BONE="
        + max(errors, key=errors.__getitem__)
    )
    print(f"CALIBRATION_OUTPUT={arguments.output}")


if __name__ == "__main__":
    main()
