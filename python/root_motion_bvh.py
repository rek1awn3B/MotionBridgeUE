from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from bvh_parser import BvhData, BvhJoint, parse_bvh
from root_motion import build_bvh_root_pelvis_motion


_ROOT_CHANNELS = [
    "Xposition",
    "Yposition",
    "Zposition",
    "Zrotation",
    "Yrotation",
    "Xrotation",
]
_POSITION_COMPONENTS = {
    "Xposition": 0,
    "Yposition": 1,
    "Zposition": 2,
}


def matrices_to_euler_zyx(rotations: np.ndarray) -> np.ndarray:
    """把 Rz @ Ry @ Rx 旋转矩阵组还原为连续的 Z/Y/X 欧拉角。"""

    matrices = np.asarray(rotations, dtype=np.float64)
    if matrices.ndim != 3 or matrices.shape[1:] != (3, 3):
        raise ValueError(
            "rotations must have shape (frame_count, 3, 3), "
            f"got {matrices.shape}"
        )
    if matrices.shape[0] == 0:
        raise ValueError("rotations must contain at least one frame")
    if not np.all(np.isfinite(matrices)):
        raise ValueError("rotations must contain only finite values")

    cosine_y = np.hypot(matrices[:, 0, 0], matrices[:, 1, 0])
    if np.any(cosine_y <= 1e-8):
        frame_index = int(np.flatnonzero(cosine_y <= 1e-8)[0])
        raise ValueError(
            "ZYX Euler conversion reached gimbal lock at frame "
            f"{frame_index}"
        )

    angle_z = np.arctan2(matrices[:, 1, 0], matrices[:, 0, 0])
    angle_y = np.arctan2(-matrices[:, 2, 0], cosine_y)
    angle_x = np.arctan2(matrices[:, 2, 1], matrices[:, 2, 2])
    angles = np.stack([angle_z, angle_y, angle_x], axis=1)
    return np.unwrap(angles, axis=0)


def matrices_to_euler_yxz(rotations: np.ndarray) -> np.ndarray:
    """把 Ry @ Rx @ Rz 旋转矩阵组还原为连续的 Y/X/Z 欧拉角。"""

    matrices = np.asarray(rotations, dtype=np.float64)
    if matrices.ndim != 3 or matrices.shape[1:] != (3, 3):
        raise ValueError(
            "rotations must have shape (frame_count, 3, 3), "
            f"got {matrices.shape}"
        )
    if matrices.shape[0] == 0:
        raise ValueError("rotations must contain at least one frame")
    if not np.all(np.isfinite(matrices)):
        raise ValueError("rotations must contain only finite values")

    cosine_x = np.hypot(matrices[:, 1, 0], matrices[:, 1, 1])
    if np.any(cosine_x <= 1e-8):
        frame_index = int(np.flatnonzero(cosine_x <= 1e-8)[0])
        raise ValueError(
            "YXZ Euler conversion reached gimbal lock at frame "
            f"{frame_index}"
        )

    angle_x = np.arctan2(-matrices[:, 1, 2], cosine_x)
    angle_y = np.arctan2(matrices[:, 0, 2], matrices[:, 2, 2])
    angle_z = np.arctan2(matrices[:, 1, 0], matrices[:, 1, 1])
    angles = np.stack([angle_y, angle_x, angle_z], axis=1)
    return np.unwrap(angles, axis=0)


def matrices_to_euler_xyz(rotations: np.ndarray) -> np.ndarray:
    """把 Rx @ Ry @ Rz 旋转矩阵组还原为连续的 X/Y/Z 欧拉角。"""

    matrices = np.asarray(rotations, dtype=np.float64)
    if matrices.ndim != 3 or matrices.shape[1:] != (3, 3):
        raise ValueError(
            "rotations must have shape (frame_count, 3, 3), "
            f"got {matrices.shape}"
        )
    if matrices.shape[0] == 0:
        raise ValueError("rotations must contain at least one frame")
    if not np.all(np.isfinite(matrices)):
        raise ValueError("rotations must contain only finite values")

    cosine_y = np.hypot(matrices[:, 0, 0], matrices[:, 0, 1])
    if np.any(cosine_y <= 1e-8):
        frame_index = int(np.flatnonzero(cosine_y <= 1e-8)[0])
        raise ValueError(
            "XYZ Euler conversion reached gimbal lock at frame "
            f"{frame_index}"
        )

    angle_x = np.arctan2(-matrices[:, 1, 2], matrices[:, 2, 2])
    angle_y = np.arctan2(matrices[:, 0, 2], cosine_y)
    angle_z = np.arctan2(-matrices[:, 0, 1], matrices[:, 0, 0])
    angles = np.stack([angle_x, angle_y, angle_z], axis=1)
    return np.unwrap(angles, axis=0)


def matrices_to_euler_for_channels(
    rotations: np.ndarray,
    rotation_channels: list[str],
) -> np.ndarray:
    """按 BVH 声明的旋转通道顺序还原欧拉角。"""

    channel_order = tuple(rotation_channels)
    if channel_order == ("Zrotation", "Yrotation", "Xrotation"):
        return matrices_to_euler_zyx(rotations)
    if channel_order == ("Yrotation", "Xrotation", "Zrotation"):
        return matrices_to_euler_yxz(rotations)
    if channel_order == ("Xrotation", "Yrotation", "Zrotation"):
        return matrices_to_euler_xyz(rotations)

    raise ValueError(
        "unsupported root rotation channel order: "
        f"{list(rotation_channels)}"
    )


def _format_number(value: float) -> str:
    if abs(value) < 5e-10:
        value = 0.0
    return f"{value:.9f}".rstrip("0").rstrip(".") or "0"


def _children_by_parent(data: BvhData) -> dict[int, list[int]]:
    children: dict[int, list[int]] = {
        index: [] for index in range(len(data.joints))
    }
    for joint_index, joint in enumerate(data.joints):
        if joint.parent_index >= 0:
            children[joint.parent_index].append(joint_index)
    return children


def _write_joint(
    lines: list[str],
    data: BvhData,
    children: dict[int, list[int]],
    joint_index: int,
    indentation: int,
    offset_override: np.ndarray | None = None,
) -> None:
    joint = data.joints[joint_index]
    indent = " " * indentation
    if joint.is_end_site:
        lines.append(f"{indent}End Site")
    else:
        lines.append(f"{indent}JOINT {joint.name}")
    lines.append(f"{indent}{{")

    offset = (
        np.asarray(offset_override, dtype=np.float64)
        if offset_override is not None
        else np.asarray(joint.offset, dtype=np.float64)
    )
    lines.append(
        f"{indent}  OFFSET "
        + " ".join(_format_number(value) for value in offset)
    )

    if not joint.is_end_site:
        lines.append(
            f"{indent}  CHANNELS {len(joint.channels)} "
            + " ".join(joint.channels)
        )
        for child_index in children[joint_index]:
            _write_joint(
                lines,
                data,
                children,
                child_index,
                indentation + 2,
            )

    lines.append(f"{indent}}}")


def _encode_pelvis_channels(
    joint: BvhJoint,
    position: np.ndarray,
    offset: np.ndarray,
    rotation_channels: list[str],
    euler_degrees: np.ndarray,
) -> list[float]:
    rotation_values = dict(zip(rotation_channels, euler_degrees))
    values: list[float] = []
    for channel in joint.channels:
        if channel in _POSITION_COMPONENTS:
            component = _POSITION_COMPONENTS[channel]
            values.append(float(position[component] - offset[component]))
        elif channel in rotation_values:
            values.append(float(rotation_values[channel]))
        else:
            raise ValueError(
                f"unsupported root channel for virtual-root export: {channel}"
            )
    return values


def add_virtual_root_to_bvh(
    data: BvhData,
    root_name: str = "MotionRoot",
    align_forward: bool = False,
    straighten_trajectory: bool = False,
) -> str:
    """返回新增地面 root、且世界姿势等价的 BVH 文本。"""

    if not data.joints or data.joints[0].parent_index != -1:
        raise ValueError("BVH must contain a valid hierarchy root")
    if data.joints[0].is_end_site:
        raise ValueError("BVH hierarchy root cannot be an End Site")
    if root_name in {joint.name for joint in data.joints}:
        raise ValueError(f"root name already exists: {root_name}")

    original_root = data.joints[0]
    position_channels = [
        channel
        for channel in original_root.channels
        if channel in _POSITION_COMPONENTS
    ]
    rotation_channels = [
        channel
        for channel in original_root.channels
        if channel.endswith("rotation")
    ]
    if position_channels != _ROOT_CHANNELS[:3]:
        raise ValueError(
            "current virtual-root exporter expects root position channels "
            f"{_ROOT_CHANNELS[:3]}, got {position_channels}"
        )

    motion = build_bvh_root_pelvis_motion(data)
    forward_alignment = 0.0
    if align_forward or straighten_trajectory:
        displacement = motion.root_positions[-1] - motion.root_positions[0]
        horizontal_length = float(np.hypot(displacement[0], displacement[2]))
        if horizontal_length <= 1e-8:
            raise ValueError("cannot align a root trajectory with no horizontal motion")
        # BVH 使用 Y 轴向上。把整段轨迹绕 Y 轴旋转到 +Z，仅改变
        # 世界朝向，不改变双脚相对骨盆的步态。
        forward_alignment = float(
            np.arctan2(-displacement[0], displacement[2])
        )

    cosine = np.cos(forward_alignment)
    sine = np.sin(forward_alignment)
    aligned_root_positions = motion.root_positions.copy()
    aligned_root_positions[:, 0] = (
        cosine * motion.root_positions[:, 0]
        + sine * motion.root_positions[:, 2]
    )
    aligned_root_positions[:, 2] = (
        -sine * motion.root_positions[:, 0]
        + cosine * motion.root_positions[:, 2]
    )
    if straighten_trajectory:
        horizontal_steps = np.diff(
            motion.root_positions[:, [0, 2]],
            axis=0,
        )
        cumulative_distance = np.concatenate(
            [
                np.zeros(1, dtype=np.float64),
                np.cumsum(np.linalg.norm(horizontal_steps, axis=1)),
            ]
        )
        aligned_root_positions[:, 0] = 0.0
        aligned_root_positions[:, 2] = cumulative_distance
    pelvis_euler_degrees = np.rad2deg(
        matrices_to_euler_for_channels(
            motion.pelvis_local_rotations,
            rotation_channels,
        )
    )
    heading_degrees = np.rad2deg(
        np.unwrap(
            np.arctan2(
                motion.root_rotations[:, 0, 2],
                motion.root_rotations[:, 2, 2],
            )
        )
    )
    heading_degrees += np.rad2deg(forward_alignment)
    if straighten_trajectory:
        # pelvis_local_rotations 已经移除了逐帧 heading；把 root 朝向固定在
        # 首帧对齐角，就能拉直路径而不改动四肢相对身体的动作。
        heading_degrees.fill(np.rad2deg(forward_alignment))

    pelvis_offset = np.zeros(3, dtype=np.float64)
    pelvis_offset[1] = motion.pelvis_local_positions[0, 1]
    children = _children_by_parent(data)
    lines = [
        "HIERARCHY",
        f"ROOT {root_name}",
        "{",
        "  OFFSET 0 0 0",
        "  CHANNELS 6 " + " ".join(_ROOT_CHANNELS),
    ]
    _write_joint(
        lines,
        data,
        children,
        joint_index=0,
        indentation=2,
        offset_override=pelvis_offset,
    )
    lines.extend(
        [
            "}",
            "MOTION",
            f"Frames: {data.motion.frame_count}",
            f"Frame Time: {_format_number(data.motion.frame_time)}",
        ]
    )

    original_root_channel_count = len(original_root.channels)
    for frame_index, source_frame in enumerate(data.motion.frames):
        root_values = [
            *aligned_root_positions[frame_index],
            0.0,
            heading_degrees[frame_index],
            0.0,
        ]
        pelvis_values = _encode_pelvis_channels(
            original_root,
            motion.pelvis_local_positions[frame_index],
            pelvis_offset,
            rotation_channels,
            pelvis_euler_degrees[frame_index],
        )
        remaining_values = source_frame[original_root_channel_count:]
        output_values = [
            *root_values,
            *pelvis_values,
            *remaining_values,
        ]
        lines.append(
            " ".join(_format_number(value) for value in output_values)
        )

    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Add a ground-level motion root to a BVH animation."
    )
    parser.add_argument("source_bvh", type=Path)
    parser.add_argument("output_bvh", type=Path)
    parser.add_argument(
        "--align-forward",
        action="store_true",
        help="Rotate the whole clip around Y so its net motion points to +Z.",
    )
    parser.add_argument(
        "--straighten-trajectory",
        action="store_true",
        help=(
            "Replace a gently curved ground trajectory with cumulative "
            "distance along +Z while preserving in-place body motion."
        ),
    )
    arguments = parser.parse_args()

    data = parse_bvh(arguments.source_bvh.read_text(encoding="utf-8"))
    output_text = add_virtual_root_to_bvh(
        data,
        align_forward=arguments.align_forward,
        straighten_trajectory=arguments.straighten_trajectory,
    )
    arguments.output_bvh.parent.mkdir(parents=True, exist_ok=True)
    arguments.output_bvh.write_text(output_text, encoding="utf-8")

    converted = parse_bvh(output_text)
    print(f"SOURCE_JOINTS={len(data.joints)}")
    print(f"OUTPUT_JOINTS={len(converted.joints)}")
    print(f"SOURCE_CHANNELS={data.channel_count}")
    print(f"OUTPUT_CHANNELS={converted.channel_count}")
    print(f"FRAMES={converted.motion.frame_count}")
    print(f"OUTPUT={arguments.output_bvh.resolve()}")


if __name__ == "__main__":
    main()
