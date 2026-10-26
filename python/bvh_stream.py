from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

import numpy as np

from build_bvh_source_calibration import matrix_to_quaternion
from build_manny_retarget_calibration import apply_calibration
from bvh_parser import parse_bvh
from bvh_pose import decode_frame_pose
from root_motion_bvh import add_virtual_root_to_bvh


def load_calibration(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as calibration_file:
        calibration = json.load(calibration_file)
    if int(calibration.get("schema_version", 0)) != 1:
        raise ValueError("unsupported BVH source calibration schema")
    if not calibration.get("bones"):
        raise ValueError("calibration has no bones")
    return calibration


def apply_affine_translation(
    value: np.ndarray,
    affine: list[list[float]],
) -> list[float]:
    source = np.append(np.asarray(value, dtype=np.float64), 1.0)
    result = source @ np.asarray(affine, dtype=np.float64)
    return [float(component) for component in result]


def build_stream_from_bvh(
    bvh_path: Path,
    calibration_path: Path,
    stream_id: str | None = None,
) -> dict[str, Any]:
    calibration = load_calibration(calibration_path)
    source = parse_bvh(bvh_path.read_text(encoding="utf-8"))
    source_with_root = parse_bvh(add_virtual_root_to_bvh(source))

    source_bones = [calibration["wrapper_bone"]] + [
        joint.name
        for joint in source_with_root.joints
        if not joint.is_end_site
    ]
    bones = list(calibration["bones"])
    if source_bones != bones:
        raise ValueError(
            "BVH hierarchy is incompatible with the calibrated source mesh: "
            f"expected {bones}, got {source_bones}"
        )

    left_maps = [
        np.asarray(value, dtype=np.float64)
        for value in calibration["left_rotation_maps"]
    ]
    right_maps = [
        np.asarray(value, dtype=np.float64)
        for value in calibration["right_rotation_maps"]
    ]
    reference_translations = calibration["reference_local_translations"]
    affine_maps = calibration["translation_affine_maps"]
    actor_axes = [int(axis) for axis in calibration["actor_translation_axes"]]
    actor_scale = float(calibration["actor_position_scale"])

    frames: list[dict[str, Any]] = []
    for frame_index in range(source_with_root.motion.frame_count):
        local_pose = decode_frame_pose(source_with_root, frame_index)
        raw_rotations = [np.eye(3, dtype=np.float64)] + [
            local_pose.rotations[joint_index]
            for joint_index, joint in enumerate(source_with_root.joints)
            if not joint.is_end_site
        ]
        raw_translations = [np.zeros(3, dtype=np.float64)] + [
            local_pose.translations[joint_index]
            for joint_index, joint in enumerate(source_with_root.joints)
            if not joint.is_end_site
        ]

        rotations = [
            matrix_to_quaternion(left @ raw @ right)
            for left, raw, right in zip(
                left_maps,
                raw_rotations,
                right_maps,
            )
        ]
        translations: list[list[float]] = []
        for bone_name, raw_translation, reference_translation in zip(
            bones,
            raw_translations,
            reference_translations,
        ):
            if bone_name in affine_maps:
                translations.append(
                    apply_affine_translation(
                        raw_translation,
                        affine_maps[bone_name],
                    )
                )
            else:
                translations.append(
                    [float(component) for component in reference_translation]
                )

        motion_root_index = bones.index("MotionRoot")
        motion_root_translation = raw_translations[motion_root_index]
        motion_root_rotation = raw_rotations[motion_root_index]
        # 原 BVH 为 Y-up；映射到 UE Z-up 后，heading 的正方向相反。
        # Actor 根运动只能接收纯 yaw，不能把源骨架 MotionRoot 自带的
        # 90 度坐标系校正一起写进去，否则胶囊会倾倒并产生假竖直位移。
        heading = math.atan2(
            float(motion_root_rotation[0, 2]),
            float(motion_root_rotation[2, 2]),
        )
        half_yaw = -0.5 * heading
        actor_rotation = [
            0.0,
            0.0,
            math.sin(half_yaw),
            math.cos(half_yaw),
        ]
        actor_position = [
            float(motion_root_translation[axis] * actor_scale)
            for axis in actor_axes
        ]
        frames.append(
            {
                "frame_index": frame_index,
                "timestamp": frame_index * source_with_root.motion.frame_time,
                "root_position": actor_position,
                "root_rotation": actor_rotation,
                "bone_translations": translations,
                "bone_rotations": rotations,
            }
        )

    return {
        "protocol_version": 1,
        "stream_id": stream_id or f"bvh-{bvh_path.stem}",
        "skeleton": calibration["source_skeleton"],
        "source_format": "BVH",
        "source_file": bvh_path.name,
        "fps": 1.0 / source_with_root.motion.frame_time,
        "bones": bones,
        "frames": frames,
    }


def build_manny_stream_from_bvh(
    bvh_path: Path,
    source_calibration_path: Path,
    manny_calibration_path: Path,
    stream_id: str | None = None,
) -> dict[str, Any]:
    """从原始 BVH 逐帧计算 Manny 局部姿势，不读取预导出的目标动画帧。"""
    source_stream = build_stream_from_bvh(
        bvh_path,
        source_calibration_path,
        stream_id=stream_id,
    )
    with manny_calibration_path.open(
        "r",
        encoding="utf-8",
    ) as calibration_file:
        manny_calibration = json.load(calibration_file)
    if (
        int(manny_calibration.get("schema_version", 0)) != 1
        or manny_calibration.get("target_skeleton") != "UE5_Manny"
    ):
        raise ValueError("unsupported Manny retarget calibration")
    stream = apply_calibration(source_stream, manny_calibration)
    if stream_id:
        stream["stream_id"] = stream_id
    return stream
