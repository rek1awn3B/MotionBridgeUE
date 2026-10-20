from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from bvh_parser import parse_bvh
from bvh_pose import calculate_world_motion


def rotation_angle_degrees(rotations: np.ndarray) -> np.ndarray:
    """把一组相对旋转矩阵转换成最短旋转角。"""

    traces = np.trace(rotations, axis1=-2, axis2=-1)
    cosines = np.clip((traces - 1.0) * 0.5, -1.0, 1.0)
    return np.rad2deg(np.arccos(cosines))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="扫描 BVH，寻找横向摆脚较小且首尾姿势接近的步态窗口。"
    )
    parser.add_argument("path", type=Path)
    parser.add_argument("--window", type=int, default=165)
    parser.add_argument("--reference-start", type=int, default=530)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--min-root-distance", type=float, default=150.0)
    parser.add_argument("--max-root-distance", type=float, default=400.0)
    parser.add_argument(
        "--max-travel-angle",
        type=float,
        default=10.0,
        help="Maximum angle in degrees between body forward and travel direction.",
    )
    parser.add_argument(
        "--max-heading-change",
        type=float,
        default=15.0,
        help="Maximum body-heading range within one candidate window.",
    )
    args = parser.parse_args()

    data = parse_bvh(args.path.read_text(encoding="utf-8"))
    motion = calculate_world_motion(data)
    joint_indices = {joint.name: index for index, joint in enumerate(data.joints)}
    hips_index = joint_indices["Hips"]
    def find_joint(*names: str) -> int:
        for name in names:
            if name in joint_indices:
                return joint_indices[name]
        raise KeyError(f"none of the expected joints exist: {names}")

    right_ankle_index = find_joint("RightAnkle", "RightFoot")
    left_ankle_index = find_joint("LeftAnkle", "LeftFoot")

    hips_positions = motion.positions[:, hips_index]
    right_relative = motion.positions[:, right_ankle_index] - hips_positions
    left_relative = motion.positions[:, left_ankle_index] - hips_positions

    # BVH 局部 X 是人体左右方向。去掉竖直分量后，得到逐帧随骨盆朝向
    # 转动的水平右轴；在这个方向上测量，转弯不会被误报成横向摆脚。
    local_x = np.array([1.0, 0.0, 0.0], dtype=np.float64)
    heading_right = motion.rotations[:, hips_index] @ local_x
    heading_right[:, 1] = 0.0
    heading_lengths = np.linalg.norm(heading_right, axis=1)
    valid_heading = heading_lengths > 1e-8
    heading_right[valid_heading] /= heading_lengths[valid_heading, np.newaxis]
    heading_right[~valid_heading] = local_x

    local_forward = np.array([0.0, 0.0, 1.0], dtype=np.float64)
    heading_forward = motion.rotations[:, hips_index] @ local_forward
    heading_forward[:, 1] = 0.0
    forward_lengths = np.linalg.norm(heading_forward, axis=1)
    valid_forward = forward_lengths > 1e-8
    heading_forward[valid_forward] /= forward_lengths[valid_forward, np.newaxis]
    heading_forward[~valid_forward] = local_forward
    heading_angles = np.unwrap(
        np.arctan2(heading_forward[:, 0], heading_forward[:, 2])
    )

    right_lateral = np.einsum("fi,fi->f", right_relative, heading_right)
    left_lateral = np.einsum("fi,fi->f", left_relative, heading_right)

    candidates: list[
        tuple[float, int, float, float, float, float, float, float]
    ] = []
    for start in range(0, data.motion.frame_count - args.window + 1):
        end = start + args.window - 1
        root_delta = hips_positions[end] - hips_positions[start]
        root_distance = np.linalg.norm(root_delta[[0, 2]])
        if not args.min_root_distance <= root_distance <= args.max_root_distance:
            continue

        travel_direction = root_delta[[0, 2]] / root_distance
        mean_forward = np.mean(heading_forward[start : end + 1, :][:, [0, 2]], axis=0)
        mean_forward_length = np.linalg.norm(mean_forward)
        if mean_forward_length <= 1e-8:
            continue
        mean_forward /= mean_forward_length
        travel_angle = float(
            np.rad2deg(
                np.arccos(np.clip(np.dot(travel_direction, mean_forward), -1.0, 1.0))
            )
        )
        if travel_angle > args.max_travel_angle:
            continue
        heading_change = float(
            np.rad2deg(np.ptp(heading_angles[start : end + 1]))
        )
        if heading_change > args.max_heading_change:
            continue

        right_lateral_range = float(
            np.ptp(right_lateral[start : end + 1])
        )
        left_lateral_range = float(
            np.ptp(left_lateral[start : end + 1])
        )
        lateral_range = max(right_lateral_range, left_lateral_range)
        relative_rotations = np.einsum(
            "fji,fjk->fik",
            motion.rotations[start],
            motion.rotations[end],
        )
        seam_rotation = float(np.mean(rotation_angle_degrees(relative_rotations)))
        right_seam = float(
            np.linalg.norm(right_relative[end] - right_relative[start])
        )
        left_seam = float(
            np.linalg.norm(left_relative[end] - left_relative[start])
        )
        feet_seam = 0.5 * (right_seam + left_seam)

        # 横向摆脚是本次首要目标，但首尾姿势仍必须足够接近才能循环。
        score = (
            lateral_range
            + 1.5 * seam_rotation
            + 0.35 * feet_seam
            + 0.5 * travel_angle
            + 0.5 * heading_change
        )
        candidates.append(
            (
                score,
                start,
                lateral_range,
                seam_rotation,
                feet_seam,
                root_distance,
                travel_angle,
                heading_change,
            )
        )

    candidates.sort()
    reference = next(
        (item for item in candidates if item[1] == args.reference_start),
        None,
    )
    if reference is not None:
        print("reference score start end lateral_range seam_rotation feet_seam root_distance travel_angle heading_change")
        print(
            f"{reference[0]:9.3f} {reference[1]:5d} "
            f"{reference[1] + args.window - 1:5d} {reference[2]:13.3f} "
            f"{reference[3]:13.3f} {reference[4]:9.3f} {reference[5]:13.3f} "
            f"{reference[6]:12.3f} {reference[7]:14.3f}"
        )

    print("top score start end lateral_range seam_rotation feet_seam root_distance travel_angle heading_change")
    for score, start, lateral_range, seam_rotation, feet_seam, root_distance, travel_angle, heading_change in (
        candidates[: args.top]
    ):
        print(
            f"{score:9.3f} {start:5d} {start + args.window - 1:5d} "
            f"{lateral_range:13.3f} {seam_rotation:13.3f} "
            f"{feet_seam:9.3f} {root_distance:13.3f} {travel_angle:12.3f} "
            f"{heading_change:14.3f}"
        )

    print("top_lateral score start end lateral_range seam_rotation feet_seam root_distance travel_angle heading_change")
    for score, start, lateral_range, seam_rotation, feet_seam, root_distance, travel_angle, heading_change in sorted(
        candidates,
        key=lambda item: (item[2], item[3], item[4]),
    )[: args.top]:
        print(
            f"{score:9.3f} {start:5d} {start + args.window - 1:5d} "
            f"{lateral_range:13.3f} {seam_rotation:13.3f} "
            f"{feet_seam:9.3f} {root_distance:13.3f} {travel_angle:12.3f} "
            f"{heading_change:14.3f}"
        )


if __name__ == "__main__":
    main()
