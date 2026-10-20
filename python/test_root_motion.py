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
from root_motion import (
    build_bvh_root_pelvis_motion,
    build_root_pelvis_motion,
    extract_root_positions,
    extract_root_rotations,
    split_bvh_root_translation,
    split_bvh_root_rotation,
    split_root_rotation,
    split_root_translation,
)
from test_bvh_parser import MINIMAL_BVH


class RootTranslationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.positions = np.array(
            [
                [10.0, 100.0, -5.0],
                [13.0, 98.0, -1.0],
                [18.0, 103.0, 7.0],
            ],
            dtype=np.float64,
        )

    def test_splits_horizontal_trajectory_from_vertical_motion(self) -> None:
        result = split_root_translation(self.positions, up_axis=1)

        np.testing.assert_allclose(
            result.trajectory_displacements,
            [
                [0.0, 0.0, 0.0],
                [3.0, 0.0, 4.0],
                [8.0, 0.0, 12.0],
            ],
        )
        np.testing.assert_allclose(
            result.local_displacements,
            [
                [0.0, 0.0, 0.0],
                [0.0, -2.0, 0.0],
                [0.0, 3.0, 0.0],
            ],
        )

    def test_in_place_motion_preserves_initial_position_and_height_changes(self) -> None:
        result = split_root_translation(self.positions, up_axis=1)

        np.testing.assert_allclose(
            result.in_place_positions,
            [
                [10.0, 100.0, -5.0],
                [10.0, 98.0, -5.0],
                [10.0, 103.0, -5.0],
            ],
        )

    def test_reconstruction_exactly_recovers_source_positions(self) -> None:
        result = split_root_translation(self.positions, up_axis=1)

        np.testing.assert_allclose(
            result.reconstruct_source_positions(),
            self.positions,
        )

    def test_does_not_modify_the_input_array(self) -> None:
        original = self.positions.copy()

        split_root_translation(self.positions, up_axis=1)

        np.testing.assert_array_equal(self.positions, original)

    def test_supports_a_different_up_axis(self) -> None:
        result = split_root_translation(self.positions, up_axis=2)

        np.testing.assert_allclose(
            result.trajectory_displacements,
            [
                [0.0, 0.0, 0.0],
                [3.0, -2.0, 0.0],
                [8.0, 3.0, 0.0],
            ],
        )

    def test_rejects_invalid_inputs(self) -> None:
        with self.assertRaisesRegex(ValueError, "shape"):
            split_root_translation(np.zeros((3, 2)))
        with self.assertRaisesRegex(ValueError, "at least one frame"):
            split_root_translation(np.zeros((0, 3)))
        with self.assertRaisesRegex(ValueError, "up axis"):
            split_root_translation(self.positions, up_axis=3)
        with self.assertRaisesRegex(ValueError, "finite"):
            split_root_translation(
                np.array([[0.0, np.nan, 0.0]]),
            )


class BvhRootTranslationTests(unittest.TestCase):
    def test_extracts_root_positions_through_the_channel_decoder(self) -> None:
        data = parse_bvh(MINIMAL_BVH)

        np.testing.assert_allclose(
            extract_root_positions(data),
            [
                [0.0, 0.0, 0.0],
                [1.0, 2.0, 3.0],
            ],
        )

    def test_splits_parsed_bvh_root_motion(self) -> None:
        result = split_bvh_root_translation(parse_bvh(MINIMAL_BVH))

        np.testing.assert_allclose(
            result.trajectory_displacements,
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 3.0],
            ],
        )
        np.testing.assert_allclose(
            result.in_place_positions,
            [
                [0.0, 0.0, 0.0],
                [0.0, 2.0, 0.0],
            ],
        )


class RootRotationTests(unittest.TestCase):
    def test_separates_heading_change_from_constant_pelvis_tilt(self) -> None:
        initial_heading = rotation_y(np.deg2rad(30.0))
        pelvis_tilt = (
            rotation_x(np.deg2rad(10.0))
            @ rotation_z(np.deg2rad(-5.0))
        )
        source_rotations = np.stack(
            [
                rotation_y(np.deg2rad(0.0))
                @ initial_heading
                @ pelvis_tilt,
                rotation_y(np.deg2rad(15.0))
                @ initial_heading
                @ pelvis_tilt,
                rotation_y(np.deg2rad(45.0))
                @ initial_heading
                @ pelvis_tilt,
            ],
            axis=0,
        )

        result = split_root_rotation(source_rotations)

        np.testing.assert_allclose(
            np.rad2deg(result.heading_angles),
            [0.0, 15.0, 45.0],
            atol=1e-9,
        )
        np.testing.assert_allclose(
            result.in_place_rotations,
            np.repeat(
                source_rotations[0][np.newaxis, :, :],
                3,
                axis=0,
            ),
            atol=1e-9,
        )

    def test_unwraps_heading_across_the_signed_angle_boundary(self) -> None:
        source_rotations = np.stack(
            [
                rotation_y(np.deg2rad(170.0)),
                rotation_y(np.deg2rad(-170.0)),
            ],
            axis=0,
        )

        result = split_root_rotation(source_rotations)

        np.testing.assert_allclose(
            np.rad2deg(result.heading_angles),
            [0.0, 20.0],
            atol=1e-9,
        )

    def test_rotation_reconstruction_recovers_source_matrices(self) -> None:
        source_rotations = np.stack(
            [
                rotation_y(np.deg2rad(20.0))
                @ rotation_x(np.deg2rad(5.0)),
                rotation_y(np.deg2rad(55.0))
                @ rotation_x(np.deg2rad(-8.0)),
            ],
            axis=0,
        )

        result = split_root_rotation(source_rotations)

        np.testing.assert_allclose(
            result.reconstruct_source_rotations(),
            source_rotations,
            atol=1e-9,
        )

    def test_rotation_split_rejects_invalid_input(self) -> None:
        with self.assertRaisesRegex(ValueError, "shape"):
            split_root_rotation(np.zeros((3, 4)))
        with self.assertRaisesRegex(ValueError, "at least one frame"):
            split_root_rotation(np.zeros((0, 3, 3)))
        with self.assertRaisesRegex(ValueError, "finite"):
            split_root_rotation(
                np.full((1, 3, 3), np.nan),
            )


class BvhRootRotationTests(unittest.TestCase):
    def test_extracts_and_splits_parsed_bvh_root_rotations(self) -> None:
        data = parse_bvh(MINIMAL_BVH)
        source_rotations = extract_root_rotations(data)
        result = split_bvh_root_rotation(data)

        self.assertEqual(source_rotations.shape, (2, 3, 3))
        np.testing.assert_allclose(
            result.reconstruct_source_rotations(),
            source_rotations,
            atol=1e-9,
        )


class RootPelvisMotionTests(unittest.TestCase):
    def test_two_level_hierarchy_recovers_original_hips_transform(self) -> None:
        positions = np.array(
            [
                [2.0, 100.0, -3.0],
                [12.0, 98.0, 17.0],
                [22.0, 103.0, 37.0],
            ],
            dtype=np.float64,
        )
        rotations = np.stack(
            [
                rotation_y(np.deg2rad(20.0))
                @ rotation_x(np.deg2rad(5.0)),
                rotation_y(np.deg2rad(65.0))
                @ rotation_x(np.deg2rad(-8.0)),
                rotation_y(np.deg2rad(110.0))
                @ rotation_z(np.deg2rad(12.0)),
            ],
            axis=0,
        )

        result = build_root_pelvis_motion(positions, rotations)

        np.testing.assert_allclose(
            result.compose_pelvis_world_positions(),
            positions,
            atol=1e-9,
        )
        np.testing.assert_allclose(
            result.compose_pelvis_world_rotations(),
            rotations,
            atol=1e-9,
        )

    def test_pelvis_position_accounts_for_parent_heading(self) -> None:
        positions = np.array(
            [
                [1.0, 100.0, 0.0],
                [11.0, 100.0, 0.0],
            ],
            dtype=np.float64,
        )
        rotations = np.stack(
            [
                rotation_y(0.0),
                rotation_y(np.pi / 2.0),
            ],
            axis=0,
        )

        result = build_root_pelvis_motion(positions, rotations)

        # 第二帧扣掉10 cm轨迹后，世界偏移仍为(1,100,0)。
        # root 已转90度，所以 pelvis 局部坐标必须逆转后再保存。
        np.testing.assert_allclose(
            result.pelvis_local_positions[1],
            [0.0, 100.0, 1.0],
            atol=1e-9,
        )
        np.testing.assert_allclose(
            result.compose_pelvis_world_positions(),
            positions,
            atol=1e-9,
        )

    def test_rejects_mismatched_position_and_rotation_frame_counts(self) -> None:
        with self.assertRaisesRegex(ValueError, "frame counts must match"):
            build_root_pelvis_motion(
                np.zeros((2, 3)),
                np.repeat(
                    np.eye(3)[np.newaxis, :, :],
                    3,
                    axis=0,
                ),
            )

    def test_real_bvh_preserves_every_joints_world_pose(self) -> None:
        fixture_path = (
            Path(__file__).resolve().parent.parent
            / "data"
            / "bvh"
            / "test_freebvh.bvh"
        )
        data = parse_bvh(fixture_path.read_text(encoding="utf-8"))
        root_pelvis = build_bvh_root_pelvis_motion(data)
        composed_positions = (
            root_pelvis.compose_pelvis_world_positions()
        )
        composed_rotations = (
            root_pelvis.compose_pelvis_world_rotations()
        )

        for frame_index in range(data.motion.frame_count):
            source_local = decode_frame_pose(data, frame_index)
            source_world = calculate_world_pose(data, source_local)

            reconstructed_local = type(source_local)(
                translations=source_local.translations.copy(),
                rotations=source_local.rotations.copy(),
            )
            reconstructed_local.translations[0] = (
                composed_positions[frame_index]
            )
            reconstructed_local.rotations[0] = (
                composed_rotations[frame_index]
            )
            reconstructed_world = calculate_world_pose(
                data,
                reconstructed_local,
            )

            np.testing.assert_allclose(
                reconstructed_world.positions,
                source_world.positions,
                atol=1e-9,
            )
            np.testing.assert_allclose(
                reconstructed_world.rotations,
                source_world.rotations,
                atol=1e-9,
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
