from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from bvh_parser import BvhData


@dataclass
class LocalPose:
    """BVH 某一帧中，所有节点的局部位移和局部旋转。"""

    translations: np.ndarray
    rotations: np.ndarray


@dataclass
class WorldPose:
    """BVH 某一帧中，所有节点的世界位置和世界旋转。"""

    positions: np.ndarray
    rotations: np.ndarray


@dataclass
class WorldMotion:
    """整段 BVH 动作中，每一帧所有节点的世界变换。"""

    positions: np.ndarray
    rotations: np.ndarray
    frame_time: float


def rotation_x(angle_radians: float) -> np.ndarray:
    cosine = np.cos(angle_radians)
    sine = np.sin(angle_radians)
    return np.array(
        [
            [1.0, 0.0, 0.0],
            [0.0, cosine, -sine],
            [0.0, sine, cosine],
        ],
        dtype=np.float64,
    )


def rotation_y(angle_radians: float) -> np.ndarray:
    cosine = np.cos(angle_radians)
    sine = np.sin(angle_radians)
    return np.array(
        [
            [cosine, 0.0, sine],
            [0.0, 1.0, 0.0],
            [-sine, 0.0, cosine],
        ],
        dtype=np.float64,
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


_ROTATION_BUILDERS = {
    "Xrotation": rotation_x,
    "Yrotation": rotation_y,
    "Zrotation": rotation_z,
}

_POSITION_COMPONENTS = {
    "Xposition": 0,
    "Yposition": 1,
    "Zposition": 2,
}


def decode_frame_pose(data: BvhData, frame_index: int) -> LocalPose:
    """按照各关节声明的 CHANNELS 顺序解码一帧 BVH 数据。"""

    if not 0 <= frame_index < data.motion.frame_count:
        raise IndexError(f"frame index out of range: {frame_index}")

    frame = data.motion.frames[frame_index]
    joint_count = len(data.joints)

    # 局部位移从静态 OFFSET 开始；位置通道会在对应分量上继续相加。
    translations = np.array(
        [joint.offset for joint in data.joints],
        dtype=np.float64,
    )

    # 每个关节在没有旋转通道时，局部旋转都是单位矩阵。
    rotations = np.repeat(
        np.eye(3, dtype=np.float64)[np.newaxis, :, :],
        joint_count,
        axis=0,
    )

    channel_index = 0

    for joint_index, joint in enumerate(data.joints):
        for channel in joint.channels:
            value = frame[channel_index]
            channel_index += 1

            if channel in _POSITION_COMPONENTS:
                component = _POSITION_COMPONENTS[channel]
                translations[joint_index, component] += value
                continue

            if channel in _ROTATION_BUILDERS:
                angle_radians = np.deg2rad(value)
                channel_rotation = _ROTATION_BUILDERS[channel](
                    angle_radians
                )

                # 严格保留 BVH 文件声明的 Euler 旋转顺序。
                rotations[joint_index] = (
                    rotations[joint_index] @ channel_rotation
                )
                continue

            raise ValueError(f"unsupported BVH channel: {channel!r}")

    if channel_index != data.channel_count:
        raise ValueError(
            f"decoded {channel_index} channels, "
            f"expected {data.channel_count}"
        )

    return LocalPose(
        translations=translations,
        rotations=rotations,
    )


def calculate_world_pose(data: BvhData, local_pose: LocalPose) -> WorldPose:
    """沿父子层级执行 FK，将局部姿态转换为世界姿态。"""

    joint_count = len(data.joints)
    expected_translation_shape = (joint_count, 3)
    expected_rotation_shape = (joint_count, 3, 3)

    if local_pose.translations.shape != expected_translation_shape:
        raise ValueError(
            "local translation shape must be "
            f"{expected_translation_shape}, got "
            f"{local_pose.translations.shape}"
        )
    if local_pose.rotations.shape != expected_rotation_shape:
        raise ValueError(
            "local rotation shape must be "
            f"{expected_rotation_shape}, got "
            f"{local_pose.rotations.shape}"
        )

    world_positions = np.zeros(
        expected_translation_shape,
        dtype=np.float64,
    )
    world_rotations = np.zeros(
        expected_rotation_shape,
        dtype=np.float64,
    )

    for joint_index, joint in enumerate(data.joints):
        if joint.parent_index == -1:
            world_positions[joint_index] = (
                local_pose.translations[joint_index]
            )
            world_rotations[joint_index] = (
                local_pose.rotations[joint_index]
            )
            continue

        parent_index = joint.parent_index

        # 当前节点的局部位移位于父坐标系中，要先用父世界旋转
        # 把它转到世界坐标系，再加到父节点的世界位置上。
        world_positions[joint_index] = (
            world_positions[parent_index]
            + world_rotations[parent_index]
            @ local_pose.translations[joint_index]
        )

        # 当前节点的世界朝向由父世界旋转和自身局部旋转共同组成。
        world_rotations[joint_index] = (
            world_rotations[parent_index]
            @ local_pose.rotations[joint_index]
        )

    return WorldPose(
        positions=world_positions,
        rotations=world_rotations,
    )


def calculate_world_motion(data: BvhData) -> WorldMotion:
    """解码并通过 FK 计算整段 BVH 动作。"""

    frame_count = data.motion.frame_count
    joint_count = len(data.joints)

    world_positions = np.zeros(
        (frame_count, joint_count, 3),
        dtype=np.float64,
    )
    world_rotations = np.zeros(
        (frame_count, joint_count, 3, 3),
        dtype=np.float64,
    )

    for frame_index in range(frame_count):
        local_pose = decode_frame_pose(data, frame_index)
        world_pose = calculate_world_pose(data, local_pose)

        world_positions[frame_index] = world_pose.positions
        world_rotations[frame_index] = world_pose.rotations

    return WorldMotion(
        positions=world_positions,
        rotations=world_rotations,
        frame_time=data.motion.frame_time,
    )
