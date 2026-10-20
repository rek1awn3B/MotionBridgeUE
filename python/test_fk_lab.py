import unittest

import numpy as np

from fk_lab import Joint, Skeleton, forward_kinematics, rotation_z


class ForwardKinematicsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.identity = np.eye(3, dtype=np.float64)

    def create_three_joint_skeleton(
        self,
        hand_rotation: np.ndarray | None = None,
    ) -> Skeleton:
        if hand_rotation is None:
            hand_rotation = self.identity

        return Skeleton(
            joints=[
                Joint("root", -1, [0.0, 0.0, 0.0], self.identity),
                Joint(
                    "shoulder",
                    0,
                    [0.0, 1.0, 0.0],
                    rotation_z(np.pi / 2.0),
                ),
                Joint("hand", 1, [1.0, 0.0, 0.0], hand_rotation),
            ]
        )

    def test_parent_rotation_moves_child_offset(self) -> None:
        skeleton = self.create_three_joint_skeleton()
        positions, _ = forward_kinematics(skeleton)

        np.testing.assert_allclose(
            positions,
            [[0.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 2.0, 0.0]],
            atol=1e-9,
        )

    def test_joint_rotation_does_not_move_its_own_pivot(self) -> None:
        unrotated = self.create_three_joint_skeleton(self.identity)
        rotated = self.create_three_joint_skeleton(rotation_z(np.pi / 3.0))

        unrotated_positions, _ = forward_kinematics(unrotated)
        rotated_positions, _ = forward_kinematics(rotated)

        np.testing.assert_allclose(
            unrotated_positions[2],
            rotated_positions[2],
            atol=1e-9,
        )

    def test_joint_rotation_moves_its_descendant(self) -> None:
        skeleton = self.create_three_joint_skeleton(rotation_z(np.pi / 2.0))
        skeleton.joints.append(
            Joint("finger", 2, [1.0, 0.0, 0.0], self.identity)
        )

        positions, _ = forward_kinematics(skeleton)

        np.testing.assert_allclose(
            positions[2],
            [0.0, 2.0, 0.0],
            atol=1e-9,
        )
        np.testing.assert_allclose(
            positions[3],
            [-1.0, 2.0, 0.0],
            atol=1e-9,
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
