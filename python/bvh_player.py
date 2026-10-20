from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter
from mpl_toolkits.mplot3d.art3d import Line3DCollection

from bvh_parser import BvhData, parse_bvh
from bvh_pose import WorldMotion, calculate_world_motion


def to_display_coordinates(world_positions: np.ndarray) -> np.ndarray:
    """把 BVH 的 Y-up 坐标旋转成适合 Matplotlib 显示的 Z-up 坐标。"""

    display_positions = np.empty_like(world_positions)
    display_positions[..., 0] = world_positions[..., 0]
    display_positions[..., 1] = -world_positions[..., 2]
    display_positions[..., 2] = world_positions[..., 1]
    return display_positions


def build_bone_segments(
    data: BvhData,
    frame_positions: np.ndarray,
) -> list[np.ndarray]:
    """根据父索引，把一帧关节位置转换为可绘制的骨骼线段。"""

    return [
        np.stack(
            (
                frame_positions[joint.parent_index],
                frame_positions[joint_index],
            )
        )
        for joint_index, joint in enumerate(data.joints)
        if joint.parent_index != -1
    ]


def set_equal_motion_bounds(
    axes: plt.Axes,
    display_positions: np.ndarray,
) -> None:
    """使用整段动作的包围盒固定坐标范围，避免播放时画面抖动。"""

    minimum = display_positions.min(axis=(0, 1))
    maximum = display_positions.max(axis=(0, 1))
    center = (minimum + maximum) * 0.5
    half_extent = max(float(np.max(maximum - minimum)) * 0.55, 1.0)

    axes.set_xlim(center[0] - half_extent, center[0] + half_extent)
    axes.set_ylim(center[1] - half_extent, center[1] + half_extent)
    axes.set_zlim(center[2] - half_extent, center[2] + half_extent)
    axes.set_box_aspect((1.0, 1.0, 1.0))


def save_bvh_animation(
    data: BvhData,
    world_motion: WorldMotion,
    output_path: Path,
) -> None:
    """将已经完成 FK 的 BVH 动作保存为 GIF。"""

    display_positions = to_display_coordinates(world_motion.positions)

    figure = plt.figure(figsize=(7, 7))
    axes = figure.add_subplot(111, projection="3d")
    set_equal_motion_bounds(axes, display_positions)

    axes.set_xlabel("BVH X")
    axes.set_ylabel("BVH -Z")
    axes.set_zlabel("BVH Y (Up)")
    axes.view_init(elev=15.0, azim=-75.0)

    initial_positions = display_positions[0]
    points = axes.scatter(
        initial_positions[:, 0],
        initial_positions[:, 1],
        initial_positions[:, 2],
        s=8,
        color="#f59e0b",
    )
    bones = Line3DCollection(
        build_bone_segments(data, initial_positions),
        colors="#2563eb",
        linewidths=2.0,
    )
    axes.add_collection3d(bones)

    def update(frame_index: int):
        frame_positions = display_positions[frame_index]
        points._offsets3d = (
            frame_positions[:, 0],
            frame_positions[:, 1],
            frame_positions[:, 2],
        )
        bones.set_segments(build_bone_segments(data, frame_positions))
        axes.set_title(
            f"MotionBridge BVH | "
            f"frame {frame_index + 1}/{data.motion.frame_count}"
        )
        return points, bones

    animation = FuncAnimation(
        figure,
        update,
        frames=data.motion.frame_count,
        interval=world_motion.frame_time * 1000.0,
        blit=False,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    frames_per_second = 1.0 / world_motion.frame_time
    animation.save(
        output_path,
        writer=PillowWriter(fps=frames_per_second),
        dpi=110,
    )
    plt.close(figure)


def main() -> None:
    argument_parser = argparse.ArgumentParser(
        description="Render a BVH file as a skeleton animation."
    )
    argument_parser.add_argument("input_path", type=Path)
    argument_parser.add_argument("output_path", type=Path)
    arguments = argument_parser.parse_args()

    source = arguments.input_path.read_text(encoding="utf-8")
    data = parse_bvh(source)
    world_motion = calculate_world_motion(data)
    save_bvh_animation(data, world_motion, arguments.output_path)

    print(f"frames: {data.motion.frame_count}")
    print(f"fps: {data.motion.fps:.3f}")
    print(f"saved: {arguments.output_path.resolve()}")


if __name__ == "__main__":
    main()
