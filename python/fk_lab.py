from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt


@dataclass
class Joint:
    """一个关节节点；offset 使用父关节的局部坐标表示。"""

    name: str
    parent_index: int
    offset: np.ndarray
    local_rotation: np.ndarray

    def __post_init__(self) -> None:
        self.offset = np.asarray(self.offset, dtype=np.float64)
        self.local_rotation = np.asarray(
            self.local_rotation,
            dtype=np.float64,
        )

        if self.offset.shape != (3,):
            raise ValueError(f"{self.name}: offset must have shape (3,)")
        if self.local_rotation.shape != (3, 3):
            raise ValueError(
                f"{self.name}: local_rotation must have shape (3, 3)"
            )


@dataclass
class Skeleton:
    joints: list[Joint]

    def __post_init__(self) -> None:
        if not self.joints:
            raise ValueError("A skeleton must contain at least one joint.")

        for index, joint in enumerate(self.joints):
            if index == 0:
                if joint.parent_index != -1:
                    raise ValueError("The first joint must be the root.")
            elif not 0 <= joint.parent_index < index:
                raise ValueError(
                    f"{joint.name}: parent must appear before its child"
                )


def rotation_z(angle_radians: float) -> np.ndarray:
    cosine = np.cos(angle_radians)
    sine = np.sin(angle_radians)
    return np.array(
        [
            [cosine, -sine, 0.0],
            [sine, cosine, 0.0],
            [0.0, 0.0, 1.0],
        ],
        dtype=np.float64,
    )


def forward_kinematics(
    skeleton: Skeleton,
    root_translation: np.ndarray | None = None,
) -> tuple[np.ndarray, np.ndarray]:
    """返回所有关节的世界位置和世界旋转。"""

    if root_translation is None:
        root_translation = np.zeros(3, dtype=np.float64)
    else:
        root_translation = np.asarray(root_translation, dtype=np.float64)

    if root_translation.shape != (3,):
        raise ValueError("root_translation must have shape (3,)")

    joint_count = len(skeleton.joints)
    world_positions = np.zeros((joint_count, 3), dtype=np.float64)
    world_rotations = np.zeros((joint_count, 3, 3), dtype=np.float64)

    for index, joint in enumerate(skeleton.joints):
        if joint.parent_index == -1:
            world_positions[index] = root_translation + joint.offset
            world_rotations[index] = joint.local_rotation
            continue

        parent_index = joint.parent_index

        # offset 位于父坐标系中，所以由父关节的世界旋转带到世界空间。
        world_positions[index] = (
            world_positions[parent_index]
            + world_rotations[parent_index] @ joint.offset
        )

        # 子关节的世界朝向等于父世界旋转乘以子局部旋转。
        world_rotations[index] = (
            world_rotations[parent_index] @ joint.local_rotation
        )

    return world_positions, world_rotations


def plot_skeleton(
    skeleton: Skeleton,
    world_positions: np.ndarray,
    output_path: Path,
) -> None:
    figure = plt.figure(figsize=(7, 7))
    axes = figure.add_subplot(111, projection="3d")

    for index, joint in enumerate(skeleton.joints):
        position = world_positions[index]
        axes.scatter(*position, s=45)
        axes.text(*position, f" {joint.name}")

        if joint.parent_index != -1:
            parent_position = world_positions[joint.parent_index]
            axes.plot(
                [parent_position[0], position[0]],
                [parent_position[1], position[1]],
                [parent_position[2], position[2]],
                linewidth=3,
            )

    minimum = world_positions.min(axis=0)
    maximum = world_positions.max(axis=0)
    center = (minimum + maximum) * 0.5
    half_extent = max(float(np.max(maximum - minimum)) * 0.6, 1.0)

    axes.set_xlim(center[0] - half_extent, center[0] + half_extent)
    axes.set_ylim(center[1] - half_extent, center[1] + half_extent)
    axes.set_zlim(center[2] - half_extent, center[2] + half_extent)
    axes.set_xlabel("X")
    axes.set_ylabel("Y")
    axes.set_zlabel("Z")
    axes.set_title("MotionBridge FK static skeleton")
    axes.set_box_aspect((1.0, 1.0, 1.0))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, dpi=160, bbox_inches="tight")
    plt.close(figure)


def create_demo_skeleton() -> Skeleton:
    identity = np.eye(3, dtype=np.float64)
    return Skeleton(
        joints=[
            Joint("root", -1, [0.0, 0.0, 0.0], identity),
            Joint(
                "shoulder",
                0,
                [0.0, 1.0, 0.0],
                rotation_z(np.pi / 2.0),
            ),
            Joint("hand", 1, [1.0, 0.0, 0.0], identity),
            Joint("hip", 0, [-0.6, -1.0, 0.0], identity),
            Joint("foot", 3, [0.0, -1.0, 0.3], identity),
        ]
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("artifacts/fk_static_skeleton.png"),
    )
    arguments = parser.parse_args()

    skeleton = create_demo_skeleton()
    world_positions, _ = forward_kinematics(skeleton)
    plot_skeleton(skeleton, world_positions, arguments.output)

    for joint, position in zip(skeleton.joints, world_positions):
        print(f"{joint.name:>8}: {position}")
    print(f"saved: {arguments.output.resolve()}")


if __name__ == "__main__":
    main()
