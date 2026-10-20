from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from bvh_parser import BvhData, parse_bvh
from bvh_pose import decode_frame_pose, rotation_y


@dataclass(frozen=True)
class RootTranslationDecomposition:
    """将根平移拆为水平运动轨迹与留在身体上的局部平移。"""

    initial_position: np.ndarray
    trajectory_displacements: np.ndarray
    local_displacements: np.ndarray
    in_place_positions: np.ndarray

    def reconstruct_source_positions(self) -> np.ndarray:
        """重新组合两部分，用于验证拆分没有丢失原始根位置。"""

        return self.in_place_positions + self.trajectory_displacements


@dataclass(frozen=True)
class RootRotationDecomposition:
    """将根旋转拆为地面朝向变化与留在骨盆上的局部旋转。"""

    heading_angles: np.ndarray
    heading_rotations: np.ndarray
    in_place_rotations: np.ndarray

    def reconstruct_source_rotations(self) -> np.ndarray:
        """重新组合朝向与局部旋转，验证拆分没有丢失原始旋转。"""

        return np.einsum(
            "fij,fjk->fik",
            self.heading_rotations,
            self.in_place_rotations,
        )


@dataclass(frozen=True)
class RootPelvisMotion:
    """新增虚拟 root 后，root 轨迹与 pelvis 局部变换的逐帧结果。"""

    root_positions: np.ndarray
    root_rotations: np.ndarray
    pelvis_local_positions: np.ndarray
    pelvis_local_rotations: np.ndarray

    def compose_pelvis_world_positions(self) -> np.ndarray:
        """按照 root -> pelvis 层级重新计算 pelvis 世界位置。"""

        return self.root_positions + np.einsum(
            "fij,fj->fi",
            self.root_rotations,
            self.pelvis_local_positions,
        )

    def compose_pelvis_world_rotations(self) -> np.ndarray:
        """按照 root -> pelvis 层级重新计算 pelvis 世界旋转。"""

        return np.einsum(
            "fij,fjk->fik",
            self.root_rotations,
            self.pelvis_local_rotations,
        )


def extract_root_positions(data: BvhData) -> np.ndarray:
    """通过现有帧解码器提取每帧根关节的局部位置。"""

    if not data.joints:
        raise ValueError("BVH hierarchy must contain a root joint")
    if data.joints[0].parent_index != -1:
        raise ValueError("the first BVH joint must be the hierarchy root")
    if data.motion.frame_count == 0:
        raise ValueError("BVH motion must contain at least one frame")

    return np.stack(
        [
            decode_frame_pose(data, frame_index).translations[0]
            for frame_index in range(data.motion.frame_count)
        ],
        axis=0,
    )


def extract_root_rotations(data: BvhData) -> np.ndarray:
    """通过现有帧解码器提取每帧根关节的局部旋转矩阵。"""

    if not data.joints:
        raise ValueError("BVH hierarchy must contain a root joint")
    if data.joints[0].parent_index != -1:
        raise ValueError("the first BVH joint must be the hierarchy root")
    if data.motion.frame_count == 0:
        raise ValueError("BVH motion must contain at least one frame")

    return np.stack(
        [
            decode_frame_pose(data, frame_index).rotations[0]
            for frame_index in range(data.motion.frame_count)
        ],
        axis=0,
    )


def split_root_translation(
    root_positions: np.ndarray,
    up_axis: int = 1,
) -> RootTranslationDecomposition:
    """把根位置拆成水平轨迹、竖直局部位移和原地根位置。

    `up_axis=1` 对应当前 BVH 的 Y-up 约定。轨迹从零开始，只包含
    水平位移；原地位置保留第一帧的水平位置、绝对根高度和后续竖直起伏。
    """

    positions = np.asarray(root_positions, dtype=np.float64)
    if positions.ndim != 2 or positions.shape[1] != 3:
        raise ValueError(
            "root positions must have shape (frame_count, 3), "
            f"got {positions.shape}"
        )
    if positions.shape[0] == 0:
        raise ValueError("root positions must contain at least one frame")
    if up_axis not in {0, 1, 2}:
        raise ValueError(f"up axis must be 0, 1, or 2, got {up_axis}")
    if not np.all(np.isfinite(positions)):
        raise ValueError("root positions must contain only finite values")

    initial_position = positions[0].copy()
    total_displacements = positions - initial_position

    trajectory_displacements = total_displacements.copy()
    trajectory_displacements[:, up_axis] = 0.0

    local_displacements = total_displacements - trajectory_displacements
    in_place_positions = positions - trajectory_displacements

    return RootTranslationDecomposition(
        initial_position=initial_position,
        trajectory_displacements=trajectory_displacements,
        local_displacements=local_displacements,
        in_place_positions=in_place_positions,
    )


def split_bvh_root_translation(
    data: BvhData,
    up_axis: int = 1,
) -> RootTranslationDecomposition:
    """从解析后的 BVH 中提取并拆分根平移。"""

    return split_root_translation(
        extract_root_positions(data),
        up_axis=up_axis,
    )


def split_root_rotation(
    root_rotations: np.ndarray,
) -> RootRotationDecomposition:
    """拆出 Y-up BVH 根旋转中的水平朝向变化。

    当前源数据使用 Y-up，并以局部 +Z 作为人物前方向。每帧先把该方向
    旋转到世界空间，再投影到 XZ 地面求 heading。输出的 heading 从第一帧
    归零；初始朝向以及前倾、侧倾继续保留在 in-place 骨盆旋转中。
    """

    rotations = np.asarray(root_rotations, dtype=np.float64)
    if rotations.ndim != 3 or rotations.shape[1:] != (3, 3):
        raise ValueError(
            "root rotations must have shape (frame_count, 3, 3), "
            f"got {rotations.shape}"
        )
    if rotations.shape[0] == 0:
        raise ValueError("root rotations must contain at least one frame")
    if not np.all(np.isfinite(rotations)):
        raise ValueError("root rotations must contain only finite values")

    local_forward = np.array([0.0, 0.0, 1.0], dtype=np.float64)
    world_forwards = np.einsum("fij,j->fi", rotations, local_forward)
    planar_forwards = world_forwards.copy()
    planar_forwards[:, 1] = 0.0
    planar_lengths = np.linalg.norm(planar_forwards, axis=1)

    heading_angles = np.zeros(rotations.shape[0], dtype=np.float64)
    for frame_index, planar_length in enumerate(planar_lengths):
        if planar_length <= 1e-8:
            if frame_index == 0:
                raise ValueError(
                    "cannot determine heading from a vertical forward "
                    "direction in the first frame"
                )
            heading_angles[frame_index] = heading_angles[frame_index - 1]
            continue

        forward = planar_forwards[frame_index] / planar_length
        heading_angles[frame_index] = np.arctan2(
            forward[0],
            forward[2],
        )

    heading_angles = np.unwrap(heading_angles)
    heading_angles -= heading_angles[0]
    heading_rotations = np.stack(
        [rotation_y(angle) for angle in heading_angles],
        axis=0,
    )

    # R_source = R_heading @ R_in_place，因此左乘 heading 的逆即可
    # 得到仍然保留初始朝向、前倾和侧倾的骨盆局部旋转。
    in_place_rotations = np.einsum(
        "fji,fjk->fik",
        heading_rotations,
        rotations,
    )

    return RootRotationDecomposition(
        heading_angles=heading_angles,
        heading_rotations=heading_rotations,
        in_place_rotations=in_place_rotations,
    )


def split_bvh_root_rotation(data: BvhData) -> RootRotationDecomposition:
    """从解析后的 BVH 中提取并拆分根旋转。"""

    return split_root_rotation(extract_root_rotations(data))


def build_root_pelvis_motion(
    root_positions: np.ndarray,
    root_rotations: np.ndarray,
    up_axis: int = 1,
) -> RootPelvisMotion:
    """把原 Hips 变换改写为虚拟 root 与 pelvis 两层变换。

    虚拟 root 保存水平轨迹和相对首帧的 heading；pelvis 保存初始高度、
    竖直起伏、初始朝向以及前倾/侧倾。两层组合后必须精确还原原 Hips。
    """

    translation = split_root_translation(
        root_positions,
        up_axis=up_axis,
    )
    rotation = split_root_rotation(root_rotations)
    if (
        translation.trajectory_displacements.shape[0]
        != rotation.heading_rotations.shape[0]
    ):
        raise ValueError(
            "root position and rotation frame counts must match"
        )

    positions = np.asarray(root_positions, dtype=np.float64)
    positions_without_trajectory = (
        positions - translation.trajectory_displacements
    )

    # p_source = p_root + R_root @ p_pelvis，因此：
    # p_pelvis = R_root^-1 @ (p_source - p_root)。旋转矩阵的逆是转置。
    pelvis_local_positions = np.einsum(
        "fji,fj->fi",
        rotation.heading_rotations,
        positions_without_trajectory,
    )

    return RootPelvisMotion(
        root_positions=translation.trajectory_displacements,
        root_rotations=rotation.heading_rotations,
        pelvis_local_positions=pelvis_local_positions,
        pelvis_local_rotations=rotation.in_place_rotations,
    )


def build_bvh_root_pelvis_motion(data: BvhData) -> RootPelvisMotion:
    """从解析后的 BVH 构造虚拟 root 与 pelvis 两层动作。"""

    return build_root_pelvis_motion(
        extract_root_positions(data),
        extract_root_rotations(data),
    )


def main() -> None:
    argument_parser = argparse.ArgumentParser(
        description="Split BVH root translation into trajectory and in-place motion."
    )
    argument_parser.add_argument("path", type=Path)
    arguments = argument_parser.parse_args()

    data = parse_bvh(arguments.path.read_text(encoding="utf-8"))
    decomposition = split_bvh_root_translation(data)
    rotation_decomposition = split_bvh_root_rotation(data)
    root_pelvis_motion = build_bvh_root_pelvis_motion(data)
    trajectory = decomposition.trajectory_displacements
    local = decomposition.local_displacements
    reconstruction_error = np.max(
        np.abs(
            decomposition.reconstruct_source_positions()
            - extract_root_positions(data)
        )
    )

    horizontal_distance = np.linalg.norm(
        trajectory[:, [0, 2]],
        axis=1,
    )
    print(f"Frames: {data.motion.frame_count}")
    print(f"Initial root position: {decomposition.initial_position}")
    print(
        "Maximum horizontal distance from start: "
        f"{np.max(horizontal_distance):.4f} cm"
    )
    print(
        "Vertical displacement range: "
        f"[{np.min(local[:, 1]):.4f}, {np.max(local[:, 1]):.4f}] cm"
    )
    heading_degrees = np.rad2deg(rotation_decomposition.heading_angles)
    rotation_reconstruction_error = np.max(
        np.abs(
            rotation_decomposition.reconstruct_source_rotations()
            - extract_root_rotations(data)
        )
    )
    hierarchy_position_error = np.max(
        np.abs(
            root_pelvis_motion.compose_pelvis_world_positions()
            - extract_root_positions(data)
        )
    )
    hierarchy_rotation_error = np.max(
        np.abs(
            root_pelvis_motion.compose_pelvis_world_rotations()
            - extract_root_rotations(data)
        )
    )
    print(
        "Heading change range: "
        f"[{np.min(heading_degrees):.4f}, "
        f"{np.max(heading_degrees):.4f}] degrees"
    )
    print(f"Reconstruction max error: {reconstruction_error:.12f} cm")
    print(
        "Rotation reconstruction max error: "
        f"{rotation_reconstruction_error:.12f}"
    )
    print(
        "Root-pelvis hierarchy position max error: "
        f"{hierarchy_position_error:.12f} cm"
    )
    print(
        "Root-pelvis hierarchy rotation max error: "
        f"{hierarchy_rotation_error:.12f}"
    )


if __name__ == "__main__":
    main()
