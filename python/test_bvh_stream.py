import json
import unittest
from pathlib import Path

import numpy as np

from build_bvh_source_calibration import (
    quaternion_to_matrix,
    rotation_error_degrees,
)
from bvh_stream import build_manny_stream_from_bvh, build_stream_from_bvh


ROOT = Path(__file__).resolve().parents[1]
BVH_PATH = (
    ROOT
    / "data"
    / "bvh"
    / "100style"
    / "Neutral_FW_frames_530_694.bvh"
)
CALIBRATION_PATH = (
    ROOT
    / "data"
    / "calibration"
    / "100style_source_calibration.json"
)
REFERENCE_PATH = (
    ROOT
    / "artifacts"
    / "realtime"
    / "source_reference_stream.json"
)
MANNY_CALIBRATION_PATH = (
    ROOT
    / "data"
    / "calibration"
    / "100style_to_manny_calibration.json"
)


class BvhStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.stream = build_stream_from_bvh(BVH_PATH, CALIBRATION_PATH)
        cls.manny_stream = build_manny_stream_from_bvh(
            BVH_PATH,
            CALIBRATION_PATH,
            MANNY_CALIBRATION_PATH,
        )
        cls.reference = json.loads(REFERENCE_PATH.read_text(encoding="utf-8"))

    def test_stream_is_source_skeleton_not_manny(self) -> None:
        self.assertEqual(self.stream["skeleton"], "100style-bvh-source")
        self.assertEqual(self.stream["source_format"], "BVH")
        self.assertIn("MotionRoot", self.stream["bones"])
        self.assertIn("Hips", self.stream["bones"])
        self.assertNotIn("pelvis", self.stream["bones"])
        self.assertNotIn("spine_01", self.stream["bones"])

    def test_exact_clip_matches_source_mesh_coordinate_calibration(self) -> None:
        max_rotation_error = 0.0
        max_translation_error = 0.0
        for actual_frame, expected_frame in zip(
            self.stream["frames"],
            self.reference["frames"],
        ):
            for actual, expected in zip(
                actual_frame["bone_rotations"],
                expected_frame["bone_rotations"],
            ):
                max_rotation_error = max(
                    max_rotation_error,
                    rotation_error_degrees(
                        quaternion_to_matrix(actual),
                        quaternion_to_matrix(expected),
                    ),
                )
            for actual, expected in zip(
                actual_frame["bone_translations"],
                expected_frame["bone_translations"],
            ):
                max_translation_error = max(
                    max_translation_error,
                    float(np.linalg.norm(
                        np.asarray(actual) - np.asarray(expected)
                    )),
                )
        self.assertLess(max_rotation_error, 1.0)
        self.assertLess(max_translation_error, 1.0e-5)

    def test_actor_root_track_is_centimetres_and_moves_forward(self) -> None:
        final_position = self.stream["frames"][-1]["root_position"]
        self.assertGreater(np.linalg.norm(final_position[:2]), 200.0)
        self.assertLess(abs(final_position[2]), 1.0e-6)

    def test_runtime_retarget_outputs_manny_without_target_frame_input(self) -> None:
        self.assertEqual(self.manny_stream["skeleton"], "UE5_Manny")
        self.assertEqual(
            self.manny_stream["retarget_mode"],
            "calibrated-two-sided-local-rotation-map",
        )
        self.assertIn("pelvis", self.manny_stream["bones"])
        self.assertIn("foot_l", self.manny_stream["bones"])
        self.assertNotIn("Hips", self.manny_stream["bones"])
        self.assertEqual(
            len(self.manny_stream["frames"]),
            len(self.stream["frames"]),
        )
        self.assertGreater(
            np.linalg.norm(
                np.asarray(
                    self.manny_stream["frames"][-1]["root_position"]
                )[:2]
            ),
            200.0,
        )


if __name__ == "__main__":
    unittest.main()
