import unittest

import numpy as np

from bvh_parser import parse_bvh
from bvh_pose import (
    calculate_world_motion,
    calculate_world_pose,
    decode_frame_pose,
    rotation_x,
    rotation_y,
    rotation_z,
)
from test_bvh_parser import MINIMAL_BVH


class BvhPoseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.data = parse_bvh(MINIMAL_BVH)

    def test_decodes_position_channels_and_preserves_offsets(self) -> None:
        pose = decode_frame_pose(self.data, 1)

        np.testing.assert_allclose(pose.translations[0], [1.0, 2.0, 3.0])
        np.testing.assert_allclose(pose.translations[1], [0.0, 10.0, 0.0])
        np.testing.assert_allclose(pose.translations[2], [0.0, 8.0, 0.0])

    def test_composes_each_joints_declared_rotation_order(self) -> None:
        pose = decode_frame_pose(self.data, 1)

        expected_root = (
            rotation_z(np.deg2rad(10.0))
            @ rotation_x(np.deg2rad(20.0))
            @ rotation_y(np.deg2rad(30.0))
        )
        expected_chest = (
            rotation_y(np.deg2rad(40.0))
            @ rotation_z(np.deg2rad(50.0))
            @ rotation_x(np.deg2rad(60.0))
        )

        np.testing.assert_allclose(pose.rotations[0], expected_root)
        np.testing.assert_allclose(pose.rotations[1], expected_chest)
        np.testing.assert_allclose(pose.rotations[2], np.eye(3))

    def test_rejects_frame_index_outside_motion(self) -> None:
        with self.assertRaisesRegex(IndexError, "frame index out of range"):
            decode_frame_pose(self.data, 2)


FK_BVH = """\
HIERARCHY
ROOT Root
{
    OFFSET 0 0 0
    CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
    JOINT Child
    {
        OFFSET 1 0 0
        CHANNELS 3 Zrotation Xrotation Yrotation
        End Site
        {
            OFFSET 1 0 0
        }
    }
}
MOTION
Frames: 1
Frame Time: 0.0333333
0 0 0 90 0 0 0 0 0
"""


class BvhForwardKinematicsTests(unittest.TestCase):
    def test_parent_rotation_moves_child_and_end_site(self) -> None:
        data = parse_bvh(FK_BVH)
        local_pose = decode_frame_pose(data, 0)
        world_pose = calculate_world_pose(data, local_pose)

        np.testing.assert_allclose(
            world_pose.positions,
            [
                [0.0, 0.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.0, 2.0, 0.0],
            ],
            atol=1e-9,
        )

    def test_calculates_every_frame_with_expected_array_shapes(self) -> None:
        data = parse_bvh(MINIMAL_BVH)
        world_motion = calculate_world_motion(data)

        self.assertEqual(world_motion.positions.shape, (2, 3, 3))
        self.assertEqual(world_motion.rotations.shape, (2, 3, 3, 3))
        self.assertAlmostEqual(
            world_motion.frame_time,
            data.motion.frame_time,
        )

        first_local_pose = decode_frame_pose(data, 0)
        first_world_pose = calculate_world_pose(data, first_local_pose)
        np.testing.assert_allclose(
            world_motion.positions[0],
            first_world_pose.positions,
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
