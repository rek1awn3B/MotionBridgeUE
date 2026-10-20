import unittest
from pathlib import Path

import numpy as np

from bvh_parser import parse_bvh
from bvh_pose import (
    calculate_world_pose,
    decode_frame_pose,
    rotation_x,
    rotation_y,
    rotation_z,
)
from root_motion_bvh import (
    add_virtual_root_to_bvh,
    matrices_to_euler_xyz,
    matrices_to_euler_yxz,
    matrices_to_euler_zyx,
)


class EulerZyxTests(unittest.TestCase):
    def test_round_trip_preserves_rotation_matrices(self) -> None:
        rotations = np.stack(
            [
                rotation_z(np.deg2rad(10.0))
                @ rotation_y(np.deg2rad(20.0))
                @ rotation_x(np.deg2rad(30.0)),
                rotation_z(np.deg2rad(-170.0))
                @ rotation_y(np.deg2rad(40.0))
                @ rotation_x(np.deg2rad(-60.0)),
            ],
            axis=0,
        )

        angles = matrices_to_euler_zyx(rotations)
        reconstructed = np.stack(
            [
                rotation_z(angle_z)
                @ rotation_y(angle_y)
                @ rotation_x(angle_x)
                for angle_z, angle_y, angle_x in angles
            ],
            axis=0,
        )

        np.testing.assert_allclose(reconstructed, rotations, atol=1e-9)


class EulerYxzTests(unittest.TestCase):
    def test_round_trip_preserves_rotation_matrices(self) -> None:
        rotations = np.stack(
            [
                rotation_y(np.deg2rad(20.0))
                @ rotation_x(np.deg2rad(30.0))
                @ rotation_z(np.deg2rad(10.0)),
                rotation_y(np.deg2rad(-160.0))
                @ rotation_x(np.deg2rad(40.0))
                @ rotation_z(np.deg2rad(-60.0)),
            ],
            axis=0,
        )

        angles = matrices_to_euler_yxz(rotations)
        reconstructed = np.stack(
            [
                rotation_y(angle_y)
                @ rotation_x(angle_x)
                @ rotation_z(angle_z)
                for angle_y, angle_x, angle_z in angles
            ],
            axis=0,
        )

        np.testing.assert_allclose(reconstructed, rotations, atol=1e-9)


class EulerXyzTests(unittest.TestCase):
    def test_round_trip_preserves_rotation_matrices(self) -> None:
        rotations = np.stack(
            [
                rotation_x(np.deg2rad(30.0))
                @ rotation_y(np.deg2rad(20.0))
                @ rotation_z(np.deg2rad(10.0)),
                rotation_x(np.deg2rad(-60.0))
                @ rotation_y(np.deg2rad(40.0))
                @ rotation_z(np.deg2rad(-170.0)),
            ],
            axis=0,
        )

        angles = matrices_to_euler_xyz(rotations)
        reconstructed = np.stack(
            [
                rotation_x(angle_x)
                @ rotation_y(angle_y)
                @ rotation_z(angle_z)
                for angle_x, angle_y, angle_z in angles
            ],
            axis=0,
        )

        np.testing.assert_allclose(reconstructed, rotations, atol=1e-9)


class VirtualRootBvhTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        fixture_path = (
            Path(__file__).resolve().parent.parent
            / "data"
            / "bvh"
            / "test_freebvh.bvh"
        )
        cls.source = parse_bvh(fixture_path.read_text(encoding="utf-8"))
        cls.converted = parse_bvh(add_virtual_root_to_bvh(cls.source))

    def test_adds_one_root_and_six_channels(self) -> None:
        self.assertEqual(
            len(self.converted.joints),
            len(self.source.joints) + 1,
        )
        self.assertEqual(
            self.converted.channel_count,
            self.source.channel_count + 6,
        )
        self.assertEqual(self.converted.joints[0].name, "MotionRoot")
        self.assertEqual(
            self.converted.joints[1].name,
            self.source.joints[0].name,
        )
        self.assertEqual(self.converted.joints[1].parent_index, 0)
        self.assertAlmostEqual(
            self.converted.joints[1].offset[1],
            98.2948,
        )

    def test_preserves_every_original_joints_world_pose(self) -> None:
        self.assertEqual(
            self.converted.motion.frame_count,
            self.source.motion.frame_count,
        )

        for frame_index in range(self.source.motion.frame_count):
            source_world = calculate_world_pose(
                self.source,
                decode_frame_pose(self.source, frame_index),
            )
            converted_world = calculate_world_pose(
                self.converted,
                decode_frame_pose(self.converted, frame_index),
            )

            np.testing.assert_allclose(
                converted_world.positions[1:],
                source_world.positions,
                atol=1e-6,
            )
            np.testing.assert_allclose(
                converted_world.rotations[1:],
                source_world.rotations,
                atol=1e-8,
            )

    def test_straightened_root_moves_monotonically_along_z(self) -> None:
        straightened = parse_bvh(
            add_virtual_root_to_bvh(
                self.source,
                straighten_trajectory=True,
            )
        )
        root_positions = np.stack(
            [
                decode_frame_pose(straightened, frame).translations[0]
                for frame in range(straightened.motion.frame_count)
            ]
        )

        np.testing.assert_allclose(root_positions[:, 0], 0.0, atol=1e-9)
        np.testing.assert_allclose(root_positions[:, 1], 0.0, atol=1e-9)
        self.assertTrue(np.all(np.diff(root_positions[:, 2]) >= -1e-9))


class VirtualRootYxzBvhTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        fixture_path = (
            Path(__file__).resolve().parent.parent
            / "data"
            / "bvh"
            / "100style"
            / "Neutral_FW_frames_507_687.bvh"
        )
        cls.source = parse_bvh(fixture_path.read_text(encoding="utf-8"))
        cls.converted = parse_bvh(add_virtual_root_to_bvh(cls.source))

    def test_preserves_yxz_source_world_pose(self) -> None:
        self.assertEqual(
            self.source.joints[0].channels[-3:],
            ["Yrotation", "Xrotation", "Zrotation"],
        )

        for frame_index in range(self.source.motion.frame_count):
            source_world = calculate_world_pose(
                self.source,
                decode_frame_pose(self.source, frame_index),
            )
            converted_world = calculate_world_pose(
                self.converted,
                decode_frame_pose(self.converted, frame_index),
            )

            np.testing.assert_allclose(
                converted_world.positions[1:],
                source_world.positions,
                atol=1e-6,
            )
            np.testing.assert_allclose(
                converted_world.rotations[1:],
                source_world.rotations,
                atol=1e-8,
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
